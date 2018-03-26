/*
 * Copyright 2017  Stacy <stacy@sks.uz>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <stdio.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <dahdi/user.h>
#include <dahdi/tonezone.h>

#include "trace.h"
#include "dcp.h"
#include "console.h"


#define ZAPDEV	"/dev/dahdi/channel"

static void hexdump(unsigned char *b, int len, bool xmt);
static void dcp_process_pkt(struct dcp_hdlc_state *st, dcp_hdlc_pkt *p);
static inline void dcp_new_iframe(struct dcp_hdlc_state *st);
static inline void dcp_set_nsnr(struct dcp_hdlc_state *st);
static int dcp_cansend(struct dcp_hdlc_state *st);
static void dcp_send_sabm(struct dcp_hdlc_state *st);
static void dcp_process_rr(struct dcp_hdlc_state *st, dcp_hdlc_pkt *p);
static void strange_frame(struct dcp_hdlc_state *st, dcp_hdlc_pkt *p);
static void dcp_ack_iframe(struct dcp_hdlc_state *st, dcp_hdlc_pkt *p);
static void dcp_reset_state(struct dcp_hdlc_state *st);
static void dcp_reset_nsnr(struct dcp_hdlc_state *st);
static int resend_iframe(struct dcp_hdlc_state *st);
static int write_packet(struct dcp_hdlc_state *st, dcp_hdlc_pkt *p);
static int write_packet_hdlc(int fd, dcp_hdlc_pkt *p);
static int write_packet_xio(struct dcp_hdlc_state *st, dcp_hdlc_pkt *p);
static int read_packet(struct dcp_hdlc_state *st, void *buf, size_t count);
static int read_packet_hdlc(int fd, void *buf, size_t count);
static int read_packet_xio(struct dcp_hdlc_state *st, void *buf, size_t count);
static long refresh_dcptime(struct dcp_hdlc_state *st);
static void send_dummy(struct dcp_hdlc_state *st);
static void report_status(struct dcp_hdlc_state *st, int seen);
static const char *packet_name(int ctl);

#define SINGLE_EVENTS_MAX	32
#define UNKNOWN_EVENT		31
static const char *event_names[SINGLE_EVENTS_MAX] = {
	"NONE", "ONHOOK", 
	"RINGOFFHOOK", "WINKFLASH",
	"ALARM", "NOALARM", "HDLC_ABORT", 
	"HDLC_OVERRUN", "BADFCS", "DIALCOMPLETE", 
	"RINGERON", "RINGEROFF", "HOOKCOMPLETE", 
	"BITSCHANGED", "PULSE_START", "TIMER_EXPIRED",
	"TIMER_PING", "POLARITY", "RINGBEGIN", 
	"ECDISABLED", "REMOVED", "NEONMWI_ACTIVE",
	"NEONMWI_INACTIVE", "TX_CED_DETECTED", "RX_CED_DETECTED",
	"TX_CNG_DETECTED", "RX_CNG_DETECTED",
	"EC_NLP_DISABLED", "EC_NLP_ENABLED", 
	"READ_OVERRUN", "WRITE_UNDERRUN",
	"UNKNOWN"
};

static char tone2ascii(int tone)
{
	static const char tonetab[16] = "0123456789*#ABCD";
	
	tone &= ~(DAHDI_EVENT_DTMFUP | DAHDI_EVENT_DTMFDOWN);
	tone -= DAHDI_TONE_DTMF_BASE;
	if ((tone < 0) || (tone > 15)) return ' ';
	return tonetab[tone];
}

static const char *decode_event(int evt)
{	
	int	multi_evt; 
	
	if ((evt >= 0) && (evt < UNKNOWN_EVENT)) {
		return event_names[evt];
	} else {
		multi_evt = evt & (DAHDI_EVENT_PULSEDIGIT | 
			DAHDI_EVENT_DTMFDOWN | DAHDI_EVENT_DTMFUP);
		switch (multi_evt) {
			case DAHDI_EVENT_PULSEDIGIT:
				return "PULSEDIGIT";
			case DAHDI_EVENT_DTMFDOWN:
				return "DTMFDOWN";
			case DAHDI_EVENT_DTMFUP:
				return "DTMFUP";
		}
	}
	return "UNKNOWN";
}


static const char *packet_name(int ctl)
{
	if (DCP_IS_IFRAME(ctl)) {
		return "I-FRAME";
	}
	ctl &= (~DCP_PFBIT);
	if (ctl == (DCP_CTL_FRAME_SABM & (~DCP_PFBIT))) {
		return "SABM   ";
	} else if (ctl == (DCP_CTL_FRAME_DM & (~DCP_PFBIT))) {
		return "DM     ";
	} else if (ctl == (DCP_CTL_FRAME_UA & (~DCP_PFBIT))) {
		return "UA     ";
	} else if ((ctl & ~DCP_NR_MASK) == (DCP_CTL_FRAME_RR & ~(DCP_PFBIT))) {
		return "RR     ";
	}
	return "UNK   ";
}


static void report_status(struct dcp_hdlc_state *st, int seen)
{
	char sline[STATUS_LINE_SIZE+1];
	const char *sname[] = { 
		"UNK ", "RST ", "W4UA", "UNK3", "UP  "};
		
	assert(st);
	memset(sline, 0, (STATUS_LINE_SIZE + 1));
	snprintf(sline, STATUS_LINE_SIZE, 
		"State=%s  ln(s):%d rn(r):%d  rn(s):%d  lcn:%0x Last=%s retr:%0ld",
		sname[st->state], st->local_ns, st->remote_nr, st->remote_ns,
		st->lcn, packet_name(seen), st->retransmit);
	console_set_status(sline);
	console_srefresh();
}

static long refresh_dcptime(struct dcp_hdlc_state *st)
{
	long long msecs = 0;
	struct timespec tp;
	int	res;

	assert(st);
	res = clock_gettime(CLOCK_MONOTONIC, &tp);
	if (res < 0) {
		dcptrace(TRC_ERR, "clock_gettime() error: %s\n", strerror(errno));
		return DCP_FAIL;
	}
	msecs = (((tp.tv_sec) - (st->dcptime.tv_sec)) * 1000) + 
		(((tp.tv_nsec) - (st->dcptime.tv_nsec)) / 1000000);

	st->dcptime.tv_sec = tp.tv_sec;
	st->dcptime.tv_nsec = tp.tv_nsec;

	return msecs;
}

void hexdump(unsigned char *b, int len, bool xmt)
{
	int	i;
	static int pktnum = 0;
	
	dcptrace(TRC_DBG, "%s packet #%0d\t", (xmt ? "XMT" : "RCV"), pktnum++);
	if (len > 0) {
		for (i = 0; i < len; i++) {
			dcptrace(TRC_DBG, "%02x ", b[i]);
		}
	} else {
		dcptrace(TRC_DBG, "Nothing to print, len=%d\n", len);
	}
	dcptrace(TRC_DBG, "\n");
}

static void dcp_ack_iframe(struct dcp_hdlc_state *st, dcp_hdlc_pkt *p)
{
	dcp_hdlc_pkt	rr;
	
	assert(st);
	assert(p);

	if (p->pktlen >=2) {
		st->retransmit = KEEPALIVE_RETRANSMIT;
		st->remote_nr = DCP_GET_NR(p->control);
		st->remote_ns = DCP_GET_NS(p->control);
		memset(&rr, 0, sizeof(rr));
		/* always response */
		rr.addr = DCP_SET_ADDR_LCN(st->lcn) | DCP_ADDR_CONSTANT;
		rr.control = DCP_CTL_FRAME_RR | DCP_SET_PFBIT(1) | DCP_SET_NR(st->remote_ns + 1);
		rr.pktlen = 2;
		write_packet(st, &rr);
	}
}

static int resend_iframe(struct dcp_hdlc_state *st)
{
	int	res;
	
	assert(st);
	if (st->xpkt.pktlen >= 2) {
		dcp_set_nsnr(st);
		st->retransmit = IFRAME_RETRANSMIT;
		res = write_packet(st, &(st->xpkt));
		if (res < 0) {
			dcptrace(TRC_ERR, "write error: %s\n", strerror(errno));
			return DCP_ERR;
		} else if (res < st->xpkt.pktlen) {
			dcptrace(TRC_WARN, "incomplete write: %d instead of %d\n", res, st->xpkt.pktlen);
			return DCP_WARN;
		}
	}
	return DCP_OK;
}


static void strange_frame(struct dcp_hdlc_state *st, dcp_hdlc_pkt *p)
{
	assert(st);
	assert(p);
	dcptrace(TRC_INFO, "Strange frame disregarded in state %0d: addr=0x%0x, ctl=0x%0x, len=%0d\n",
		st->state, p->addr, p->control, p->pktlen);
}

static inline void dcp_process_rr(struct dcp_hdlc_state *st, dcp_hdlc_pkt *p)
{
	int	expected_nr;

	assert(st);
	assert(p);
	st->remote_nr = DCP_GET_NR(p->control);
	expected_nr = (st->local_ns + 1) & NSNR_MASK;
	if (st->remote_nr != expected_nr) { 
		resend_iframe(st);
	} else {
		st->retransmit = KEEPALIVE_RETRANSMIT;
	}
}

static int write_packet(struct dcp_hdlc_state *st, dcp_hdlc_pkt *p)
{
	int	res = DCP_FAIL;
	
	assert(st);
	switch (st->transport_mode) {
		case DCP_TRANSPORT_HDLC:
			res = write_packet_hdlc(st->fd, p);
			break;
		case DCP_TRANSPORT_XIO:
			res = write_packet_xio(st, p);
			break;
		default:
			dcptrace(TRC_ERR, "Unknown transport %d\n", st->transport_mode);
			
	}
	return res;
}

static int write_packet_hdlc(int fd, dcp_hdlc_pkt *p)
{
	int	res, x;

	assert(p);
	if (dcp_cantrace(TRC_DBG)) hexdump(&p->addr, p->pktlen + 2, true);
	for (;;) {
		res = write(fd, &p->addr, p->pktlen + 2);
		if ((res < 0) && (errno == ELAST)) {
			res = ioctl(fd, DAHDI_GETEVENT, &x);
			if (res < 0) break;
			dcptrace(TRC_INFO, "Event: 0x%0.8x  - %s, \tTone: %c \n", x, decode_event(x),
				tone2ascii(x));
		} else break;
	}
	return res;
}


static int write_packet_xio(struct dcp_hdlc_state *st, dcp_hdlc_pkt *p)
{
	int	res;

	assert(st);
	assert(p);
	if (dcp_cantrace(TRC_DBG)) hexdump(&p->addr, p->pktlen, true);
	res = (st->wrt)(st->transport_ctx, st->ifi, &p->addr, p->pktlen);
	return res;
}


static void dcp_reset_nsnr(struct dcp_hdlc_state *st)
{
	assert(st);
	st->remote_nr = 0;
	st->remote_ns = 0;
	st->local_ns = 7;
}

static void dcp_reset_state(struct dcp_hdlc_state *st)
{
	assert(st);
	
	st->state = DCP_STATE_RESET;
	dcp_reset_nsnr(st);
}

static void dcp_send_sabm(struct dcp_hdlc_state *st)
{
	dcp_hdlc_pkt p;
	int res;

	assert(st);
	dcp_reset_nsnr(st);
	memset(&p, 0, sizeof(p));
	p.addr = DCP_SET_ADDR_LCN(st->lcn) | DCP_SET_CR(1) | DCP_ADDR_CONSTANT;
	p.control = DCP_CTL_FRAME_SABM;
	p.pktlen = 2;
	res = write_packet(st, &p);
	st->state = DCP_STATE_EXPECT_UA;
	if (res < 0) {
		dcptrace(TRC_ERR, "cannot send SABM: %s\n", strerror(errno));
		dcp_reset_state(st);
	} else if (res < p.pktlen) {
		dcptrace(TRC_ERR, "incomplete write: %d bytes instead of %d\n",
			res, p.pktlen);
		dcp_reset_state(st);
	}
	return;
}

static inline void dcp_new_iframe(struct dcp_hdlc_state *st)
{
	assert(st);
	st->local_ns = (st->local_ns + 1) & NSNR_MASK;
	st->xpkt.control = DCP_SET_PFBIT(1);
}

static inline void dcp_set_nsnr(struct dcp_hdlc_state *st)
{
	uint8_t	nsnr;

	assert(st);
	nsnr = (DCP_SET_NS(st->local_ns) | DCP_SET_NR(st->remote_ns + 1)) & 0xff;
	st->xpkt.control &= ~(DCP_NS_MASK | DCP_NR_MASK);
	st->xpkt.control |= nsnr;
}

static int dcp_cansend(struct dcp_hdlc_state *st)
{
	__label__ out;
	int	nr;
	int	res = DCP_ERR;

	assert(st);
	if (st->state != DCP_STATE_LINK_UP) {
		dcptrace(TRC_ERR, "cannot send - link is down\n");
		goto out;
	}
	nr = (st->local_ns + 1) & NSNR_MASK;
	if (nr == st->remote_nr) {
		res = DCP_OK;
	} else {
		res = DCP_BUSY;
	}
out:
	return res;
}

static void dcp_process_pkt(struct dcp_hdlc_state *st, dcp_hdlc_pkt *p)
{
	int	ftype;

	assert(st);
	assert(p);
	if (p->pktlen < 2) {
		dcptrace(TRC_WARN, "Packet too short - %0d byte(s)\n", p->pktlen);
		strange_frame(st, p);
		return;
	}
	dcptrace(TRC_DBG, "State=%d\n", st->state);
	switch (st->state) {
		case DCP_STATE_RESET:
			if (p->control == DCP_CTL_FRAME_DM) {
				dcptrace(TRC_DBG, "DM seen\n");
				dcp_send_sabm(st);
			} else {
				strange_frame(st, p);
			}
			break;
		case DCP_STATE_EXPECT_UA:
			if (p->control == DCP_CTL_FRAME_UA) {
				dcptrace(TRC_DBG, "UA seen\n");
				st->state = DCP_STATE_LINK_UP;
			} else if (p->control == DCP_CTL_FRAME_DM) {
				dcptrace(TRC_DBG, "SABM resend in EXPECT_UA\n");
				dcp_send_sabm(st);
			} else {
				strange_frame(st, p);
			}
			break;
		case DCP_STATE_LINK_UP:
			ftype = p->control;
			if (!DCP_IS_IFRAME(ftype)) {
				if ((ftype & 0xf0) == DCP_CTL_FRAME_RR) {
					dcptrace(TRC_DBG, "RR seen\n");
					dcp_process_rr(st, p);
				} else if (ftype == DCP_CTL_FRAME_DM) {
					dcptrace(TRC_DBG, "DM seen - link restart\n");
					dcp_send_sabm(st);
					break;
				} else {
					strange_frame(st, p);
				}
			} else {
				dcp_ack_iframe(st, p);
			}
			break;
		default:
			dcptrace(TRC_ERR, "Invalid state: %d\n", st->state);
	}
}

int dcp_init_hdlc(struct dcp_hdlc_state *st, int dchan, int lcn)
{
	__label__ out;
	__label__ out_with_fd;
	int	fd;
	int	res = DCP_FAIL;
	int	bs;
	struct	dahdi_params p;

	assert(st);
	memset(st, 0, sizeof(struct dcp_hdlc_state));
	if (dchan < 1) {
		dcptrace(TRC_ERR, "Invalid channel number: %d\n", dchan);
	}
	fd = open(ZAPDEV, O_RDWR);
	if (fd < 0) {
		dcptrace(TRC_ERR, "Unable to open device %s : %s\n", ZAPDEV, 
			strerror(errno));
		goto out;
	}	
	if (ioctl(fd, DAHDI_SPECIFY, &dchan)) {
		int tmp = errno;
		dcptrace(TRC_ERR, "Unable to specify dchan #%d: %s\n", dchan, strerror(tmp));
		goto out_with_fd;
	}
	bs = HDLC_DATA_BUFFER;
	if (ioctl(fd, DAHDI_SET_BLOCKSIZE, &bs) == -1) {
		dcptrace(TRC_WARN, "Unable to set blocksize %d: %s\n", bs, strerror(errno));
	}
	if (ioctl(fd, DAHDI_GET_PARAMS, &p)) {
		dcptrace(TRC_ERR, "Unable to get dchan parameters: %s\n", strerror(errno));
		goto out_with_fd;
	}
	if ((p.sigtype != DAHDI_SIG_HARDHDLC) && (p.sigtype != DAHDI_SIG_HDLCFCS)) {
		dcptrace(TRC_ERR, "dchan is in %d signalling, not FCS HDLC or HW HDLC mode\n", p.sigtype);
		goto out_with_fd;
	}
	st->fd = fd;
	st->lcn = lcn;
	st->transport_ctx = NULL;
	st->transport_mode = DCP_TRANSPORT_HDLC;
	st->ifi = dchan;
	dcp_reset_state(st);
	res = DCP_OK;
	goto out;
out_with_fd:
	close(fd);
out:
	return	res;
}

int dcp_init_xio(struct dcp_hdlc_state *st, int dchan, int lcn, void *trn_ctx, xioreader rdr, xiowriter wrt)
{
	int	res = DCP_FAIL;

	assert(st);
	assert(rdr);
	assert(wrt);
	memset(st, 0, sizeof(struct dcp_hdlc_state));
	if (dchan < 1) {
		dcptrace(TRC_ERR, "Invalid channel number: %d\n", dchan);
	}
	st->fd = -1;
	st->lcn = lcn;
	st->transport_ctx = trn_ctx;
	st->transport_mode = DCP_TRANSPORT_XIO;
	st->ifi = dchan;
	st->rdr = rdr;
	st->wrt = wrt;
	dcp_reset_state(st);
	res = DCP_OK;
	return	res;
}


void dcp_close(struct dcp_hdlc_state *st)
{
	assert(st);

	switch (st->transport_mode) {
		case DCP_TRANSPORT_HDLC:
			close(st->fd);
			break;
		case DCP_TRANSPORT_XIO:
			break;
		default:
			dcptrace(TRC_ERR, "Unknown transport %d\n", st->transport_mode);
	}
}

static int read_packet(struct dcp_hdlc_state *st, void *buf, size_t count)
{
	int res = DCP_FAIL;
	
	assert(st);
	assert(buf);
	assert(count > 0);
	switch (st->transport_mode) {
		case DCP_TRANSPORT_HDLC:
			res = read_packet_hdlc(st->fd, buf, count);
			break;
		case DCP_TRANSPORT_XIO:
			res = read_packet_xio(st, buf, count);
			break;
		default:
			dcptrace(TRC_FAIL, "Unknown transport %d\n", st->transport_mode);
	}
	return res;
}


static int read_packet_hdlc(int fd, void *buf, size_t count)
{
	int	len;
	
	assert(buf);
	assert(count > 0);
	len = read(fd, buf, count);
	if (len < 0) {
		if (errno == ELAST) {
			int x;
			
			if (ioctl(fd, DAHDI_GETEVENT, &x) < 0) {
				dcptrace(TRC_ERR, "Unable to get event: %s\n", strerror(errno));
				return DCP_ERR;
			}
			dcptrace(TRC_INFO, "Event: 0x%0.8x  - %s, \tTone: %c \n", x, decode_event(x),
				 tone2ascii(x));
			len = 0;
		} else {
			dcptrace(TRC_WARN, "read error: %s\n", strerror(errno));
			return DCP_ERR;
		}
	}
	if (dcp_cantrace(TRC_DBG)) hexdump(buf, len, false);
	len -= 2;
	if (len < 0) len = 0;
	return len;
}


static int read_packet_xio(struct dcp_hdlc_state *st, void *buf, size_t count)
{
	int	len;
	
	assert(st);
	assert(buf);
	assert(count > 0);
	len = (st->rdr)(st->transport_ctx, st->ifi, buf, count);
	if (len < 0) return DCP_ERR;
	if (dcp_cantrace(TRC_DBG)) hexdump(buf, len, false);
	return len;
}

/* receive dcp packet. Don't copy to caller if data is NULL 	*
 * returns received packet size if ok, zero if it was		*
 * RR or other unnumbered frame, negative error code otherwise	*
 */
int dcp_rcv(struct dcp_hdlc_state *st, bool *cmdflag, unsigned char *data)
{
	ssize_t	len;
	dcp_hdlc_pkt	rpkt;
	int	lcn;
	
	assert(st);
	refresh_dcptime(st);
	memset(&rpkt, 0, sizeof(rpkt));
	len = read_packet(st, &rpkt.addr, HDLC_DATA_BUFFER);
	if (len < 0) return len;
	rpkt.pktlen = len;
	if (len == 0) return DCP_OK;
	dcp_process_pkt(st, &rpkt);
	report_status(st, rpkt.control);
	if (DCP_IS_IFRAME(rpkt.control)) {
		lcn = DCP_GET_ADDR_LCN(rpkt.addr);
		if (!(rpkt.addr & DCP_ADDR_CONSTANT)) {
			dcptrace(TRC_WARN, "Strange addr: 0x%0x\n", rpkt.addr);
		}
		if (lcn == st->lcn) {
			if (cmdflag) *cmdflag = DCP_GET_CR(rpkt.addr);
			if (data && len > 2) {
				memcpy(data, &rpkt.data, len - 2);
				return len - 2;
			} else if (len <=2) {
				dcptrace(TRC_INFO, "zero-data I-frame\n");
			}
		} else {
			dcptrace(TRC_INFO, "Packet for wrong LCN:%0d, ours is %0d\n", lcn, st->lcn);
		}
	}
	return DCP_OK;
}

int dcp_xmt(struct dcp_hdlc_state *st, bool cmdflag, unsigned char *data, size_t datalen)
{
	int	res;

	assert(st);
	assert(data);
	if (datalen <= 0) return DCP_OK;
	if (datalen > HDLC_DATA_LEN) {
		dcptrace(TRC_WARN, "too long data to transmit- %0d bytes, truncated to %0d bytes\n",
			datalen, HDLC_DATA_LEN);
		datalen = HDLC_DATA_LEN;
	}
	refresh_dcptime(st);
	res = dcp_cansend(st);
	if (res != DCP_OK) return res;
	if (datalen > 0) memcpy(&((st->xpkt).data), data, datalen);
	st->xpkt.pktlen = datalen + 2;
	st->xpkt.addr = (DCP_SET_ADDR_LCN(st->lcn) | DCP_SET_CR(cmdflag) | DCP_ADDR_CONSTANT);
	dcp_new_iframe(st);
	res = resend_iframe(st);
	return res;
}

static void send_dummy(struct dcp_hdlc_state *st)
{
	int	res;
	
	assert(st);
	res = dcp_cansend(st);
	if (res != DCP_OK) return;
	memset(&((st->xpkt).data), 0, HDLC_DATA_BUFFER);
	st->xpkt.pktlen = 2;
	st->xpkt.addr = (DCP_SET_ADDR_LCN(st->lcn) | DCP_SET_CR(1) | DCP_ADDR_CONSTANT);
	dcp_new_iframe(st);
	resend_iframe(st);
}

int dcp_run(struct dcp_hdlc_state *st)
{
	long	msecs;
	int	expected_nr;
	
	assert(st);
	msecs = refresh_dcptime(st);
	if (st->state != DCP_STATE_LINK_UP) return DCP_OK;
	if (msecs < 0) return DCP_FAIL;
	st->retransmit -= msecs;
	if (st->retransmit <=0) {
		expected_nr = (st->local_ns + 1) & NSNR_MASK;
		if (st->remote_nr != expected_nr) { 
			resend_iframe(st);
		} else {
			send_dummy(st);
		}
	}
	return DCP_OK;
}

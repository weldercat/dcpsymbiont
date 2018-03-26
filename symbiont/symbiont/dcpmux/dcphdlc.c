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

#define _GNU_SOURCE	1

#include <stdio.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>

//#define	DEBUG_ME_HARDER	1

#include <symbiont/symerror.h>
#define NOFAIL_LOCK_UNNEEDED 	1
#include <symbiont/nofail_wrappers.h>
#include <symbiont/dcphdlc.h>
#include "trace.h"


static void report_status(struct dcp_hdlc_state *st, int seen);
#if 0
static const char *packet_name(int ctl);
#endif

#ifdef DEBUG_ME_HARDER
static void hexdump(unsigned char *b, int len, bool xmt);
#endif

static void dcp_ack_iframe(struct dcp_hdlc_state *st, dcp_hdlc_pkt *p);
static int resend_iframe(struct dcp_hdlc_state *st);
static void strange_frame(struct dcp_hdlc_state *st, dcp_hdlc_pkt *p);
static int write_packet(struct dcp_hdlc_state *st, dcp_hdlc_pkt *p);
static void dcp_reset_nsnr(struct dcp_hdlc_state *st);
static void dcp_reset_state(struct dcp_hdlc_state *st);
static void dcp_send_sabm(struct dcp_hdlc_state *st);
#if 0
static void dcp_send_dm(struct dcp_hdlc_state *st);
#endif
static inline void dcp_new_iframe(struct dcp_hdlc_state *st);
static inline void dcp_set_nsnr(struct dcp_hdlc_state *st);
static int dcp_cansend(struct dcp_hdlc_state *st);
static void dcp_process_pkt(struct dcp_hdlc_state *st, dcp_hdlc_pkt *p);
static void send_dummy(struct dcp_hdlc_state *st);
static void set_timer(struct dcp_hdlc_state *st, int msecs);
static bool timer_armed(struct dcp_hdlc_state *st);
static void dcp_timeout_hndlr(union sigval arg);



#if 0
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
#endif

#ifdef ENABLE_STATUS_REPORT
static void report_status(struct dcp_hdlc_state *st, int seen)
{
	char sline[STATUS_LINE_SIZE+1];
	const char *sname[] = { 
		"UNK ", "RST ", "W4UA", "UNK3", "UP  "};
		
	assert(st);
	memset(sline, 0, (STATUS_LINE_SIZE + 1));
	snprintf(sline, STATUS_LINE_SIZE, 
		"State=%s  ln(s):%d rn(r):%d  rn(s):%d  lcn:%0x Last=%s",
		sname[st->state], st->local_ns, st->remote_nr, st->remote_ns,
		st->lcn, packet_name(seen));
	dcptrace(TRC_DBG2, "%s\n", sline);
}
#else 
static void report_status(struct dcp_hdlc_state *st, int seen)
{
	return;
}
#endif /* ENABLE_STATUS_REPORT */

#ifdef	DEBUG_ME_HARDER
#define DUMP_LINE	16
#define MAX_DUMP_LINE	79
static void hexdump(unsigned char *b, int len, bool xmt)
{
	static int pktnum = 0;
	int	i, j;
	unsigned char c;
	char	outbuf[MAX_DUMP_LINE + 1];
	int	pos;
	int	res;
	int	space;

	dcptrace(TRC_DBG, "%s packet #%0d\t", (xmt ? "XMT" : "RCV"), pktnum++);
	if (len > 0) {
		for (i = 0; i < len; i += DUMP_LINE) {
			memset(outbuf, 0, MAX_DUMP_LINE + 1);
			space = MAX_DUMP_LINE;
			pos = 0;
			res = snprintf(&outbuf[pos], space, "%04x:   ", i);
			if (res > space) res = space;
			pos += res;
			space -= res;
			for (j = 0; ((j < DUMP_LINE) && (i+j < len)); j++) {
				res = snprintf(&outbuf[pos], space, "%02x ", b[i+j]);
				if (res > space) res = space;
				pos += res;
				space -= res;
				if ((j == DUMP_LINE / 2 - 1) && (space > 0)) {
					outbuf[pos] = ' ';
					pos++;
					space--;
				}
			}
			strncpy(&outbuf[pos], "  |", space);
			space -= 3;
			for (j = 0; ((j < DUMP_LINE) && (i+j < len) && (space > 0)); j++) {
				c = b[i+j];
				if (c < 0x20) c = '.';
				else if (c > 0x7e) c = '.';
				outbuf[pos] = c;
				pos++;
				space--;
			}
			if (space > 0) strncpy(&outbuf[pos],"|\n", space);
			dcptrace(TRC_DBG, "%s", outbuf);
			
		}
	} else {
		dcptrace(TRC_DBG, "Nothing to print, len=%d\n", len);
	}
	dcptrace(TRC_DBG, "\n");
}
#endif


static void dcp_ack_iframe(struct dcp_hdlc_state *st, dcp_hdlc_pkt *p)
{
	dcp_hdlc_pkt	rr;
	
	assert(st);
	assert(p);

	if (p->pktlen >=2) {
		set_timer(st, KEEPALIVE_RETRANSMIT);
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
		set_timer(st, IFRAME_RETRANSMIT);
		res = write_packet(st, &(st->xpkt));
		if (res < 0) {
			SYMERROR("write error: %s\n", strerror(errno));
			return DCP_ERR;
		} else if (res < st->xpkt.pktlen) {
			SYMWARNING("incomplete write: %d instead of %d\n", res, st->xpkt.pktlen);
			return DCP_WARN;
		}
		st->sent_iframes++;
	}
	return DCP_OK;
}


static void strange_frame(struct dcp_hdlc_state *st, dcp_hdlc_pkt *p)
{
	assert(st);
	assert(p);
	SYMINFO("Strange frame disregarded in state %0d: addr=0x%0x, ctl=0x%0x, len=%0d\n",
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
		st->iretr_cnt_total++;
		st->iretr_cnt++;
		if (st->iretr_cnt > st->max_iretr) {
			SYMDEBUG("excessive retransmits (%d), link lost\n", st->iretr_cnt);
			dcp_reset_state(st);
		} else resend_iframe(st);
	} else {
		/* %%% signal available condition */
		st->iretr_cnt = 0;
		st->keepalive_cnt = 0;
		set_timer(st, KEEPALIVE_RETRANSMIT);
	}
}

static int write_packet(struct dcp_hdlc_state *st, dcp_hdlc_pkt *p)
{
	int	res;

	assert(st);
	assert(p);
#ifdef	DEBUG_ME_HARDER
	if (dcp_cantrace(TRC_DBG)) hexdump(&p->addr, p->pktlen, true);
#endif
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
	st->link_resets++;
	st->iretr_cnt = 0;
	st->keepalive_cnt = 0;
}


void dcp_enable_link(struct dcp_hdlc_state *st)
{
	assert(st);
	mutex_lock(&st->dcp_mutex);
	dcp_send_sabm(st);
	mutex_unlock(&st->dcp_mutex);
}


void dcp_disable_link(struct dcp_hdlc_state *st)
{
	assert(st);
	mutex_lock(&st->dcp_mutex);
	st->state = DCP_STATE_DISABLE;
	set_timer(st, 0);
	mutex_unlock(&st->dcp_mutex);
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
	st->iretr_cnt = 0;
	st->keepalive_cnt = 0;
	if (res < 0) {
		SYMERROR("cannot send SABM: %s\n", strerror(errno));
		dcp_reset_state(st);
	} else if (res < p.pktlen) {
		SYMERROR("incomplete write: %d bytes instead of %d\n",
			res, p.pktlen);
		dcp_reset_state(st);
	}
	return;
}

#if 0
static void dcp_send_dm(struct dcp_hdlc_state *st)
{
	dcp_hdlc_pkt p;
	int res;

	assert(st);
	dcp_reset_nsnr(st);
	dcp_reset_state(st);
	memset(&p, 0, sizeof(p));
	p.addr = DCP_SET_ADDR_LCN(st->lcn) | DCP_SET_CR(1) | DCP_ADDR_CONSTANT;
	p.control = DCP_CTL_FRAME_DM;
	p.pktlen = 2;
	res = write_packet(st, &p);
	if (res < 0) {
		SYMERROR("cannot send DM: %s\n", strerror(errno));
	} else if (res < p.pktlen) {
		SYMERROR("incomplete write: %d bytes instead of %d\n",
			res, p.pktlen);
	}
	return;
}
#endif

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
	int	nr;
	int	res = DCP_ERR;

	assert(st);
	if (st->state != DCP_STATE_LINK_UP) {
		SYMERROR("cannot send - link is down\n");
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
	int	res;

	assert(st);
	assert(p);
#ifdef	DEBUG_ME_HARDER
	if (dcp_cantrace(TRC_DBG)) hexdump(&p->addr, p->pktlen, false);
#endif
	if (p->pktlen < 2) {
		SYMWARNING("Packet too short - %0d byte(s)\n", p->pktlen);
		strange_frame(st, p);
		return;
	}
	SYMDEBUGHARD("State=%d\n", st->state);
	switch (st->state) {
		case DCP_STATE_DISABLE:
			SYMDEBUG("Link is administratively down, packet discarded\n");
			st->pkt_drops++;
			break;
		case DCP_STATE_RESET:
			if (p->control == DCP_CTL_FRAME_DM) {
				SYMDEBUG("DM seen\n");
				dcp_send_sabm(st);
			} else {
				strange_frame(st, p);
			}
			break;
		case DCP_STATE_EXPECT_UA:
			if (p->control == DCP_CTL_FRAME_UA) {
				SYMDEBUG("UA seen\n");
				st->state = DCP_STATE_LINK_UP;
				set_timer(st, KEEPALIVE_RETRANSMIT);
			} else if (p->control == DCP_CTL_FRAME_DM) {
				SYMDEBUG("SABM resend in EXPECT_UA\n");
				dcp_send_sabm(st);
			} else {
				strange_frame(st, p);
			}
			break;
		case DCP_STATE_LINK_UP:
			ftype = p->control;
			if (!DCP_IS_IFRAME(ftype)) {
				if ((ftype & 0xf0) == DCP_CTL_FRAME_RR) {
					SYMDEBUGHARD("RR seen\n");
					dcp_process_rr(st, p);
				} else if (ftype == DCP_CTL_FRAME_DM) {
					SYMDEBUG("DM seen - link restart\n");
					dcp_send_sabm(st);
					break;
				} else {
					strange_frame(st, p);
				}
			} else {
				dcp_ack_iframe(st, p);
			}
			if (dcp_cansend(st) == DCP_OK) {
				res = pthread_cond_broadcast(&st->cansend_cond);
				if (res) SYMFATAL("cannot broadcast cansend condition: %s\n", STRERROR_R(res));
			}
			break;
		default:
			SYMERROR("Invalid state: %d\n", st->state);
	}
}

int dcp_init(struct dcp_hdlc_state *st, int ifi, int lcn, void *trn_ctx, xiowriter wrt)
{
	int	res = DCP_FAIL;
	struct sigevent sevp;

	assert(st);
	assert(wrt);
	memset(st, 0, sizeof(struct dcp_hdlc_state));
	if (ifi < 1) {
		SYMERROR("Invalid interface id: %d\n", ifi);
	}
	st->max_iretr = MAX_IRETR;
	st->max_keepalives = MAX_KEEPALIVES;
	st->lcn = lcn;
	st->transport_ctx = trn_ctx;
	st->ifi = ifi;
	st->wrt = wrt;
	dcp_reset_state(st);
	st->state = DCP_STATE_DISABLE;
	memset(&sevp, 0, sizeof(struct sigevent));
	sevp.sigev_notify = SIGEV_THREAD;
	sevp.sigev_value.sival_ptr = st;
	sevp.sigev_notify_function = dcp_timeout_hndlr;
	res = timer_create(CLOCK_MONOTONIC, &sevp, &st->retransmit);
	if (res) {
		SYMERROR("cannot create retransmit timer: %s\n", STRERROR_R(errno));
		return DCP_FAIL;
	}
	res = pthread_mutex_init(&st->dcp_mutex, NULL);
	if (res) {
		SYMERROR("cannot init mutex: %s\n", STRERROR_R(errno));
		res = DCP_FAIL;
	} else res = DCP_OK;
	res = pthread_cond_init(&st->cansend_cond, NULL);
	if (res) {
		SYMERROR("cannot init cond variable: %s\n", STRERROR_R(errno));
		res = DCP_FAIL;
	} else res = DCP_OK;
	return	res;
}

void dcp_close(struct dcp_hdlc_state *st)
{
	int	res;
	
	assert(st);
	res = timer_delete(st->retransmit);
	assert(res == 0);
	res = pthread_mutex_destroy(&st->dcp_mutex);
	assert(res == 0);
}


int dcp_handle_data(struct dcp_hdlc_state *st, unsigned char *pkt, size_t pktlen, bool *cmdflag, unsigned char *data)
{
	ssize_t	len;
	dcp_hdlc_pkt	rpkt;
	int	lcn, res;
	
	assert(st);
	assert(pkt);
	
	if (pktlen == 0) return 0;
	mutex_lock(&st->dcp_mutex);
	res = DCP_FAIL;
	if (st->state == DCP_STATE_DISABLE) {
		res = 0;
		goto out;
	}
	memset(&rpkt, 0, sizeof(rpkt));
	if (pktlen > HDLC_DATA_BUFFER) pktlen = HDLC_DATA_BUFFER;
	memcpy(&rpkt.addr, pkt, pktlen);
	len = pktlen;

	rpkt.pktlen = len;
	dcp_process_pkt(st, &rpkt);
	report_status(st, rpkt.control);
	if (DCP_IS_IFRAME(rpkt.control)) {
		lcn = DCP_GET_ADDR_LCN(rpkt.addr);
		if (!(rpkt.addr & DCP_ADDR_CONSTANT)) {
			SYMWARNING("Strange addr: 0x%0x\n", rpkt.addr);
		}
		if (lcn == st->lcn) {
			st->rcvd_iframes++;
			if (cmdflag) *cmdflag = DCP_GET_CR(rpkt.addr);
			if (data && len > 2) {
				memcpy(data, &rpkt.data, len - 2);
				res = len - 2;
				goto out;
			} else if (len <=2) {
				SYMINFO("zero-data I-frame\n");
			}
		} else {
			SYMINFO("Packet for wrong LCN:%0d, ours is %0d\n", lcn, st->lcn);
		}
	}
	res = 0;
out:	
	mutex_unlock(&st->dcp_mutex);
	return res;
}

int dcp_xmt(struct dcp_hdlc_state *st, bool cmdflag, unsigned char *data, size_t datalen)
{
	int	res;
	struct timespec timeout;
	struct timespec actual;
	long long	tmpnsec;

	assert(st);
	assert(data);
	if (datalen <= 0) return DCP_OK;
	mutex_lock(&st->dcp_mutex);
	if (datalen > HDLC_DATA_LEN) {
		SYMWARNING("too long data to transmit- %0d bytes, truncated to %0d bytes\n",
			datalen, HDLC_DATA_LEN);
		datalen = HDLC_DATA_LEN;
	}
	for (;;) {	/* endless loop is needed because cond_timedwait() can wake-up spuriously */
		res = dcp_cansend(st);
		if (res == DCP_BUSY) {
			res = clock_gettime(CLOCK_REALTIME, &timeout);
			if (res) SYMFATAL("can't get current time: %s\n", STRERROR_R(errno));	
			SYMDEBUGHARD("Waiting for dcp link cansend() - start at: %lld sec, %lld nsec\n", 
				(long long)(timeout.tv_sec), (long long)(timeout.tv_nsec));
			tmpnsec = (CANSEND_USLEEP * 1000) * CANSEND_BUSY_RETRIES;
			if ((tmpnsec + timeout.tv_nsec) >= 1000000000LL) {
				tmpnsec -= 1000000000LL;
				timeout.tv_sec += 1;
			}
			timeout.tv_nsec += tmpnsec;
			SYMDEBUGHARD("Waiting for dcp link cansend() - timeout at: %lld sec, %lld nsec\n", 
				(long long)(timeout.tv_sec), (long long)(timeout.tv_nsec));
			res = pthread_cond_timedwait(&st->cansend_cond, &st->dcp_mutex, &timeout);
			if (res) {
				if (res == ETIMEDOUT) {
					res = clock_gettime(CLOCK_REALTIME, &actual);
					if (!res) {
						SYMERROR("dcp link is busy for too long (%lld secs, %lld nsecs)\n",
							(long long)(actual.tv_sec - timeout.tv_sec), (long long)(actual.tv_nsec - timeout.tv_nsec));
					} else 	SYMERROR("dcp link is busy for too long - and failed to get actual timeout\n");
					res = DCP_BUSY;
				} else {
					SYMERROR("pthread_cond_timedwait() error %s\n", STRERROR_R(res));
					res = DCP_FAIL;
				}
				break;
			};
		} else break;
	}
	if (res != DCP_OK) goto out;
#ifdef DEBUG_ME_HARDER
	res = clock_gettime(CLOCK_REALTIME, &actual);
	if (!res) {
		SYMDEBUGHARD("dcp link available at %lld secs, %lld nsecs before timeout\n",
				(long long)(timeout.tv_sec - actual.tv_sec), 
				(long long)(timeout.tv_nsec - actual.tv_nsec));
	} else 	SYMDEBUGHARD("dcp link is available - and failed to get actual waiting time\n");
#endif
	if (datalen > 0) memcpy(&((st->xpkt).data), data, datalen);
	st->xpkt.pktlen = datalen + 2;
	st->xpkt.addr = (DCP_SET_ADDR_LCN(st->lcn) | DCP_SET_CR(cmdflag) | DCP_ADDR_CONSTANT);
	dcp_new_iframe(st);
	res = resend_iframe(st);
out:
	mutex_unlock(&st->dcp_mutex);
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


static void set_timer(struct dcp_hdlc_state *st, int msecs)
{
	struct itimerspec tval;
	int	res;
	
	assert(st);
	assert(msecs >= 0);
	memset(&tval, 0, sizeof(struct itimerspec));
	tval.it_value.tv_sec = (msecs / 1000);
	tval.it_value.tv_nsec = (msecs % 1000) * 1000000;
	res = timer_settime(st->retransmit, 0, &tval, NULL);
	if (res) SYMFATAL("cannot set timer:%s\n", STRERROR_R(errno));
}


static bool timer_armed(struct dcp_hdlc_state *st)
{
	struct itimerspec tval;
	int	res;
	bool	armed = false;
	
	assert(st);
	memset(&tval, 0, sizeof(struct itimerspec));
	res = timer_gettime(st->retransmit, &tval);
	if (res) SYMFATAL("cannot get timer value:%s\n", STRERROR_R(errno));
	assert(tval.it_interval.tv_sec == 0);
	assert(tval.it_interval.tv_nsec == 0);
	armed = ((tval.it_value.tv_sec != 0) || (tval.it_value.tv_nsec != 0));
	return armed;
}

static void dcp_timeout_hndlr(union sigval arg)
{
	struct dcp_hdlc_state	*st;
	int	expected_nr;
	
	st = (struct dcp_hdlc_state *)(arg.sival_ptr);
	assert(st);
	mutex_lock(&st->dcp_mutex);
	if (timer_armed(st)) {
		SYMDEBUGHARD("timer rearmed while waiting\n");
		goto out;
	}

	if (st->state != DCP_STATE_LINK_UP) goto out;
	
	expected_nr = (st->local_ns + 1) & NSNR_MASK;
	if (st->remote_nr != expected_nr) { 
		st->iretr_cnt++;
		if (st->iretr_cnt > st->max_iretr) {
			SYMDEBUG("excessive retransmits (%d), link lost\n", st->iretr_cnt);
			dcp_reset_state(st);
		} else resend_iframe(st);
	} else {
		st->keepalive_cnt++;
		if (st->keepalive_cnt > st->max_keepalives) {
			SYMDEBUG("too many unanswered keepalives (%d), link lost\n", 
					st->keepalive_cnt);
			dcp_reset_state(st);
		} else send_dummy(st);
	}
out:
	mutex_unlock(&st->dcp_mutex);
}


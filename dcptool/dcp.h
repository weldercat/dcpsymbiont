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

#ifndef DCP_HDR_LOADED__
#define DCP_HDR_LOADED__

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#define DCP_ADDR_LCN_SHIFT	4
#define DCP_ADDR_LCN_MASK	(0x0f << DCP_ADDR_LCN_SHIFT)

/* LCN 0 is serving bearer channel 0,
 * LCN 1 is serving bearer channel 1
 */
 
#define DCP_LCN_CHAN0		0x0e
#define DCP_LCN_CHAN1		0x0f

#define DCP_ADDR_CR_SHIFT	1
#define DCP_ADDR_CR_MASK	(1 << DCP_ADDR_CR_SHIFT)

#define DCP_ADDR_CONSTANT	1


#define DCP_ADDR_COMMAND0	0xe3
#define DCP_ADDR_RESPONSE0	0xe1
#define DCP_ADDR_COMMAND1	0xf3
#define DCP_ADDR_RESPONSE1	0xf1

#define DCP_GET_ADDR_LCN(addr)	((addr & DCP_ADDR_LCN_MASK) >> DCP_ADDR_LCN_SHIFT)
#define DCP_SET_ADDR_LCN(lcn)	(((lcn)  << DCP_ADDR_LCN_SHIFT) & DCP_ADDR_LCN_MASK)
#define DCP_GET_CR(addr)	(((addr) & DCP_ADDR_CR_MASK) >> DCP_ADDR_CR_SHIFT)
#define DCP_SET_CR(cr)		(((cr)  << DCP_ADDR_CR_SHIFT) & DCP_ADDR_CR_MASK)


/* CTL frame - S or U frame, data - I frame */

#define DCP_CTLFRAME_FLAG_SHIFT	7
#define DCP_CTLFRAME_FLAG_MASK	(1 << DCP_CTLFRAME_FLAG_SHIFT)

#define DCP_IS_IFRAME(ctl)	(((ctl) & DCP_CTLFRAME_FLAG_MASK) == 0)

/* P bit is always 1 when terminal sends SABM */
#define DCP_CTL_FRAME_SABM	0xfc

/* P/F is always 1 for switch-originated RR 
 * P/F = P/F of I message for terminal originated RR 
 */
#define DCP_CTL_FRAME_RR	0x80

/* F bit is always 0 in DM frame */
#define DCP_CTL_FRAME_DM	0xf0

/* for terminal P/F = P/F of SABM message, it is always set in SABM *
 * so it is always 1 in UA too.
 */
#define DCP_CTL_FRAME_UA	0xce

#define DCP_PFBIT_SHIFT		3
#define DCP_PFBIT_MASK		(1 << DCP_PFBIT_SHIFT)
#define DCP_PFBIT	DCP_PFBIT_MASK

#define DCP_GET_PFBIT(ctl)	(((ctl) & DCP_PFBIT_MASK) >> DCP_PFBIT_SHIFT)
#define DCP_SET_PFBIT(pf)	(((pf) << DCP_PFBIT_SHIFT) & DCP_PFBIT_MASK)

#define NSNR_MASK		0x7

#define DCP_NR_SHIFT		0
#define DCP_NR_MASK		(NSNR_MASK << DCP_NR_SHIFT)

#define DCP_NS_SHIFT		4
#define DCP_NS_MASK		(NSNR_MASK << DCP_NS_SHIFT)

#define DCP_GET_NR(ctl)		(((ctl) & DCP_NR_MASK) >> DCP_NR_SHIFT)
#define DCP_SET_NR(nr)		(((nr) << DCP_NR_SHIFT) & DCP_NR_MASK)
#define DCP_GET_NS(ctl)		(((ctl) & DCP_NS_MASK) >> DCP_NS_SHIFT)
#define DCP_SET_NS(ns)		(((ns) << DCP_NS_SHIFT) & DCP_NS_MASK)

/*Other HDLC types are seems to be unused */

#define HDLC_DATA_BUFFER		32
#define HDLC_DATA_LEN			14
#define HDLC_REMOTE_WINDOW		1

/* milliseconds */
#define IFRAME_RETRANSMIT		300
/* milliseconds */
#define KEEPALIVE_RETRANSMIT		1010
 
/* DCP states */
/* Timeouts are about 200ms  with 3 times resent (for I frames) */
/* I-frames are acknowledged with RR */

enum dcp_switch_state {
	DCP_STATE_UNK = 0,		/* just after init */
	DCP_STATE_RESET = 1,		/* all structures init'd, wait for DM */
	DCP_STATE_EXPECT_UA = 2,		/* DM seen, SABM sent - wait for UA */
	DCP_STATE_LINK_UP = 4		/* Link is UP */
}; 

enum dcp_transport {
	DCP_TRANSPORT_HDLC = 0,		/* direct io to an HDLC device */
	DCP_TRANSPORT_XIO = 1		/* external IO via callback */
};

typedef struct dcp_hdlc_pkt_ {
	ssize_t		pktlen;	/* len is the total length including addr & control */
	uint8_t		addr;
	uint8_t		control;
	uint8_t 	data[HDLC_DATA_BUFFER+4]; /* +2bytes  for FCS and another 2 for device status */
} dcp_hdlc_pkt;


/* eXternal I/O reader and writer callbacks */

typedef int (*xioreader)(void *trn_ctx, int dchan, uint8_t *data, size_t length);
typedef int (*xiowriter)(void *trn_ctx, int dchan, uint8_t *data, size_t length);


struct dcp_hdlc_state {
	int	fd;
	int	ifi;		/* any kind of interface id */	
	void	*transport_ctx;
	int	transport_mode;
	xioreader rdr;
	xiowriter wrt;
	int	lcn;
	int	remote_nr;
	int	remote_ns;
	int	local_ns;
	long	retransmit;
	struct timespec dcptime;
	enum dcp_switch_state	state;
	dcp_hdlc_pkt xpkt;
};

/* DCP command is ALLEGEDLY coded as TYPE/ADDR FIELD 
 * where type field is 2 bits long and located in bits 6,5
 * and addr field is 5 bits long and located in bits 0-4
 * Bit 7 seems to be always 1
 *
 * TYPEs are as follows: 
 *	0 - command - a command from the switch or event from the terminal
 *	1 - inquiry - a request to determine status
 *	2 - response - status report following the inquiry
 *	3 - transparent - uses to transmit ascii text
 * 
 * ADDRs are as follows:
 *	0x0 - Terminal
 *	0x1 - Function Key Module
 *	0x2 - Call Coverage Module
 *	0x3 - Display
 *	0x4 - Data module
 *	0x1f - internal 
 * 
 * 
 *
 */

/* Return codes */
 
#define DCP_OK		0	/* everything fine */
#define DCP_FAIL	-1	/* Fatal error - cannot carry out this operation */
#define DCP_ERR		-2	/* Cannot proceed but condition may go away later */
#define DCP_BUSY	-3	/* Line is busy - cannot send */
#define DCP_WARN	-4	/* Operation carried out but there are issues */

int dcp_init_hdlc(struct dcp_hdlc_state *st, int dchan, int lcn);

int dcp_init_xio(struct dcp_hdlc_state *st, int dchan, int lcn, void *trn_ctx, xioreader rdr, xiowriter wrt);

void dcp_close(struct dcp_hdlc_state *st);

int dcp_rcv(struct dcp_hdlc_state *st, bool *cmdflag, unsigned char *data);

int dcp_xmt(struct dcp_hdlc_state *st, bool cmdflag, unsigned char *data, size_t datalen);

int dcp_run(struct dcp_hdlc_state *st);

#endif /* DCP_HDR_LOADED__ */

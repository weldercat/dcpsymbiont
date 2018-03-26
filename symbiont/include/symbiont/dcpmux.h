/*
 * Copyright 2018 Stacy <stacy@sks.uz> 
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

#ifndef DCPMUX_HDR_LOADED_
#define DCPMUX_HDR_LOADED_



#include <symbiont/symbiont.h>
#include <symbiont/hua.h>

/* this module performs muxing/demuxing several DCP links into/from 
 * one HUA link.
 */


/* opaque type for dcpmux context */
typedef struct dcpmux_s dcpmux_t;

/* a structure to contain additional interface params 
 * for initialization.
 * Currently contains lcn only.
 * May be extended in future.
 */
struct dcpmux_ifparams {
	int	lcn;
};

dcpmux_t *dummy_dcpmux(void);


/* special values of length parameter to signal 
 * transport events to the datahandler */

#define DCPMUX_TRANSPORT_UP	0
#define DCPMUX_TRANSPORT_DOWN	(-1)

/* callback that is called upon reception of a valid dcp packet 
 * NULL data and zero or negative length values 
 * signal various transport events (DCPMUX_TRANSPORT_UP, DCPMUX_TRANSPORT_DOWN etc)
 * or with _NULL_ data and zero datalength if link just came up
 */
typedef int (*datahandler)(void *arg, int ifi, bool cmdflag, uint8_t *data, ssize_t length);

/* hctx should be initialized already by hua_connect() or hua_listen() with 
 * nworkers = 0. dcpmux will do it's own I/O  
 */

int dcpmux_attach(dcpmux_t *dm, hua_ctx *hctx, datahandler dhandlr, void *dh_arg);

/* delete all interfaces, shot down worker threads and destroy the context */
int dcpmux_close(dcpmux_t *dm);

/* returns error code, not the length of sent data */
int dcpmux_send(dcpmux_t *dm, int ifi, bool cmdflag, uint8_t *data, size_t length);

/******************************/
/* add an interface to the dcpmux context 
 *  ifp can be NULL, the defaults will be used then.
 */

int dcpmux_ifadd(dcpmux_t *dm, int ifi, struct dcpmux_ifparams *ifp);

/* change interface state to up,
 * dcpmux will periodically attempt to bring up the 
 * dcp link on that interface 
 */
int dcpmux_ifup(dcpmux_t *dm, int ifi);

/* change interface state to down,
 * dcpmux will cease  any actvity on that 
 * interface 
 */
int dcpmux_ifdown(dcpmux_t *dm, int ifi);

/* delete interface from dcpmux context */

int dcpmux_ifdel(dcpmux_t *dm, int ifi);

enum dcpmux_ifstate {
	DCPMUX_IFSTATE_NOTFOUND = 0,
	DCPMUX_IFSTATE_INACTIVE = 1,
	DCPMUX_IFSTATE_RUNNING = 2
};


/* 
 * check state of the interface - 
 *
 * return value - enum dcpmux_ifstate
 */
int dcpmux_ifstate(dcpmux_t *dm, int ifi);

/* controls internal debug features *
 * is not of interest to anyone but developers.
 * don't call in production 
 */
void dcpmux_debug_ctl(int arg);


#endif /* DCPMUX_HDR_LOADED_ */


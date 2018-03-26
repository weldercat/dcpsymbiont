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


#define _GNU_SOURCE	1

#include <poll.h>
#include <errno.h>
#include <stdio.h>


//#define DEBUG_ME_HARDER	1

#include <symbiont/symerror.h>
#include <symbiont/dcpmux.h>
#include <symbiont/dcphdlc.h>
#define NOFAIL_LOCK_UNNEEDED	1
#include <symbiont/nofail_wrappers.h>


/* was - 5 */
#define DCPMUX_MAXWRK	5

#define HAVE_PTHREAD_SETNAME_NP 1

#ifndef HAVE_PTHREAD_SETNAME_NP
static int pthread_setname_np(pthread_t thread, const char *name);

static int pthread_setname_np(pthread_t thread, const char *name)
{
	return 0;
}
#endif



struct dcpmux_s {
	hua_ctx	*hc;
	pthread_t	workers[DCPMUX_MAXWRK];
	pthread_mutex_t	poll_mutex;	/* a mutex to serialize poll() */
	datahandler	dh;	/* dcp payload handler */
	void	*dh_arg;
	
};

static void *dcp_worker(void *rarg);
static void handle_hua_pkt(dcpmux_t *dm, uint8_t *msgbuf, int msglen);
static int hua_xmt_wrapper(void *trn_ctx, int ifi, uint8_t *data, size_t length);
static struct dcp_hdlc_state *dcp_ctx_from_iface(dcpmux_t *dm, int ifi);

static int dbgval1 = 0;

void dcpmux_debug_ctl(int arg)
{
	dbgval1 = arg;
	SYMINFO("debug1 value set to %d\n", arg);
}




dcpmux_t *dummy_dcpmux(void)
{
	dcpmux_t *dm = NULL;
	
	dm = malloc(sizeof(dcpmux_t));
	if (!dm) {
		SYMERROR("cannot allocate memory\n");
		goto out;
	}
	memset(dm, 0, sizeof(dcpmux_t));

out:	return dm;
}

#define PTHREAD_NAME_LEN	16

int dcpmux_attach(dcpmux_t *dm, hua_ctx *hctx, datahandler dh, void *dh_arg)
{
	int	res;
	int	i;
	pthread_attr_t	attrs;
	char thread_name[PTHREAD_NAME_LEN + 1];
	
	assert(dm);
	assert(hctx);
	assert(dh);
	
	res = hua_set_nonblocking(hctx, true);
	if (res != HUA_OK) return SYM_FAIL;
	dm->hc = hctx;
	dm->dh = dh;
	dm->dh_arg = dh_arg;
	res = pthread_mutex_init(&dm->poll_mutex, NULL);
	assert(!res);
	res = pthread_attr_init(&attrs);
	eassert(res == 0);
	for (i = 0; i < DCPMUX_MAXWRK; i++) {
		res = pthread_create(&dm->workers[i], &attrs, dcp_worker, dm);
		eassert(res == 0);
		memset(thread_name, 0, PTHREAD_NAME_LEN);
		snprintf(thread_name, PTHREAD_NAME_LEN, "dcpmux_%d", i);
		res = pthread_setname_np(dm->workers[i], thread_name);
		eassert(res == 0);
		SYMDEBUG("dcpmux thread #%d (%s) started\n", i, thread_name);
	}
	return SYM_OK;
}

/* this function is called from one of dcp_worker() threads with poll_mutex unlocked
 * so it may do some long work
 *
 */ 
static void handle_hua_pkt(dcpmux_t *dm, uint8_t *msgbuf, int msglen)
{
	uint8_t	*dptr;
	int	datalen = 0;
	int	ifi = -1;
#ifdef SYMDEBUGHARD
	int	res;
#endif
	int	prevstate;
	struct interface_s *iface;
	struct	dcp_hdlc_state *dcps;
	bool	cmdflag = false;
	uint8_t	databuf[HDLC_DATA_BUFFER];
	
	assert(dm);
	assert(msgbuf);
	assert(msglen > 0);
	hua_parse_pkt(msgbuf, msglen, &ifi, &dptr, &datalen);
	if ((datalen <= 0) || (!dptr)) {
		SYMWARNING("received HUA packet without payload\n");
		return;
	}
	SYMDEBUGHARD("%d bytes dcp payload\n", datalen);
	iface = hua_find_iface(dm->hc, ifi);
	if (!iface) {
		SYMWARNING("received HUA packet for inactive/undefined interface, ifi=%d\n", ifi);
		return;
	}
	dcps = (struct dcp_hdlc_state *)iface->userdata;
	if (!dcps) {
		SYMWARNING("no dcp context for interface. ifi=%d\n", ifi);
		return;
	}
	prevstate = dcps->state;
	datalen = dcp_handle_data(dcps, dptr, datalen, &cmdflag, databuf);
	if (datalen > 0) {
		res = dm->dh(dm->dh_arg, ifi, cmdflag, databuf, datalen);
		SYMDEBUGHARD("datahandler(arg=%p, ifi=%d, buf=%p, len=%d) returns: %d\n",
				dm->dh_arg, ifi, databuf, datalen, res);
	} else {
		if ((prevstate != DCP_STATE_LINK_UP) && 
				(dcps->state == DCP_STATE_LINK_UP)) {
			SYMDEBUG("Link is up (ifi=%d)\n", ifi);
			(void)dm->dh(dm->dh_arg, ifi, cmdflag, NULL, DCPMUX_TRANSPORT_UP);
		}
	}
}


static void *dcp_worker(void *rarg)
{
	dcpmux_t	*dm;
	struct pollfd fds[1];
	int	res;
	bool	canread;
	uint8_t	msgbuf[HUA_MAX_MSG_SIZE + 4096];
	int	msglen;
	
	assert(rarg);
	dm = (dcpmux_t *)rarg;
	
	fds[0].fd = dm->hc->fd;
	fds[0].events = POLLIN | POLLRDHUP;
		
	for(;;) {
		mutex_lock(&dm->poll_mutex);
		res = poll(fds, 1, -1);
		if ((res < 0) && (errno == EINTR)) {
			mutex_unlock(&dm->poll_mutex);
			continue;
		}
		eassert(res >= 0);
		if (res == 0) {
			mutex_unlock(&dm->poll_mutex);
			SYMDEBUGHARD("poll timeout\n");
			continue;
		}
		if (fds[0].revents & POLLRDHUP) SYMFATAL("hua connection lost\n");
		canread = (fds[0].revents & POLLIN);
		if (canread) msglen = hua_read_pkt(dm->hc, msgbuf, HUA_MAX_MSG_SIZE);
		else {
			msglen = 0;
			SYMDEBUGHARD("read would block\n");
		}
		mutex_unlock(&dm->poll_mutex);
		if (canread && msglen > 0) handle_hua_pkt(dm, msgbuf, msglen);
	}
	return NULL;
}

int dcpmux_close(dcpmux_t *dm)
{
	assert(dm);
	
// locate and shot down all interfaces
// currently there is no provision in hua.c
// to locate and disable all the interfaces - so this functionality must be written
// in order for dcpmux_close() to work
// kill worker threads
// delete context
#warning Not implemented
	SYMFATAL("not implemented\n");
	return SYM_FAIL;
}

int dcpmux_send(dcpmux_t *dm, int ifi, bool cmdflag, uint8_t *data, size_t length)
{
	struct dcp_hdlc_state *dcps;
	int	res = 0;

	assert(dm);
	assert(data);
	if (!length) goto out;
	dcps = dcp_ctx_from_iface(dm, ifi);
	if (!dcps) {
		SYMDEBUG("cannot find DCP context for ifi=%d\n", ifi);
		res = SYM_FAIL;
		goto out;
	}
	res = dcp_xmt(dcps, cmdflag, data, length);
out:
	return res;
}

static int hua_xmt_wrapper(void *trn_ctx, int ifi, uint8_t *data, size_t length)
{
	hua_ctx *hc;
	
	hc = trn_ctx;
	SYMDEBUGHARD("%d bytes dcp payload\n", length);
	return hua_send_data(hc, ifi, data, length);
}

static struct dcp_hdlc_state *dcp_ctx_from_iface(dcpmux_t *dm, int ifi)
{
	struct interface_s *iface = NULL;
	struct dcp_hdlc_state *dcps = NULL;
	
	assert(dm);
	iface = hua_find_iface(dm->hc, ifi);
	if (!iface) {
		SYMERROR ("no interface with ifi=%d\n", ifi);
		goto out;
	}
	dcps = iface->userdata;
	if (!dcps) {
		SYMERROR("interface %d has no associated dcp context\n", ifi);
		goto out;
	}

out:	
	return dcps;
}

int dcpmux_ifadd(dcpmux_t *dm, int ifi, struct dcpmux_ifparams *ifp)
{
	struct	interface_s iface;
	int	res;
	struct dcp_hdlc_state *dcps;
	int	lcn;
	
	assert(dm);
	assert(ifi > 0);
	
	dcps = malloc(sizeof(struct dcp_hdlc_state));
	if (!dcps) {
		SYMERROR("cannot allcate memory\n");
		return SYM_FAIL;
	}
	memset(dcps, 0, sizeof(struct dcp_hdlc_state));
	if (ifp) lcn = ifp->lcn;
	else lcn = DCP_LCN_CHAN0;
	res = dcp_init(dcps, ifi, lcn, dm->hc, hua_xmt_wrapper);
	if (res != DCP_OK) {
		free(dcps);
		return SYM_FAIL;
	}
	iface.ifi = ifi;
	iface.userdata = dcps;
	iface.stream = hua_get_sid(dm->hc);
	res = hua_add_iface(dm->hc, &iface);
	if (res == HUA_OK) return SYM_OK;
	else {
		dcp_close(dcps);
		free(dcps);
		return SYM_FAIL;
	}
	return SYM_OK;
}


int dcpmux_ifup(dcpmux_t *dm, int ifi)
{
	struct dcp_hdlc_state *dcps;
	
	assert(dm);
	dcps = dcp_ctx_from_iface(dm, ifi);
	if (!dcps) return SYM_FAIL;
	dcp_enable_link(dcps);
	return SYM_OK;
}

int dcpmux_ifdown(dcpmux_t *dm, int ifi)
{
	struct dcp_hdlc_state *dcps;
	
	assert(dm);
	dcps = dcp_ctx_from_iface(dm, ifi);
	if (!dcps) return SYM_FAIL;
	dcp_disable_link(dcps);
	return SYM_OK;
}

int dcpmux_ifdel(dcpmux_t *dm, int ifi)
{
	struct dcp_hdlc_state *dcps;
	struct interface_s *iface;
	int	res = SYM_FAIL;
	
	assert(dm);
	iface = hua_find_iface(dm->hc, ifi);
	if (!iface) {
		SYMERROR ("no interface with ifi=%d\n", ifi);
		goto out;
	}
	dcps = iface->userdata;
	
	if (dcps) {
		if (dcps->state != DCP_STATE_DISABLE) {
		SYMERROR("interface %d is active - refusing to delete\n", ifi);
		goto out;
		}
		dcp_close(dcps);
		free(dcps);
		iface->userdata = NULL;
	} else SYMWARNING("interface %d has no associated dcp context, removing from hua ctx only\n", ifi);
	hua_release_sid(dm->hc, iface->stream);
	res = hua_drop_iface(dm->hc, ifi);
	if (res != HUA_OK) res = SYM_FAIL;
	else res = SYM_OK;

out:	
	return res;
}


int dcpmux_ifstate(dcpmux_t *dm, int ifi)
{
	struct dcp_hdlc_state *dcps;
	
	assert(dm);
	dcps = dcp_ctx_from_iface(dm, ifi);
	if (!dcps) return DCPMUX_IFSTATE_NOTFOUND;
	
	if (dcps->state == DCP_STATE_DISABLE) return DCPMUX_IFSTATE_INACTIVE;
	else return DCPMUX_IFSTATE_RUNNING;

}

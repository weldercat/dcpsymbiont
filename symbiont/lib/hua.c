/*
* Copyright 2017 Stacy <stacy@sks.uz>
*
*  This program is free software: you can redistribute it and/or modify
*  it under the terms of the GNU General Public License as published by
*  the Free Software Foundation, either version 2 of the License, or
*  (at your option) any later version.
*
*  This program is distributed in the hope that it will be useful,
*  but WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*  GNU General Public License for more details.
*
*  You should have received a copy of the GNU General Public License
*  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#define _GNU_SOURCE	1

#include <stdio.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/sctp.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>


//#define DEBUG_ME_HARDER	1

#include <symbiont/symerror.h>
#include <symbiont/hua.h>
#include <symbiont/sigtran.h>
#include <symbiont/nofail_wrappers.h>
#include "config.h"

#define HUA_LISTENQ	3


//#define CHECK_ADDRESSES_ONLY	1

#define MAX_OSTREAMS	65535
#define MAX_ISTREAMS	65535


struct reader_arg {
	hua_ctx *ctx;
	hua_receiver rcvr;
	void *rcvarg;
	sem_t copied;
	
};

static int decode_connstr(const char *connstr, char **addrstr, uint16_t *port, uint32_t *ppid);

static struct ifshell_s *scan_shells(struct ifshell_s *start, int ifi);
static void init_ctx(hua_ctx *ctx);
static void start_workers(hua_ctx *ctx, hua_receiver rcvr, void *rcvarg, int nworkers);
static void *reader_thread(void *rarg);
static void rcv_mutex_cleanup(void *arg);
#ifdef DEBUG_ME_HARDER
static void hexdump(unsigned char *b, int len);
#endif
static int set_inimaxstreams(int sock_fd);
static int get_maxstreams(int sock_fd);

#ifndef HAVE_PTHREAD_SETNAME_NP
static int pthread_setname_np(pthread_t thread, const char *name);
#endif




#ifndef HAVE_PTHREAD_SETNAME_NP
static int pthread_setname_np(pthread_t thread, const char *name)
{
	return 0;
}
#endif

#ifdef DEBUG_ME_HARDER
#define DUMP_LINE	16
#define MAX_DUMP_LINE	79
static void hexdump(unsigned char *b, int len)
{
	int	i, j;
	unsigned char c;
	char	outbuf[MAX_DUMP_LINE + 1];
	int	pos;
	int	res;
	int	space;

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
			symtrace(TRC_DBG2, "%s", outbuf);
			
		}
	} else {
		symtrace(TRC_DBG2, "Nothing to print, len=%d\n", len);
	}
	symtrace(TRC_DBG2, "\n");
}
#endif

static	int	dbgval1 = 0;


void hua_debug_ctl(int arg)
{
	dbgval1 = arg;
	SYMINFO("debug1 value set to %d\n", arg);
}


/* 0.0.0.0:0_1 */ 
#define MIN_CONNSTR_LEN	11

static int decode_connstr(const char *connstr, char **addrstr, uint16_t *port, uint32_t *ppid)
{
	int	len;
	char	*portsep;
	char	*pidsep;
	
	assert(connstr);
	assert(addrstr);
	assert(port);
	assert(ppid);
	
	len = strlen(connstr);
	if (len < MIN_CONNSTR_LEN) return HUA_FAIL;
	portsep = strchr(connstr, ':');
	pidsep = strrchr(connstr, '_');
	if ((!portsep) || (!pidsep)) return HUA_FAIL;
	if ((portsep + 1) >= connstr + len) return HUA_FAIL;
	if ((pidsep + 1) >= connstr + len) return HUA_FAIL;
	*addrstr = strndup(connstr, (portsep - connstr));
	*port = strtol(portsep + 1, NULL, 10);
	*ppid = strtol(pidsep + 1, NULL, 10);
	return HUA_OK;
}


static void rcv_mutex_cleanup(void *arg)
{
	hua_ctx *ctx;
	
	ctx = (hua_ctx *)arg;
	assert(arg);
	mutex_unlock(&ctx->rcv_mutex);
}

static void *reader_thread(void *rarg)
{
	struct reader_arg *rp;
	struct reader_arg r;
	hua_ctx	*ctx;
	unsigned char message[HUA_MAX_MSG_SIZE];
	int	msglen, datalen, ifi;
	struct sctp_sndrcvinfo sinfo;
	struct sockaddr_in rem_addr;
	unsigned int rem_length;
	int	msg_flags = 0;
	uint8_t	*data;
	int	res;
	char	remstr[INET_ADDRSTRLEN+1];
	char	wantedstr[INET_ADDRSTRLEN+1];

	assert(rarg);
	rp = (struct reader_arg *)rarg;
	r = *rp;
	ctx = r.ctx;
	res = sem_post(&rp->copied);
	eassert(res == 0);
	
	for(;;) {
		memset(&message, 0, HUA_MAX_MSG_SIZE);
		memset(&sinfo, 0, sizeof(sinfo));
		(void)pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
		mutex_lock(&ctx->rcv_mutex);
		pthread_cleanup_push(rcv_mutex_cleanup, ctx);
		rem_length = sizeof(rem_addr);
		msglen = sctp_recvmsg(ctx->fd, message, HUA_MAX_MSG_SIZE, (struct sockaddr *) &rem_addr, 
					&rem_length, &sinfo, &msg_flags);
		pthread_cleanup_pop(1);
		(void)pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
		if (msglen < 0) {
			SYMERROR("sctp_recvmsg() error: %s\n", STRERROR_R(errno));
			continue;
		}
		if (msglen == 0) {
			SYMERROR("zero-length message\n");
			continue;
		}
		if (msglen < XRN_MIN_MSGLEN) {
			SYMERROR("SIGTRAN packet too short: %d bytes\n", msglen);
			continue;
		}
		if (ctx->connected) {
			if (cantrace(TRC_DBG2)) {
				memset(remstr, 0, INET_ADDRSTRLEN+1);
				inet_ntop(AF_INET, &rem_addr.sin_addr, remstr, INET_ADDRSTRLEN);
				SYMDEBUGHARD("packet from %s:%d\n", remstr, (int)ntohs(rem_addr.sin_port));
			}
#ifdef CHECK_ADDRESSES_ONLY
			if (memcmp(&rem_addr.sin_addr, &ctx->remote.sin_addr, sizeof(rem_addr.sin_addr))) {
#else
			if (memcmp(&rem_addr, &ctx->remote, sizeof(rem_addr))) {
#endif
				memset(remstr, 0, INET_ADDRSTRLEN+1);
				inet_ntop(AF_INET, &rem_addr.sin_addr, remstr, INET_ADDRSTRLEN);
				memset(wantedstr, 0, INET_ADDRSTRLEN+1);
				inet_ntop(AF_INET, &ctx->remote.sin_addr, wantedstr, INET_ADDRSTRLEN);
				SYMWARNING("packet from unknown remote peer %s:%d, expected %s:%d - dropped\n", remstr, 
					(int)ntohs(rem_addr.sin_port), wantedstr, (int)ntohs((ctx->remote).sin_port));
				continue;
			}
		} else {
			memset(remstr, 0, INET_ADDRSTRLEN+1);
			inet_ntop(AF_INET, &rem_addr.sin_addr, remstr, INET_ADDRSTRLEN);
			SYMDEBUG("connect from %s:%d%x\n", remstr, (int)ntohs(rem_addr.sin_port));
			ctx->remote = rem_addr;
			ctx->connected = true;
		}
		hua_parse_pkt(message, msglen, &ifi, &data, &datalen);
		if (datalen && data) {
			SYMDEBUG("about to call receiver handler: ifi=%d, data=%p, datalen=%d, arg=%p\n",
					ifi, data, datalen, r.rcvarg);
			r.rcvr(ifi, data, datalen, r.rcvarg);
		}
	}

	return NULL;
}

int hua_set_nonblocking(hua_ctx *ctx, bool nb)
{
	int	res = HUA_FAIL;
	int	flags;
	
	assert(ctx);
	mutex_lock(&ctx->rcv_mutex);
	if (ctx->nworkers == 0) {
		res = fcntl(ctx->fd, F_GETFL);
		if (res < 0) {
			SYMERROR("fcntl() cannot read flags:%s\n", STRERROR_R(errno));
			res = HUA_FAIL;
		} else {
			flags = res;
			if (nb) flags |= O_NONBLOCK;
			else flags &= ~O_NONBLOCK;
			res = fcntl(ctx->fd, F_SETFL, flags);
			if (res < 0) {
				SYMERROR("fcntl() cannot %s non-blocking mode: %s\n", 
					((nb) ? "set" : "clear"), STRERROR_R(errno));
				res = HUA_FAIL;
			} else res = HUA_OK;
		}
	} else {
		SYMDEBUG("cannot set nonblocking mode when workers are active\n");
		res = HUA_FAIL;
	}
	mutex_unlock(&ctx->rcv_mutex);
	return res;
}


int hua_read_pkt(hua_ctx *ctx, uint8_t *message, int maxlen)
{
	int	msglen = -1;
	struct sctp_sndrcvinfo sinfo;
	struct sockaddr_in rem_addr;
	unsigned int rem_length;
	int	msg_flags = 0;
	char	remstr[INET_ADDRSTRLEN+1];
	char	wantedstr[INET_ADDRSTRLEN+1];

	assert(ctx);
	assert(message);
	assert(maxlen >= XRN_MIN_MSGLEN);
	SYMDEBUGHARD("about to call sctp_recvmsg() on fd=%d, maxlen=%d\n", ctx->fd, maxlen);
	memset(message, 0, maxlen);
	memset(&sinfo, 0, sizeof(sinfo));
	mutex_lock(&ctx->rcv_mutex);
	rem_length = sizeof(rem_addr);
	memset(&rem_addr, 0, rem_length);
//	rem_addr.sin_family = AF_INET;
	msglen = sctp_recvmsg(ctx->fd, message, maxlen, (struct sockaddr *) &rem_addr, 
				&rem_length, &sinfo, &msg_flags);
	mutex_unlock(&ctx->rcv_mutex);
	SYMDEBUGHARD("sctp_recvmsg() msglen=%d\n", msglen);

#ifdef DEBUG_ME_HARDER
//	if (msglen > 0) hexdump(message, msglen);
#endif
	if (msglen < 0) {
		if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
			SYMDEBUG("operation would block\n");
			msglen = HUA_AGAIN;
		} else {
			SYMERROR("sctp_recvmsg() error: %s\n", STRERROR_R(errno));
			goto out;
		}
	}
	if (msglen == 0) {
		SYMERROR("zero-length message\n");
		msglen = -1;
		goto out;
	}
	if (msglen < XRN_MIN_MSGLEN) {
		SYMERROR("SIGTRAN packet too short: %d bytes\n", msglen);
		msglen = -1;
		goto out;
	}

	if (ctx->connected) {
		if (cantrace(TRC_DBG2)) {
			memset(remstr, 0, INET_ADDRSTRLEN+1);
			inet_ntop(AF_INET, &rem_addr.sin_addr, remstr, INET_ADDRSTRLEN);
			SYMDEBUGHARD("packet from %s:%d\n", remstr, (int)ntohs(rem_addr.sin_port));
		}
#ifdef CHECK_ADDRESSES_ONLY
		if (memcmp(&rem_addr.sin_addr, &ctx->remote.sin_addr, sizeof(rem_addr.sin_addr))) {
#else
		if (memcmp(&rem_addr, &ctx->remote, sizeof(rem_addr))) {
#endif
			memset(remstr, 0, INET_ADDRSTRLEN+1);
			inet_ntop(AF_INET, &rem_addr.sin_addr, remstr, INET_ADDRSTRLEN);
			memset(wantedstr, 0, INET_ADDRSTRLEN+1);
			inet_ntop(AF_INET, &ctx->remote.sin_addr, wantedstr, INET_ADDRSTRLEN);
			SYMWARNING("packet from unknown remote peer %s:%d, expected %s:%d - dropped\n", remstr, 
				(int)ntohs(rem_addr.sin_port), wantedstr, (int)ntohs((ctx->remote).sin_port));
			msglen = -1;	
			goto out;
		}
	} else {
		memset(remstr, 0, INET_ADDRSTRLEN+1);
		inet_ntop(AF_INET, &rem_addr.sin_addr, remstr, INET_ADDRSTRLEN);
		SYMDEBUG("connect from %s:%d%x\n", remstr, (int)ntohs(rem_addr.sin_port));
		ctx->remote = rem_addr;
		ctx->connected = true;
	}
out:
	return msglen;
}



#define PTHREAD_NAME_LEN	16
static void start_workers(hua_ctx *ctx, hua_receiver rcvr, void *rcvarg, int nworkers)
{
	int	res, i; 
	pthread_attr_t attrs;
	char	thread_name[PTHREAD_NAME_LEN + 1];
	struct reader_arg rarg;
	
	assert(ctx);
	assert(nworkers >= 0);
	if (!rcvr) nworkers = 0;
	res = pthread_attr_init(&attrs);
	eassert(res == 0);
	if (nworkers > MAX_NWORKERS) {
		SYMWARNING("too many workers requested (%d), clamped down to %d\n", nworkers, MAX_NWORKERS);
		nworkers = MAX_NWORKERS;
	}
	rarg.ctx = ctx;
	rarg.rcvr = rcvr;
	rarg.rcvarg = rcvarg;
	if (nworkers > 0) {
		res = sem_init(&rarg.copied, 0, 0);
		eassert(res == 0);
	
		for(i = 0; i < nworkers; i++) {
			res = pthread_create(&ctx->workers[i], &attrs, reader_thread, &rarg);
			eassert(res == 0);
			memset(thread_name, 0, PTHREAD_NAME_LEN);
			snprintf(thread_name, PTHREAD_NAME_LEN, "hua_rdr_%d", i);
			res = pthread_setname_np(ctx->workers[i], thread_name);
			eassert(res == 0);
			for (;;) {
				res = sem_wait(&rarg.copied);
				if (res == 0) break;
				if (errno == EINTR) continue;
				SYMFATAL("error waiting for reader_thread to copy parameters: %s\n", STRERROR_R(errno));
			}
			SYMDEBUG("worker thread #%d (%s) started\n", i, thread_name);
		}
		sem_destroy(&rarg.copied);
	} else {
		nworkers = 0;
		SYMDEBUG("no workers requested - upper layer has to poll() socket fd\n");
	}
	ctx->nworkers = nworkers;
}


static void init_ctx(hua_ctx *ctx)
{
	int	res;
	
	assert(ctx);
	res = pthread_mutex_init(&ctx->state_mutex, NULL);
	assert(!res);
	res = pthread_rwlock_init(&ctx->iftab_rwlock, NULL);
	assert(!res);
	res = pthread_mutex_init(&ctx->sidfactory_mutex, NULL);
	assert(!res);
	res = pthread_mutex_init(&ctx->rcv_mutex, NULL);
	assert(!res);
	ctx->sidstack = malloc((DEFAULT_SIDSTACK_ENTRIES + 1) * (sizeof(uint16_t)));
	assert(ctx->sidstack);
	ctx->stacklen = DEFAULT_SIDSTACK_ENTRIES;
	memset(ctx->sidstack, 0, (DEFAULT_SIDSTACK_ENTRIES + 1) * (sizeof(uint16_t)));
}

static int set_inimaxstreams(int sock_fd)
{
	struct sctp_initmsg initmsg;
	socklen_t optlen;
	int	res;

	optlen = sizeof(initmsg);
	memset(&initmsg, 0, optlen);
	res = getsockopt(sock_fd, IPPROTO_SCTP, SCTP_INITMSG, &initmsg, &optlen);
	if (res) {
		SYMERROR("getsockopt() error: %s\n", STRERROR_R(errno));
		return HUA_FAIL;
	}	
	SYMDEBUG("default params - num_ostreams=%d, max_instreams=%d, max_attempts=%d, init_timeo=%d\n",
			(int)initmsg.sinit_num_ostreams, (int)initmsg.sinit_max_instreams,
			(int)initmsg.sinit_max_attempts,  (int)initmsg.sinit_max_init_timeo);
	
	initmsg.sinit_num_ostreams = MAX_OSTREAMS;
	initmsg.sinit_max_instreams = MAX_ISTREAMS;
	res = setsockopt(sock_fd, IPPROTO_SCTP, SCTP_INITMSG, &initmsg, sizeof(initmsg));
	if (res) {
		SYMERROR("setsockopt() error: %s\n", STRERROR_R(errno));
		return HUA_FAIL;
	}
	SYMDEBUG("max streams set successfully\n");
	return HUA_OK;
}

/* returns lower of max istreams and max ostreams or -1 if error */
static int get_maxstreams(int sock_fd)
{
	struct sctp_initmsg initmsg;
	socklen_t optlen;
	int	res;

	optlen = sizeof(initmsg);
	memset(&initmsg, 0, optlen);
	res = getsockopt(sock_fd, IPPROTO_SCTP, SCTP_INITMSG, &initmsg, &optlen);
	if (res) {
		SYMERROR("getsockopt() error: %s\n", STRERROR_R(errno));
		return -1;
	}	
	res = initmsg.sinit_num_ostreams;
	if (initmsg.sinit_max_instreams < res) res = initmsg.sinit_max_instreams;

	return res;
}



hua_ctx *hua_connect(const char *dest, hua_receiver rcvr, void *rcvarg, int nworkers)
{
	int	res;
	int	sock_fd = -1;
	hua_ctx *ctx = NULL;
	struct sockaddr_in servaddr;
	char	*addrstr = NULL;
	uint16_t port = DEFAULT_HUA_PORT;
	uint32_t ppid = DEFAULT_HUA_PPID;
	struct sctp_event_subscribe evnts;
	
	assert(dest);
	if (!rcvr) nworkers = 0;
	if (nworkers < 0) nworkers = 0;
	else if (nworkers > MAX_NWORKERS) {
		SYMWARNING("too many workers requested (%d), clamped down to %d\n", nworkers, MAX_NWORKERS);
		nworkers = MAX_NWORKERS;
	}
	decode_connstr(dest, &addrstr, &port, &ppid);
	if (!addrstr) {
			SYMERROR("there is no default for server address - cannot connect\n");
			goto out;
	}
	
	memset(&servaddr, 0, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servaddr.sin_port = htons(port);
	res = inet_pton(AF_INET, addrstr, &servaddr.sin_addr);
	if (res != 1) {
		SYMERROR("invalid server address \"%s\"\n", addrstr);
		goto out;
	}
	free(addrstr);
	addrstr = NULL;
	
	sock_fd = socket(PF_INET, SOCK_SEQPACKET, IPPROTO_SCTP);
	if (sock_fd < 0) {
		SYMERROR("cannot create SCTP socket: %s\n", STRERROR_R(errno));
		goto out;
	}
	ctx = calloc(1, sizeof(hua_ctx));
	if (!ctx) {
		SYMERROR("cannot allocate memory\n");
		goto out;
	}
	ctx->fd = sock_fd;
	ctx->ppid = ppid;
	memset(&evnts, 0, sizeof(evnts));
	evnts.sctp_data_io_event = 1;
	res = setsockopt(sock_fd, IPPROTO_SCTP, SCTP_EVENTS, &evnts, sizeof(evnts));
	if (res) {
		SYMERROR("setsockopt() error: %s\n", STRERROR_R(errno));
		goto out;
	}
	res = set_inimaxstreams(sock_fd);
	if (res != HUA_OK) goto out;
	res = connect(sock_fd, &servaddr, sizeof(servaddr));
	if (res) {
		SYMERROR("cannot connect to %s: %s\n", dest, STRERROR_R(errno));
		goto out;
	}
	init_ctx(ctx);
	res = get_maxstreams(sock_fd);
	SYMDEBUG("connection max streams = %d\n", res);
	if (res > 0) ctx->maxstreams = res;
	else ctx->maxstreams = MAX_SID;
	ctx->client = true;
	ctx->remote = servaddr;
	ctx->connected = true;
	ctx->conn_state = HUA_SCTP_UP;
	start_workers(ctx, rcvr, rcvarg, nworkers);
	return ctx;
out:	
	if (addrstr) free(addrstr);
	if (ctx) free(ctx);
	if (sock_fd >= 0) close(sock_fd);
	return NULL;
}


static struct ifshell_s *scan_shells(struct ifshell_s *start, int ifi)
{
	assert(start);
	for (;;) {
		if (start->iface.ifi == ifi) break;
		if (!start->next) break;
		start = start->next;
	}
	return start;
}

int hua_add_iface(hua_ctx *ctx, struct interface_s *iface)
{
	struct ifshell_s *shell;
	struct ifshell_s *sptr;
	int	idx;
	int	res = HUA_FAIL;
	
	assert(ctx);
	assert(iface);
	shell = calloc(1, sizeof(struct ifshell_s));
	if (!shell) {
		SYMERROR("cannot allocate memory\n");
		goto out;
	}
	shell->iface = *iface;
	idx = iface->ifi & IFTABLE_IDX_MASK;
	lock_write(&ctx->iftab_rwlock);
	sptr = ctx->shells[idx];
	if (sptr) {
		sptr = scan_shells(sptr, iface->ifi);
		if (sptr->iface.ifi == iface->ifi) {
			SYMERROR("interface #%0u is already configured\n", iface->ifi);
			lock_unlock(&ctx->iftab_rwlock);
			free(shell);
			goto out;
		}
		sptr->next = shell;
		shell->prev = sptr;
		res = HUA_OK;
	} else {
		ctx->shells[idx] = shell;
		res = HUA_OK;
	}
	lock_unlock(&ctx->iftab_rwlock);
out:
	return res;
}

int hua_drop_iface(hua_ctx *ctx, int ifi)
{
	struct ifshell_s *sptr;
	int	idx;
	int	res = HUA_FAIL;

	assert(ctx);
	idx = ifi & IFTABLE_IDX_MASK;
	lock_write(&ctx->iftab_rwlock);
	sptr = ctx->shells[idx];
	if (sptr) {
		sptr = scan_shells(sptr, ifi);
		if (sptr->iface.ifi == ifi) {
			if (sptr->prev) sptr->prev->next = sptr->next;
			else ctx->shells[idx] = sptr->next;
			if (sptr->next) sptr->next->prev = sptr->prev;
			free(sptr);
			res = HUA_OK;
			goto out;
		}
	}
	SYMERROR("interface #%0u does not exist\n", ifi);
out:
	lock_unlock(&ctx->iftab_rwlock);
	return res;
}

struct interface_s *hua_find_iface(hua_ctx *ctx, int ifi)
{
	struct ifshell_s *sptr;
	struct interface_s *iface = NULL;
	int	idx;
	
	assert(ctx);
	idx = ifi & IFTABLE_IDX_MASK;
	lock_read(&ctx->iftab_rwlock);
	sptr = ctx->shells[idx];
	if (sptr) {
		sptr = scan_shells(sptr, ifi);
		if (sptr->iface.ifi == ifi) iface = &sptr->iface;
	}
	lock_unlock(&ctx->iftab_rwlock);
	return iface;
}

uint16_t hua_get_sid(hua_ctx *ctx)
{
	uint16_t sid;

	assert(ctx);
	mutex_lock(&ctx->sidfactory_mutex);
	assert(ctx->sidstack);
	if (ctx->stacktop > 0) {
		sid = ctx->sidstack[ctx->stacktop];
		ctx->stacktop--;
	} else {
		if (ctx->max_sid < (ctx->maxstreams - 1)) {
			ctx->max_sid++;
		} else SYMWARNING("max streams count exceeded - performance may drop significantly for some interfaces\n");
		sid = ctx->max_sid;
	}
	mutex_unlock(&ctx->sidfactory_mutex);
	return sid;
}

void	hua_release_sid(hua_ctx *ctx, uint16_t sid)
{
	uint16_t	*stackptr = NULL;
	
	assert(ctx);
	mutex_lock(&ctx->sidfactory_mutex);
	ctx->stacktop++;
	if (ctx->stacktop >= ctx->stacklen) {
		stackptr = realloc(ctx->sidstack, (ctx->stacklen + SIDSTACK_GROW_PACE + 1) * sizeof(uint16_t));
		if (!stackptr) SYMFATAL("cannot grow sid stack, stacklen=%0u\n", ctx->stacklen);
		ctx->sidstack = stackptr;
		ctx->stacklen += SIDSTACK_GROW_PACE;
	}
	assert(ctx->stacktop <= MAX_SID);
	ctx->sidstack[ctx->stacktop] = sid;
	mutex_unlock(&ctx->sidfactory_mutex);
}

#define MAX_DIAG_LINE	256

/* returns pointer to protocol data in *data, data length in *datalen, 
 * interface idientifier in *ifi
 * Returns NULL in *data and zero in *datalength if packet format is bad. 
 *
 */
void hua_parse_pkt(uint8_t *message, int msglen, int *ifi, uint8_t **data, int *datalen)
{
	xrn_tlv	tlv;
	xrn_pktbuf p;
	uint8_t	*res;
#ifdef	DEBUG_ME_HARDER
	char diagline[MAX_DIAG_LINE];
	int diaglen;
#endif

	assert(message);
	assert(ifi);
	assert(data);
	assert(datalen);
	if (msglen < HUA_MIN_MSG_SIZE) goto errout;
	p.memlen = msglen;
	p.datalen = msglen;
	p.data = message;
	
	SYMDEBUGHARD("about to parse HUA packet - %d bytes long\n", msglen);
#ifdef DEBUG_ME_HARDER
//	hexdump(message, msglen);
#endif
	if (!xrn_valid_packet(&p)) {
		SYMDEBUG("invalid packet format\n");
		goto errout;
	}
	if (!(xrn_get_mclass(&p) == QPTM)) {
		SYMDEBUG("message class is not QPTM. Disregarded\n");
		goto errout;
	}
	if (!(xrn_get_mtype(&p) == QPTM_UNIT_DATA_REQ)) {
		SYMDEBUG("message type is not UNIT DATA REQUEST. Disregarded\n");
		goto errout;
	}
#ifdef DEBUG_ME_HARDER
	res = xrn_find_tlv(&p, &tlv, TAG_DIAG_INFO, NULL);
	if (res) {
		diaglen = tlv.vlen;
		if (diaglen > 0) {
			if (diaglen > (MAX_DIAG_LINE - 1)) diaglen = MAX_DIAG_LINE - 1;
			memset(&diagline[0], 0, MAX_DIAG_LINE);
			strncpy(&diagline[0], (char *)tlv.value, diaglen);
			SYMDEBUG("Diag: %s\n", &diagline[0]);
		}
	}
#endif
	res = xrn_find_tlv(&p, &tlv, TAG_INTERFACE_ID_NUM, NULL);
	if (!res) {
		SYMDEBUG("no interface identifier in message. Disregarded\n");
		goto errout;
	}
	if (tlv.vlen != HUA_IFI_SIZE) {
		SYMDEBUG("invalid ifi length\n");
		goto errout;
	}
	*ifi = xrn_get_net32(tlv.value);
	SYMDEBUGHARD("got ifi=%d\n", *ifi);
	res = xrn_find_tlv(&p, &tlv, TAG_PROTOCOL_DATA, NULL);
	if (!res) {
		SYMDEBUG("no PROTOCOL_DATA in message. Disregarded\n");
		goto errout;
	}
	*data = tlv.value;
	*datalen = tlv.vlen;
	return;
errout:
	*data = NULL;
	*datalen = 0;
}


xrn_pktbuf *hua_assemble_pkt(int ifi, uint8_t *data, int datalen)
{
	xrn_pktbuf *p;
	uint8_t	net_ifi[HUA_IFI_SIZE];
	
	assert(data);
	assert(datalen > 0);
	assert(datalen <= 0xffff);
	p = xrn_alloc_pkt(datalen + HUA_DEFAULT_OVERHEAD);
	xrn_init_msg(p);
	xrn_set_mclass(p, QPTM);
	xrn_set_mtype(p, QPTM_UNIT_DATA_REQ);
	xrn_store_net32(&net_ifi[0], ifi);
	xrn_append_tlv(p, TAG_INTERFACE_ID_NUM, &net_ifi[0], HUA_IFI_SIZE);
	xrn_append_tlv(p, TAG_PROTOCOL_DATA, data, datalen);
	return p;
}

int hua_send_data(hua_ctx *ctx, int ifi, uint8_t *data, int datalen)
{
	xrn_pktbuf *p;

	struct interface_s *ifptr;
	uint16_t sno;
	int	res;

	assert(ctx);
	assert(data);
	assert(datalen > 0);
	p = hua_assemble_pkt(ifi, data, datalen);
	ifptr = hua_find_iface(ctx, ifi);
	if (ifptr) sno = ifptr->stream;
	else {
		SYMDEBUG("no stream found for ifi=%d, sending using stream 1\n", ifi);
		sno = 1;
	}
	if (!ctx->connected) {
		SYMERROR("not connected - remote addr is not known\n");
		res = HUA_FAIL;
	}
	res = sctp_sendmsg(ctx->fd, xrn_dataptr(p), xrn_datalen(p), (struct sockaddr *) &ctx->remote, sizeof(ctx->remote),
				ctx->ppid, 0, sno, 0, 0);
	SYMDEBUGHARD("sctp_sendmsg() returns %d for fd=%d, datalen=%d, ppid=%d, sno=%d\n", res, ctx->fd, 
			xrn_datalen(p), ctx->ppid, sno);
	if (res < 0) {
		if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
			SYMDEBUG("operation would block\n");
			res = HUA_AGAIN;
		} else {
			SYMERROR("error sending sctp message: %s\n", STRERROR_R(errno));
			res = HUA_FAIL;
		}
	}
	xrn_free_pkt(p);
	return res;
}

hua_ctx *hua_listen(const char *bindstr, hua_receiver rcvr, void *rcvarg, int nworkers)
{
	int	res;
	int	sock_fd = -1;
	hua_ctx *ctx = NULL;
	struct sockaddr_in servaddr;
	char	*addrstr = NULL;
	uint16_t port = DEFAULT_HUA_PORT;
	uint32_t ppid = DEFAULT_HUA_PPID;
	struct sctp_event_subscribe evnts;

	assert(bindstr);
	if (!rcvr) nworkers = 0;
	if (nworkers < 0) nworkers = 0;
	else if (nworkers > MAX_NWORKERS) {
		SYMWARNING("too many workers requested (%d), clamped down to %d\n", nworkers, MAX_NWORKERS);
		nworkers = MAX_NWORKERS;
	}
	decode_connstr(bindstr, &addrstr, &port, &ppid);
	if (!addrstr) {
			SYMERROR("there is no default for server address - cannot connect\n");
			goto out;
	}
	
	memset(&servaddr, 0, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servaddr.sin_port = htons(port);
	res = inet_pton(AF_INET, addrstr, &servaddr.sin_addr);
	free(addrstr);
	if (res != 1) {
		SYMERROR("invalid server address \"%s\"\n", addrstr);
		goto out;
	}
	sock_fd = socket(PF_INET, SOCK_SEQPACKET, IPPROTO_SCTP);
	if (sock_fd < 0) {
		SYMERROR("cannot create SCTP socket: %s\n", STRERROR_R(errno));
		goto out;
	}
	ctx = calloc(1, sizeof(hua_ctx));
	if (!ctx) {
		SYMERROR("cannot allocate memory\n");
		goto out;
	}
	ctx->fd = sock_fd;
	ctx->ppid = ppid;
	res = bind(sock_fd, (struct sockaddr *) &servaddr, sizeof(servaddr));
	if (res) {
		SYMERROR("bind() error: %s\n", STRERROR_R(errno));
		goto out;
	}
	memset(&evnts, 0, sizeof(evnts));
	evnts.sctp_data_io_event = 1;
	res = setsockopt(sock_fd, IPPROTO_SCTP, SCTP_EVENTS, &evnts, sizeof(evnts));
	if (res) {
		SYMERROR("setsockopt() error: %s\n", STRERROR_R(errno));
		goto out;
	}
	res = set_inimaxstreams(sock_fd);
	if (res != HUA_OK) goto out;

	res = listen(sock_fd, HUA_LISTENQ);
	if (res) {
		SYMERROR("cannot listen: %s\n", STRERROR_R(errno));
		goto out;
	}
	init_ctx(ctx);
	res = get_maxstreams(sock_fd);
	SYMDEBUG("connection max streams = %d\n", res);
	if (res > 0) ctx->maxstreams = res;
	else ctx->maxstreams = MAX_SID;
	ctx->client = false;
	ctx->conn_state = HUA_SCTP_UP;
	start_workers(ctx, rcvr, rcvarg, nworkers);
	return ctx;
out:
	if (ctx) free(ctx);
	if (sock_fd >= 0) close(sock_fd);
	return NULL;

}


/* close/unlisten hua link, free all resources and destroy the context */
void hua_shutdown(hua_ctx *ctx)
{
	assert(ctx);
#warning Not implemented
	SYMFATAL("not implemented\n");
}


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
#include <errno.h>
#include <assert.h>
#include <unistd.h>
#include <stdarg.h>
#include <uuid/uuid.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>

#define DEBUG_ME_HARDER	1

#include <symbiont/symbiont.h>
#define NOFAIL_LOCK_UNNEEDED	1
#include <symbiont/nofail_wrappers.h>

#define SOCKPATH	"/var/run/yate/ysock"
#define CALL_ID_MAXLEN	256
#define UUID_LEN	36
#define MAX_CALLED_LEN	256

#define INTERDIGIT_TIMEOUT	3500
#define FIRST_DIGIT_TIMEOUT	10000

/* timeout waiting for INFORMATION before providing dialtone */
#define INFORMATION_TIMEOUT	550

#define ROUTE_OK	0
#define ROUTE_FAIL	(-1)
#define	ROUTE_NOROUTE	(-2)
#define ROUTE_BUSY	(-3)


int tmr_watcher(conn_ctx *ctx, yatemsg *msg, void *arg);
int chan_dtmf_hndlr(conn_ctx *ctx, yatemsg *msg, void *arg, struct dwhook *dwh);
int chan_notify_hndlr(conn_ctx *ctx, yatemsg *msg, void *arg, struct dwhook *dwh);
int call_exec_hndlr(conn_ctx *ctx, yatemsg *msg, void *arg, struct dwhook *dwh);
static int dummy_vprintf(const char *format, va_list ap);
static void new_callid(void);
static void set_state(int newstate);
static void set_state_nolock(int new_state);
static int get_state(void);
static int wait_state_change(int oldstate);
static void run(void);
static void send_progress(void);
static void attach_cpt(char *src, char *maxlen);
static void interdigit_hndlr(union sigval arg);
static void reset_interdigit(int msecs);
static bool timer_armed(void);
static char *get_digits(void);
static int append_digits(char *text);


enum call_state {
	CALL_STATE_INIT = 0,
	CALL_STATE_SEIZED,	/* trunk has been sezed, no dialtone yet */
	CALL_STATE_PROMPT,	/* provide dialtone */
	CALL_STATE_COLLECTING,	/* collecting digits, cease dialtone */
	CALL_STATE_ROUTING,	/* number complete - routing */
	CALL_STATE_NOROUTE,	/* No route */
	CALL_STATE_BUSY		/* destination is busy */
};

/* local static data */

static conn_ctx *staticctx;

static int state = CALL_STATE_INIT;
static pthread_mutex_t	state_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t	new_state_signal = PTHREAD_COND_INITIALIZER;


static char ourcallid[CALL_ID_MAXLEN];
static char *partycallid = NULL;

static timer_t	interdigit;

static pthread_mutex_t data_mutex = PTHREAD_MUTEX_INITIALIZER;
static char called_str[MAX_CALLED_LEN + 1];
static int called_len = 0;
yatemsg	*initial_exec = NULL;


static void new_callid(void)
{
	uuid_t	uuid;
	int	len;
	static const char *idprefix = "ovrlp/";
	
	uuid_generate(uuid);
	memset(ourcallid, 0, CALL_ID_MAXLEN);
	strcpy(ourcallid, idprefix);
	len = strlen(idprefix);
	assert(len < (CALL_ID_MAXLEN - UUID_LEN));
	uuid_unparse_lower(uuid, &ourcallid[len]);
	SYMDEBUG("our call id=%s\n", ourcallid);
}

static void set_state(int new_state)
{
	mutex_lock(&state_mutex);
	set_state_nolock(new_state);
	mutex_unlock(&state_mutex);
}

static void set_state_nolock(int new_state)
{
	int	oldstate;
	
	oldstate = state;
	state = new_state;
	if (new_state != oldstate) pthread_cond_broadcast(&new_state_signal);
}



static int get_state(void)
{
	int	res;
	
	mutex_lock(&state_mutex);
	res = state;
	mutex_unlock(&state_mutex);
	return res;
}

static int wait_state_change(int oldstate)
{
	int	res;
	mutex_lock(&state_mutex);
	while (state == oldstate) {
		res = pthread_cond_wait(&new_state_signal, &state_mutex);
		assert(res == 0);
	}
	res = state;
	mutex_unlock(&state_mutex);
	return res;
}


int call_exec_hndlr(conn_ctx *ctx, yatemsg *msg, void *arg, struct dwhook *dwh)
{
	char	*tmpstr, *callername, *caller;
	int	res;
	void	*ptmp;
	
	SYMINFO("at enter, msg=%p, ctx=%p, arg=%p\n", msg, ctx, arg);
	SYMINFO("arg=%p,\n   message name=\"%s\", time=%s\n", arg, msg->name, get_msg_param(msg, "time"));
	
	res = strcmp(msg->name, "call.execute");
	if (res) {
		SYMINFO("%s received, catchall won't handle it\n", msg->name);
		return YXT_OK;
	}
	mutex_lock(&data_mutex);
	ptmp = initial_exec;
	mutex_unlock(&data_mutex);
	if (ptmp) {
		SYMERROR("another call.execute? WTF?\n");
		return YXT_OK;
	}
//	dump_message(msg);
	if (partycallid) free(partycallid);
	tmpstr = get_msg_param(msg, "id");
	if (tmpstr) partycallid = strdup(tmpstr);
	else partycallid = NULL;
	if (!partycallid) return YXT_OK;
	tmpstr = get_msg_param(msg, "called");
	if (tmpstr) {
		res = strcmp(tmpstr, "off-hook");
		if (res) append_digits(tmpstr);
	}
	caller = get_msg_param(msg, "caller");
	callername = get_msg_param(msg, "callername");
	if ((!caller) && (!callername)) {
		tmpstr = get_msg_param(msg, "address");
		set_msg_param(msg, "callername", tmpstr);
	}
	set_msg_param(msg, "targetid", ourcallid);
	mutex_lock(&data_mutex);
	if (initial_exec) SYMFATAL("race condition hit - initial_exec has been changed under our feets\n");
	initial_exec = copy_message(msg);
	mutex_unlock(&data_mutex);
	dump_message(msg);
	set_state(CALL_STATE_SEIZED);
	return YXT_PROCESSED;
}

static char *get_digits(void)
{
	char	*dst = NULL;
	
	mutex_lock(&data_mutex);
	if (called_len > 0) {
		assert(called_len <= MAX_CALLED_LEN);
		dst = strndup(&called_str[0], called_len);
	}
	mutex_unlock(&data_mutex);
	return dst;
}

static int append_digits(char *text)
{
	int	res;
	int	len = 0;
	int	space;
	
	if (text) len = strlen(text);
	mutex_lock(&data_mutex);
	if (len > 0) {
		space = MAX_CALLED_LEN - called_len;
		if (space > 0) {
			if (len > space) len = space;
			strncpy(&called_str[called_len], text, len);
			called_len += len;
		}
	}
	res = called_len;
	mutex_unlock(&data_mutex);
	return res;
}

int chan_dtmf_hndlr(conn_ctx *ctx, yatemsg *msg, void *arg, struct dwhook *dwh)
{
	int	res;
	int	curstate;
	char	*digits = NULL;
	
	SYMINFO("at enter, msg=%p, ctx=%p, arg=%p\n", msg, ctx, arg);
	SYMINFO("arg=%p,\n   message name=\"%s\", time=%s\n", arg, msg->name, get_msg_param(msg, "time"));
	
	digits = get_msg_param(msg, "text");
	if (!digits) {
		SYMWARNING("chan.dtmf with empty text attr!\n");
		dump_message(msg);
		return YXT_OK;
	}
	curstate = get_state();
	switch (curstate) {
		case CALL_STATE_SEIZED:
			SYMDEBUG("dialed number received before providing dialtone - will route immediately\n");
		case CALL_STATE_PROMPT:
			res = append_digits(digits);
			if ((res >= MAX_CALLED_LEN) || (curstate == CALL_STATE_SEIZED))
				set_state(CALL_STATE_ROUTING);
			else {
				set_state(CALL_STATE_COLLECTING);
				reset_interdigit(INTERDIGIT_TIMEOUT);
			}
			break;
		case CALL_STATE_COLLECTING:
			res = append_digits(digits);
			if (res >= MAX_CALLED_LEN) set_state(CALL_STATE_ROUTING);
			reset_interdigit(INTERDIGIT_TIMEOUT);
			break;
		default:
			SYMDEBUG("chan.dtmf in state %d, ignored\n", curstate);
			return YXT_OK;
	}
	return YXT_PROCESSED;
}


int chan_notify_hndlr(conn_ctx *ctx, yatemsg *msg, void *arg, struct dwhook *dwh)
{
	
	SYMINFO("at enter, msg=%p, ctx=%p, arg=%p\n", msg, ctx, arg);
	SYMINFO("arg=%p,\n   message name=\"%s\", time=%s\n", arg, msg->name, get_msg_param(msg, "time"));

#warning implementation

	return YXT_PROCESSED;
}


static void interdigit_hndlr(union sigval arg)
{
	int	curstate;
	
	mutex_lock(&state_mutex);
	if (timer_armed()) {
		SYMDEBUGHARD("timer rearmed while waiting\n");
		goto out;
	}
	curstate = state;
	switch (curstate) {
			case CALL_STATE_SEIZED:
				set_state_nolock(CALL_STATE_PROMPT);
				break;
			case CALL_STATE_PROMPT:
			case CALL_STATE_COLLECTING:
				set_state_nolock(CALL_STATE_ROUTING);
			default:
				break;
	}
out:
	mutex_unlock(&state_mutex);
}

static void reset_interdigit(int msecs)
{
	struct itimerspec tval;
	int	res = 0;
	int	curstate;
	
	assert(msecs >= 0);
	memset(&tval, 0, sizeof(struct itimerspec));
	tval.it_value.tv_sec = (msecs / 1000);
	tval.it_value.tv_nsec = (msecs % 1000) * 1000000;
	mutex_lock(&state_mutex);
	curstate = state;
	if ((curstate == CALL_STATE_SEIZED) ||
		(curstate == CALL_STATE_PROMPT) ||
		(curstate == CALL_STATE_COLLECTING)) {
		res = timer_settime(interdigit, 0, &tval, NULL);
		mutex_unlock(&state_mutex);
	} else {
		mutex_unlock(&state_mutex);
		SYMWARNING("refuse to reset timer in state %d\n", curstate);
	}
	if (res) SYMFATAL("cannot set timer:%s\n", STRERROR_R(errno));
}


static bool timer_armed(void)
{
	struct itimerspec tval;
	int	res;
	bool	armed = false;
	
	memset(&tval, 0, sizeof(struct itimerspec));
	res = timer_gettime(interdigit, &tval);
	if (res) SYMFATAL("cannot get timer value:%s\n", STRERROR_R(errno));
	assert(tval.it_interval.tv_sec == 0);
	assert(tval.it_interval.tv_nsec == 0);
	armed = ((tval.it_value.tv_sec != 0) || (tval.it_value.tv_nsec != 0));
	return armed;
}



static int dummy_vprintf(const char *format, va_list ap)
{
	return 0;
}


int main(int argc, char **argv)
{
	int	res;
	struct sigevent sevp;

	/* cannot trace to stdout - syslog only */
	symtrace_hookctl(true, dummy_vprintf);
	SYMINFO("about to start symbiont connection...\n");
	staticctx = yxt_conn_fds();
	if (!staticctx) {
		SYMFATAL("cannot create context\n");
		goto out;
	}
	memset(called_str, 0, MAX_CALLED_LEN + 1);
	new_callid();
	
	res = yxt_add_default_handler(staticctx, call_exec_hndlr, NULL);
	assert(res == YXT_OK);
	SYMINFO("catchall handler installed\n");
	res = yxt_run(staticctx, 3);
	assert(res == YXT_OK);

	res = yxt_set_param(staticctx, "trackparam", "overlapped");
	assert(res == YXT_OK);

	memset(&sevp, 0, sizeof(struct sigevent));
	sevp.sigev_notify = SIGEV_THREAD;
	sevp.sigev_value.sival_ptr = NULL;
	sevp.sigev_notify_function = interdigit_hndlr;
	res = timer_create(CLOCK_MONOTONIC, &sevp, &interdigit);
	assert(res == 0);

	res = yxt_add_handler_filtered(staticctx, chan_dtmf_hndlr, "chan.dtmf", 95, 
				"targetid", ourcallid, NULL);
	assert(res == YXT_OK);
	res = yxt_add_handler_filtered(staticctx, chan_notify_hndlr, "chan.notify", 95, 
				"targetid", ourcallid, NULL);
	assert(res == YXT_OK);
	run();
	
out:
	return 0;
}

/* attach call progress tone */
static void attach_cpt(char *src, char *maxlen)
{
	yatemsg *msg = NULL;
	yatemsg *reply = NULL;
	int	res;
	
	assert(src);
	msg = alloc_message("chan.attach");
	assert(msg);
	set_msg_param(msg, "id", ourcallid);
	set_msg_param(msg, "source", src);
	set_msg_param(msg, "notify", ourcallid);
	if (maxlen) set_msg_param(msg, "maxlen", maxlen);
	set_msg_param(msg, "single", "true");
	res = yxt_dispatch(staticctx, msg, &reply);
	SYMINFO("attach to %s dispatched, res=%d\n", src, res);
	if (reply) dump_message(reply);
	else SYMINFO("No reply to attach to %s received\n", src);
	if (reply) free_message(reply);
	if (msg) free_message(msg);
}

void send_progress(void) 
{
	yatemsg *msg = NULL;
	yatemsg *reply = NULL;
	int	res;

	msg = alloc_message("call.progress");
	assert(msg);
	set_msg_param(msg, "id", ourcallid);
	set_msg_param(msg, "peerid", partycallid);
	set_msg_param(msg, "earlymedia", "true");
	res = yxt_dispatch(staticctx, msg, &reply);
	SYMDEBUG("call.progress dispatched, res=%d\n", res);
	if (reply) dump_message(reply);
	else SYMINFO("No reply to call.progress received\n");
	if (reply) free_message(reply);
	if (msg) free_message(msg);

}

static int route_call(char *dst)
{
	yatemsg	*msg = NULL;
	yatemsg	*reply = NULL;
	char	*tmp;
	char	*callto = NULL;
	char	*error = NULL;
	char	*cause = NULL;
	bool	autoanswer = false;
	int	res;
	int	result = ROUTE_NOROUTE;

	assert(dst);
	msg = alloc_message("call.route");
	assert(msg);
	assert(initial_exec);
	mutex_lock(&data_mutex);
	copy_msg_params(msg, initial_exec);
	mutex_unlock(&data_mutex);
	remove_msg_param(msg, "callto");
	remove_msg_param(msg, "handlers");
	set_msg_param(msg, "called", dst);
	set_msg_param(msg, "overlapped", "no");
	yxt_dispatch(staticctx, msg, &reply);
	SYMINFO("routing call to \"%s\"\n", dst);
	free_message(msg);
	msg = NULL;
	if (!reply) {
		SYMINFO("No route to \"%s\"\n", dst); 
		goto	errout;
	}
	error = get_msg_param(reply, "error");
	callto = get_msg_retvalue(reply);
	if (!callto) goto errout;
	if (strcmp(callto, "-") == 0) goto errout;
	if (strcmp(callto, "error") == 0) goto errout;
	
	/* check returned dst and copy it to callto */
	tmp = get_msg_param(reply, "autoanswer");
	autoanswer = tmp ? is_true(tmp) : false;
	if (autoanswer) {
		msg = alloc_message("call.answered");
		assert(msg);
		set_msg_param(msg, "id", ourcallid);
		set_msg_param(msg, "targetid", partycallid);
		SYMINFO("enqueueing autoanswer...\n");
		yxt_enqueue(staticctx, msg);
		free_message(msg);
		msg = NULL;
	}
	
	msg = alloc_message("chan.masquerade");
	assert(msg);
	copy_msg_params(msg, reply);
	remove_msg_param(msg, "handlers");
//	remove_msg_param(msg, "targetid");
	set_msg_param(msg, "message", "call.execute");
	set_msg_param(msg, "complete_minimal", "true");
	if (partycallid) set_msg_param(msg, "id", partycallid);
	assert(callto);
	set_msg_param(msg, "callto", callto);
	if (reply) free_message(reply);
	reply = NULL;
	yxt_dispatch(staticctx, msg, &reply);
/* overlapped process is terminated at this point by yate if everything
 * went OK. Otherwise handle an error
 */
	if (reply) {
#ifdef DEBUG_ME_HARDER
		SYMDEBUGHARD("masqueraded call.execute reply\n");
		dump_message(reply);
#endif
		error = NULL;
		cause = NULL;
		error = get_msg_param(reply, "error");
		cause = get_msg_param(reply, "cause");
		if (error) {
			SYMINFO("call to %s failed, error=%s, cause=%s\n",
				dst, error, cause);
			res = strcmp(error, "busy");
			if (res == 0) result = ROUTE_BUSY;
			goto errout;
		}
	} else SYMDEBUG("no reply to masqueraded call.execute\n");
	if (reply) free_message(reply);
	if (msg) free_message(msg);
	return YXT_OK;
errout:
	SYMINFO("%s \"%s\", reason=\"%s\"\n", 
	((result != ROUTE_BUSY) ? "no route to " : "busy - "), dst, error);
	res = yxt_set_param(staticctx, "reason", error ? error : "noroute" );
	assert(res == YXT_OK);
	if (reply) free_message(reply);
	if (msg) free_message(msg);
	return result;
}

static void run(void)
{
	int tmpstate = CALL_STATE_INIT;
	int res;
	char	*routedst;
	bool	media = false;
	
	for (;;) {
		tmpstate = wait_state_change(tmpstate);
		switch (tmpstate) {
			case CALL_STATE_INIT:
					break;
			case CALL_STATE_SEIZED:
					reset_interdigit(INFORMATION_TIMEOUT);
					break;
			case CALL_STATE_PROMPT:
					reset_interdigit(FIRST_DIGIT_TIMEOUT);
					attach_cpt("tone/dial", NULL);
					send_progress();
					media = true;
					break;
			case CALL_STATE_COLLECTING:
					attach_cpt("tone/noise", NULL);
					break;
			case CALL_STATE_ROUTING:
					attach_cpt("tone/noise", NULL);
					if (!media) {
						send_progress();
						media = true;
					}
					routedst = get_digits();
					if (!routedst) {
						set_state(CALL_STATE_NOROUTE);
					} else {
						res = route_call(routedst);
						if (res == ROUTE_BUSY) set_state(CALL_STATE_BUSY);
						else if (res != ROUTE_OK) set_state(CALL_STATE_NOROUTE);
					}
					break;
			case CALL_STATE_NOROUTE:
					attach_cpt("tone/info", "32000");
					if (!media) {
						send_progress();
						media = true;
					}
					break;
			case CALL_STATE_BUSY:
					attach_cpt("tone/busy", "32000");
					if (!media) {
						send_progress();
						media = true;
					}
					break;
		}
	}

}


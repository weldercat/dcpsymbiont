/*
* Copyright 2017,2018  Stacy <stacy@sks.uz>
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
#include <assert.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <stdio.h>
#include <uuid/uuid.h>

#define	DEBUG_ME_HARDER		1

#include <symbiont/symerror.h>
#include <symbiont/yxtlink.h>
#include <symbiont/mmi.h>
#include <symbiont/mmi_print.h>
#define NOFAIL_LOCK_UNNEEDED	1
#include <symbiont/nofail_wrappers.h>
#include "call_control.h"
#include "global_lookup.h"
#include "classifiers.h"
#include "cctl_misc.h"
#include "station_control.h"

#define FIRST_DIGIT_TIMEOUT	10000
#define INTERDIGIT_TIMEOUT	3000
#define INIT_POLL_TIMEOUT	3000
#define ANSWERED_DISPLAY_TIMER	500
#define DROP_TIMEOUT		500
#define SIT_TIMEOUT		10000
#define INCOMING_SEIZURE_TIMEOUT 1500

#define IMT_CALLTO		"imt/imtdcp"
#define MAX_NEW_CALLTO	128

struct runner_arg {
	struct symline *sl;
	yatemsg	*msg;
};


static void line_tmr_hndlr(union sigval arg);
static int mmi_send(struct symline *sl, struct mmi_command *cmd);
static int mmi_send_noargs(struct symline *sl, int cmdtype);
static void clear_digits(struct symline *sl);
static void add_digits(struct symline *sl, char *text);
static int provide_dialtone(struct symline *sl, char *resource);
static int do_hangup(struct symline *sl, bool send_onhook, bool force_onhook);
static void set_callername(struct symline *sl, yatemsg *msg);
static int attach_cpt(struct symline *sl, char *resource, int msecs);
static int out_call_exec(struct symline *sl, char *dest, bool direct);
static int out_call_replace(struct symline *sl, char *dest, yatemsg *copy);
static int transfer_party(struct symline *sl, char *dest, char *reason);
static int routed_call(struct symline *sl, char *dest);
static int call_drop(struct symline *sl, char *reason);
static void display_connected(struct symline *sl);
static void display_callername(struct symline *sl, char *name);
static void display_callernum(struct symline *sl, char *name);
static void display_called(struct symline *sl);
static void erase_display(struct symline *sl);
static void display_text(struct symline *sl, char *text);
static int init_mmi(struct symline *sl);
static void status_led(struct symline *sl, int mode);
static void select_led(struct symline *sl, int mode);
static void set_selected(struct symline *sl, bool selected);
static bool check_isblfno(struct mmi_event *evt, int blfno);
static void new_calltrack(struct symline *sl);
static int pickup_held(struct symline *sl);
static char extract_pad_digit(struct mmi_event *evt);
static int cctl_hangup_nolock(struct symline *sl);
static int cctl_hold_nolock(struct symline *sl);
/* print debug for state transitions caused by various events */
static void dbg_xbyevt(struct symline *sl, struct mmi_event *evt, int oldstate, int newstate);
static void dbg_xbymsg(struct symline *sl, yatemsg *msg, int oldstate, int newstate);
static void dbg_ext(struct symline *sl, char *text, int res, int oldstate, int newstate);
static void dbg_tmr(struct symline *sl, int oldstate, int newstate);

/* state change helpers */
static void enter_sit(struct symline *sl, char *dest);
static void enter_drop(struct symline *sl);
static void enter_dialtone_or_sit(struct symline *sl);
static void cease_ringing(struct symline *sl);
static void answer_ringing(struct symline *sl);
static void send_ringing(struct symline *sl, yatemsg *copyfrom);
static void enter_hold(struct symline *sl);

/* state handlers */
static void state_idle(struct symline *sl, struct mmi_event *evt);
static void state_dialtone(struct symline *sl, struct mmi_event *evt);
static void state_collect(struct symline *sl, struct mmi_event *evt);
static void state_sit(struct symline *sl, struct mmi_event *evt);
static void state_calling(struct symline *sl, struct mmi_event *evt);
static void state_connected(struct symline *sl, struct mmi_event *evt);
static void state_drop(struct symline *sl, struct mmi_event *evt);

/* yate message processing */
static struct runner_arg *make_runner_arg(struct symline *sl, yatemsg *msg);
void process_chan_dtmf(void *arg);
void process_call_answered(struct symline *sl, yatemsg *msg);
int process_call_execute(struct symline *sl, yatemsg *msg);
void process_chan_disconnect(void *arg);
int preprocess_chan_startup(struct symline *sl, yatemsg *msg);
void process_chan_startup(void *arg);
void process_chan_hangup(void *arg);


static void line_tmr_hndlr(union sigval arg)
{
	struct symline *sl;
	int	res;
	int	oldstate;
	
	sl = arg.sival_ptr;
	assert(sl);
	mutex_lock(&sl->lmx);
	oldstate = sl->state;
	if (timer_armed(sl->tmr)) {
		SYMDEBUG("timer rearmed while waiting\n");
	} else {
		switch (sl->state) {
			case SL_STATE_DIALTONE:
			case SL_STATE_COLLECTING:
				if (sl->digits_len > 0) {
					res = routed_call(sl, &sl->digits[0]);
					if (res == SYM_OK) {
						sl->state = SL_STATE_CALLING;
						break;
					}
				}
				enter_sit(sl, "tone/info");
				break;
			case SL_STATE_CONNECTED:
				display_connected(sl);
				break;
			case SL_STATE_DROP:
				if (sl->offhook) {
					res = provide_dialtone(sl, NULL);
					if (res == SYM_OK) {
						clear_digits(sl);
						sl->state = SL_STATE_DIALTONE;
						reset_timer(sl->tmr, FIRST_DIGIT_TIMEOUT);
						break;
					}
				}
				sl->state = SL_STATE_IDLE;
				break;
			case SL_STATE_INCOMING:
				call_drop(sl, "timeout");
				sl->state = SL_STATE_IDLE;
			case SL_STATE_SIT:
				do_hangup(sl, true, true);
				sl->state = SL_STATE_IDLE;
				break;
			default:
				break;
		}
	}
	dbg_tmr(sl, oldstate, sl->state);
	mutex_unlock(&sl->lmx);
}

static int mmi_send(struct symline *sl, struct mmi_command *cmd)
{
	int	res;
	struct ccstation *st;
	
	assert(sl);
	assert(cmd);
	st = sl->ccst;
	assert(st);
	res = st_mmi_send(st, cmd, sl->number);
	if (res != SYM_OK) res = SYM_FAIL;
	return res;
}

struct symline *new_symline(void)
{
	struct symline *sl = NULL;
	struct sigevent sevp;
	int res;
	
	sl = malloc(sizeof(struct symline));
	if (!sl) {
		SYMERROR("cannot allocate memory\n");
		return NULL;
	}
	memset(sl, 0, sizeof(struct symline));
	sl->state = SL_STATE_DISABLED;
	res = pthread_mutex_init(&sl->lmx, NULL);
	assert(res == 0);
	memset(&sevp, 0, sizeof(struct sigevent));
	sevp.sigev_notify = SIGEV_THREAD;
	sevp.sigev_value.sival_ptr = sl;
	sevp.sigev_notify_function = line_tmr_hndlr;
	res = timer_create(CLOCK_MONOTONIC, &sevp, &sl->tmr);
	assert(res == 0);
	sl->magic = LINE_MAGIC;
	return sl;
}


#define CALL_ID_MAXLEN	256
#define UUID_LEN	36

static void new_calltrack(struct symline *sl)
{
	uuid_t	uuid;
	int	len;
	static const char *idprefix = "dcp/";
	char	track_id[CALL_ID_MAXLEN];
	
	assert(sl);
	uuid_generate(uuid);
	memset(track_id, 0, CALL_ID_MAXLEN);
	strcpy(track_id, idprefix);
	len = strlen(idprefix);
	assert(len < (CALL_ID_MAXLEN - UUID_LEN));
	uuid_unparse_lower(uuid, &track_id[len]);
	SYMDEBUG("call tracking id=%s\n", track_id);
	if (sl->calltrack) {
		update_cdb(CDB_ID_CALLTRACK, sl->calltrack, NULL);
		free(sl->calltrack);
	}
	sl->calltrack = strdup(track_id);
	update_cdb(CDB_ID_CALLTRACK, sl->calltrack, sl);
}



int cctl_enable(struct symline *sl)
{
	int res = CCTL_FAIL;
	int	oldstate;
	
	assert(sl);
	mutex_lock(&sl->lmx);
	oldstate = sl->state;
	if (sl->state == SL_STATE_DISABLED) {
		if (!sl->name) {
			SYMERROR("cannot enable line with no name\n");
			goto out;
		}
		if (!sl->ccst) {
			SYMERROR("cannot enable line with no pointer to controlling station\n");
			goto out;
		}
		if (!sl->ccst->bchan) {
			SYMERROR("cannot enable line with no B-chan for controlling station\n");
			goto out;
		}
		sl->state = SL_STATE_IDLE;
		reset_timer(sl->tmr, 0);
		set_selected(sl, false);
		init_mmi(sl);
		res = CCTL_OK;
	}
out:
	dbg_ext(sl, "enable", res, oldstate, sl->state);
	mutex_unlock(&sl->lmx);
	return res;
}

int cctl_disable(struct symline *sl)
{
	int	oldstate;
	assert(sl);
	mutex_lock(&sl->lmx);
	oldstate = sl->state;
	cctl_hangup_nolock(sl);
	set_selected(sl, false);
	sl->state = SL_STATE_DISABLED;	
	dbg_ext(sl, "disable", CCTL_OK, oldstate, sl->state);
	mutex_unlock(&sl->lmx);
	return CCTL_OK;
}


/* all types of off-hook - blf press, unhold etc */
int cctl_select(struct symline *sl)
{
	int	res = CCTL_FAIL;
	int	oldstate;

	assert(sl);
	mutex_lock(&sl->lmx);
	oldstate = sl->state;
	switch (sl->state) {
		case SL_STATE_IDLE:
			res = provide_dialtone(sl, NULL);
			if (res != SYM_OK) break;
			clear_digits(sl);
			sl->state = SL_STATE_DIALTONE;
			reset_timer(sl->tmr, FIRST_DIGIT_TIMEOUT);
			res = CCTL_OK;
			break;
		case SL_STATE_HOLD:
			mmi_send_noargs(sl, MMI_CMD_OFFHOOK);
			if (!sl->outgoing) {
				display_callername(sl, NULL);
				display_callernum(sl, NULL);
			} else display_called(sl);
			sl->offhook = true;
			pickup_held(sl);
			res = CCTL_OK;
			break;
		case SL_STATE_RING:
			if (!sl->offhook) {
				res = mmi_send_noargs(sl, MMI_CMD_OFFHOOK);
				if (res != SYM_OK) break;
			}
			sl->offhook = true;
			answer_ringing(sl);
			res = CCTL_OK;
			break;
		default:
			break;
	}
	dbg_ext(sl, "select", res, oldstate, sl->state);
	mutex_unlock(&sl->lmx);
	return res;
}


int cctl_unselect(struct symline *sl)
{
	int	res = CCTL_FAIL;
	int	oldstate;
	
	assert(sl);
	mutex_lock(&sl->lmx);
	oldstate = sl->state;
	switch (sl->state) {
		case SL_STATE_DISABLED:
		case SL_STATE_IDLE:
			if (sl->selected) res = CCTL_OK;
			break;
		case SL_STATE_DIALTONE:
		case SL_STATE_COLLECTING:
		case SL_STATE_CALLING:
			res = cctl_hangup_nolock(sl);
			break;
		case SL_STATE_CONNECTED:
			res = cctl_hold_nolock(sl);
			break;
		case SL_STATE_HOLD:
			if (sl->selected) res = CCTL_OK;
			break;
		default:
			break;
	}
	if (res == CCTL_OK) set_selected(sl, false);
	dbg_ext(sl, "unselect", res, oldstate, sl->state);
	mutex_unlock(&sl->lmx);
	return res;
}

int cctl_hold(struct symline *sl)
{
	int	res;
	int	oldstate;
	
	assert(sl);
	mutex_lock(&sl->lmx);
	oldstate = sl->state;
	res = cctl_hold_nolock(sl);
	dbg_ext(sl, "hold", res, oldstate, sl->state);
	mutex_unlock(&sl->lmx);
	return res;
}

/* put the line on hold */
static int cctl_hold_nolock(struct symline *sl)
{
	int res = CCTL_FAIL;
	assert(sl);
	if (sl->state == SL_STATE_CONNECTED) {
		res = transfer_party(sl, "moh/default", "hold");
		if (res == SYM_OK) {
			enter_hold(sl);
			res =CCTL_OK;
		}
	}
	return res;
}
/* hangup */
int cctl_hangup(struct symline *sl)
{
	int	res;
	int	oldstate;
	
	assert(sl);
	mutex_lock(&sl->lmx);
	oldstate = sl->state;
	res = cctl_hangup_nolock(sl);
	dbg_ext(sl, "hangup", res, oldstate, sl->state);
	mutex_unlock(&sl->lmx);
	return res;
}


static int cctl_hangup_nolock(struct symline *sl)
{
	bool	need_hangup = false;
	int	res = CCTL_OK;

	assert(sl);
	switch (sl->state) {
		case SL_STATE_DISABLED:
		case SL_STATE_IDLE:
			res = CCTL_FAIL;
			break;
		case SL_STATE_HOLD:
			need_hangup = true;
			sl->outgoing = false;
		case SL_STATE_INCOMING:
			call_drop(sl, "reject");
			reset_timer(sl->tmr, 0);
			enter_sit(sl, "tone/busy");
			sl->state = SL_STATE_IDLE;
			res = CCTL_OK;
			break;
		case SL_STATE_RING:
			cease_ringing(sl);
			res = CCTL_OK;
			break;
		default:
			need_hangup = true;
			break;
	}
	if (need_hangup) {
		do_hangup(sl, false, true);
		sl->state = SL_STATE_IDLE;
		reset_timer(sl->tmr, 0);
		res =CCTL_OK;
	};
	return res;
}

static void clear_digits(struct symline *sl)
{
	assert(sl);
	sl->digits_len = 0;
	memset(&(sl->digits[0]), 0, MAX_NUMBER_LENGTH);
}

static void add_digits(struct symline *sl, char *text)
{
	int	txtlen = -1;
	int	space;
	
	assert(sl);
	assert(text);
	txtlen = strlen(text);
	if (txtlen <= 0) return;
	space = MAX_NUMBER_LENGTH - sl->digits_len;
	if (space <= 0) return;
	if (txtlen > space) txtlen = space;
	strncpy(&sl->digits[sl->digits_len], text, txtlen);
	sl->digits_len += txtlen;
	if (sl->softecho) display_text(sl, text);
}


#define PAD_PFX_LEN	4
#define PAD_CHAR_POS	PAD_PFX_LEN
#define PAD_DIGIT_LEN	(PAD_PFX_LEN + 1)
static char extract_pad_digit(struct mmi_event *evt)
{
	char	digit = '\000';
	const 	char *prefix = "pad/";
	
	assert(evt);
	if (evt->type != MMI_EVT_PRESS) return digit;
	if ((bisstemeqblk(evt->ctlname, prefix, PAD_PFX_LEN) == 1) && 
			(blength(evt->ctlname) == PAD_DIGIT_LEN)) {
		digit = (bdata(evt->ctlname))[PAD_CHAR_POS];
	}
	return digit;
}

#define MAX_DLEN	10
static int attach_cpt(struct symline *sl, char *resource, int msecs)
{
	conn_ctx *ctx;
	yatemsg *msg = NULL;
	yatemsg *reply = NULL;
	int res;
	char duration[MAX_DLEN];

	assert(sl);
	assert(resource);
	ctx = yctx_for_line(sl);
	if (!ctx) {
		SYMERROR("no yxt context for line %s\n", sl->name);
		return SYM_FAIL;
	}
	if (!sl->partycallid) {
		SYMERROR("no peer call id in context for line %s\n", sl->name);
		return SYM_FAIL;
	}
	memset(duration, 0, MAX_DLEN);
	snprintf(duration, MAX_DLEN - 1, "%d", msecs);
	msg = alloc_message("chan.masquerade");
	assert(msg);
	set_msg_param(msg, "message", "chan.attach");
	set_msg_param(msg, "id", sl->partycallid);
	set_msg_param(msg, "source", resource);
//	set_msg_param(msg, "notify", sl->ourcallid);
	if (msecs > 0) set_msg_param(msg, "maxlen", duration);
	set_msg_param(msg, "single", "true");
	res = yxt_dispatch(ctx, msg, &reply);
	SYMINFO("attach to %s dispatched, res=%d\n", resource, res);
#ifdef DEBUG_ME_HARDER
	if (reply) dump_message(reply);
#endif
	if (!reply) SYMINFO("No reply to attach to %s received\n", resource);
	if (reply) free_message(reply);
	if (msg) free_message(msg);
	if (reply) return SYM_OK;
	else return SYM_FAIL;
}

static void set_callername(struct symline *sl, yatemsg *msg)
{
	char	*caller = NULL;
	char	*slashp = NULL;
	
	assert(sl);
	assert(msg);
	if (sl->name) {
		slashp = strrchr(sl->name, '/');
		if (slashp) {
			slashp++;
			if (slashp) caller = strdup(slashp);
		} 
	}
	if (caller) {
		set_msg_param(msg, "caller", caller);
		set_msg_param(msg, "callername", sl->name);
	} else {
		set_msg_param(msg, "caller", sl->name);
	}
	if (caller) free(caller);
}


static int out_call_exec(struct symline *sl, char *dest, bool direct)
{
	conn_ctx *ctx;
	yatemsg *msg = NULL;
	yatemsg *reply = NULL;
	int res;
	char	*tmpstr = NULL;

	assert(sl);
	assert(dest);
	ctx = yctx_for_line(sl);
	if (!ctx) {
		SYMERROR("no yxt context for line %s\n", sl->name);
		return SYM_FAIL;
	}
	if (sl->ourcallid) {
		SYMWARNING("our call id is still set (%s)\n", sl->ourcallid);
		update_cdb(CDB_ID_OUR, sl->ourcallid, NULL);
		free(sl->ourcallid);
		sl->ourcallid = NULL;
	}
	if (sl->partycallid) {
		SYMWARNING("party call id is still set (%s)\n", sl->partycallid);
		update_cdb(CDB_ID_PARTY, sl->partycallid, NULL);
		free(sl->partycallid);
		sl->partycallid = NULL;
	}
	if (sl->caller) {
		free(sl->caller);
		sl->caller = NULL;
	}
	if (sl->callername) {
		free(sl->callername);
		sl->callername = NULL;
	}
	new_calltrack(sl);
	msg = alloc_message("call.execute");
	assert(msg);

	set_msg_param(msg, "symbiont_id", sl->calltrack);
	set_msg_param(msg, "callto", IMT_CALLTO);
	set_msg_param(msg, "address", sl->ccst->bchan);
	set_callername(sl, msg);
	set_msg_param(msg, "target", dest);
	if (direct) set_msg_param(msg, "direct", dest);
	res = yxt_dispatch(ctx, msg, &reply);
	SYMINFO("call.execute to %s dispatched, res=%d\n", dest, res);
#ifdef DEBUG_ME_HARDER
	if (reply) dump_message(reply);
#endif
	if (!reply) SYMINFO("No reply to call.execute to %s received\n", dest);
	if (reply) {
		sl->outgoing = true;
		tmpstr = get_msg_param(reply, "id");
		if (tmpstr) {
			sl->ourcallid = strdup(tmpstr);
			update_cdb(CDB_ID_OUR, sl->ourcallid, sl);
		}
		tmpstr = get_msg_param(reply, "peerid");
		if (tmpstr) {
			sl->partycallid = strdup(tmpstr);
			update_cdb(CDB_ID_PARTY, sl->partycallid, sl);
		}
		free_message(reply);
	}
	if (msg) free_message(msg);
	if (reply) return SYM_OK;
	else return SYM_FAIL;
}

static int out_call_replace(struct symline *sl, char *dest, yatemsg *copy)
{
	conn_ctx *ctx;
	yatemsg *msg = NULL;
	yatemsg *reply = NULL;
	int res;

	assert(sl);
	assert(dest);
	ctx = yctx_for_line(sl);
	if (!ctx) {
		SYMERROR("no yxt context for line %s\n", sl->name);
		return SYM_FAIL;
	}
	if (!sl->ourcallid) {
		SYMERROR("cannot replace - no our call id\n");
		return SYM_FAIL;
	}
	
	msg = alloc_message("chan.masquerade");
	assert(msg);
	set_msg_param(msg, "message", "call.execute");
	set_msg_param(msg, "id", sl->ourcallid);
	set_msg_param(msg, "callto", dest);
	if (copy) {
		copy_msg_params(msg, copy);
		remove_msg_param(msg, "handlers");
	}
	res = yxt_dispatch(ctx, msg, &reply);
	SYMINFO("masqueraded call.execute to %s dispatched (id=%s), res=%d\n", 
			dest, sl->ourcallid, res);
#ifdef DEBUG_ME_HARDER
	if (reply) dump_message(reply);
#endif
	if (!reply) SYMINFO("No reply to call.execute to %s received\n", dest);
	if (reply) free_message(reply);
	if (msg) free_message(msg);
	if (reply) return SYM_OK;
	else return SYM_FAIL;
}

static int transfer_party(struct symline *sl, char *dest, char *reason)
{
	conn_ctx *ctx;
	yatemsg *msg = NULL;
	yatemsg *reply = NULL;
	int res;

	assert(sl);
	assert(dest);
	ctx = yctx_for_line(sl);
	if (!ctx) {
		SYMERROR("no yxt context for line %s\n", sl->name);
		return SYM_FAIL;
	}
	if (!sl->partycallid) {
		SYMERROR("cannot transfer - no party call id\n");
		return SYM_FAIL;
	}
	
	msg = alloc_message("chan.masquerade");
	assert(msg);
	set_msg_param(msg, "message", "call.execute");
	set_msg_param(msg, "id", sl->partycallid);
	set_msg_param(msg, "callto", dest);
	if (reason) set_msg_param(msg, "reason", reason);
	set_msg_param(msg, "symbiont_id", sl->calltrack);

	res = yxt_dispatch(ctx, msg, &reply);
	SYMINFO("masqueraded call.execute (transfer to %s) dispatched (id=%s), res=%d\n", 
			dest, sl->partycallid, res);
#ifdef DEBUG_ME_HARDER
	if (reply) dump_message(reply);
#endif
	if (!reply) SYMINFO("No reply to call.execute to %s received\n", dest);
	if (reply) free_message(reply);
	if (msg) free_message(msg);
	if (reply) return SYM_OK;
	else return SYM_FAIL;
}

static int routed_call(struct symline *sl, char *dest)
{
	conn_ctx *ctx;
	yatemsg *msg = NULL;
	yatemsg *reply = NULL;
	int res;
	char	*callto = NULL;

	assert(sl);
	assert(dest);
	ctx = yctx_for_line(sl);
	if (!ctx) {
		SYMERROR("no yxt context for line %s\n", sl->name);
		return SYM_FAIL;
	}
	msg = alloc_message("call.route");
	assert(msg);
	set_msg_param(msg, "driver", "dcp");
	set_msg_param(msg, "id", sl->ourcallid);
	set_msg_param(msg, "called", dest);
	set_callername(sl, msg);
	res = yxt_dispatch(ctx, msg, &reply);
	SYMINFO("call.route to %s dispatched, res=%d\n", dest, res);
#ifdef DEBUG_ME_HARDER
	if (reply) dump_message(reply);
#endif
	if (!reply) {
		SYMINFO("No route to %s received\n", dest);
		goto errout;
	}
	callto = get_msg_retvalue(reply);
	
	if (!callto) goto errout;
	if (strcmp(callto, "-") == 0) goto errout;
	if (strcmp(callto, "error") == 0) goto errout;
	res = out_call_replace(sl, callto, reply);
	goto out;
errout:
	res = SYM_FAIL;
out:
	if (msg) free_message(msg);
	if (reply) free_message(reply);
	return res;
}

static int call_drop(struct symline *sl, char *reason)
{
	conn_ctx *ctx;
	yatemsg *msg = NULL;
	yatemsg *reply = NULL;
	int res;

	assert(sl);
	ctx = yctx_for_line(sl);
	if (!ctx) {
		SYMERROR("no yxt context for line %s\n", sl->name);
		return SYM_FAIL;
	}
	if (!(sl->outgoing ? sl->ourcallid : sl->partycallid)) {
		SYMERROR("no call id - nothing to drop\n");
		return SYM_FAIL;
	}
	
	msg = alloc_message("call.drop");
	assert(msg);
	set_msg_param(msg, "id", (sl->outgoing ? sl->ourcallid : sl->partycallid));
	if (reason) set_msg_param(msg, "reason", reason);

	res = yxt_dispatch(ctx, msg, &reply);
	SYMINFO("call.drop dispatched (id=%s), reason=%s res=%d\n", 
			(sl->outgoing ? sl->ourcallid : sl->partycallid), 
			reason, res);
#ifdef DEBUG_ME_HARDER
	if (reply) dump_message(reply);
#endif
	if (!reply) SYMINFO("No reply to call.drop for %s received\n", 
				(sl->outgoing ? sl->ourcallid : sl->partycallid));
	if (reply) free_message(reply);
	if (msg) free_message(msg);
	if (sl->ourcallid) {
		update_cdb(CDB_ID_OUR, sl->ourcallid, NULL);
		free(sl->ourcallid);
		sl->ourcallid = NULL;
	}
	if (sl->partycallid) {
		update_cdb(CDB_ID_OUR, sl->partycallid, NULL);
		free(sl->partycallid);
		sl->partycallid = NULL;
	}
	if (sl->calltrack) {
		update_cdb(CDB_ID_CALLTRACK, sl->calltrack, NULL);
		free(sl->calltrack);
		sl->calltrack = NULL;
	}
	if (reply) return SYM_OK;
	else return SYM_FAIL;
}

static void display_connected(struct symline *sl)
{
	struct mmi_command cmd;
	int	res;

	memset(&cmd, 0, sizeof(struct mmi_command));
	cmd.type = MMI_CMD_TEXT;
	cmd.arg.text_arg.erase = MMI_ERASE_NONE;
	cmd.arg.text_arg.row = 0;
	cmd.arg.text_arg.col = 15;
	cmd.arg.text_arg.text = bformat("connected");
	res = mmi_send(sl, &cmd);
	bdestroy(cmd.arg.text_arg.text);
	if (res != SYM_OK) {
		SYMERROR("send error\n");
	}
}

static void display_callername(struct symline *sl, char *name)
{
	struct mmi_command cmd;
	
	assert(sl);
	if (name) {
		if (sl->callername) free(sl->callername);
		sl->callername = strdup(name);
	}
	if (sl->callername) {
		memset(&cmd, 0, sizeof(struct mmi_command));
		cmd.type = MMI_CMD_TEXT;
		cmd.arg.text_arg.erase = MMI_ERASE_NONE;
		cmd.arg.text_arg.row = 0;
		cmd.arg.text_arg.col = 0;
		cmd.arg.text_arg.text = bformat("%s", sl->callername);
		(void)mmi_send(sl, &cmd);
		bdestroy(cmd.arg.text_arg.text);
	} 
}

static void display_callernum(struct symline *sl, char *caller)
{
	struct mmi_command cmd;
	assert(sl);
		
	if (caller) {
		if (sl->caller) free(sl->caller);
		sl->caller = strdup(caller);
	}
	memset(&cmd, 0, sizeof(struct mmi_command));
	cmd.type = MMI_CMD_TEXT;
	cmd.arg.text_arg.erase = MMI_ERASE_NONE;
	cmd.arg.text_arg.row = 0;
	cmd.arg.text_arg.col = 24;
	cmd.arg.text_arg.text = bformat("%s", (sl->caller ? sl->caller : "unknown"));
	(void)mmi_send(sl, &cmd);
	bdestroy(cmd.arg.text_arg.text);
}

static void display_called(struct symline *sl)
{
	struct mmi_command cmd;
	assert(sl);
		
	memset(&cmd, 0, sizeof(struct mmi_command));
	cmd.type = MMI_CMD_TEXT;
	cmd.arg.text_arg.erase = MMI_ERASE_TAIL;
	cmd.arg.text_arg.row = 0;
	cmd.arg.text_arg.col = 0;
	cmd.arg.text_arg.text = bformat("%c=", sl->number + 'a');
	bcatblk(cmd.arg.text_arg.text, sl->digits, sl->digits_len);
	(void)mmi_send(sl, &cmd);
	bdestroy(cmd.arg.text_arg.text);
}


/* prints the text in the current cursor position */
static void display_text(struct symline *sl, char *text)
{
	struct mmi_command cmd;
	assert(sl);
	assert(text);
		
	memset(&cmd, 0, sizeof(struct mmi_command));
	cmd.type = MMI_CMD_TEXT;
	cmd.arg.text_arg.erase = MMI_ERASE_NONE;
	cmd.arg.text_arg.row = TEXT_CONTINUE;
	cmd.arg.text_arg.col = TEXT_CONTINUE;
	cmd.arg.text_arg.text = bfromcstr(text);
	(void)mmi_send(sl, &cmd);
	bdestroy(cmd.arg.text_arg.text);
}


static int provide_dialtone(struct symline *sl, char *resource)
{
	struct mmi_command cmd;
	int	res;
	
	assert(sl);
// 1. switch keypad to DTMF
	res = mmi_send_noargs(sl, MMI_CMD_KEYPAD_DTMF);
	if (res != SYM_OK) {
		SYMERROR("send error at \"keypad to DTMF\"\n");
		return res;
	}
// 2. erase display & 3. print line letter
	memset(&cmd, 0, sizeof(struct mmi_command));
	cmd.type = MMI_CMD_TEXT;
	cmd.arg.text_arg.erase = MMI_ERASE_TAIL;
	cmd.arg.text_arg.row = 3;
	cmd.arg.text_arg.col = 0;
	cmd.arg.text_arg.text = bformat("%c=", sl->number + 'a');
	res = mmi_send(sl, &cmd);
	bdestroy(cmd.arg.text_arg.text);
	if (res != SYM_OK) {
		SYMERROR("send error at \"print line letter\"\n");
		return res;
	}
// 4. indicate active line (red led )
	set_selected(sl, true);
// 5.  if station voice hw is not off-hook already - enable
//	the speakerphone
	if (!sl->offhook) {
		res = mmi_send_noargs(sl, MMI_CMD_OFFHOOK);
		if (res != SYM_OK) {
			SYMERROR("send error at turning on speakerphone\n");
			return res;
		}
		sl->offhook = true;
	}
// 6.	attach cpt "tone/dial" to bchan
	res = out_call_exec(sl, (resource ? resource : "tone/dial"), true);
	if (res != SYM_OK) {
		SYMERROR("error attaching cpt\n");
		return res;
	}
// 7. indicate line status  (green led) & line selection (red led)
	status_led(sl, LIGHT_STEADY);
	set_selected(sl, true);
// 8. enable keypad echo
	res = mmi_send_noargs(sl, MMI_CMD_ECHO_ON);
	if (res != SYM_OK) SYMERROR("error enabling keypad echo\n");
	return res;
}

static void erase_display(struct symline *sl)
{
	struct mmi_command cmd;

	assert(sl);
	memset(&cmd, 0, sizeof(struct mmi_command));
	cmd.type = MMI_CMD_TEXT;
	cmd.arg.text_arg.erase = MMI_ERASE_ALL;
	cmd.arg.text_arg.row = 0;
	cmd.arg.text_arg.col = 0;
	mmi_send(sl, &cmd);
}

static int init_mmi(struct symline *sl)
{	
	assert(sl);
	mmi_send_noargs(sl, MMI_CMD_ECHO_OFF);
	erase_display(sl);
	mmi_send_noargs(sl, MMI_CMD_ONHOOK);
	sl->offhook = false;
	status_led(sl, LIGHT_OFF);
	mmi_send_noargs(sl, MMI_CMD_KEYPAD_EVENTS);
	return SYM_OK;
}

static int do_hangup(struct symline *sl, bool send_onhook, bool force_onhook)
{
	int	res;
	
	assert(sl);
// 1. disable keypad echo
	res = mmi_send_noargs(sl, MMI_CMD_ECHO_OFF);
	if (res != SYM_OK) SYMERROR("error disabling keypad echo\n");

// 2. drop the call
	call_drop(sl, "EndedByLocalUser");
// 3. erase display
	erase_display(sl);
	sl->softecho = false;
//4. go on-hook
	if (sl->offhook && send_onhook) {
		res = mmi_send_noargs(sl, MMI_CMD_ONHOOK);
		if (res != SYM_OK) SYMERROR("send error at turning off speakerphone\n");
	}
	if (force_onhook) sl->offhook = false;
// 5. extinguish line status  (green led)
	status_led(sl, LIGHT_OFF);
//6. make keypad send events
	mmi_send_noargs(sl, MMI_CMD_KEYPAD_EVENTS);
	return SYM_OK;
}


static void status_led(struct symline *sl, int mode)
{
	struct mmi_command cmd;
	int	res;
	
	assert(sl);
	memset(&cmd, 0, sizeof(struct mmi_command));
	cmd.type = MMI_CMD_LED;
	cmd.ctlname = bformat("blf%d", sl->number + 1);
	cmd.arg.led_arg.color = LED_COLOR_GREEN;
	cmd.arg.led_arg.mode = mode;
	res = mmi_send(sl, &cmd);
	bdestroy(cmd.ctlname);
	if (res != SYM_OK)  SYMERROR("error at lighting green led\n");
}


static void select_led(struct symline *sl, int mode)
{
	struct mmi_command cmd;
	int	res;
	
	assert(sl);
	memset(&cmd, 0, sizeof(struct mmi_command));
	cmd.type = MMI_CMD_LED;
	cmd.ctlname = bformat("blf%d", sl->number + 1);
	cmd.arg.led_arg.color = LED_COLOR_RED;
	cmd.arg.led_arg.mode = mode;
	res = mmi_send(sl, &cmd);
	bdestroy(cmd.ctlname);
	if (res != SYM_OK)  SYMERROR("error at lighting red led\n");
}


static void set_selected(struct symline *sl, bool selected)
{
	assert(sl);
	
	if (selected) select_led(sl, LIGHT_STEADY);
	else select_led(sl, LIGHT_OFF);
	sl->selected = selected;

}

static int mmi_send_noargs(struct symline *sl, int cmdtype)
{
	struct mmi_command cmd;
	int	res;

	assert(sl);
	memset(&cmd, 0, sizeof(struct mmi_command));
	cmd.type = cmdtype;
	res = mmi_send(sl, &cmd);
	if (res != SYM_OK) SYMERROR("error sending command: %d\n", cmdtype);
	return res;
}


/* ================================ */
/* state change helpers & event handlers  for all the states */

static void enter_drop(struct symline *sl)
{
	do_hangup(sl, false, false);
	sl->state = SL_STATE_DROP;
	reset_timer(sl->tmr, DROP_TIMEOUT);
}

static void enter_sit(struct symline *sl, char *dest)
{
	if (dest) attach_cpt(sl, dest, 0);
	sl->state = SL_STATE_SIT;
	mmi_send_noargs(sl, MMI_CMD_ECHO_OFF);
	reset_timer(sl->tmr, SIT_TIMEOUT);
}

static void enter_dialtone_or_sit(struct symline *sl)
{
	int	res;
	
	assert(sl);
	res = provide_dialtone(sl, NULL);
	if (res == SYM_OK) {
		clear_digits(sl);
		sl->state = SL_STATE_DIALTONE;
		reset_timer(sl->tmr, FIRST_DIGIT_TIMEOUT);
	} else {
		enter_sit(sl, "tone/info");
	}
}

static void cease_ringing(struct symline *sl)
{	
	assert(sl);
	mmi_send_noargs(sl, MMI_CMD_NORING);
	call_drop(sl, "reject");
	sl->state = SL_STATE_IDLE;
	erase_display(sl);
	status_led(sl, LIGHT_OFF);
}

static void answer_ringing(struct symline *sl)
{
	yatemsg	*oper = NULL;
	conn_ctx *ctx;
	
	assert(sl);
	mmi_send_noargs(sl, MMI_CMD_NORING);
	sl->state = SL_STATE_CONNECTED;
	status_led(sl, LIGHT_STEADY);
	set_selected(sl, true);
	mmi_send_noargs(sl, MMI_CMD_KEYPAD_DTMF);
	reset_timer(sl->tmr, 0);

	ctx = yctx_for_line(sl);
	assert(ctx);
	oper = alloc_message("imt.operation");
	assert(oper);
	set_msg_param(oper, "operation", "answer");
	set_msg_param(oper, "address", sl->ccst->bchan);
	set_msg_param(oper, "symbiont_id", sl->calltrack);
	(void)yxt_enqueue(ctx, oper);
	free_message(oper);
}

static void send_ringing(struct symline *sl, yatemsg *copyfrom)
{
	yatemsg *ringing = NULL;
	conn_ctx *ctx;
	
	assert(sl);
	ctx = yctx_for_line(sl);
	assert(ctx);
	
	ringing = alloc_message("call.ringing");
	assert(ringing);
	if (copyfrom) {
		copy_msg_params(ringing, copyfrom);
		remove_msg_param(ringing, "handlers");
		remove_msg_param(ringing, "status");
		remove_msg_param(ringing, "callto");
		remove_msg_param(ringing, "callername");
		remove_msg_param(ringing, "called");
	}	
	set_msg_param(ringing, "status", "ringing");
	set_msg_param(ringing, "address", sl->ccst->bchan);
	set_msg_param(ringing, "peerid", sl->partycallid);
	(void)yxt_enqueue(ctx, ringing);
	free_message(ringing);
}


static void enter_hold(struct symline *sl)
{
	assert(sl);
	sl->state = SL_STATE_HOLD;
	status_led(sl, LIGHT_DARKENING);
	set_selected(sl, false);
	// signal to the upper layer that line is unselected
	
}

#define MAX_BLF_LEN	8
static bool check_isblfno(struct mmi_event *evt, int blfno)
{
	bool res;
	char blfname[MAX_BLF_LEN];
	
	assert(evt);
	memset(&blfname[0], 0, MAX_BLF_LEN);
	snprintf(&blfname[0], MAX_BLF_LEN - 1, "blf%d", blfno);
	res = check_ctlname(evt, &blfname[0]);
	return res;
}

// off-hook -> provide dialtone, switch to DIALTONE
//ignore all other events

static void state_idle(struct symline *sl, struct mmi_event *evt)
{
	char	digits[2] = { '\000', '\000' };
	int	res;
	
	assert(sl);
	assert(evt);
	switch (evt->type) {
		case MMI_EVT_PRESS: 
			if (check_isblfno(evt, sl->number + 1)) {
				enter_dialtone_or_sit(sl);
			} else if (sl->pad_offhook) {
				digits[0] = extract_pad_digit(evt);
				if (digits[0]) {
					sl->softecho = true;
					res = provide_dialtone(sl, "tone/noise");
					if (res == SYM_OK) {
						clear_digits(sl);
						add_digits(sl, &digits[0]);
						sl->state = SL_STATE_COLLECTING;
						reset_timer(sl->tmr, INTERDIGIT_TIMEOUT);
					} else {
						enter_sit(sl, "tone/info");
					}
				}
			}
			break;
		case MMI_EVT_OFFHOOK:
			sl->offhook = true;
			enter_dialtone_or_sit(sl);
			break;
		default:
			break;
	}
}

static int pickup_held(struct symline *sl)
{
	char	new_callto[MAX_NEW_CALLTO];
	
	assert(sl);
	memset(&new_callto[0], 0, MAX_NEW_CALLTO);
	snprintf(&new_callto[0], MAX_NEW_CALLTO - 1, 
			"imt/%s", sl->ccst->bchan);
	set_selected(sl, true);
	return transfer_party(sl, new_callto, "pickup");
}


static void state_hold(struct symline *sl, struct mmi_event *evt)
{
	char	ownblf[MAX_BLF_LEN];
	
	assert(sl);
	assert(evt);
	switch (evt->type) {
		case MMI_EVT_PRESS: 
			memset(&ownblf[0], 0, MAX_BLF_LEN);
			snprintf(&ownblf[0], MAX_BLF_LEN - 1, "blf%d", sl->number + 1);
			if (!check_ctlname(evt, &ownblf[0])) break;
			if (sl->offhook) {
				pickup_held(sl);
				break;
			} else mmi_send_noargs(sl, MMI_CMD_OFFHOOK);
			/* fallthrough */
		case MMI_EVT_OFFHOOK:
			if (!sl->offhook) {
				display_callername(sl, NULL);
				display_callernum(sl, NULL);
				sl->offhook = true;
				pickup_held(sl);
			}
			break;
		case MMI_EVT_ONHOOK:
			mmi_send_noargs(sl, MMI_CMD_ECHO_OFF);
			erase_display(sl);
//			mmi_send_noargs(sl, MMI_CMD_ONHOOK);
			sl->offhook = false;
			break;
		default:
			break;
	}
}


// digit -> cease dialtone, set interdigit timeout,
// 	switch to collecting
// hangup or hold -> hang up and switch to idle
static void state_dialtone(struct symline *sl, struct mmi_event *evt)
{
	assert(sl);
	assert(evt);
	switch (evt->type) {
		case MMI_EVT_PRESS:
			// keypad -> add digit, switch to collecting
			// blf -> add number and place call
			// drop -> clear collected digits, reset first digit tmo
			// hold -> ignore
			if (check_ctlname(evt, "drop")) {
				enter_drop(sl);
			}
			break;
		case MMI_EVT_ONHOOK:
			// hangup, switch to idle
			SYMDEBUG("on-hook in DIALTONE - about to call hangup\n");
			do_hangup(sl, false, true);
			sl->state = SL_STATE_IDLE;
			break;
		default:
			break;
	}

}

static void state_sit(struct symline *sl, struct mmi_event *evt)
{
	assert(sl);
	assert(evt);
	switch (evt->type) {
		case MMI_EVT_PRESS:
			// drop -> dialtone
			if (check_ctlname(evt, "drop")) {
				enter_drop(sl);
			}
			break;
		case MMI_EVT_ONHOOK:
			// hangup, switch to idle
			do_hangup(sl, false, true);
			sl->state = SL_STATE_IDLE;
			break;
		default:
			break;
	}
}

static void state_drop(struct symline *sl, struct mmi_event *evt)
{
	assert(sl);
	assert(evt);
	if (evt->type == MMI_EVT_ONHOOK) {
			// hangup, switch to idle
		if (sl->offhook) {
//			mmi_send_noargs(sl, MMI_CMD_ONHOOK);
			sl->offhook = false;
		}
		sl->state = SL_STATE_IDLE;
		reset_timer(sl->tmr, 0);
	}
}


static void state_calling(struct symline *sl, struct mmi_event *evt)
{
	assert(sl);
	assert(evt);
	switch (evt->type) {
		case MMI_EVT_PRESS:
			// drop -> dialtone
			if (check_ctlname(evt, "drop")) {
				enter_drop(sl);
			}
			break;
		case MMI_EVT_ONHOOK:
			// hangup, switch to idle
			do_hangup(sl, false, true);
			sl->state = SL_STATE_IDLE;
			break;
		default:
			break;
	}
}

static void state_connected(struct symline *sl, struct mmi_event *evt)
{
	int	res;
	
	assert(sl);
	assert(evt);
	switch (evt->type) {
		case MMI_EVT_PRESS:
			// drop -> dialtone
			if (check_ctlname(evt, "drop")) {
				enter_drop(sl);
			} else if (check_ctlname(evt, "hold")) {
				res = transfer_party(sl, "moh/default", "hold");
				if (res == SYM_OK) {
					enter_hold(sl);
				}
			}
			break;
		case MMI_EVT_ONHOOK:
			// hangup, switch to idle
			do_hangup(sl, false, true);
			sl->state = SL_STATE_IDLE;
			break;
		default:
			break;
	}
}


static void state_collect(struct symline *sl, struct mmi_event *evt)
{
	assert(sl);
	assert(evt);
	switch (evt->type) {
		case MMI_EVT_PRESS:
			// drop -> dialtone
			if (check_ctlname(evt, "drop")) {
				enter_drop(sl);
			}
			break;
		case MMI_EVT_ONHOOK:
			// hangup, switch to idle
			do_hangup(sl, false, true);
			sl->state = SL_STATE_IDLE;
			break;
		default:
			break;
	}
}

static void state_incoming(struct symline *sl, struct mmi_event *evt)
{
	
	assert(sl);
	assert(evt);
	switch (evt->type) {
		case MMI_EVT_OFFHOOK:
			sl->offhook = true;
			break;
		case MMI_EVT_ONHOOK:
			sl->offhook = false;
		default:
			break;
	}
}


static void state_ring(struct symline *sl, struct mmi_event *evt)
{
	int	res;
	
	assert(sl);
	assert(evt);
	switch (evt->type) {
		case MMI_EVT_PRESS:
			if (check_ctlname(evt, "drop")) {
				cease_ringing(sl);
				break;
			} 
			if (!check_isblfno(evt, sl->number + 1)) break;
			if (!sl->offhook) {
				res = mmi_send_noargs(sl, MMI_CMD_OFFHOOK);
				if (res != SYM_OK) break;
			}
			/* fallthrough */
		case MMI_EVT_OFFHOOK:
			sl->offhook = true;
			answer_ringing(sl);
			break;
		default:
			break;
	}
}

/* print debug for state transition caused by mmi event */
static void dbg_xbyevt(struct symline *sl, struct mmi_event *evt, int oldstate, int newstate)
{
	assert(sl);
	char *station = NULL;
	char *control = NULL;

	if (oldstate != newstate) {
		station = bstr2cstr(evt->station, '_');
		control = bstr2cstr(evt->ctlname, '_');

		SYMDEBUG("line %s, %s -> %s\n by \"%s [%s @ %s]\" event", sl->name, decode_linestate(oldstate), 
					decode_linestate(newstate), mmi_evt2txt(evt->type),
					control, station);
		if (station) bcstrfree(station);
		if (control) bcstrfree(control);
	}
}

/* print debug for state transition caused by yate msg */
static void dbg_xbymsg(struct symline *sl, yatemsg *msg, int oldstate, int newstate)
{
	assert(sl);
	assert(msg);
	
	if (oldstate != newstate) {
		SYMDEBUG("line %s, %s -> %s by \"%s\" message\n", sl->name, decode_linestate(oldstate),
				decode_linestate(newstate), get_msg_name(msg));
	}
}

/* print debug for state transition caused by external call */
static void dbg_ext(struct symline *sl, char *text, int res, int oldstate, int newstate)
{
	assert(sl);
	
	if (oldstate != newstate) {
		SYMDEBUG("line %s, %s -> %s by external %s; res=%d", sl->name, decode_linestate(oldstate),
				decode_linestate(newstate), text, res);
	}
}

/* print debug for state transition caused by timeout */
static void dbg_tmr(struct symline *sl, int oldstate, int newstate)
{
	assert(sl);
	
	if (oldstate != newstate) {
		SYMDEBUG("line %s, %s -> %s by timeout", sl->name, decode_linestate(oldstate),
				decode_linestate(newstate));
	}
}



void cctl_process_evt(struct symline *sl, struct mmi_event *evt)
{
	int	prevstate;
	assert(sl);
	assert(evt);
	mutex_lock(&sl->lmx);
	prevstate = sl->state;
	if (!sl->selected) {
		SYMERROR("an event dispatched to the unselected line - dropped\n");
		goto out;
	}
	switch (sl->state) {
		case SL_STATE_DISABLED:
			break;
		case SL_STATE_IDLE:
	// off-hook -> provide dialtone, switch to DIALTONE
	//ignore all other events
			state_idle(sl, evt);
			break;
		case SL_STATE_DIALTONE:
	// digit -> cease dialtone, set interdigit timeout,
	// 	switch to collecting
	// hangup -> hang up and switch to idle
			state_dialtone(sl, evt);
			break;
		case SL_STATE_COLLECTING:
	// digit -> set interdigit timeout, continue collecting
	// switch to CALLING if # pressed (number complete)
	// hangup  -> hangup and switch to idle
			state_collect(sl, evt);
			break;
		case SL_STATE_CALLING:
	//outgoing call in progress:
	// hold -> switch to hold
	// drop -> clear call and switch to dialtone
	// hangup -> clear call and switch to idle
			state_calling(sl, evt);
			break;
		case SL_STATE_CONNECTED:
	// transfer, conf - process 
	// hold -> switch to HOLD
	// drop, hangup - release call, switch to idle if hangup or dialtone if drop
			state_connected(sl, evt);
			break;
		case SL_STATE_HOLD:
	// select -> switch to connected or calling
	// hangup -> release call, switch to idle
			state_hold(sl, evt);
			break;
		case SL_STATE_SIT:

	//  hangup -> switch to idle
	// drop -> switch to dialtone
			state_sit(sl, evt);
			break;
		case SL_STATE_INCOMING:
			state_incoming(sl, evt);
			break;
		case SL_STATE_RING:
	// incoming call rings
	// off-hook - answr, switch to connected
	// drop - cancel the call
			state_ring(sl, evt);
			break;
		case SL_STATE_DROP:
			state_drop(sl, evt);
			break;
		default:
			SYMWARNING("line:%s, invalid state %d\n", sl->name, sl->state);
			break;
	}
out:
	dbg_xbyevt(sl, evt, prevstate, sl->state);
	mutex_unlock(&sl->lmx);
}

static struct runner_arg *make_runner_arg(struct symline *sl, yatemsg *msg)
{
	struct runner_arg *ra;
	
	assert(sl);
	assert(msg);
	ra = malloc(sizeof(struct runner_arg));
	assert(ra);
	memset(ra, 0, sizeof(struct runner_arg));
	
	ra->sl = sl;
	ra->msg = copy_message(msg);
	return ra;
}

/* returns CCTL_PROCESSED or CCTL_CHANGED or CCTL_OK or CCTL_FAIL */
int cctl_process_msg(struct symline *sl, yatemsg *msg, struct dwhook *dwh)
{
	int	res;
	char	*msgname;
	int	mclass;
	struct runner_arg *ra;
	int	oldstate;
	
	assert(sl);
	assert(msg);
	assert(dwh);
	msgname = get_msg_name(msg);
	assert(msgname);
	mclass = classify_msg(msgname);
	mutex_lock(&sl->lmx);
	oldstate = sl->state;
	switch (mclass) {
		case YMSG_CHAN_DTMF:
			ra = make_runner_arg(sl, msg);
			dwh->runner = process_chan_dtmf;
			dwh->arg = ra;
			res = CCTL_PROCESSED;
			break;
		case YMSG_CALL_ANSWERED:
			process_call_answered(sl, msg);
			res = CCTL_OK;
			break;
		case YMSG_CALL_EXECUTE:
			res = process_call_execute(sl, msg);
			break;
		case YMSG_CHAN_DISCONNECTED:
			ra = make_runner_arg(sl, msg);
			dwh->runner = process_chan_disconnect;
			dwh->arg = ra;
			res = CCTL_PROCESSED;
			break;
		case YMSG_CHAN_STARTUP:
			res = preprocess_chan_startup(sl, msg);
			if (res == CCTL_CHANGED) {
				ra = make_runner_arg(sl, msg);
				dwh->runner = process_chan_startup;
				dwh->arg = ra;
			}
			break;
		case YMSG_CHAN_HANGUP:
			ra = make_runner_arg(sl, msg);
			dwh->runner = process_chan_hangup;
			dwh->arg = ra;
			res = CCTL_PROCESSED;
			break;
		default:
			res = CCTL_OK;
			break;
	}
	dbg_xbymsg(sl, msg, oldstate, sl->state);
	mutex_unlock(&sl->lmx);
	return res;
}


int process_call_execute(struct symline *sl, yatemsg *msg)
{
	char	*id;
	
	char	new_callto[MAX_NEW_CALLTO];
	id = get_msg_param(msg, "id");
	if ((sl->state == SL_STATE_IDLE) && (sl->ccst->bchan) && id) {
		sl->outgoing = false;
		sl->partycallid = strdup(id);
		update_cdb(CDB_ID_PARTY, sl->partycallid, sl);
		new_calltrack(sl);
		set_msg_param(msg, "symbiont_id", sl->calltrack);
		memset(&new_callto[0], 0, MAX_NEW_CALLTO);
		snprintf(&new_callto[0], MAX_NEW_CALLTO - 1, 
				"imt/%s", sl->ccst->bchan);
		remove_msg_param(msg, "callto");
		set_msg_param(msg, "callto", new_callto);
		sl->state = SL_STATE_INCOMING;
		reset_timer(sl->tmr, INCOMING_SEIZURE_TIMEOUT);
		return CCTL_CHANGED;
	} else {
		set_msg_param(msg, "error", "busy");
		set_msg_param(msg, "cause", "line is busy");
	}
	return CCTL_OK;
}




void process_call_answered(struct symline *sl, yatemsg *msg)
{
	char	*newparty = NULL;
	assert(sl);
	SYMDEBUG("at enter\n");
	if (sl->state == SL_STATE_CALLING) {
		SYMDEBUG("Call answered\n");
		sl->state = SL_STATE_CONNECTED;
		reset_timer(sl->tmr, ANSWERED_DISPLAY_TIMER);
		newparty = get_msg_param(msg, "id");
		if (newparty) {
			if (sl->partycallid) {
				update_cdb(CDB_ID_PARTY, sl->partycallid, NULL);
				free(sl->partycallid);
			}
			sl->partycallid = strdup(newparty);
			update_cdb(CDB_ID_PARTY, sl->partycallid, sl);
		}
	}
}

int preprocess_chan_startup(struct symline *sl, yatemsg *msg)
{
	int res = CCTL_OK;
	char	*id = NULL;
	
	id = get_msg_param(msg, "id");
	if (!id) {
		SYMERROR("no id in chan.startup\n");
		return CCTL_OK;
	}
	switch (sl->state) {
		case SL_STATE_INCOMING:
			if (sl->ourcallid) SYMERROR("our call id already set before chan.startup\n");
		case SL_STATE_HOLD:
			if (sl->ourcallid) {
				update_cdb(CDB_ID_OUR, sl->ourcallid, NULL);
				free(sl->ourcallid);
			}
			sl->ourcallid = strdup(id);
			update_cdb(CDB_ID_OUR, sl->ourcallid, sl);
			res = CCTL_CHANGED;
			break;
		default:
			break;
	}
	return res;
}

void process_chan_startup(void *arg)
{
	int	oldstate;
	struct runner_arg *ra;
	char	*caller;
	char	*callername;
	
	assert(arg);
	ra = arg;
	caller = get_msg_param(ra->msg, "caller");
	callername = get_msg_param(ra->msg, "callername");

	mutex_lock(&ra->sl->lmx);
	oldstate = ra->sl->state;
	switch (ra->sl->state) {
		case SL_STATE_HOLD:
		case SL_STATE_INCOMING:
			if (ra->sl->offhook) {
				answer_ringing(ra->sl);
			} else {
				mmi_send_noargs(ra->sl, MMI_CMD_RING1);
				status_led(ra->sl, LIGHT_SLOWFLASH);
				display_callername(ra->sl, (callername ? callername : ""));
				display_callernum(ra->sl, (caller ? caller : "unknown"));
				ra->sl->state = SL_STATE_RING;
				reset_timer(ra->sl->tmr, 0);
				send_ringing(ra->sl, ra->msg);
			}
			break;
		default:
			break;
	}
	dbg_xbymsg(ra->sl, ra->msg, oldstate, ra->sl->state);
	mutex_unlock(&ra->sl->lmx);
	free_message(ra->msg);
	free(ra);
}


void process_chan_dtmf(void *arg)
{
	int	oldstate;
	struct runner_arg *ra;
	char	*text;
	char	*txtcopy = NULL;
	int	txtlen = 0;
	bool	complete = false;
	int	res;
	
	assert(arg);
	ra = arg;
	text = get_msg_param(ra->msg, "text");
	if (text) txtcopy = strdup(text);
	if (txtcopy) txtlen = strlen(txtcopy);
	mutex_lock(&ra->sl->lmx);
	oldstate = ra->sl->state;
	if (txtcopy && (txtlen > 0)) {
		switch (ra->sl->state) {
			case SL_STATE_DIALTONE:
				attach_cpt(ra->sl, "tone/noise", 0);
				ra->sl->state = SL_STATE_COLLECTING;
			case SL_STATE_COLLECTING:
				if (txtcopy[txtlen - 1] == '#') {
					complete = true;
					txtcopy[txtlen - 1] = '\000';
				}
				add_digits(ra->sl, txtcopy);
				reset_timer(ra->sl->tmr, INTERDIGIT_TIMEOUT);
				break;
			default:
				break;
		}
	}
	if ((ra->sl->digits_len > 0) && complete) {
		reset_timer(ra->sl->tmr, 0);
		res = routed_call(ra->sl, &ra->sl->digits[0]);
		if (res == SYM_OK) {
			ra->sl->state = SL_STATE_CALLING;
		} else {
			enter_sit(ra->sl, "tone/info");
		}
	}
	dbg_xbymsg(ra->sl, ra->msg, oldstate, ra->sl->state);
	mutex_unlock(&ra->sl->lmx);
	if (txtcopy) free(txtcopy);
	free_message(ra->msg);
	free(ra);
}


void process_chan_disconnect(void *arg)
{
	int	oldstate;
	char	*id = NULL;
	struct runner_arg *ra;

	assert(arg);
	ra = arg;
	id = get_msg_param(ra->msg, "id");
	if (id) {
		mutex_lock(&ra->sl->lmx);
		oldstate = ra->sl->state;
		if (!ra->sl->ourcallid) {
			ra->sl->ourcallid = strdup(id);
			update_cdb(CDB_ID_OUR, ra->sl->ourcallid, ra->sl);
		}
		switch(ra->sl->state) {
			case SL_STATE_CALLING:
			case SL_STATE_CONNECTED:
				out_call_exec(ra->sl, "tone/busy", true);
				enter_sit(ra->sl, NULL);
				break;
			case SL_STATE_INCOMING:
				ra->sl->state = SL_STATE_IDLE;
				reset_timer(ra->sl->tmr, 0);
				break;
			case SL_STATE_RING:
				cease_ringing(ra->sl);
				break;
			default:
				break;
		}
		dbg_xbymsg(ra->sl, ra->msg, oldstate, ra->sl->state);
		mutex_unlock(&ra->sl->lmx);
	} else SYMWARNING("no channel id in chan.disconnect message, cannot handle far-end hangup\n");
	free_message(ra->msg);
	free(ra);
}

void process_chan_hangup(void *arg)
{
	int	oldstate;
	struct runner_arg *ra;

	assert(arg);
	ra = arg;
	mutex_lock(&ra->sl->lmx);
	oldstate = ra->sl->state;
	if (ra->sl->state == SL_STATE_HOLD) {
		if (ra->sl->offhook) {
			out_call_exec(ra->sl, "tone/busy", true);
			status_led(ra->sl, LIGHT_STEADY);
			enter_sit(ra->sl, NULL);
		} else {
			do_hangup(ra->sl, false, true);
			ra->sl->state = SL_STATE_IDLE;
			reset_timer(ra->sl->tmr, 0);
		}
	}
	dbg_xbymsg(ra->sl, ra->msg, oldstate, ra->sl->state);
	mutex_unlock(&ra->sl->lmx);
	free_message(ra->msg);
	free(ra);
}

bool cctl_isline(void *ptr)
{
	struct symline *sl;
	
	assert(ptr);
	sl = (struct symline *)ptr;
	if (sl->magic == LINE_MAGIC) return true;
	else return false;
}


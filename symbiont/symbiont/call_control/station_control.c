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

#define	DEBUG_ME_HARDER		1

#include <symbiont/symerror.h>
#include <symbiont/yxtlink.h>
#include <symbiont/mmi.h>
#include <symbiont/mmi_print.h>
#include <symbiont/nofail_wrappers.h>
#include "call_control.h"
#include "global_lookup.h"
#include "classifiers.h"
#include "station_control.h"
#include "cctl_misc.h"

#define INIT_POLL_TIMEOUT	3000

/* state handlers */
static void state_init(struct ccstation *st, struct mmi_event *evt);
static void state_running(struct ccstation *st, struct mmi_event *evt);
static void ccstation_tmr_hndlr(union sigval arg);

static int mmi_send(struct ccstation *st, struct mmi_command *cmd);
static int mmi_send_noargs(struct ccstation *st, int cmdtype);
static int handle_selection(struct ccstation *st, int hint);
static int filter_command(struct ccstation *st, struct mmi_command *cmd, int lineno);
static bool line_enabled(struct ccstation *st, int number);


static void ccstation_tmr_hndlr(union sigval arg)
{
	struct ccstation *st;
	
	st = arg.sival_ptr;
	assert(st);
	lock_read(&st->sml);
	if (timer_armed(st->tmr)) {
		SYMDEBUG("timer rearmed while waiting\n");
	} else {
		switch (st->state) {
			case ST_STATE_DISABLED:
			case ST_STATE_INIT:
			case ST_STATE_RUNNING:
#warning implementation
			default:
				break;
		}
	}
	lock_unlock(&st->sml);
}




/***************/
/* ident response - reset interface and switch to IDLE
	other - send ident request and continue waiting 
 */

static void state_init(struct ccstation *st, struct mmi_event *evt)
{
	int	res;

	assert(st);
	assert(evt);
	if (evt->type == MMI_EVT_INIT) {
		res = mmi_send_noargs(st, MMI_CMD_INIT);
		if (res == SYM_OK) {
			st->state = ST_STATE_RUNNING;
			reset_timer(st->tmr, 0);
			SYMDEBUG("station %s: INIT->RUNNING\n", st->name);
			mmi_send_noargs(st, MMI_CMD_RING_PATTERN1);
//			select_led(sl, LIGHT_STEADY);
//			init_mmi(sl);
			return;
		}
	} else {
		(void)mmi_send_noargs(st, MMI_CMD_IDENTIFY);
	} 
	reset_timer(st->tmr, INIT_POLL_TIMEOUT);
}

/* choose what line to select:
 *
 * 1. If hint > 0 then try to select hinted line
 * 2. If hint is not set (<= 0) and there is a defselection
 *    then try to select defselected line
 * 3. Otherwise select first available (running) line.
 *
 */

static int handle_selection(struct ccstation *st, int hint)
{
	struct symline *sl = NULL;
	struct symline *newsl = NULL;
	int	res;
	int	i;
	
	assert(st);
	if (hint <= 0) hint = st->defselect;
	if (st->selected > 0) {
		sl = st_get_line_nolock(st, st->selected - 1);
	}
	if (hint > 0) {
		newsl = st_get_line_nolock(st, hint - 1);
	} else {
		for (i = 0; i < MAX_LINES; i++) {
			newsl = st_get_line_nolock(st, i);
			if (!newsl) continue;
			if (newsl->state == SL_STATE_DISABLED) continue;
			hint = i + 1;
			break;
		}
	}
	if (hint <= 0)  {
		SYMERROR("no lines to select\n");
		return SYM_FAIL;
	}
	
	if (sl) {
		if (st->selected == hint) {
			if (sl->selected) return hint;
			else {
				res = cctl_select(sl);
				if (res == SYM_OK) return hint;
				else {
					SYMERROR("cannot select line %s\n", sl->name);
					return SYM_FAIL;
				}
			}
		} else {
			if (sl->selected) {
				res = cctl_unselect(sl);
				if (res != SYM_OK) {
					SYMERROR("cannot unselect line %s\n", sl->name);
					return SYM_FAIL;
				}
			}
		}
	}
	res = cctl_select(newsl);
	if (res != SYM_OK) {
		SYMERROR("cannot select line %s\n", newsl->name);
		if (sl) {
			res = cctl_select(sl);
			if (res == SYM_OK) {
				SYMERROR("line %s selected back as a fallback\n", sl->name);
			}
		} else {
			SYMERROR("and no previously selected line\n");
		}
		return SYM_FAIL;
	}
	st->selected = hint;
	return hint;
}


static void state_running(struct ccstation *st, struct mmi_event *evt)
{
	int	sel;
	struct symline *sl;

	assert(st);
	assert(evt);
	if (evt->type == MMI_EVT_PRESS) {
		sel = get_blfno(evt);
		SYMDEBUG("blf key number %d\n", sel);
		if ((sel > 0) && (line_enabled(st, sel - 1))) {
			handle_selection(st, sel);
		}
	} else if ((evt->type == MMI_EVT_OFFHOOK) && 
			st->selected <= 0) {
		handle_selection(st, -1);
	}
	sl = st_get_line_nolock(st, st->selected - 1);
	if (!sl) {
		SYMWARNING("no selected line - event dropped\n");
		lock_unlock(&st->sml);
		return;
	}
	lock_unlock(&st->sml);
	cctl_process_evt(sl, evt);
}

void st_process_evt(struct ccstation *st, struct mmi_event *evt)
{
	assert(st);
	assert(evt);
#ifdef DEBUG_ME_HARDER
	print_mmievt(evt);
#endif
	lock_write(&st->sml);
	SYMDEBUGHARD("at enter, state=%d\n", st->state);
	switch (st->state) {
		case ST_STATE_DISABLED:
			break;
		case ST_STATE_INIT:
	//ident response - reset interface and switch to RUNNING
	//other - send ident request and continue waiting
			state_init(st, evt);
			break;
		case ST_STATE_RUNNING:
	// process events and call line call control handlers
			state_running(st, evt);
			return;	/* no lock_unlock required - already unlocked */
		default:
			SYMWARNING("station:%s, invalid state %d\n", st->name, st->state);
			break;
	}
	lock_unlock(&st->sml);
}



static int filter_command(struct ccstation *st, struct mmi_command *cmd, int lineno)
{
	uint32_t	mask = 0;
	bool	replace = false;
	int	res = SYM_OK;

	if (lineno < 0) goto out;
	mask = 0x01 << lineno;
	mutex_lock(&st->rmask_mx);
	switch (cmd->type) {
		case MMI_CMD_NORING:
			st->ring1_mask &= ~mask;
			st->ring2_mask &= ~mask;
			st->ring3_mask &= ~mask;
			st->beep_mask &= ~mask;
			replace = true;
			break;
		case MMI_CMD_RING_ONCE:
		case MMI_CMD_BEEP_ONCE:
			if (st->ring1_mask ||
				st->ring2_mask ||
				st->ring3_mask ||
				st->beep_mask) {
					res = SYM_FAIL;
					goto out;
			}
			break;
		case MMI_CMD_BEEP:
			st->beep_mask |= mask;
			replace = true;
			break;
		case MMI_CMD_RING1:
			st->ring1_mask |= mask;
			replace = true;
			break;
		case MMI_CMD_RING2:
			st->ring2_mask |= mask;
			replace = true;
			break;
		case MMI_CMD_RING3:
			st->ring3_mask |= mask;
			break;
		default:
			break;
	}
	if (replace) {
		if (st->ring3_mask) cmd->type = MMI_CMD_RING3;
		else if (st->ring2_mask) cmd->type = MMI_CMD_RING2;
		else if (st->ring1_mask) cmd->type = MMI_CMD_RING1;
		else if (st->beep_mask) cmd->type = MMI_CMD_BEEP;
	}
	res = SYM_OK;
out:	mutex_unlock(&st->rmask_mx);
	return res;
}



int st_mmi_send(struct ccstation *st, struct mmi_command *cmd, int lineno)
{
	int	res = SYM_OK;
	
	assert(st);
	assert(cmd);
	if (lineno >= 0) res = filter_command(st, cmd, lineno);
	if (res == SYM_OK) {
		res = mmi_send(st, cmd);
		goto out;
	}
	res = SYM_OK;
out: 	
	return res;
}
 

static int mmi_send(struct ccstation *st, struct mmi_command *cmd)
{
	int	res;
	
	assert(st);
	assert(cmd);
	res = (st->mmi_cb)(cmd, st->mmi_cb_arg);
	if (res != SYM_OK) res = SYM_FAIL;
	return res;
}


static int mmi_send_noargs(struct ccstation *st, int cmdtype)
{
	struct mmi_command cmd;
	int	res;

	assert(st);
	memset(&cmd, 0, sizeof(struct mmi_command));
	cmd.type = cmdtype;
	res = mmi_send(st, &cmd);
	if (res != SYM_OK) SYMERROR("error sending command: %d\n", cmdtype);
	return res;
}

struct ccstation *new_ccstation(void)
{
	struct ccstation *st = NULL;
	struct sigevent sevp;
	int res;
	
	st = malloc(sizeof(struct ccstation));
	if (!st) {
		SYMERROR("cannot allocate memory\n");
		return NULL;
	}
	memset(st, 0, sizeof(struct ccstation));
	st->state = ST_STATE_DISABLED;
	res = pthread_rwlock_init(&st->sml, NULL);
	assert(res == 0);
	res = pthread_mutex_init(&st->rmask_mx, NULL);
	assert(res == 0);
	
	memset(&sevp, 0, sizeof(struct sigevent));
	sevp.sigev_notify = SIGEV_THREAD;
	sevp.sigev_value.sival_ptr = st;
	sevp.sigev_notify_function = ccstation_tmr_hndlr;
	res = timer_create(CLOCK_MONOTONIC, &sevp, &st->tmr);
	assert(res == 0);
	st->magic = STATION_MAGIC;
	return st;
}


int st_add_line(struct ccstation *st, struct symline *sl)
{
	int	res = SYM_FAIL;
	int	lineno;
	struct symline **destsl;
	assert(st);
	assert(sl);
	lineno = sl->number;
	
	if (lineno >= MAX_LINES) {
		SYMERROR("cannot add line %s to station %s- lineno %d >= MAX_LINES (%d)\n",
				sl->name, st->name, lineno, MAX_LINES);
		return SYM_FAIL;
	}
	lock_write(&st->sml);
	destsl = &(st->lines)[lineno];
	if (*destsl) {
		SYMERROR("cannot add line %s #%d to station %s - already set to %s\n",
			sl->name, lineno, st->name, (*destsl)->name);
			goto out;
	}
	*destsl = sl;	
	res = SYM_OK;
out:
	lock_unlock(&st->sml);
	return res;
}


struct symline *st_get_line_nolock(struct ccstation *st, int number)
{
	assert(st);
	if (number >= MAX_LINES) {
		SYMERROR("line number too big (%d >= %d)\n",
				number, MAX_LINES);
		return NULL;
	}
	if (number < 0) {
		SYMERROR("invalid line number %d\n", number);
		return NULL;
	}
	return (st->lines)[number];
}

static bool line_enabled(struct ccstation *st, int number)
{
	struct symline *sl;
	
	assert(st);
	if (number >= MAX_LINES) {
		SYMERROR("line number too big (%d >= %d)\n",
				number, MAX_LINES);
		return false;
	}
	if (number < 0) {
		SYMERROR("invalid line number %d\n", number);
		return false;
	}
	sl = (st->lines)[number];
	if (!sl) return false;
	if ((sl->name) && (sl->ccst) && 
		(sl->state != SL_STATE_DISABLED)) return true;
	else return false;
}




struct symline *st_get_line(struct ccstation *st, int number)
{
	struct symline *sl;
	
	assert(st);
	lock_read(&st->sml);
	sl = st_get_line_nolock(st, number);
	lock_unlock(&st->sml);
	return sl;
}


int st_enable(struct ccstation *st)
{
	int	res = SYM_FAIL;
	
	assert(st);
	lock_write(&st->sml);
	if (st->state == ST_STATE_DISABLED) {
		if (!st->name) {
			SYMERROR("cannot enable station with no name\n");
			goto out;
		}
		if (!st->bchan) {
			SYMERROR("cannot enable station without bchan\n");
			goto out;
		}
		if (st->ifi <= 0) {
			SYMERROR("cannot enable station with no IFI set\n");
			goto out;
		}
		st->state = ST_STATE_INIT;
		(void)mmi_send_noargs(st, MMI_CMD_IDENTIFY);
		res = SYM_OK;
	}

out:	
	lock_unlock(&st->sml);
	return res;
}

int st_disable(struct ccstation *st)
{
	assert(st);
	lock_write(&st->sml);
#warning implementaion
	
	lock_unlock(&st->sml);
	return SYM_OK;
}

bool st_evt_filter_pass(struct ccstation *st, struct mmi_event *evt)
{
	bool	allow = true;
	int	blf;
	struct symline *sl;
	
	assert(st);
	assert(evt);
	
	lock_read(&st->sml);
	blf = get_blfno(evt);
	if (blf <= 0) goto out;
	sl = st_get_line_nolock(st, blf - 1);
	if (!sl) goto out;
	if (!line_enabled(st, blf - 1)) goto out;
	if (!sl->evtsend) allow = false; 
out:
	lock_unlock(&st->sml);
	return allow;
}


bool st_cmd_filter_pass(struct ccstation *st, struct mmi_command *cmd)
{
	bool	allow = true;
	int	blf;
	struct symline *sl;
	char	*p;
	
	assert(st);
	assert(cmd);
	
	lock_read(&st->sml);
	if ((cmd->type == MMI_CMD_ONHOOK) || 
		(cmd->type == MMI_CMD_OFFHOOK)) {
		sl = st_get_line_nolock(st, st->selected - 1);
		if (!sl) goto out;
		if (sl->state == SL_STATE_DISABLED) goto out;
		if (!sl->cmdrcv) allow = false;
	} else if (cmd->type == MMI_CMD_LED) {
		if (blength(cmd->ctlname) < 4) goto out;
		p = bdata(cmd->ctlname);
		if (strncmp(p, "blf", 3) != 0) goto out;
		blf = atoi(p + 3);
		if (blf <= 0) goto out;
		sl = st_get_line_nolock(st, blf - 1);
		if (!sl) goto out;
		if (!line_enabled(st, blf - 1)) goto out;
		if (!sl->cmdrcv) allow = false; 
	}
out:
	lock_unlock(&st->sml);
	return allow;
}

bool st_isccstation(void *ptr)
{
	struct ccstation *st;
	
	assert(ptr);
	st = (struct ccstation *)ptr;
	if (st->magic == STATION_MAGIC) return true;
	else return false;
}

int free_ccstation(struct ccstation *st)
{
	assert(st);
#warning implementation
	SYMFATAL("not implemented\n");

	return SYM_OK;
}


int st_unselect_me(struct ccstation *st, struct symline *sl)
{
	assert(st);
	assert(sl);

#warning implementation
	

	return SYM_FAIL;
}


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

#include <stdio.h>
#include <errno.h>
#include <symbiont/symbiont.h>
#include <symbiont/hua.h>
#include <symbiont/dcpmux.h>
#include <symbiont/transcode.h>
#include <symbiont/mmi_print.h>
#include <unistd.h>
#include <symbiont/call_control.h>
#include <symbiont/station_control.h>
#include <symbiont/classify/classifiers.h>
#include <symbiont/mmi.h>
#include <symbiont/global_lookup.h>
#include <symbiont/cfdb.h>
#include <symbiont/cfload.h>


#define DEFAULT_IFI	1
#define YXT_WORKERS	5

#include "common.h"
#include "commands.h"

/* */
#define OUR_BCHAN	"imtdcp/1"

/* call.execute will be served only if callto starts from CALLTO_PREFIX */
#define CALLTO_PREFIX		"dcp/"
#define CALLTO_PREFIX_LEN	4

#define	DEBUG_ME_HARDER	1

#define MAX_HOSTNAME_LEN	256

extern hua_ctx	*hctx;
extern conn_ctx	*yctx;
extern dcpmux_t	*dctx;
extern cfdb	*confdb;
extern struct global_params gparm;
static char myhostname[MAX_HOSTNAME_LEN + 1];

struct	mmi_name {
	const char *n;
	int	t;
};

static int attach_st_iter(int objtype, void *object, void *arg);
static int attach_stations(void);
static void start_station(struct ccstation *st);
int dmrcv_handler(void *arg, int ifi, bool cmdflag, uint8_t *data, ssize_t length);
int sendmmi(struct mmi_command *cmd, void *arg);
static const char *search_name(const struct mmi_name *list, int idx, int maxcount);
static void dcp_event_enq(struct ccstation *st, struct mmi_event *evt);
int dcp_command_hndlr(struct conn_ctx_s *ctx, yatemsg *msg, void *arg, struct dwhook *dwh);
int symbiont_command_hndlr(struct conn_ctx_s *ctx, yatemsg *msg, void *arg, struct dwhook *dwh);

/* compare address to bchan */
int address_preselect(struct conn_ctx_s *ctx, yatemsg *msg, void *arg, struct dwhook *dwh);

/* compare peerid to ourcallid */
int peerid_preselect(struct conn_ctx_s *ctx, yatemsg *msg, void *arg, struct dwhook *dwh);

/* compare callto to line name */
int callto_preselect(struct conn_ctx_s *ctx, yatemsg *msg, void *arg, struct dwhook *dwh);

/* compare lastpeerid to ourcallid & lastpeerid in symline */
int chan_disc_preselect(struct conn_ctx_s *ctx, yatemsg *msg, void *arg, struct dwhook *dwh);

/* compare id to partycallid */
int chan_hangup_preselect(struct conn_ctx_s *ctx, yatemsg *msg, void *arg, struct dwhook *dwh);

/* compare symbiont_id to calltrack */
int sym_id_preselect(struct conn_ctx_s *ctx, yatemsg *msg, void *arg, struct dwhook *dwh);

/* engine.command handler */
int engine_command_hndlr(struct conn_ctx_s *ctx, yatemsg *msg, void *arg, struct dwhook *dwh);


static void start_station(struct ccstation *st)
{
	int	i;
	int	res;
	struct symline *sl;
	
	assert(st);
	res = st_enable(st);
	if (res != SYM_OK) {
		SYMERROR("cannot autostart station %s\n", st->name);
		return;
	}
	for (i = 0; i < MAX_LINES; i++) {
		sl = (st->lines)[i];
		if (!sl) continue;
		SYMDEBUG("autostarting line %s\n", sl->name);
		res = cctl_enable(sl);
		if (res != CCTL_OK) {
			SYMERROR("cannot autostart line %s\n", sl->name);
		}
	}
}


static int attach_st_iter(int objtype, void *object, void *arg)
{
	struct ccstation *st;
	int	res;
	
	assert(object);
	assert(objtype == CFG_OBJ_STATION);
	st = (struct ccstation *)object;
	SYMDEBUG("Attaching station %s, bchan %s, ifi=%d\n", st->name, st->bchan, st->ifi);
	res = dcpmux_ifadd(dctx, st->ifi, NULL);
	if (res != SYM_OK) {
		SYMERROR("error adding interface %d\n", st->ifi);
		goto out;
	}
	res = dcpmux_ifup(dctx, st->ifi);
	if (res != SYM_OK) {
		SYMERROR("error enabling interface %d\n", st->ifi);
		goto out;
	}
	st->mmi_cb = sendmmi;
	st->mmi_cb_arg = st;
	if (st->autostart) start_station(st);
out:
	return 0;
}


static int attach_stations(void)
{
	int	res;
	
	res = cfg_iterate(confdb, CFG_OBJ_STATION, attach_st_iter, NULL);
	return res;
}

int ccinit(void)
{
	int res;
	
	memset(myhostname, 0, MAX_HOSTNAME_LEN + 1);
	res = gethostname(myhostname, MAX_HOSTNAME_LEN);
	if (res) {
		SYMERROR("cannot get hostname: %s\n", STRERROR_R(errno));
		goto errout;
	}
	
	res = yxt_run(yctx, YXT_WORKERS);
	if (res != SYM_OK) goto errout;
	
	res = dcpmux_attach(dctx, hctx, dmrcv_handler, NULL);
	if (res != SYM_OK) goto errout;

	init_cdb();

 	res = yxt_add_handler(yctx, address_preselect, "chan.dtmf", 20, NULL);
 	if (res != YXT_OK) goto errout;

 	res = yxt_add_handler(yctx, peerid_preselect, "call.answered", 60, NULL);
 	if (res != YXT_OK) goto errout;

 	res = yxt_add_handler(yctx, callto_preselect, "call.execute", 20, NULL);
 	if (res != YXT_OK) goto errout;

 	res = yxt_add_handler(yctx, chan_disc_preselect, "chan.disconnected", 60, NULL);
 	if (res != YXT_OK) goto errout;

 	res = yxt_add_handler(yctx, chan_hangup_preselect, "chan.hangup", 60, NULL);
 	if (res != YXT_OK) goto errout;
 	
 	res = yxt_add_handler(yctx, sym_id_preselect, "chan.startup", 60, NULL);
 	if (res != YXT_OK) goto errout;

 	res = yxt_add_handler(yctx, dcp_command_hndlr, "dcp.command", 100, NULL);
 	if (res != YXT_OK) goto errout;

 	res = yxt_add_handler(yctx, engine_command_hndlr, "engine.command", 100, NULL);
 	if (res != YXT_OK) goto errout;

	res = attach_stations();
	if (res != YXT_OK) goto errout;

	res = SYM_OK;
	goto out;
errout:
	res = SYM_FAIL;
out:
	return res;
}


int dmrcv_handler(void *arg, int ifi, bool cmdflag, uint8_t *data, ssize_t length)
{
	struct	mmi_event *evt = NULL;
	struct ccstation *st;

	if (!data) {
		if (length == DCPMUX_TRANSPORT_UP) evt = mmi_up();
		else if (length == DCPMUX_TRANSPORT_DOWN) evt = mmi_lost();
	} else evt = dcp2mmi(data, length);
	if (!evt) {
		SYMERROR("cannot decode DCP response\n");
		return SYM_FAIL;
	}

/* place to insert dcp.event message generator 
 *
 * 	1. lookup station by ifi
 *	2. fill evt->station field
 *	3. check whether this event should cause sending of dcp.event
 *	   and send it if so.
 *	4. check whether this event should be passed to internal handler
 *	5. call st_process_evt() if so
 *
 */
 	st = (struct ccstation *)cfg_lookupifi(confdb, CFG_OBJ_STATION, ifi);
 	if (!st) {
 		SYMERROR("no station with ifi %d - packet discarded\n", ifi);
 		return SYM_FAIL;
 	}
 	evt->station = bfromcstr(st->name);

	if (st_evt_filter_pass(st, evt)) dcp_event_enq(st, evt);

	st_process_evt(st, evt);
	if (evt->station) bdestroy(evt->station);
	if (evt->ctlname) bdestroy(evt->ctlname);
	free(evt);
	return SYM_OK;
}

/* test sender - it derives line ifi from it's number.
 * It's a quick and dirty hack.
 */
int sendmmi(struct mmi_command *cmd, void *arg)
{
	int	ifi, res;
	struct dcp_dblk *dtrain, *dptr;
	struct ccstation *st;
	
	assert(cmd);
	assert(arg);
	st = (struct ccstation *)arg;
	ifi = st->ifi;
	if (ifi <= 0) {
		SYMERROR("invalid ifi=%d, refuse to send\n", ifi);
		return SYM_FAIL;
	}
#ifdef DEBUG_ME_HARDER
	print_mmicmd(cmd);
#endif
	dtrain = mmi2dcp(cmd);
	if (!dtrain) {
		SYMERROR("transcode returned dummy dtrain\n");
		return SYM_FAIL;
	}
	dptr = dtrain;
	res = SYM_OK;
	while (dptr) {
		SYMDEBUG("sending %d bytes:\n", dptr->length);
		HEXDUMP(dptr->data, dptr->length);
		res = dcpmux_send(dctx, ifi, true, dptr->data, dptr->length);
		if (res != SYM_OK) break;
		dptr = dptr->next;
	}
	if (res == DCP_BUSY) {
		SYMERROR("dcp link is busy for too long...\n");
	}
	free_dblks(dtrain);
	return res;
}

#define MAX_EVTNAMES (MMI_EVT_PRG_FAILED + 1)
static const struct mmi_name evtypes[MAX_EVTNAMES] = {
	{ .n = "link_lost",		.t = MMI_EVT_LOST },
	{ .n = "link_up",		.t = MMI_EVT_UP },
	{ .n = "hw_ident",		.t = MMI_EVT_INIT },
	{ .n = "press",			.t = MMI_EVT_PRESS },
	{ .n = "release",		.t = MMI_EVT_RELEASE },
	{ .n = "menu_select",		.t = MMI_EVT_MENU_SELECT },
	{ .n = "menu_exit",		.t = MMI_EVT_MENU_EXIT },
	{ .n = "onhook",		.t = MMI_EVT_ONHOOK },
	{ .n = "offhook",		.t = MMI_EVT_OFFHOOK },
	{ .n = "menu_saved",		.t = MMI_EVT_MENU_SAVED },
	{ .n = "prg_fail",	 	.t = MMI_EVT_PRG_FAILED }
};


static const char *search_name(const struct mmi_name *list, int idx, int maxcount)
{
	assert(list);
	static const char *neg = "IDX NEGATIVE";
	static const char *toolarge = "IDX TOO LARGE";
	static const char *err = "INCONSISTENT TABLE";
	
	if (idx < 0) return neg;
	if (idx > maxcount) return toolarge;
	if (list[idx].t != idx) {
		SYMERROR("inconsistent table: idx=%d, %p[%d].t=%d\n", 
			idx, list, idx, list[idx].t);
		return err;
	}
	return list[idx].n;
}



static void dcp_event_enq(struct ccstation *st, struct mmi_event *evt)
{
	yatemsg	*msg = NULL;
	char	*str = NULL;
	struct flte *fe = NULL;

	assert(evt);
	msg = alloc_message("dcp.event");
	assert(msg);

	str = bstr2cstr(evt->station, '_');
	if (str) {
		set_msg_param(msg, "station", str);
		bcstrfree(str);
		str = NULL;
	}
	str = bstr2cstr(evt->ctlname, '_');
	if (str) {
		if (st) {
			if (st->filter) {
				fe = lookup_name(st->filter, str);
				if (fe) {
					if (!(fe->sendevt)) {
						bcstrfree(str);
						goto out;
					}
					if (fe->alias) {
						bcstrfree(str);
						str = NULL;
						set_msg_param(msg, "ctlname", fe->alias);
					}
				}
			}
		}
		if (str) {
			set_msg_param(msg, "ctlname", str);
			bcstrfree(str);
			str = NULL;
		}
	}

	str = (char *)search_name(evtypes, evt->type, MAX_EVTNAMES - 1);
	if (str) set_msg_param(msg, "type", str);

	(void)yxt_enqueue(yctx, msg);
out:
	free_message(msg);
}


int dcp_command_hndlr(struct conn_ctx_s *ctx, yatemsg *msg, void *arg, struct dwhook *dwh)
{
	struct mmi_command cmd;
	int	res = YXT_OK;
	char	*station;
	char	*type;
	char	*ctlname;
	char	*tmp;
	struct	ccstation *st;
	struct	flte *fe;

	memset(&cmd, 0, sizeof(struct mmi_command));
	station = get_msg_param(msg, "station");	
	if (!station) {
		SYMERROR("no station name in dcp.command\n");
		goto out;
	}
	st = ccstation_by_name(station);
	if (!st) {
		SYMWARNING("statuion \"%s\" not found\n", station);
		goto out;
	}
	
	type = get_msg_param(msg, "type");
	if (!type) {
		SYMERROR("no type parameter for dcp.command\n");
		goto out;
	}

	cmd.type = classify_cmdtype(type);
	if (cmd.type == MMI_CMD_UNDEFINED) {
		SYMERROR("unknown type parameter (%s) for dcp.command\n", type);
		goto out;
	}
	cmd.station = bfromcstr(station);
	ctlname = get_msg_param(msg, "ctlname");
	if (ctlname) {
		if (st->filter) {
			fe = lookup_alias(st->filter, ctlname);
			if (fe) {
				if (!(fe->rcvcmd)) goto out;
				if (fe->hwname) ctlname = fe->hwname;
			} else {
				fe = lookup_name(st->filter, ctlname);
				if (fe) {
					if (!(fe->rcvcmd)) goto out;
				}
			}
		}
		cmd.ctlname = bfromcstr(ctlname);
	};
	switch (cmd.type) {
		case MMI_CMD_LED:
			if (!ctlname) {
				SYMERROR("no ctlname specified\n");
				goto out;
			}
			tmp = get_msg_param(msg, "mode");
			if (!tmp) {
				SYMERROR("no led mode specified\n");
				goto out;
			}
			cmd.arg.led_arg.mode = classify_ledmode(tmp);
			if (cmd.arg.led_arg.mode < 0) {
				SYMERROR("unknown led mode \"%s\"\n", tmp);
				goto out;
			}
			tmp = get_msg_param(msg, "color");
			if (tmp) cmd.arg.led_arg.color = classify_color(tmp);
			else cmd.arg.led_arg.color = LED_COLOR_GREEN;
			break;

		case MMI_CMD_PROGRAM:
			tmp = get_msg_param(msg, "item");
			if (!tmp) {
				SYMERROR("no menu item number specified\n");
				goto out;
			}
			cmd.arg.program_arg.number = atoi(tmp);
			tmp = get_msg_param(msg, "text");
			cmd.arg.program_arg.text = bfromcstr(tmp);
			break;

		case MMI_CMD_TEXT:
			tmp = get_msg_param(msg, "text");
			if (tmp) cmd.arg.text_arg.text = bfromcstr(tmp);
			tmp = get_msg_param(msg, "row");
			if (tmp) cmd.arg.text_arg.row = atoi(tmp);
			else cmd.arg.text_arg.row = TEXT_CONTINUE;

			tmp = get_msg_param(msg, "col");
			if (tmp) cmd.arg.text_arg.col = atoi(tmp);
			else cmd.arg.text_arg.col = TEXT_CONTINUE;
			
			tmp = get_msg_param(msg, "erase");
			if (tmp) cmd.arg.text_arg.erase = classify_erase(tmp);
			else cmd.arg.text_arg.erase = MMI_ERASE_NONE;
			break;
		default:
			break;
	}

	if (cmd.type ==  MMI_CMD_PROGRAM) {
		// - switch the station to program mode
		SYMERROR("not implemented\n");
		goto out;
	}
	// send the command if line filter allows
	if (!st_cmd_filter_pass(st, &cmd)) goto out;
	res = sendmmi(&cmd, st);
	if (res == SYM_OK) res = YXT_PROCESSED;
	else res = YXT_OK;
out:
	if (cmd.station) bdestroy(cmd.station);
	if (cmd.ctlname) bdestroy(cmd.ctlname);
	return res;
}

int symbiont_command_hndlr(struct conn_ctx_s *ctx, yatemsg *msg, 
	void *arg, struct dwhook *dwh)
{
	int	res = YXT_OK;

	char	*station;
	char	*type;
	char	*line;
	char	*tmp;
	struct	ccstation *st;

#warning implementation
// calling the cctl_x must be done from delayed work handler
	
	return res;
}


int engine_command_hndlr(struct conn_ctx_s *ctx, yatemsg *msg, 
	void *arg, struct dwhook *dwh)
{
	int	res = YXT_OK;
	int	tmp;
	int	outlen = 0;
	char	*outbuf = NULL;
	char	*line;
	char	*symname;
	char	*command;
	int	symnamelen;

	line = get_msg_param(msg, "line");
	if (!line) goto out;
	symname = strchr(line, ' ');
	if (!symname) goto out;
	++symname;
	if (!(*symname)) goto out;
	symnamelen = strlen(gparm.symname);
	if (symnamelen <= 0) goto out;
	tmp = strncmp(symname, gparm.symname, symnamelen);
	if (tmp) goto out;
	command = strchr(symname, ' ');
	if (!command) goto out;
	++command;
	if (!(*command)) goto out;
	SYMDEBUG("about to execute command '%s'\n", command);
	tmp = run_command(command, &outbuf, &outlen);
	if (tmp != CMD_OK) {
		if (outbuf) {
			free(outbuf);
			outbuf = NULL;
		}
		outlen = asprintf(&outbuf, "%s running on %s: invalid command\r\n",
			gparm.symname,myhostname); 
		res = YXT_PROCESSED;
		goto out;
	} else res = YXT_PROCESSED;

out:
	if (res == YXT_PROCESSED) {
		if (outbuf) {
			set_msg_retvalue(msg, outbuf);
			free(outbuf);
		}
	}
	return res;
}




/* message handler preselectors - 
 * a preselector should find the line from the message content
 * and call the cctl_process_msg()
 * for the chan.dtmf msg the line is found by address field
 */
int address_preselect(struct conn_ctx_s *ctx, yatemsg *msg, 
				void *arg, struct dwhook *dwh)
{
	char	*address = NULL;
	int	res;
	struct ccstation *st;
	struct symline *sl = NULL;
	
	address = get_msg_param(msg, "address");
	if (address) {
		st = (struct ccstation *)cfg_lookupvc(confdb, CFG_OBJ_STATION, address);
		if (st) {
			sl = st_get_line_nolock(st, (st->selected - 1));
		}
	}
	if (sl) { 
		SYMDEBUG("selected line %s\n", sl->name);
		res = cctl_process_msg(sl, msg, dwh);
		if (res == CCTL_PROCESSED) res = YXT_PROCESSED;
		else if (res == CCTL_CHANGED) res = YXT_CHANGED;
		else if (res == CCTL_OK) res = YXT_OK;
		else res = YXT_FAIL;
		return res;
	} else SYMDEBUG("no line selected\n");
	return YXT_OK;
}

int sym_id_preselect(struct conn_ctx_s *ctx, yatemsg *msg, 
				void *arg, struct dwhook *dwh)
{
	char	*sym_id = NULL;
	int	res;
	struct symline *sl = NULL;
	
	sym_id = get_msg_param(msg, "symbiont_id");
	if (sym_id) sl = lookup_cdb(CDB_ID_CALLTRACK, sym_id);
	if (sl) {
		SYMDEBUG("selected line %s\n", sl->name);
		res = cctl_process_msg(sl, msg, dwh);
		if (res == CCTL_PROCESSED) res = YXT_PROCESSED;
		else if (res == CCTL_CHANGED) res = YXT_CHANGED;
		else if (res == CCTL_OK) res = YXT_OK;
		else res = YXT_FAIL;
		return res;
	} else SYMDEBUG("no line selected\n");
	return YXT_OK;
}

int peerid_preselect(struct conn_ctx_s *ctx, yatemsg *msg, 
				void *arg, struct dwhook *dwh)
{
	char	*peerid = NULL;
	int	res;
	struct symline *sl = NULL;
	
	peerid = get_msg_param(msg, "peerid");
	if (peerid) sl = lookup_cdb(CDB_ID_OUR, peerid);
	if (sl) {
		SYMDEBUG("selected line %s\n", sl->name);
		res = cctl_process_msg(sl, msg, dwh);
		if (res == CCTL_PROCESSED) res = YXT_PROCESSED;
		else if (res == CCTL_CHANGED) res = YXT_CHANGED;
		else if (res == CCTL_OK) res = YXT_OK;
		else res = YXT_FAIL;
		return res;
	} else SYMDEBUG("no line selected\n");
	return YXT_OK;
}


int callto_preselect(struct conn_ctx_s *ctx, yatemsg *msg, 
				void *arg, struct dwhook *dwh)
{
	char	*callto = NULL;
	char	*lname = NULL;
	int	res;
	int	ctlen;
	struct symline *sl = NULL;
	callto = get_msg_param(msg, "callto");
	if (!callto) return YXT_OK;
	ctlen = strlen(callto);
	if (ctlen <= CALLTO_PREFIX_LEN) return YXT_OK;
	res = strncmp(callto, CALLTO_PREFIX, CALLTO_PREFIX_LEN);
	if (res) return YXT_OK;
	lname = callto + CALLTO_PREFIX_LEN;
	
	if (lname) {
		sl = (struct symline *)cfg_lookupname(confdb, CFG_OBJ_LINE, lname);
	}
	if (sl) {
		SYMDEBUG("selected line %s\n", sl->name);
		res = cctl_process_msg(sl, msg, dwh);
		if (res == CCTL_PROCESSED) res = YXT_PROCESSED;
		else if (res == CCTL_CHANGED) res = YXT_CHANGED;
		else if (res == CCTL_OK) res = YXT_OK;
		else res = YXT_FAIL;
		return res;
	} else SYMDEBUG("no line selected\n");
	return YXT_OK;
}


int chan_disc_preselect(struct conn_ctx_s *ctx, yatemsg *msg, 
				void *arg, struct dwhook *dwh)
{
	char	*id = NULL;
	char	*lastpeerid = NULL;
	int	res;
	struct symline *sl = NULL;
	
	id = get_msg_param(msg, "id");
	lastpeerid = get_msg_param(msg, "lastpeerid");
	if ((!id) && (!lastpeerid)) return YXT_OK;
	else {
		sl = lookup_cdb(CDB_ID_OUR, id);
		if (!sl) sl = lookup_cdb(CDB_ID_PARTY, lastpeerid);	
	}
	if (sl) {
		SYMDEBUG("selected line %s\n", sl->name);
		res = cctl_process_msg(sl, msg, dwh);
		if (res == CCTL_PROCESSED) res = YXT_PROCESSED;
		else if (res == CCTL_CHANGED) res = YXT_CHANGED;
		else if (res == CCTL_OK) res = YXT_OK;
		else res = YXT_FAIL;
		return res;
	} else SYMDEBUG("no line selected\n");
	return YXT_OK;
}

int chan_hangup_preselect(struct conn_ctx_s *ctx, yatemsg *msg, 
				void *arg, struct dwhook *dwh)
{
	char	*id = NULL;
	int	res;
	struct symline *sl = NULL;
	
	id = get_msg_param(msg, "id");
	if (!id) return YXT_OK;
	else sl = lookup_cdb(CDB_ID_PARTY, id);
	if (sl) {
		SYMDEBUG("selected line %s\n", sl->name);
		res = cctl_process_msg(sl, msg, dwh);
		if (res == CCTL_PROCESSED) res = YXT_PROCESSED;
		else if (res == CCTL_CHANGED) res = YXT_CHANGED;
		else if (res == CCTL_OK) res = YXT_OK;
		else res = YXT_FAIL;
		return res;
	} else SYMDEBUG("no line selected\n");
	return YXT_OK;
}


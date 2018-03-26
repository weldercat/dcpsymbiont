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
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <ctype.h>
#include <fnmatch.h>
#include <symbiont/symbiont.h>
#include <symbiont/dcpmux.h>

#include "commands.h"
#include "console.h"
#include "common.h"
#include <symbiont/call_control.h>
#include <symbiont/station_control.h>
#include <symbiont/cctl_misc.h>
#include <symbiont/cfdb.h>
#include <symbiont/filter.h>

extern cfdb *confdb;

#define MAX_ARGS	4
#define MAX_PACKET	32

extern hua_ctx	*hctx;


static void dcmd_help(int argc, char **argv);

static void dcmd_huadebug(int argc, char **argv);
static void dcmd_dmdebug(int argc, char **argv);
static void dcmd_line_enable(int argc, char **argv);
static void dcmd_line_disable(int argc, char **argv);
static void dcmd_line_select(int argc, char **argv);
static void dcmd_line_hold(int argc, char **argv);
static void dcmd_line_hangup(int argc, char **argv);
static void dcmd_line_unselect(int argc, char **argv);
static void dcmd_line_status(int argc, char **argv);
static void dcmd_station_enable(int argc, char **argv);
static void dcmd_station_disable(int argc, char **argv);
static void dcmd_station_status(int argc, char **argv);
static void dcmd_line_defsel(int argc, char **argv);
static void dcmd_list(int argc, char **argv);
static void list_stations(char *pattern);
static void list_lines(char *pattern);
static void list_filters(char *pattern);
static int list_iter(int objtype, void *object, void *arg);

static struct symline *get_line_ptr(int argc, char **argv);
static struct ccstation *get_station_ptr(int argc, char **argv);
static const char *decode_st_state(int state);
static void print_ring_mask(uint32_t rmask);
#if 0
static int dump_line(unsigned char *p, int offset, int len);
static void dump_pkt(unsigned char *p, int len);
#endif

static void brief_help(void);
static void long_help(char *cmd);

static int argsep(char *line, int maxlen, char **argv);
static int exec_command(int argc, char **argv);

static char *g_argv[MAX_ARGS];

static struct dcmd_s cmdarray[] = {
	{	.cmd = "help",
		.help = "print this help",
		.command = dcmd_help },

	{	.cmd = "hua_debug",
		.help = "hua_debug <dbgval>    - call hua_debug_ctl() with <dbgval> as an argument", 
		.command = dcmd_huadebug },

	{	.cmd = "dcpmux_debug",
		.help = "dcpmux_debug <dbgval>    - call dcpmux_debug_ctl() with <dbgval> as an argument", 
		.command = dcmd_dmdebug },



	{	.cmd = "enable",
		.help = "enable <linename>    - enable line", 
		.command = dcmd_line_enable },

	{	.cmd = "disable",
		.help = "disable <linename>    - disable line", 
		.command = dcmd_line_disable },
		
	{	.cmd = "select",
		.help = "select <linename>    - get line off-hook", 
		.command = dcmd_line_select },

	{	.cmd = "unselect",
		.help = "unselect <linename>    - put line on-hold or hangup and unselect", 
		.command = dcmd_line_unselect },


	{	.cmd = "hold",
		.help = "hold <linename>    - put line on hold", 
		.command = dcmd_line_hold },

	{	.cmd = "hangup",
		.help = "hangup <linename>    - hang-up line", 
		.command = dcmd_line_hangup },

	{	.cmd = "status",
		.help = "status <linename>    - print line status", 
		.command = dcmd_line_status },

	{	.cmd = "list",
		.help = "list <lines|stations> [pattern]  - list configured lines, stations or filters\n"
			"  matching objects' names with pattern (shell wildcards) if specified", 
		.command = dcmd_list },

	{	.cmd = "st_enable",
		.help = "st_enable <stname>    - enable station <stname>", 
		.command = dcmd_station_enable },

	{	.cmd = "st_disable",
		.help = "st_disable <stname>    - disable station <stname>", 
		.command = dcmd_station_disable },

	{	.cmd = "st_status",
		.help = "st_status <stname>    - print status for station <stname>", 
		.command = dcmd_station_status },


	{	.cmd = "defselection",
		.help = "defselection <stname> <lineno>    - set default line selection for station <stname> to <lineno>", 
		.command = dcmd_line_defsel },


	{ .cmd = NULL, .help = NULL, .command = NULL }
};


#define COMMANDS_PER_LINE	5
static void brief_help(void)
{
	int i;
	int	ncmd = 1;
	const struct dcmd_s *cm;
	
	console_mprintf("Available commands: \n    ");
	for (i = 0; cmdarray[i].cmd != NULL; i++) {
		cm = &cmdarray[i];
		console_mprintf("%s  ", cm->cmd);
		ncmd++;
		if (ncmd >= 5) {
			console_mprintf("\n    ");
			ncmd = 1;
		}
	}
	console_mprintf("\n");

}

static void long_help(char *cmd)
{
	int	i, res;
	const struct dcmd_s *cm = NULL;
	
	for (i = 0; cmdarray[i].cmd != NULL; i++) {
		cm = &cmdarray[i];
		res = strcmp(cmd, cm->cmd);
		if (res) continue;
		console_mprintf("\n%s\n", cm->help);
		break;
	}
	if (cmdarray[i].cmd == NULL) console_mprintf("No such command - %s, use help for list\n", cmd); 
	console_mprintf("\n");
}

static void dcmd_help(int argc, char **argv)
{
	if ((argc < 2) || (!argv[1])) {
		brief_help();
		console_mprintf("use help <command> for more info\n");
	} else long_help(argv[1]);
}


static int argsep(char *line, int maxlen, char **argv)
{
	char	*p;
	int	argc = 0;

	p = line;
	while (p < line + maxlen) {
		char	c;
		
		c = *p;
		if (!c) break;
		if (c > ' ') {
			argv[argc] = p;
			argc++;
			if (argc >= MAX_ARGS) break;
			while ((p < line + maxlen) && (*p > ' ')) p++;
			*p = '\000';
			p++;
			continue;
		}
		p++;
	}
	return argc;
}

static int exec_command(int argc, char **argv)
{
	int	i;
	const struct dcmd_s *cm;
	
	if (argc < 1) return NO_COMMAND;
	for (i = 0; cmdarray[i].cmd != NULL; i++) {
		cm = &cmdarray[i];
		if (strcmp(cm->cmd, argv[0])) continue;
		(cm->command)(argc, argv);
		return CMD_OK;	
	}
	return NO_COMMAND;
}


int run_command(char *line)
{
	char	*cmdline = NULL;
	int	argc;
	int	len;
	int	res = NO_COMMAND;
	
	len = strlen(line);
	if (len < 1) return NO_COMMAND;
	console_mprintf(">%s\n", line);
	cmdline = strndup(line, len);
	if (!cmdline) {
		console_mprintf("run_command: cannot allocate memory\n");
		goto errout;
	}
	argc = argsep(cmdline, len, g_argv);
	if (argc < 1) {
		res = NO_COMMAND;
		goto out_with_line;
	}
	res = exec_command(argc, g_argv);
//	console_mprintf("argc = %d\r\n", argc);
//	for (i = 0; i < argc; i++) {
//		printf("argv[%d] = %s\r\n", i, g_argv[i]);
//	}
out_with_line:
	if (cmdline) free(cmdline);
errout:
	console_mrefresh();
	return res;
}

#if 0
#define MAXDMP	16
static int dump_line(unsigned char *p, int offset, int len)
{
	int	i;
	
	if (len > MAXDMP) len = MAXDMP;
	console_mprintf("%04x  ", offset);
	for (i = 0; i < len; i++) console_mprintf("%02x ", p[i]);
	for (i = 0; i < (MAXDMP - len); i++) console_mprintf("   ");
	console_mprintf(" |");
	for (i = 0; i < len; i++) {
		unsigned char c;
		c = p[i] & 0x7f;
		if ((c < ' ') || (c > '~')) c = '.';
		console_mputc(c);
	}
	for (i = 0; i < (MAXDMP - len); i++) console_mputc(' ');
	console_mprintf("|\n\r");
	console_mrefresh();
	return len;
}

static void dump_pkt(unsigned char *p, int len)
{
	int	offset = 0;

	while (len > 0) {
		int dlen;
		dlen = dump_line(&(p[offset]), offset, len);
		len -= dlen;
		offset += dlen;
	}
}
#endif



static void print_ring_mask(uint32_t rmask)
{
	int	j;
	
	for (j = 31; j >= 0; j--) {
			if (rmask & (1 << j)) console_mprintf("%c", (j % 10) + '0');
			else console_mprintf("-");
	}
}

static const char *decode_st_state(int state)
{
	struct state_desc {
		int	state;
		const char *name;
	};

	static struct state_desc snames[] = {
		{ .state = ST_STATE_DISABLED,	.name = "DISABLED" },
		{ .state = ST_STATE_INIT,	.name = "INIT" },
		{ .state = ST_STATE_RUNNING,	.name = "RUNNING" },
		{ .state = -1, .name = NULL }
	};

	if ((state >= ST_STATE_UNUSED) || (state < 0)) return "unknown";
	return (snames[state].name);
}





static void dcmd_huadebug(int argc, char **argv)
{
	int	dbg;
	
	if ((argc < 2) || (!argv[1])) {
		console_mprintf("dcmd_huadebug: invalid arguments\n");
		return;
	}
	sscanf(argv[1], "%u", &dbg);
	hua_debug_ctl(dbg);
	console_mprintf("hua_debug_ctl(%d) called\n", dbg);
}


static void dcmd_list(int argc, char **argv)
{
	char	*ltype;
	char	*pattern = NULL;
	
	if ((argc < 2) || (!argv[1])) {
		console_mprintf("dcmd_huadebug: invalid arguments\n");
		return;
	}
	ltype = argv[1];
	if ((argc > 2) && (argv[2])) pattern = argv[2];
	if (strcmp(ltype, "stations") == 0) {
		list_stations(pattern);
	} else if (strcmp(ltype, "lines") == 0) {
		list_lines(pattern);
	} else if (strcmp(ltype, "filters") == 0) {
		list_filters(pattern);
	} else {
		console_mprintf("don't know how to list \"%s\"\n", ltype);
	}
}

static void list_stations(char *pattern)
{
	console_mprintf("ifi   station             B-chan   lines filter       state\n"
			"-------------------------------------------------------------------\n");
	(void)cfg_iterate(confdb, CFG_OBJ_STATION, list_iter, pattern);
	console_mprintf("\n");
}

static void list_lines(char *pattern)
{
	console_mprintf("line                                letter state\n"
			"---------------------------------------------------------------\n");
	(void)cfg_iterate(confdb, CFG_OBJ_LINE, list_iter, pattern);
	console_mprintf("\n");
}

static void list_filters(char *pattern)
{
	console_mprintf("filter                          refcount\n"
			"----------------------------------------\n");
	(void)cfg_iterate(confdb, CFG_OBJ_FILTER, list_iter, pattern);
	console_mprintf("\n");
}

#define DISPLAY_STRLEN	79
#define IFI_POS		0
#define STNAME_POS	6
#define BCHAN_POS	26
#define LINENO_POS	38
#define FLTNAME_POS	41
#define STATENAME_POS	54

#define SLNAME_POS	0
#define SLLETTER_POS	40
#define SLSTATE_POS	43

#define FILTERNAME_POS	0
#define REFCOUNT_POS	32
static int list_iter(int objtype, void *object, void *arg)
{
	struct ccstation *st;
	struct symline *sl;
	struct filter *flt;
	int	i, numlines, res;
	const char *pattern;
	char	printout[DISPLAY_STRLEN + 1];
	
	if (!object) {
		console_mprintf("NULL object\n");
		return 0;
	}
	pattern = (const char *)arg;
	memset(printout, ' ', DISPLAY_STRLEN);
	printout[DISPLAY_STRLEN] = '\000';
	switch(objtype) {
		case CFG_OBJ_STATION:
			st = (struct ccstation *)object;
			if (pattern) {
				res = fnmatch(pattern, st->name, 0);
				if (res) return 0;
			}
			numlines = 0;
			for (i = 0; i < MAX_LINES; i++) {
				if((st->lines)[i]) ++numlines;
			}
			snprintf(&printout[IFI_POS], STNAME_POS - IFI_POS, "%d", st->ifi);
			snprintf(&printout[STNAME_POS], BCHAN_POS - STNAME_POS, "%s", st->name);
			snprintf(&printout[BCHAN_POS], LINENO_POS - BCHAN_POS, "%s", st->bchan);
			snprintf(&printout[LINENO_POS], FLTNAME_POS - LINENO_POS, "%d", numlines);
			snprintf(&printout[FLTNAME_POS], STATENAME_POS - FLTNAME_POS, "%s",
					(st->filter ? st->filter->name : "   "));
			snprintf(&printout[STATENAME_POS], DISPLAY_STRLEN - STATENAME_POS, "%s", 
					decode_st_state(st->state)); 
			break;
		case CFG_OBJ_LINE:
			sl = (struct symline *)object;
			if (pattern) {
				res = fnmatch(pattern, sl->name, 0);
				if (res) return 0;
			}
			snprintf(&printout[SLNAME_POS], SLLETTER_POS - SLNAME_POS, "%s", sl->name);
			snprintf(&printout[SLLETTER_POS], SLSTATE_POS - SLLETTER_POS, "%c", sl->number + 'a');
			snprintf(&printout[SLSTATE_POS], DISPLAY_STRLEN - SLSTATE_POS, "%s", 
				decode_linestate(sl->state));
			break;
		case CFG_OBJ_FILTER:
			flt = (struct filter *)object;
			if (pattern) {
				res = fnmatch(pattern, flt->name, 0);
				if (res) return 0;
			}
			snprintf(&printout[FILTERNAME_POS], REFCOUNT_POS - FILTERNAME_POS, "%s", flt->name);
			snprintf(&printout[REFCOUNT_POS], DISPLAY_STRLEN - REFCOUNT_POS, "%d", flt->refcnt);
			break;
		default:
			snprintf(&printout[0], DISPLAY_STRLEN, "Unknown object at %p, type=%d",
					object, objtype);
			break;
	}
	for (i = 0; i < DISPLAY_STRLEN; i++) {
		if (printout[i] == '\000') printout[i] = ' ';
	}
	console_mprintf("%s\n", printout);
	return 0;
}


static void dcmd_dmdebug(int argc, char **argv)
{
	int	dbg;
	
	if ((argc < 2) || (!argv[1])) {
		console_mprintf("dcmd_dmdebug: invalid arguments\n");
		return;
	}
	sscanf(argv[1], "%u", &dbg);
	dcpmux_debug_ctl(dbg);
	console_mprintf("dcpmux_debug_ctl(%d) called\n", dbg);
}

static struct symline *get_line_ptr(int argc, char **argv)
{
	struct symline *sl;
	char	*lname;
	
	if ((argc < 2) || (!argv[1])) {
		console_mprintf("line name must be specified\n");
		return NULL;
	}
	lname = argv[1];
	sl = (struct symline *)cfg_lookupname(confdb, CFG_OBJ_LINE, lname);
	if (!sl) {
		console_mprintf("no such line \"%s\"\n", lname);
		return NULL;
	}
	return sl;
}


static struct ccstation *get_station_ptr(int argc, char **argv)
{
	struct ccstation *st;
	char	*sname;

	if ((argc < 2) || (!argv[1])) {
		console_mprintf("station name must be specified\n");
		return NULL;
	}
	sname = argv[1];
	st = (struct ccstation *)cfg_lookupname(confdb, CFG_OBJ_STATION, sname);
	if (!st) {
		console_mprintf("no such station \"%s\"\n", sname);
		return NULL;
	}
	return st;
}

static void dcmd_line_enable(int argc, char **argv)
{
	struct symline *sl;
	int res;
	
	sl = get_line_ptr(argc, argv);
	if (sl) {
		res = cctl_enable(sl);
		console_mprintf("cctl_enable() returns %d\n", res);
	}
}


static void dcmd_line_disable(int argc, char **argv)
{
	struct symline *sl;
	int res;
	
	sl = get_line_ptr(argc, argv);
	if (sl) {
		res = cctl_disable(sl);
		console_mprintf("cctl_disable() returns %d\n", res);
	}
}


static void dcmd_line_select(int argc, char **argv)
{
	struct symline *sl;
	int res;
	
	sl = get_line_ptr(argc, argv);
	if (sl) {
		res = cctl_select(sl);
		console_mprintf("cctl_select() returns %d\n", res);
	}
}


static void dcmd_line_unselect(int argc, char **argv)
{
	struct symline *sl;
	int res;
	
	sl = get_line_ptr(argc, argv);
	if (sl) {
		res = cctl_unselect(sl);
		console_mprintf("cctl_unselect() returns %d\n", res);
	}
}


static void dcmd_line_hold(int argc, char **argv)
{
	struct symline *sl;
	int res;
	
	sl = get_line_ptr(argc, argv);
	if (sl) {
		res = cctl_hold(sl);
		console_mprintf("cctl_hold() returns %d\n", res);
	}
}



static void dcmd_line_defsel(int argc, char **argv)
{
	int	defsel = 0;
	struct  ccstation *st;
	
	if ((argc < 3) || (!argv[1])) return;
	st = get_station_ptr(argc, argv);
	sscanf(argv[2], "%d", &defsel);
	st->defselect = defsel;
}



static void dcmd_line_hangup(int argc, char **argv)
{
	struct symline *sl;
	int res;
	
	sl = get_line_ptr(argc, argv);
	if (sl) {
		res = cctl_hangup(sl);
		console_mprintf("cctl_hangup() returns %d\n", res);
	}
}



static void dcmd_line_status(int argc, char **argv)
{
	struct symline *sl;
	char	numstr[MAX_NUMBER_LENGTH + 1];
	int	copylen;
	
	sl = get_line_ptr(argc, argv);
	if (sl) {
		memset(&numstr[0], 0, MAX_NUMBER_LENGTH + 1);
		copylen = sl->digits_len;
		if (copylen > MAX_NUMBER_LENGTH) copylen = MAX_NUMBER_LENGTH;
		if (copylen > 0) strncpy(&numstr[0], &sl->digits[0], copylen);
		console_mprintf("Line status:\n");
		console_mprintf("\tName: %s\n", sl->name);
		console_mprintf("\tDisplay name: %s\n", sl->displayname);
		console_mprintf("\tState: %s (%d)\n", decode_linestate(sl->state), sl->state);
		console_mprintf("\tSelected: %s\n", (sl->selected ? "yes" : "no"));
		console_mprintf("\tLine is %s\n", sl->offhook ? "off-hook" : "on-hook");
		console_mprintf("\tLine letter: \'%c\'\n", ('a' + sl->number));
		console_mprintf("\tBLF sends dcp.event: %s\n", (sl->evtsend ? "yes" : "no"));
		console_mprintf("\tBLF accepts dcp.command: %s\n", (sl->cmdrcv ?  "yes" : "no"));
		console_mprintf("\tLine accepts symbiont.command: %s\n", (sl->xcontrol ?  "yes" : "no"));
		console_mprintf("\tLast or current call: %s\n", 
				sl->outgoing ? "outgoing" : "incoming");
		if (copylen > 0) {
			console_mprintf("\t%d digits collected: %s\n", copylen, &numstr[0]);
		} else console_mprintf("\tno digits collected\n");
		if (sl->ccst) {
			console_mprintf("\tBchan: %s\n", sl->ccst->bchan);
		}
		console_mprintf("\tOur call id: %s\n", sl->ourcallid);
		console_mprintf("\tParty call id: %s\n", sl->partycallid);
		console_mprintf("\tCaller name: %s\n", sl->callername);
		console_mprintf("\tCaller number: %s\n", sl->caller);
		console_mprintf("\tCall tracking id: %s\n", sl->calltrack);
	} else {
		console_mprintf("not initialized\n\n");
	}
}


static void dcmd_station_enable(int argc, char **argv)
{
	struct ccstation *st;
	int res;
	
	st = get_station_ptr(argc, argv);
	if (st) {
		res = st_enable(st);
		console_mprintf("st_enable() returns %d\n", res);
	}
}

static void dcmd_station_disable(int argc, char **argv)
{
	struct ccstation *st;
	int res;
	
	st = get_station_ptr(argc, argv);
	if (st) {
		res = st_disable(st);
		console_mprintf("st_disable() returns %d\n", res);
	}
}

static void dcmd_station_status(int argc, char **argv)
{
	struct ccstation *st;
	
	st = get_station_ptr(argc, argv);
	if (st) {
		console_mprintf("Station status:\n");
		console_mprintf("\tName: %s\n", st->name);
		console_mprintf("\tAutostart: %s\n", (st->autostart ? "yes" : "no"));
		console_mprintf("\tBchan: %s\n", st->bchan);
		console_mprintf("\tIFI: %d\n", st->ifi);
		console_mprintf("\tState: %s (%d)\n", decode_st_state(st->state), st->state);
		console_mprintf("\tLines configured: %d\n", st->numlines);
		console_mprintf("\tSelected line: %d\n", st->selected);
		console_mprintf("\tDefault selection: %d\n", st->defselect);
		console_mprintf("Ring1 mask: ");
			print_ring_mask(st->ring1_mask);
		console_mprintf("\nRing2 mask: ");
			print_ring_mask(st->ring2_mask);
		console_mprintf("\nRing3 mask: ");
			print_ring_mask(st->ring3_mask);
		console_mprintf("\nBeep mask: ");
			print_ring_mask(st->beep_mask);

		console_mprintf("\n");
	} else {
		console_mprintf("not initialized\n");
	}
}

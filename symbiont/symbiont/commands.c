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
#define NOFAIL_LOCK_UNNEEDED	1
#include <symbiont/nofail_wrappers.h>
#include <symbiont/call_control.h>
#include <symbiont/station_control.h>
#include <symbiont/cctl_misc.h>
#include <symbiont/cfdb.h>
#include <symbiont/filter.h>

#define INITIAL_OUTBUF	512
#define OUTBUF_REALLOC_STEP	512

extern cfdb *confdb;

#define MAX_ARGS	4
#define MAX_PACKET	32

extern hua_ctx	*hctx;
extern conn_ctx	*yctx;

static pthread_mutex_t ctag_mutex = PTHREAD_MUTEX_INITIALIZER;
static unsigned int ctag_counter = 1;

struct cmdp {
	char	*outbuf;
	int	outlen;
	int	memlen;
	int	argc;
	char	**argv;
};


struct dcmd_s {
	const char *cmd;
	const char *help;
	void (* command)(struct cmdp *p);
};

struct list_params {
	char *pattern;
	unsigned int ctag;
};

typedef void *(* batch_cmd)(void *arg);

static unsigned int get_ctag(void);
static int console_mprintf(struct cmdp *p, const char *format, ...);
static int console_lprintf(const char *format, ...);
static int console_mputc(struct cmdp *p, int c);

static void dcmd_help(struct cmdp *p);
static void submit_cmd(batch_cmd bcmd, void *arg);

static void dcmd_huadebug(struct cmdp *p);
static void dcmd_dmdebug(struct cmdp *p);
static void dcmd_line_enable(struct cmdp *p);
static void dcmd_line_disable(struct cmdp *p);
static void dcmd_line_select(struct cmdp *p);
static void dcmd_line_hold(struct cmdp *p);
static void dcmd_line_hangup(struct cmdp *p);
static void dcmd_line_unselect(struct cmdp *p);
static void dcmd_line_status(struct cmdp *p);
static void dcmd_station_enable(struct cmdp *p);
static void dcmd_station_disable(struct cmdp *p);
static void dcmd_station_status(struct cmdp *p);
static void dcmd_line_defsel(struct cmdp *p);
static void dcmd_list(struct cmdp *p);
static void *list_stations(void *arg);
static void *list_lines(void *arg);
static void *list_filters(void *arg);
static int list_iter(int objtype, void *object, void *arg);


static struct symline *get_line_ptr(struct cmdp *p);
static struct ccstation *get_station_ptr(struct cmdp *p);
static const char *decode_st_state(int state);
static void print_ring_mask(struct cmdp *p, uint32_t rmask);
#if 0
static int dump_line(struct cmdp *p, unsigned char *p, int offset, int len);
static void dump_pkt(struct cmdp *p, unsigned char *p, int len);
#endif

static void brief_help(struct cmdp *p);
static void long_help(struct cmdp *p, char *cmd);

static int argsep(char *line, int maxlen, char **argv);
static int exec_command(struct cmdp *p);

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
		.help = "list <lines|stations> [pattern]  - list configured lines, stations or filters\r\n"
			"  matching objects' names with pattern (shell wildcards) if specified\r\n"
			"  please note that list command sends its printout via log messages,\r\n"
			"  so you have to enable debug 1 at least to see it",
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

static void submit_cmd(batch_cmd bcmd, void *arg)
{
	int res;
	pthread_attr_t	attrs;
	pthread_t	thread;
	
	res = pthread_attr_init(&attrs);
	if (res) {
		SYMERROR("cannot init pthread attributes\n");
		return;
	}
	res = pthread_create(&thread, &attrs, bcmd, arg);
	if (res) {
		SYMERROR("error creating batch command thread\n");
		return;
	}
	res = pthread_detach(thread);
	if (res) {
		SYMERROR("error detaching batch command thread\n");
	}
	pthread_attr_destroy(&attrs);
}


static unsigned int get_ctag(void)
{
	unsigned int res;
	
	mutex_lock(&ctag_mutex);
	++ctag_counter;
	res = ctag_counter;
	mutex_unlock(&ctag_mutex);
	return res;
}


static int concat_outstr(struct cmdp *p, const char *chunk)
{
	int	len;
	int	step;
	char	*tmp;
	
	assert(p);
	assert(chunk);

	len = strlen(chunk);
	if (len <= 0) return 0;
	if (!p->outbuf) {
		p->outbuf = malloc(INITIAL_OUTBUF);
		if (!p->outbuf) return SYM_FAIL;
		memset(p->outbuf, 0, INITIAL_OUTBUF);
		(p->outbuf)[0] = '\r';
		(p->outbuf)[1] = '\n';
		p->outlen = 2;
		p->memlen = INITIAL_OUTBUF;
	}
	if (p->outlen + len >= p->memlen) {
		step = OUTBUF_REALLOC_STEP;
		if (step <= len) step = len + OUTBUF_REALLOC_STEP;
		tmp = realloc(p->outbuf, p->memlen + step);
		if (!tmp) return SYM_FAIL;
		p->outbuf = tmp;
		p->memlen += step;
		memset(p->outbuf + p->outlen, 0, (p->memlen - p->outlen));
	}
	memcpy(p->outbuf + p->outlen, chunk, len);
	p->outlen += len;
	return len;
}

static int console_mprintf(struct cmdp *p, const char *format, ...)
{
	int	res;
	char	*chunk = NULL;
	va_list	ap;

	assert(p);
	assert(format);
	va_start(ap, format);
	res = vasprintf(&chunk, format, ap);
	va_end(ap); 
	if (res > 0) {
		res = concat_outstr(p, chunk);
		free(chunk);
	}
	return res;
}

static int console_lprintf(const char *format, ...)
{
	int	res;
	char	*line = NULL;
	va_list	ap;

	assert(format);
	va_start(ap, format);
	res = vasprintf(&line, format, ap);
	va_end(ap); 
	if (res > 0) {
		yxt_log(yctx, line);
		free(line);
	}
	return res;
}


static int console_mputc(struct cmdp *p, int c)
{
	char str[2] = { '\000', '\000' };
	
	str[0] = c & 0xff;
	assert(p);
	return concat_outstr(p, (const char *)(&str[0]));
}


#define COMMANDS_PER_LINE	5
static void brief_help(struct cmdp *p)
{
	int i;
	int	ncmd = 1;
	const struct dcmd_s *cm;
	
	console_mprintf(p, "Available commands: \r\n    ");
	for (i = 0; cmdarray[i].cmd != NULL; i++) {
		cm = &cmdarray[i];
		console_mprintf(p, "%s  ", cm->cmd);
		ncmd++;
		if (ncmd >= 5) {
			console_mprintf(p, "\r\n    ");
			ncmd = 1;
		}
	}
	console_mprintf(p, "\r\n");

}

static void long_help(struct cmdp *p, char *cmd)
{
	int	i, res;
	const struct dcmd_s *cm = NULL;
	
	for (i = 0; cmdarray[i].cmd != NULL; i++) {
		cm = &cmdarray[i];
		res = strcmp(cmd, cm->cmd);
		if (res) continue;
		console_mprintf(p, "%s", cm->help);
		break;
	}
	if (cmdarray[i].cmd == NULL) console_mprintf(p, "No such command - %s, use help for list\r\n", cmd); 
	console_mprintf(p, "\r\n");
}

static void dcmd_help(struct cmdp *p)
{
	if ((p->argc < 2) || (!(p->argv)[1])) {
		brief_help(p);
		console_mprintf(p, "use help <command> for more info\r\n");
	} else long_help(p, (p->argv)[1]);
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

static int exec_command(struct cmdp *p)
{
	int	i;
	const struct dcmd_s *cm;
	
	if (p->argc < 1) return NO_COMMAND;
	for (i = 0; cmdarray[i].cmd != NULL; i++) {
		cm = &cmdarray[i];
		if (strcmp(cm->cmd, (p->argv)[0])) continue;
		(cm->command)(p);
		return CMD_OK;	
	}
	return NO_COMMAND;
}


int run_command(char *line, char **outbuf, int *outlen)
{
	char	*cmdline = NULL;
	int	argc;
	int	len;
	int	res = NO_COMMAND;
	struct cmdp p;
	char *g_argv[MAX_ARGS];
	
	len = strlen(line);
	if (len < 1) return NO_COMMAND;
	cmdline = strndup(line, len);
	if (!cmdline) {
		console_mprintf(&p, "run_command: cannot allocate memory\r\n");
		goto errout;
	}
	argc = argsep(cmdline, len, g_argv);
	if (argc < 1) {
		res = NO_COMMAND;
		goto out_with_line;
	}
	p.argc = argc;
	p.argv = g_argv;
	p.outbuf = NULL;
	p.outlen = 0;
	p.memlen = 0;
	res = exec_command(&p);
//	console_mprintf(p, "argc = %d\r\n", argc);
//	for (i = 0; i < argc; i++) {
//		printf("argv[%d] = %s\r\n", i, g_argv[i]);
//	}
	console_mprintf(&p, "\r\n");
	*outbuf = p.outbuf;
	*outlen = p.outlen;
out_with_line:
	if (cmdline) free(cmdline);
errout:
	return res;
}

#if 0
#define MAXDMP	16
static int dump_line(unsigned char *p, int offset, int len)
{
	int	i;
	
	if (len > MAXDMP) len = MAXDMP;
	console_mprintf(p, "%04x  ", offset);
	for (i = 0; i < len; i++) console_mprintf(p, "%02x ", p[i]);
	for (i = 0; i < (MAXDMP - len); i++) console_mprintf(p, "   ");
	console_mprintf(p, " |");
	for (i = 0; i < len; i++) {
		unsigned char c;
		c = p[i] & 0x7f;
		if ((c < ' ') || (c > '~')) c = '.';
		console_mputc(p, c);
	}
	for (i = 0; i < (MAXDMP - len); i++) console_mputc(p, ' ');
	console_mprintf(p, "|\r\n");
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



static void print_ring_mask(struct cmdp *p, uint32_t rmask)
{
	int	j;
	
	for (j = 31; j >= 0; j--) {
			if (rmask & (1 << j)) console_mprintf(p, "%c", (j % 10) + '0');
			else console_mprintf(p, "-");
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


static void dcmd_huadebug(struct cmdp *p)
{
	int	dbg;
	
	if ((p->argc < 2) || (!((p->argv)[1]))) {
		console_mprintf(p, "dcmd_huadebug: invalid arguments\r\n");
		return;
	}
	sscanf((p->argv)[1], "%u", &dbg);
	hua_debug_ctl(dbg);
	console_mprintf(p, "hua_debug_ctl(%d) called\r\n", dbg);
}


static void dcmd_list(struct cmdp *p)
{
	char	*ltype;
	char	*pattern = NULL;
	unsigned int ctag;
	struct list_params *lp;
	batch_cmd	bcmd = NULL;
	
	if ((p->argc < 2) || (!((p->argv)[1]))) {
		console_mprintf(p, "dcmd_list: invalid arguments\r\n");
		return;
	}
	ltype = (p->argv)[1];
	if ((p->argc > 2) && ((p->argv)[2])) pattern = (p->argv)[2];
	ctag = get_ctag();
	if (strcmp(ltype, "stations") == 0) {
		bcmd = list_stations;
	} else if (strcmp(ltype, "lines") == 0) {
		bcmd = list_lines;
	} else if (strcmp(ltype, "filters") == 0) {
		bcmd = list_filters;
	} else {
		console_mprintf(p, "don't know how to list \"%s\"\r\n", ltype);
	}
	if (bcmd) {
		lp = malloc(sizeof(struct list_params));
		if (!lp) {
			console_mprintf(p, "cannot allocate memory\r\n");
			return;
		}
		memset(lp, 0, sizeof(struct list_params));
		lp->ctag = ctag;
		lp->pattern = pattern;
		console_mprintf(p, "Printout follows, ctag=%u\r\n", ctag);
		submit_cmd(bcmd, (void *)lp);
	}
}

static void *list_stations(void *arg)
{
	struct list_params *lp;
	
	lp = (struct list_params *)arg;
	assert(lp);
	console_lprintf("[%u] ifi   station             B-chan   lines filter       state\n", lp->ctag);
	console_lprintf("[%u] -------------------------------------------------------------------\n", lp->ctag);
	(void)cfg_iterate(confdb, CFG_OBJ_STATION, list_iter, lp);
	console_lprintf("\n");
	free(lp);
	return NULL;
}

static void *list_lines(void *arg)
{
	struct list_params *lp;
	
	lp = (struct list_params *)arg;
	assert(lp);
	console_lprintf("[%u] line                                letter   state [lastdialed]\n", lp->ctag);
	console_lprintf("[%u] ---------------------------------------------------------------\n", lp->ctag);
	(void)cfg_iterate(confdb, CFG_OBJ_LINE, list_iter, lp);
	console_lprintf("\n");
	free(lp);
	return NULL;
}

static void *list_filters(void *arg)
{
	struct list_params *lp;
	
	lp = (struct list_params *)arg;
	assert(lp);
	console_lprintf("[%u] filter                          refcount\n", lp->ctag);
	console_lprintf("[%u] ----------------------------------------\n", lp->ctag);
	(void)cfg_iterate(confdb, CFG_OBJ_FILTER, list_iter, lp);
	console_lprintf("\n");
	free(lp);
	return NULL;
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
	struct list_params *lp;
	struct ccstation *st;
	struct symline *sl;
	struct filter *flt;
	int	i, numlines, res;
	const char *pattern;
	char	printout[DISPLAY_STRLEN + 1];
	char	numstr[MAX_NUMBER_LENGTH + 1];
	int	copylen;

	if (!object) {
		console_lprintf("NULL object\n");
		return 0;
	}
	lp = (struct list_params *)arg;
	assert(lp);
	pattern = (const char *)lp->pattern;
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
			memset(&numstr[0], 0, MAX_NUMBER_LENGTH + 1);
			copylen = sl->digits_len;
			if (copylen > MAX_NUMBER_LENGTH) copylen = MAX_NUMBER_LENGTH;
			if (copylen > 0) strncpy(&numstr[0], &sl->digits[0], copylen);

			snprintf(&printout[SLNAME_POS], SLLETTER_POS - SLNAME_POS, "%s", sl->name);
			snprintf(&printout[SLLETTER_POS], SLSTATE_POS - SLLETTER_POS, "%c", sl->number + 'a');
			snprintf(&printout[SLSTATE_POS], DISPLAY_STRLEN - SLSTATE_POS, "%s [%s]", 
				decode_linestate(sl->state), &numstr[0]);
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
	console_lprintf("[%u] %s\n", lp->ctag, printout);
	return 0;
}


static void dcmd_dmdebug(struct cmdp *p)
{
	int	dbg;
	
	if ((p->argc < 2) || (!((p->argv)[1]))) {
		console_mprintf(p, "dcmd_dmdebug: invalid arguments\r\n");
		return;
	}
	sscanf((p->argv)[1], "%u", &dbg);
	dcpmux_debug_ctl(dbg);
	console_mprintf(p, "dcpmux_debug_ctl(%d) called\r\n", dbg);
}

static struct symline *get_line_ptr(struct cmdp *p)
{
	struct symline *sl;
	char	*lname;
	
	if ((p->argc < 2) || (!((p->argv)[1]))) {
		console_mprintf(p, "line name must be specified\r\n");
		return NULL;
	}
	lname = (p->argv)[1];
	sl = (struct symline *)cfg_lookupname(confdb, CFG_OBJ_LINE, lname);
	if (!sl) {
		console_mprintf(p, "no such line \"%s\"\r\n", lname);
		return NULL;
	}
	return sl;
}


static struct ccstation *get_station_ptr(struct cmdp *p)
{
	struct ccstation *st;
	char	*sname;

	if ((p->argc < 2) || (!((p->argv)[1]))) {
		console_mprintf(p, "station name must be specified\r\n");
		return NULL;
	}
	sname = (p->argv)[1];
	st = (struct ccstation *)cfg_lookupname(confdb, CFG_OBJ_STATION, sname);
	if (!st) {
		console_mprintf(p, "no such station \"%s\"\r\n", sname);
		return NULL;
	}
	return st;
}

static void dcmd_line_enable(struct cmdp *p)
{
	struct symline *sl;
	int res;
	
	sl = get_line_ptr(p);
	if (sl) {
		res = cctl_enable(sl);
		console_mprintf(p, "cctl_enable() returns %d\r\n", res);
	}
}


static void dcmd_line_disable(struct cmdp *p)
{
	struct symline *sl;
	int res;
	
	sl = get_line_ptr(p);
	if (sl) {
		res = cctl_disable(sl);
		console_mprintf(p, "cctl_disable() returns %d\r\n", res);
	}
}


static void dcmd_line_select(struct cmdp *p)
{
	struct symline *sl;
	int res;
	
	sl = get_line_ptr(p);
	if (sl) {
		res = cctl_select(sl);
		console_mprintf(p, "cctl_select() returns %d\r\n", res);
	}
}


static void dcmd_line_unselect(struct cmdp *p)
{
	struct symline *sl;
	int res;
	
	sl = get_line_ptr(p);
	if (sl) {
		res = cctl_unselect(sl);
		console_mprintf(p, "cctl_unselect() returns %d\r\n", res);
	}
}


static void dcmd_line_hold(struct cmdp *p)
{
	struct symline *sl;
	int res;
	
	sl = get_line_ptr(p);
	if (sl) {
		res = cctl_hold(sl);
		console_mprintf(p, "cctl_hold() returns %d\r\n", res);
	}
}



static void dcmd_line_defsel(struct cmdp *p)
{
	int	defsel = 0;
	struct  ccstation *st;
	
	if ((p->argc < 3) || (!((p->argv)[1]))) return;
	st = get_station_ptr(p);
	sscanf((p->argv)[2], "%d", &defsel);
	st->defselect = defsel;
}



static void dcmd_line_hangup(struct cmdp *p)
{
	struct symline *sl;
	int res;
	
	sl = get_line_ptr(p);
	if (sl) {
		res = cctl_hangup(sl);
		console_mprintf(p, "cctl_hangup() returns %d\r\n", res);
	}
}



static void dcmd_line_status(struct cmdp *p)
{
	struct symline *sl;
	char	numstr[MAX_NUMBER_LENGTH + 1];
	int	copylen;
	
	sl = get_line_ptr(p);
	if (sl) {
		memset(&numstr[0], 0, MAX_NUMBER_LENGTH + 1);
		copylen = sl->digits_len;
		if (copylen > MAX_NUMBER_LENGTH) copylen = MAX_NUMBER_LENGTH;
		if (copylen > 0) strncpy(&numstr[0], &sl->digits[0], copylen);
		console_mprintf(p, "Line status:\r\n");
		console_mprintf(p, "\tName: %s\r\n", sl->name);
		console_mprintf(p, "\tDisplay name: %s\r\n", sl->displayname);
		console_mprintf(p, "\tState: %s (%d)\r\n", decode_linestate(sl->state), sl->state);
		console_mprintf(p, "\tSelected: %s\r\n", (sl->selected ? "yes" : "no"));
		console_mprintf(p, "\tLine is %s\r\n", sl->offhook ? "off-hook" : "on-hook");
		console_mprintf(p, "\tLine letter: \'%c\'\r\n", ('a' + sl->number));
		console_mprintf(p, "\tBLF sends dcp.event: %s\r\n", (sl->evtsend ? "yes" : "no"));
		console_mprintf(p, "\tBLF accepts dcp.command: %s\r\n", (sl->cmdrcv ?  "yes" : "no"));
		console_mprintf(p, "\tLine accepts symbiont.command: %s\r\n", (sl->xcontrol ?  "yes" : "no"));
		console_mprintf(p, "\tLast or current call: %s\r\n", 
				sl->outgoing ? "outgoing" : "incoming");
		if (copylen > 0) {
			console_mprintf(p, "\t%d digits collected: %s\r\n", copylen, &numstr[0]);
		} else console_mprintf(p, "\tno digits collected\r\n");
		if (sl->ccst) {
			console_mprintf(p, "\tBchan: %s\r\n", sl->ccst->bchan);
		}
		console_mprintf(p, "\tOur call id: %s\r\n", sl->ourcallid);
		console_mprintf(p, "\tParty call id: %s\r\n", sl->partycallid);
		console_mprintf(p, "\tCaller name: %s\r\n", sl->callername);
		console_mprintf(p, "\tCaller number: %s\r\n", sl->caller);
		console_mprintf(p, "\tCall tracking id: %s\r\n", sl->calltrack);
	} else {
		console_mprintf(p, "not initialized\r\n\n");
	}
}


static void dcmd_station_enable(struct cmdp *p)
{
	struct ccstation *st;
	int res;
	
	st = get_station_ptr(p);
	if (st) {
		res = st_enable(st);
		console_mprintf(p, "st_enable() returns %d\r\n", res);
	}
}

static void dcmd_station_disable(struct cmdp *p)
{
	struct ccstation *st;
	int res;
	
	st = get_station_ptr(p);
	if (st) {
		res = st_disable(st);
		console_mprintf(p, "st_disable() returns %d\r\n", res);
	}
}

static void dcmd_station_status(struct cmdp *p)
{
	struct ccstation *st;
	
	st = get_station_ptr(p);
	if (st) {
		console_mprintf(p, "Station status:\r\n");
		console_mprintf(p, "\tName: %s\r\n", st->name);
		console_mprintf(p, "\tBchan: %s\r\n", st->bchan);
		console_mprintf(p, "\tIFI: %d\r\n", st->ifi);
		console_mprintf(p, "\tState: %s (%d)\r\n", decode_st_state(st->state), st->state);
		console_mprintf(p, "\tLines configured: %d\r\n", st->numlines);
		console_mprintf(p, "\tSelected line: %d\r\n", st->selected);
		console_mprintf(p, "\tDefault selection: %d\r\n", st->defselect);
		console_mprintf(p, "Ring1 mask: ");
		print_ring_mask(p, st->ring1_mask);
		console_mprintf(p, "\r\nRing2 mask: ");
		print_ring_mask(p, st->ring2_mask);
		console_mprintf(p, "\r\nRing3 mask: ");
		print_ring_mask(p, st->ring3_mask);
		console_mprintf(p, "\r\nBeep mask: ");
		print_ring_mask(p, st->beep_mask);

		console_mprintf(p, "\r\n");
	} else {
		console_mprintf(p, "not initialized\r\n");
	}
}

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
#include <symbiont/symbiont.h>

#include "commands.h"
#include "console.h"


#define MAX_ARGS	10

static void dcmd_help(int argc, char **argv);
static void dcmd_open(int argc, char **argv);
static void dcmd_disconnect(int argc, char **argv);
static void dcmd_send(int argc, char **argv);
static void dcmd_watch(int argc, char **argv);
static void dcmd_unwatch(int argc, char **argv);
static void dcmd_status(int argc, char **argv);
static void dcmd_log(int argc, char **argv);
//static void dcmd_control(int argc, char **argv);

static int symtool_watcher(conn_ctx *ctx, yatemsg *msg, void *arg, struct dwhook *dwh);

static int argsep(char *line, int maxlen, char **argv);
static int exec_command(int argc, char **argv);

static char *g_argv[MAX_ARGS];

static struct dcmd_s cmdarray[] = {
	{	.cmd = "help",
		.help = "print this help",
		.command = dcmd_help },

	{	.cmd = "open",
		.help = "connect to yate extmodule socket",
		.command = dcmd_open },

	{	.cmd = "disconnect",
		.help = "close connection to yate",
		.command = dcmd_disconnect },

	{	.cmd = "send",
		.help = "send <message_name> [parm1=val1, parm2=val2...]\n    - dispatch the message to the yate", 
		.command = dcmd_send },

	{	.cmd = "watch",
		.help = "watch <message_name>|<*>\n    - start to watch & print the messages of type message_name or any", 
		.command = dcmd_watch },

	{	.cmd = "unwatch",
		.help = "unwatch <message_name>|<*>\n    - no longer watch & print messages of type message_name or any", 
		.command = dcmd_unwatch },

	{	.cmd = "status",
		.help = "status <string>\n    - sends engine.status message (similar to rmanager status command)", 
		.command = dcmd_status },

	{	.cmd = "log",
		.help = "log <string>\n    - sends <string> as a log message to the yate", 
		.command = dcmd_log },


/*
	{	.cmd = "control",
		.help = "control <component> <operation>\n    - sends chan.control message (similar to rmanager control command)", 
		.command = dcmd_unwatch },
 */

	{ .cmd = NULL, .help = NULL, .command = NULL }
};


static conn_ctx *gctx = NULL;
extern bool use_ip;
extern const char *connstring;


static void dcmd_help(int argc, char **argv)
{
	const struct dcmd_s *cm;
	int	i;

	for (i = 0; cmdarray[i].cmd != NULL; i++) {
		cm = &cmdarray[i];
		console_mprintf("%s - %s\n\r", cm->cmd, cm->help);
	}
	console_mprintf("\n");
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





static void dcmd_open(int argc, char **argv)
{
	int	res; 
	
	if (gctx) {
		console_mprintf("connection already open. \n"
		"use \"disconnect first\"\n");
	} else {
		if (use_ip) gctx = yxt_conn_tcp((char *)connstring, "global");
		else gctx = yxt_conn_unix((char *)connstring, "global");
		if (!gctx) {
			console_mprintf("cannot connect to %s\n", connstring);
			return;
		}
		res = yxt_run(gctx, 3);
		assert(res == YXT_OK);
		console_mprintf("connected to yate via %s\n", connstring);
	}
}


static void dcmd_disconnect(int argc, char **argv)
{
	if (!gctx) {
		console_mprintf("connection not open\n");
	} else {
		yxt_disconnect(gctx);
		gctx = NULL;
		console_mprintf("disconnected - all watchers are removed\n");
	}
}


static void dcmd_send(int argc, char **argv)
{
	int	res, i;
	yatemsg *msg = NULL;
	yatemsg *reply = NULL;
	char	*eqsign, *valptr, *comma;
	int	arglen;
	
	if ((argc < 2) || (!argv[1])) {
		console_mprintf("dcmd_send: invalid arguments\n");
		return;
	}
	if (!gctx) {
		console_mprintf("not connected\n");
		return;
	}
	msg = alloc_message(argv[1]);
	if (!msg) return;
	for (i = 2; i < argc; i++) {
		if (argv[i]) {
			valptr = NULL;
			arglen = strlen(argv[i]);
			eqsign = strchr(argv[i], '=');
			if (eqsign) *eqsign = '\000';
			if ((argv[i] + arglen) > eqsign) valptr = eqsign + 1;
			comma = strrchr(valptr, ',');
			if (comma) *comma = '\000';
			if (eqsign && valptr) set_msg_param(msg, argv[i], valptr);
			else {
				console_mprintf("dcmd_send: invalid argument #%d\n", i);
				goto out;
			}
		}
	}
	res = yxt_dispatch(gctx, msg, &reply);
	console_mprintf("%s sent..\n", argv[1]);
	if (reply) {
		console_mprintf("  reply: \"%s\"\n", get_msg_retvalue(reply));
	} else {
		console_mprintf("no reply - res=%d\n", res);
	}
	console_mprintf("returned id=%s\n", get_msg_param(reply, "id"));
out:
	if (msg) free_message(msg);
	if (reply) free_message(reply);
}

int symtool_watcher(conn_ctx *ctx, yatemsg *msg, void *arg, struct dwhook *dwh)
{
	assert(msg);
	dump_message(msg);
	console_mrefresh();
	return YXT_OK;
}

static void dcmd_watch(int argc, char **argv)
{
	int	res;
	
	if ((argc < 2) || (!argv[1])) {
		console_mprintf("dcmd_watch: invalid arguments\n");
		return;
	}
	if (!gctx) {
		console_mprintf("not connected\n");
		return;
	}

	res = yxt_add_watcher(gctx, symtool_watcher, argv[1], NULL);
	if (res == YXT_OK) {
		console_mprintf("installed watcher for %s messages\n", argv[1]);
	} else {
		console_mprintf("watcher not installed - res=%d\n", res);
	}
}


static void dcmd_unwatch(int argc, char **argv)
{
	int	res;
	
	if ((argc < 2) || (!argv[1])) {
		console_mprintf("dcmd_unwatch: invalid arguments\n");
		return;
	}

	if (!gctx) {
		console_mprintf("not connected\n");
		return;
	}
	res = yxt_remove_watcher(gctx, argv[1]);
	if (res == YXT_OK) {
		console_mprintf("removed watcher for %s messages\n", argv[1]);
	} else {
		console_mprintf("watcher not removed - res=%d\n", res);
	}
}



static void dcmd_status(int argc, char **argv)
{
	int	res;
	yatemsg	*msg = NULL;
	yatemsg *reply = NULL;
	
	if ((argc < 2) || (!argv[1])) {
		console_mprintf("dcmd_status: invalid arguments\n");
		return;
	}
	if (!gctx) {
		console_mprintf("not connected\n");
		return;
	}
	msg = alloc_message("engine.status");
	if (!msg) return;
	set_msg_param(msg, "module", argv[1]);
	set_msg_param(msg, "cmd_machine", "false");
	
	res = yxt_dispatch(gctx, msg, &reply);
	if (reply) {
		console_mprintf("status:\n%s\n", get_msg_retvalue(reply));
		free_message(reply);
	} else {
		console_mprintf("no reply to status message - res=%d\n", res);
	}
	if (msg) free_message(msg);
}


static void dcmd_log(int argc, char **argv)
{
	char	*logstr;
	
	if ((argc < 2) || (!argv[1])) {
		console_mprintf("dcmd_status: invalid arguments\n");
		return;
	}
	if (!gctx) {
		console_mprintf("not connected\n");
		return;
	}
	logstr = argv[1];
	if (strlen(logstr) <= 0) {
		console_mprintf("empty log message\n");
		return;
	}
	(void)yxt_log(gctx, logstr);
}


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
#include <symbiont/symbiont.h>
#include <symbiont/dcpmux.h>

#include "commands.h"
#include "console.h"


#define MAX_ARGS	4
#define MAX_PACKET	32

extern hua_ctx	*hctx;


static void dcmd_help(int argc, char **argv);

static void dcmd_open(int argc, char **argv);
static void dcmd_disconnect(int argc, char **argv);
static void dcmd_send(int argc, char **argv);
static void dcmd_status(int argc, char **argv);
static void dcmd_ifadd(int argc, char **argv);
static void dcmd_ifup(int argc, char **argv);
static void dcmd_ifdown(int argc, char **argv);
static void dcmd_ifdel(int argc, char **argv);
static void dcmd_ifstate(int argc, char **argv);
static void dcmd_huadebug(int argc, char **argv);
static void dcmd_dmdebug(int argc, char **argv);


static int rcv_handler(void *arg, int ifi, bool cmdflag, uint8_t *data, ssize_t length);
static int parse_line(unsigned char *buf, char *line);
static int dump_line(unsigned char *p, int offset, int len);
static void dump_pkt(unsigned char *p, int len);
static bool dm_running(void);

static void brief_help(void);
static void long_help(char *cmd);

static int argsep(char *line, int maxlen, char **argv);
static int exec_command(int argc, char **argv);

static char *g_argv[MAX_ARGS];

static struct dcmd_s cmdarray[] = {
	{	.cmd = "help",
		.help = "print this help",
		.command = dcmd_help },

	{	.cmd = "open",
		.help = "Attach DCPMUX to the HUA link and activate workers",
		.command = dcmd_open },

	{	.cmd = "disconnect",
		.help = "delete all interfaces, stop worker threads and destroy the context",
		.command = dcmd_disconnect },

	{	.cmd = "send",
		.help = "send <ifi> <c|r> <data> \n"
			"    - send Command|Response DCP data to the ifi where\n"
			"      <data> is a hex bytes (eg. 00 a3 21 c5 c5 33 44 etc)\n"
			"      or double-quotes enclosed text", 
		.command = dcmd_send },

	{	.cmd = "status",
		.help = "status \n    - display dcp link info", 
		.command = dcmd_status },

	{	.cmd = "ifadd",
		.help = "ifadd <ifi> [lcn]\n    - add an interface to the dcpmux context", 
		.command = dcmd_ifadd },

	{	.cmd = "ifdel",
		.help = "ifdel <ifi>\n    - delete the interface from dcpmux context", 
		.command = dcmd_ifdel },

	{	.cmd = "ifup",
		.help = "ifup <ifi>\n    - activate the interface, dcpmux will try to bring up the DCP link", 
		.command = dcmd_ifup },

	{	.cmd = "ifdown",
		.help = "ifdown <ifi>\n    - shutdown DCP link on the interface", 
		.command = dcmd_ifdown },

	{	.cmd = "ifstate",
		.help = "ifstate <ifi>\n    - show the interface state", 
		.command = dcmd_ifstate },

	{	.cmd = "hua_debug",
		.help = "hua_debug <dbgval>    - call hua_debug_ctl() with <dbgval> as an argument", 
		.command = dcmd_huadebug },

	{	.cmd = "dcpmux_debug",
		.help = "dcpmux_debug <dbgval>    - call dcpmux_debug_ctl() with <dbgval> as an argument", 
		.command = dcmd_dmdebug },


	{ .cmd = NULL, .help = NULL, .command = NULL }
};

static dcpmux_t *dm = NULL;
static int xmt_pktno = 1;
static int rcv_pktno = 1;

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
		console_mprintf("dcpmuxtool - dcpmux, dcphdlc & HUA layers testing tool\n");
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


static int parse_line(unsigned char *buf, char *line)
{
	int	len;
	int	blen;
	char	*p;
	char	c;
	unsigned int byte;
	int	res;
	
	assert(buf);
	assert(line);
	len = strlen(line);
	blen = 0;
	p = line;
	while (len && (blen < MAX_PACKET)) {
		c = *p;
		if (isxdigit(c)) {
			sscanf(p, "%x%n", &byte, &res);
//			printf("byte = %0x, res=%0d\n", byte, res);
			p += res;
			len -= res;
			buf[blen++] = byte & 0xff;
		} else if (c == '"') {
			p++;
			len--;
			while (len && (blen < MAX_PACKET)) {
				c = *p++;
				len--;
				if (c == '"') break;
				buf[blen++] = c;
			}
		} else {
			p++;
			len--;
		}
	}
	return blen;
}

static int rcv_handler(void *arg, int ifi, bool cmdflag, uint8_t *data, ssize_t length)
{
	if (!data) {
		if (length == DCPMUX_TRANSPORT_UP) 
			console_mprintf("Link is up for ifi %d\n", ifi);
		else if (length == DCPMUX_TRANSPORT_DOWN)
			console_mprintf("Links is down for ifi %d\n", ifi);
		console_mrefresh();
	} else {
		console_mprintf("RCV %s packet #%d for ifi=%d, data @%p, len=%d\n",
			(cmdflag ? "command" : "response" ), rcv_pktno, ifi, data, length);
		rcv_pktno++;
		if (data && (length > 0)) dump_pkt(data, length);
		else console_mrefresh();
	}
	return SYM_OK;
}

static bool dm_running(void)
{
	bool	res;
	
	res = (dm != NULL);
	if (!res) console_mprintf("not connected\n");
	return res;
}

static void dcmd_open(int argc, char **argv)
{
	int	res; 
	
	if (dm) {
		console_mprintf("connection already open. \n"
		"use \"disconnect first\"\n");
	} else {
		dm = dummy_dcpmux();
		if (!dm) {
			console_mprintf("cannot create dummy dcpmux context\n");
			return;
		}
		res = dcpmux_attach(dm, hctx, rcv_handler, NULL);
		if (res != SYM_OK) {
			console_mprintf("cannot attach dcpmux\n");
			return;
		}
		xmt_pktno = 1;
		rcv_pktno = 1;
		console_mprintf("dcpmux activated\n");
	}
}


static void dcmd_disconnect(int argc, char **argv)
{
	int	res;
	
	if (!dm) {
		console_mprintf("connection not open\n");
	} else {
		res = dcpmux_close(dm);
		if (res != SYM_OK) {
			console_mprintf("cannot shutdown dcpmux\n");
		} else {
			console_mprintf("dcpmux shut down, context freed\n");
			dm = NULL;
		}
	}
}

/* send <ifi> <c|r> <data> */
static void dcmd_send(int argc, char **argv)
{
	int	res;
	int	ifi = -1;
	bool	cmdflag = false;
	uint8_t	data[MAX_PACKET+2];
	int	datalen;
	
	if ((argc < 4) || (!argv[1])) {
		console_mprintf("dcmd_send: invalid arguments\n");
		return;
	}
	if (!dm_running()) return;
	
	sscanf(argv[1], "%u", &ifi);
	if (ifi < 0) {
		console_mprintf("invalid ifi=\"%s\" (must be 0..MAXINT)\n", argv[1]);
		return;
	}
	
	if (!argv[2]) {
		console_mprintf("cmdflag must be specified (either c or r)\n");
		return;
	}
	if ((argv[2])[0] == 'c') cmdflag = true;
	else {
		if ((argv[2])[0] == 'r') cmdflag = false;
		else {
			console_mprintf("invalid cmdflag \"%s\"- mut be c or r\n", argv[2]);
			return;
		}
	}
	if (!argv[3]) {
		console_mprintf("nothing to send - no data specified\n");
		return;
	}
	memset(data, 0, MAX_PACKET+2);
	datalen = parse_line(data, argv[3]);
	if (datalen < 1) {
		console_mprintf("unable to parse data - nothing to send\n");
		return;
	}
	
	console_mprintf("sending %s packet #%d, for ifi=%d\n", cmdflag ? "command" : "response" ,
			xmt_pktno, ifi);
	res = dcpmux_send(dm, ifi, cmdflag, data, datalen);
	if (res < 0) console_mprintf("error sending data\n");
	xmt_pktno++;
}

static void dcmd_status(int argc, char **argv)
{
	
	if (dm) console_mprintf("dcpmux running\n");
	else console_mprintf("dcpmux shut down\n");
}

static void dcmd_ifadd(int argc, char **argv)
{
	int	ifi;
	int	lcn = -1;
	int	res;
	struct dcpmux_ifparams ifp;
	
	if ((argc < 2) || (!argv[1])) {
		console_mprintf("dcmd_ifadd: invalid arguments\n");
		return;
	}
	if (!dm_running()) return;
	sscanf(argv[1], "%u", &ifi);
	if (argc > 2) {
		if (argv[2]) sscanf(argv[2], "%u", &lcn);
	}
	memset(&ifp, 0, sizeof(struct dcpmux_ifparams));
	ifp.lcn = lcn;
	res = dcpmux_ifadd(dm, ifi, ((lcn >= 0) ? &ifp : NULL));
	if (res == SYM_OK) {
		console_mprintf("added interface %d, ", ifi);
		if (lcn < 0) console_mprintf("default LCN\n");
		else console_mprintf("lcn=%d (%x hex)\n", lcn, lcn);
	} else console_mprintf("error adding interface\n");
}

static void dcmd_ifup(int argc, char **argv)
{
	int	ifi, res;
	
	if ((argc < 2) || (!argv[1])) {
		console_mprintf("dcmd_ifup: invalid arguments\n");
		return;
	}
	if (!dm_running()) return;
	sscanf(argv[1], "%u", &ifi);
	res = dcpmux_ifup(dm, ifi);
	if (res == SYM_OK) console_mprintf("interface %d enabled\n", ifi);
	else console_mprintf("error enabling interface %d\n", ifi);
}

static void dcmd_ifdown(int argc, char **argv)
{
	int	ifi, res;
	
	if ((argc < 2) || (!argv[1])) {
		console_mprintf("dcmd_ifdown: invalid arguments\n");
		return;
	}
	if (!dm_running()) return;
	sscanf(argv[1], "%u", &ifi);
	res = dcpmux_ifdown(dm, ifi);
	if (res == SYM_OK) console_mprintf("interface %d disabled\n", ifi);
	else console_mprintf("error disabling interface %d\n", ifi);
}

static void dcmd_ifdel(int argc, char **argv)
{
	int	ifi, res;
	
	if ((argc < 2) || (!argv[1])) {
		console_mprintf("dcmd_ifdel: invalid arguments\n");
		return;
	}
	if (!dm_running()) return;
	sscanf(argv[1], "%u", &ifi);
	res = dcpmux_ifdel(dm, ifi);
	if (res == SYM_OK) console_mprintf("interface %d deleted\n", ifi);
	else console_mprintf("error deleting interface %d\n", ifi);
}



static void dcmd_huadebug(int argc, char **argv)
{
	int	dbg;
	
	if ((argc < 2) || (!argv[1])) {
		console_mprintf("dcmd_huadebug: invalid arguments\n");
		return;
	}
	if (!dm_running()) return;
	sscanf(argv[1], "%u", &dbg);
	hua_debug_ctl(dbg);
	console_mprintf("hua_debug_ctl(%d) called\n", dbg);
}

static void dcmd_dmdebug(int argc, char **argv)
{
	int	dbg;
	
	if ((argc < 2) || (!argv[1])) {
		console_mprintf("dcmd_dmdebug: invalid arguments\n");
		return;
	}
	if (!dm_running()) return;
	sscanf(argv[1], "%u", &dbg);
	dcpmux_debug_ctl(dbg);
	console_mprintf("dcpmux_debug_ctl(%d) called\n", dbg);
}


static void dcmd_ifstate(int argc, char **argv)
{
	int	ifi, res;
	
	if ((argc < 2) || (!argv[1])) {
		console_mprintf("dcmd_ifstate: invalid arguments\n");
		return;
	}
	if (!dm_running()) return;
	sscanf(argv[1], "%u", &ifi);
	res = dcpmux_ifstate(dm, ifi);
	switch (res) {
		case DCPMUX_IFSTATE_NOTFOUND:
			console_mprintf("interface %d not found\n", ifi);
			break;
		case DCPMUX_IFSTATE_INACTIVE:
			console_mprintf("interface %d inactive\n", ifi);
			break;
		case DCPMUX_IFSTATE_RUNNING:
			console_mprintf("interface %d is up\n", ifi);
			break;
		default:
			console_mprintf("interface %d is in the unknown state %d\n", ifi, res);
	}
}

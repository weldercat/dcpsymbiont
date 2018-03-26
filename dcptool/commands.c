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

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <dahdi/user.h>
#include "commands.h"
#include "trace.h"
#include "console.h"


#define MAX_ARGS	10
#define ZAP_CTL		"/dev/dahdi/ctl"
#define ZAP_CHAN	"/dev/dahdi/channel"

static void dcmd_help(int argc, char **argv);
static void dcmd_rxmute(int argc, char **argv);
static void dcmd_dtmf(int argc, char **argv);
static void dcmd_loop(int argc, char **argv);
static void dcmd_release(int argc, char **argv);
static int argsep(char *line, int maxlen, char **argv);
static int exec_command(int argc, char **argv);
static void set_span_number(int fd);
static int chan_open(int num);

static char *g_argv[MAX_ARGS];
static int zapfd = -1;
static int bch1_fd = -1;
static int bch2_fd = -1;

static int tone_mode = 0;
static int spanno = -1;

static struct dcmd_s cmdarray[] = {
	{	.cmd = "help",
		.help = "print this help",
		.command = dcmd_help },

	{	.cmd = "rxmute",
		.help = "rxmute on|off - mute/unmute B-channel RX (towards host)",
		.command = dcmd_rxmute },

	{	.cmd = "dtmf",
		.help = "dtmf on|off - switch hw DTMF detector on or off",
		.command = dcmd_dtmf },

	{	.cmd = "loop",
		.help = "loop local|remote|off - set IBC loopback mode", 
		.command = dcmd_loop },

	{	.cmd = "release",
		.help = "release b-channels", 
		.command = dcmd_release },


	{ .cmd = NULL, .help = NULL, .command = NULL }
};


static int chan_open(int num)
{
	int	fd;
	int	res;
	
	fd = open(ZAP_CHAN, O_RDWR);
	if (fd < 0) {
		console_mprintf("cannot open device %s: %s\n", ZAP_CHAN,
			strerror(errno));
		return -1;
	}
	res = ioctl(fd, DAHDI_SPECIFY, &num);
	if (res) {
		console_mprintf("cannot specify channel: %s\n", strerror(errno));
		return -1;
	}
	return fd;
}

static void set_span_number(int fd)
{
	struct dahdi_params	p;
	int	res, b1, b2;
	
	
	memset(&p, 0, sizeof(p));
	res = ioctl(fd, DAHDI_GET_PARAMS, &p);
	if (res) {
		console_mprintf("dcptool: cannot get span info: %s\n",
			strerror(errno));
		return;
	}
	spanno = p.spanno;
	b1 = p.channo - (p.chanpos - 1);
	b2 = p.channo - (p.chanpos - 2);
	console_mprintf("Span number: %d, channo: %d, B1 chan: %d, B2 chan: %d\n", 
		spanno, p.channo, b1, b2); 
	zapfd = open(ZAP_CTL, O_RDWR);
	if (zapfd < 0) {
		console_mprintf("Unable to open %s: %s\n", ZAP_CTL,
			strerror(errno));
	}
	bch1_fd = chan_open(b1);
	bch2_fd = chan_open(b2);
}


static void dcmd_help(int argc, char **argv)
{
	const struct dcmd_s *cm;
	int	i;

	for (i = 0; cmdarray[i].cmd != NULL; i++) {
		cm = &cmdarray[i];
		console_mprintf("%s - %s\n\r", cm->cmd, cm->help);
	}
	console_mprintf("Any other string is assumed to be a sequence \n"
			"of hex-coded bytes or a quoted text\n"
			"and will be sent to the DCP terminal as command.\n");
}

static void dcmd_loop(int argc, char **argv)
{
	struct dahdi_maintinfo	m;
	int	res;
	
	if ((argc < 2) || (!argv[1])) {
		console_mprintf("dcmd_loop: invalid arguments\n");
		return;
	}
	if (strcmp(argv[1], "local") == 0) {
		m.command = DAHDI_MAINT_LOCALLOOP;
	} else if (strcmp(argv[1], "remote") == 0) {
		m.command = DAHDI_MAINT_LOOPUP;
	} else if (strcmp(argv[1], "off") == 0) {
		m.command = DAHDI_MAINT_NONE;
	} else {
		console_mprintf("invalid loop mode %s\n", argv[1]);
		return;
	}
	m.spanno = spanno;
	res = ioctl(zapfd, DAHDI_MAINT, &m);
	if (res) {
		console_mprintf("error setting loopback mode: %s\n",
			strerror(errno));
	} else {
		console_mprintf("Loop mode is set to \"%s\"\n", argv[1]);
	}
}



static void dcmd_release(int argc, char **argv)
{
	int	res;
	
	if (argc != 1) {
		console_mprintf("dcmd_release: invalid arguments\n");
		return;
	}
	console_mprintf("Releasing B-channels - DTMF/RXMUTE/LOOPBACK controls will become unavailable...\n");
	res = close(bch1_fd);
	if (res) console_mprintf("error closing bchan 1: %s\n",
			strerror(errno));
	res = close(bch2_fd);
	if (res) console_mprintf("error closing bchan 2: %s\n",
			strerror(errno));
}


static void dcmd_rxmute(int argc, char **argv)
{
	int	res;
	
	if ((argc < 2) || (!argv[1])) {
		console_mprintf("dcmd_rxmute: invalid arguments\n");
		return;
	}
	if (strcmp(argv[1], "on") == 0) {
		tone_mode  |= DAHDI_TONEDETECT_MUTE;
	} else if (strcmp(argv[1], "off") == 0) {
		tone_mode &= ~DAHDI_TONEDETECT_MUTE;
	} else {
		console_mprintf("dcmd_rxmute: invalid argument: %s\n", argv[1]);
		return;
	}
//	m.spanno = spanno;
	console_mprintf("Setting RX mute to %s\n", argv[1]);
	res = ioctl(bch1_fd, DAHDI_TONEDETECT, &tone_mode);
	if (res) console_mprintf("error controlling RX mute on bchan 1: %s\n",
			strerror(errno));

	res = ioctl(bch2_fd, DAHDI_TONEDETECT, &tone_mode);
	if (res) console_mprintf("error controlling RX mute on bchan 2: %s\n",
			strerror(errno));
}


static void dcmd_dtmf(int argc, char **argv)
{
	int	res;
	
	if ((argc < 2) || (!argv[1])) {
		console_mprintf("dcmd_dtmf: invalid arguments\n");
		return;
	}
	if (strcmp(argv[1], "on") == 0) {
		tone_mode  |= DAHDI_TONEDETECT_ON;
	} else if (strcmp(argv[1], "off") == 0) {
		tone_mode &= ~DAHDI_TONEDETECT_ON;
	} else {
		console_mprintf("dcmd_dtmf: invalid argument: %s\n", argv[1]);
		return;
	}
	//m.spanno = spanno;
	console_mprintf("Setting hw DTMF to %s\n", argv[1]);
	res = ioctl(bch1_fd, DAHDI_TONEDETECT, &tone_mode);
	if (res) console_mprintf("error controlling hw DTMF on bchan 1: %s\n",
			strerror(errno));
	res = ioctl(bch2_fd, DAHDI_TONEDETECT, &tone_mode);
	if (res) console_mprintf("error controlling hw DTMF on bchan 2: %s\n",
			strerror(errno));
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


int run_command(char *line, int fd)
{
	__label__ errout;
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
	if (spanno < 0) {
		set_span_number(fd);
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

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
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/time.h>
#include <assert.h>
#include <ctype.h>
#include <curses.h>
#include <readline/history.h>
#include <symbiont/symbiont.h>
#include <symbiont/hua.h>
#include <symbiont/dcpmux.h>
#include <symbiont/cfdb.h>
#include <symbiont/cfload.h>

#include "console.h"
#include "commands.h"




const char *histfile = "/tmp/.cctool.history";
const char *deflog = "./cctool.log";
const char *defconffile = "/etc/symbiont/dcpsym.conf";

static FILE	*log = NULL;
static bool exit_request = false;
bool use_ip = false;
char *hconnstring = NULL;
char *yconnstring = NULL;
hua_ctx		*hctx = NULL;
conn_ctx	*yctx = NULL;
dcpmux_t	*dctx = NULL;
cfdb		*confdb = NULL;
struct global_params gparm;

/* from ccmain.c */

int ccinit(const char *cffile);

static void usage(void);
static void print_help(void);
static int run(const char *hsockname, const char *ysockname, const char *logfile, const char *cffile);
static int process_line(char *line);
static int dummy_vprintf(const char *format, va_list ap);




static int process_line(char *line)
{
	static int cmdno = 1;
	int	res;
	
	if (!line) {
		exit_request = true; 
		return 0;
	} 
	if (*line == '\000') {
		return 0;
	}
	res = run_command(line);
	if (res == CMD_OK) ++cmdno;
	else {
		console_mprintf("ERROR - invalid command\n\n");
		console_mrefresh();
	}
//	console_srefresh();
	return 0;
}


static void usage(void)
{
	printf("symbiont call control testing tool\n");
	printf("Usage: cctool [options]\n");
	printf("where options are:\n");
	printf("        -h - print this text\n");
	printf("        -c <filename> - config file name (default is %s)\n", defconffile);
	printf("	-p <ppid> - SCTP ppid to connect to HUA gateway \n");
	printf("        -a <ip_addr:port_ppid> - HUA gateway address:port\n");
	printf("	-y <ip_addr:port> - yate extmodule address:port\n");
	printf("	-u <sockpath> - yate extmodule unix socket path\n");
	printf("        -l <logfile> - specify session log file.\n");
	printf("                       cctool appends data to the logfile if it exists.\n");
	printf("		-y and -u are mutually exclusive\n");
	printf("\n");
}

static void print_help(void)
{
	console_mprintf("\nuse \"help\" to get help on commands\n");
	console_mprintf("Use \" quotes to enclose text containing commas and spaces\n");
	console_mprintf("CTRL-D to exit...\n\n");
	console_mrefresh();
	console_srefresh();
}


static int run(const char *hsockname, const char *ysockname, const char *logfile, const char *cffile)
{
	int	res;
	struct timeval tv;
	fd_set	rfds;
	
	if (logfile) {
		log = fopen(logfile, "a");
		if (!log) {
			SYMERROR("error opening log file %s: %s\n", logfile, strerror(errno));
			return 2;
		}
	}
	assert(hsockname);
	assert(ysockname);
	assert(cffile);
	hconnstring = strdup(hsockname);
	yconnstring = strdup(ysockname);
	
	res = console_init(process_line);
	if (res != CONSOLE_OK) return 2;
	print_help();
	
	res = read_history(histfile);
	if (res) {
		console_mprintf("cannot read history file: %s\n", strerror(errno));
		console_mrefresh();
	}
	stifle_history(1000);
//	symtrace_hookctl(false, console_vmprintf);
	hctx = hua_connect(hconnstring, NULL, NULL, 0);
	if (!hctx) {
		SYMERROR("cannot connect to HUA server %s\n", hconnstring);
		goto errout;
	}
	if (use_ip) yctx = yxt_conn_tcp(yconnstring, "global");
	else yctx = yxt_conn_unix(yconnstring, "global");
	if (!yctx) {
		SYMERROR("cannot connect to yate at %s\n", yconnstring);
		goto errout;
	}
	
	dctx = dummy_dcpmux();
	assert(dctx);
	
	console_mprintf("connected to YATE extmodule & HUA link...\n");
	res = ccinit(cffile);
	assert(res == SYM_OK);
	console_mrefresh();
	console_sprintf("cctool");
	console_srefresh();
	
//	memset(&dstate, 0, sizeof(dstate));	
//	res = dcp_init_hdlc(&dstate, dchan, DCP_LCN_CHAN0);
//	if (res != YXT_OK) {
//		SYMERROR("Cannot initialize dcp engine: %d\n", res);
//		goto	errout;
//	}
	
	while (!exit_request) {
		FD_ZERO(&rfds);
		FD_SET(fileno(stdin), &rfds);
//		FD_SET(dstate.fd, &rfds);
		
		tv.tv_sec = 10;
		tv.tv_usec = 0;
//		if (dstate.retransmit) {
//			if (dstate.retransmit > 1000) tv.tv_sec = dstate.retransmit / 1000;
//			tv.tv_usec = (dstate.retransmit % 1000) * 1000;
//		}

		res = select(FD_SETSIZE, &rfds, NULL, NULL, &tv);
		if (res < 0) {
			SYMERROR("cctool: select error %s", strerror(errno));
			goto out;
		}
		if (FD_ISSET(fileno(stdin), &rfds)) {
			
			res = console_input(console_getch());
			if (res != CONSOLE_OK) goto errout;

		};
//		if (FD_ISSET(dstate.fd, &rfds)) {
//			input_data(dstate.fd);
//		} else {
//			dcp_run(&dstate);
//		}
		
	}

	res = write_history(histfile);
	if (res) printf("cannot write history: %s\n", strerror(errno));

	printf("cctool: user requested exit\n");
	
out:	console_rundown();
	if (log) fclose(log);
	return 0;
errout:
	console_rundown();
	if (log) fclose(log);
	return 3;
}



static int dummy_vprintf(const char *format, va_list ap)
{
	return 0;
}



int main(int argc, char **argv)
{
	int	ch, result = 0;
	const char *cfname = NULL;
	char	*logname = NULL;
	char	*hsockname = NULL;
	char	*ysockname = NULL;
	char	logpath[257];
	char	cfpath[257];
	char	hsockpath[257];
	char	ysockpath[257];
	int	ppid = 1;
//	bool	ppid_set = false;
	bool	tcp_set = false;
	bool	unix_set = false;

	memset(logpath, 0, 257);
	while ((ch = getopt(argc, argv, "hy:u:p:a:l:c:")) != -1) {
		switch (ch) {
			case 'l':
				sscanf(optarg, "%250s", (char *)&logpath);
				logname = logpath;
				break;
			case 'c':
				sscanf(optarg, "%250s", (char *)&cfpath);
				cfname = cfpath;
				break;
			case 'p':
				sscanf(optarg, "%u", &ppid);
//				ppid_set = true;
				break;
			case 'a':
				sscanf(optarg, "%250s", (char *)&hsockpath);
				hsockname = hsockpath;
				break;
			case 'y':
				sscanf(optarg, "%250s", (char *)&ysockpath);
				ysockname = ysockpath;
				tcp_set = true;
				use_ip = true;
				break;
			case 'u':
				sscanf(optarg, "%250s", (char *)&ysockpath);
				ysockname = ysockpath;
				unix_set = true;
				break;
			case 'h':
				usage();
				goto out;
			default:
				result = 1;
				goto out;
		}
	}
	if ((argc > optind) || (argc == 1)) {
		printf("cctool: use \"-h\" for help\n");
		result = 1;
		goto out;
	}
	if (!hsockname) {
		printf("cctool: HUA gateway addr:port must be specified\n");
		result = 1;
		goto out;
	}
	if (unix_set && tcp_set) {
		printf("cctool: -y and -u are mutually exclusive\n");
		result = 1;
		goto out;
	}
	if (!ysockname) {
		printf("cctool yate extmodule socket or addr:port must be specified\n");
		result = 1;
		goto out;
	}

//	if (!logname) logname = (char *)deflog;
	/* cannot trace to stdout - syslog only */
	if (!cfname) cfname = defconffile;
	symtrace_hookctl(true, dummy_vprintf);
	result = run(hsockname, ysockname, (const char *)logname, (const char *)cfname);

out:	return result;
};



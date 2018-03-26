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
#include <symbiont/symbiont.h>
#include <symbiont/hua.h>
#include <readline/history.h>

#include "console.h"
#include "commands.h"



const char *histfile = "/tmp/.dcpmux.history";
const char *deflog = "./dcpmuxtool.log";
static FILE	*log = NULL;
static bool exit_request = false;
bool use_ip = false;
const char *connstring = NULL;
hua_ctx	*hctx = NULL;


static void usage(void);
static void print_help(void);
static int run(const char *sockname, const char *logfile);
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
//	add_history(line);
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
	printf("dcpmux testing tool\n");
	printf("Usage: dcpmuxtool [options]\n");
	printf("where options are:\n");
	printf("        -h - print this text\n");
	printf("	-p <ppid> - SCTP ppid to connect to HUA gateway \n");
	printf("        -a <ip_addr:port> - HUA gateway address:port\n");
	printf("        -l <logfile> - specify session log file.\n");
	printf("                       dcpmuxtool appends data to the logfile if it exists.\n");
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


static int run(const char *sockname, const char *logfile)
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
	connstring = sockname;
	
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
	hctx = hua_connect(sockname, NULL, NULL, 0);
	if (!hctx) {
		SYMERROR("cannot connect to HUA server %s\n", sockname);
		goto errout;
	}
	console_sprintf("dcpmuxtool ");
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
			SYMERROR("dcpmuxtool: select error %s", strerror(errno));
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
	printf("dcpmuxtool: user requested exit\n");
	
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
	char	*logname = NULL;
	char	*sockname = NULL;
	char	logpath[257];
	char	sockpath[257];
	int	ppid = 1;
	bool	ppid_set = false;

	memset(logpath, 0, 257);
	while ((ch = getopt(argc, argv, "hp:a:l:")) != -1) {
		switch (ch) {
			case 'l':
				sscanf(optarg, "%250s", (char *)&logpath);
				logname = logpath;
				break;
			case 'p':
				sscanf(optarg, "%u", &ppid);
				ppid_set = true;
				break;
			case 'a':
				sscanf(optarg, "%250s", (char *)&sockpath);
				sockname = sockpath;
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
		printf("dcpmuxtool: use \"-h\" for help\n");
		result = 1;
		goto out;
	}
	if (!sockname) {
		printf("dcpmuxtool: HUA gateway addr:port must be specified\n");
		result = 1;
		goto out;
	}
	
//	if (!logname) logname = (char *)deflog;
	/* cannot trace to stdout - syslog only */
	symtrace_hookctl(true, dummy_vprintf);
	result = run(sockname, (const char *)logname);

out:	return result;
};



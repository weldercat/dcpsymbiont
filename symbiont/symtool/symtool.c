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

#include "console.h"
#include "commands.h"




const char *histfile = "/tmp/.symtool.history";
const char *deflog = "./symtool.log";
static FILE	*log = NULL;
static bool exit_request = false;
bool use_ip = false;
const char *connstring = NULL;


void usage(void);
void print_help(void);
int run(const char *sockname, const char *logfile);
int process_line(char *line);
int dump_line(unsigned char *p, int offset, int len);
void dump_pkt(unsigned char *p, int len, int pktno, bool xmt);
void input_data(int fd);
int parse_line(unsigned char *buf, char *line);





int process_line(char *line)
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


void usage(void)
{
	printf("Yate symbiont testing tool\n");
	printf("Usage: symtool [options]\n");
	printf("where options are:\n");
	printf("        -h - print this text\n");
	printf("	-u <sockpath> - yate extmodule unix socket path\n");
	printf("        -a <ip_addr:port> - yate extmodule server addr/port\n");
	printf("        -l <logfile> - specify session log file.\n");
	printf("                       symtool appends data to the logfile if it exists.\n");
	printf("        -u and -a are mutually exclusive\n");
	printf("\n");
}

void print_help(void)
{
	console_mprintf("\nuse \"help\" to get help on commands\n");
	console_mprintf("Use \" quotes to enclose text containing commas and spaces\n");
	console_mprintf("CTRL-D to exit...\n\n");
	console_mrefresh();
	console_srefresh();
}


int run(const char *sockname, const char *logfile)
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
	symtrace_hookctl(false, console_vmprintf);
	
//	memset(&dstate, 0, sizeof(dstate));	
//	res = dcp_init_hdlc(&dstate, dchan, DCP_LCN_CHAN0);
//	if (res != YXT_OK) {
//		SYMERROR("Cannot initialize dcp engine: %d\n", res);
//		goto	errout;
//	}
	console_sprintf("symtool ");
	console_srefresh();
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
			SYMERROR("symtool: select error %s", strerror(errno));
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
	printf("symtool: user requested exit\n");
	
out:	console_rundown();
	if (log) fclose(log);
	return 0;
errout:
	console_rundown();
	if (log) fclose(log);
	return 3;
}

int main(int argc, char **argv)
{
	int	ch, result = 0;
	char	*logname = NULL;
	char	*sockname = NULL;
	char	logpath[257];
	char	sockpath[257];
	bool	sock_set = false;
	bool	ip_set = false;

	memset(logpath, 0, 257);
	while ((ch = getopt(argc, argv, "hu:a:l:")) != -1) {
		switch (ch) {
			case 'l':
				sscanf(optarg, "%250s", (char *)&logpath);
				logname = logpath;
				break;
			case 'u':
				sscanf(optarg, "%250s", (char *)&sockpath);
				sockname = sockpath;
				sock_set = true;
				break;
			case 'a':
				sscanf(optarg, "%250s", (char *)&sockpath);
				sockname = sockpath;
				ip_set = true;
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
		printf("symtool: use \"-h\" for help\n");
		result = 1;
		goto out;
	}
	if (!sockname) {
		printf("symtool: yate extmodule socket path or addr/port must be specified\n");
		result = 1;
		goto out;
	}
	if (sock_set && ip_set) {
		printf("symtool: -u and -a are mutually exclusive\n");
		result = 1;
		goto out;
	}
	
//	if (!logname) logname = (char *)deflog;
	result = run(sockname, (const char *)logname);

out:	return result;
};



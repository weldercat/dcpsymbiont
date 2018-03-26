/*
 * Copyright 2017,2018  Stacy <stacy@sks.uz>
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
#include <syslog.h>
#include <symbiont/symbiont.h>
#include <symbiont/hua.h>
#include <symbiont/dcpmux.h>
#include <symbiont/cfdb.h>
#include <symbiont/cfload.h>

#include "commands.h"
#include "common.h"



const char *defconffile = "/etc/symbiont/dcpsym.conf";

hua_ctx		*hctx = NULL;
conn_ctx	*yctx = NULL;
dcpmux_t	*dctx = NULL;
cfdb		*confdb = NULL;
struct global_params gparm;
sem_t		rundown_req;


static void usage(void);
static int run(const char *cffile, bool dontfork, bool nosyslog, int debuglevel);
static int dummy_vprintf(const char *format, va_list ap);
static int stderr_vprintf(const char *format, va_list ap);




static void usage(void)
{
	printf("dcp symbiont\n");
	printf("Usage: symbiont [options]\n");
	printf("where options are:\n");
	printf("        -h - print this text\n");
	printf("        -c <filename> - config file name (default is %s)\n", defconffile);
	printf("        -l - log to stderr instead of syslog\n");
	printf("	-f - don't fork\n");
	printf("	-v - increase debug level (can be specified several times)\n");
	printf("\n");
}


static int run(const char *cffile, bool dontfork, bool nosyslog, int debuglevel)
{
	int	res;
	pid_t	pid;

	assert(cffile);
	confdb = new_cfdb();
	assert(confdb);
	
	if (!dontfork) {
		pid = fork();
		if (pid < 0) {
			SYMFATAL("cannot fork: %s\n", STRERROR_R(errno));
			goto out;
		} else if (pid > 0) {
			fprintf(stderr, "forked pid=%d\n", pid);
			goto out;
		} 
	} else fprintf(stderr, "\n");

/* child or dontfork */
	res= cfload(confdb, &gparm, (char *)cffile);
	if (res != SYM_OK) {
		SYMFATAL("cannot load config\n");
		goto out;
	}
	fprintf(stderr, "Configuration loaded from %s\n", cffile);
	fprintf(stderr, "HUA: %s\nYXT: %s\n", gparm.hualink, gparm.yxtlink);
	fprintf(stderr, "starting symbiont \"%s\"...\n", gparm.symname);
	if (!nosyslog) {
		openlog((const char *)gparm.symname, 0, 0);
	}

	res = sem_init(&rundown_req, 0, 0);
	if (res) {
		SYMERROR("cannot init semaphore: %s\n", STRERROR_R(errno));
		goto errout;
	}
	if ((!dontfork) && (!nosyslog)) {
		/* disable stderr printing */
		symtrace_hookctl(true, dummy_vprintf);
	} else {
		symtrace_hookctl(!nosyslog, stderr_vprintf);
	}
	set_tracelevel(gparm.debuglevel + debuglevel);
	SYMINFO("connecting to HUA...\n");
	hctx = hua_connect(gparm.hualink, NULL, NULL, 0);
	if (!hctx) {
		SYMERROR("cannot connect to HUA server %s\n", gparm.hualink);
		goto errout;
	}
	SYMINFO("connecting to YXT...\n");
	if (gparm.yxt_unix) yctx = yxt_conn_unix(gparm.yxtlink, "global");
	else  yctx = yxt_conn_tcp(gparm.yxtlink, "global");
	if (!yctx) {
		SYMERROR("cannot connect to yate at %s\n", gparm.yxtlink);
		goto errout;
	}
	
	dctx = dummy_dcpmux();
	assert(dctx);
	
	SYMINFO("connected to YATE extmodule & HUA link...\n");
	res = ccinit();
	assert(res == SYM_OK);
	
	for (;;) {
		res = sem_wait(&rundown_req);
		if (res) {
			if (errno == EINTR) continue;
			SYMERROR("error waiting for rundown: %s\n", STRERROR_R(errno));
			goto errout;
		} else goto out;
		
	}
out:
	return SYM_OK;
errout:
	return SYM_FAIL;
}



static int dummy_vprintf(const char *format, va_list ap)
{
	return 0;
}

static int stderr_vprintf(const char *format, va_list ap)
{
	return vfprintf(stderr, format, ap);
}



int main(int argc, char **argv)
{
	int	ch, result = 0;
	const char *cfname = NULL;
	char	cfpath[257];
	bool	dontfork = false;
	bool	nosyslog = false;
	int	debuglevel = 0;

	memset(cfpath, 0, 257);
	while ((ch = getopt(argc, argv, "hc:flv")) != -1) {
		switch (ch) {
			case 'f':
				dontfork = true;
				break;
			case 'c':
				sscanf(optarg, "%250s", (char *)&cfpath);
				cfname = cfpath;
				break;
			case 'l':
				nosyslog = true;
				break;
			case 'v':
				++debuglevel;
				break;
			case 'h':
				usage();
				goto out;
			default:
				result = 1;
				goto out;
		}
	}
	if ((argc > optind)) {
		printf("symbiont: use \"-h\" for help\n");
		result = 1;
		goto out;
	}

	if (!cfname) cfname = defconffile;

	result = run((const char *)cfname, dontfork, nosyslog, debuglevel);

out:	return result;
};



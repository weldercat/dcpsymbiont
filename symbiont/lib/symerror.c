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
#include <stdarg.h>
#include <assert.h>
#include <stdbool.h>
#include <syslog.h>

#include <symbiont/symerror.h>

static int tracelevel = TRC_DBG;
static bool initialized = false;
static bool syslog_on = true;
static err_vprintf vprintf_hook = NULL;

static void trace_init(void);

static void trace_init(void)
{
	if (!initialized) {
		initialized = true;
//		openlog("symbiont", 0, LOG_USER);
	}
}

void set_tracelevel(int severity)
{
	if ((severity <= TRC_MAX) && (severity > 0)) tracelevel = severity - 1;
}

int cantrace(int severity)
{
	return (1);
}

void symtrace_hookctl(bool call_syslog, err_vprintf vphook)
{
	syslog_on = call_syslog;
	vprintf_hook = vphook;
}

void symtrace(int prio, const char *format, ...)
{
	va_list ap;

	if (!initialized) trace_init();
	assert(format);
	va_start(ap,format);
	if (vprintf_hook) (vprintf_hook)(format, ap);
	else vprintf(format, ap);
	va_end(ap);
	if (syslog_on) {
		va_start(ap, format);
		vsyslog(LOG_INFO, format, ap);
		va_end(ap);
	}
};

#define DUMP_LINE	16
#define MAX_DUMP_LINE	79
void symhexdump(int prio, unsigned char *b, int len)
{
	int	i, j;
	unsigned char c;
	char	outbuf[MAX_DUMP_LINE + 1];
	int	pos;
	int	res;
	int	space;

	if (len > 0) {
		for (i = 0; i < len; i += DUMP_LINE) {
			memset(outbuf, 0, MAX_DUMP_LINE + 1);
			space = MAX_DUMP_LINE;
			pos = 0;
			res = snprintf(&outbuf[pos], space, "%04x:   ", i);
			if (res > space) res = space;
			pos += res;
			space -= res;
			for (j = 0; ((j < DUMP_LINE) && (i+j < len)); j++) {
				res = snprintf(&outbuf[pos], space, "%02x ", b[i+j]);
				if (res > space) res = space;
				pos += res;
				space -= res;
				if ((j == DUMP_LINE / 2 - 1) && (space > 0)) {
					outbuf[pos] = ' ';
					pos++;
					space--;
				}
			}
			strncpy(&outbuf[pos], "  |", space);
			space -= 3;
			for (j = 0; ((j < DUMP_LINE) && (i+j < len) && (space > 0)); j++) {
				c = b[i+j];
				if (c < 0x20) c = '.';
				else if (c > 0x7e) c = '.';
				outbuf[pos] = c;
				pos++;
				space--;
			}
			if (space > 0) strncpy(&outbuf[pos],"|\n", space);
			symtrace(prio, "%s", outbuf);
			
		}
	} else {
		symtrace(prio, "Nothing to print, len=%d\n", len);
	}
	symtrace(prio, "\n");
}

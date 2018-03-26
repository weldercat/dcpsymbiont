#include <stdio.h>
#include <stdarg.h>
#include <assert.h>
#include <stdbool.h>
#include <syslog.h>

#include "trace.h"
#include "console.h"

static int tracelevel = TRC_DBG;
static bool initialized = false;

static void trace_init(void);


void trace_init(void)
{
	initialized = true;
	openlog("dcptool", 0, LOG_USER);
}

void dcp_set_tracelevel(int severity)
{
	if ((severity <= TRC_MAX) && (severity > 0)) tracelevel = severity - 1;
}

int dcp_cantrace(int severity)
{
	return (severity > tracelevel);
}

void dcptrace(int prio, const char *format, ...)
{
	va_list ap;
	
	if (!initialized) trace_init();
//	if (prio > tracelevel) {
//		assert(format);
//		va_start(ap,format);
//		console_vmprintf(format, ap);
//		va_end(ap);
//		console_mrefresh();
//	}
	va_start(ap, format);
	vsyslog(LOG_INFO, format, ap);
	va_end(ap);
};


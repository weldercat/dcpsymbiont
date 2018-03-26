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

#ifndef SYMERROR_HDR_LOADED__
#define SYMERROR_HDR_LOADED__
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdbool.h>
#ifndef _GNU_SOURCE
#error _GNU_SOURCE must be defined - symerror depends on GNU implementation of strerror_r()
#endif

#define ERRTXT_MAX	512


#define	SYM_OK	0
#define SYM_FAIL	(-1)

void symtrace(int severety, const char *format, ...);
void set_tracelevel(int severity);
int cantrace(int severity);
typedef int (*err_vprintf)(const char *format, va_list ap);
void symtrace_hookctl(bool call_syslog, err_vprintf vphook);
void symhexdump(int prio, unsigned char *b, int len);

#define SYMTRACE(sev, fmt, ...) {	\
	char	*eb_;			\
	eb_ = calloc(ERRTXT_MAX, 1);	\
	assert(eb_);			\
	symtrace(sev, ("%s:%d: %s(): " fmt), 	\
		__FILE__, __LINE__,  __func__, ##__VA_ARGS__);	\
	free(eb_);			\
}
#ifndef SYMLOGNAME
#define SYMLOGNAME "symbiont"
#endif

#define SYMLOG(sev, fmt, ...) {	\
	char	*eb_;			\
	eb_ = calloc(ERRTXT_MAX, 1);	\
	assert(eb_);			\
	symtrace(sev, ("%s: " fmt), 	\
		SYMLOGNAME, ##__VA_ARGS__);	\
	free(eb_);			\
}

#define	SE_PREPARE	char	*eb_ = alloca(ERRTXT_MAX)

#define STRERROR_R(e)	(strerror_r(e, eb_, (ERRTXT_MAX-1)))

#define SYMFATAL(...)		{		\
	SYMTRACE(TRC_FAIL, ##__VA_ARGS__)	\
	abort();				\
}

#define	eassert(a)	{		\
	if (!(a))	SYMFATAL("%s\n", STRERROR_R(errno));	\
}

#define SYMERROR(...)		SYMTRACE(TRC_ERR, ##__VA_ARGS__)
#define SYMWARNING(...)		SYMTRACE(TRC_WARN, ##__VA_ARGS__)

#ifndef NODEBUG
#define SYMDEBUG(...)		SYMTRACE(TRC_DBG, ##__VA_ARGS__)
#else 
#undef DEBUG_ME_HARDER
#define SYMDEBUG(...)	
#endif

#define SYMINFO(...)		SYMTRACE(TRC_INFO, ##__VA_ARGS__)
#define SYMPRINTF(...)		SYMLOG(TRC_INFO, ##__VA_ARGS__)
#ifdef DEBUG_ME_HARDER
#define SYMDEBUGHARD(...)	SYMTRACE(TRC_DBG2, ##__VA_ARGS__)
#else
#define SYMDEBUGHARD(...)	
#endif

#define	HEXDUMP(a, l)	{	\
	symhexdump(TRC_DBG, a, l);	\
}	


#define TRC_DBG2	1
#define TRC_DBG		1
#define TRC_INFO	2
#define TRC_WARN	3
#define TRC_ERR		4
#define TRC_FAIL	5

#define TRC_ALL	0
#define TRC_MAX		TRC_FAIL


#endif /* SYMERROR_HDR_LOADED__ */

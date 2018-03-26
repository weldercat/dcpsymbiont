#ifndef TRACE_HDR_LOADED_
#define TRACE_HDR_LOADED
/*
 * Copyright 2018 Stacy <stacy@sks.uz> 
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



#define dcptrace	symtrace
/* void dcptrace(int severety, const char *format, ...); */

#define dcp_set_tracelevel set_tracelevel

/* void dcp_set_tracelevel(int severity); */

#define dcp_cantrace	cantrace
/* int dcp_cantrace(int severity); */

#if 0

#define TRC_DBG		1
#define TRC_INFO	2
#define TRC_WARN	3
#define TRC_ERR		4
#define TRC_FAIL	5

#define TRC_ALL	0
#define TRC_MAX		TRC_FAIL
#endif


#endif /*TRACE_HDR_LOADED_*/
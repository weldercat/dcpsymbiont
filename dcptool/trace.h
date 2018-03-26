#ifndef TRACE_HDR_LOADED_
#define TRACE_HDR_LOADED
void dcptrace(int severety, const char *format, ...);
void dcp_set_tracelevel(int severity);
int dcp_cantrace(int severity);


#define TRC_DBG		1
#define TRC_INFO	2
#define TRC_WARN	3
#define TRC_ERR		4
#define TRC_FAIL	5

#define TRC_ALL	0
#define TRC_MAX		TRC_FAIL



#endif /*TRACE_HDR_LOADED_*/
#ifndef DCP_CMDTYPE_HDR_LOADED_
#define DCP_CMDTYPE_HDR_LOADED_

#include <symbiont/mmi.h>


const struct dcp_cmdtype_s *
lookup_cmdtype(register const char *str, register unsigned int len);
int classify_cmdtype(const char *cmdtype);
#endif /* DCP_CMDTYPE_HDR_LOADED_ */


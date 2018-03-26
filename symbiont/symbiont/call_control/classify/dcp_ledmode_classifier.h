#ifndef DCP_LEDMODE_HDR_LOADED_
#define DCP_LEDMODE_HDR_LOADED_

#include <symbiont/mmi.h>


const struct dcp_ledmode_s *
lookup_ledmode(register const char *str, register unsigned int len);
int classify_ledmode(const char *ledmode);
#endif /* DCP_LEDMODE_HDR_LOADED_ */


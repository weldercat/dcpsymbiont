#ifndef DCP_ERASE_HDR_LOADED_
#define DCP_ERASE_HDR_LOADED_

#include <symbiont/mmi.h>


const struct dcp_erase_s *
lookup_erase(register const char *str, register unsigned int len);
int classify_erase(const char *erase);
#endif /* DCP_ERASE_HDR_LOADED_ */


#ifndef DCP_COLOR_HDR_LOADED_
#define DCP_COLOR_HDR_LOADED_

#include <symbiont/mmi.h>


const struct dcp_color_s *
lookup_color(register const char *str, register unsigned int len);
int classify_color(const char *color);
#endif /* DCP_COLOR_HDR_LOADED_ */


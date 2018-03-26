#ifndef MMI_PRINT_HDR_LOADED_
#define MMI_PRINT_HDR_LOADED_
#include <symbiont/mmi.h>

void print_mmicmd(struct mmi_command *cmd);
void print_mmievt(struct mmi_event *evt);
const char *mmi_evt2txt(int evttype);
const char *mmi_cmd2txt(int cmdtype);
const char *mmi_color2txt(int color);
const char *mmi_mode2txt(int mode);
const char *mmi_erase2txt(int erase);

#endif /* MMI_PRINT_HDR_LOADED_ */

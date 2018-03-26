#ifndef DCP_RESP_STATUS_HDR_LOADED_
#define DCP_RESP_STATUS_HDR_LOADED_

enum dcp_status {
	DCP_ST_UNKNOWN = 0,
	DCP_ST_HWID = 1,
	DCP_ST_OFFHOOK,
	DCP_ST_ONHOOK,
	DCP_ST_KEY,
	DCP_ST_PAD_PRESS,
	DCP_ST_PAD_RELEASE,
	DCP_ST_MENU_EXIT,
	DCP_ST_MENU_ITEM,
	DCP_ST_PRG_RESULT
};

/*DCP command prefix is always 2 bytes with arguments following */
#define DCP_CMD_PREFIX_LEN	2
const struct dcp_status_s *
decode_dcp_resp(register const char *str, register unsigned int len);

#endif /* DCP_RESP_STATUS_HDR_LOADED_ */


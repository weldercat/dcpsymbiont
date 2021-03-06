#ifndef MSG_TYPES_HDR_LOADED_
#define MSG_TYPES_HDR_LOADED_

enum yate_msg_class {
	YMSG_UNKNOWN = 0,
	YMSG_ENGINE_STATUS,
	YMSG_ENGINE_TIMER,
	YMSG_ENGINE_DEBUG,
	YMSG_ENGINE_COMMAND,
	YMSG_ENGINE_HELP,
	YMSG_ENGINE_HALT,
	YMSG_ENGINE_STOP,
	YMSG_CALL_PREROUTE,
	YMSG_CALL_ROUTE,
	YMSG_CALL_EXECUTE,
	YMSG_CALL_DROP,
	YMSG_CALL_PROGRESS,
	YMSG_CALL_RINGING,
	YMSG_CALL_ANSWERED,
	YMSG_CALL_CDR,
	YMSG_CALL_UPDATE,
	YMSG_CHAN_DTMF,
	YMSG_CHAN_MASQUERADE,
	YMSG_CHAN_LOCATE,
	YMSG_CHAN_TRANSFER,
	YMSG_CHAN_CONTROL,
	YMSG_CHAN_ATTACH,
	YMSG_CHAN_CONNECTED,
	YMSG_CHAN_DISCONNECTED,
	YMSG_CHAN_HANGUP,
	YMSG_CHAN_OPERATION,
	YMSG_CHAN_REPLACED,
	YMSG_CHAN_RTP,
	YMSG_CHAN_STARTUP,
	YMSG_CHAN_TEXT,
	YMSG_MODULE_UPDATE,
	YMSG_MSG_EXECUTE,
	YMSG_USER_AUTH,
	YMSG_USER_REGISTER,
	YMSG_IMT_OPERATION,
	YMSG_DCP_EVENT,
	YMSG_DCP_COMMAND,
	YMSG_SYMBIONT_COMMAND
};

const struct msg_class_s *
lookup_msg_class(register const char *str, register unsigned int len);
int classify_msg(const char *msgname);
#endif /* MSG_TYPES_HDR_LOADED_ */


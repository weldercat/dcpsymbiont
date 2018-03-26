#ifndef SIGTRAN_HDR_LOADED_
#define SIGTRAN_HDR_LOADED_
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

#define _GNU_SOURCE	1

#include <stdbool.h>
#include <stdint.h>


#define XRN_DEF_PKTSIZE	512

/* packet size increment */
#define XRN_PKT_REALLOC	512
#define XRN_MIN_MSGLEN	8
#define XRN_MIN_TLV_SIZE 4
#define XRN_TLV_TAG_SIZE	2
#define XRN_TLV_LENGTH_SIZE	2

#define XRN_OK	0
#define XRN_FAIL	(-1)



typedef struct pktbuf_s {
	ssize_t	memlen;
	ssize_t	datalen;
	uint8_t	*data;
} xrn_pktbuf;

typedef struct xrn_tlv_s {
	int		tag;
	int		vlen;	/* data length - _NOT_ the total TLV length like when it's stored in packet */
	uint8_t		*value;	/* pointer to data */
} xrn_tlv;

typedef struct sigtranmsg_s {
	uint8_t		version;
	uint8_t		reserved;
	uint8_t		mclass;
	uint8_t		type;
	uint32_t	length;
	uint8_t		data;
} sigtranmsg;

xrn_pktbuf *xrn_alloc_pkt(int deflen);
void xrn_free_pkt(xrn_pktbuf *p);

/* copies data to the packet*/
xrn_pktbuf *xrn_copy_data(uint8_t *data, int datalen);

/* extracting and storing the data in network byte order 
 * (data operand and result is in host byte order)
 */
uint16_t xrn_get_net16(uint8_t *ptr);
uint32_t xrn_get_net32(uint8_t *ptr);
uint64_t xrn_get_net64(uint8_t *ptr);

void xrn_store_net16(uint8_t *ptr, uint16_t data);
void xrn_store_net32(uint8_t *ptr, uint32_t data);
void xrn_store_net64(uint8_t *ptr, uint64_t data);


uint8_t	*xrn_dataptr(xrn_pktbuf *p);
size_t	xrn_datalen(xrn_pktbuf *p);
void xrn_init_msg(xrn_pktbuf *p);

void xrn_set_mclass(xrn_pktbuf *p, uint8_t mclass);
void xrn_set_mtype(xrn_pktbuf *p, uint8_t mtype);
uint8_t xrn_get_mclass(xrn_pktbuf *p);
uint8_t xrn_get_mtype(xrn_pktbuf *p);

/* this routine reallocs packet buffer if there's not enough space */
void xrn_append_tlv(xrn_pktbuf *p, uint16_t tag, void *data, uint16_t length);

/* 
 *
 * these functions store found TLV pointers in tlv and also 
 * return a pointer to the TLV's location within the packet.
 */
/* find the first tlv with type==tag starting from start
 * and store its tag. length and value ptr into *tlv
 */
 
uint8_t *xrn_find_tlv(xrn_pktbuf *p, xrn_tlv *tlv, uint16_t tag, uint8_t *start);
uint8_t *xrn_first_tlv(xrn_pktbuf *p, xrn_tlv *tlv);
uint8_t *xrn_next_tlv(xrn_pktbuf *p, xrn_tlv *tlv, uint8_t *start);
bool xrn_valid_packet(xrn_pktbuf *p);


/* most message classes defs & msg types are taken from yate sigtran.h */
 
#define XRN_DEFAULT_VERSION	1
#define XRN_MSG_VERSION_OFFSET	0
#define XRN_MSG_RESERVED_OFFSET	1
#define XRN_MSG_CLASS_OFFSET	2
#define XRN_MSG_TYPE_OFFSET	3
#define XRN_MSG_LENGTH_OFFSET	4
#define XRN_FIRST_TLV_OFFSET	8


enum xrn_msg_class {
	MGMT = 0, 	/* Management (IUA/M2UA/M3UA/SUA) */
	TRAN = 1,	/* Transfer (M3UA) */
	SSNM = 2,	/* SS7 Signalling Network Management (M3UA/SUA) */
	ASPSM = 3,	/* ASP State Maintenance (IUA/M2UA/M3UA/SUA) */
	ASPTM = 4,	/* ASP Traffic Maintenance (IUA/M2UA/M3UA/SUA) */
	QPTM = 5,	/* Q.921/Q.931 Boundary Primitives Transport (IUA) */
	MAUP = 6,	/* MTP2 User Adaptation (M2UA) */
	CLMSG = 7,	/* Connectionless Messages (SUA) */
	COMSG = 8,	/* Connection-Oriented Messages (SUA) */
	RKM = 9,	/* Routing Key Management (M3UA/SUA) */
	IIM = 10,	/* Interface Identifier Management (M2UA) */
	M2PA = 11	/* M2PA Messages (M2PA) */
};

enum xrn_mgmt_msg {
	MGMT_ERR = 0,
	MGMT_NTFY = 1,
	MGMT_TEI_STATUS_REQ = 2,
	MGMT_TEI_STATUS_CFM =3,
	MGMT_TEI_STATUS_IND = 4,
	MGMT_TEI_QUERY_REQ = 5,
};

enum xrn_ssnm_msg {
	SSNM_DUNA = 1, /* Destination Unavailable */
	SSNM_DAVA = 2, /* Destination Available */
	SSNM_DAUD = 3, /* Destination State Audit */
	SSNM_SCON = 4, /* Signalling Congestion */
	SSNM_DUPU = 5, /* Destination User Part Unavailable */
	SSNM_DRST = 6, /* Destination Restricted */
};


    /**
     * ASP State Maintenance messages
     */
enum xrn_aspsm_msg {
	ASPSM_UP	= 1,	/* ASP Up */
	ASPSM_DOWN	= 2,	/* ASP Down */
	ASPSM_BEAT	= 3,	/* Heartbeat */
	ASPSM_ACK	= 4,	/* ASP up ACK */
	ASPSM_DOWN_ACK	= 5,	/* ASP down ACK */
	ASPSM_BEAT_ACK	= 6,	/* ASP BEAT ACK */
};


    /**
     * ASP Traffic Maintenance messages
     */
enum xrn_asptm_msg {
	ASPTM_ACTIVE		= 1,
	ASPTM_INACTIVE		= 2,
	ASPTM_ACTIVE_ACK	= 3,
	ASPTIM_INACTIVE_ACK	= 4,
};

    /**
     * Routing Key Management messages
     */
enum xrn_rkm_msg {
	RKM_REG_REQ	= 1,
	RKM_REG_RESP	= 2,
	RKM_DEREG_REQ	= 3,
	RKM_DEREG_RESP	= 4,
};

    /**
     * Interface Identifier Management messages
     */
enum xrn_iim_msg {
	IIM_REG_REQ	= 1,
	IIM_REG_RESP	= 2,
	IIM_DEREG_REQ	= 3,
	IIM_DEREG_RESP	= 4,
};

enum xrn_qptm_msg {
	QPTM_DATA_REQ		= 1,
	QPTM_DATA_IND		= 2,
	QPTM_UNIT_DATA_REQ	= 3,
	QPTM_UNIT_DATA_IND	= 4,
	QPTM_ESTABLISH_REQ	= 5,
	QPTM_ESTABLISH_CFM	= 6,
	QPTM_ESTABLISH_IND	= 7,
	QPTM_RELEASE_REQ	= 8,
	QPTM_RELEASE_CFM	= 9,
	QPTM_RELEASE_IND	= 10,
};

/* IUA only */
enum xrn_tag {
	TAG_INTERFACE_ID_NUM	= 0x0001,
	TAG_INTERFACE_ID_TXT	= 0x0003,
	TAG_INFO_STRING		= 0x0004,
	TAG_DLCI		= 0x0005,
	TAG_DIAG_INFO		= 0x0007,
	TAG_INTERFACE_ID_RANGE	= 0x0008,
	TAG_HEARTBEAT_DATA	= 0x0009,
	TAG_TRAFF_MODE_TYPE	= 0x000b,
	TAG_ERROR_CODE		= 0x000c,
	TAG_STATUS		= 0x000d,
	TAG_PROTOCOL_DATA	= 0x000e,
	TAG_RELEASE_REASON	= 0x000f,
	TAG_TEI_STATUS		= 0x0010,
	TAG_ASP_IDENTIFIER	= 0x0011,
};

enum xrn_traffic_mode {
	TRAFFIC_OVERRIDE = 1,
	TRAFFIC_LOAD_SHARE = 2,
	TRAFFIC_BROADCAST = 3,
};

enum xrn_error_code {
	ERR_INVALID_VERSION	= 0x01,
	ERR_INVALID_IFI		= 0x02,
	ERR_UNSUPPORTED_CLASS	= 0x03,
	ERR_UNSUPPORTED_MESSAGE = 0x04,
	ERR_UNSUPPORTED_TRAFF_MODE = 0x05,
	ERR_UNEXPECTED_MESSAGE	= 0x06,
	ERR_PROTOCOL_ERROR	= 0x07,
	ERR_UNSUPPORTED_IFI_TYPE= 0x08,
	ERR_INVALID_STREAM_ID	= 0x09,
	ERR_UNASSIGNED_TEI	= 0x0a,
	ERR_UNRECOGNIZED_SAPI	= 0x0b,
	ERR_INVALID_TEI_SAPI_COMB = 0x0c,
	ERR_REFUSED		= 0x0d,
	ERR_ASP_ID_REQUIRED	= 0x0e,
	ERR_INVALID_ASP_ID	= 0x0f,
};

enum xrn_status_type {
	STYPE_AS_STATE_CHANGE	= 1,
	STYPE_OTHER		= 2,
};

enum xrn_status_info_as {
	SINFO_AS_INACTIVE	= 2,
	SINFO_AS_ACTIVE		= 3,
	SINFO_AS_PENDING	= 4,
};

enum xrn_status_info_other {
	SINFO_INS_RESOURCE	= 1,
	SINFO_ALTERNATE_ASP_ACTIVE = 2,
	SINFO_ASP_FAILURE	=3,
};



#endif /* SIGTRAN_HDR_LOADED_ */

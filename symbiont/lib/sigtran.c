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

//#define DEBUG_ME_HARDER	1

#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdint.h>
#include <symbiont/sigtran.h>
#include <symbiont/symerror.h>


static void store_tlv_ptrs(xrn_tlv *tlv, uint8_t *ptr, long long int restlen);

xrn_pktbuf *xrn_alloc_pkt(int deflen)
{
	xrn_pktbuf *p = NULL;
	
	p = malloc(sizeof(xrn_pktbuf));
	if (!p) goto errout;
	memset(p, 0, sizeof(xrn_pktbuf));
	if (deflen > 0) {
		p->data = malloc(deflen);
		if (!p->data) {
			free(p);
			p = NULL;
			goto errout;
		} else {
			memset(p->data, 0, deflen);
			p->memlen = deflen;
		}
	}
	goto out;
	
errout:	SYMERROR("cannot allocate memory\n");
out:	return p;
}

void xrn_free_pkt(xrn_pktbuf *p)
{
	if (!p) return;
	if (p->data) free(p->data);
	free(p);
}

xrn_pktbuf *xrn_copy_data(uint8_t *data, int datalen)
{
	xrn_pktbuf *p;
	
	assert(data);
	assert(datalen > XRN_MIN_MSGLEN);
	
	p = xrn_alloc_pkt(datalen);
	memcpy(p->data, data, datalen);
	p->datalen = datalen;
	return p;
}

uint8_t	*xrn_dataptr(xrn_pktbuf *p)
{
	assert(p);
	return p->data;
}

size_t	xrn_datalen(xrn_pktbuf *p)
{
	assert(p);
	return p->datalen;
}

uint16_t xrn_get_net16(uint8_t *ptr)
{
	uint16_t res;
	
	res = ptr[0] << 8;
	res |= ptr[1];
	return res;
}

uint32_t xrn_get_net32(uint8_t *ptr)
{
	uint32_t res;
	
	res = ptr[0] << 24;
	res |= ptr[1] << 16;
	res |= ptr[2] << 8;
	res |= ptr[3];
	return res;
}

uint64_t xrn_get_net64(uint8_t *ptr)
{
	uint64_t res;
	
	res = (uint64_t)(ptr[0]) << 56;
	res |= (uint64_t)(ptr[1]) << 48;
	res |= (uint64_t)(ptr[2]) << 40;
	res |= (uint64_t)(ptr[3]) << 32;
	res |= (uint64_t)(ptr[4]) << 24;
	res |= (uint64_t)(ptr[5]) << 16;
	res |= (uint64_t)(ptr[6]) << 8;
	res |= (uint64_t)(ptr[7]);
	return res;
}

void xrn_store_net16(uint8_t *ptr, uint16_t data)
{
	ptr[0] = (data & 0xff00) >> 8;
	ptr[1] = (data & 0x00ff);
}

void xrn_store_net32(uint8_t *ptr, uint32_t data)
{
	ptr[0] = (data & 0xff000000) >> 24;
	ptr[1] = (data & 0x00ff0000) >> 16;
	ptr[2] = (data & 0x0000ff00) >> 8;
	ptr[3] = (data & 0x000000ff);
}

void xrn_store_net64(uint8_t *ptr, uint64_t data)
{
	ptr[0] = (data & 0xff00000000000000LL) >> 56;
	ptr[1] = (data & 0x00ff000000000000LL) >> 48;
	ptr[2] = (data & 0x0000ff0000000000LL) >> 40;
	ptr[3] = (data & 0x000000ff00000000LL) >> 32;
	ptr[4] = (data & 0x00000000ff000000LL) >> 24;
	ptr[5] = (data & 0x0000000000ff0000LL) >> 16;
	ptr[6] = (data & 0x000000000000ff00LL) >> 8;
	ptr[7] = (data & 0x00000000000000ffLL);
}


void xrn_init_msg(xrn_pktbuf *p)
{	
	assert(p);
	assert(p->data);
	assert(p->memlen > XRN_MIN_MSGLEN);
	assert(p->datalen == 0);
	
	p->datalen = XRN_MIN_MSGLEN;
	p->data[XRN_MSG_VERSION_OFFSET] = XRN_DEFAULT_VERSION;
	p->data[XRN_MSG_RESERVED_OFFSET] = 0;
	xrn_store_net32(p->data + XRN_MSG_LENGTH_OFFSET, p->datalen);
}

void xrn_set_mclass(xrn_pktbuf *p, uint8_t mclass)
{
	assert(xrn_valid_packet(p));
	p->data[XRN_MSG_CLASS_OFFSET] = mclass;
}

void xrn_set_mtype(xrn_pktbuf *p, uint8_t mtype)
{
	assert(xrn_valid_packet(p));
	p->data[XRN_MSG_TYPE_OFFSET] = mtype;
}


uint8_t xrn_get_mclass(xrn_pktbuf *p)
{
	assert(xrn_valid_packet(p));
	return p->data[XRN_MSG_CLASS_OFFSET];
}


uint8_t xrn_get_mtype(xrn_pktbuf *p)
{
	assert(xrn_valid_packet(p));
	return p->data[XRN_MSG_TYPE_OFFSET];
}


bool xrn_valid_packet(xrn_pktbuf *p)
{
	uint32_t msglen;
	
	if (!p) {
		SYMWARNING("null packet pointer\n");
		return false;
	}
	if (!p->data) {
		SYMWARNING("no packet data\n");
		return false;
	}
	if (p->memlen < XRN_MIN_MSGLEN) {
		SYMWARNING("packet buffer too short\n");
		return false;
	}
	if (p->datalen < XRN_MIN_MSGLEN) {
		SYMWARNING("packet too short\n");
		return false;
	}
	if (p->datalen > p->memlen) {
		SYMWARNING("malformed packet - data len > buffer len\n");
		return false;
	}
	msglen = xrn_get_net32(&p->data[XRN_MSG_LENGTH_OFFSET]);
	if (msglen != p->datalen) {
		SYMWARNING("malformed packet - msg len %d != data len %d\n",
			msglen, p->datalen);
			return false;
	}
	return true;
}

static void store_tlv_ptrs(xrn_tlv *tlv, uint8_t *ptr, long long int restlen)
{
	assert(tlv);
	assert(ptr);
	tlv->tag = xrn_get_net16(ptr);
	SYMDEBUGHARD("tag = %d at %p\n", tlv->tag, ptr);
	ptr += XRN_TLV_TAG_SIZE;
	tlv->vlen = xrn_get_net16(ptr);
	SYMDEBUGHARD("raw vlen = %d at %p\n", tlv->vlen, ptr);
	ptr += XRN_TLV_LENGTH_SIZE;
	if (restlen < tlv->vlen) {
		SYMWARNING("malformed tlv %d - vlen [%d] > restlen [%d]\n",
				tlv->tag, tlv->vlen, restlen);
		tlv->vlen = restlen;
	}
	tlv->vlen -= (XRN_TLV_TAG_SIZE + XRN_TLV_LENGTH_SIZE);
	tlv->value = ptr;
	SYMDEBUGHARD("data vlen=%d, value points to %p\n", tlv->vlen, ptr);
	if (tlv->vlen < 0) {
		SYMWARNING("malformed tlv %d - length=%d is too short\n", tlv->tag, tlv->vlen);
	}
}


uint8_t *xrn_first_tlv(xrn_pktbuf *p, xrn_tlv *tlv)
{
	long long int msglen;
	uint8_t	*tptr;

	assert(tlv);
	assert(xrn_valid_packet(p));
	msglen = xrn_get_net32(&p->data[XRN_MSG_LENGTH_OFFSET]);
	msglen -= XRN_MIN_MSGLEN;
	if (msglen < XRN_MIN_TLV_SIZE) {
		tlv->tag = -1;
		tlv->vlen = 0;
		tlv->value = NULL;
		return NULL;
	};

	tptr = p->data + XRN_FIRST_TLV_OFFSET;
	store_tlv_ptrs(tlv, tptr, msglen);
	return tptr;
}

uint8_t *xrn_next_tlv(xrn_pktbuf *p, xrn_tlv *tlv, uint8_t *start)
{
	long long int msglen;
	uint8_t	*tptr;
	int	skiplen, pad;

	assert(tlv);
	assert(xrn_valid_packet(p));

	msglen = xrn_get_net32(&p->data[XRN_MSG_LENGTH_OFFSET]);
	if (msglen < (XRN_MIN_MSGLEN + XRN_MIN_TLV_SIZE)) {
		SYMDEBUG("message too short - no TLVs\n");
		goto errout;
	}
	assert(start >= (p->data));
	if (!start) goto errout;
	if (start > ((p->data + msglen) - XRN_MIN_TLV_SIZE)) goto errout;
	
	msglen -= XRN_MIN_MSGLEN;
	tptr = start;
	skiplen = xrn_get_net16(tptr + XRN_TLV_TAG_SIZE);
	if (skiplen < XRN_MIN_TLV_SIZE) goto errout;
	pad = (4 - (skiplen & 0x03)) & 0x03;
	skiplen += pad;
	if (skiplen > msglen) goto errout;
	tptr += skiplen;
	msglen -= skiplen;
	store_tlv_ptrs(tlv, tptr, msglen);
	return tptr;

errout:
	tlv->tag = -1;
	tlv->vlen = 0;
	tlv->value = NULL;
	return NULL;
}


void xrn_append_tlv(xrn_pktbuf *p, uint16_t tag, void *data, uint16_t length)
{
	int	new_msglen, old_msglen, pad;
	uint8_t	*new_data, *tptr;
	int	i;
	
	assert(xrn_valid_packet(p));
	old_msglen = xrn_get_net32(&p->data[XRN_MSG_LENGTH_OFFSET]);
	assert(p->datalen == old_msglen);
	pad = (4 - (length & 0x03)) & 0x03;
	new_msglen = old_msglen + length + pad + XRN_TLV_TAG_SIZE + XRN_TLV_LENGTH_SIZE;
	if (new_msglen > p->memlen) {
		new_data = realloc(p->data, new_msglen);
		if (!new_data) SYMFATAL("cannot allocate memory\n");
		p->data = new_data;
		p->memlen = new_msglen;
	}
	tptr = p->data + old_msglen;
	SYMDEBUGHARD("tag=%hd, data=%p, length=%hd, old_msglen=%d, new_msglen=%d, pad=%d, tptr=%p\n",
			tag, data, length, old_msglen, new_msglen, pad, tptr);
	xrn_store_net16(tptr, tag);
	tptr += XRN_TLV_TAG_SIZE;
	xrn_store_net16(tptr, length + XRN_TLV_TAG_SIZE + XRN_TLV_LENGTH_SIZE);
	tptr += XRN_TLV_LENGTH_SIZE;
	memcpy(tptr, data, length);
	tptr += length;
	for (i = 0; i < pad; i++) *tptr++ = 0;
	p->datalen = new_msglen;
	xrn_store_net32(p->data + XRN_MSG_LENGTH_OFFSET, new_msglen);
}

uint8_t *xrn_find_tlv(xrn_pktbuf *p, xrn_tlv *tlv, uint16_t tag, uint8_t *start)
{
	uint8_t	*tptr;
	xrn_tlv	curtlv;
	
	assert(xrn_valid_packet(p));
	assert(tlv);
	memset(&curtlv, 0, sizeof(xrn_tlv));
	if (!start) tptr = xrn_first_tlv(p, &curtlv);
	else tptr = start;
	while (tptr) {
		if (curtlv.tag == tag) break;
		tptr = xrn_next_tlv(p, &curtlv, tptr);
	}
	*tlv = curtlv;
	return tptr;
}


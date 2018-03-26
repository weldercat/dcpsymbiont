/**
 * rslmux.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * remote signalling link multiplexer
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2010-2014 Null Team
 *
 * This software is distributed under multiple licenses;
 * see the COPYING file in the main directory for licensing
 * information for this specific distribution.
 *
 * This use of this software may be subject to additional restrictions.
 * See the LEGAL file in the main directory for details.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 */

#include <yatephone.h>
#include <yatesig.h>

#define DEBUG_ME_HARDER	1

using namespace TelEngine;
namespace { // anonymous

class RslReceiver;

//this class should bridge the data between HUA sctp transport 
// and signalling interfaces

class HUALink : public SIGTRAN
{
public:
	virtual SignallingInterface *attach_iface(SignallingInterface *intface);
	virtual SignallingInterface *detach_iface(SignallingInterface *intface);
	virtual SignallingInterface *detach_iface(unsigned int ifi);
	
	virtual SignallingInterface *iface(unsigned int ifi);
	virtual RslReceiver *receiver(unsigned int ifi);

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

	virtual bool sendHDLC(const DataBlock &msg, unsigned int ifi);

protected:
	virtual bool processMSG(unsigned char msgVersion, unsigned char msgClass,
			unsigned char msgType, const DataBlock& msg, int streamId);
private:
	HashList m_receivers;
	mutable Mutex m_hualinkMutex;
	int ifi2sid(unsigned int ifi);
};

class RslReceiver : public SignallingReceiver
{
public:
	inline RslReceiver(const char *name) : SignallingReceiver(name)
	{	m_hualink = NULL; 
		m_ifi = 0;
		m_ifi_set = false;
	}

	bool sendPkt(const DataBlock& packet);

	void attachHua(HUALink *hua_link);

	inline HUALink *hualink(void) const
		{ return m_hualink; }

	void setIFI(unsigned int ifi);
	
	unsigned int getIFI(void) const;
	
	virtual const String& toString(void) const;

protected:
	virtual bool receivedPacket(const DataBlock &packet);

private:
	HUALink *m_hualink;
	unsigned int m_ifi;
	String m_ifi_ident;
	bool m_ifi_set;
	
};


class RslMux : public Plugin
{
public:
    inline RslMux()
	: Plugin("rslmux")
	{ Output("Loaded module RSL multiplexer"); }
    inline ~RslMux()
	{ Output("Unloading module RSL multiplexer"); }
    virtual void initialize();
};

static void configure_multiplexer(NamedList *params);

static ObjList s_multiplexers;


/* implementation */

/* RSL receiver */

void RslReceiver::setIFI(unsigned int ifi)
{
	m_ifi = ifi;
	m_ifi_ident = (String)ifi;
	m_ifi_set = true;
}

unsigned int RslReceiver::getIFI(void) const
{
	if (m_ifi_set) return m_ifi;
	else return 0;
}

const String& RslReceiver::toString(void) const
{
	if (m_ifi_set) return m_ifi_ident;
	else return SignallingReceiver::toString();
}


bool RslReceiver::sendPkt(const DataBlock& packet)
{
	return SignallingReceiver::transmitPacket(packet, false);
}

void RslReceiver::attachHua(HUALink *hua_link)
{
	m_hualink = hua_link;
}


bool RslReceiver::receivedPacket(const DataBlock &packet)
{
//	int	i, l;
	
/* send received packet to the attached HUALink using m_hualink->transmitMSG() */
//	l = packet.length();
//	Debug(this, DebugNote, "Received packet with length %u [%p]", l, this);
//	if (l > 0) {
//		for (i = 0; i < l; i++) {
//			Output("Packet data at %0d : 0x%0x", i, packet.at(i));
//		}
//	}
	/* format the packet:
	 * 1. Insert if as INTERFACE_ID_NUMERIC
	 * 2. Insert HDLC data as PROTOCOL_DATA 
	 */ 
	/* type 3 is the QPTM_UNIT_DATA_REQ */
	if (!m_hualink) return false;
	if (!m_ifi_set) return false;
	
	return (m_hualink->sendHDLC(packet, m_ifi));

}

SignallingInterface *HUALink::attach_iface(SignallingInterface *intface)
{
	RslReceiver *rcv;
	
	if (!intface) return NULL;
	rcv = dynamic_cast<RslReceiver *>(intface->receiver());

	if (rcv) { 
		m_hualinkMutex.lock();
		m_receivers.append(rcv);
		rcv->attachHua(this);
		m_hualinkMutex.unlock();
	} else {
		Debug(DebugNote, "interface %p has no attached receiver", intface);
		return NULL;
	}
	return intface;
}

SignallingInterface *HUALink::detach_iface(SignallingInterface *intface)
{
	RslReceiver *rcv;
	RslReceiver *res = NULL;

	if (intface) {
		rcv = dynamic_cast<RslReceiver *>(intface->receiver());
		m_hualinkMutex.lock();
		res = dynamic_cast<RslReceiver *>(m_receivers.remove(rcv, false, false));
		if (rcv && res) rcv->attachHua(NULL);
		m_hualinkMutex.unlock();
	}
	if (!res) Debug(DebugNote, "trying to detach unattached interface %p", intface);
	if (res) return res->iface();
	else return NULL;
}


SignallingInterface *HUALink::detach_iface(unsigned int ifi)
{
	RslReceiver *rcv;
	RslReceiver *res = NULL;
	String ifi_str;
	
	ifi_str = static_cast<String>(ifi);
	m_hualinkMutex.lock();
	rcv = dynamic_cast<RslReceiver *>(m_receivers[ifi_str]);
	if (rcv) {
		res = dynamic_cast<RslReceiver *>(m_receivers.remove(rcv, false, false));
		if (res && rcv) rcv->attachHua(NULL);
	} else {
		Debug(DebugNote, "trying to detach unattached interface with ifi %u", ifi);
	}
	m_hualinkMutex.unlock();
	if (res) return res->iface();
	else return NULL;
}

RslReceiver *HUALink::receiver(unsigned int ifi)
{
	String ifi_str;
	RslReceiver *res;

	ifi_str = static_cast<String>(ifi);
	m_hualinkMutex.lock();
	res = dynamic_cast<RslReceiver *>(m_receivers[ifi_str]);
	m_hualinkMutex.unlock();
	return res;
}

SignallingInterface *HUALink::iface(unsigned int ifi)
{
	return HUALink::receiver(ifi)->iface();
}

/* 1. extract ifi 
 * 2. find corresponding interface
 * 3. find attached receiver
 * 4. send the message via the receiver's sendPkt();
 */

bool HUALink::processMSG(unsigned char msgVersion, unsigned char msgClass,
	unsigned char msgType, const DataBlock& msg, int streamId)
{
	unsigned int ifi;
	DataBlock hdlcdata;
	bool	res;
	RslReceiver *rcv;
	
	if ((msgVersion != 1) || (msgClass != QPTM) ||
		(msgType != QPTM_UNIT_DATA_REQ)) {
		Debug(DebugWarn, "do not know how to process message: ver=%u, class=%u, type=%u, sid=%d", 
				msgVersion, msgClass, msgType, streamId);	
		return false;
	}
	res = SIGAdaptation::getTag(msg, TAG_INTERFACE_ID_NUM, ifi);
	if (!res) {
		Debug(DebugWarn, "No interface id in the message: ver=%u, class=%u, type=%u, sid=%d", 
				msgVersion, msgClass, msgType, streamId);
		return false;
	}
	res = SIGAdaptation::getTag(msg, TAG_PROTOCOL_DATA, hdlcdata);
	if (!res) {
		Debug(DebugWarn, "No protocol data in the message: ver=%u, class=%u, type=%u, sid=%d", 
				msgVersion, msgClass, msgType, streamId);
		return false;
	}
	rcv = receiver(ifi);
	if (!rcv) {
		Debug(DebugWarn, "no receiver with ifi=%u registered, dropping the packet", ifi);
		return false;
	}
	return (rcv->sendPkt(hdlcdata));
}

int HUALink::ifi2sid(unsigned int ifi)
{
#warning stub
	return 1;
}

bool HUALink::sendHDLC(const DataBlock& packet, unsigned int ifi)
{
	int		streamid;
	DataBlock	msg;
#ifdef	DEBUG_ME_HARDER
	static int	pktno = 0;
#endif
	streamid = ifi2sid(ifi);
	if (!connected(streamid)) return false;
	SIGAdaptation::addTag(msg, TAG_INTERFACE_ID_NUM, ifi);
	SIGAdaptation::addTag(msg, TAG_PROTOCOL_DATA, packet);
#ifdef	DEBUG_ME_HARDER
	String	tmp;
	tmp << "pkt=";
	tmp << pktno;
	SIGAdaptation::addTag(msg, TAG_DIAG_INFO, tmp.c_str());
	pktno++;
#endif
	return transmitMSG(QPTM, QPTM_UNIT_DATA_REQ, msg, streamid);
}

/* code */

INIT_PLUGIN(RslMux);

/* configuring:
 *
 * 1. create SignallingInterface
 * 2. create RslReceiver
 * 3. create SIGTransport
 *
 */
static void configure_multiplexer(NamedList *params)
{
	SignallingInterface *ifc;
	RslReceiver *rcv;
	SIGTransport *trn;
	HUALink *hl;
	
#warning implementation

}



void RslMux::initialize()
{
	Output("Initializing module RSL Multiplexer");
	SignallingEngine* engine = SignallingEngine::self();
	if (!engine) {
		Debug(DebugWarn,"SignallingEngine not yet created, cannot install RSL multiplexers [%p]",this);
		return;
	}
	unsigned int n = s_multiplexers.length();
	unsigned int i;
//    for (i = 0; i < n; i++) {
//	IsupIntercept* isup = YOBJECT(IsupIntercept,s_manglers[i]);
//	if (isup)
//	    isup->m_used = false;
//  }
	Configuration cfg(Engine::configFile("rslmux"));
	n = cfg.sections();
	for (i = 0; i < n; i++) {
		NamedList* sect = cfg.getSection(i);
		if (TelEngine::null(sect)) continue;
		if (!sect->getBoolValue("enable",true)) continue;
		configure_multiplexer(sect);
	}
//	IsupIntercept* isup = YOBJECT(IsupIntercept,s_manglers[*sect]);
//	if (!isup) {
//	    isup = new IsupIntercept(*sect);
//	    engine->insert(isup);
//	    s_manglers.append(isup);
//	}
//	isup->m_used = true;
//	isup->initialize(sect);
//    }
//    ListIterator iter(s_manglers);
//    while (IsupIntercept* isup = YOBJECT(IsupIntercept,iter.get())) {
//	if (!isup->m_used)
//	    s_manglers.remove(isup);
//    }
	NamedList params("");
	params.addParam("local-config", "true");
	params.addParam("basename", "zap_isdn");
	params.addParam("sig", "zap_isdn");
	params.assign("rsl/D");
	params.addParam("rxunderrun", "2500");
	
	SignallingInterface *ifc = YSIGCREATE(SignallingInterface, &params);
	if (!ifc) {
		Debug(DebugWarn, "RSL interface is not created");
		return;
	}
	RslReceiver *rcv = new RslReceiver("defreceiver");
	rcv->setIFI(1);
	rcv->attach(ifc);
	if (ifc->initialize(&params)) {
		rcv->control(SignallingInterface::Enable);
		Debug(DebugWarn, "RSL interface is up");
	}

	NamedList tr_params("");
	tr_params.addParam("local-config", "true");
	tr_params.addParam("basename", "hua_link");
	SIGTransport *trn = YSIGCREATE(SIGTransport, &tr_params);
	if (!trn) {
		Debug(DebugWarn, "HUA transport is not created");
		return;
	}
	trn->initialize(&tr_params);
	Output("HUA transport initialized");
//	HUALink *hl = YSIGCREATE(HUALink);
	HUALink *hl = new HUALink();
	hl->attach(trn);
	hl->attach_iface(ifc);

}

UNLOAD_PLUGIN(unloadNow)
{
//    if (unloadNow)
//	s_multiplexers.clear();
    return true;
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */

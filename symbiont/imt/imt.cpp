/**
 * imt.cpp
 * Copyright (C) 2017 Stacy 
 * Most of the code shamelessly stolen from 
 *  	dumbchan.cpp & analog.cpp from yate project.
 * analog.cpp: Copyright (C) 2004-2014 Null Team
 * dumbchan.cpp: Copyright (C) 2005 Maciek Kaminski
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include <yatengine.h>
#include <yatephone.h>
#include "imt_line.h"
#include <assert.h>

using namespace TelEngine;
namespace { // anonymous

class ImtDriver;
class ImtChannel;
class ImtReceiver;


static Configuration s_cfg;
static void build_group(ImtGroup *group, ObjList &spanlist, String &error);
static bool create_spans(ImtGroup *group, const NamedList &params, const NamedList &defaults, 
		String &error);
static bool init_group(ImtGroup *group, const NamedList &params, const NamedList &defaults, 
		String &error);
static int decode_addr(const String &src,  String &group,  bool first);
static bool execute_incoming(ImtDriver *drv, CallEndpoint *dst_ep, Message &msg, String &dest);
static bool execute_outgoing(ImtDriver *drv, Message &msg, String &dest);

/* various control messages handlers */
static bool handle_answer(ImtDriver *drv, ImtLine *line, Message &msg);
static bool handle_hangup(ImtDriver *drv, ImtLine *line, Message &msg);
static bool handle_seize(ImtDriver *drv, ImtLine *line, Message &msg);
static bool handle_busyout(ImtDriver *drv, ImtLine *line, Message &msg);
static bool handle_release(ImtDriver *drv, ImtLine *line, Message &msg);
static bool handle_debug(ImtDriver *drv, ImtLine *line, Message &msg);

static ImtLine *find_line_byaddr(ImtDriver *drv, String &address);


class ImtDriver : public Driver
{
public:
	ImtDriver();
	~ImtDriver();
	virtual void initialize();
	virtual bool msgExecute(Message& msg, String& dest);
	ImtGroup *findGroup(const String &name);
	void removeGroup(ImtGroup *group);
private:
	void configure(void);
	ObjList m_groups;
	ImtReceiver *m_receiver;
};

INIT_PLUGIN(ImtDriver);

class ImtChannel :  public Channel
{
public:
	ImtChannel(ImtLine *line, const NamedList& exeMsg, bool outgoing);
	~ImtChannel();
	bool setAudio(bool from_tdm);
	virtual void disconnected(bool final, const char *reason);
	void outCallAnswered(void);
	virtual bool msgProgress(Message &msg);
	virtual bool msgRinging(Message &msg);
	virtual bool msgAnswered(Message &msg);
	virtual bool msgDrop(Message &msg, const char *reason);
	inline void setTargetid(const char* targetid)
		{ m_targetid = targetid; }
	ImtLine *m_line;
	bool	voice_on;
	String	m_symbiont_id;
};


class ImtReceiver : public MessageReceiver
{
public:	
	enum {
		Operation,
	};
	ImtReceiver(ImtDriver *drv);
	virtual bool received(Message &msg, int id);
private:
	ImtDriver *m_driver;
};



ImtChannel::ImtChannel(ImtLine *line, const NamedList& exeMsg, bool outgoing) :
	Channel(__plugin, 0, outgoing),
	m_line(line),
	voice_on(false)
{
	if (line->chanptr) {
		Debug(DebugNote, "line already has a chanptr set");
	}
	m_line->chanptr = this;
	Message* s = message("chan.startup");
	s->setParam("direction", "startup");
	if (isOutgoing()) s->copyParams(exeMsg,"caller,callername,called,billid,callto,username,symbiont_id");
	m_symbiont_id = exeMsg.getValue("symbiont_id");
	
	Engine::enqueue(s);
}



void ImtChannel::disconnected(bool final, const char *reason)
{
	Lock lock(m_mutex);
	
	Debug(DebugAll,"ImtChannel::disconnected() '%s'",reason);
	Channel::disconnected(final,reason);
}


// Set data source and consumer
// from_tdm == true means voice from the dcp terminal to the yate
// is enabled,
// false means that voice from yate to the dcp terminal is enabled
bool ImtChannel::setAudio(bool from_tdm)
{
	if (!voice_on) {
		SignallingCircuit *cic = m_line->circuit();
		if (cic) cic->status(SignallingCircuit::Connected, true);
		voice_on = true;
	}

	if ((from_tdm && getSource()) || (!from_tdm && getConsumer()))
		return true;

	SignallingCircuit *cic = m_line ? m_line->circuit() : 0;
	if (cic) {
		XDebug(this, DebugNote, "About to set audio - from_tdm=%d", from_tdm);
		if (from_tdm) setSource(static_cast<DataSource *>(cic->getObject(YATOM("DataSource"))));
		else setConsumer(static_cast<DataConsumer *>(cic->getObject(YATOM("DataConsumer"))));
	}

	DataNode *res = from_tdm ? (DataNode *)getSource() : (DataNode *)getConsumer();
	if (res)
		DDebug(this, DebugAll, "Data %s set to (%p): '%s' [%p]", 
		       from_tdm?"source":"consumer", res, res->getFormat().c_str(), this);
	else
		Debug(this, DebugNote, "Failed to set data %s%s [%p]", 
		      from_tdm?"source":"consumer", cic?"":". Circuit is missing", this);
	return res != 0;
}


void ImtChannel::outCallAnswered(void)
{
	if (isAnswered()) return;
	status("answered");
	if (m_line) {
// %%%%%%%%%%%%%%%%%%%%
#warning - following line is for exploration only
		SignallingCircuit *cic = m_line ? m_line->circuit() : NULL ;
		if (cic) cic->status(SignallingCircuit::Connected, true);
// %%%%%%%%%%%%%%%%
		m_line->changeState(ImtLine::Talking);
//		m_line->setCircuitParam("echotrain");
	}
	setAudio(true);
	setAudio(false);
	Engine::enqueue(message("call.answered", false, true));
}

bool ImtChannel::msgProgress(Message &msg)
{
	Lock lock(m_mutex);
	
	Debug(this, DebugAll, "call progress ");

	if (getPeer()->getSource()) {
		setAudio(false);
	}
	return true;
}

bool ImtChannel::msgRinging(Message &msg)
{
	Lock lock(m_mutex);
	
	Debug(this, DebugAll,"call ringing ");
	if (getPeer()->getSource()) {
		setAudio(false);
	}
	return true;
}


bool ImtChannel::msgAnswered(Message &msg)
{
	Lock lock(m_mutex);
	
	Debug(this, DebugAll, "call answered ");
	if (m_line) m_line->changeState(ImtLine::Talking);
	setAudio(true);
	setAudio(false);
	Channel::msgAnswered(msg);
	return true;
}

bool ImtChannel::msgDrop(Message &msg, const char *reason)
{
	Lock lock(m_mutex);
		
	return Channel::msgDrop(msg, reason);
}


ImtChannel::~ImtChannel()
{
    if (m_line) m_line->chanptr = NULL;
    Debug(this,DebugAll,"ImtChannel::~ImtChannel() src=%p cons=%p",getSource(),getConsumer());
    Message* s = message("chan.hangup");
    s->setParam("symbiont_id", m_symbiont_id);
    Engine::enqueue(s);
}

ImtReceiver::ImtReceiver(ImtDriver *drv) :
	MessageReceiver(),
	m_driver(drv)
{

}


bool ImtReceiver::received(Message &msg, int id)
{
	String operation(msg.getValue("operation"));
	String address(msg.getValue("address"));
	ImtLine	*line;
	const char *error = "failure";
	String cause;
	
	if (!m_driver) return false;
	if (id != ImtReceiver::Operation) return false;
	if (operation == "ping") {
		msg.retValue() << "pong";
		return true;
	}
	line = find_line_byaddr(m_driver, address);
	if (!line) {
		cause << "No line with address '" << address << "'";
		error = "noroute";
		msg.setParam("error", error);
		Debug(m_driver, DebugNote, "IMT operation \"%s\" failed: %s", operation.c_str(), cause.c_str());
		return false;
	}


	
	if (operation == "answer") {
		return handle_answer(m_driver, line, msg);
	} else if (operation == "hangup") {
		return handle_hangup(m_driver, line, msg);
	} else if (operation == "seize") {
		return handle_seize(m_driver, line, msg);
	} else if (operation == "busyout") {
		return handle_busyout(m_driver, line, msg);
	} else if (operation == "release") {
		return handle_release(m_driver, line, msg);
	} else if (operation == "debug") {
		return handle_debug(m_driver, line, msg);
	}
	Debug(m_driver, DebugNote, "IMT operation \"%s\" unknown", operation.c_str());
	return false;
}

/* control message handlers */

// answer incoming call if in progress
static bool handle_answer(ImtDriver *drv, ImtLine *line, Message &msg)
{
	ImtLine::State	state;
	bool	res = false;
	ImtChannel	*chan;

	if (!line) return false;
	if (!drv) return false;

	state = line->state();
//	if (state == ImtLine::Seized) {
		chan = (ImtChannel *)line->chanptr;
		if (!chan) {
			Debug(drv, DebugNote, "Line has no channel");
			return false;
		}
		chan->outCallAnswered();
//	}
	return res;

}

//hangup call in progress
static bool handle_hangup(ImtDriver *drv, ImtLine *line, Message &msg)
{
#warning implementation
	return false;
}

//seize the line
static bool handle_seize(ImtDriver *drv, ImtLine *line, Message &msg)
{
	ImtLine::State	state;
	bool	res = false;

	if (!line) return false;
	if (!drv) return false;

	state = line->state();
	if (state == ImtLine::Idle) {
		res = line->changeState(ImtLine::Seized);
	}
	return res;
}

//put the line in the out-of-service state
static bool handle_busyout(ImtDriver *drv, ImtLine *line, Message &msg)
{
	ImtLine::State	state;
	bool	res = false;

	if (!line) return false;
	if (!drv) return false;

	state = line->state();
	if (state == ImtLine::Idle) {
		res = line->changeState(ImtLine::OutOfService);
	}
	return res;
}

//release busyied out or seized line back to idle
static bool handle_release(ImtDriver *drv, ImtLine *line, Message &msg)
{
	ImtLine::State	state;
	bool	res = false;

	if (!line) return false;
	if (!drv) return false;

	state = line->state();
	if (state == ImtLine::Seized) {
//#error clear calls in progress
		res = line->changeState(ImtLine::Idle);
	} else if (state == ImtLine::OutOfService) {
		res = line->changeState(ImtLine::Idle);
	}
	return res;
}

// this is to implement various debug actions, don't call
// in production env.
static bool handle_debug(ImtDriver *drv, ImtLine *line, Message &msg)
{
	ImtLine::State	state;
	bool	res = false;
	ImtChannel	*chan;

	if (!line) return false;
	if (!drv) return false;

	state = line->state();
	chan = (ImtChannel *)line->chanptr;
	if (!chan) {
		Debug(drv, DebugNote, "Line has no channel");
		return false;
	}
	Debug(drv, DebugNote, "test answer in state %d", state);
//	chan->outCallAnswered();
	if (state != ImtLine::Talking) {
		SignallingCircuit *cic = line->circuit();
		if (cic) cic->status(SignallingCircuit::Connected, true);
		line->changeState(ImtLine::Talking);
//		m_line->setCircuitParam("echotrain");
	}
	String direction(msg.getValue("direction"));
	if (direction == "in") chan->setAudio(true);
	else if (direction == "out") chan->setAudio(false);
	return res;
}




/* end of control message handlers */

static ImtLine *find_line_byaddr(ImtDriver *drv, String &address)
{
	int	cic;
	ImtLine	*line = NULL;
	String	grpname;
	ImtGroup *group;
	
	if (!drv) return line;
	cic = decode_addr(address, grpname, true);
	if (cic < 0) return line;
	group = drv->findGroup(grpname);
	if (!group) return line;
	line = static_cast<ImtLine *>(group->findLine(cic));
	return line;
}


static bool execute_incoming(ImtDriver *drv, CallEndpoint *dst_ep, 
		Message &msg, String &dest)
{

	ImtLine	*line = NULL;
	String cause;
	const char *error = "failure";
	int cic;
	String grpname;
	ImtGroup *group;
	
	assert(drv);
	assert(dst_ep);
		
	line = find_line_byaddr(drv, dest);
	if (!line) {
		cause << "No line with address '" << dest << "'";
		error = "noroute";
		goto errout;
	}
#warning - check for busy line
/*
 *	if (line->userdata()) {
 *		cause << "Line '" << line->address() << "' is busy";
 *		error = "busy";
 *		goto errout;
 *	}
 */
	if (line->state() == ImtLine::OutOfService) {
		cause << "Line '" << line->address() << "' is out of service";
		error = "noroute";
		goto errout;
	}
	if (!line->ref()) {
		cause = "ref() failed";
		goto errout;
	}
	
	Debug(drv, DebugAll, "Executing call. caller=%s called=%s line=%s", 
	      msg.getValue("caller"), msg.getValue("called"), line->address());
	{ //this is just a block to enclose constructor/destructor for above goto
		msg.clearParam("error");
		ImtChannel *chan = new ImtChannel(line,msg,true);
		chan->debugChain(drv);
		chan->debugCopy(drv);
		chan->initChan(); // must be called after object is fully constructed
// connect() comes from DataEndpoint:: - connect source and consumer to a peer
		if (chan->connect(dst_ep, msg.getValue("reason"))) {	
			chan->callConnect(msg);
			msg.setParam("peerid", chan->id());
			msg.setParam("targetid", chan->id());
			chan->setTargetid(dst_ep->id());
// autoring unless parameter is already set in message
			if (!msg.getParam("autoring"))	msg.addParam("autoring","true");
// %%%%%%%%%%%%%%%%%
//#warning - following line just for autoanswer testing - remove 
//			String autoanswer(msg.getValue("autoanswer"));
//			if (autoanswer == "yes") chan->outCallAnswered();	
// %%%%%%%%%%%%%%%%%
			chan->deref();
			return true;
		} else {
			chan->destruct();
			return false;
		}
	}
errout:
	Debug(drv, DebugNote, "IMT call failed: %s", cause.c_str());
	msg.setParam("error", error);
	return false;
}

static bool execute_outgoing(ImtDriver *drv, Message &msg, String &dest)
{
	ImtLine *line;
	int	cic;
	String grpname;
	String src;
	ImtGroup *group;
	String cause;
	const char *error = "failure";

	assert(drv);
//outgoing call - call.execute with no call endpoint
	const char *target = msg.getValue("target");
	if (!target) {
		Debug(drv,DebugWarn,"Outgoing call with no target!");
		return false;
	}
	
	src = msg.getValue("address");
	if (src.null()) src = msg.getValue("srcline");
	if (src.null()) {
		Debug(drv, DebugWarn, "Outgoing call with no originating line");
		return false;
	}
	line = find_line_byaddr(drv, src);
	if (!line) {
		cause << "No line with address '" << src << "'";
		goto errout;
	}

#warning - check for busy line
/*
 *	if (line->userdata()) {
 *		cause << "Line '" << line->address() << "' is busy";
 *		error = "busy";
 *		goto errout;
 *	}
 */
	if (line->state() == ImtLine::OutOfService) {
		cause << "Line '" << line->address() << "' is out of service";
		goto errout;
	}
	if (!line->ref()) {
		cause = "ref() failed";
		goto errout;
	}

	{	// this is to enclose constructors 
		ImtChannel* chan = new ImtChannel(line,msg,false);
		chan->initChan();

		String caller = msg.getValue("caller");
		if (caller.null()) caller << "imt/" << src;

		Message m("call.route");
		m.addParam("driver","imt");
		m.addParam("id", chan->id());
		m.addParam("caller",caller);
		m.addParam("called",target);
		m.copyParam(msg,"callername");
		m.copyParam(msg,"maxcall");
		m.copyParam(msg,"timeout");
		m.addParam("address", src);
		m.copyParams(msg,msg.getValue("copyparams"));

		const String& callto = msg["direct"];
		if (callto || Engine::dispatch(m)) {
			m = "call.execute";
			if (callto) m.addParam("callto",callto);
			else {
				m.addParam("callto",m.retValue());
				m.retValue().clear();
			}
			m.setParam("id", chan->id());
			m.userData(chan);
			if (Engine::dispatch(m) && chan->callRouted(m)) {
				chan->callAccept(m);
				msg.copyParam(m,"id");
				msg.copyParam(m,"peerid");
				const char* targetid = m.getValue("targetid");
				if (targetid) {
					msg.setParam("targetid",targetid);
					chan->setTargetid(targetid);
				}
				chan->deref();
				return true;
			} else {
				msg.copyParam(m,"error");
				msg.copyParam(m,"reason");
			}
			Debug(drv,DebugWarn,"Outgoing call not accepted!");
		} else Debug(drv,DebugWarn,"Outgoing call but no route!");
		chan->destruct();
		return false;
	}	
errout:
	Debug(drv, DebugNote, "IMT call failed: %s", cause.c_str());
	msg.setParam("error", error);
	return false;
}

bool ImtDriver::msgExecute(Message& msg, String& dest)
{
	CallEndpoint *dst_ep = YOBJECT(CallEndpoint,msg.userData());
	bool res;

	Debug(this, DebugNote, "call.execute for imt - dest=%s, ep@%p", 
		dest.c_str(), dst_ep);
	if (dst_ep) res = execute_incoming(this, dst_ep, msg, dest);
	else res = execute_outgoing(this, msg, dest);
	return res;
}
	

ImtDriver::ImtDriver()
    : Driver("imt", "varchans")
{
    Output("inter-machine trunk module loaded");
}

ImtDriver::~ImtDriver()
{
    Output("Unloading inter-machine trunk module");
}

void ImtDriver::initialize()
{
    Output("Initializing inter-machine trunk module");
    s_cfg = Engine::configFile("imt");
    s_cfg.load();
    setup("imt", false);
    configure();
    m_receiver = new ImtReceiver(this);
    Engine::install(new MessageRelay("imt.operation",m_receiver,ImtReceiver::Operation,100,name()));
    Output("ImtDriver initialized");
}

ImtGroup *ImtDriver::findGroup(const String &name)
{
	Lock lock(this);
	ObjList *obj = m_groups.find(name);
	return obj ? static_cast<ImtGroup *>(obj->get()) : 0;
}

void ImtDriver::removeGroup(ImtGroup *group)
{
	if (!group) return;
	Lock lock(this);
	Debug(this, DebugAll, "Removing group '%s' @ %p", group->debugName(), group);
	m_groups.remove(group);
}


void ImtDriver::configure(void)
{
	int	i, maxsec;
	bool	enable, create;
	ImtGroup *group;
	NamedList *sect, *general;
	NamedList dummy("");
	String error;
	
	general = s_cfg.getSection("general");
	if (!general) general = &dummy;
	
	maxsec = s_cfg.sections();
	for (i = 0; i < maxsec; i++) {
		sect = s_cfg.getSection(i);
		if (!sect || sect->null() || 
			*sect == "general" ||
			sect->startsWith("line")) continue;

		group = findGroup(*sect);
		enable = sect->getBoolValue("enable", true);
		if (!enable) {
			if (group) removeGroup(group);
			continue;
		}
		create = (group == NULL);
		Debug(this, DebugAll, "%s group %s", (create ? "creating" : "reloading"), 
			sect->c_str());
		if (create) {
			group = new ImtGroup(*sect);
			group->debugChain(this);
			group->debugCopy(this);
			lock();
			m_groups.append(group);
			unlock();
			XDebug(this, DebugAll, "Added group '%s' @ %p", group->debugName(), group);
		}
		if (!init_group(group, *sect, *general, error)) {
			Debug(this, DebugWarn, "Failed to %s group '%s'. Error: %s",
				(create ? "create" : "reload"), sect->c_str(), error.safe());
			if (create) removeGroup(group);
		}
	}
}

static void build_group(ImtGroup *group, ObjList &spanlist, String &error)
{
	unsigned int start = 0;
	ObjList *o;
	String *s;
	SignallingCircuitSpan *span;
	
	if (!group) return;
	for (o = spanlist.skipNull(); o; o = o->skipNext()) {
		s = static_cast<String *>(o->get());
		if (s->null()) continue;
		NamedList spanParams(*s);
		spanParams.addParam("local-config","true");
		span = group->buildSpan(*s, start, &spanParams);
		if (!span) {
			error << "Failed to build span '" << *s << "'";
			break;
		}
		start += span->increment();
	}
}

static bool create_spans(ImtGroup *group, const NamedList &params, const NamedList &defaults, 
			String &error)
{
	if (!group) return false;
	
	String device = params.getValue("spans");
	ObjList *voice = device.split(',' , false);
	if (voice) build_group(group, *voice, error);
	else error << "Missing or invalid spans = " << device;
	TelEngine::destruct(voice);
	if (error) return false;
	return true;
}

static bool init_group(ImtGroup *group, const NamedList &params, const NamedList &defaults, 
		String &error)
{
	bool ok = true;
	bool all, remove;
	unsigned int n, i;
	SignallingCircuit *cic;
	NamedList dummy("");
	NamedList *lineParams;
	String sectName;
	ImtLine *line;
	
	XDebug(DebugNote, "init_group() - at enter");
	if (!group) return false;
	XDebug(DebugNote, "init_group() - continuing");
	group->lock();
	if (!group->m_init) {
		group->m_init = true;
		ok = create_spans(group, params, defaults, error);
		if (!ok) return false;
		group->m_thread = new ImtThread(group);
		if (!group->m_thread->startup()) {
			Debug(DebugNote, "Cannot start IMT worker thread");
			return false;
		}
	}
	all = params.getBoolValue("useallcircuits", true);
	n = group->circuits().length();
	XDebug(DebugNote, "init_group() - continuing, ok=%d, all=%d, n=%d", ok, all, n);
	for (i = 0; i < n; i++) {
		cic = static_cast<SignallingCircuit *>(group->circuits()[i]);
		if (!cic) continue;
		sectName = "line " + group->toString() + "/" + String(cic->code());
		lineParams = s_cfg.getSection(sectName);
		XDebug(DebugNote, "init_group() - line params loaded for %s, @%p ", sectName.c_str(), lineParams);
		if (!lineParams) lineParams = &dummy;
		remove = lineParams->getBoolValue("enable", true);
		line = static_cast<ImtLine *>(group->findLine(cic->code()));
		if (remove) {
			if (line) {
				XDebug(DebugAll, "Removing line '%s' @ group %p", line->address(), group);
#warning implement lineUnavalable()
//				plugin.lineUnavailable(line);
				TelEngine::destruct(line);
				continue;
			}
		}
#warning completeLineParams()
		if (!all && lineParams == &dummy) continue;
		XDebug(DebugNote, "init_group() - about to create line");
		line = new ImtLine(group, cic->code(), params);
		XDebug(DebugNote, "init_group: created line with cic %d", cic->code());
		if (line) {
			group->appendLine(line);
			XDebug(DebugNote, "line  %u appended to group %s @ %p",
				cic->code(), group->debugName(), group);			
		} else {
			Debug(DebugNote, "Failed to create line %s/%u @ %p",
				group->debugName(), cic->code(), group);
			TelEngine::destruct(line);
		}
	}

#warning reload params
//	ok = group->reload(
	group->debugLevel(10);	// %%%
	group->unlock();
	return ok;
}


// Decode a line address into group name and circuit code
// Set first to decode group name until first '/'
// Return:
//   -1 if src is the name of the group
//   -2 if src contains invalid circuit code
//   Otherwise: The integer part of the circuit code
static int decode_addr(const String &src,  String &group,  bool first)
{
	int pos = first ? src.find("/") : src.rfind('/');
	if (pos == -1) {
		group = src;
		return -1;
	}
	group = src.substr(0, pos);
	return src.substr(pos+1).toInteger(-2);
}



}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */

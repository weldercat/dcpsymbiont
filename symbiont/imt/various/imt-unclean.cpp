/**
 * imt.cpp
 *
 * inter-machine trunk channel
 *
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004-2014 Null Team
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
#define XDEBUG	1
#define DEBUG 1

#include <yatephone.h>
#include <yatesig.h>

using namespace TelEngine;
namespace { // anonymous

class ModuleLine;
class ModuleGroup;
class ImtChannel;
class ImtDriver;			//inter-machine trunk driver




// Module's interface to an analog line or monitor
class ModuleLine : public AnalogLine
{
public:
	ModuleLine(ModuleGroup *grp,  unsigned int cic,  const NamedList &params,  const NamedList &groupParams);
	// Get the module group representation of this line's owner
	ModuleGroup *moduleGroup();
	inline const String &caller() const
	{
		return m_caller;
	}
	inline const String &callerName() const
	{
		return m_callerName;
	}
	inline String &called()
	{
		return m_called;
	}

	// Set the caller,  callername and called parameters
	inline void setCall(const char *caller = 0,  const char *callername = 0, 
	                    const char *called = 0)
	{
		m_caller = caller;
		m_callerName = callername;
		m_called = called;
	}
	// Set the caller,  callername and called parameters
	void copyCall(NamedList &dest,  bool privacy = false);
	// Fill a string with line status parameters
	void statusParams(String &str);
	// Fill a string with line status detail parameters
	void statusDetail(String &str);

protected:

	String m_called;                     // Called's extension
	// Call setup (caller id)
	String m_caller;                     // Caller's extension
	String m_callerName;                 // Caller's name

};



// Module's interface to a group of lines
class ModuleGroup : public AnalogLineGroup
{
public:
	// Create an FXO group of analog lines to be attached to a group of recorders
	inline ModuleGroup(const char *name)
		: AnalogLineGroup(AnalogLine::Unknown, name),  m_init(false)
	{
		m_prefix << name << "/";
	}
	virtual ~ModuleGroup()
	{}
	inline const String &prefix()
	{
		return m_prefix;
	}
	inline bool ringback() const
	{
		return m_ringback;
	}
	// Remove all channels associated with this group and stop worker thread
	virtual void destruct();
	// Process an event geberated by a line
	void handleEvent(ModuleLine &line,  SignallingCircuitEvent &event);
	// Apply debug level. Call create and create worker thread on first init
	// Re(load) lines and calls specific group reload
	// Return false on failure
	bool initialize(const NamedList &params,  const NamedList &defaults,  String &error);
	// Append/remove endpoints from list
	void setEndpoint(CallEndpoint *ep,  bool add);
protected:
	// Disconnect all group's endpoints
	void clearEndpoints(const char *reason = 0);
private:
	// Create FXS/FXO group data: called by initialize() on first init
	bool create(const NamedList &params,  const NamedList &defaults, 
	            String &error);
	// Reload FXS/FXO data: called by initialize() (not called on first init if create failed)
	bool reload(const NamedList &params,  const NamedList &defaults, 
	            String &error);
	// Reload existing line's parameters
	void reloadLine(ModuleLine *line,  const NamedList &params);
	// Build the group of circuits (spans)
	void buildGroup(ModuleGroup *group,  ObjList &spanList,  String &error);
	// Complete missing line parameters from other list of parameters
	inline void completeLineParams(NamedList &dest,  const NamedList &src,  const NamedList &defaults) {
		for (unsigned int i = 0; lineParams[i]; i++)
			if (!dest.getParam(lineParams[i]))
				dest.addParam(lineParams[i], src.getValue(lineParams[i], 
				              defaults.getValue(lineParams[i])));
	}
	// Line parameters that can be overridden
	static const char *lineParams[];

	bool m_init;                         // Init flag
	bool m_ringback;                     // Lines need to provide ringback
	String m_prefix;                     // Line prefix used to complete commands
	// Recorder group data
	ObjList m_endpoints;                 // Record data endpoints
};


// Channel associated with an analog line
class ImtChannel : public Channel
{
	friend class ModuleGroup;            // Copy data
public:
	ImtChannel(ModuleLine *line,  Message *msg);
	virtual ~ImtChannel();
	inline ModuleLine *line() const
	{
		return m_line;
	}
	void outCallAnswered(bool stopDial = true);
	
protected:
	// Route incoming. If first is false the router is started on second ring
	// Set data source and consumer
	bool setAudio(bool in);
	// Hangup. Release memory
	virtual void destroyed();
	// Detach the line from this channel and reset it
	// Outgoing call answered: set call state,  start echo train,  open data source/consumer


private:
	ModuleLine *m_line;                  // The analog line associated with this channel
	bool m_hungup;                       // Hang up flag
};



// The driver
class ImtDriver : public Driver
{
public:
	ImtDriver();
	~ImtDriver();
	virtual void initialize();
	virtual bool msgExecute(Message &msg, String &dest);
	virtual void dropAll(Message &msg);
	bool lineUnavailable(ModuleLine *line);
	// Disconnect or deref a channel
	void terminateChan(ImtChannel *ch, const char *reason = "normal");
	// Find a group by its name
	inline ModuleGroup *findGroup(const String &name) {
		Lock lock(this);
		ObjList *obj = m_groups.find(name);
		return obj ? static_cast<ModuleGroup *>(obj->get()) : 0;
	}
protected:
	virtual bool received(Message &msg, int id);
	// Handle command complete requests
	void removeGroup(ModuleGroup *group);
	// Find a group by its name
	ModuleGroup *findGroup(const char *name);
private:
	bool m_init;                         // Init flag
	ObjList m_groups;                    // Analog line groups
};

/**
 * Module data and functions
 */
static ImtDriver plugin;
static Configuration s_cfg;
static bool s_engineStarted = false;               // Received engine.start message
static const char *s_lineSectPrefix = "line ";     // Prefix for line sections in config
static const char *s_unk = "unknown";              // Used to set caller
// Status detail formats
static const char *s_lineStatusDetail = "format=State|UsedBy";
static const char *s_groupStatusDetail = "format=Type|Lines";
static const char *s_recStatusDetail = "format=Status|Address|Peer";


// Decode a line address into group name and circuit code
// Set first to decode group name until first '/'
// Return:
//   -1 if src is the name of the group
//   -2 if src contains invalid circuit code
//   Otherwise: The integer part of the circuit code
inline int decodeAddr(const String &src,  String &group,  bool first)
{
	int pos = first ? src.find("/") : src.rfind('/');
	if (pos == -1) {
		group = src;
		return -1;
	}
	group = src.substr(0, pos);
	return src.substr(pos+1).toInteger(-2);
}


/* ModuleLine implementation */

ModuleLine::ModuleLine(ModuleGroup *grp,  unsigned int cic,  const NamedList &params,  const NamedList &groupParams)
	: AnalogLine(grp, cic, params)
{
}


inline ModuleGroup *ModuleLine::moduleGroup()
{
	return static_cast<ModuleGroup *>(group());
}

// Set the caller,  callername and called parameters
void ModuleLine::copyCall(NamedList &dest,  bool privacy)
{
	if (privacy)
		dest.addParam("callerpres", "restricted");
	else {
		if (m_caller)
			dest.addParam("caller", m_caller);
		if (m_callerName)
			dest.addParam("callername", m_callerName);
	}
	if (m_called)
		dest.addParam("called", m_called);
}

void ModuleLine::statusParams(String &str)
{
	str.append("module=", ";") << plugin.name();
	str << ", address=" << address();
	str << ", type=" << lookup(type(), typeNames());
	str << ", state=" << lookup(state(), stateNames());
	str  << ", usedby=";
	if (userdata())
		str << (static_cast<CallEndpoint *>(userdata()))->id();
	str << ", polaritycontrol=" << polarityControl();
	if (type() == AnalogLine::FXO) {
		str << ", answer-on-polarity=" << answerOnPolarity();
		str << ", hangup-on-polarity=" << hangupOnPolarity();
	}
	else
		str << ", answer-on-polarity=not-defined, hangup-on-polarity=not-defined";
	str << ", callsetup=" << lookup(callSetup(), AnalogLine::csNames());
	// Lines with peer are used in recorders (don't send DTMFs)
	if (!getPeer())
		str << ", dtmf=" << (outbandDtmf() ? "outband" : "inband");
	else
		str << ", dtmf=not-defined";

	// Fill peer status
	bool master = (type() == AnalogLine::FXS && getPeer());
	if (master)
		(static_cast<ModuleLine *>(getPeer()))->statusParams(str);
}

// Fill a string with line status detail parameters
void ModuleLine::statusDetail(String &str)
{
	// format=State|UsedBy
	Lock lock(this);
	str.append(address(), ";") << "=";
	str << lookup(state(), AnalogLine::stateNames()) << "|";
	if (userdata())
		str << (static_cast<CallEndpoint *>(userdata()))->id();
}


/* ModuleGroup implementation */

const char *ModuleGroup::lineParams[] = {"echocancel", "dtmfinband", 
					 "autoanswer", 0 }; 


// Remove all channels associated with this group and stop worker thread
void ModuleGroup::destruct()
{
	clearEndpoints(Engine::exiting()?"shutdown":"out-of-service");
	AnalogLineGroup::destruct();
}

// Process an event generated by a line
void ModuleGroup::handleEvent(ModuleLine &line,  SignallingCircuitEvent &event)
{
	Lock lock(&plugin);
	ImtChannel *ch = static_cast<ImtChannel *>(line.userdata());
	DDebug(this, DebugInfo, "Processing event %u '%s' line=%s channel=%s", 
	       event.type(), event.c_str(), line.address(), ch?ch->id().c_str():"");

	switch (event.type()) {
	case SignallingCircuitEvent::OffHook:
	case SignallingCircuitEvent::Wink:
	default:
		;
	}
	if (ch) {
		switch (event.type()) {
		case SignallingCircuitEvent::Dtmf:
//			ch->evDigits(event.getValue("tone"), true);
			break;
		case SignallingCircuitEvent::PulseDigit:
//			ch->evDigits(event.getValue("pulse"), false);
			break;
		case SignallingCircuitEvent::OnHook:
//			ch->hangup(false);
			plugin.terminateChan(ch);
			break;
		case SignallingCircuitEvent::OffHook:
		case SignallingCircuitEvent::Wink:
//			ch->evOffHook();
			break;
		case SignallingCircuitEvent::RingBegin:
		case SignallingCircuitEvent::RingerOn:
//			ch->evRing(true);
			break;
		case SignallingCircuitEvent::RingEnd:
		case SignallingCircuitEvent::RingerOff:
//			ch->evRing(false);
			break;
		case SignallingCircuitEvent::LineStarted:
//			ch->evLineStarted();
			break;
		case SignallingCircuitEvent::DialComplete:
//			ch->evDialComplete();
			break;
		case SignallingCircuitEvent::Polarity:
//			ch->evPolarity();
			break;
		case SignallingCircuitEvent::Flash:
//			ch->evDigits("F", true);
			break;
		case SignallingCircuitEvent::PulseStart:
			DDebug(ch, DebugAll, "Pulse dialing started [%p]", ch);
			break;
		case SignallingCircuitEvent::Alarm:
		case SignallingCircuitEvent::NoAlarm:
//			ch->evAlarm(event.type() == SignallingCircuitEvent::Alarm, event.getValue("alarms"));
			break;
		default:
			Debug(this, DebugStub, "handleEvent(%u, '%s') not implemented [%p]", 
			      event.type(), event.c_str(), this);
		}
	} else {
		DDebug(this, DebugNote, "Event (%p, %u, %s) from line (%p, %s) without channel [%p]", 
		       &event, event.type(), event.c_str(), &line, line.address(), this);
	}
}



// Apply debug level. Call create and create worker thread on first init
// Re(load) lines and calls specific group reload
bool ModuleGroup::initialize(const NamedList &params,  const NamedList &defaults, 
                             String &error)
{
	if (!m_init)
		debugChain(&plugin);

	int level = params.getIntValue("debuglevel", m_init ? DebugEnabler::debugLevel() : plugin.debugLevel());
	if (level >= 0) {
		debugEnabled(0 != level);
		debugLevel(level);
	}

	m_ringback = params.getBoolValue("ringback");

	Lock lock(this);
	bool ok = true;
	if (!m_init) {
		m_init = true;
		ok = create(params, defaults, error);
		if (!ok) return false;
	}

	// (Re)load analog lines
	bool all = params.getBoolValue("useallcircuits", true);

	unsigned int n = circuits().length();
	Debug(this, DebugAll, "Total cic count=%d [%p]", n, this);
	for (unsigned int i = 0; i < n; i++) {
		SignallingCircuit *cic = static_cast<SignallingCircuit *>(circuits()[i]);
		if (!cic)
			continue;

		// Setup line parameter list
		NamedList dummy("");
		String sectName = s_lineSectPrefix + toString() + "/" + String(cic->code());
		NamedList *lineParams = s_cfg.getSection(sectName);
		if (!lineParams) lineParams = &dummy;
		bool remove = !lineParams->getBoolValue("enable", true);

		ModuleLine *line = static_cast<ModuleLine *>(findLine(cic->code()));

		// Remove existing line if required
		if (remove) {
			if (line) {
				XDebug(this, DebugAll, "Removing line=%s [%p]", line->address(), this);
				plugin.lineUnavailable(line);
				TelEngine::destruct(line);
			}
			continue;
		}

		// Reload line if already created. Notify plugin if service state changed
		completeLineParams(*lineParams, params, defaults);
		if (line) {
			bool inService = (line->state() != AnalogLine::OutOfService);
			reloadLine(line, *lineParams);
			if (inService != (line->state() != AnalogLine::OutOfService))
				plugin.lineUnavailable(line);
			continue;
		}

		// Don't create the line if useallcircuits is false and no section in config
		if (!all && lineParams == &dummy)
			continue;

		Debug(this, DebugAll, "Creating line for cic=%u [%p]", cic->code(), this);
		// Create a new line (create its peer if this is a monitor)
		line = new ModuleLine(this, cic->code(), *lineParams, params);

		// Append line to group: constructor may fail
		if (line) {
			appendLine(line);
			// Disconnect the line if not expecting call setup
			if (line->callSetup() != AnalogLine::Before)
				line->disconnect(true);
		} else {
			Debug(this, DebugNote, "Failed to create line %s/%u [%p]", 
			      debugName(), cic->code(), this);
			TelEngine::destruct(line);
		}
	}

	ok = reload(params, defaults, error);
	return ok;
}

// Append/remove endpoints from list
void ModuleGroup::setEndpoint(CallEndpoint *ep,  bool add)
{
	if (!ep)
		return;
	Lock lock(this);
	if (add)
		m_endpoints.append(ep);
	else
		m_endpoints.remove(ep, false);
}

// Disconnect all group's endpoints
void ModuleGroup::clearEndpoints(const char *reason)
{

	if (!reason) reason = "shutdown";

	DDebug(this, DebugAll, "Clearing endpoints with reason=%s [%p]", reason, this);
	lock();
	ListIterator iter(m_endpoints);
	for (;;) {
		RefPointer<CallEndpoint> c = static_cast<CallEndpoint *>(iter.get());
		unlock();
		if (!c) break;
		plugin.terminateChan(static_cast<ImtChannel *>((CallEndpoint *)c), reason);
		c = 0;
		lock();
	}
}


// Create FXS/FXO group data: called by initialize() on first init
bool ModuleGroup::create(const NamedList &params,  const NamedList &defaults, 
                         String &error)
{
	String device;
	ObjList *voice;
	
	device = params.getValue("spans");
	voice = device.split(',', false);
	if (voice && voice->count()) buildGroup(this, *voice, error);
	else error << "Missing or invalid spans=" << device;
	TelEngine::destruct(voice);
	if (error) return false;
	return true;
}

// Reload FXS/FXO data: called by initialize() (not called on first init if create failed)
bool ModuleGroup::reload(const NamedList &params,  const NamedList &defaults, 
                         String &error)
{
	// (Re)load tone targets
//	if (type() == AnalogLine::FXS) {
//		int tmp = params.getIntValue("call-ended-playtime", 
//		                             defaults.getIntValue("call-ended-playtime", 5));
//		if (tmp < 0)
//			tmp = 5;
//		m_callEndedPlayTime = 1000 * (unsigned int)tmp;
//		m_callEndedTarget = params.getValue("call-ended-target", 
//		                                    defaults.getValue("call-ended-target"));
//		if (!m_callEndedTarget)
//			m_callEndedTarget = "tone/busy";
//		m_oooTarget = params.getValue("outoforder-target", 
//		                              defaults.getValue("outoforder-target"));
//		if (!m_oooTarget)
//			m_oooTarget = "tone/outoforder";
//		m_lang = params.getValue("lang", defaults.getValue("lang"));
//		XDebug(this, DebugAll, "Targets: call-ended='%s' outoforder='%s' [%p]", 
//		       m_callEndedTarget.c_str(), m_oooTarget.c_str(), this);
//	}
	return true;
}



// Reload existing line's parameters
void ModuleGroup::reloadLine(ModuleLine *line,  const NamedList &params)
{
	if (!line) return;
	bool inService = !params.getBoolValue("out-of-service", false);
	if (inService == (line->state() != AnalogLine::OutOfService))
		return;
	Lock lock(line);
	Debug(this, DebugAll, "Reloading line %s in-service=%s [%p]", line->address(), String::boolText(inService), this);
	line->ref();
	line->enable(inService, true);
	line->deref();
}

// Build the circuit list for a given group
void ModuleGroup::buildGroup(ModuleGroup *group,  ObjList &spanList,  String &error)
{
	if (!group) return;
	unsigned int start = 0;
	for (ObjList *o = spanList.skipNull(); o; o = o->skipNext()) {
		String *s = static_cast<String *>(o->get());
		if (s->null())
			continue;
		NamedList spanParams(*s);
		spanParams.addParam("local-config","true");
		SignallingCircuitSpan *span = buildSpan(*s, start, &spanParams);
		if (!span) {
			error << "Failed to build span '" << *s << "'";
			break;
		}
		start += span->increment();
	}
}


/* ImtChannel implementation */


ImtChannel::ImtChannel(ModuleLine *line,  Message *msg)	: Channel(&plugin, 0, (msg != 0)), 
	  m_line(line), 
	  m_hungup(false)
{
	m_line->userdata(this);
	if (m_line->moduleGroup()) {
		m_line->moduleGroup()->setEndpoint(this, true);
//		m_ringback = m_line->moduleGroup()->ringback();
	}

	// Set caller/called from line
	if (isOutgoing()) {
		m_line->setCall(msg->getValue("caller"), msg->getValue("callername"), msg->getValue("called"));
	}
	m_line->setCall("", "", "off-hook");

	const char *mode = 0;
	mode = isOutgoing() ? "Outgoing" : "Incoming";
	Debug(this, DebugCall, "%s call on line %s caller=%s called=%s [%p]", 
	      mode, 
	      m_line->address(), 
	      m_line->caller().c_str(), m_line->called().c_str(), this);

	m_line->connect(false);

	// Incoming on FXO:
	// Caller id after first ring: delay router until the second ring and
	//  set/remove call setup detector
	m_address = m_line->address();

	// Startup
	Message *m = message("chan.startup");
	m->setParam("direction", status());
	if (msg)
		m->copyParams(*msg, "caller, callername, called, billid, callto, username");
	m_line->copyCall(*m);
	if (isOutgoing())
		m_targetid = msg->getValue("id");
	Engine::enqueue(m);

	// Init call
	setAudio(isIncoming());
	if (isOutgoing()) {
		// Check for parameters override
		// FXO: send start line event
		// FXS: start ring and send call setup (caller id)
		// Return if failed to send events

//		switch (line->type()) {
//		case AnalogLine::FXO:
//			m_line->sendEvent(SignallingCircuitEvent::StartLine, AnalogLine::Dialing);
//			break;
//		case AnalogLine::FXS:
//			m_callsetup = m_line->callSetup();
			// Check call setup override
//			{
//				NamedString *ns = msg->getParam("callsetup");
//				if (ns)
//					m_callsetup = lookup(*ns, AnalogLine::csNames(), AnalogLine::NoCallSetup);
//			}
//			m_privacy = getPrivacy(*msg);
//			if (m_callsetup == AnalogLine::Before)
//				m_line->sendCallSetup(m_privacy);
//			{
//				NamedList *params = 0;
//				NamedList callerId("");
//				if (m_callsetup != AnalogLine::NoCallSetup) {
//					params = &callerId;
//					m_line->copyCall(callerId, m_privacy);
//				}
//				m_line->sendEvent(SignallingCircuitEvent::RingBegin, AnalogLine::Dialing, params);
//			}
//			if (m_callsetup == AnalogLine::After)
//				m_dialTimer.interval(500);
//			break;
//		default:
//			;
//		}
		if (line->state() == AnalogLine::Idle) {
//			setReason("failure");
//			msg->setParam("error", "not everything implemented");
//			outCallAnswered(true);
			return;
		}
	} else {
//		m_line->changeState(AnalogLine::Dialing);

		// FXO: start ring timer (check if the caller hangs up before answer)
		// FXS: do nothing
//		switch (line->type()) {
//		case AnalogLine::FXO:
//			if (recorder == FXO) {
//				m_line->noRingTimer().stop();
//				break;
//			}
//			m_line->noRingTimer().interval(m_line->noRingTimeout());
//			DDebug(this, DebugAll, "Starting ring timer for " FMT64 "ms [%p]", 
//			       m_line->noRingTimer().interval(), this);
//			m_line->noRingTimer().start();
//			if (recorder == FXS) {
//				// The FXS recorder will route only on off-hook
//				m_routeOnSecondRing = false;
//				return;
//			}
//			break;
//		case AnalogLine::FXS:
//			break;
//		default:
//			;
//		}
	}
}

ImtChannel::~ImtChannel()
{
	XDebug(this, DebugCall, "ImtChannel::~ImtChannel() [%p]", this);
}



// Set data source and consumer
bool ImtChannel::setAudio(bool in)
{
	if ((in && getSource()) || (!in && getConsumer()))
		return true;

	SignallingCircuit *cic = m_line ? m_line->circuit() : 0;
	if (cic) {
		if (in) {
			setSource(static_cast<DataSource *>(cic->getObject(YATOM("DataSource"))));
			cic->setParam("tonedetect", "off");	// %%%
		} else {
			setConsumer(static_cast<DataConsumer *>(cic->getObject(YATOM("DataConsumer"))));
		}
	}

	DataNode *res = in ? (DataNode *)getSource() : (DataNode *)getConsumer();
	if (res)
		DDebug(this, DebugAll, "Data %s set to (%p): '%s' [%p]", 
		       in?"source":"consumer", res, res->getFormat().c_str(), this);
	else
		Debug(this, DebugNote, "Failed to set data %s%s [%p]", 
		      in?"source":"consumer", cic?"":". Circuit is missing", this);
	return res != 0;
}


// Outgoing call answered: set call state,  start echo train,  open data source/consumer
void ImtChannel::outCallAnswered(bool stopDial)
{

	if (isAnswered())
		return;

	m_answered = true;
	if (m_line) {
		m_line->changeState(AnalogLine::Answered);
		m_line->setCircuitParam("echotrain");
	}
	setAudio(true);
	setAudio(false);
	Engine::enqueue(message("call.answered", false, true));
}


// Hangup. Release memory
void ImtChannel::destroyed()
{
//	if (!m_hungup) { 
//		hangup(true);
//	} else {
//		setConsumer();
//		setSource();
//	}
//	setStatus("destroyed");
	Channel::destroyed();
}

/* ImtDriver implementation */

ImtDriver::ImtDriver()
	: Driver("imt","varchans"),
	  m_init(false)
{
	Output("Loaded module IMT Channel");
//	m_statusCmd << "status " << name();
//	m_recPrefix << prefix() << "rec/";
}

ImtDriver::~ImtDriver()
{
	Output("Unloading module IMT Channel");
//	m_groups.clear();
}


void ImtDriver::dropAll(Message &msg)
{
	const char *reason = msg.getValue("reason");
	if (!(reason && *reason)) reason = "dropped";
	DDebug(this, DebugInfo, "dropAll('%s')", reason);
	Driver::dropAll(msg);
	// Drop recorders
}


// Notification of line service state change or removal
// Return true if a channel or recorder was found
bool ImtDriver::lineUnavailable(ModuleLine *line)
{
	if (!line) return false;

	const char *reason = (line->state() == AnalogLine::OutOfService) ? "line-out-of-service" : "line-shutdown";
	Lock lock(this);
	for (ObjList *o = channels().skipNull(); o; o = o->skipNext()) {
		ImtChannel *ch = static_cast<ImtChannel *>(o->get());
		if (ch->line() != line)
			continue;
		terminateChan(ch, reason);
		return true;
	}

	// Check for recorders
	if (!line->getPeer()) return false;
	ModuleGroup *grp = line->moduleGroup();
	if (grp) return true;
	else return false;
}

// Destroy a channel
void ImtDriver::terminateChan(ImtChannel *ch,  const char *reason)
{
	if (!ch) return;
	DDebug(this, DebugAll, "Terminating channel %s peer=%p reason=%s", 
	       ch->id().c_str(), ch->getPeer(), reason);
	if (ch->getPeer()) ch->disconnect(reason);
	else ch->deref();
}

// Remove a group from list
void ImtDriver::removeGroup(ModuleGroup *group)
{
	if (!group) return;
	Lock lock(this);
	Debug(this, DebugAll, "Removing group (%p, '%s')", group, group->debugName());
	m_groups.remove(group);
}

// Find a group or recorder by its name
// Set useFxo to true to find a recorder by its fxo's name
ModuleGroup *ImtDriver::findGroup(const char *name)
{
	return findGroup(name);
}


void ImtDriver::initialize()
{
	Output("Initializing module IMT Channel");
	s_cfg = Engine::configFile("imt");
	s_cfg.load();

	NamedList dummy("");
	NamedList *general = s_cfg.getSection("general");
	if (!general)
		general = &dummy;

	// Startup
	setup("imt", false);
	if (!m_init) {
		m_init = true;
		installRelay(Masquerade);
		installRelay(Halt);
		installRelay(Progress);
		installRelay(Update);
		installRelay(Route);
//		Engine::install(new EngineStartHandler);
//		Engine::install(new ChanNotifyHandler);
	}

	// Build/initialize groups
//	String tmpRec = m_recPrefix.substr(0,m_recPrefix.length()-1);
	unsigned int n = s_cfg.sections();
	for (unsigned int i = 0; i < n; i++) {
		NamedList *sect = s_cfg.getSection(i);
		
		if (!sect || sect->null() || *sect == "general" || 
				sect->startsWith(s_lineSectPrefix))
			continue;

		// Check section name
		Output("section name=\"%s\"", sect->c_str());
		bool valid = true;
		if (*sect == name()) valid = false;
//		else
//			for (unsigned int i = 0; i < StatusCmdCount; i++)
//				if (*sect == s_statusCmd[i]) {
//					valid = false;
//					break;
//				}
		if (!valid) {
			Debug(this,DebugWarn,"Invalid use of reserved word in section name '%s'",sect->c_str());
			continue;
		}

		ModuleGroup *group = findGroup(*sect);
		if (!sect->getBoolValue("enable",true)) {
			if (group)
				removeGroup(group);
			continue;
		}

		// Create and/or initialize. Check for valid type if creating
		const char *stype = sect->getValue("type");
//		int type = lookup(stype,AnalogLine::typeNames(),AnalogLine::Unknown);
//		switch (type) {
//		case AnalogLine::FXO:
//		case AnalogLine::FXS:
//		case AnalogLine::Recorder:
//		case AnalogLine::Monitor:
//			break;
//		default:
			Debug(this,DebugWarn,"Unknown type for group '%s'", sect->c_str());
//			continue;
//		}

		bool create = true;
		Debug(this,DebugAll,"%sing group '%s' of type '%s'",create?"Creat":"Reload",sect->c_str(),stype);

		if (create) {
//			if (type != AnalogLine::Monitor)
//				group = new ModuleGroup((AnalogLine::Type)type,*sect);
//			else {
//				tmp << "/fxo";
				group = new ModuleGroup(*sect);
//			}
			lock();
			m_groups.append(group);
			unlock();
			XDebug(this,DebugAll,"Added group (%p,'%s')",group,group->debugName());
		}

		String error;
		if (!group->initialize(*sect,*general,error)) {
			Debug(this,DebugWarn,"Failed to %s group '%s'. Error: '%s'",
			      create?"create":"reload",sect->c_str(),error.safe());
			if (create)
				removeGroup(group);
		}
	}
}

bool ImtDriver::msgExecute(Message &msg, String &dest)
{
	CallEndpoint *peer = YOBJECT(CallEndpoint, msg.userData());
	ModuleLine *line = 0;
	String cause;
	const char *error = "failure";

	// Check message parameters: peer channel,  group,  circuit,  line
	while (true) {
		if (!peer) {
			cause = "No data channel";
			break;
		}
		String tmp;
		/* cic will be set to integer part of group addr or 
		 * -1 or -2 
		 * tmp will be set to the group part of addr */
		int cic = decodeAddr(dest, tmp, true);
		Debug(this, DebugNote, "call to group: %s", tmp.c_str());
		ModuleGroup *group = findGroup(tmp);
		if (group) {
			Debug(this, DebugNote, "call to cic=%d", cic);
			if (cic >= 0)
				line = static_cast<ModuleLine *>(group->findLine(cic));
			else if (cic == -1) {
				Lock lock(group);
				// Destination is a group: find the first free idle line
				for (ObjList *o = group->lines().skipNull(); o; o = o->skipNext()) {
					line = static_cast<ModuleLine *>(o->get());
					Lock lockLine(line);
					if (!line->userdata() && line->state() == AnalogLine::Idle)
						break;
					line = 0;
				}
				lock.drop();
				if (!line) {
					cause << "All lines in group '" << dest << "' are busy";
					error = "busy";
					break;
				}
			}
		}

		if (!line) {
			cause << "No line with address '" << dest << "'";
			error = "noroute";
			break;
		}
//		if (line->type() == AnalogLine::Unknown) {
//			cause << "Line '" << line->address() << "' has unknown type";
//			break;
//		}
		if (line->userdata()) {
			cause << "Line '" << line->address() << "' is busy";
			error = "busy";
			break;
		}
		if (line->state() == AnalogLine::OutOfService) {
			cause << "Line '" << line->address() << "' is out of service";
			error = "noroute";
			break;
		}
		if (!line->ref())
			cause = "ref() failed";
		break;
	}

	if (!line || cause) {
		Debug(this, DebugNote, "IMT call failed. %s", cause.c_str());
		msg.setParam("error", error);
		return false;
	}

	Debug(this, DebugAll, "Executing call. caller=%s called=%s line=%s", 
	      msg.getValue("caller"), msg.getValue("called"), line->address());

	msg.clearParam("error");
	// Create channel
	ImtChannel *imtCh = new ImtChannel(line, &msg);
	imtCh->initChan();
	error = msg.getValue("error");
	if (!error) {
		if (imtCh->connect(peer, msg.getValue("reason"))) {
			imtCh->callConnect(msg);
			msg.setParam("peerid", imtCh->id());
			msg.setParam("targetid", imtCh->id());
			if (imtCh->line()) {
				if (imtCh->line()->type() == AnalogLine::FXS)
					Engine::enqueue(imtCh->message("call.ringing", false, true));
				else imtCh->outCallAnswered(true);
			}
		}
	} else Debug(this, DebugNote, "IMT call failed with reason '%s'", error);
	imtCh->deref();
	return !error;

}



bool ImtDriver::received(Message &msg,  int id)
{
	String target;

	switch (id) {
	case Masquerade:
		target = msg.getValue("id");
		if (target.startsWith(prefix())) {
			
		}
	case Status:
	case Drop:
		return Driver::received(msg, id);
	case Halt:
		lock();
		m_groups.clear();
		unlock();
		return Driver::received(msg, id);
	default:
		;
	}
	return Driver::received(msg, id);

}

/* **************************** */
}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */

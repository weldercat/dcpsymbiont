#include "analog_line.h"


/**
 * AnalogLine
 */
const TokenDict* AnalogLine::typeNames()
{
    static const TokenDict names[] = {
	{"FXO",       FXO},
	{"FXS",       FXS},
	{"recorder",  Recorder},
	{"monitor",   Monitor},
	{0,0}
    };
    return names;
}

const TokenDict* AnalogLine::stateNames()
{
    static const TokenDict names[] = {
	{"OutOfService",   OutOfService},
	{"Idle",           Idle},
	{"Dialing",        Dialing},
	{"DialComplete",   DialComplete},
	{"Ringing",        Ringing},
	{"Answered",       Answered},
	{"CallEnded",      CallEnded},
	{"OutOfOrder",     OutOfOrder},
	{0,0}
	};
    return names;
}

const TokenDict* AnalogLine::csNames() {
    static const TokenDict names[] = {
	{"after",  After},
	{"before", Before},
	{"none",   NoCallSetup},
	{0,0}
	};
    return names;
}

inline u_int64_t getValidInt(const NamedList& params, const char* param, int defVal)
{
    int tmp = params.getIntValue(param,defVal);
    return tmp >= 0 ? tmp : defVal;
}

// Reserve the line's circuit
AnalogLine::AnalogLine(AnalogLineGroup* grp, unsigned int cic, const NamedList& params)
    : Mutex(true,"AnalogLine"),
    m_type(Unknown),
    m_state(Idle),
    m_inband(false),
    m_echocancel(0),
    m_acceptPulseDigit(true),
    m_answerOnPolarity(false),
    m_hangupOnPolarity(false),
    m_polarityControl(false),
    m_callSetup(NoCallSetup),
    m_callSetupTimeout(0),
    m_noRingTimeout(0),
    m_alarmTimeout(0),
    m_group(grp),
    m_circuit(0),
    m_private(0),
    m_peer(0),
    m_getPeerEvent(false)
{
    // Check and set some data
    const char* error = 0;
    while (true) {
#define CHECK_DATA(test,sError) if (test) { error = sError; break; }
	CHECK_DATA(!m_group,"circuit group is missing")
	CHECK_DATA(m_group->findLine(cic),"circuit already allocated")
	SignallingCircuit* circuit = m_group->find(cic);
	if (circuit && circuit->ref())
	    m_circuit = circuit;
	CHECK_DATA(!m_circuit,"circuit is missing")
	break;
#undef CHECK_DATA
    }
    if (error) {
	Debug(m_group,DebugNote,"Can't create analog line (cic=%u): %s",
	    cic,error);
	return;
    }

    m_type = m_group->type();
    if (m_type == Recorder)
	m_type = FXO;
    m_address << m_group->toString() << "/" << m_circuit->code();
    m_inband = params.getBoolValue(YSTRING("dtmfinband"),false);
    String tmp = params.getValue(YSTRING("echocancel"));
    if (tmp.isBoolean())
	m_echocancel = tmp.toBoolean() ? 1 : -1;
    m_answerOnPolarity = params.getBoolValue(YSTRING("answer-on-polarity"),false);
    m_hangupOnPolarity = params.getBoolValue(YSTRING("hangup-on-polarity"),false);
    m_polarityControl = params.getBoolValue(YSTRING("polaritycontrol"),false);

    m_callSetup = (CallSetupInfo)lookup(params.getValue(YSTRING("callsetup")),csNames(),After);

    m_callSetupTimeout = getValidInt(params,"callsetup-timeout",2000);
    m_noRingTimeout = getValidInt(params,"ring-timeout",10000);
    m_alarmTimeout = getValidInt(params,"alarm-timeout",30000);
    m_delayDial = getValidInt(params,"delaydial",2000);

    DDebug(m_group,DebugAll,"AnalogLine() addr=%s type=%s [%p]",
	address(),lookup(m_type,typeNames()),this);

    if (!params.getBoolValue(YSTRING("out-of-service"),false)) {
	resetCircuit();
	if (params.getBoolValue(YSTRING("connect"),true))
	    connect(false);
    }
    else
	enable(false,false);
}

AnalogLine::~AnalogLine()
{
    DDebug(m_group,DebugAll,"~AnalogLine() addr=%s [%p]",address(),this);
}

// Remove old peer's peer. Set this line's peer
void AnalogLine::setPeer(AnalogLine* line, bool sync)
{
    Lock mylock(this);
    if (line == this) {
	Debug(m_group,DebugNote,"%s: Attempt to set peer to itself [%p]",
		address(),this);
	return;
    }
    if (line == m_peer) {
	if (sync && m_peer) {
	    XDebug(m_group,DebugAll,"%s: Syncing with peer (%p) '%s' [%p]",
		address(),m_peer,m_peer->address(),this);
	    m_peer->setPeer(this,false);
	}
	return;
    }
    AnalogLine* tmp = m_peer;
    m_peer = 0;
    if (tmp) {
	DDebug(m_group,DebugAll,"%s: Removed peer (%p) '%s' [%p]",
	    address(),tmp,tmp->address(),this);
	if (sync)
	    tmp->setPeer(0,false);
    }
    m_peer = line;
    if (m_peer) {
	DDebug(m_group,DebugAll,"%s: Peer set to (%p) '%s' [%p]",
	    address(),m_peer,m_peer->address(),this);
	if (sync)
	    m_peer->setPeer(this,false);
    }
}

// Reset the line circuit's echo canceller to line default echo canceller state
void AnalogLine::resetEcho(bool train)
{
    if (!(m_circuit || m_echocancel))
	return;
    bool enable = (m_echocancel > 0);
    m_circuit->setParam("echocancel",String::boolText(enable));
    if (enable && train)
	m_circuit->setParam("echotrain",String(""));
}

// Connect the line's circuit. Reset line echo canceller
bool AnalogLine::connect(bool sync)
{
    Lock mylock(this);
    bool ok = m_circuit && m_circuit->connect();
    resetEcho(true);
    if (sync && ok && m_peer)
	m_peer->connect(false);
    return ok;
}

// Disconnect the line's circuit. Reset line echo canceller
bool AnalogLine::disconnect(bool sync)
{
    Lock mylock(this);
    bool ok = m_circuit && m_circuit->disconnect();
    resetEcho(false);
    if (sync && ok && m_peer)
	m_peer->disconnect(false);
    return ok;
}

// Send an event through this line
bool AnalogLine::sendEvent(SignallingCircuitEvent::Type type, NamedList* params)
{
    Lock mylock(this);
    if (state() == OutOfService)
	return false;
    if (m_inband &&
	(type == SignallingCircuitEvent::Dtmf || type == SignallingCircuitEvent::PulseDigit))
	return false;
    return m_circuit && m_circuit->sendEvent(type,params);
}

// Get events from the line's circuit if not out of service
AnalogLineEvent* AnalogLine::getEvent(const Time& when)
{
    Lock mylock(this);
    if (state() == OutOfService) {
	checkTimeouts(when);
	return 0;
    }

    SignallingCircuitEvent* event = m_circuit ? m_circuit->getEvent(when) : 0;
    if (!event) {
	checkTimeouts(when);
	return 0;
    }

    if ((event->type() == SignallingCircuitEvent::PulseDigit ||
	event->type() == SignallingCircuitEvent::PulseStart) &&
	!m_acceptPulseDigit) {
	DDebug(m_group,DebugInfo,"%s: ignoring pulse event '%s' [%p]",
	    address(),event->c_str(),this);
	delete event;
	return 0;
    }

    return new AnalogLineEvent(this,event);
}

// Alternate get events from this line or peer
AnalogLineEvent* AnalogLine::getMonitorEvent(const Time& when)
{
    Lock mylock(this);
    m_getPeerEvent = !m_getPeerEvent;
    AnalogLineEvent* event = 0;
    if (m_getPeerEvent) {
	event = getEvent(when);
	if (!event && m_peer)
	    event = m_peer->getEvent(when);
    }
    else {
	if (m_peer)
	    event = m_peer->getEvent(when);
	if (!event)
	    event = getEvent(when);
    }
    return event;
}

// Change the line state if neither current or new state are OutOfService
bool AnalogLine::changeState(State newState, bool sync)
{
    Lock mylock(this);
    bool ok = false;
    while (true) {
	if (m_state == newState || m_state == OutOfService || newState == OutOfService)
	    break;
	if (newState != Idle && newState < m_state)
	    break;
	DDebug(m_group,DebugInfo,"%s: changed state from %s to %s [%p]",
	    address(),lookup(m_state,stateNames()),
	    lookup(newState,stateNames()),this);
	m_state = newState;
	ok = true;
	break;
    }
    if (sync && ok && m_peer)
	m_peer->changeState(newState,false);
    return true;
}

// Enable/disable line. Change circuit's state to Disabled/Reserved when
//  entering/exiting the OutOfService state
bool AnalogLine::enable(bool ok, bool sync, bool connectNow)
{
    Lock mylock(this);
    while (true) {
	if (ok) {
	    if (m_state != OutOfService)
		break;
	    Debug(m_group,DebugInfo,"%s: back in service [%p]",address(),this);
	    m_state = Idle;
	    if (m_circuit) {
		m_circuit->status(SignallingCircuit::Reserved);
		if (connectNow)
		    connect(false);
	    }
	    break;
	}
	// Disable
	if (m_state == OutOfService)
	    break;
	Debug(m_group,DebugNote,"%s: out of service [%p]",address(),this);
	m_state = OutOfService;
	disconnect(false);
	if (m_circuit)
	    m_circuit->status(SignallingCircuit::Disabled);
	break;
    }
    if (sync && m_peer)
	m_peer->enable(ok,false,connectNow);
    return true;
}

// Deref the circuit
void AnalogLine::destroyed()
{
    lock();
    disconnect(false);
    if (m_circuit)
	m_circuit->status(SignallingCircuit::Idle);
    setPeer(0,true);
    if (m_group)
	m_group->removeLine(this);
    TelEngine::destruct(m_circuit);
    unlock();
    RefObject::destroyed();
}


/**
 * AnalogLineGroup
 */

// Construct an analog line group owning single lines
AnalogLineGroup::AnalogLineGroup(AnalogLine::Type type, const char* name, bool slave)
    : SignallingCircuitGroup(0,SignallingCircuitGroup::Increment,name),
    m_type(type),
    m_fxo(0),
    m_slave(false)
{
    setName(name);
    if (m_type == AnalogLine::FXO)
	m_slave = slave;
    XDebug(this,DebugAll,"AnalogLineGroup() [%p]",this);
}

// Constructs an FXS analog line monitor
AnalogLineGroup::AnalogLineGroup(const char* name, AnalogLineGroup* fxo)
    : SignallingCircuitGroup(0,SignallingCircuitGroup::Increment,name),
    m_type(AnalogLine::FXS),
    m_fxo(fxo)
{
    setName(name);
    if (m_fxo)
	m_fxo->debugChain(this);
    else
	Debug(this,DebugWarn,"Request to create monitor without fxo group [%p]",this);
    XDebug(this,DebugAll,"AnalogLineGroup() monitor fxo=%p [%p]",m_fxo,this);
}

AnalogLineGroup::~AnalogLineGroup()
{
    XDebug(this,DebugAll,"~AnalogLineGroup() [%p]",this);
}

// Append it to the list
bool AnalogLineGroup::appendLine(AnalogLine* line, bool destructOnFail)
{
    AnalogLine::Type type = m_type;
    if (type == AnalogLine::Recorder)
	type = AnalogLine::FXO;
    if (!(line && line->type() == type && line->group() == this)) {
	if (destructOnFail)
	    TelEngine::destruct(line);
	return false;
    }
    Lock mylock(this);
    m_lines.append(line);
    DDebug(this,DebugAll,"Added line (%p) %s [%p]",line,line->address(),this);
    return true;
}

// Remove a line from the list and destruct it
void AnalogLineGroup::removeLine(unsigned int cic)
{
    Lock mylock(this);
    AnalogLine* line = findLine(cic);
    if (!line)
	return;
    removeLine(line);
    TelEngine::destruct(line);
}

// Remove a line from the list without destroying it
void AnalogLineGroup::removeLine(AnalogLine* line)
{
    if (!line)
	return;
    Lock mylock(this);
    if (m_lines.remove(line,false))
	DDebug(this,DebugAll,"Removed line %p %s [%p]",line,line->address(),this);
}

// Find a line by its circuit
AnalogLine* AnalogLineGroup::findLine(unsigned int cic)
{
    Lock mylock(this);
    for (ObjList* o = m_lines.skipNull(); o; o = o->skipNext()) {
	AnalogLine* line = static_cast<AnalogLine*>(o->get());
	if (line->circuit() && line->circuit()->code() == cic)
	    return line;
    }
    return 0;
}

// Find a line by its address
AnalogLine* AnalogLineGroup::findLine(const String& address)
{
    Lock mylock(this);
    ObjList* tmp = m_lines.find(address);
    return tmp ? static_cast<AnalogLine*>(tmp->get()) : 0;
}

// Iterate through the line list to get an event
AnalogLineEvent* AnalogLineGroup::getEvent(const Time& when)
{
    lock();
    ListIterator iter(m_lines);
    for (;;) {
	AnalogLine* line = static_cast<AnalogLine*>(iter.get());
	// End of iteration?
	if (!line)
	    break;
	RefPointer<AnalogLine> lineRef = line;
	// Dead pointer?
	if (!lineRef)
	    continue;
	unlock();
	AnalogLineEvent* event = !fxo() ? lineRef->getEvent(when) : lineRef->getMonitorEvent(when);
	if (event)
	    return event;
	lock();
    }
    unlock();
    return 0;
}

// Remove all spans and circuits. Release object
void AnalogLineGroup::destroyed()
{
    lock();
    for (ObjList* o = m_lines.skipNull(); o; o = o->skipNext()) {
	AnalogLine* line = static_cast<AnalogLine*>(o->get());
	Lock lock(line);
	line->m_group = 0;
    }
    m_lines.clear();
    TelEngine::destruct(m_fxo);
    unlock();
    SignallingCircuitGroup::destroyed();
}


/**
 *
 * imt_line.cpp
 * Copyright (C) 2017 Stacy
 * Based on AnalogLine implementation from sigcall.cpp
 * from YATE project which is  Copyright (C) 2004-2014 Null Team
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

#define FIX_ZAPCARD_TONES		1

#include <yatengine.h>
#include <yatephone.h>
#include <yatesig.h>
#ifdef FIX_ZAPCARD_TONES
#include <dahdi/user.h>
#include <dahdi/tonezone.h>
#endif

#include "imt_line.h"

static void handle_event(ImtLine *line, SignallingCircuitEvent *evt);
static char tone2ascii(int tone);
/**
 * ImtLine
 */

const TokenDict* ImtLine::stateNames()
{
    static const TokenDict names[] = {
	{"OutOfService",   OutOfService},
	{"Idle",           Idle},
	{"Seized",         Seized},
	{"Talking",        Talking},
	{"OutOfOrder",     OutOfOrder},
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
ImtLine::ImtLine(ImtGroup* grp, unsigned int cic, const NamedList& params)
    : Mutex(true,"ImtLine"),
    chanptr(NULL),
    m_state(Idle),
    m_dtmfdetect(false),
    m_echocancel(0),
    m_group(grp),
    m_circuit(0)
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
	Debug(m_group,DebugNote,"Can't create IMT line (cic=%u): %s",
	    cic,error);
	return;
    }

    m_address << m_group->toString() << "/" << m_circuit->code();
    m_dtmfdetect = params.getBoolValue(YSTRING("dtmfdetect"),false);
    String tmp = params.getValue(YSTRING("echocancel"));
    if (tmp.isBoolean())
	m_echocancel = tmp.toBoolean() ? 1 : -1;

    DDebug(m_group,DebugAll,"ImtLine() addr=%s [%p]",
	address(),this);

    if (!params.getBoolValue(YSTRING("out-of-service"),false)) {
	resetCircuit();
    }
    else
	enable(false);
}

ImtLine::~ImtLine()
{
    DDebug(m_group,DebugAll,"~ImtLine() addr=%s [%p]",address(),this);
}


// Reset the line circuit's echo canceller to line default echo canceller state
void ImtLine::resetEcho(bool train)
{
    if (!(m_circuit || m_echocancel))
	return;
    bool enable = (m_echocancel > 0);
    m_circuit->setParam("echocancel",String::boolText(enable));
    if (enable && train)
	m_circuit->setParam("echotrain",String(""));
}

// Connect the line's circuit. Reset line echo canceller
bool ImtLine::connect(bool sync)
{
    Lock mylock(this);
    bool ok = m_circuit && m_circuit->connect();
    if (ok) resetEcho(true);
    return ok;
}

// Disconnect the line's circuit. Reset line echo canceller
bool ImtLine::disconnect(bool sync)
{
    Lock mylock(this);
    bool ok = m_circuit && m_circuit->disconnect();
    resetEcho(false);
    return ok;
}


// Change the line state if neither current or new state are OutOfService
bool ImtLine::changeState(State newState)
{
    Lock mylock(this);
    while (true) {
	if (m_state == newState || m_state == OutOfService || newState == OutOfService)
	    break;
	if (newState != Idle && newState < m_state)
	    break;
	DDebug(m_group,DebugInfo,"%s: changed state from %s to %s [%p]",
	    address(),lookup(m_state,stateNames()),
	    lookup(newState,stateNames()),this);
	m_state = newState;
	break;
    }
    return true;
}

// Enable/disable line. Change circuit's state to Disabled/Reserved when
//  entering/exiting the OutOfService state
bool ImtLine::enable(bool ok)
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
    return true;
}

// Deref the circuit
void ImtLine::destroyed()
{
    lock();
    disconnect(false);
    if (m_circuit) m_circuit->status(SignallingCircuit::Idle);
    if (m_group) m_group->removeLine(this);
    TelEngine::destruct(m_circuit);
    unlock();
    RefObject::destroyed();
}


/**
 * ImtGroup
 */

// Construct an Imt line group owning single lines
ImtGroup::ImtGroup(const char* name)
    : SignallingCircuitGroup(0,SignallingCircuitGroup::Increment,name),
    m_init(false),
    m_thread(NULL)
{
    setName(name);
    
    XDebug(this,DebugAll,"ImtGroup() [%p]",this);
}

ImtGroup::~ImtGroup()
{
    XDebug(this,DebugAll,"~ImtGroup() [%p]",this);
}

// Append it to the list
bool ImtGroup::appendLine(ImtLine* line, bool destructOnFail)
{
    if (!(line && line->group() == this)) {
	if (destructOnFail)
	    TelEngine::destruct(line);
	return false;
    }
    Lock mylock(this);
    m_lines.append(line);
    DDebug(this, DebugNote,"Added line (%p) %s [%p]",line,line->address(),this);
    return true;
}

// Remove a line from the list and destruct it
void ImtGroup::removeLine(unsigned int cic)
{
    Lock mylock(this);
    ImtLine* line = findLine(cic);
    if (!line)
	return;
    removeLine(line);
    TelEngine::destruct(line);
}

// Remove a line from the list without destroying it
void ImtGroup::removeLine(ImtLine* line)
{
    if (!line)
	return;
    Lock mylock(this);
    if (m_lines.remove(line,false))
	DDebug(this, DebugNote, "Removed line %p %s [%p]",line,line->address(),this);
}

// Find a line by its circuit
ImtLine* ImtGroup::findLine(unsigned int cic)
{
    Lock mylock(this);
    for (ObjList* o = m_lines.skipNull(); o; o = o->skipNext()) {
	ImtLine* line = static_cast<ImtLine*>(o->get());
//	XDebug(this, DebugNote, "Looking for line %u, found %u", cic, line->circuit()->code());
	if (line->circuit() && line->circuit()->code() == cic)
	    return line;
    }
    return 0;
}

// Find a line by its address
ImtLine* ImtGroup::findLine(const String& address)
{
    Lock mylock(this);
    ObjList* tmp = m_lines.find(address);
    return tmp ? static_cast<ImtLine*>(tmp->get()) : 0;
}

// Remove all spans and circuits. Release object
void ImtGroup::destroyed()
{
	lock();
	for (ObjList* o = m_lines.skipNull(); o; o = o->skipNext()) {
		ImtLine* line = static_cast<ImtLine*>(o->get());
		Lock lock(line);
		line->m_group = 0;
	}
	m_lines.clear();
	if (m_thread) {
    		XDebug(this, DebugInfo, "Terminating worker thread [%p]", this);
		m_thread->cancel(false);
		while (m_thread) Thread::yield(true);
		Debug(this, DebugInfo, "Worker thread terminated [%p]", this);
	}
    unlock();
    SignallingCircuitGroup::destroyed();
}

ImtThread::ImtThread(ImtGroup *group)
	: Thread("IMT worker"),
	m_client(group),
	m_groupname(group ? group->debugName() : "")
{
}

ImtThread::~ImtThread()
{
	DDebug(DebugAll, "ImtThread(%p, '%s') terminated [%p]", 
	       m_client, m_groupname.c_str(), this);
	if (m_client) m_client->m_thread = NULL;
}


#ifdef FIX_ZAPCARD_TONES
static char tone2ascii(int tone)
{
	static const char tonetab[17] = "0123456789*#ABCD";
	
	tone &= ~(DAHDI_EVENT_DTMFUP | DAHDI_EVENT_DTMFDOWN);
	tone -= DAHDI_TONE_DTMF_BASE;
	if ((tone < 0) || (tone > 15)) return ' ';
	return tonetab[tone];
}
#else 
static char tone2ascii(int tone)
{
	return (tone & 0xff);
}

#endif


static void handle_event(ImtLine *line, SignallingCircuitEvent *evt)
{
	if (!line) return;
	if (!evt) return;
	
	switch(evt->type()) {
		case SignallingCircuitEvent::Dtmf :
			{	
				const char	*tone = NULL;
				char c = '\000';
				
				Message *m = new Message("chan.dtmf");
				m->addParam("address", line->address());
				tone = evt->getValue("tone");
				c = tone2ascii(tone[0]);
				
				if (c) m->addParam("text", String(c));
				m->addParam("detected", "inband");
				Engine::enqueue(m);
			}
			break;
		default:
			break;
	}
}

void ImtThread::run()
{
	SignallingCircuitEvent	*evt = NULL;
	SignallingCircuit *cic;
	ImtLine		*line;
	int	n, i;

	Debug(DebugAll, "ImtThread(%p, '%s') started [%p]", 
	      m_client, m_groupname.c_str(), this);
	if (!m_client) 	return;
	
	for (;;) {
		Time t = Time();
		m_client->lock();
		n = m_client->circuits().length();
		for (i = 0; i < n; i++) {
			cic = static_cast<SignallingCircuit *>(m_client->circuits()[i]);
			if (!cic) continue;
			line = static_cast<ImtLine *>(m_client->findLine(cic->code()));
			if (!line) continue;
			if (line->state() == ImtLine::OutOfService) continue;
			if (line->state() == ImtLine::OutOfOrder) continue;
//			if (line->state() == ImtLine::Idle) continue;
			evt = cic->getEvent(t);
			if (evt) {
				m_client->unlock();
				handle_event(line, evt);
				TelEngine::destruct(evt);
				evt = NULL;
				m_client->lock();
			}
		}
		m_client->unlock();
		if (!evt) {
//			m_client->checkTimers(t);
			Thread::idle(true);
			continue;
		}
		if (Thread::check(true))
			break;
	}
}

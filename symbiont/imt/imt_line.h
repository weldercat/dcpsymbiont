#ifndef IMT_LINE_HDR_LOADED__
#define IMT_LINE_HDR_LOADED__

/**
 *
 * imt_line.h
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



//#include <yateclass.h>
//#include <yatecbase.h>
#include <yatengine.h>
#include <yatephone.h>
#include <yatesig.h>


// IMT lines - modelled after AnalogLine 
// but many unneded functionality is thrown away
using namespace TelEngine;

class ImtLine;				// IMT trunk line
class ImtGroup;				// A group of IMT trunk lines
class ImtThread;			// Event poller


/**
 * This class is used to manage an IMT line and keep data associated with it.
 * @short An IMT line
 */
class ImtLine : public RefObject, public Mutex
{
    YCLASS(ImtLine, RefObject)
    friend class ImtGroup;        // Reset group if destroyed before the line
public:

    /**
     * Line state enumeration
     */
    enum State {
	OutOfService   = -1,             // Line is out of service
	Idle           = 0,              // Line is idle (on hook)
	Seized         = 1,              // Line is seized for dialing or for incoming call
	Talking        = 2,              // Call is answered
	OutOfOrder     = 3,              // Disabled due to error
    };

    /**
     * Constructor. Reserve the line's circuit. Connect it if requested. Creation will fail if no group,
     *  circuit, caller or the circuit is already allocated for another line in the group
     * @param grp The group owning this IMT line
     * @param cic The code of the signalling circuit used this line
     * @param params The line's parameters
     */
    ImtLine(ImtGroup* grp, unsigned int cic, const NamedList& params);

    /**
     * Destructor
     */
    virtual ~ImtLine();

    /**
     * Get the line state
     * @return The line state as enumeration
     */
    inline State state() const
	{ return m_state; }

    /**
     * Get the group owning this line
     * @return The group owning this line
     */
    inline ImtGroup* group()
	{ return m_group; }


    /**
     * Get the line's circuit
     * @return SignallingCircuit pointer or 0 if no circuit was attached to this line
     */
    inline SignallingCircuit* circuit()
	{ return m_circuit; }

    /**
     * Get the line address: group_name/circuit_number
     * @return The line address
     */
    inline const char* address() const
	{ return m_address; }

    inline bool dtmfdetect_on() const
	{ return m_dtmfdetect; }

    /**
     * Get this line's address
     * @return This line's address
     */
    virtual const String& toString() const
	{ return m_address; }

    /**
     * Reset the line circuit's echo canceller to line default echo canceller state
     * @param train Start echo canceller training if enabled
     */
    void resetEcho(bool train);


    /**
     * Reset the line's circuit (change its state to Reserved)
     * @return True if the line's circuit state was changed to Reserved
     */
    inline bool resetCircuit()
	{ return state() != OutOfService && m_circuit && m_circuit->reserve(); }

    /**
     * Set a parameter of this line's circuit
     * @param param Parameter name
     * @param value Optional parameter value
     * @return True if the line's circuit parameter was set
     */
    inline bool setCircuitParam(const char* param, const char* value = 0)
	{ return m_circuit && m_circuit->setParam(param,value); }

    /**
     * Connect the line's circuit. Reset line echo canceller
     * @param sync True to synchronize (connect) the peer
     * @return True if the line's circuit state was changed to Connected
     */
    bool connect(bool sync);

    /**
     * Disconnect the line's circuit. Reset line echo canceller
     * @param sync True to synchronize (disconnect) the peer
     * @return True if the line's circuit was disconnected (changed state from Connected to Reserved)
     */
    bool disconnect(bool sync);

    /**
     * Change the line state if neither current or new state are OutOfService
     * @param newState The new state of the line
     * @return True if line state changed
     */
    bool changeState(State newState);

    /**
     * Enable/disable line. Change circuit's state to Disabled/Reserved when
     *  entering/exiting the OutOfService state
     * @param ok Enable (change state to Idle) or disable (change state to OutOfService) the line
     * @return True if line state changed
     */
    bool enable(bool ok);

    /**
     * Line state names dictionary
     */
    static const TokenDict* stateNames();

    void	*chanptr;

protected:
    /**
     * Deref the circuit. Remove itself from group
     */
    virtual void destroyed();

private:
    State m_state;                             // Line state
    bool m_dtmfdetect;
    int m_echocancel;                          // Default echo canceller state (0: managed by the circuit, -1: off, 1: on)
    ImtGroup* m_group;                  // The group owning this line
    SignallingCircuit* m_circuit;              // The circuit managed by this line
    String m_address;                          // Line address: group and circuit
};


/**
 * This class is an IMT line container.
 * @short A group of IMT lines
 */
class ImtGroup : public SignallingCircuitGroup
{
    YCLASS(ImtGroup, SignallingCircuitGroup)
public:
    /**
     * Constructor. Construct an IMT line group owning single lines
     * @param name Name of this component
     */
    ImtGroup(const char* name);

    /**
     * Destructor
     */
    virtual ~ImtGroup();


    /**
     * Get the IMT lines belonging to this group
     * @return The group's lines list
     */
    inline ObjList& lines()
	{ return m_lines; }


    /**
     * Append a line to this group. Line must have the same type as this group and must be owned by this group
     * @param line The line to append
     * @param destructOnFail Destroy line if failed to append. Defaults to true
     * @return True on success
     */
    bool appendLine(ImtLine* line, bool destructOnFail = true);

    /**
     * Remove a line from the list and destruct it
     * @param cic The signalling circuit's code used by the line
     */
    void removeLine(unsigned int cic);

    /**
     * Remove a line from the list without destroying it
     * @param line The line to be removed
     */
    void removeLine(ImtLine* line);

    /**
     * Find a line by its circuit
     * @param cic The signalling circuit's code used by the line
     * @return ImtLine pointer or 0 if not found
     */
    ImtLine* findLine(unsigned int cic);

    /**
     * Find a line by its address
     * @param address The address of the line
     * @return ImtLine pointer or 0 if not found
     */
    ImtLine* findLine(const String& address);

    bool m_init;

    ImtThread	*m_thread;
    
protected:
    /**
     * Remove all lines. Release object
     */
    virtual void destroyed();

    /**
     * The IMT lines belonging to this group
     */
    ObjList m_lines;

   

private:
};

class ImtThread : public Thread
{
public:
	ImtThread(ImtGroup *group);
	virtual ~ImtThread();
	virtual void run(void);
	ImtGroup	*m_client;
	String		m_groupname;
};

#endif /* IMT_LINE_HDR_LOADED__ */


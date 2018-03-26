// Analog lines
class AnalogLine;                        // An analog line
class AnalogLineEvent;                   // A single analog line related event
class AnalogLineGroup;                   // A group of analog lines



/**
 * This class is used to manage an analog line and keep data associated with it.
 * Also it can be used to monitor a pair of FXS/FXO analog lines
 * @short An analog line
 */
class YSIG_API AnalogLine : public RefObject, public Mutex
{
    YCLASS(AnalogLine,RefObject)
    friend class AnalogLineGroup;        // Reset group if destroyed before the line
public:
    /**
     * Line type enumerator
     */
    enum Type {
	FXO,                             // Telephone linked to an exchange
	FXS,                             // Telephone exchange linked to a telephone
	Recorder,                        // Passive FXO recording a 2 wire line
	Monitor,                         // Monitor (a pair of FXS/FXO lines)
	Unknown
    };

    /**
     * Line state enumeration
     */
    enum State {
	OutOfService   = -1,             // Line is out of service
	Idle           = 0,              // Line is idle (on hook)
	Dialing        = 1,              // FXS line is waiting for the FXO to dial the number
	DialComplete   = 2,              // FXS line: got enough digits from the FXO to reach a destination
	Ringing        = 3,              // Line is ringing
	Answered       = 4,              // Line is answered
	CallEnded      = 5,              // FXS line: notify the FXO on call termination
	OutOfOrder     = 6,              // FXS line: notify the FXO that the hook is off after call ended notification
    };

    /**
     * Call setup (such as Caller ID) management (send and detect)
     */
    enum CallSetupInfo {
	After,                           // Send/detect call setup after the first ring
	Before,                          // Send/detect call setup before the first ring
	NoCallSetup                      // No call setup detect or send
    };

    /**
     * Constructor. Reserve the line's circuit. Connect it if requested. Creation will fail if no group,
     *  circuit, caller or the circuit is already allocated for another line in the group
     * @param grp The group owning this analog line
     * @param cic The code of the signalling circuit used this line
     * @param params The line's parameters
     */
    AnalogLine(AnalogLineGroup* grp, unsigned int cic, const NamedList& params);

    /**
     * Destructor
     */
    virtual ~AnalogLine();

    /**
     * Get this line's type
     * @return The line type as enumeration
     */
    inline Type type() const
	{ return m_type; }

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
    inline AnalogLineGroup* group()
	{ return m_group; }

    /**
     * Get this line's peer if belongs to a pair of monitored lines
     * @return This line's peer if belongs to a pair of monitored lines
     */
    inline AnalogLine* getPeer()
	{ return m_peer; }

    /**
     * Remove old peer's peer. Set this line's peer
     * @param line This line's peer
     * @param sync True to synchronize (set/reset) with the old peer
     */
    void setPeer(AnalogLine* line = 0, bool sync = true);

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

    /**
     * Check if allowed to send outband DTMFs (DTMF events)
     * @return True if allowed to send outband DTMFs
     */
    inline bool outbandDtmf() const
	{ return !m_inband; }

    /**
     * Check if the line should be answered on polarity change
     * @return True if the line should be answered on polarity change
     */
    inline bool answerOnPolarity() const
	{ return m_answerOnPolarity; }

    /**
     * Check if the line should be hanged up on polarity change
     * @return True if the line should be hanged up on polarity change
     */
    inline bool hangupOnPolarity() const
	{ return m_hangupOnPolarity; }

    /**
     * Check if the line polarity change should be used
     * @return True if the line polarity change should be used
     */
    inline bool polarityControl() const
	{ return m_polarityControl; }

    /**
     * Check if the line is processing (send/receive) the setup info (such as caller id) and when it does it
     * @return Call setup info processing as enumeration
     */
    inline CallSetupInfo callSetup() const
	{ return m_callSetup; }

    /**
     * Get the time allowed to ellapse between the call setup data and the first ring
     * @return The time allowed to ellapse between the call setup data and the first ring
     */
    inline u_int64_t callSetupTimeout() const
	{ return m_callSetupTimeout; }

    /**
     * Get the time allowed to ellapse without receiving a ring on incoming calls
     * @return The time allowed to ellapse without receiving a ring on incoming calls
     */
    inline u_int64_t noRingTimeout() const
	{ return m_noRingTimeout; }

    /**
     * Get the time allowed to stay in alarm. This option can be used by the clients to terminate an active call
     * @return The time allowed to stay in alarm
     */
    inline u_int64_t alarmTimeout() const
	{ return m_alarmTimeout; }

    /**
     * Get the time delay of dialing the called number
     * @return The time delay of dialing the called number
     */
    inline u_int64_t delayDial() const
	{ return m_delayDial; }

    /**
     * Set/reset accept pulse digits flag
     * @param ok True to accept incoming pulse digits, false to ignore them
     */
    inline void acceptPulseDigit(bool ok)
	{ m_acceptPulseDigit = ok; }

    /**
     * Get the private user data of this line
     * @return The private user data of this line
     */
    inline void* userdata() const
	{ return m_private; }

    /**
     * Set the private user data of this line and its peer if any
     * @param data The new private user data value of this line
     * @param sync True to synchronize (set data) with the peer
     */
    inline void userdata(void* data, bool sync = true)
    {
	Lock lock(this);
	m_private = data;
	if (sync && m_peer)
	    m_peer->userdata(data,false);
    }

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
     * Send an event through this line if not out of service
     * @param type The type of the event to send
     * @param params Optional event parameters
     * @return True on success
     */
    bool sendEvent(SignallingCircuitEvent::Type type, NamedList* params = 0);

    /**
     * Send an event through this line if not out of service and change its state on success
     * @param type The type of the event to send
     * @param newState The new state of the line if the event was sent
     * @param params Optional event parameters
     * @return True on success
     */
    inline bool sendEvent(SignallingCircuitEvent::Type type, State newState,
	NamedList* params = 0)
    {
	if (!sendEvent(type,params))
	    return false;
	changeState(newState,false);
	return true;
    }

    /**
     * Get events from the line's circuit if not out of service. Check timeouts
     * @param when The current time
     * @return AnalogLineEvent pointer or 0 if no events
     */
    virtual AnalogLineEvent* getEvent(const Time& when);

    /**
     * Alternate get events from this line or peer
     * @param when The current time
     * @return AnalogLineEvent pointer or 0 if no events
     */
    virtual AnalogLineEvent* getMonitorEvent(const Time& when);

    /**
     * Check timeouts if the line is not out of service and no event was generated by the circuit
     * @param when Time to use as computing base for timeouts
     */
    virtual void checkTimeouts(const Time& when)
	{ }

    /**
     * Change the line state if neither current or new state are OutOfService
     * @param newState The new state of the line
     * @param sync True to synchronize (change state) the peer
     * @return True if line state changed
     */
    bool changeState(State newState, bool sync = false);

    /**
     * Enable/disable line. Change circuit's state to Disabled/Reserved when
     *  entering/exiting the OutOfService state
     * @param ok Enable (change state to Idle) or disable (change state to OutOfService) the line
     * @param sync True to synchronize (enable/disable) the peer
     * @param connectNow Connect the line if enabled. Ignored if the line will be disabled
     * @return True if line state changed
     */
    bool enable(bool ok, bool sync, bool connectNow = true);

    /**
     * Line type names dictionary
     */
    static const TokenDict* typeNames();

    /**
     * Line state names dictionary
     */
    static const TokenDict* stateNames();

    /**
     * Call setup info names
     */
    static const TokenDict* csNames();

protected:
    /**
     * Deref the circuit. Remove itself from group
     */
    virtual void destroyed();

private:
    Type m_type;                               // Line type
    State m_state;                             // Line state
    bool m_inband;                             // Refuse to send DTMFs if they should be sent in band
    int m_echocancel;                          // Default echo canceller state (0: managed by the circuit, -1: off, 1: on)
    bool m_acceptPulseDigit;                   // Accept incoming pulse digits
    bool m_answerOnPolarity;                   // Answer on line polarity change
    bool m_hangupOnPolarity;                   // Hangup on line polarity change
    bool m_polarityControl;                    // Set line polarity flag
    CallSetupInfo m_callSetup;                 // Call setup management
    u_int64_t m_callSetupTimeout;              // FXO: timeout period for received call setup data before first ring
    u_int64_t m_noRingTimeout;                 // FXO: timeout period with no ring received on incoming calls
    u_int64_t m_alarmTimeout;                  // Timeout period to stay in alarms
    u_int64_t m_delayDial;                     // FXO: Time to delay sending number
    AnalogLineGroup* m_group;                  // The group owning this line
    SignallingCircuit* m_circuit;              // The circuit managed by this line
    String m_address;                          // Line address: group and circuit
    void* m_private;                           // Private data used by this line's user
    // Monitor data
    AnalogLine* m_peer;                        // This line's peer if any
    bool m_getPeerEvent;                       // Flag used to get events from peer
};

/**
 * An object holding an event generated by an analog line and related references
 * @short A single analog line related event
 */
class YSIG_API AnalogLineEvent : public GenObject
{
public:
    /**
     * Constructor
     * @param line The analog line that generated this event
     * @param event The signalling circuit event
     */
    AnalogLineEvent(AnalogLine* line, SignallingCircuitEvent* event)
	: m_line(0), m_event(event)
	{ if (line && line->ref()) m_line = line; }

    /**
     * Destructor, dereferences any resources
     */
    virtual ~AnalogLineEvent()
    {
	TelEngine::destruct(m_line);
	TelEngine::destruct(m_event);
    }

    /**
     * Get the analog line that generated this event
     * @return The analog line that generated this event
     */
    inline AnalogLine* line()
	{ return m_line; }

    /**
     * Get the signalling circuit event carried by this analog line event
     * @return The signalling circuit event carried by this analog line event
     */
    inline SignallingCircuitEvent* event()
	{ return m_event; }

    /**
     * Disposes the memory
     */
    virtual void destruct()
    {
	TelEngine::destruct(m_line);
	TelEngine::destruct(m_event);
	GenObject::destruct();
    }

private:
    AnalogLine* m_line;
    SignallingCircuitEvent* m_event;
};

/**
 * This class is an analog line container.
 * It may contain another group when used to monitor analog lines
 * @short A group of analog lines
 */
class YSIG_API AnalogLineGroup : public SignallingCircuitGroup
{
    YCLASS(AnalogLineGroup,SignallingCircuitGroup)
public:
    /**
     * Constructor. Construct an analog line group owning single lines
     * @param type Line type as enumeration
     * @param name Name of this component
     * @param slave True if this is an FXO group owned by an FXS one. Ignored if type is not FXO
     */
    AnalogLineGroup(AnalogLine::Type type, const char* name, bool slave = false);

    /**
     * Constructor. Construct an FXS analog line group owning another group of FXO analog lines.
     * The fxo group is owned by this component and will be destructed if invalid (not FXO type)
     * @param name Name of this component
     * @param fxo The FXO group
     */
    AnalogLineGroup(const char* name, AnalogLineGroup* fxo);

    /**
     * Destructor
     */
    virtual ~AnalogLineGroup();

    /**
     * Get this group's type
     * @return The group's type
     */
    inline AnalogLine::Type type() const
	{ return m_type; }

    /**
     * Get the analog lines belonging to this group
     * @return The group's lines list
     */
    inline ObjList& lines()
	{ return m_lines; }

    /**
     * Get the group holding the FXO lines if present
     * @return The group holding the FXO lines or 0
     */
    inline AnalogLineGroup* fxo()
	{ return m_fxo; }

    /**
     * Check if this is an FXO group owned by an FXS one
     * @return True if this is an FXO group owned by an FXS one
     */
    inline bool slave()
	{ return m_slave; }

    /**
     * Append a line to this group. Line must have the same type as this group and must be owned by this group
     * @param line The line to append
     * @param destructOnFail Destroy line if failed to append. Defaults to true
     * @return True on success
     */
    bool appendLine(AnalogLine* line, bool destructOnFail = true);

    /**
     * Remove a line from the list and destruct it
     * @param cic The signalling circuit's code used by the line
     */
    void removeLine(unsigned int cic);

    /**
     * Remove a line from the list without destroying it
     * @param line The line to be removed
     */
    void removeLine(AnalogLine* line);

    /**
     * Find a line by its circuit
     * @param cic The signalling circuit's code used by the line
     * @return AnalogLine pointer or 0 if not found
     */
    AnalogLine* findLine(unsigned int cic);

    /**
     * Find a line by its address
     * @param address The address of the line
     * @return AnalogLine pointer or 0 if not found
     */
    AnalogLine* findLine(const String& address);

    /**
     * Iterate through the line list to get an event
     * @param when The current time
     * @return AnalogLineEvent pointer or 0 if no events
     */
    virtual AnalogLineEvent* getEvent(const Time& when);

protected:
    /**
     * Remove all lines. Release object
     */
    virtual void destroyed();

    /**
     * The analog lines belonging to this group
     */
    ObjList m_lines;

private:
    AnalogLine::Type m_type;             // Line type
    AnalogLineGroup* m_fxo;              // The group containing the FXO lines if this is a monitor
    bool m_slave;                        // True if this is an FXO group owned by an FXS one
};


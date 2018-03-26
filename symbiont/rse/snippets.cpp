
// Initialize layer, attach interface if not managed
bool ISDNQ921::initialize(const NamedList* config)
{
#ifdef DEBUG
    String tmp;
    if (config && debugAt(DebugAll))
	config->dump(tmp,"\r\n  ",'\'',true);
    Debug(this,DebugInfo,"ISDNQ921::initialize(%p) [%p]%s",config,this,tmp.c_str());
#endif
    if (config) {
	debugLevel(config->getIntValue(YSTRING("debuglevel_q921"),
	    config->getIntValue(YSTRING("debuglevel"),-1)));
	setDebug(config->getBoolValue(YSTRING("print-frames"),false),
	    config->getBoolValue(YSTRING("extended-debug"),false));
    }
    if (config && !m_management && !iface()) {
	NamedList params("");
	if (resolveConfig(YSTRING("sig"),params,config) ||
		resolveConfig(YSTRING("basename"),params,config)) {
	    params.addParam("basename",params);
	    params.assign(params + "/D");
		// copy config to params and set rxunderrun to 2500 if < 0 or > 2500
	    fixParams(params,config);
	    SignallingInterface* ifc = YSIGCREATE(SignallingInterface,&params);
	    if (!ifc)
		return false;
	    SignallingReceiver::attach(ifc);
	    if (ifc->initialize(&params)) {
		SignallingReceiver::control(SignallingInterface::Enable);
		multipleFrame(0,true,false);
	    }
	    else
		TelEngine::destruct(SignallingReceiver::attach(0));
	}
    }
    return m_management || iface();
}

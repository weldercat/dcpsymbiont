This is the first early alpha release of dcpsymbiont.

It allows you to connect AT&T/Lucent/Avaya DCP phones
(like the 8410D or 8434DX) to the unix machine and
control these phones from yate telephony  engine
(i.e. place and receive calls).

Well - sort of. There are bugs, unimplemented features etc. etc.

This software is not of a production quality and documentation is almost
        completely lacking.
Also, the hardware configuration for connecting DCP
phones is non-trivial and probably will require you to buy
expensive S/T-to-Up0 converter boxes or tinker with soldering iron.


There are however some pieces that may be useful besides the
main purpose of the dcpsymbiont. For example
yxtlink.c - the C library that abstracts connection to the Yate
external module interface.


What is included:

Documentation/
        dcp - some info on reverse-engineered
                DCP protocol.
        hw - some info on Up0 SBC controller used in
                early vintages of 8410D phones and
                info on cologne chip - popular ISDN controller.

dahdi-linux-complete - dahdi drivers release that includes
        patched wcb4xxp driver for Junghanns HFC-4S card.
        It allows you to connect external SBC via the HFC-4S's PCM
        bus and turn a DCP phone to the Up0 LT device.

dcptool - ncurses-based interactive tool for querying and
        exploring the DCP device.

scripts - some scripts that set wcb4xxp module params to use
        an external SBC with it.


yate-svn        - patched version of the Yate-5 telephony engine
                The patches are required to enable U-law encoding
                in BRI links and other things that are nessessary
                to connect DCP phones.


symbiont/       - the dcpsymbiont and related stuff:

        cctool  - call control tool - an incomlete debug tool with
                  ncurses interface and some call-control functionality
                  that later was re-implemented in symbiont.
                  Was used for initial debug.

        dcpmuxtool      - a debug tool used to send and receive DCP
                        commands via HUA link to/from DCP phones connected to
                        the yate.

        imt             -special signalless trunk module for yate
                        that provides yate B-channels endpoints for DCP devices
                        on behalf of dcpsymbiont.
                        Dirty, buggy and incomplete C++ code.
                        (Sorry, I'm not good at C++)

        include         -dcpsymbiont header files and links to dcpsymbiont
                        header files.

        lib             -libsymbiont.a
                         This dir also contains some examples of
                         how to use it. Consider looking at overlapped.c -
                         a C implementaion of overlapped dialer functionality
                         similar to yate-svn/share/scripts/overlapped.php

        rse             - remote signalling element module for yate.
                        This module multiplexes several D-chan HDLC streams
                        from DAHDI BRI links configured in yate
                        and transports them to/from the dcpsymbiont via
                        SCTP sigtran IUA-like protocol. (Namely HUA - Hdlc User Adaptation).
                        Dirty, buggy and incomplete C++ code.


        symbiont        - main dcpsymbiont dir. Go there and run make.
                        There is also the "config" subdir that contains
                        an example of dcpsymbiont config.

        symtool         - a ncurses tool to send and receive yate extmodule
                        messages.

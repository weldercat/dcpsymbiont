CFLAGS= -g -O2 -Wall -Wextra -Wno-sign-compare \
  -Wno-unused-parameter -Wmissing-declarations -Wredundant-decls \
  -Wstrict-prototypes -I ../include -I ../symbiont/call_control/classify
  



DCPOBJDIR = ../symbiont/dcpmux
MMIOBJDIR = ../symbiont/mmi_transcode

DCPOBJS= $(DCPOBJDIR)/dcphdlc.o $(DCPOBJDIR)/dcpmux.o
MMIOBJS = $(MMIOBJDIR)/mmi_misc.o $(MMIOBJDIR)/transcode.o \
   $(MMIOBJDIR)/mmi_print.o

CTLOBJDIR = ../symbiont/call_control
CLSOBJDIR = $(CTLOBJDIR)/classify
CLSLIB = $(CLSOBJDIR)/libclassify.a


LDFLAGS= -L/usr/local/lib -L/usr/lib -L../lib -L$(CLSOBJDIR) \
    -lncursesw -lreadline -lsymbiont 

CFDBDIR = ../symbiont/newcfdb
CFDBOBJS = $(CFDBDIR)/cfdb.o $(CFDBDIR)/cfload.o $(CFDBDIR)/filter.o


CTLOBJS = $(CTLOBJDIR)/call_control.o $(CTLOBJDIR)/cctl_misc.o \
   $(CTLOBJDIR)/global_lookup.o $(CTLOBJDIR)/station_control.o 


OBJECTS = cctool.o ccmain.o console.o commands.o 



all:	cctool cfload_test

cctool:	$(OBJECTS) $(DCPOBJS) $(MMIOBJS) $(CTLOBJS) $(CLSLIB) $(CFDBOBJS)
	gcc -o cctool $(LDFLAGS) $(OBJECTS) $(DCPOBJS) $(MMIOBJS) $(CTLOBJS) $(CFDBOBJS) \
	-lsymbiont -lpthread -lsctp -lrt -luuid -lclassify -lconfuse


cfload_test: cfload_test.o $(DCPOBJS) $(DCPOBJS) $(MMIOBJS) $(CTLOBJS) $(CLSLIB) $(CFDBOBJS)
	gcc -o cfload_test $(LDFLAGS) cfload_test.o $(DCPOBJS) $(CFDBOBJS) $(MMIOBJS)  $(CTLOBJS) \
	-lsymbiont -lpthread -lsctp -lrt -luuid -lclassify -lconfuse



$(CTLOBJS):
	$(MAKE) -C $(CTLOBJDIR) all

$(DCPOBJS):
	$(MAKE) -C $(DCPOBJDIR) all
	
$(MMIOBJS):
	$(MAKE) -C $(MMIOBJDIR) all

$(CLSLIB):
	$(MAKE) -C $(CLSOBJDIR) libclassify.a

$(CFDBOBJS):
	$(MAKE) -C $(CFDBDIR) all

clean:
	rm -f *.o
	rm -f cctool
	rm -f cfload_test

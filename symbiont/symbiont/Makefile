CFLAGS= -g -O2 -Wall -Wextra -Wno-sign-compare \
  -Wno-unused-parameter -Wmissing-declarations -Wredundant-decls \
  -Wstrict-prototypes -I ../include -I ./call_control/classify
  


DCPOBJDIR = ./dcpmux
MMIOBJDIR = ./mmi_transcode

DCPOBJS= $(DCPOBJDIR)/dcphdlc.o $(DCPOBJDIR)/dcpmux.o
MMIOBJS = $(MMIOBJDIR)/mmi_misc.o $(MMIOBJDIR)/transcode.o \
   $(MMIOBJDIR)/mmi_print.o

CTLOBJDIR = ./call_control
CLSOBJDIR = $(CTLOBJDIR)/classify
CLSLIB = $(CLSOBJDIR)/libclassify.a

LSYMBIONTDIR = ../lib

LDFLAGS= -L/usr/local/lib -L/usr/lib -L$(LSYMBIONTDIR) -L$(CLSOBJDIR) \
    -lsymbiont 

LSYMBIONT = $(LSYMBIONTDIR)/libsymbiont.a

CFDBDIR = ./newcfdb
CFDBOBJS = $(CFDBDIR)/cfdb.o $(CFDBDIR)/cfload.o $(CFDBDIR)/filter.o


CTLOBJS = $(CTLOBJDIR)/call_control.o $(CTLOBJDIR)/cctl_misc.o \
   $(CTLOBJDIR)/global_lookup.o $(CTLOBJDIR)/station_control.o 


OBJECTS = symbiont.o ccmain.o commands.o


all:	symbiont

symbiont: $(OBJECTS) $(DCPOBJS) $(MMIOBJS) $(CTLOBJS) $(CLSLIB) $(CFDBOBJS) $(LSYMBIONT)
	gcc -o symbiont $(LDFLAGS) $(OBJECTS) $(DCPOBJS) $(MMIOBJS) $(CTLOBJS) $(CFDBOBJS) \
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


$(LSYMBIONT):
	$(MAKE) -C $(LSYMBIONTDIR) all


clean:
	rm -f *.o
	rm -f symbiont

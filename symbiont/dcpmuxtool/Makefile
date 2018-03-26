CFLAGS= -g -O2 -Wall -Wextra -Wno-sign-compare \
  -Wno-unused-parameter -Wmissing-declarations -Wredundant-decls \
  -Wstrict-prototypes -I ../include


LDFLAGS= -L/usr/local/lib -L/usr/lib -L../lib -lncursesw -lreadline -lsymbiont

DCPMUXDIR = ../symbiont/dcpmux

DCPOBJS= $(DCPMUXDIR)/dcphdlc.o $(DCPMUXDIR)/dcpmux.o

OBJECTS = dcpmuxtool.o console.o commands.o

all:	dcpmuxtool


$(DCPOBJS):	
	$(MAKE) -C $(DCPMUXDIR) all


dcpmuxtool: $(OBJECTS) $(DCPOBJS)
	gcc $(LDFLAGS) -o dcpmuxtool dcpmuxtool.o  \
		console.o commands.o $(DCPOBJS) \
		-lsymbiont -lpthread -lsctp -lrt

clean:
	rm -f *.o
	rm -f dcpmuxtool

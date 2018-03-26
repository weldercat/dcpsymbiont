CFLAGS = -g3 -ggdb -O2 -std=gnu11 -Wall -Wextra -Wno-sign-compare \
  -Wno-unused-parameter -Wmissing-declarations -Wredundant-decls \
  -Wstrict-prototypes -I ../symbiont/include

LDFLAGS = -L/usr/local/lib -L/usr/lib -L../symbiont/lib/ -lncursesw -lreadline 


OBJECTS = dcp.o dcptool.o trace.o console.o commands.o 

all:	dcptool

dcptool:	$(OBJECTS)
	gcc -o dcptool $(LDFLAGS) $(OBJECTS) -lsymbiont -lpthread -lsctp

clean:
	rm -f *.o
	rm -f dcptool

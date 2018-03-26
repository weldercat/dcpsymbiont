INCDIR = -I ../../include -I ./classify/ -I .

CLSDIR = ./classify

CFLAGS=-fPIC -g -O3  -Wall -Wextra -Wno-sign-compare \
  -Wno-unused-parameter -Wmissing-declarations -Wredundant-decls \
  -Wstrict-prototypes $(INCDIR) 


OBJECTS = call_control.o cctl_misc.o global_lookup.o \
	station_control.o 
	
all: $(OBJECTS)

clean:
	rm -f *.o




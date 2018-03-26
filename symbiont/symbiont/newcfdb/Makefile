FILES = Makefile cfdb.c cfload.c filter.c

INCDIR = ../../include

OBJECTS = cfdb.o cfload.o filter.o



CFLAGS = -fPIC -g -O3  -Wall -Wextra -Wno-sign-compare \
  -Wno-unused-parameter -Wmissing-declarations -Wredundant-decls \
  -Wstrict-prototypes -I $(INCDIR) 

all: $(OBJECTS)


clean:
	rm -f *.o

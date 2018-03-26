CFLAGS= -g -O2 -Wall -Wextra -Wno-sign-compare \
  -Wno-unused-parameter -Wmissing-declarations -Wredundant-decls \
  -Wstrict-prototypes -I ../include -lsymbiont

LDFLAGS= -L/usr/local/lib -L/usr/lib -L../lib -lncursesw -lreadline

OBJECTS = symtool.o console.o commands.o 


all:	symtool

symtool: $(OBJECTS)
	gcc $(LDFLAGS) -o symtool symtool.o  console.o commands.o -lsymbiont -lpthread

clean:
	rm -f *.o
	rm -f symtool

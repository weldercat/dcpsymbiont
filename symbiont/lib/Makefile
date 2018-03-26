INCDIR = -I ../include -I .


CFLAGS = -fPIC -g -O3  -Wall -Wextra -Wno-sign-compare \
  -Wno-unused-parameter -Wmissing-declarations -Wredundant-decls \
  -Wstrict-prototypes $(INCDIR) 

LDFLAGS = -L/usr/local/lib -L/usr/lib -L../../misc/bstrlib -lpthread

LOBJECTS = symerror.o yxtlink.o sigtran.o hua.o strmap.o \
	tmqueue.o bstrlib.o
	
OBJECTS = huatest.o symtest.o overlapped.o symtest_imt.o 

APPS = symtest overlapped symtest_imt huatest

	
all: libsymbiont.a $(APPS)

symtest: symerror.o yxtlink.o symtest.o bstrlib.o
	gcc $(LDFLAGS) -o symtest symerror.o yxtlink.o symtest.o bstrlib.o
	
overlapped: symerror.o yxtlink.o overlapped.o bstrlib.o
	gcc $(LDFLAGS) -luuid -lrt -o overlapped symerror.o yxtlink.o overlapped.o bstrlib.o

symtest_imt: symerror.o yxtlink.o symtest_imt.o bstrlib.o
	gcc $(LDFLAGS) -o symtest_imt symerror.o yxtlink.o symtest_imt.o bstrlib.o

huatest: symerror.o hua.o sigtran.o huatest.o bstrlib.o
	gcc $(LDFLAGS) -lsctp -o huatest symerror.o hua.o sigtran.o huatest.o bstrlib.o

libsymbiont.a: $(LOBJECTS)
	ar -rc libsymbiont.a $(LOBJECTS)
	
bstrlib.o: bstrlib.c
	$(CC) -g -fPIC -O3 -Wall -pedantic -ansi -s $(INCDIR) -c $< -o $@

clean:
	rm -f *.o
	rm -f libsymbiont.a
	rm -f $(APPS)




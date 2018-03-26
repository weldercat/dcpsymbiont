INCDIR = -I ../include -I ../../../include

CFLAGS=-fPIC -g -O3  -Wall -Wextra -Wno-sign-compare \
  -Wno-unused-parameter -Wmissing-declarations -Wredundant-decls \
  -Wstrict-prototypes $(INCDIR) 


LOBJECTS = msg_classifier.o dcp_ledmode_classifier.o \
	dcp_cmdtype_classifier.o dcp_erase_classifier.o \
	dcp_color_classifier.o 

OBJECTS = $(LOBJECTS) clstest.o

GPERF = /usr/bin/gperf

GPFLAGS = -t -L ANSI-C -K name -7 -C -E --null-strings 


all: $(OBJECTS) clstest libclassify.a

libclassify.a: $(LOBJECTS)
	ar -rc libclassify.a  $(LOBJECTS)

clstest: $(OBJECTS)
	$(CC) -o clstest clstest.o msg_classifier.o


#*_classifier.o: $(*).c $(*).h
	

dcp_cmdtype_classifier.c: dcp_cmdtype_classifier.gperf
	$(GPERF) $(GPFLAGS) -H lookup_cmdtype_hash -N lookup_cmdtype \
	$< > $@

dcp_ledmode_classifier.c: dcp_ledmode_classifier.gperf
	$(GPERF) $(GPFLAGS) -H lookup_ledmode_hash -N lookup_ledmode \
	$< > $@

dcp_erase_classifier.c: dcp_erase_classifier.gperf
	$(GPERF) $(GPFLAGS) -H lookup_erase_hash -N lookup_erase \
	$< > $@

dcp_color_classifier.c: dcp_color_classifier.gperf
	$(GPERF) $(GPFLAGS) -H lookup_color_hash -N lookup_color \
	$< > $@

msg_classifier.c: msg_classifier.gperf
	$(GPERF) $(GPFLAGS) -H lookup_msg_hash -N lookup_msg_class \
	$< > $@

clean:
	rm -f *.o
	rm -f *_classifier.c
	rm -f clstest
	rm -f libclassify.a
	




INCDIR = -I ../../include -I .

CFLAGS=-fPIC -g -O3  -Wall -Wextra -Wno-sign-compare \
  -Wno-unused-parameter -Wmissing-declarations -Wredundant-decls \
  -Wstrict-prototypes $(INCDIR) 


OBJECTS = transcode.o mmi_print.o mmi_misc.o transcode_test.o 	


GPERF = /usr/bin/gperf

GPFLAGS = -t -L ANSI-C -K s -l -C -E --null-strings 

all: $(OBJECTS)

transcode.o:	dcp_resp.c transcode.c dcp_resp_status.h

dcp_resp.c: dcp_responces.gperf
	gperf $(GPFLAGS) -H dcp_resp_hash -N decode_dcp_resp $< > $@




clean:
	rm -f *.o
	rm -f dcp_resp.c




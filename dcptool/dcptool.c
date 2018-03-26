/*
 * Copyright 2017  Stacy <stacy@sks.uz>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/time.h>
#include <assert.h>
#include <ctype.h>
#include <curses.h>

#include "trace.h"
#include "console.h"
#include "dcp.h"
#include "commands.h"
#include <symbiont/hua.h>

#define TESTDEV		"/dev/tty7"
#define MAX_PACKET	32



const char *deflog = "./dcplog.txt";
static FILE	*log = NULL;
static bool exit_request = false;
static bool xiomode = false;
static struct dcp_hdlc_state	dstate;

void usage(void);
void print_help(void);
int run(int dchan, const char *logfile, const char *hua_connstr);
int process_line(char *line);
int dump_line(unsigned char *p, int offset, int len);
void dump_pkt(unsigned char *p, int len, int pktno, bool xmt);
void input_data(int fd);
int parse_line(unsigned char *buf, char *line);
int xio_dcpread(void *trn_ctx, int dchan, uint8_t *data, size_t length);
int xio_dcpwrite(void *trn_ctx, int dchan, uint8_t *data, size_t length);


int xio_dcpread(void *trn_ctx, int dchan, uint8_t *data, size_t length)
{
	hua_ctx *hctx;
	int	msglen;
	uint8_t	msgbuf[HUA_MAX_MSG_SIZE+1];
	uint8_t	*dptr = NULL;
	int	datalen = 0;
	int	ifi = -1;
	
	hctx = trn_ctx;
	memset(msgbuf, 0, HUA_MAX_MSG_SIZE+1);
	msglen = hua_read_pkt(hctx, msgbuf, HUA_MAX_MSG_SIZE);
	if (msglen <= 0) return msglen;
	hua_parse_pkt(msgbuf, msglen, &ifi, &dptr, &datalen);
	if (ifi != dchan) {
		dcptrace(TRC_WARN, "received HUA packet for wrong dchan: %d, wanted %d\n", ifi, dchan);
		return 0;
	}
	if ((datalen <= 0) || (!dptr)) {
		dcptrace(TRC_WARN, "rteceived HUA packet without payload\n");
		return 0;
	}
	if (datalen > length) datalen = length;
	memcpy(data, dptr, datalen);
	return datalen;
}

int xio_dcpwrite(void *trn_ctx, int dchan, uint8_t *data, size_t length)
{
	hua_ctx *hctx;
	int	res;
	
	hctx = trn_ctx;
	res = hua_send_data(hctx, dchan, data, length);
	return res;
}


int parse_line(unsigned char *buf, char *line)
{
	int	len;
	int	blen;
	char	*p;
	char	c;
	unsigned int byte;
	int	res;
	
	assert(buf);
	assert(line);
	len = strlen(line);
	blen = 0;
	p = line;
	while (len && (blen < MAX_PACKET)) {
		c = *p;
		if (isxdigit(c)) {
			sscanf(p, "%x%n", &byte, &res);
//			printf("byte = %0x, res=%0d\n", byte, res);
			p += res;
			len -= res;
			buf[blen++] = byte & 0xff;
		} else if (c == '"') {
			p++;
			len--;
			while (len && (blen < MAX_PACKET)) {
				c = *p++;
				len--;
				if (c == '"') break;
				buf[blen++] = c;
			}
		} else {
			p++;
			len--;
		}
	}
	return blen;
}

int process_line(char *line)
{
	static int xmtno = 1;
	int	len;
	int	res;
	unsigned char buffer[MAX_PACKET + 1];
	
	if (!line) {
		exit_request = true; 
		return 0;
	} 
	if (*line == '\000') {
		return 0;
	}
	if (!xiomode) {
		res = run_command(line, dstate.fd);
		if (res == CMD_OK) return 0;
	}
	
	memset(buffer, 0, MAX_PACKET+1);
	len = parse_line(buffer, line);
	if (len > 0) {
		dump_pkt(buffer, len, xmtno++, true);
		dcp_xmt(&dstate, true, buffer, len);
	} else {
		console_mprintf("dcptool: cannot parse line - nothing to send\n");
		console_mrefresh();
	}
	return 0;
}

#define MAXDMP	16
int dump_line(unsigned char *p, int offset, int len)
{
	int	i;
	
	if (len > MAXDMP) len = MAXDMP;
	console_mprintf("%04x  ", offset);
	for (i = 0; i < len; i++) console_mprintf("%02x ", p[i]);
	for (i = 0; i < (MAXDMP - len); i++) console_mprintf("   ");
	console_mprintf(" |");
	for (i = 0; i < len; i++) {
		unsigned char c;
		c = p[i] & 0x7f;
		if ((c < ' ') || (c > '~')) c = '.';
		console_mputc(c);
	}
	for (i = 0; i < (MAXDMP - len); i++) console_mputc(' ');
	console_mprintf("|\n\r");
	console_mrefresh();
	return len;
}

void dump_pkt(unsigned char *p, int len, int pktno, bool xmt)
{
	int	offset = 0;

	if (len > 0) console_mprintf("%s packet #%0d\n", (xmt ? "xmt" : "rcv"), pktno);
	while (len > 0) {
		int dlen;
		dlen = dump_line(&(p[offset]), offset, len);
		len -= dlen;
		offset += dlen;
	}
}


void usage(void)
{
	printf("ISDN/DCP digital phones protocol testing tool\n");
	printf("Usage: dcptool [options]\n");
	printf("where options are:\n");
	printf("        -h - print this text\n");
	printf("        -n <dchan> - specify dchannel number (required)\n");
	printf("        -c <xio_conn_string> - connect to HUA gateway\n"
	       "           instead of hardware D-chan.\n"
	       "           This option also disables all B-chan & span \n"
	       "           related functionality.\n"
	       "           This is because there is no way for dcptool \n"
	       "           to access remote B-chans.\n");
	printf("        -p <ppid> - use specified sctp ppid\n"
	       "                    while connecting to HUA gw (works with -c only)\n");
	printf("        -l <logfile> - specify session log file. Default is \"dcplog.txt\".\n");
	printf("                       dpctool appends data to the logfile if it exists.\n");
	printf("\n");
}

void print_help(void)
{
	console_mprintf("Enter lines to be sent to DCP D-chan\n");
	console_mprintf("Use \" quotes to enclose text.\n");
	console_mprintf("All other data are interpreted as sequence of hex-encoded bytes.\n");
	console_mprintf("Example 02 0a 0d 07 \"sample text\" etc...\n");
	console_mprintf("CTRL-D to exit...\n\n");
	console_mrefresh();
	console_srefresh();
}

void input_data(int fd)
{
	unsigned char buffer[MAX_PACKET];
	int	len, res;
	bool	data_avail = true;
	static int rcvno = 1;
	bool	cmdflag = false;
	fd_set	fds;
	struct timeval tv;
	
	while (data_avail) {
		memset(buffer, 0, MAX_PACKET);
		len = dcp_rcv(&dstate, &cmdflag, buffer);
		if (len < 0) {
			console_mprintf("dcptool: error reading DCP packet:%d\n", len);
			console_mrefresh();
			return;
		}
		if (len==0) {
//			console_mprintf("dcptool: zero-length DCP packet\n");
//			console_mrefresh();
			return;
		}
		dump_pkt(buffer, len, rcvno++, false);
		FD_ZERO(&fds);
		FD_SET(fd, &fds);
		tv.tv_sec = 0;
		tv.tv_usec = 0;
		res = select(FD_SETSIZE, &fds, NULL, NULL, &tv);
		if (res < 0) {
			console_mprintf("dcptool: select error:%s\n", strerror(errno));
			console_mrefresh();
			return;
		} else {
			data_avail = (res != 0);
		}
	}
}


int run(int dchan, const char *logfile, const char *hua_connstr)
{
	__label__ out;
	__label__ errout;
	int	res;
	fd_set	rfds;
	struct timeval tv;
	int	c, dcpfd;
	hua_ctx *hctx;
	
	if (logfile) {
		log = fopen(logfile, "a");
		if (!log) {
			dcptrace(TRC_ERR, "error opening log file %s: %s\n", logfile, strerror(errno));
			return 2;
		}
	}
	res = console_init(process_line);
	if (res != CONSOLE_OK) return 2;
	print_help();
	memset(&dstate, 0, sizeof(dstate));	
	if (xiomode) {
		hctx = hua_connect(hua_connstr, NULL, NULL, 0);
		if (!hctx) {
			dcptrace(TRC_ERR, "Cannot connect to hua server %s\n", hua_connstr);
			goto errout;
		}
		res = dcp_init_xio(&dstate, dchan, DCP_LCN_CHAN0, hctx, xio_dcpread, xio_dcpwrite);
		dcpfd = hctx->fd;
	} else {
		res = dcp_init_hdlc(&dstate, dchan, DCP_LCN_CHAN0);
		dcpfd = dstate.fd;
	};
	if (res != DCP_OK) {
		dcptrace(TRC_ERR, "Cannot initialize dcp engine: %d\n", res);
		goto	errout;
	}
	
	while (!exit_request) {
		FD_ZERO(&rfds);
		FD_SET(fileno(stdin), &rfds);
		FD_SET(dcpfd, &rfds);
		
		tv.tv_sec = 0;
		tv.tv_usec = 0;
		if (dstate.retransmit) {
			if (dstate.retransmit > 1000) tv.tv_sec = dstate.retransmit / 1000;
			tv.tv_usec = (dstate.retransmit % 1000) * 1000;
		}

		res = select(FD_SETSIZE, &rfds, NULL, NULL, &tv);
		if (res < 0) {
			dcptrace(TRC_ERR, "dcptool: select error %s", strerror(errno));
			goto out;
		}
		if (FD_ISSET(fileno(stdin), &rfds)) {
			c = console_getch();
			res = console_input(c);
			if (res != CONSOLE_OK) goto errout;
		}
		if (FD_ISSET(dcpfd, &rfds)) {
			input_data(dcpfd);
		} else {
			dcp_run(&dstate);
		}
		
	}

	printf("dcptool: Event loop has exited\n");
	
out:	console_rundown();
	if (log) fclose(log);
	return 0;
errout:
	console_rundown();
	if (log) fclose(log);
	return 3;
}

int main(int argc, char **argv)
{
	__label__ out;
	int	ch, result = 0;
	char	*logname = NULL;
	char	logpath[257];
	char	connstr[257];
	int	dchan = -1;

	memset(logpath, 0, 257);
	while ((ch = getopt(argc, argv, "hn:l:c:")) != -1) {
		switch (ch) {
			case 'l':
				sscanf(optarg, "%250s", (char *)&logpath);
				logname = logpath;
				break;
			case 'n':
				sscanf(optarg, "%u", &dchan);
				break;
			case 'c':
				sscanf(optarg, "%250s", (char *)&connstr);
				xiomode = true;
				break;
			case 'h':
				usage();
				goto out;
			default:
				result = 1;
				goto out;
		}
	}
	if ((argc > optind) || (argc == 1)) {
		printf("dcptool: use \"-h\" for help\n");
		result = 1;
		goto out;
	}
	if (dchan < 1) {
		printf("dcptool: D-chan number must be specified\n");
		result = 1;
		goto out;
	}
//	if (!logname) logname = (char *)deflog;
	result = run(dchan, (const char *)logname, (const char *)connstr);

out:	return result;
};



/*
 * Written by Mark Spencer <markster@digium.com>
 * Based on previous works, designs, and architectures conceived and
 * written by Jim Dixon <jim@lambdatel.com>.
 *
 * Copyright (C) 2001 Jim Dixon / Zapata Telephony.
 * Copyright (C) 2001-2008 Digium, Inc.
 *
 * All rights reserved.
 *
 * Primary Author: Mark Spencer <markster@digium.com>
 * Radio Support by Jim Dixon <jim@lambdatel.com>
 */

/*
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2 as published by the
 * Free Software Foundation. See the LICENSE file included with
 * this program for more details.
 */

#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <linux/types.h>
#include <linux/ppp_defs.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdlib.h>
#include <dahdi/user.h>

#define FAST_HDLC_NEED_TABLES
#include <dahdi/fasthdlc.h>

#include "bittest.h"

#include "dahdi_tools_version.h"

#define BLOCK_SIZE 2039

static unsigned short fcstab[256] =
{
	0x0000, 0x1189, 0x2312, 0x329b, 0x4624, 0x57ad, 0x6536, 0x74bf,
	0x8c48, 0x9dc1, 0xaf5a, 0xbed3, 0xca6c, 0xdbe5, 0xe97e, 0xf8f7,
	0x1081, 0x0108, 0x3393, 0x221a, 0x56a5, 0x472c, 0x75b7, 0x643e,
	0x9cc9, 0x8d40, 0xbfdb, 0xae52, 0xdaed, 0xcb64, 0xf9ff, 0xe876,
	0x2102, 0x308b, 0x0210, 0x1399, 0x6726, 0x76af, 0x4434, 0x55bd,
	0xad4a, 0xbcc3, 0x8e58, 0x9fd1, 0xeb6e, 0xfae7, 0xc87c, 0xd9f5,
	0x3183, 0x200a, 0x1291, 0x0318, 0x77a7, 0x662e, 0x54b5, 0x453c,
	0xbdcb, 0xac42, 0x9ed9, 0x8f50, 0xfbef, 0xea66, 0xd8fd, 0xc974,
	0x4204, 0x538d, 0x6116, 0x709f, 0x0420, 0x15a9, 0x2732, 0x36bb,
	0xce4c, 0xdfc5, 0xed5e, 0xfcd7, 0x8868, 0x99e1, 0xab7a, 0xbaf3,
	0x5285, 0x430c, 0x7197, 0x601e, 0x14a1, 0x0528, 0x37b3, 0x263a,
	0xdecd, 0xcf44, 0xfddf, 0xec56, 0x98e9, 0x8960, 0xbbfb, 0xaa72,
	0x6306, 0x728f, 0x4014, 0x519d, 0x2522, 0x34ab, 0x0630, 0x17b9,
	0xef4e, 0xfec7, 0xcc5c, 0xddd5, 0xa96a, 0xb8e3, 0x8a78, 0x9bf1,
	0x7387, 0x620e, 0x5095, 0x411c, 0x35a3, 0x242a, 0x16b1, 0x0738,
	0xffcf, 0xee46, 0xdcdd, 0xcd54, 0xb9eb, 0xa862, 0x9af9, 0x8b70,
	0x8408, 0x9581, 0xa71a, 0xb693, 0xc22c, 0xd3a5, 0xe13e, 0xf0b7,
	0x0840, 0x19c9, 0x2b52, 0x3adb, 0x4e64, 0x5fed, 0x6d76, 0x7cff,
	0x9489, 0x8500, 0xb79b, 0xa612, 0xd2ad, 0xc324, 0xf1bf, 0xe036,
	0x18c1, 0x0948, 0x3bd3, 0x2a5a, 0x5ee5, 0x4f6c, 0x7df7, 0x6c7e,
	0xa50a, 0xb483, 0x8618, 0x9791, 0xe32e, 0xf2a7, 0xc03c, 0xd1b5,
	0x2942, 0x38cb, 0x0a50, 0x1bd9, 0x6f66, 0x7eef, 0x4c74, 0x5dfd,
	0xb58b, 0xa402, 0x9699, 0x8710, 0xf3af, 0xe226, 0xd0bd, 0xc134,
	0x39c3, 0x284a, 0x1ad1, 0x0b58, 0x7fe7, 0x6e6e, 0x5cf5, 0x4d7c,
	0xc60c, 0xd785, 0xe51e, 0xf497, 0x8028, 0x91a1, 0xa33a, 0xb2b3,
	0x4a44, 0x5bcd, 0x6956, 0x78df, 0x0c60, 0x1de9, 0x2f72, 0x3efb,
	0xd68d, 0xc704, 0xf59f, 0xe416, 0x90a9, 0x8120, 0xb3bb, 0xa232,
	0x5ac5, 0x4b4c, 0x79d7, 0x685e, 0x1ce1, 0x0d68, 0x3ff3, 0x2e7a,
	0xe70e, 0xf687, 0xc41c, 0xd595, 0xa12a, 0xb0a3, 0x8238, 0x93b1,
	0x6b46, 0x7acf, 0x4854, 0x59dd, 0x2d62, 0x3ceb, 0x0e70, 0x1ff9,
	0xf78f, 0xe606, 0xd49d, 0xc514, 0xb1ab, 0xa022, 0x92b9, 0x8330,
	0x7bc7, 0x6a4e, 0x58d5, 0x495c, 0x3de3, 0x2c6a, 0x1ef1, 0x0f78
};

#define PPP_INITFCS	0xffff	/* Initial FCS value */
#define PPP_GOODFCS	0xf0b8	/* Good final FCS value */
#define PPP_FCS(fcs, c)	(((fcs) >> 8) ^ fcstab[((fcs) ^ (c)) & 0xff])

void print_packet(unsigned char *buf, int len)
{
	int x;
	printf("{ ");
	for (x = 0; x < len; x++) {
		printf("%02x ", buf[x]);
	}
	printf("}\n");
}

static int bytes;
static int errors;
static int c;

void dump_bits(unsigned char *outbuf, int len)
{
	int x, i;
	for (x = 0; x < len; x++) {
		for (i = 0; i < 8; i++) {
			if (outbuf[x] & (1 << (7 - i))) {
				printf("1");
			} else {
				printf("0");
			}
		}
	}
	printf("\n");
}

void dump_bitslong(unsigned int outbuf, int bits)
{
	int i;
	printf("Dumping %d bits from %04x\n", bits, outbuf);
	for (i = 0; i < bits; i++) {
		if (outbuf & (1 << (31 - i))) {
			printf("1");
		} else {
			printf("0");
		}
	}
	printf("\n");
}

int check_frame(unsigned char *outbuf, int res)
{
	static int setup = 0;
	int x;
	unsigned short fcs = PPP_INITFCS;
	if (c < 1) {
		c = 1;
	}
	if (!setup) {
		c = outbuf[0];
		setup++;
	}
	for (x = 0; x < res; x++) {
		if (outbuf[x] != c && (x < res - 2)) {
			printf("(Error %d): Unexpected result, %d != %d, position %d %d bytes since last error.\n",
				   ++errors, outbuf[x], c, x, bytes);
			if (!x) {
				c = outbuf[0];
			}
			bytes = 0;
		} else {
			bytes++;
		}
		fcs = PPP_FCS(fcs, outbuf[x]);
	}
	if (fcs != PPP_GOODFCS) {
		printf("FCS Check failed :( (%04x != %04x)\n", fcs, PPP_GOODFCS);
	}
#if 0
	if (res != c) {
		printf("Res is %d, expected %d\n", res, c+2);
	}
#endif
	c = bit_next(c);
	return 0;
}

int main(int argc, char *argv[])
{
	int fd;
	int res, x;
	struct dahdi_params tp;
	struct dahdi_bufferinfo bi;
	int bs = BLOCK_SIZE;
	int pos = 0;
	unsigned char inbuf[BLOCK_SIZE];
	unsigned char outbuf[BLOCK_SIZE];
	int bytes = 0;
	int out;
	unsigned int olddata1;
	int oldones1;
	int oldbits1;
	unsigned int olddata = 0;
	int oldones = 0;
	int oldbits = 0;
	int hdlcmode = 0;
	struct fasthdlc_state fs;
	if (argc < 2) {
		fprintf(stderr, "Usage: %s <DAHDI device>\n", argv[0]);
		exit(1);
	}
	fd = open(argv[1], O_RDWR, 0600);
	if (fd < 0) {
		fprintf(stderr, "Unable to open %s: %s\n", argv[1], strerror(errno));
		exit(1);
	}
	if (ioctl(fd, DAHDI_SET_BLOCKSIZE, &bs)) {
		fprintf(stderr, "Unable to set block size to %d: %s\n", bs, strerror(errno));
		exit(1);
	}
	if (ioctl(fd, DAHDI_GET_PARAMS, &tp)) {
		fprintf(stderr, "Unable to get channel parameters\n");
		exit(1);
	}
	if ((tp.sigtype & DAHDI_SIG_HDLCRAW) == DAHDI_SIG_HDLCRAW) {
		printf("In HDLC mode\n");
		hdlcmode = 1;
	} else if ((tp.sigtype & DAHDI_SIG_CLEAR) == DAHDI_SIG_CLEAR) {
		printf("In CLEAR mode\n");
		hdlcmode = 0;
	} else {
		fprintf(stderr, "Not in a reasonable mode\n");
		exit(1);
	}
	res = ioctl(fd, DAHDI_GET_BUFINFO, &bi);
	if (!res) {
		bi.txbufpolicy = DAHDI_POLICY_IMMEDIATE;
		bi.rxbufpolicy = DAHDI_POLICY_IMMEDIATE;
		bi.numbufs = 4;
		res = ioctl(fd, DAHDI_SET_BUFINFO, &bi);
		if (res < 0) {
			fprintf(stderr, "Unable to set buf info: %s\n", strerror(errno));
			exit(1);
		}
	} else {
		fprintf(stderr, "Unable to get buf info: %s\n", strerror(errno));
		exit(1);
	}
	ioctl(fd, DAHDI_GETEVENT);
	fasthdlc_precalc();
	fasthdlc_init(&fs, FASTHDLC_MODE_64);
	for (;;) {
		res = read(fd, outbuf, sizeof(outbuf));
		if (hdlcmode) {
			if (res < 0) {
				if (errno == ELAST) {
					if (ioctl(fd, DAHDI_GETEVENT, &x) < 0) {
						fprintf(stderr, "Unaable to get event: %s\n", strerror(errno));
						exit(1);
					}
					fprintf(stderr, "Event: %d (%d bytes since last error)\n", x, bytes);
					bytes = 0;
					continue;
				} else {
					fprintf(stderr, "Error: %s\n", strerror(errno));
					exit(1);
				}
			}
#if 0
			printf("Res is %d, buf0 is %d, buf1 is %d\n", res, outbuf[0], outbuf[1]);
#endif
			if (res < 2) {
				fprintf(stderr, "Too small?  Only got %d bytes\n", res);
			}
			check_frame(outbuf, res);
		} else {
			for (x = 0; x < res; x++) {
				oldones1 = oldones;
				oldbits1 = oldbits;
				olddata1 = olddata;
				oldones = fs.ones;
				oldbits = fs.bits;
				olddata = fs.data;
				fasthdlc_rx_load(&fs, outbuf[x]);
				out = fasthdlc_rx_run(&fs);
				if (out & RETURN_EMPTY_FLAG) {
					/* Empty */
				} else if (out & RETURN_COMPLETE_FLAG) {
					if (pos && (pos < 2)) {
						printf("Too short? (%d)\n", pos);
					} else if (pos) {
						check_frame(inbuf, pos);
					}
					pos = 0;
				} else if (out & RETURN_DISCARD_FLAG) {
					printf("Discard (search = %d, len = %d, buf = %d, x=%d, res=%d, oldones: %d, oldbits: %d)\n",
						   c, pos, inbuf[0], x, res, oldones, oldbits);
					dump_bitslong(olddata, oldbits);
					printf("Discard                                                 oldones: %d, oldbits: %d)\n",
						   oldones1, oldbits1);
					dump_bitslong(olddata1, oldbits1);
					if (x > 64) {
						dump_bits(outbuf + x - 64, 64);
						dump_bits(outbuf + x, 64);
					}
					pos = 0;
				} else {
					if ((out != c) && (pos < c) && !pos) {
						printf("Warning: Expecting %d at pos %d, got %d (x =%d)\n", c, pos, out, x);
						if (x > 64) {
							dump_bits(outbuf + x - 64, 64);
							dump_bits(outbuf + x, 64);
						}
					}
					inbuf[pos++] = out;
				}
			}
		}
	}

}

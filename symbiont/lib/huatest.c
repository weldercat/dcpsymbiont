/*
*  This program is free software: you can redistribute it and/or modify
*  it under the terms of the GNU General Public License as published by
*  the Free Software Foundation, either version 2 of the License, or
*  (at your option) any later version.
*
*  This program is distributed in the hope that it will be useful,
*  but WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*  GNU General Public License for more details.
*
*  You should have received a copy of the GNU General Public License
*  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#define _GNU_SOURCE	1

#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <pthread.h>

#define RAND_DEV	"/dev/urandom"

#include <symbiont/hua.h>
#include <symbiont/symerror.h>
#define NOFAIL_LOCK_UNNEEDED	1
#include <symbiont/nofail_wrappers.h>

#define MAX_BUFFER_LEN		4096
#define DEFAULT_BLOCK_LEN	510
#define DEFAULT_IFI		1

static int random_data(unsigned char *buffer, int length);
static void usage(void);
static void hexdump(unsigned char *b, int len);
static void run(char *conn);
static void echo_reflect(int ifi, uint8_t *data, int datalen, void *arg);
static void echo_check(int ifi, uint8_t *data, int datalen, void *arg);
static void lurker_dump(int ifi, uint8_t *data, int datalen, void *arg);
static void cansend(void);
static void send_data(void);
static void cansend_signal(void);

static bool server = false;
static bool client = false;
static bool lurkmode = false;

static hua_ctx *context = NULL;
static unsigned char data_buffer[MAX_BUFFER_LEN];
static int sent_length = -1;
static int sent_credit = 0;
static pthread_cond_t cansend_cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t cansend_mutex = PTHREAD_MUTEX_INITIALIZER;
static int goodblks = 0;
static int badblks = 0;
static int blknum = 0;



static void cansend(void)
{
	int	res;
	
	mutex_lock(&cansend_mutex);
	while (sent_credit <= 0) {
		res = pthread_cond_wait(&cansend_cond, &cansend_mutex);
		if (res) {
			SYMWARNING("error waiting for CAN SEND condition: %s\n", STRERROR_R(res));
			if (res == EINVAL) abort();
		}
	}
	mutex_unlock(&cansend_mutex);
}

static void send_data(void)
{
	int	res;
	
	res = random_data(data_buffer, DEFAULT_BLOCK_LEN);
	assert(res == DEFAULT_BLOCK_LEN);
	mutex_lock(&cansend_mutex);
	if (sent_credit > 0) --sent_credit;
	else sent_credit = 0;
	mutex_unlock(&cansend_mutex);
	hua_send_data(context, DEFAULT_IFI, data_buffer, DEFAULT_BLOCK_LEN);
	sent_length = DEFAULT_BLOCK_LEN;
}

static void cansend_signal(void)
{
	int	blk_count;

	mutex_lock(&cansend_mutex);
	blk_count = ++sent_credit;
	mutex_unlock(&cansend_mutex);
	if (blk_count > 0) pthread_cond_signal(&cansend_cond);
}

static void echo_reflect(int ifi, uint8_t *data, int datalen, void *arg)
{
	assert(data);
	if (!context) {
		SYMWARNING("context is not defined. Cannot proceed\n");
		return;
	}
	if (datalen <= 0) {
		SYMWARNING("nothing to send\n");
		return;
	}
	++goodblks;
	hua_send_data(context, ifi, data, datalen);
}

static void echo_check(int ifi, uint8_t *data, int datalen, void *arg)
{
	int	res;
	
	assert(data);
	if (datalen > MAX_BUFFER_LEN) {
		SYMWARNING("datalen %d > max buffer length (%d); truncated.\n", datalen, MAX_BUFFER_LEN)
		datalen = MAX_BUFFER_LEN;
	}
	if (datalen == sent_length) {
		res = memcmp(data_buffer, data, datalen);
	} else {
		SYMWARNING("%d bytes sent, %d bytes received\n", sent_length, datalen);
		res = 1;
	}
	blknum++;
	if (!res) {
		SYMINFO("ifi=%d, block #%d (%d bytes) received OK, good:%d, bad:%d\n", ifi, blknum, datalen, goodblks, badblks);
		++goodblks;
	} else {
		SYMWARNING("ifi=%d, block #%d - echo data are different:\n", ifi, blknum);
		SYMWARNING("\tsent bytes:\n");
		hexdump(data_buffer, sent_length);
		SYMWARNING("\n\treceived bytes:\n");
		hexdump(data, datalen);
		++badblks;
	}
	cansend_signal();
}

static void lurker_dump(int ifi, uint8_t *data, int datalen, void *arg)
{
	SYMINFO("received block #%d, %d bytes @%p, ifi=%d\n", blknum, datalen, data, ifi);
	hexdump(data, datalen);
	blknum++;
}

static int random_data(unsigned char *buffer, int length)
{
	static int rand_fd = -1;
	int	res;
	
	assert(buffer);
	assert(length > 0);
	if (rand_fd < 0) {
		rand_fd = open(RAND_DEV, 0);
		if (rand_fd < 0) {
			printf("cannot open %s: %s\n", RAND_DEV, strerror(errno));
			exit(2);
		}
		assert(rand_fd >= 0);
	}
	
	res = read(rand_fd, buffer, length);
	if (res < length) {
		printf("cannot read random data: %s\n", strerror(errno));
		exit(2);
	}
	return res;
}



static void usage(void) 
{
	printf("HUA test client/server\n");
	printf("Usage: huatest <-c connect_string>|<-s bind_string>\n");
	printf("        -l      -lurk mode. Do not send anything,\n");
	printf("                    just print what other side sends\n");
	printf("\n");
}


#define DUMP_LINE	16


static void hexdump(unsigned char *b, int len)
{
	int	i, j;
	unsigned char c;

	if (len > 0) {
		for (i = 0; i < len; i += DUMP_LINE) {
			printf("%04x:   ", i);
			for (j = 0; ((j < DUMP_LINE) && (i+j < len)); j++) {
				printf("%02x ", b[i+j]);
				if (j == DUMP_LINE / 2 - 1) printf(" ");
			}
			printf("  |");
			for (j = 0; ((j < DUMP_LINE) && (i+j < len)); j++) {
				c = b[i+j];
				if (c < 0x20) c = '.';
				else if (c > 0x7e) c = '.';
				printf("%c", c);
			}
			printf("|\n");
			
		}
	} else {
		printf("Nothing to print, len=%d\n", len);
	}
	printf("\n");
}


static void run(char *conn)
{
	hua_ctx	*ctx = NULL;

	assert(conn);
	if (server) {
		ctx = hua_listen(conn, (lurkmode ? lurker_dump : echo_reflect), NULL, 3);
		if (!ctx) {
			printf("huatest: cannot start listener\n");
			exit(3);
		}
	} else {
		ctx = hua_connect(conn, (lurkmode ? lurker_dump : echo_check), NULL, 3);
		if (!ctx) {
			printf("huatest: cannot connect\n");
			exit(3);
		}
	}
	context = ctx;
	for(;;) {
		printf("Waiting for data: server=%d, client=%d\n", server, client);
		if (server) {
			printf("%d blocks reflected\r", goodblks);
			sleep(3);
		} else { 
			if (!lurkmode) {
				send_data();
				cansend();
			} else {
				printf("Lurking... %d blocks seen\r", blknum);
				sleep(3);
			}
		}
	}
}


int main(int argc, char **argv)
{
	int ch, result = 0;
	char connstr[257];
	
	memset(connstr, 0, 257);
	while ((ch = getopt(argc, argv, "hs:c:l")) != -1) {
		switch (ch) {
			case 's':
				sscanf(optarg, "%250s", (char *)&connstr);
				server = true;
				break;
			case 'c':
				sscanf(optarg, "%250s", (char *)&connstr);
				client = true;
				break;
			case 'l':
				lurkmode = true;
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
		printf("huatest: use \"-h\" for help\n");
		result = 1;
		goto out;
	}
	if (!client && !server) {
		printf("huatest: either client or server mode must be selected\n");
		result = 1;
		goto out;
	}
	if (client && server) {
		printf("huatest: client and server modes are mutually exclusive\n");
		result = 1;
		goto out;
	}
	run(connstr);
out:
	return result;

}

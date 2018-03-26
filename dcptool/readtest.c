#include <stdio.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <sys/select.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <dahdi/user.h>
#include <dahdi/tonezone.h>

#define ZAPDEV	"/dev/dahdi/channel"
#define BUFSIZE	160

void hexdump(unsigned char *b, int len)
{
	int	i;
	static int pktnum = 0;
	
	printf("Packet #%0d\t", pktnum++);
	if (len > 0) {
		for (i = 0; i < len; i++) {
			printf("%02x ", b[i]);
		}
	} else {
		printf("Nothing to print, len=%d\n", len);
	}
	printf("\n");
}

void dump(int fd)
{
	unsigned char buffer[BUFSIZE];
	int	len, res;
	fd_set	fds;
	
	for(;;) {
		FD_ZERO(&fds);
		FD_SET(fd, &fds);
		
		res = select(FD_SETSIZE, &fds, NULL, NULL, NULL);
		if (res < 0) {
			printf("Select error: %s\n", strerror(errno));
			break;
		}
		if (FD_ISSET(fd, &fds)) {
			len = read(fd, buffer, BUFSIZE);
			if (len < 0) {
				if (errno == ELAST) {
					int x;

					if (ioctl(fd, DAHDI_GETEVENT, &x) < 0) {
						printf("Unable to get event: %s\n", strerror(errno));
						break;
					}
					printf("Event: %d\n", x);
				} else {
					printf("Read error:%s\n", strerror(errno));
				}
				continue;
			}
			hexdump(buffer, len);
		} else {
			printf("select() returned with no signalled fd\n");
		}
	}

}


int main(int argc, char **argv)
{
	__label__ out;
	int	res, tmp;
	int	fd;
	int	dchan = 15;
	struct dahdi_params p;

	fd = open(ZAPDEV, O_RDWR);
	if (fd < 0) {
		printf("Unable to open device %s : %s\n", ZAPDEV, strerror(errno));
		goto out;
	}	
	if (ioctl(fd, DAHDI_SPECIFY, &dchan)) {
		int tmp = errno;
		printf("Unable to specify dchan #%d: %s\n", dchan, strerror(tmp));
		goto out;
	}
	tmp = BUFSIZE;
	if (ioctl(fd, DAHDI_SET_BLOCKSIZE, &tmp) == -1) {
		printf("Unable to set blocksize %d: %s\n", tmp, strerror(errno));
	}
	if (ioctl(fd, DAHDI_GET_PARAMS, &p)) {
		printf("Unable to get dchan parameters: %s\n", strerror(errno));
		goto out;
	}
	if ((p.sigtype != DAHDI_SIG_HARDHDLC) && (p.sigtype != DAHDI_SIG_HDLCFCS)) {
		printf("dchan is in %d signalling, not FCS HDLC or RAW HDLC mode\n", p.sigtype);
		goto out;
	}

	dump(fd);

out:
	return 0;
}
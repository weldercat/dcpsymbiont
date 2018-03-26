#include <stdio.h>
#include "msg_classifier.h"

int main(int argc, char **argv)
{
	if ((argc < 2) || !argv[1]) {
		printf("usage: clstest <msg_name>\n");
		return 1;
	} else {
		printf("message name: %s\n", argv[1]);
		printf("message type: %d\n", classify_msg(argv[1]));
	}
	return 0;
}

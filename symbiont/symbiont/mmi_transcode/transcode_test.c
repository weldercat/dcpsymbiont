#include <stdio.h>
#include <symbiont/mmi.h>
#include <symbiont/mmi_print.h>
#include <symbiont/transcode.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

static int dump_line(unsigned char *p, int offset, int len);
static void hexdump(unsigned char *p, int len);
static void dump_dblks(struct dcp_dblk *dtrain);
static void dump_cmd(struct mmi_command *cmd);
static void test_cmd_encode(void);
static void test_evt_encode(void);

struct dcp_blk {
	const char *d;
	const char *p;
	size_t	l;
};


#define MAXDMP	16
static int dump_line(unsigned char *p, int offset, int len)
{
	int	i;
	
	if (len > MAXDMP) len = MAXDMP;
	printf("%04x  ", offset);
	for (i = 0; i < len; i++) printf("%02x ", p[i]);
	for (i = 0; i < (MAXDMP - len); i++) printf("-- ");
	printf(" |");
	for (i = 0; i < len; i++) {
		unsigned char c;
		c = p[i] & 0x7f;
		if ((c < ' ') || (c > '~')) c = '.';
		printf("%c", c);
	}
	for (i = 0; i < (MAXDMP - len); i++) printf(".");
	printf("|\n");
	return len;
}

static void hexdump(unsigned char *p, int len)
{
	int	offset = 0;

	while (len > 0) {
		int dlen;
		dlen = dump_line(&(p[offset]), offset, len);
		len -= dlen;
		offset += dlen;
	}
}

static void dump_dblks(struct dcp_dblk *dtrain)
{
	unsigned char *p;
	int	bcnt = 0;
	
	if (!dtrain) printf("NULL dtrain\n");
	else {
		while (dtrain) {
			printf("Block #%0d:\n", bcnt);
			p = &dtrain->data[0];
			hexdump(p, dtrain->length);
			dtrain = dtrain->next;
			++bcnt;
		}
	}
}


static void dump_cmd(struct mmi_command *cmd)
{
	struct dcp_dblk	*dblk = NULL;

	assert(cmd);
	printf("------MMI command:\n");
	print_mmicmd(cmd);
	dblk = mmi2dcp(cmd);
	printf("------encoded to DCP block train:\n");
	dump_dblks(dblk);
	if (dblk) free_dblks(dblk);
	printf("\n");
}

static void test_cmd_encode(void)
{
	struct mmi_command cmd;
	
	memset(&cmd, 0, sizeof(struct mmi_command));
//	dump_cmd(&cmd);

	cmd.station = bfromcstr("rifle_shop/01");
	cmd.ctlname = bfromcstr("blf1");
	cmd.type = MMI_CMD_LED;
	cmd.arg.led_arg.color = LED_COLOR_BLUE;
	cmd.arg.led_arg.mode = LIGHT_FLUTTER;

	dump_cmd(&cmd);
	
	cmd.arg.led_arg.color = LED_COLOR_RED;
	dump_cmd(&cmd);

	cmd.arg.led_arg.color = LED_COLOR_GREEN;
	dump_cmd(&cmd);
	
	bdestroy(cmd.ctlname);
	cmd.ctlname = bfromcstr("mwi");
	dump_cmd(&cmd);

	cmd.arg.led_arg.mode = LIGHT_STEADY;
	dump_cmd(&cmd);

	bdestroy(cmd.ctlname);
	cmd.ctlname = bfromcstr("blf7");
	dump_cmd(&cmd);


	bdestroy(cmd.ctlname);
	cmd.ctlname = bfromcstr("blf10");
	dump_cmd(&cmd);


	bdestroy(cmd.ctlname);
	cmd.ctlname = bfromcstr("unx4");
	dump_cmd(&cmd);

	bdestroy(cmd.ctlname);
	cmd.ctlname = bfromcstr("blf4");
	dump_cmd(&cmd);


	cmd.arg.led_arg.color = LED_COLOR_RED;
	dump_cmd(&cmd);
	mmi_cmd_erase(&cmd);
	
	cmd.station = bfromcstr("rifle_shop/02");
	cmd.ctlname = bfromcstr("menu/1");
	cmd.type = MMI_CMD_PROGRAM;
	cmd.arg.program_arg.text = bfromcstr("Girls");
	cmd.arg.program_arg.number = 3;

	dump_cmd(&cmd);
	bdestroy(cmd.arg.program_arg.text);
	cmd.arg.program_arg.text = bfromcstr("A_VERY_LONG_MENU_TEXT");
	cmd.arg.program_arg.number = 4;
	dump_cmd(&cmd);
	mmi_cmd_erase(&cmd);

	cmd.station = bfromcstr("rifle_shop/03");
//	cmd.ctlname = bfromcstr("display");
	cmd.type = MMI_CMD_TEXT;
	cmd.arg.text_arg.text = bfromcstr("A quick brown fox jumps over the lazy dog"
				" 01234567890 A QUICK BROWN FOX JUMPS OVER THE LAZY DOG");
	cmd.arg.text_arg.col = 12;
	cmd.arg.text_arg.row = 33;
	cmd.arg.text_arg.erase = MMI_ERASE_TAIL;

	dump_cmd(&cmd);
	mmi_cmd_erase(&cmd);
	
	cmd.station = bfromcstr("rifle_shop/03");
	cmd.ctlname = bfromcstr("hookswitch");
	cmd.type = MMI_CMD_ONHOOK;
	dump_cmd(&cmd);
	cmd.type = MMI_CMD_OFFHOOK;
	dump_cmd(&cmd);
	
	bdestroy(cmd.ctlname);
	cmd.ctlname = bfromcstr("ringer");
	cmd.type = MMI_CMD_BEEP_ONCE;
	dump_cmd(&cmd);

	cmd.type = MMI_CMD_BEEP;
	dump_cmd(&cmd);

	cmd.type = MMI_CMD_RING_ONCE;
	dump_cmd(&cmd);
	
	cmd.type = MMI_CMD_RING1;
	dump_cmd(&cmd);
	
	cmd.type = MMI_CMD_RING2;
	dump_cmd(&cmd);
	
	cmd.type = MMI_CMD_RING3;
	dump_cmd(&cmd);
	
	cmd.type = MMI_CMD_NORING;
	dump_cmd(&cmd);

	bdestroy(cmd.ctlname);
	cmd.ctlname = bfromcstr("timer");
	cmd.type = MMI_CMD_TMR_START;
	dump_cmd(&cmd);

	cmd.type = MMI_CMD_TMR_STOP;
	dump_cmd(&cmd);
	mmi_cmd_erase(&cmd);
}


struct dcp_blk test_events[] = {
	{ .d = "ident", .p ="\xc0\x0b\x24\x2a\x0a\x40", .l = 6 },

	{ .d = "off-hook",	.p ="\x80\x02", .l = 2 },
	{ .d = "on-hook",	.p ="\x80\x01", .l = 2 },

	{ .d = "Drop",		.p ="\x80\x20\x04", .l = 3 },
	{ .d = "Conf",		.p ="\x80\x20\x06", .l = 3 },
	{ .d = "Transfer",	.p ="\x80\x20\x08", .l = 3 },
	{ .d = "Hold",		.p ="\x80\x20\x0a", .l = 3 },

	{ .d = "Blf 1",		.p ="\x80\x20\x0e", .l = 3 },
	{ .d = "Blf 2",		.p ="\x80\x20\x10", .l = 3 },
	{ .d = "Blf 11",	.p ="\x80\x20\x22", .l = 3 },
	{ .d = "Blf 30",	.p ="\x80\x20\x48", .l = 3 },
	
	{ .d = "Unexisting key",.p ="\x80\x20\x05", .l = 3 },
	
	{ .d = "Keypad press 1",.p ="\x80\x22\x02", .l = 3 },
	{ .d = "Keypad press 2",.p ="\x80\x22\x04", .l = 3 },
	{ .d = "Keypad press 3",.p ="\x80\x22\x06", .l = 3 },
	{ .d = "Keypad press *",.p ="\x80\x22\x16", .l = 3 },
	{ .d = "Keypad press #",.p ="\x80\x22\x18", .l = 3 },

	{ .d = "Keypad press unexisting",.p ="\x80\x22\x19", .l = 3 },

	{ .d = "Keypad release 1",.p ="\x80\x23\x02", .l = 3 },
	{ .d = "Keypad release 2",.p ="\x80\x23\x04", .l = 3 },
	{ .d = "Keypad release 3",.p ="\x80\x23\x06", .l = 3 },
	{ .d = "Keypad release *",.p ="\x80\x23\x16", .l = 3 },
	{ .d = "Keypad release #",.p ="\x80\x23\x18", .l = 3 },

	{ .d = "Keypad release unexisting",.p ="\x80\x23\x19", .l = 3 },
	
	{ .d = "Menu exit", .p ="\x83\x20\x02", .l = 3 },
	{ .d = "Menu item 1",	.p ="\x81\x20\x02", .l = 3 },
	{ .d = "Menu item 2",	.p ="\x81\x20\x04", .l = 3 },
	{ .d = "Menu item 14",	.p ="\x81\x20\x1c", .l = 3 },
	{ .d = "Menu item 15",	.p ="\x81\x20\x1e", .l = 3 },

	{ .d = "Menu item unexisting",	.p ="\x81\x20\x20", .l = 3 },
	
	{ .d = "Prg OK",	.p ="\xe1\x33\x00", .l = 3 },
	{ .d = "Prg FAIL - off-hook",	.p ="\xe1\x33\x01", .l = 3 },
	{ .d = "Prg FAIL - checksum",	.p ="\xe1\x33\x02", .l = 3 },
	{ .d = "Prg FAIL - inhibit",	.p ="\xe1\x33\x05", .l = 3 },
	{ .d = "Prg FAIL - unknown-7",	.p ="\xe1\x33\x07", .l = 3 },
	{ .d = "Prg FAIL - unknown-4",	.p ="\xe1\x33\x04", .l = 3 },
	{ .d = NULL, .p = NULL, .l = 0 }
};


static void test_evt_encode(void)
{
	struct mmi_event evt;
	struct dcp_blk	*bp;
	struct mmi_event *evp = NULL;
	int i;

	evt.station = bfromcstr("rifle_shop/01");
	evt.ctlname = bfromcstr("hookswitch");
	evt.type = MMI_EVT_OFFHOOK;
	
	print_mmievt(&evt);
	mmi_evt_erase(&evt);

	for (i = 0;;i++) {
		bp = &test_events[i];
		if (!bp->p) break;
		printf("Decoding '%s'...\n", bp->d);
		evp = dcp2mmi((uint8_t *)bp->p, bp->l);
		print_mmievt(evp);
		printf("\n");
	}

}



int main(int argc, char **argv)
{


	
	printf("\nmmi <-> dcp transcode test:\n");
	test_cmd_encode();
	test_evt_encode();
	
	return 0;
}


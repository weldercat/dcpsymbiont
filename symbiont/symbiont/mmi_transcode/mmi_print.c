/*
 * Copyright 2018 Stacy <stacy@sks.uz> 
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



#define _GNU_SOURCE	1

#include <stdio.h>
#include <symbiont/mmi.h>
#include <symbiont/symerror.h>
#include <symbiont/mmi_print.h>

/* pretty printing the MMI commands and events */

struct mmi_name {
	const char *n;
	int	t;
};



#if 0
static const char *mmi_ctl2txt(int ctl);
#endif

static const char *search_name(const struct mmi_name *list, int idx, int maxcount);

#if 0
#define MAX_CTLNAMES (MMI_LED + 1)
static const struct mmi_name ctlnames[MAX_CTLNAMES] = {
	{ .n = "BLF key",	.t = MMI_BLF_KEY },
	{ .n = "Menu item",	.t = MMI_MENU_ITEM },
	{ .n = "Display",	.t = MMI_DISPLAY },
	{ .n = "Func key",	.t = MMI_FUNC_KEY },
	{ .n = "Hookswitch",	.t = MMI_HOOKSWITCH },
	{ .n = "Timer",		.t = MMI_TIMER },
	{ .n = "Speaker",	.t = MMI_SPEAKER },
	{ .n = "Keypad",	.t = MMI_KEYPAD },
	{ .n = "Ringer",	.t = MMI_RINGER },
	{ .n = "LED",		.t = MMI_LED }
};
#endif

#define MAX_EVTNAMES (MMI_EVT_PRG_FAILED + 1)
static const struct mmi_name evtnames[MAX_EVTNAMES] = {
	{ .n = "DCP link lost",		.t = MMI_EVT_LOST },
	{ .n = "Station reconnected",	.t = MMI_EVT_UP },
	{ .n = "HW ident",		.t = MMI_EVT_INIT },
	{ .n = "Key depressed",		.t = MMI_EVT_PRESS },
	{ .n = "Key released",		.t = MMI_EVT_RELEASE },
	{ .n = "Menu item selected",	.t = MMI_EVT_MENU_SELECT },
	{ .n = "Menu exited",		.t = MMI_EVT_MENU_EXIT },
	{ .n = "Station on-hook",	.t = MMI_EVT_ONHOOK },
	{ .n = "Station off-hook",	.t = MMI_EVT_OFFHOOK },
	{ .n = "Menu item saved",	.t = MMI_EVT_MENU_SAVED },
	{ .n = "Programming failure", 	.t = MMI_EVT_PRG_FAILED }
};

#define MAX_CMDNAMES	(MMI_CMD_UNUSED + 1)
static const struct mmi_name cmdnames[MAX_CMDNAMES] = {
	{ .n = "Undefined",		.t = MMI_CMD_UNDEFINED },
	{ .n = "LED control",		.t = MMI_CMD_LED  },
	{ .n = "Menu item program",	.t = MMI_CMD_PROGRAM },
	{ .n = "Display text",		.t = MMI_CMD_TEXT },
	{ .n = "Force speaker on-hook",	.t = MMI_CMD_ONHOOK },
	{ .n = "Force speaker off-hook",.t = MMI_CMD_OFFHOOK },
	{ .n = "Cease ringing",		.t = MMI_CMD_NORING },
	{ .n = "Beep once",		.t = MMI_CMD_BEEP_ONCE },
	{ .n = "Periodic beep",		.t = MMI_CMD_BEEP },
	{ .n = "Ring once",		.t = MMI_CMD_RING_ONCE },
	{ .n = "Ring type 1",		.t = MMI_CMD_RING1 },
	{ .n = "Ring type 2",		.t = MMI_CMD_RING2 },
	{ .n = "Ring type 3",		.t = MMI_CMD_RING3 },
	{ .n = "Start the timer",	.t = MMI_CMD_TMR_START },
	{ .n = "Stop the timer",	.t = MMI_CMD_TMR_STOP },
	{ .n = "Terminal INIT",		.t = MMI_CMD_INIT }, 
	{ .n = "Identify",		.t = MMI_CMD_IDENTIFY },
	{ .n = "Keypad echo on",	.t = MMI_CMD_ECHO_ON },
	{ .n = "Keypad echo off",	.t = MMI_CMD_ECHO_OFF },
	{ .n = "Scroll down",		.t = MMI_CMD_SCROLL_DOWN },
	{ .n = "Reset scroll",		.t = MMI_CMD_RESET_SCROLL },
	{ .n = "Keypad to DTMF",	.t = MMI_CMD_KEYPAD_DTMF },
	{ .n = "Keypad to events",	.t = MMI_CMD_KEYPAD_EVENTS },
	{ .n = "Set ring pattern 1",	.t = MMI_CMD_RING_PATTERN1 },
	{ .n = "Set ring pattern 2",	.t = MMI_CMD_RING_PATTERN2 },
	{ .n = "Set ring pattern 3",	.t = MMI_CMD_RING_PATTERN3 },
	{ .n = "Set ring pattern 4",	.t = MMI_CMD_RING_PATTERN4 },
	{ .n = "Set ring pattern 5",	.t = MMI_CMD_RING_PATTERN5 },
	{ .n = "Set ring pattern 6",	.t = MMI_CMD_RING_PATTERN6 },
	{ .n = "Set ring pattern 7",	.t = MMI_CMD_RING_PATTERN7 },
	{ .n = "Set ring pattern 8",	.t = MMI_CMD_RING_PATTERN8 },
	{ .n = "UNUSED COMMAND",	.t = MMI_CMD_UNUSED }
};

#define MAX_COLORNAMES	(LED_COLOR_WHITE + 1)
static const struct mmi_name colornames[MAX_COLORNAMES] = {
	{ .n = "Green",	.t = LED_COLOR_GREEN },
	{ .n = "Red",	.t = LED_COLOR_RED },
	{ .n = "Blue",	.t = LED_COLOR_BLUE },
	{ .n = "Yellow",.t = LED_COLOR_YELLOW },
	{ .n = "White",	.t = LED_COLOR_WHITE }
};

#define MAX_MODENAMES	(LIGHT_FLUTTER + 1)
static const struct mmi_name modenames[MAX_MODENAMES] = {
	{ .n = "Off",		.t = LIGHT_OFF },
	{ .n = "Steady",	.t = LIGHT_STEADY },
	{ .n = "Darkening",	.t = LIGHT_DARKENING },
	{ .n = "Slow flash",	.t = LIGHT_SLOWFLASH },
	{ .n = "Medium flash",	.t = LIGHT_MEDIUMFLASH },
	{ .n = "Fast flash",	.t = LIGHT_FASTFLASH },
	{ .n = "Flutter",	.t = LIGHT_FLUTTER }
};

#define MAX_ERASENAMES (MMI_ERASE_ALL + 1)
static const struct mmi_name erasenames[MAX_ERASENAMES] = {
	{ .n = "No erase",			.t = MMI_ERASE_NONE },
	{ .n = "Erase to the end of line",	.t = MMI_ERASE_TAIL },
	{ .n = "Erase to the beginning of line",.t = MMI_ERASE_HEAD },
	{ .n = "Erase whole line",		.t = MMI_ERASE_LINE },
	{ .n = "Erase display",			.t = MMI_ERASE_ALL }
};

static const char *search_name(const struct mmi_name *list, int idx, int maxcount)
{
	assert(list);
	static const char *neg = "IDX NEGATIVE";
	static const char *toolarge = "IDX TOO LARGE";
	static const char *err = "INCONSISTENT TABLE";
	
	if (idx < 0) return neg;
	if (idx > maxcount) return toolarge;
	if (list[idx].t != idx) {
		SYMERROR("inconsistent table: idx=%d, %p[%d].t=%d\n", 
			idx, list, idx, list[idx].t);
		return err;
	}
	return list[idx].n;
}

#if 0
static const char *mmi_ctl2txt(int ctl)
{
	return search_name(ctlnames, ctl, MAX_CTLNAMES - 1);
}
#endif

const char *mmi_evt2txt(int evttype)
{
	return search_name(evtnames, evttype, MAX_EVTNAMES - 1);
}

const char *mmi_cmd2txt(int cmdtype)
{
	return search_name(cmdnames, cmdtype, MAX_CMDNAMES - 1);
}

const char *mmi_color2txt(int color)
{
	return search_name(colornames, color, MAX_COLORNAMES - 1);
}

const char *mmi_mode2txt(int mode)
{
	return search_name(modenames, mode, MAX_MODENAMES - 1);
}

const char *mmi_erase2txt(int erase)
{
	return search_name(erasenames, erase, MAX_ERASENAMES - 1);
}

void print_mmicmd(struct mmi_command *cmd)
{
	char	*station = NULL;
	char	*control = NULL;
	char	*text = NULL;
	const char 	*type = NULL;
	if (!cmd) {
		SYMPRINTF("NULL mmi cmd\n");
		return;
	}
	type = mmi_cmd2txt(cmd->type);
	if (cmd->station) station = bstr2cstr(cmd->station, '_');
	if (cmd->ctlname) control = bstr2cstr(cmd->ctlname, '_');
	SYMPRINTF("%%MMI cmd: %s, station: %s, ctlname: %s\n", 
			type, station, control);
	switch (cmd->type) {
		case MMI_CMD_LED:
			SYMPRINTF("-MMI led color: %s, mode: %s\n", 
				mmi_color2txt(cmd->arg.led_arg.color),
				mmi_mode2txt(cmd->arg.led_arg.mode));
			break;
		case MMI_CMD_PROGRAM:
			text = bstr2cstr(cmd->arg.program_arg.text, '_');
			SYMPRINTF("-MMI item #%d, text: %s\n", 
				cmd->arg.program_arg.number, text);
			break;
		case MMI_CMD_TEXT:
			text = bstr2cstr(cmd->arg.text_arg.text, '_');
			SYMPRINTF("-MMI display @ row: %d, col %d; %s, Text: '%s'\n",
				cmd->arg.text_arg.row,
				cmd->arg.text_arg.col,
				(mmi_erase2txt(cmd->arg.text_arg.erase)), text);
			break;
		default:
			break;
	}
	if (station) bcstrfree(station);
	if (control) bcstrfree(control);
	if (text) bcstrfree(text);
}

void print_mmievt(struct mmi_event *evt)
{
	char	*station = NULL;
	char	*control = NULL;
	const char 	*type = NULL;

	if (!evt) {
		SYMPRINTF("NULL mmi evt\n");
		return;
	}
	
	type = mmi_evt2txt(evt->type);
	station = bstr2cstr(evt->station, '_');
	control = bstr2cstr(evt->ctlname, '_');
	SYMPRINTF("%%MMI evt: %s, station: %s, ctlname: %s\n", 
			type, station, control);
	if (station) bcstrfree(station);
	if (control) bcstrfree(control);

}


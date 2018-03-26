/*
 * Copyright 2017,2018 Stacy <stacy@sks.uz> 
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


#ifndef MMI_HDR_LOADED_
#define MMI_HDR_LOADED_
#include <stdbool.h>
#include <bstrlib.h>

enum mmi_ctltype {
	MMI_BLF_KEY,
	MMI_MENU_ITEM,
	MMI_DISPLAY,
	MMI_FUNC_KEY,
	MMI_HOOKSWITCH,
	MMI_TIMER,
	MMI_SPEAKER,
	MMI_KEYPAD,
	MMI_RINGER,
	MMI_LED
};

enum mmi_event_type {
	MMI_EVT_LOST,	/* station DCP link dropped  */
	MMI_EVT_UP,	/* link is up */
	MMI_EVT_INIT,	/* station reconnected and requires init */
	MMI_EVT_PRESS,	/* key press */
	MMI_EVT_RELEASE,	/* key released */
	MMI_EVT_MENU_SELECT,	/* menu item selected */
	MMI_EVT_MENU_EXIT,	/* menu exited */
	MMI_EVT_ONHOOK,		/* station on hook */
	MMI_EVT_OFFHOOK,	/* station off hook */
	MMI_EVT_MENU_SAVED,	/* menu programming command ok */
	MMI_EVT_PRG_FAILED
};

enum mmi_cmd_type {
	MMI_CMD_UNDEFINED = 0,
	MMI_CMD_LED = 1,	
	MMI_CMD_PROGRAM = 2,	/* program menu item */
	MMI_CMD_TEXT = 3,		/* display text */
	MMI_CMD_ONHOOK = 4,		/* forced speaker onhook */
	MMI_CMD_OFFHOOK = 5,	/* forced speaker offhook */
	MMI_CMD_NORING = 6,		/* silence the ringer */
	MMI_CMD_BEEP_ONCE = 7,
	MMI_CMD_BEEP = 8,
	MMI_CMD_RING_ONCE = 9,
	MMI_CMD_RING1 = 10,
	MMI_CMD_RING2 = 11,
	MMI_CMD_RING3 = 12,
	MMI_CMD_TMR_START = 13,	/* start the timer */
	MMI_CMD_TMR_STOP = 14,	/* stop the timer */
	MMI_CMD_INIT = 15,
	MMI_CMD_IDENTIFY = 16,	/* request terminal config */
	MMI_CMD_ECHO_ON = 17,	/* enable keypad echo */
	MMI_CMD_ECHO_OFF = 18,	/* disable keypad echo */
	MMI_CMD_SCROLL_DOWN = 19,
	MMI_CMD_RESET_SCROLL = 20,
	MMI_CMD_KEYPAD_DTMF = 21,	/* switch keypad to dtmf mode */
	MMI_CMD_KEYPAD_EVENTS = 22,	/* switch keypad to events mode */
	MMI_CMD_RING_PATTERN1 = 23,
	MMI_CMD_RING_PATTERN2 = 24,
	MMI_CMD_RING_PATTERN3 = 25,
	MMI_CMD_RING_PATTERN4 = 26,
	MMI_CMD_RING_PATTERN5 = 27,
	MMI_CMD_RING_PATTERN6 = 28,
	MMI_CMD_RING_PATTERN7 = 29,
	MMI_CMD_RING_PATTERN8 = 30,
	MMI_CMD_UNUSED
};

enum mmi_led_colors {
	LED_COLOR_GREEN,
	LED_COLOR_RED,
	LED_COLOR_BLUE,
	LED_COLOR_YELLOW,
	LED_COLOR_WHITE
};

enum mmi_led_modes {
	LIGHT_OFF,
	LIGHT_STEADY,
	LIGHT_DARKENING,
	LIGHT_SLOWFLASH,
	LIGHT_MEDIUMFLASH,
	LIGHT_FASTFLASH,
	LIGHT_FLUTTER
};
/* led control */
struct mmi_led_arg {
	int	color;
	int	mode;
};

/* menu programming */
struct mmi_program_arg {
	bstring	text;
	int	number;
};

enum mmi_text_erase {
	MMI_ERASE_NONE = 0,
	MMI_ERASE_TAIL,
	MMI_ERASE_HEAD,
	MMI_ERASE_LINE,
	MMI_ERASE_ALL
};

#define TEXT_CONTINUE	(-1)
/* display text argument */
struct mmi_text_arg {
	bstring	text;
	int	row;	/* do not position if row or col are set to TEXT_CONTINUE */
	int	col;
	int	erase;
};

struct mmi_event {
	bstring station;
	bstring ctlname;
	int	type;
};


union mmi_cmdarg {
	struct mmi_led_arg led_arg;
	struct mmi_program_arg program_arg;
	struct mmi_text_arg text_arg;
};

struct mmi_command {
	bstring station;
	bstring	ctlname;
	int	type;
	union mmi_cmdarg arg;
};

/* free all cmd components */
void mmi_cmd_erase(struct mmi_command *cmd);

/* free all evt components */
void mmi_evt_erase(struct mmi_event *evt);


#endif /* MMI_HDR_LOADED_ */

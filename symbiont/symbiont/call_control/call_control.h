/*
* Copyright 2017,2018  Stacy <stacy@sks.uz>
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
#ifndef CALL_CONTROL_HDR_LOADED_
#define CALL_CONTROL_HDR_LOADED_
#include <symbiont/yxtlink.h>
#include <symbiont/mmi.h>
#include <stdint.h>
#include <stdbool.h>


#define CCTL_PROCESSED	1
#define CCTL_CHANGED	2
#define CCTL_OK		0
#define	CCTL_FAIL	(-1)

#define LINE_MAGIC	(0xab155f54)

/* lines print it's letter on display when selected
 * so there can be at most 26 lines (from a to z)
 */
#define MAX_LINE_NUMBER	26
#define MAX_NUMBER_LENGTH	32

enum symline_state {
	SL_STATE_DISABLED = 0,	/* administratively  disabled */
	SL_STATE_IDLE,		/* waiting for events */
	SL_STATE_DIALTONE,	/* off hook - provide dialtone */
	SL_STATE_COLLECTING,	/* collecting digits - adter first one, no dialtone */
	SL_STATE_CALLING,	/* Number complete, calling */
	SL_STATE_CONNECTED,	/* call connected, talking */
	SL_STATE_HOLD,		/* line is on hold */
	SL_STATE_SIT,		/* remote side hung up, or anything wrong */
	SL_STATE_INCOMING,	/* incoming line seized, but no channel yet */
	SL_STATE_RING,		/* incoming call rings */
	SL_STATE_DROP,		/* drop button pressed, timeout before new dialtone */
	SL_STATE_UNUSED
};

struct station;

struct symline {
	uint32_t magic;
	pthread_mutex_t lmx;	/* this structure access mutex */
	struct 	ccstation *ccst;
	char	*name;		/* line name like rifleshop/139 etc*/
	char	*displayname;	/* caller displayed name */
	int	number;		/* a letter number (e.g. a= )*/
	int	state;
	bool	offhook;	/* station is off-hook */
	bool	outgoing;
	bool	softecho;	/* software will echo the number */
	bool	pad_offhook;	/* pressing any keypad button causes auto off-hook */
	bool	evtsend;	/* sends dcp.event for blf button */
	bool	cmdrcv;		/* accepts dcp.command for blf button (led control) */
	bool	xcontrol;	/* accepts symbiont.command */
	bool	selected;
	char	digits[MAX_NUMBER_LENGTH];
	int	digits_len;
	char	*ourcallid;
	char	*partycallid;
	char	*calltrack;
	char	*callername;
	char	*caller;
	timer_t	tmr;		/* line timer for various purposes */
};

/* initialize line  */
struct symline *new_symline(void);

/* administrative controls - enable/disable call processing */
int cctl_enable(struct symline *sl);
int cctl_disable(struct symline *sl);


/* all types of off-hook - blf press, unhold etc */
int cctl_select(struct symline *sl);

/* put the line on hold 
 * it will return the error and do nothing 
 * if line is not in holdable state - eg. dialing, ringing etc
 * dialing 
 */
int cctl_hold(struct symline *sl);
/* hangup */
int cctl_hangup(struct symline *sl);

/* almost same as hold but in case the line is non-holdable:
 * it hangs-up if line is dialing and just leaves it as it is if it's ringing
 */
int cctl_unselect(struct symline *sl);

void cctl_process_evt(struct symline *sl, struct mmi_event *evt);

/* returns CCTL_PROCESSED or CCTL_OK or CCTL_FAIL */
int cctl_process_msg(struct symline *sl, yatemsg *msg, struct dwhook *dwh);

bool cctl_isline(void *ptr);

#endif /* CALL_CONTROL_HDR_LOADED_ */

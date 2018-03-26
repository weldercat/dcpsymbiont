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
#define _GNU_SOURCE	1
#include <assert.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

//#define	DEBUG_ME_HARDER		1

#include <symbiont/symerror.h>
#include <symbiont/yxtlink.h>


#include "cctl_misc.h"
#include "call_control.h"

bool timer_armed(timer_t timer)
{
	struct itimerspec tval;
	int	res;
	bool	armed = false;
	
	memset(&tval, 0, sizeof(struct itimerspec));
	res = timer_gettime(timer, &tval);
	if (res) SYMFATAL("cannot get timer value:%s\n", STRERROR_R(errno));
	assert(tval.it_interval.tv_sec == 0);
	assert(tval.it_interval.tv_nsec == 0);
	armed = ((tval.it_value.tv_sec != 0) || (tval.it_value.tv_nsec != 0));
	return armed;
}

void reset_timer(timer_t timer, int msecs)
{
	struct itimerspec tval;
	int	res = 0;
	
	assert(msecs >= 0);
	memset(&tval, 0, sizeof(struct itimerspec));
	tval.it_value.tv_sec = (msecs / 1000);
	tval.it_value.tv_nsec = (msecs % 1000) * 1000000;
	res = timer_settime(timer, 0, &tval, NULL);
	if (res) SYMFATAL("cannot set timer:%s\n", STRERROR_R(errno));
}


bool check_ctlname(struct mmi_event *evt, char *name)
{
	char	*p;

	assert(evt);
	assert(name);
	if (blength(evt->ctlname) <= 0) return false;
	p = bdata(evt->ctlname);
	if (strcmp(p, name) == 0) return true;
	return false;
}


int get_blfno(struct mmi_event *evt)
{
	char	*p;
	int	res;

	assert(evt);
	if (blength(evt->ctlname) < 4) return -1;
	p = bdata(evt->ctlname);
	if (strncmp(p, "blf", 3) == 0) {
		res = atoi(p + 3);
		if (!res) return -1;
		return res;
	}
	return -1;
}

const char *decode_linestate(int state)
{
	struct state_desc {
		int	state;
		const char *name;
	};

	static struct state_desc snames[] = {
		{ .state = SL_STATE_DISABLED,	.name = "DISABLED" },
		{ .state = SL_STATE_IDLE,	.name = "IDLE" },
		{ .state = SL_STATE_DIALTONE,	.name = "DIALTONE" },
		{ .state = SL_STATE_COLLECTING,	.name = "COLLECTING" },
		{ .state = SL_STATE_CALLING, .name = "CALLING" },
		{ .state = SL_STATE_CONNECTED, .name = "CONNECTED" },
		{ .state = SL_STATE_HOLD, .name = "ON_HOLD" },
		{ .state = SL_STATE_SIT, .name = "SIT" },
		{ .state = SL_STATE_INCOMING, .name = "INCOMING" },
		{ .state = SL_STATE_RING, .name = "RINGING" },
		{ .state = SL_STATE_DROP, .name = "DROPPED" },
		{ .state = -1, .name = NULL }
	};

	if ((state >= SL_STATE_UNUSED) || (state < 0)) return "unknown";
	return (snames[state].name);
}



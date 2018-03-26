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

#ifndef STATION_CONTROL_HDR_LOADED_
#define STATION_CONTROL_HDR_LOADED_
#include <stdint.h>
#include <symbiont/filter.h>

#define STATION_MAGIC	(0x16963084)


enum ccstation_state {
	ST_STATE_DISABLED = 0,
	ST_STATE_INIT,
	ST_STATE_RUNNING,
	ST_STATE_UNUSED
};

typedef int (*mmi_sender)(struct mmi_command *cmd, void *arg);

#define MAX_LINES	26
#if (MAX_LINES >= 32)
#error MAX_LINES must be less than 32
#endif

struct ccstation {
	uint32_t magic;
	pthread_rwlock_t sml;
	char	*name;		/* station name */
	char	*bchan;
	bool	autostart;	/* will be enabled automatically upon symbiont start */
	int	ifi;
	int	state;
	int	numlines;	/* number of lines configured, no larger than MAX_LINES */
	int	selected;	/* currently selected line */
	int	defselect;	/* default selected line, -1 for sticky selection */
	uint32_t	ring1_mask;
	uint32_t	ring2_mask;
	uint32_t	ring3_mask;
	uint32_t	beep_mask;
	pthread_mutex_t rmask_mx;
	struct symline	*lines[MAX_LINES];
	timer_t	tmr;		/* station timer for various purposes */
	mmi_sender	mmi_cb;
	void	*mmi_cb_arg;
	struct filter	*filter;

};


void st_process_evt(struct ccstation *st, struct mmi_event *evt);

int st_mmi_send(struct ccstation *st, struct mmi_command *cmd, int lineno);

int st_enable(struct ccstation *st);

int st_disable(struct ccstation *st);

struct ccstation *new_ccstation(void);

int st_add_line(struct ccstation *st, struct symline *sl);

/* returns true if this event is allowed to cause dcp.event message */
bool st_evt_filter_pass(struct ccstation *st, struct mmi_event *evt);

/* returns true if this command is allowed to be sent to station*/
bool st_cmd_filter_pass(struct ccstation *st, struct mmi_command *cmd);


/* symline calls this hook when it goes to idle 
 * station control code must decide what line to select 
 * instead (if any). line unselects itself if this callback
 * returns SYM_OK and does nothing otherwise.
 */
int st_unselect_me(struct ccstation *st, struct symline *sl);

/* unsafe - doesn't lock the *st */
struct symline *st_get_line_nolock(struct ccstation *st, int number);

struct symline *st_get_line(struct ccstation *st, int number);

bool st_isccstation(void *ptr);

int free_ccstation(struct ccstation *st);

#endif /* STATION_CONTROL_HDR_LOADED_ */


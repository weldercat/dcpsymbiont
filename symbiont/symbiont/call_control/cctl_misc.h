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

#ifndef CCTL_MISC_HDR_LOADED_
#define CCTL_MISC_HDR_LOADED_
#include <time.h>

#include <symbiont/mmi.h>


bool timer_armed(timer_t timer);
void reset_timer(timer_t timer, int msecs);
bool check_ctlname(struct mmi_event *evt, char *name);
int get_blfno(struct mmi_event *evt);
const char *decode_linestate(int state);


#endif /* CCTL_MISC_HDR_LOADED_ */


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
#include <assert.h>
#include <symbiont/mmi.h>


/* free all cmd components */
void mmi_cmd_erase(struct mmi_command *cmd)
{
	assert(cmd);
	if (cmd->station) {
		bdestroy(cmd->station);
		cmd->station = NULL;
	}
	if (cmd->ctlname) {
		bdestroy(cmd->ctlname);
		cmd->ctlname = NULL;
	}
	switch (cmd->type) {
		case MMI_CMD_PROGRAM:
			if (cmd->arg.program_arg.text) bdestroy(cmd->arg.program_arg.text);
			cmd->arg.program_arg.text = NULL;
			break;
		case MMI_CMD_TEXT:
			if (cmd->arg.text_arg.text) bdestroy(cmd->arg.text_arg.text);
			break;
		default:
			break;
	}
	memset(cmd, 0, sizeof(struct mmi_command));
}

/* free all evt components */
void mmi_evt_erase(struct mmi_event *evt)
{
	assert(evt);
	if (evt->station) {
		bdestroy(evt->station);
		evt->station = NULL;
	}
	if (evt->ctlname) {
		bdestroy(evt->ctlname);
		evt->ctlname = NULL;
	}
	evt->type = 0;
}



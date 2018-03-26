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
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <symbiont/symerror.h>
#include <symbiont/cfload.h>
#include <symbiont/cfdb.h>
#include <symbiont/symbiont.h>



conn_ctx	*yctx = NULL;

struct global_params gp;
cfdb	*confdb = NULL;



int main(int argc, char **argv)
{
	int	res;

	char	*cfgfile = NULL;
	if ((argc < 2) || (!argv[1])) {
		SYMERROR("usage: cfload_test <cfg_file_name>\n");
		return 1;
	}
	cfgfile = argv[1];
	assert(cfgfile);
	SYMINFO("Loading config file %s...\n", cfgfile);


	confdb = new_cfdb();
	assert(confdb);
	memset(&gp, 0, sizeof(struct global_params));
	res = cfload(confdb, &gp, cfgfile);
	
	if (res != SYM_OK) {
		SYMERROR("error loading config (%d)\n", res);
		return 2;
	}

	return 0;
}


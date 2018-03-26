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


#ifndef CFLOAD_HDR_LOADED_
#define CFLOAD_HDR_LOADED_
#include <symbiont/symerror.h>
#include <symbiont/cfdb.h>
#include <stdbool.h>


struct global_params {
	char	*symname;
	char	*hualink;	/* HUA connect string */
	char	*yxtlink;	/* YXT connect string */
	bool	yxt_unix;	/* is YXT string a unix socket path ? */
	int	debuglevel;	/* debuglevel */
};


int	cfload(cfdb *confdb, struct global_params *gcf, char *configfile);




#endif /* CFLOAD_HDR_LOADED_ */
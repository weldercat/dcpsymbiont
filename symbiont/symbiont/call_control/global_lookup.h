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
#ifndef GLOBAL_LOOKUP_HDR_LOADED_
#define GLOBAL_LOOKUP_HDR_LOADED_
#include <symbiont/yxtlink.h>
#include "call_control.h"
#include "station_control.h"

conn_ctx *yctx_for_line(struct symline *sl);
struct ccstation *ccstation_for_ifi(int ifi);
struct ccstation *ccstation_by_name(char *name);

#define CALLHASH_BUCKETS	100
enum calldb_id_type {
	CDB_ID_OUR = 0,
	CDB_ID_PARTY,
	CDB_ID_CALLTRACK
};

struct symline *lookup_cdb(int idtype, char *id);
int update_cdb(int idtype, char *id, struct symline *sl);
void init_cdb(void);

#endif /* GLOBAL_LOOKUP_HDR_LOADED_ */

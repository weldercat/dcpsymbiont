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
#define _GNU_SOURCE	1
#include <symbiont/symerror.h>
#include <symbiont/yxtlink.h>
#include "global_lookup.h"
#include "call_control.h"
#include <symbiont/strmap.h>
#include <pthread.h>
#define NOFAIL_MUTEX_UNNEEDED	1
#include <symbiont/nofail_wrappers.h>
#include <symbiont/cfdb.h>

extern conn_ctx	*yctx;
extern cfdb *confdb;

struct calldb {
	pthread_rwlock_t lock;
	strmap	*ourcid;
	strmap	*partycid;
	strmap	*ctrackid;
};


static struct calldb *cdb = NULL;

static strmap *map_from_type(int idtype);



void init_cdb(void)
{
	int	res;
	
	if (cdb) return;
	cdb = malloc(sizeof(struct calldb));
	assert(cdb);
	memset(cdb, 0, sizeof(struct calldb));
	res = pthread_rwlock_init(&cdb->lock, NULL);
	if (res) {
		SYMFATAL("cannot init call database: %s\n", STRERROR_R(res));
	}
	assert(res == 0);
	cdb->ourcid = sm_new(CALLHASH_BUCKETS);
	assert(cdb->ourcid);

	cdb->partycid = sm_new(CALLHASH_BUCKETS);
	assert(cdb->partycid);

	cdb->ctrackid = sm_new(CALLHASH_BUCKETS);
	assert(cdb->ctrackid);
}

static strmap *map_from_type(int idtype)
{
	strmap *map = NULL;

	assert(cdb);
	switch (idtype) {
		case CDB_ID_OUR:
			map = cdb->ourcid;
			break;
		case CDB_ID_PARTY:
			map = cdb->partycid;
			break;
		case CDB_ID_CALLTRACK:
			map = cdb->ctrackid;
			break;
		default:
			break;
	}
	return map;
}


struct symline *lookup_cdb(int idtype, char *id)
{
	int	len;
	strmap	*map;
	void *sl = NULL;

	assert(id);
	assert(cdb);
	len = strlen(id);
	if (len <= 0) return NULL;
	lock_read(&cdb->lock);
	map = map_from_type(idtype);
	assert(map);
	sl = sm_get(map, (unsigned char *)id, len);
	lock_unlock(&cdb->lock);
	return (struct symline *)sl;
}

int update_cdb(int idtype, char *id, struct symline *sl)
{
	int	len;
	int	res;
	strmap	*map;

	assert(id);
	assert(cdb);
	len = strlen(id);
	if (len <= 0) return SYM_FAIL;
	lock_write(&cdb->lock);
	map = map_from_type(idtype);
	assert(map);
	res = sm_put(map, (unsigned char *)id, len, sl);
	lock_unlock(&cdb->lock);
	return res;
}


/* stubs */

conn_ctx *yctx_for_line(struct symline *sl)
{
	assert(sl);
	return yctx;
}


struct ccstation *ccstation_by_name(char *name)
{
	struct ccstation *st;
	
	if (name) {
		st = (struct ccstation *)cfg_lookupname(confdb, CFG_OBJ_STATION, name);
		return st;
	}
	return NULL;
}


struct ccstation *ccstation_for_ifi(int ifi)
{
	struct ccstation *st;
	
	st = (struct ccstation *)cfg_lookupifi(confdb, CFG_OBJ_STATION, ifi);
	return st;
}


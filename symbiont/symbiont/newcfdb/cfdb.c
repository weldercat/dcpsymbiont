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

#include <symbiont/symerror.h>
#define NOFAIL_MUTEX_UNNEEDED	1
#include <symbiont/nofail_wrappers.h>
#include <symbiont/call_control.h>
#include <symbiont/station_control.h>
#include "cfdb.h"

static int register_station(cfdb *cfg, void *object);
static int register_line(cfdb *cfg, void *object);
static int register_filter(cfdb *cfg, void *object);

static int remove_station(cfdb *cfg, void *object);
static int remove_line(cfdb *cfg, void *object);
static int remove_filter(cfdb *cfg, void *object);


static void verify_type(void *object, int objtype);
static int iter_wrap(const unsigned char *key, int keylen, void *value, void *arg);

struct iter_params {
	int	objtype;
	cfg_iterator ci;
	void	*arg;
};


cfdb *new_cfdb(void)
{
	cfdb	*cfg = NULL;
	int	res;
	
	cfg = malloc(sizeof(cfdb));
	if (!cfg) {
		SYMERROR("cannot allocate memory\n");
		return NULL;
	}
	memset(cfg, 0, sizeof(cfdb));
	cfg->stnamehash = sm_new(CFG_ST_INICOUNT);
	if (!cfg->stnamehash) {
		SYMERROR("cannot initialize station name hash\n");
		goto errout;
	}
	cfg->stvchash = sm_new(CFG_ST_INICOUNT);
	if (!cfg->stvchash) {
		SYMERROR("cannot initialize station voice circuit name hash\n");
		goto errout;
	}
	cfg->stifihash = sm_new(CFG_ST_INICOUNT);
	if (!cfg->stifihash) {
		SYMERROR("cannot initialize station ifi hash\n");
		goto errout;
	}
	cfg->slhash = sm_new(CFG_SL_INICOUNT);
	if (!cfg->slhash) {
		SYMERROR("cannot initialize line name hash\n");
		goto errout;
	}

	cfg->flthash = sm_new(CFG_FLT_INICOUNT);
	if (!cfg->flthash) {
		SYMERROR("cannot initialize filter name hash\n");
		goto errout;
	}
	res = pthread_rwlock_init(&cfg->cfl, NULL);
	if (res) {
		SYMERROR("cannot initialize cfg db rwlock: %s\n", STRERROR_R(res));
		goto errout;
	}
	return cfg;
errout:
	if (cfg) {
		if (cfg->stnamehash) sm_delete(cfg->stnamehash);
		if (cfg->stvchash) sm_delete(cfg->stvchash);
		if (cfg->stifihash) sm_delete(cfg->stifihash);
		if (cfg->slhash) sm_delete(cfg->slhash);
		if (cfg->flthash) sm_delete(cfg->flthash);
		free(cfg);
	}
	return NULL;
}

int cfg_register(cfdb *cfg, int objtype, void *object)
{
	int	res;
	
	assert(cfg);
	assert(object);
	
	switch (objtype) {
		case CFG_OBJ_STATION:
			res = register_station(cfg, object);
			break;
		case CFG_OBJ_LINE:
			res = register_line(cfg, object);
			break;
		case CFG_OBJ_FILTER:
			res = register_filter(cfg, object);
			break;
		default:
			SYMERROR("invalid object type: %d\n", objtype);
			return CFDB_FAIL;
			break;
	}
	return res;
}

static int register_station(cfdb *cfg, void *object)
{
	int	res = CFDB_FAIL;
	struct ccstation *st;
	int	len;
	int	vclen;
	
	assert(cfg);
	assert(object);
	st = (struct ccstation*)object;
	if (!st_isccstation(st)) {
		SYMERROR("invalid magic - object @%p is not a struct ccstation\n", st);
		return res;
	}
	if (!st->name) {
		SYMERROR("cannot register station with no name\n");
		return res;
	}
	len = strlen(st->name);
	if (len <= 0) {
		SYMERROR("cannot register station with empty name\n");
		return res;
	}
	vclen = strlen(st->bchan);
	if (vclen <= 0) {
		SYMERROR("cannot register station with empty B-chan\n");
		return res;
	}
	if (st->ifi <= 0) {
		SYMERROR("cannot register station with invalid ifi\n");
		return res;
	}
	lock_write(&cfg->cfl);
	if (sm_exists(cfg->stnamehash, (const unsigned char *)(st->name), len)) {
		SYMERROR("station %s already registered\n", st->name);
		goto out;
	}
	if (sm_exists(cfg->stvchash, (const unsigned char *)(st->bchan), vclen)) {
		SYMERROR("A station is alreade registered for b-chan %s\n", st->bchan);
		goto out;
	}
	if (sm_exists(cfg->stifihash, (const unsigned char *)(&st->ifi), sizeof(st->ifi))) {
		SYMERROR("station with ifi=%d already registered\n", st->ifi);
		goto out;
	}
	res = sm_put(cfg->stnamehash, (const unsigned char *)(st->name), len, st);
	if (res != STRMAP_OK) {
		SYMERROR("cannot insert into station-by-name index\n");
		res = CFDB_FAIL;
		goto out;
	}
	res = sm_put(cfg->stvchash, (const unsigned char *)(st->bchan), vclen, st);
	if (res != STRMAP_OK) {
		SYMERROR("cannot insert into station-by-bchan index\n");
		res = CFDB_FAIL;
		goto out;
	}
	res = sm_put(cfg->stifihash, (const unsigned char *)(&st->ifi), sizeof(st->ifi), st);
	if (res != STRMAP_OK) {
		SYMERROR("cannot insert into station-by-ifi index\n");
		res = CFDB_FAIL;
	}
	res = CFDB_OK;
out:
	lock_unlock(&cfg->cfl);
	return res;
}

static int register_line(cfdb *cfg, void *object)
{
	int	res = CFDB_FAIL;
	struct symline *sl;
	int	len;
	
	assert(cfg);
	assert(object);
	sl = (struct symline *)object;
	if (!cctl_isline(sl)) {
		SYMERROR("invalid magic - object @%p is not a struct symline\n", sl);
		return res;
	}
	if (!sl->name) {
		SYMERROR("cannot register line with no name\n");
		return res;
	}
	len = strlen(sl->name);
	if (len <= 0) {
		SYMERROR("cannot register line with empty name\n");
		return res;
	}
	lock_write(&cfg->cfl);
	if (sm_exists(cfg->slhash, (const unsigned char *)(sl->name), len)) {
		SYMERROR("line %s is already registered\n", sl->name);
		goto out;
	}
	res = sm_put(cfg->slhash, (const unsigned char *)(sl->name), len, sl);
	if (res != STRMAP_OK) {
		SYMERROR("cannot insert into line index\n");
		res = CFDB_FAIL;
		goto out;
	}
	res = CFDB_OK;
out:
	lock_unlock(&cfg->cfl);
	return res;
}



static int register_filter(cfdb *cfg, void *object)
{
	int	res = CFDB_FAIL;
	struct filter *flt;
	int	len;
	
	assert(cfg);
	assert(object);
	flt = (struct filter *)object;
	if (!flt_isfilter(flt)) {
		SYMERROR("invalid magic - object @%p is not a struct filter\n", flt);
		return res;
	}
	if (!flt->name) {
		SYMERROR("cannot register filter with no name\n");
		return res;
	}
	len = strlen(flt->name);
	if (len <= 0) {
		SYMERROR("cannot register filter with empty name\n");
		return res;
	}
	lock_write(&cfg->cfl);
	if (sm_exists(cfg->flthash, (const unsigned char *)(flt->name), len)) {
		SYMERROR("filter %s is already registered\n", flt->name);
		goto out;
	}
	res = sm_put(cfg->flthash, (const unsigned char *)(flt->name), len, flt);
	if (res != STRMAP_OK) {
		SYMERROR("cannot insert into filter index\n");
		res = CFDB_FAIL;
		goto out;
	}
	res = CFDB_OK;
out:
	lock_unlock(&cfg->cfl);
	return res;
}


int cfg_remove(cfdb *cfg, int objtype, void *object)
{
	int	res;
	
	assert(cfg);
	assert(object);
	
	switch (objtype) {
		case CFG_OBJ_STATION:
			res = remove_station(cfg, object);
			break;
		case CFG_OBJ_LINE:
			res = remove_line(cfg, object);
			break;
		case CFG_OBJ_FILTER:
			res = remove_filter(cfg, object);
			break;
		default:
			SYMERROR("invalid object type: %d\n", objtype);
			return CFDB_FAIL;
			break;
	}
	return res;
}


static int remove_station(cfdb *cfg, void *object)
{
	int	res = CFDB_FAIL;
	int	tmpres;
	struct ccstation *st;
	struct ccstation *cfst;
	int	namelen;
	int	vclen;
	
	assert(cfg);
	assert(cfg->stnamehash);
	assert(cfg->stvchash);
	assert(cfg->stifihash);
	assert(object);
	st = (struct ccstation*)object;
	if (!st_isccstation(st)) {
		SYMERROR("invalid magic - object @%p is not a struct ccstation\n", st);
		return res;
	}
	assert(st->name);
	assert(st->bchan);
	namelen = strlen(st->name);
	vclen = strlen(st->bchan);
	lock_write(&cfg->cfl);
	
	cfst = (struct ccstation *)sm_get(cfg->stnamehash, (const unsigned char *)st->name, namelen);
	if (!cfst) {
		SYMERROR("station \"%s\" is not found in by name - cannot remove\n", st->name);
		goto out;
	}
	if (cfst != st) {
		SYMERROR("a station found by name %s is not equal to the one for removal\n", st->name);
		goto out;
	}
	cfst = (struct ccstation *)sm_get(cfg->stvchash, (const unsigned char *)st->bchan, vclen);

	if (!cfst) {
		SYMERROR("station \"%s\" is not found by bchan - cannot remove\n", st->name);
		goto out;
	}
	if (cfst != st) {
		SYMERROR("a station found by bchan %s is not equal to the one for removal\n", st->bchan);
		goto out;
	}
	
	cfst = (struct ccstation *)sm_get(cfg->stifihash, (const unsigned char *)(&st->ifi), sizeof(st->ifi));

	if (!cfst) {
		SYMERROR("station \"%s\" is not found by ifi- cannot remove\n", st->name);
		goto out;
	}
	if (cfst != st) {
		SYMERROR("a station found by ifi %d is not equal to the one for removal\n", st->ifi);
		goto out;
	}

	
	res = CFDB_OK;
	
	tmpres = sm_put(cfg->stnamehash, (const unsigned char *)(st->name), namelen, NULL);
	if (tmpres != STRMAP_OK) {
		SYMERROR("cannot remove from station-by-name index\n");
		res = CFDB_FAIL;
	}

	tmpres = sm_put(cfg->stvchash, (const unsigned char *)(st->bchan), vclen, NULL);
	if (tmpres != STRMAP_OK) {
		SYMERROR("cannot remove from station-by-bchan index\n");
		res = CFDB_FAIL;
	}

	tmpres = sm_put(cfg->stifihash, (const unsigned char *)(&st->ifi), sizeof(st->ifi), NULL);
	if (tmpres != STRMAP_OK) {
		SYMERROR("cannot remove from station-by-ifi index\n");
		res = CFDB_FAIL;
	}

out:
	lock_unlock(&cfg->cfl);
	return res;
}


static int remove_line(cfdb *cfg, void *object)
{
	SYMFATAL("not implemented\n");
	
	return CFDB_FAIL;
}


static int remove_filter(cfdb *cfg, void *object)
{
	SYMFATAL("not implemented\n");
	
	return CFDB_FAIL;
}



static void verify_type(void *object, int objtype)
{
	bool res = false;
	
	assert(object);
	switch (objtype) {
		case CFG_OBJ_STATION:
			res = st_isccstation(object);
			break;
		case CFG_OBJ_LINE:
			res = cctl_isline(object);
			break;
		case CFG_OBJ_FILTER:
			res = flt_isfilter(object);
			break;
		default:
			SYMFATAL("unsupported object type %d\n", objtype);
			break;
	}
	if (!res) SYMFATAL("object type mismatch for type %d at %p\n", 
		objtype, object);
}


void *cfg_lookupname(cfdb *cfg, int objtype, char *name)
{
	strmap	*map = NULL;
	int	len;
	void	*object = NULL;
	
	assert(cfg);
	assert(name);
	len = strlen(name);
	assert(len > 0);
	
	lock_read(&cfg->cfl);
	switch (objtype) {
		case CFG_OBJ_STATION:
			map = cfg->stnamehash;
			break;
		case CFG_OBJ_LINE:
			map = cfg->slhash;
			break;
		case CFG_OBJ_FILTER:
			map = cfg->flthash;
			break;
		default:
			break;
	}
	if (map) {
		object = sm_get(map, (const unsigned char *)name, len);
		if (!object) {
			SYMDEBUG("no object named %s\n", name);
		}
	}
	if (object) verify_type(object, objtype);
	lock_unlock(&cfg->cfl);
	return object;
}



void *cfg_lookupifi(cfdb *cfg, int objtype, int ifi)
{
	strmap	*map = NULL;
	void	*object = NULL;
	
	assert(cfg);
	if (objtype != CFG_OBJ_STATION) {
		SYMFATAL("no ifi index for type %d", objtype);
		return NULL;
	}
	
	lock_read(&cfg->cfl);
	map = cfg->stifihash;
	if (map) {
		object = sm_get(map, (const unsigned char *)(&ifi), (sizeof(ifi)));
		if (!object) {
			SYMDEBUG("no object with ifi %d\n", ifi);
		}
	}
	if (object) verify_type(object, objtype);
	lock_unlock(&cfg->cfl);
	return object;
}


void *cfg_lookupvc(cfdb *cfg, int objtype, char *vcname)
{
	strmap	*map = NULL;
	void	*object = NULL;
	int	len;
	
	assert(cfg);
	assert(vcname);
	if (objtype != CFG_OBJ_STATION) {
		SYMFATAL("no bchan index for type %d", objtype);
		return NULL;
	}
	len = strlen(vcname);
	assert(len > 0);
	lock_read(&cfg->cfl);
	map = cfg->stvchash;
	if (map) {
		object = sm_get(map, (const unsigned char *)vcname, len);
		if (!object) {
			SYMDEBUG("no object with bchan %s\n", vcname);
			object = NULL;
		}
	}
	if (object) verify_type(object, objtype);
	lock_unlock(&cfg->cfl);
	return object;
}

static int iter_wrap(const unsigned char *key, int keylen, void *value, void *arg)
{
	struct iter_params *ip;
	int	res;
	
	assert(arg);
	SYMDEBUGHARD("key=%s, keylen=%d\n", key, keylen);
	ip = (struct iter_params *)arg;
	switch (ip->objtype) {
		case CFG_OBJ_STATION:
			res = st_isccstation(value);
			break;
		case CFG_OBJ_LINE:
			res = cctl_isline(value);
			break;
		case CFG_OBJ_FILTER:
			res = flt_isfilter(value);
			break;
		default:
			res = 1;
			break;
	}
	assert(res != 0);
	res = (ip->ci)(ip->objtype, value, ip->arg);
	return res;
}


int cfg_iterate(cfdb *cfg, int objtype, cfg_iterator iter, void *arg)
{
	strmap *map = NULL;
	int	res = CFDB_FAIL;
	struct iter_params ip;
	
	assert(cfg);
	assert(iter);
	ip.objtype = objtype;
	ip.ci = iter;
	ip.arg = arg;
	lock_read(&cfg->cfl);
	switch (objtype) {
		case CFG_OBJ_STATION:
			map = cfg->stnamehash;
			break;
		case CFG_OBJ_LINE:
			map = cfg->slhash;
			break;
		case CFG_OBJ_FILTER:
			map = cfg->flthash;
			break;
		default:
			break;
	}
	if (map) {
		res = sm_iterate(map, iter_wrap, &ip);
	} else {
		SYMERROR("don't know how to iterate objects of type %d\n", objtype);
		res = CFDB_FAIL;
	}
	lock_unlock(&cfg->cfl);
	return res;
}

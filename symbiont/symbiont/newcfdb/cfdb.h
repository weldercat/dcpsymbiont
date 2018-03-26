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

#ifndef CFDB_HDR_LOADED_
#define CFDB_HDR_LOADED_
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <symbiont/strmap.h>

#define CFDB_OK		0
#define CFDB_FAIL	(-1)

/* station hash buckets  count */
#define CFG_ST_INICOUNT	100

/* lines hash bucket count */
#define CFG_SL_INICOUNT 500

/* filter hash buckets count */
#define CFG_FLT_INICOUNT	20


typedef struct cfdb_s {
	pthread_rwlock_t cfl;
	strmap *stnamehash;	/* stations by name hash*/
	strmap *stvchash;	/* stations by voice circuit (bchan) hash */
	strmap *stifihash;	/* stations by ifi hash */
	strmap *slhash;	/* lines by name hash */
	strmap *flthash;	/* filters by name hash */
} cfdb;


cfdb *new_cfdb(void);

enum cfg_objtype {
	CFG_OBJ_INVALID = 0,
	CFG_OBJ_STATION,
	CFG_OBJ_LINE,
	CFG_OBJ_FILTER
};

int cfg_register(cfdb *cfg, int objtype, void *object);
int cfg_remove(cfdb *cfg, int objtype, void *object);

/* iterator must return 0 to continue or any other value to stop */
typedef int (*cfg_iterator)(int objtype, void *object, void *arg);

void *cfg_lookupname(cfdb *cfg, int objtype, char *name);
void *cfg_lookupvc(cfdb *cfg, int objtype, char *name);
void *cfg_lookupifi(cfdb *cfg, int objtype, int ifi);
int cfg_iterate(cfdb *cfg, int objtype, cfg_iterator iter, void *arg);


#endif /* CFDB_HDR_LOADED_ */



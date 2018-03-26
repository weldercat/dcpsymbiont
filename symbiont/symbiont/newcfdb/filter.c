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
#include <symbiont/cfdb.h>


#include <symbiont/filter.h>


bool flt_isfilter(void *ptr)
{
	struct filter *flt;
	
	assert(ptr);
	flt = (struct filter *)ptr;
	
	if (flt->magic == FILTER_MAGIC) return true;
	else return false;
}


struct filter *new_filter(void)
{
	struct filter *flt;
	int	res;
	
	flt = malloc(sizeof(struct filter));
	
	if (!flt) {
		SYMERROR("cannot allocate memory\n");
		return NULL;
	}
	memset(flt, 0, sizeof(struct filter));
	res = pthread_rwlock_init(&flt->lock, NULL);
	if (res) {
		SYMERROR("cannot initialize rwlock:%s\n", STRERROR_R(res));
		free(flt);
		return NULL;
	}

	flt->namehash = sm_new(FLT_INICOUNT);
	if (!flt->namehash) {
		SYMERROR("cannot create name hashtable\n");
		pthread_rwlock_destroy(&flt->lock);
		free(flt);
		return NULL;
	}
	flt->aliashash = sm_new(FLT_INICOUNT);
	if (!flt->aliashash) {
		SYMERROR("cannot create alias hash\n");
		sm_delete(flt->namehash);
		pthread_rwlock_destroy(&flt->lock);
		free(flt);
		return NULL;
	}
	flt->magic = FILTER_MAGIC;
	return flt;
}



int free_filter(struct filter *flt)
{
	assert(flt);
	
	lock_write(&flt->lock);

	if (flt->refcnt > 0) {
		flt->refcnt--;
		
		if (flt->refcnt > 0) {
			lock_unlock(&flt->lock);
			return FLT_BUSY;
		}
	}
	sm_delete(flt->namehash);
	sm_delete(flt->aliashash);
	if (flt->name) free(flt->name);
	flt->magic = 0;
	lock_unlock(&flt->lock);
	pthread_rwlock_destroy(&flt->lock);
	return FLT_OK;
}

struct filter *ref_filter(struct filter *flt)
{
	struct filter *fltcopy;

	assert(flt);
	if (!flt_isfilter(flt)) return NULL;
	lock_write(&flt->lock);
	flt->refcnt++;
	if (flt->refcnt <= 0) {
		SYMFATAL("reference count overflow for filter \"%s\" at %p\n", flt->name, flt);
		assert(0);
	}
	fltcopy = flt;
	lock_unlock(&flt->lock);
	return fltcopy;
}


int flt_add_entry(struct filter *flt, char *hwname, char *alias, bool sendevt, bool rcvcmd)
{
	struct flte *entry;
	int	res;
	int	len;

	assert(flt);
	assert(hwname);
	
	len = strlen(hwname);
	if (len <= 0) {
		SYMERROR("cannot add filter entry with empty hwname\n");
		return FLT_FAIL;
	}
	entry = malloc(sizeof(struct flte));
	if (!entry) {
		SYMERROR("cannot allocate memory\n");
		return FLT_FAIL;
	}
	memset(entry, 0, sizeof(struct flte));
	
	entry->hwname = strdup(hwname);
	if (alias) entry->alias = strdup(alias);
	entry->sendevt = sendevt;
	entry->rcvcmd = rcvcmd;
	
	lock_write(&flt->lock);
	res = sm_put(flt->namehash, (const unsigned char *)hwname, len, entry);
	if (res != STRMAP_OK) {
		SYMERROR("cannot insert into filter hwname hashtable\n");
		res = FLT_FAIL;
		goto out;
	}
	if (alias) {
		len = strlen(alias);
		if (len <= 0) {
			SYMWARNING("non-NULL but empty alias - won't be indexed\n");
			res = FLT_OK;
			goto out;
		}
		res = sm_put(flt->aliashash, (const unsigned char *)alias, len, entry);
		if (res != STRMAP_OK) {
			SYMERROR("cannot insert into filter alias hashtable\n");
			res = FLT_FAIL;
			goto out;
		}
	}
	res = FLT_OK;
out:	
	lock_unlock(&flt->lock);
	return res;
}



struct flte *lookup_name(struct filter *flt, char *hwname)
{
	struct flte *entry;
	int	len;

	assert(flt);
	assert(hwname);
	len = strlen(hwname);
	if (len <= 0) {
		SYMERROR("cannot lookup non-NULL but empty hwname\n");
		return NULL;
	}
	lock_read(&flt->lock);
	entry = sm_get(flt->namehash, (const unsigned char *)hwname, len);
	lock_unlock(&flt->lock);
	return entry;
}


struct flte *lookup_alias(struct filter *flt, char *alias)
{
	struct flte *entry;
	int	len;

	assert(flt);
	assert(alias);
	len = strlen(alias);
	if (len <= 0) {
		SYMERROR("cannot lookup non-NULL but empty alias\n");
		return NULL;
	}
	lock_read(&flt->lock);
	entry = sm_get(flt->aliashash, (const unsigned char *)alias, len);
	lock_unlock(&flt->lock);
	return entry;
}


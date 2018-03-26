/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/

#define _GNU_SOURCE	1

//#define DEBUG_ME_HARDER	1

#include <assert.h>
#include <symbiont/symerror.h>

#include <symbiont/strmap.h>
/* very loosely based on strmap 2.0.1 by Per Ola Kristensson.
 * internal structure is somewhat different and dropping the k/v pair is added
 * hashing function is the same */

struct pair;

struct pair {
	unsigned char *key;
	int	keylen;
	void	*value;
	struct	pair *prev;
	struct	pair *next;
};


struct bucket {
	struct pair *pairs;
};

struct strmap_s {
	unsigned int count;
	struct bucket *buckets;
};



static unsigned long hash(const unsigned char *str, int len);
static struct pair *new_pair(const unsigned char *key, int keylen);
static struct pair *get_pair(struct bucket *bkt, const unsigned char *key, int keylen);
static void free_pair(struct bucket *bkt, struct pair *p);

strmap *sm_new(unsigned int capacity)
{
	strmap *sm = NULL;
	size_t	bktsize;	

	assert(capacity > 0);
	
	sm = malloc(sizeof(strmap));
	if (!sm) return NULL;

	sm->count = capacity;
	bktsize = sm->count * sizeof(struct bucket);
	
	sm->buckets = malloc(bktsize);
	if (!sm->buckets) {
		free(sm);
		return NULL;
	}
	
	memset(sm->buckets, 0, bktsize);
	SYMDEBUGHARD("strmap created at %p with %d buckets\n", sm, capacity);
	return sm;
}



void sm_delete(strmap *map)
{
	int	i;
	struct bucket *bkt;
	
	assert(map);
	
	for (i = 0; i < map->count; i++) {
		bkt = &(map->buckets[i]);
		while (bkt->pairs) {
			free_pair(bkt, bkt->pairs);
		}
	}
	free(map->buckets);
	free(map);
}


void *sm_get(const strmap *map, unsigned const char *key, int keylen)
{
	unsigned int index;
	struct bucket *bkt;
	struct pair *p;
	
	assert(map);
	assert(key);
	assert(keylen > 0);
	
	index = hash(key, keylen) % map->count;
	bkt = &(map->buckets[index]);
	
	p = get_pair(bkt, key, keylen);
	SYMDEBUGHARD("p = %p\n", p);
	SYMDEBUGHARD("looking up for %s (len=%d) at %p [idx=%d], got %p\n", 
			key, keylen, map, index, (p ? p->value : NULL));
	if (!p) return NULL;
	else {
		SYMDEBUGHARD("p->value = %p\n", p->value);
		return p->value;
	}
}

bool sm_exists(const strmap *map, unsigned const char *key, int keylen)
{
	unsigned int index;
	struct bucket *bkt;
	struct pair *p;
	bool	res;
	
	assert(map);
	assert(key);
	assert(keylen > 0);
	
	index = hash(key, keylen) % map->count;
	bkt = &(map->buckets[index]);
	
	p = get_pair(bkt, key, keylen);
	res = (p != NULL);
	return res;
}


static struct pair *new_pair(const unsigned char *key, int keylen)
{
	struct pair *p;

	assert(key);
	assert(keylen > 0);
	
	p = malloc(sizeof(struct pair));
	if (!p) return NULL;
	memset(p, 0, sizeof(struct pair));
	p->key = malloc(keylen + 1);
	if (!p->key) {
		free(p);
		return NULL;
	}
	p->key[keylen] = 0;	/* no out-of-bounds - key is allocated with keylen + 1 */
	memcpy(p->key, key, keylen);
	p->keylen = keylen;
	return p;
}

static struct pair *get_pair(struct bucket *bkt, const unsigned char *key, int keylen)
{
	struct pair *p;
	
	assert(key);
	assert(keylen > 0);
	assert(bkt);

	p = bkt->pairs;
	while (p) {
		SYMDEBUGHARD("considering %s (len=%d) == %s (len=%d)\n", 
			p->key, p->keylen, key, keylen);
		if (p->keylen == keylen) {
			if (memcmp(p->key, key, keylen) == 0) {
				SYMDEBUGHARD("Found!\n");
				break;
			}
		}
		p = p->next;
	}
	return p;
}


static void free_pair(struct bucket *bkt, struct pair *p)
{
	assert(bkt);
	assert(p);
	
	if (!p->prev) {
		assert(bkt->pairs == p);
		bkt->pairs = p->next;
	} else 	p->prev->next = p->next;
	if (p->next) p->next->prev = p->prev;

	if (p->key) free(p->key);
	free(p);
}

int sm_put(strmap *map, unsigned const char *key, int keylen, void *value)
{
	unsigned int index;
	struct bucket *bkt;
	struct pair *p;

	assert(map);
	assert(key);
	assert(keylen > 0);
	
	index = hash(key, keylen) % map->count;
	bkt = &(map->buckets[index]);
	
	p = get_pair(bkt, key, keylen);
	if (p) {
		if (value) {
			p->value = value;
		} else {
			SYMDEBUGHARD("freeing pair @%p in bucket @%p\n", p, bkt);
			free_pair(bkt, p);	/* NULL value - remove the pair */
		}
		SYMDEBUGHARD("put %p at %p [idx=%d], key=%s, len=%d\n", value, map, index, key, keylen);
	} else {
		/* there is no data with the specified key - nothing to remove */
		if (!value) return STRMAP_OK;
		
		p = new_pair(key, keylen);
		assert(p);
		p->next = bkt->pairs;
		bkt->pairs = p;
		if (p->next) p->next->prev = p;
		p->value = value;
		SYMDEBUGHARD("put %p at %p [idx= %d] (new pair), key=%s, len=%d\n", value, map, index, key, keylen);
	}
	return STRMAP_OK;
}



/*
 * Returns a hash code for the provided string.
 */
static unsigned long hash(const unsigned char *str, int len)
{
	int	i;
	unsigned long hash = 5381;
	int c;

	for (i = 0; i < len; i++) {
		c = str[i];
		hash = ((hash << 5) + hash) + c;
	}
	return hash;
}

int sm_iterate(strmap *map, sm_iterator iter, void *arg)
{
	struct bucket *bkt;
	struct pair *p;
	int	i;
	int	res = STRMAP_OK;
	
	assert(map);
	assert(iter);
	
	for (i = 0; i < map->count; i++) {
		bkt = &(map->buckets[i]);
		p = bkt->pairs;
		while (p) {
			if ((p->key) && (p->keylen > 0) && (p->value)) 
				res = (iter)(p->key, p->keylen, p->value, arg);
			if (res) goto out;
			p = p->next;
		}
	}
out:
	return res;

}

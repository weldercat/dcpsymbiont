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
#ifndef STRMAP_HDR_LOADED_
#define STRMAP_HDR_LOADED_

/* very loosely based on strmap 2.0.1 by Per Ola Kristensson.
 * internal structure is somewhat different and dropping the k/v pair is added
 * hashing function is the same */

#include <stdlib.h>
#include <string.h>
#include <stdbool.h>


#define STRMAP_OK	0
/* enumeration complete */
#define STRMAP_DONE	1
#define STRMAP_FAIL	(-1)


/* must return 0 to continue with iteration and any non-zero to stop */
typedef int (*sm_iterator)(const unsigned char *key, int keylen, void *value, void *arg);

typedef struct strmap_s strmap;

/* Creates a string map.
 *
 * Parameters:
 *
 * capacity: The number of buckets this strmap
 * should allocate. This parameter must be > 0.
 *
 * Return value: A pointer to a string map object, 
 * or null if a new string map could not be allocated.
 */
strmap *sm_new(unsigned int capacity);


/*
 * Releases all memory held by a string map object.
 *
 * Parameters:
 *
 * map: A pointer to a string map. This parameter cannot be null.
 * If the supplied string map has been previously released, the
 * behaviour of this function is undefined.
 *
 * Return value: None.
 */
void sm_delete(strmap *map);


/*
 * Returns the value associated with the supplied key.
 *
 *
 */
void *sm_get(const strmap *map, const unsigned char *key, int keylen);

/*
 * Queries the existence of a key.
 *
 * Return value: true if the key exists, false otherwise.
 */
bool sm_exists(const strmap *map, const unsigned char *key, int keylen);

/* store the pointer associated with the key. 
 * storing NULL pointer will free the associated container structure
 *
 */

int sm_put(strmap *map, const unsigned char *key, int keylen, void *value);


int sm_iterate(strmap *map, sm_iterator iter, void *arg);


#endif /* STRMAP_HDR_LOADED_ */


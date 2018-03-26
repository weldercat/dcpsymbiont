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

#ifndef FILTER_HDR_LOADED_
#define FILTER_HDR_LOADED_
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <symbiont/strmap.h>

#define FLT_OK	0
#define FLT_FAIL	(-1)
#define FLT_BUSY	1

#define FILTER_MAGIC	(0x730b6a00)

#define FLT_INICOUNT	20

struct flte {
	char	*hwname;
	char	*alias;
	bool	sendevt;
	bool	rcvcmd;
};


struct filter {
	uint32_t magic;
	pthread_rwlock_t lock;
	int	refcnt;
	char	*name;
	bool	def_sendevt;	/* action by default */
	bool	def_rcvcmd;	/* action by default */
	strmap *namehash;
	strmap *aliashash;
};

bool flt_isfilter(void *ptr);
struct filter *new_filter(void);
int free_filter(struct filter *flt);
struct filter *ref_filter(struct filter *flt);

int flt_add_entry(struct filter *flt, char *hwname, char *alias, bool sendevt, bool rcvcmd);

struct flte *lookup_name(struct filter *flt, char *hwname);
struct flte *lookup_alias(struct filter *flt, char *alias);


#endif /* FILTER_HDR_LOADED_ */

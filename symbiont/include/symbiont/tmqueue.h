/*
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 */

#ifndef TMQ_HDR_LOADED_
#define TMQ_HDR_LOADED_

#include <sys/time.h>

#define TMQ_OK	0
#define TMQ_FAIL -1

typedef struct tmq_entry_ {
	time_t			expires;
	struct tmq_entry_	*prev;
	struct tmq_entry_	*next;
	void			*data;
} tmq_entry;

typedef struct tmq_ {
	pthread_mutex_t	access_mutex;
	tmq_entry	*head;
	tmq_entry	*tail;
} tmq;

/* init timer queue *
 * q must point to the uninitialized tmq struct;
 * returns TMQ_OK or TMQ_FAIL
 */
int	init_tmq(tmq *q);


/* allocate new entry, fill it with params and add it to queue 
 * return ptr to the new entry
 */
tmq_entry *tmq_add(tmq *q, time_t expires, void *data);

/* insert entry into queue. 
 * place it according to its expiration time,
 * seek from the head
 */
void tmq_insert(tmq *q, tmq_entry *entry);


/* 
 * append entry to queue. 
 * place it according to its expiration time,
 * seek from the tail
 */

void tmq_append(tmq *q, tmq_entry *entry);


/*
 * remove entry from queue
 */
void tmq_unlink(tmq *q, tmq_entry *entry);
/*
 * remove entry from queue and free it's memory
 */
void tmq_drop(tmq *q, tmq_entry *entry);
/* 
 * change entry's expiration time
 */
void tmq_change(tmq *q, tmq_entry *entry, time_t newexpires);

typedef void (*tmq_handler)(void *arg, tmq_entry *entry);

/* 
 * expire all entries with expiration time in the past, returns
 * how long will it take for earlieast entry to expire or 0
 * if queue is empty
 */
time_t tmq_expire(tmq *q, time_t now, tmq_handler handler, void *arg);

bool tmq_isexpired(tmq_entry *entry);

void tmq_dump(tmq *q);
#endif /* TMQ_HDR_LOADED_ */

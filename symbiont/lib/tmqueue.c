/*
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 */

#define _GNU_SOURCE	1

#include <sys/time.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <symbiont/symerror.h>
#include <symbiont/tmqueue.h>
#define NOFAIL_LOCK_UNNEEDED	1
#include <symbiont/nofail_wrappers.h>
//#define DEBUG_ME_HARDER 1

tmq_entry *alloc_tmq_entry(void);
static void tmq_insert_nolock(tmq *q, tmq_entry *entry);
static void tmq_unlink_nolock(tmq *q, tmq_entry *entry);
static void tmq_dump_nolock(tmq *q);


int init_tmq(tmq *q)
{
	int	res;
	assert(q);
	memset(q, 0, sizeof (tmq));
	res = pthread_mutex_init(&q->access_mutex, NULL);
	if (res) {
		SYMERROR("cannot init mutex: %s\n", STRERROR_R(res));
		return TMQ_FAIL;
	}
	return TMQ_OK;
}


tmq_entry *alloc_tmq_entry(void)
{
	tmq_entry	*p;

	p = malloc(sizeof(tmq_entry));
	if (p) {
		memset(p, 0, sizeof(tmq_entry));
	} else {
		SYMERROR("cannot allocate memory\n");
	}
	return p;
}

tmq_entry *tmq_add(tmq *q, time_t expires, void *data)
{
	tmq_entry	*entry;
	
	assert(q);
	
	entry = alloc_tmq_entry();
	if (!entry) return NULL;
	entry->data = data;
	entry->expires = expires;
	tmq_insert(q, entry);
	return entry;
}

void tmq_insert(tmq *q, tmq_entry *entry)
{
	assert(q);
	mutex_lock(&q->access_mutex);
	tmq_insert_nolock(q, entry);
	mutex_unlock(&q->access_mutex);
}

	
static void tmq_insert_nolock(tmq *q, tmq_entry *entry)
{
	tmq_entry	*p;
	
	assert(q);
	assert(entry);

	if (!q->head) {
		q->head = entry;	/* empty queue, entry becomes first */
		assert(q->tail == NULL);
		q->tail = entry;
		return;
	}
	p = q->head;
	
	for(;;) {
		if (p->expires >= entry->expires) break;
		if (p->next) {
			p = p->next;
		} else {
			p->next = entry; /* entry becomes last in queue */
			entry->prev = p;
			entry->next = NULL;
			q->tail = entry;
			return;
		}
	}

	entry->prev = p->prev;	/* inserting before p */
	if (!entry->prev) {
		q->head = entry; /* p was first, now entry becomes first */
	} else {
		entry->prev->next = entry;
	}

	entry->next = p;
	p->prev = entry;
}

void tmq_append(tmq *q, tmq_entry *entry)
{
	tmq_entry	*p;
	
	assert(q);
	assert(entry);

	mutex_lock(&q->access_mutex);
	if (!q->tail) {
		if (q->head) {
			SYMFATAL("fatal queue corruption - queue has a head but no tail, abort\n");
		}
		tmq_insert_nolock(q, entry);
		goto out;
	}
	p = q->tail;
	for (;;) {
		if (p->expires <= entry->expires) break;
		if (p->prev) {
			p = p->prev;
		} else {
			p->prev = entry; /* entry becomes first in queue */
			entry->prev = NULL;
			entry->next = p;
			q->head = entry;
			goto out;
		}
	}
	entry->next = p->next;	/* inserting after p */
	if (!entry->next) {
		q->tail = entry; /* p was last, now entry became last */
	} else {
		entry->next->prev = entry;
	}

	entry->prev = p;
	p->next = entry;
out:	
	mutex_unlock(&q->access_mutex);
}

void tmq_unlink(tmq *q, tmq_entry *entry)
{
	assert(q);
	mutex_lock(&q->access_mutex);
	tmq_unlink_nolock(q, entry);
	mutex_unlock(&q->access_mutex);
}


static void tmq_unlink_nolock(tmq *q, tmq_entry *entry)
{
	assert(q);
	assert(entry);

	if (!entry->prev) {
		if (q->head != entry) {
			SYMFATAL("fatal queue corruption - queue head doesn't point to first entry, abort\n");
		}
		SYMDEBUG("unlinking first entry\n");
		q->head = entry->next;
		if(entry->next) {
			entry->next->prev = NULL;	/* entry to be unlinked was first */
		} else {
			if (q->tail != entry) {
				SYMFATAL("fatal queue corruption - queue tail doesn't point to the only entry, abort\n");
			}
			SYMDEBUG("unlinking the single and only entry\n");
			q->tail = NULL;
		}
	} else {
		SYMDEBUG("unlinking not the first entry. q->head=%p q->tail=%p p=%p\n", 
				q->head, q->tail, entry);
		entry->prev->next = entry->next;	/* not first */
		if (entry->next) entry->next->prev = entry->prev;
		else q->tail = entry->prev;
	}
}

void tmq_drop(tmq *q, tmq_entry *entry)
{
	assert (q);
	mutex_lock(&q->access_mutex);
	if (entry && q->head) {
		tmq_unlink_nolock(q, entry);
		free(entry);
		goto out;
	}
	SYMERROR("empty queue or NULL entry ptr - nothing to do\n");
out:
	mutex_unlock(&q->access_mutex);
}

void tmq_change(tmq *q, tmq_entry *entry, time_t newexpires)
{
	assert(q);
	assert(entry);
	if (newexpires != entry->expires) {
		mutex_lock(&q->access_mutex);
		tmq_unlink_nolock(q, entry);
		entry->expires = newexpires;
		tmq_insert_nolock(q, entry);
		mutex_unlock(&q->access_mutex);
	}
}

/* WARNING ! handler is called with access mutex locked - 
 * deadlock can occur if handler will try to operate on the same 
 * timer queue. Also handler must copy it's entry argument
 * if it should be accessible after handler's return.
 */


time_t tmq_expire(tmq *q, time_t now, tmq_handler handler, void *arg)
{
	tmq_entry	*p;
	time_t		res;

	assert(q);
	mutex_lock(&q->access_mutex);
#ifdef DEBUG_ME_HARDER
	if (q->head) {
		SYMDEBUGHARD("current time %u, expires %u\n", now, (q->head)->expires);
	}
#endif
	for (;;) {
		p = q->head;
		if (!p) {
			res = 0;
			break;
		}
		if (p->expires > now) {
			res = (p->expires - now);
			break;
		}
		if (handler) handler(arg, p);
#ifdef DEBUG_ME_HARDER
		SYMDEBUG("about to call tmq_drop(%p, %p)\n", q, p);
		tmq_dump(q);
#endif
		tmq_unlink_nolock(q, p);
		free(p);
#ifdef DEBUG_ME_HARDER
		tmq_dump_nolock(q);
#endif
	}
	mutex_unlock(&q->access_mutex);
	return res;
}


void tmq_dump(tmq *q)
{
	assert(q);
	mutex_lock(&q->access_mutex);
	tmq_dump_nolock(q);
	mutex_unlock(&q->access_mutex);
}

static void tmq_dump_nolock(tmq *q)
{
	tmq_entry	*p;
	int		i = 0;

	assert(q);
	p = q->head;
	if (!p) SYMDEBUG("queue empty\n");
	while(p) {
		SYMDEBUG("entry #%d, p=%p, p->next=%p, p->prev=%p, expires = %u\n",
				i, p, p->next, p->prev, p->expires);
		p = p->next;
		i++;
	}
}


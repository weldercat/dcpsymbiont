/*
 * Copyright 2017  Stacy <stacy@sks.uz>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef YMESSAGE_HDR_LOADED_
#define YMESSAGE_HDR_LOADED_
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <semaphore.h>
#include <pthread.h>

#define YXT_OK		0
#define YXT_FAIL	(-1)
/* these two are for handler return condition 
 * YXT_PROCESSED means stop traversing the handlers chain,
 * and tell yate that message has been processed.
 * YXT_CHANGED means that source message has benn changed 
 * by the handler but do not tell yate that msg was handled.
 */
#define YXT_PROCESSED	1
#define YXT_CHANGED	2

/* usecs to sleep on sem_post() overflow */
#define SEM_POST_RETRY_SLEEP	100000	
/* seconds to wait for spec operation to complete */
#define SPEC_OP_TIMEOUT		10

#define YXT_MAX_NREADERS	32
#define	YXT_DEF_NREADERS	3

struct kvp_list_s;
typedef struct kvp_list_s {
	struct kvp_list_s	*prev;
	struct kvp_list_s	*next;
	char	*name;
	char	*value;
} kvp_list;

typedef struct yatemsg_s {
	time_t		time;
	bool		processed;
	char		*name;
	char		*id;
	char		*retvalue;
	void		*userdata;
	int		userdatalen;
	kvp_list	*params;
} yatemsg;

#ifdef YXT_EXPORT_INTERNAL
kvp_list *find_kvp(kvp_list *l, char *name);
kvp_list *insert_kvp(kvp_list *l, char *name, char *value);
kvp_list *append_kvp(kvp_list *l, char *name, char *value);
void remove_kvp(kvp_list *l, char *name);

char *get_kvp_value(kvp_list *l, char *name);
kvp_list *set_kvp_value(kvp_list *l, char *name, char *value);
void free_kvp_list(kvp_list *l);
void copy_kvp_list(kvp_list *dst, kvp_list *src);
void dump_kvp_list(kvp_list *l);
#endif	/* YXT_EXPORT_INTERNAL */

yatemsg *alloc_message(char *name);

/* returns actual pointer - must be strdup()ed before use */
char *get_msg_param(yatemsg *m, char *name);

/* 
 * performs strdup() internally, so name & value may 
 * be destroyed after call
 */
void set_msg_param(yatemsg *m, char *name, char *value);
void remove_msg_param(yatemsg *m, char *name);

#define get_msg_time(m)	((m)->time)
#define get_msg_processed(m) ((m)->processed)
#define get_msg_name(m)	((m)->name)
#define get_msg_id(m)	((m)->id)
#define get_msg_retvalue(m)	((m)->retvalue)

#define set_msg_time(m, t) {\
	assert(m);		\
	(m)->time = (t);	\
}

#define set_msg_processed(m, p) {\
	assert(m);		\
	(m)->processed = (p);	\
}

#define set_msg_name(m, n) {\
	assert(m);		\
	assert(n);		\
	(m)->name = strdup(n);	\
};

#define set_msg_id(m, i) {\
	assert(m);		\
	assert(i);		\
	(m)->id = strdup(i);	\
};

#define set_msg_retvalue(m, r) {\
	assert(m);		\
	assert(r);		\
	(m)->retvalue = strdup(r);\
};


/* frees all message data  - will try to free() also data pointed by name, id, retvalue etc */
void free_message(yatemsg *m);

yatemsg *copy_message(yatemsg *m);

void copy_msg_params(yatemsg *dst, yatemsg *src);

/* print the message using symtrace(TRC_INFO, ...) */
void dump_message(yatemsg *m);

bool is_bool(char *val);
bool is_true(char *val);

/*
	yatemsg *text2yatemsg(char *mtext, size_t len);
	char *yatemsg2text(yatemsg *msg, size_t *len);
*/
//struct handler_list_s;
struct conn_ctx_s;
//typedef struct conn_ctx_s conn_ctx;

/* 
 * delayed work handler
 * 
 */
typedef void(*dwhook_runner)(void *arg);

/*  a pointer to this structure is passed to the handler
 * A handler may set dwhr and dwarg fields and
 * then the dwhr will be called by the read worker after all 
 * mutexes will be unlocked
 */
struct dwhook  {
	dwhook_runner	runner;
	void		*arg;
};

/* handlers and watchers callback function 
 * It is expected to return YXT_OK, YXT_FAIL, or YXT_PROCESSED
 * (possibly modified) msg will be sent to yate with 
 * "processed" flag set to true if handler returned with 
 * YXT_PROCESSED. Or false otherwise.
 * dwh argument is set for handlers only (watchers got NULL there).
 * It allows handler to request delayed work function call
 * after all mutexes will be unlocked. An argument to the function
 * can be set in dwh->arg;
 */
typedef int (*handler)(struct conn_ctx_s *ctx, yatemsg *msg, void *arg, struct dwhook *dwh);

typedef struct handler_list_s {
	handler	hdlr;
	bool	is_watcher;
	bool	is_catchall;
	char	*name;
	void	*arg;
	int	prio;
	struct handler_list_s	*prev;
	struct handler_list_s	*next;
} handler_list;

/* how many transaction id bits to use as a hash index */
#define	TID_BITS	6
#define TRANS_TABLE_SIZE (1 << (TID_BITS))
#define TRANS_IDX_MASK	(TRANS_TABLE_SIZE - 1)
typedef struct conn_ctx_s {
	bool	shutdown;	/* shutdown flag - stop operations if set */
	bool	socket;	/* socket or stdin/out etc */
	int	cmdrfd;	/* cmd read - from engine to us */
	int	cmdwfd;	/* cmd write - from us to engine */
	int	logfd;	/* logging and text reports (stderr) */
	int	brfd;	/* binary data read (from engine to us) */
	int	bwfd;	/* binary data write (from us to engine) */
	char	*role;
	char	*chan_id;
	char	*datatype;

	pthread_mutex_t rd_mutex;	/* read line mutex to avoid splitting incoming data between several workers */
	struct	bStream	*rbs;		/* pointer to the read stream */
	pthread_mutex_t	wr_mutex;	/* access to cmdwfd and logfd is protected by this mutex  to avoid interleaved writes */
	pthread_mutex_t	spec_op;	/* non-message passing operation is in progress (install/uninstall, param read/write etc) */

	int	sop_status;
	unsigned char	*sop_value;
	pthread_cond_t sop_info_cond;		/* signals completion of spec op */
	pthread_mutex_t sop_info_mutex;	/* access to spec_op_status & sop value */

	sem_t	work_available;	/* queue entries available - counting sem*/

	pthread_mutex_t	queue_mutex; /* access to transaction queue */
	struct  qblock_s *queue;

	pthread_mutex_t	id_mutex;	/* access to transaction id */
	char	*id_pfx;
	int	transaction;		/* transaction id */

	int	nreaders;
	pthread_t	reader_threads[YXT_MAX_NREADERS];	/* socket readers */
	pthread_t	writer_thread;	/* socket writer */
	handler_list *handlers;		/* handlers & watchers list */
	pthread_mutex_t	hndlr_mutex;	/* handler list ins/rem/seek operations */
	
	struct qblock_s *trtab[TRANS_TABLE_SIZE];	/* transaction table */
	pthread_mutex_t	trtab_mutex;	/* access to transaction table */
} conn_ctx;


/* a message queue block */
struct qblock_s;
struct qblock_s {
	bool	want_reply;
	bool	is_reply;
	sem_t	reply_sem;
	yatemsg	*msg;
	yatemsg *reply;
	struct	qblock_s *next;
	struct	qblock_s *prev;
};



/* generate message id */
char *gen_id(conn_ctx *cxt, int *id);

/* connect to the yate external mod interface via unix socket */
conn_ctx *yxt_conn_unix(char *path, char *role);

/* connect to the yate external mod interface via file descriptors -
 * this is used if we were spawned by yate ext module as a script 
 */
conn_ctx *yxt_conn_fds(void);

/* connect to the yate external mod interface via tcp *
 * dest - is the string in <ip_addr>:<port> format 
 */
conn_ctx *yxt_conn_tcp(char *dest, char *role);

/* run worker threads 
 * will start one writer thread and nreaders readers threads
 * nreaders will be set to YXT_DEF_NREADERS if nreaders is < 1
 */
int yxt_run(conn_ctx *ctx, int nreaders);

/* close the fds and free the context and it's components */
void yxt_disconnect(conn_ctx *ctx);

/* add special catch-all handler that receives messages not handled by 
 * any other handler. This is nessesary when using autospawn feature 
 * - yate expects external script to handle call.execute when the 
 * script is spawned from calling to external[/param]/scriptname
 * This function doesn't require running worker threads and can be 
 * called before yxt_run()
 *
 * NOTE - this handler is only can be installed first
 * otherwise it will intercept messages before other handlers -
 * This function refuses add the handlers if it's not the first.
 * This handler cannot be safely removed!
 */
int yxt_add_default_handler(conn_ctx *ctx, handler hdlr, void *arg);

int yxt_add_handler(conn_ctx *ctx, handler hdlr,
		char *name, int prio, void *arg);

int yxt_add_handler_filtered(conn_ctx *ctx, handler hdlr,
		char *name, int prio, char *fname, char *fvalue, void *arg);

int yxt_remove_handler(conn_ctx *ctx, char *name, int prio);

int yxt_add_watcher(conn_ctx *ctx, handler hdlr, char *name, void *arg);

int yxt_remove_watcher(conn_ctx *ctx, char *name);


/*WARNING! engine replies asynchronously */
int yxt_set_param(conn_ctx *ctx, char *name, char *value);
int yxt_get_param(conn_ctx *ctx, char *name, char **value);

int yxt_log(conn_ctx *ctx, char *text);

/* do not wait for reply */
int yxt_enqueue(conn_ctx *ctx, yatemsg *m);

/* send the message to yate and wait for reply */
int yxt_dispatch(conn_ctx *ctx, yatemsg *m, yatemsg **reply);


#endif /* YMESSAGE_HDR_LOADED_ */

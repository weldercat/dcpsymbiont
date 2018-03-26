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

#define _GNU_SOURCE	1
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>
#include <bstrlib.h>
#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <time.h>


//#define NDEBUG	1 /*uncomment to disable asserts */
//#define NODEBUG		1	/* uncomment to disable printing SYMDEBUG() info */
//#define DEBUG_ME_HARDER	1
#include <assert.h>

#define YXT_EXPORT_INTERNAL	1	/* to enable internal prototypes in yxtlink.h */
#include <symbiont/yxtlink.h>
#include <symbiont/symerror.h>
#define NOFAIL_LOCK_UNNEEDED	1
#include <symbiont/nofail_wrappers.h>
#include "config.h"



#ifndef HAVE_PTHREAD_SETNAME_NP
static int pthread_setname_np(pthread_t thread, const char *name);

static int pthread_setname_np(pthread_t thread, const char *name)
{
	return 0;
}
#endif


#define canrun(c) {	\
		assert(c);	\
		if (c->shutdown) return YXT_FAIL; \
	}

#define CMDSTR_LEN	512

#define CATCHALL_NAME	"*catchall*"

enum line_syntax {
	LSX_UNKNOWN = 0,
	LSX_CNXREP,	/* engine does not reply - this is never selected */
	LSX_MSGREQ,	/* "%%>message:" */
	LSX_MSGREP,	/* "%%<message:<id>:" - id is nonepmty */
	LSX_MSGWATCH,	/* "%%<message::" - id is empty */
	LSX_CMDREP,	/* " One of: "%%<install:", "%%<uninstall:", "%%<watch:", "%%<unwatch:" */
	LSX_PARAM,	/* "%%<setlocal:" */
	LSX_ERROR	/* "Error in:" */
};

enum sop_status {
	SOP_NONE = 0,
	SOP_STARTED,
	SOP_OK,
	SOP_FAIL
};

static int lsx(bstring line);
static conn_ctx* new_conn_ctx(char *role);
/* send %%>connect message with appropriate role to the engine */
static void set_role(conn_ctx *ctx);
static void *reader_thread(void *context);
static void *writer_thread(void *context);
static void save_transaction(conn_ctx *ctx, struct qblock_s *item);
static void end_transaction(conn_ctx *ctx, struct qblock_s *item);
static struct qblock_s *find_transaction(conn_ctx *ctx, char *id);
static int id2hash(char *id);
static struct qblock_s *dequeue_one(conn_ctx *ctx);
static void enqueue_request(conn_ctx *ctx, struct qblock_s *item);
static int dispatch_or_enqueue(conn_ctx *ctx, yatemsg *m, yatemsg **reply, bool is_reply);
static int send_message(int fd, yatemsg *m, bool request);
static int bcat_escapeblk(bstring dst, unsigned char *data, int datalen);
static int bcat_escapecstr(bstring dst, const char *str);
static bstring msg_to_bstr(yatemsg *m, bool request);
static bstring bstr_unescape(bstring src);
static size_t fdread(void *ptr, size_t elsize, size_t nmemb, void *parm);
static yatemsg *decode_request(bstring line);
static yatemsg *decode_reply(bstring line);
static unsigned char *msgstr_item(bstring line, int start, int end);
static void decode_kvps(yatemsg *m, bstring line, int start);
static void run_handlers(conn_ctx *ctx, yatemsg *msg);
static int bwrite(int fd, bstring str);
static int insert_handler(conn_ctx *ctx, handler hdlr, char *name, 
		int prio, void *arg, bool is_watcher);
static int add_hndlr_wtchr(conn_ctx *ctx, handler hdlr,
		char *name, int prio, char *fname, 
		char *fvalue, void *arg, bool is_watcher);
static void spec_op_init(conn_ctx *ctx);
static int spec_op_wait(conn_ctx *ctx, int *result);
static void finalize_command(conn_ctx *ctx, bstring line);
static void finalize_param(conn_ctx *ctx, bstring line);
static handler_list *find_handler(conn_ctx *ctx, char *name);
static int del_hndlr_wtchr(conn_ctx *ctx, char *name);
static void process_reply(conn_ctx *ctx, yatemsg *msg);
static void run_watchers(conn_ctx *ctx, yatemsg *msg);
static int decode_connstr(const char *connstr, char **addrstr, uint16_t *port);


static int lsx(bstring line)
{
	int	res;
	int	i;
	char	*p;
	struct	atom {
		const char	*text;
		const char	*name;
		int	tag;
	};
#define MAX_ATOMS	9

	static const struct atom atoms_array[] = {
		{	.text = "Error in:",
			.name = "syntax error",
			.tag =  LSX_ERROR },

		{	.text = "%%<message::",
			.name = "LSX_MSGWATCH",
			.tag = LSX_MSGWATCH },

		{	.text = "%%<message:",
			.name = "LSX_MSGREP",
			.tag = LSX_MSGREP },

		{	.text = "%%>message:",
			.name = "LSX_MSGREQ",
			.tag = LSX_MSGREQ },

		{	.text = "%%<setlocal:",
			.name = "LSX_PARAM",
			.tag = LSX_PARAM },

		{	.text = "%%<install:",
			.name = "LSX_CMDREP",
			.tag = LSX_CMDREP },

		{	.text = "%%<uninstall:",
			.name = "LSX_CMDREP",
			.tag = LSX_CMDREP },

		{	.text = "%%<watch:",
			.name = "LSX_CMDREP",
			.tag = LSX_CMDREP },

		{	.text = "%%<unwatch:",
			.name = "LSX_CMDREP",
			.tag = LSX_CMDREP },


		{ .text = NULL, .name = "none", .tag = 0}
	};

	assert(line);
	if (blength(line) <= 0) {
		SYMDEBUGHARD("cannot decode\n");
		return LSX_UNKNOWN;
	}
	for (i = 0; i < MAX_ATOMS; i++) {
		if (atoms_array[i].text == NULL) break;
		p = bdata(line);
		res = strncmp(p, atoms_array[i].text, 
				strlen(atoms_array[i].text));
		if (res == 0) {
			SYMDEBUGHARD("decoded as %s\n", atoms_array[i].name);
			return atoms_array[i].tag;
		}
	}
	SYMDEBUGHARD("cannot decode\n");
	return LSX_UNKNOWN;
}


static size_t fdread(void *ptr, size_t elsize, size_t nmemb, void *parm)
{
	int	fd;
	int	res = 0;
	
	assert(ptr);
	assert(parm);
	assert(elsize == 1);
	
	if (nmemb == 0) return 0;
	fd = *(int *)parm;
	while (res <= 0) {
		res = read(fd, ptr, nmemb);
		if (res < 0) {
			SYMERROR("error reading data from fd=%d: %s\n",
					fd, STRERROR_R(errno));
		}
	}
	return res;
}



kvp_list *find_kvp(kvp_list *l, char *name)
{
	kvp_list	*rl;
	int		res;

	assert(l);
	assert(name);

	rl = l;
	while (rl) {
		if (rl->name) {
			res = strcmp(rl->name, name);
			if (res == 0) break;
		}
		rl = rl->next;
	}
	return rl;
}

kvp_list *insert_kvp(kvp_list *l, char *name, char *value)
{
	kvp_list	*kl;
	
	assert(l);
	assert(name);
	kl = malloc(sizeof(kvp_list));
	if (!kl) {
		SYMERROR("cannot allocate memory\n");
		goto	errout;
	}
	memset(kl, 0, sizeof(kvp_list));
	while (l->prev) l = l->prev;

	if (name) {
		kl->name = strdup(name);
		if (!kl->name) {
			SYMERROR("strdup() error: %s\n", STRERROR_R(errno));
			free(kl);
			kl = NULL;
			goto	errout;
		}
	}
	if (value) {
		kl->value = strdup(value);
		if (!kl->value) {
			SYMERROR("strdup() error: %s\n", STRERROR_R(errno)); 
			if (kl->name) free(kl->name);
			free(kl);
			kl = NULL;
		}
	};
	kl->prev = l->prev;
	kl->next = l;
	l->prev = kl;
errout:
	return kl;
}

kvp_list *append_kvp(kvp_list *l, char *name, char *value)
{
	kvp_list	*kl;
	
	assert(l);
	assert(name);
	kl = malloc(sizeof(kvp_list));
	if (!kl) {
		SYMERROR("cannot allocate memory\n");
		goto	errout;
	}
	memset(kl, 0, sizeof(kvp_list));
	while (l->next) l = l->next;
	if (name) {
		kl->name = strdup(name);
		if (!kl->name) {
			SYMERROR("strdup() error: %s\n", STRERROR_R(errno));
			free(kl);
			kl = NULL;
			goto	errout;
		}
	}
	if (value) {
		kl->value = strdup(value);
		if (!kl->value) {
			SYMERROR("strdup() error: %s\n", STRERROR_R(errno)); 
			if (kl->name) free(kl->name);
			free(kl);
			kl = NULL;
		}
	};
	kl->prev = l;
	kl->next = l->next;
	l->next = kl;
errout:
	return	kl;
}

char *get_kvp_value(kvp_list *l, char *name)
{
	kvp_list *rl;

	assert(l);
	assert(name);
	rl = find_kvp(l, name);
	if (rl) {
		return rl->value;
	} else return NULL;
}

void remove_kvp(kvp_list *l, char *name)
{
	kvp_list	*rl;
	
	assert(l);
	assert(name);
	rl = find_kvp(l, name);
	if (rl) {
		if (rl->prev) {
			rl->prev->next = rl->next;
			if (rl->next) rl->next->prev = rl->prev;
			rl->next = NULL;
			rl->prev = NULL;
			if (rl->name) free(rl->name);
			if (rl->value) free(rl->value);
			free(rl);
		} else {
			/* strictly speaking this cannot happen -
			 * first element has NULL name so it won't be found.
			 */
			if (rl->name) {
			 	free(rl->name);
			 	rl->name = NULL;
			}
			if (rl->value) {
				free(rl->value);
			 	rl->value = NULL;
			}
			SYMFATAL("removing placeholder kvp %s@0x%p for list 0x%p - can't happen\n", name, rl, l);
		}
	}
}

kvp_list *set_kvp_value(kvp_list *l, char *name, char *value)
{
	kvp_list	*rl;
	char	*v;
	
	assert(l);
	assert(name);
	rl = find_kvp(l, name);
	if (rl) {
		if (rl->value) {
			free(rl->value);
			rl->value = NULL;
		}
		if (value) {
			v = strdup(value);
			if (!v) {
				SYMERROR("strdup() error: %s\n", STRERROR_R(errno));
				goto errout;
			}
			rl->value = v;
		}
	} else {
		rl = append_kvp(l, name, value);
	}
errout:
	return rl;
}

void free_kvp_list(kvp_list *l)
{
	kvp_list *tmp;

	if (l) {
		while(l->prev) l = l->prev;
		while (l) {
			if (l->name) free(l->name);
			if (l->value) free(l->value);
			tmp = l->next;
			free(l);
			l = tmp;
		}
	}
}

void copy_kvp_list(kvp_list *dst, kvp_list *src)
{
	kvp_list	*rl;

	assert(dst);
	assert(src);

	rl = src;
	while (rl) {
		if (rl->name) append_kvp(dst, rl->name, rl->value);
		rl = rl->next;
	}
}

void dump_kvp_list(kvp_list *l)
{
	kvp_list *rl;

	if (l) {
		rl = l;
		symtrace(TRC_INFO, "name-value pairs list at %p\n", l);
		while(rl) {
			symtrace(TRC_INFO, "at %p - %s='%s'\n", rl, rl->name ? rl->name : "NULL", 
							rl->value ? rl->value : "NULL");
			rl = rl->next;
		}
	} else SYMINFO("list is null\n");
}

yatemsg *alloc_message(char *name)
{
	yatemsg *msg = NULL;
	kvp_list *pl;
	
	SYMDEBUGHARD("at enter\n");
	pl = malloc(sizeof(kvp_list));
	if (!pl) {
		SYMERROR("cannot allocate memory\n");
		goto errout;
	}
	memset(pl, 0, sizeof(kvp_list));
	msg = malloc(sizeof(yatemsg));
	if (!msg) {
		SYMERROR("cannot allocate memory\n");
		goto errout;
	}
	memset(msg, 0, sizeof(yatemsg));
	msg->time = time(NULL);
	if (name) {
		msg->name = strdup(name);
		if (!msg->name) {
			SYMERROR("strdup() error %s\n", STRERROR_R(errno));
			free(msg);
			msg = NULL;
			goto errout;
		}
	}
	msg->params = pl;
errout:
	SYMDEBUGHARD("at exit, msg=%p\n", msg);
	return msg;
}

char *get_msg_param(yatemsg *m, char *name)
{
	assert(m);
	assert(name);
	
	if (m->params) {
		return (get_kvp_value(m->params, name));
	} else return NULL;
}

void set_msg_param(yatemsg *m, char *name, char *value)
{
	assert(m);
	assert(name);
	assert(m->params);
	
	set_kvp_value(m->params, name, value);
}

void remove_msg_param(yatemsg *m, char *name)
{
	assert(m);
	assert(name);
	assert(m->params);
	remove_kvp(m->params, name);
}

void free_message(yatemsg *m)
{
	SYMDEBUGHARD("at enter, msg=%p\n", m);
	if (m) {
		if (m->params) free_kvp_list(m->params);
		if (m->name) free(m->name);
		if (m->id) free(m->id);
		if (m->retvalue) free(m->retvalue);
		if (m->userdata) free(m->userdata);
		free(m);
	}
	SYMDEBUGHARD("at exit\n");
}

yatemsg *copy_message(yatemsg *m)
{
	yatemsg *nm;
	
	nm = alloc_message(NULL);
	if (!nm) SYMFATAL("cannot allocate memory\n");
	if (m) {
		nm->time = m->time;
		nm->processed = m->processed;
		if (m->name) nm->name = strdup(m->name);
		if (m->id) nm->id = strdup(m->id);
		if (m->retvalue) nm->retvalue = strdup(m->retvalue);
		if (m->userdata && (m->userdatalen > 0)) {
			nm->userdata = malloc(m->userdatalen);
			if (!nm->userdata) SYMFATAL("cannot allocate memory\n");
			memcpy(nm->userdata, m->userdata, m->userdatalen);
			nm->userdatalen = m->userdatalen;
		}
		if (m->params) copy_kvp_list(nm->params, m->params);
	}
	return nm;
}

void copy_msg_params(yatemsg *dst, yatemsg *src)
{
	if (dst && src) {
		if (src->params) copy_kvp_list(dst->params, src->params);
	}
}

void dump_message(yatemsg *m)
{
	if (m) {
		SYMINFO("msg@%p: time=%0d, processed=%d, name=\"%s\"\n", m, m->time, m->processed, m->name);
		symtrace(TRC_INFO, "    id=%s, retval=%s, userdata@%p - %d bytes\n", 
				m->id, m->retvalue, m->userdata, m->userdatalen);
		symtrace(TRC_INFO, "    name-value pairs:\n");
		dump_kvp_list(m->params);
	} else SYMINFO("msg is null");
}


#define BOOL_STRS	12
bool is_bool(char *val)
{
	int	i;
	int 	res = 1;
	const char *bool_strings[BOOL_STRS] = {
		"true", "false", "yes", "no",
		"on", "off", "1", "0",
		"enable", "disable",
		"enabled", "disabled"
	};
	
	assert(val);
	for (i = 0; i < BOOL_STRS; i++) {
		res = strcasecmp(val, bool_strings[i]);
		if (res == 0) break;
	}
	return (res == 0);
}

#define TRUE_STRS	4
bool is_true(char *val)
{
	bool	number = true;
	int	i, len;
	int 	res = 1;
	const char *bool_strings[TRUE_STRS] = {
		"true", "yes", "on", "enable"
	};
	
	assert(val);
	len = strlen(val);
	for (i = 0; i < len; i++) {
		if (!isdigit(val[i])) {
			number = false;
			break;
		}
	}
	if (number) {
		res = atoi(val);
		return (res != 0);
	} else {
		for (i = 0; i < TRUE_STRS; i++) {
			res = strcasecmp(val, bool_strings[i]);
			if (res == 0) break;
		}
		return (res == 0);
	}
}

static int bcat_escapeblk(bstring dst, unsigned char *data, int datalen)
{
	int	i, res;
	unsigned char b;

	assert(data);
	if (datalen <= 0) return BSTR_OK;
	for (i = 0; i < datalen; i++) {
		b = data[i];
		if (b == '%') {
			res = bcatcstr(dst, "%%");
		} else if ((b < ' ') || (b == ':')) {
			res = bconchar(dst, '%');
			if (res != BSTR_OK) break;
			res = bconchar(dst, b + 64);
		} else {
			res = bconchar(dst, b);
		}
		if (res != BSTR_OK) break;
	}
	return res;
}

static int bcat_escapecstr(bstring dst, const char *str)
{
	int	len;
	
	if (!str) return BSTR_ERR;
	len = strlen(str);
	if (len <= 0) return BSTR_OK;
	return (bcat_escapeblk(dst, (unsigned char *)str, len));
}

static bstring bstr_unescape(bstring src)
{
	bstring dst = NULL;
	int	len, i, res;
	char	c;
	bool	escflg;
	
	len = blength(src);
	if (len <= 0) return dst;
	dst = bfromcstralloc(len, "");
	escflg = false;
	for (i =0; i < len; i++) {
		c = bchar(src, i);
		if (!escflg) {
			if (c == '%') {
				escflg = true;
				continue;
			}
		} else {
			if (c != '%') c -= 64;
			escflg = false;
		}
		res = bconchar(dst, c);
		if (res != BSTR_OK) break;
	}
	return dst;
}

#define MIN_BUFFER_LEN	1024

/* request format:
 * %%>message:<id>:<time>:<name>:<retvalue>[:<key>=<value>...]
 *
 *response format:
 * %%<message:<id>:<processed>:[<name>]:<retvalue>[:<key>=<value>...]
 */
static bstring msg_to_bstr(yatemsg *m, bool request)
{
	bstring	dst = NULL;
	bstring tmp = NULL;
	kvp_list	*kvp;
	int	res;

	assert(m);
	dst = bfromcstralloc(MIN_BUFFER_LEN, (request ? "%%>message:" : "%%<message:"));
	assert(dst);
	res = bcat_escapecstr(dst, m->id);		assert(res == BSTR_OK);
	res = bconchar(dst, ':'); 		assert(res == BSTR_OK);
	if (request) {
		tmp = bformat("%d", m->time);
		assert(tmp);
		res = bconcat(dst, tmp);	assert(res == BSTR_OK);
		bdestroy(tmp);
		res = bconchar(dst, ':');	assert(res == BSTR_OK);
	} else {
		res = bcatcstr(dst, (m->processed ? "true:" : "false:"));
		assert(res == BSTR_OK);
	}
	res = bcat_escapecstr(dst, m->name); 	assert(res == BSTR_OK);
	res = bconchar(dst, ':');		assert(res == BSTR_OK);
	res = bcat_escapecstr(dst, m->retvalue ? m->retvalue : "");	assert(res == BSTR_OK);
	kvp = m->params;
	while (kvp) {
			if (kvp->name) {
				res = bconchar(dst, ':'); assert(res == BSTR_OK);
				res = bcat_escapecstr(dst, kvp->name); assert(res == BSTR_OK);
				if (kvp->value) {
					res = bconchar(dst, '=' ); assert(res == BSTR_OK);
					res = bcat_escapecstr(dst, kvp->value); assert(res == BSTR_OK);
				}
			}
			kvp = kvp->next;
	}
	res = bconchar(dst, '\n'); assert(res == BSTR_OK);
	return dst;
}

static int bwrite(int fd, bstring str)
{
	int	res = 0;
	
	while (!res) {
		res = write(fd, bdata(str), blength(str));
		if ((res < 0) && (errno == EAGAIN)) {
			res = 0;
		} else if (res < 0) {
			SYMERROR("error writing message: %s\n",
					STRERROR_R(errno));
			break;
		}
	}
	return res;
}

static int send_message(int fd, yatemsg *m, bool request)
{
	bstring	mstr = NULL;
	int	res = 0;
	char	*tmp;
	
	assert(m);
	mstr = msg_to_bstr(m, request);
	tmp = bstr2cstr(mstr, '_');
	SYMDEBUG("sending:%s\n", tmp);
	bcstrfree(tmp);
	res = bwrite(fd, mstr);
	bdestroy(mstr);
	if (res <= 0) return YXT_FAIL;
	else return YXT_OK;
}

static conn_ctx *new_conn_ctx(char *role)
{
	conn_ctx	*ctx;
	int		res;
	
	ctx = malloc(sizeof(conn_ctx));
	if (!ctx) {
		SYMERROR("cannot allocate memory\n");
		goto out;
	}
	memset(ctx, 0, sizeof(conn_ctx));
	ctx->cmdrfd = -1;
	ctx->cmdwfd = -1;
	ctx->logfd = -1;
	ctx->brfd = -1;
	ctx->bwfd = -1;
	if (role) {
		ctx->role = strdup(role);
		if (!ctx->role) {
			SYMERROR("strdup() error: %s\n", STRERROR_R(errno));
			goto out_with_ctx;
		}
	}
	res = pthread_mutex_init(&ctx->wr_mutex, NULL);
	assert(!res);

	res = pthread_mutex_init(&ctx->rd_mutex, NULL);
	assert(!res);

	res = pthread_mutex_init(&ctx->spec_op, NULL);
	assert(!res);
	res = pthread_cond_init(&ctx->sop_info_cond, NULL);
	assert(!res);
	res = pthread_mutex_init(&ctx->sop_info_mutex, NULL);
	assert(!res);

	res = sem_init(&ctx->work_available, 0, 0);
	assert(!res);

	res = pthread_mutex_init(&ctx->queue_mutex, NULL);
	assert(!res);
	res = pthread_mutex_init(&ctx->id_mutex, NULL);
	assert(!res);

	res = pthread_mutex_init(&ctx->hndlr_mutex, NULL);
	assert(!res);

	res = pthread_mutex_init(&ctx->trtab_mutex, NULL);
	assert(!res);
	goto	out;
	
out_with_ctx:
	free(ctx);
	ctx = NULL;
out:
	return ctx;
}


/* 0.0.0.0:0 */ 
#define MIN_CONNSTR_LEN	9

static int decode_connstr(const char *connstr, char **addrstr, uint16_t *port)
{
	int	len;
	char	*portsep;
	
	assert(connstr);
	assert(addrstr);
	assert(port);
	
	len = strlen(connstr);
	if (len < MIN_CONNSTR_LEN) return YXT_FAIL;
	portsep = strchr(connstr, ':');
	if (!portsep) return YXT_FAIL;
	if ((portsep + 1) >= connstr + len) return YXT_FAIL;
	*addrstr = strndup(connstr, (portsep - connstr));
	*port = strtol(portsep + 1, NULL, 10);
	return YXT_OK;
}



conn_ctx *yxt_conn_unix(char *path, char *role)
{
	conn_ctx *ctx = NULL;
	int sockfd = -1;
	int res;
	struct sockaddr_un saddr;
	
	assert(path);
	sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (sockfd < 0) {
		SYMERROR("cannot create socket: %s\n", STRERROR_R(errno));
		goto errout;
	}
	memset(&saddr, 0, sizeof(saddr));
	saddr.sun_family = AF_UNIX;
	strncpy(saddr.sun_path, path, sizeof(saddr.sun_path) - 1);
	res = connect(sockfd, (const struct sockaddr *) &saddr, sizeof(saddr));
	if (res < 0) {
		SYMERROR("cannot connect to %s: %s\n", saddr.sun_path, 
			STRERROR_R(errno));
		goto errout;
	} else {
		SYMDEBUG("connected to %s\n", saddr.sun_path);
	}
	ctx = new_conn_ctx(role);
	if (!ctx) {
		close(sockfd);
		goto errout;
	}
	ctx->socket = true;
	ctx->cmdrfd = sockfd;
	ctx->cmdwfd = sockfd;
	if (role) set_role(ctx);
errout:
	return ctx;
}



conn_ctx *yxt_conn_tcp(char *dest, char *role)
{
	conn_ctx *ctx = NULL;
	int sockfd = -1;
	int res;
	struct sockaddr_in saddr;
	char	*addrstr = NULL;
	uint16_t port = 0;
	
	
	assert(dest);
	decode_connstr(dest, &addrstr, &port);
	if (!addrstr) {
		SYMERROR("there is no default for server address - cannot connect\n");
		goto errout;
	}
	if (!port) {
		SYMERROR("ther is no default for port number - cannot connect\n");
		goto errout;
	}
	memset(&saddr, 0, sizeof(saddr));
	saddr.sin_family = AF_INET;
	saddr.sin_port = htons(port);
	res = inet_pton(AF_INET, addrstr, &saddr.sin_addr);
	if (res != 1) {
		SYMERROR("invalid server address \"%s\"\n", addrstr);
		goto errout;
	}
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd < 0) {
		SYMERROR("cannot create socket: %s\n", STRERROR_R(errno));
		goto errout;
	}
	res = connect(sockfd, (const struct sockaddr *) &saddr, sizeof(saddr));
	if (res < 0) {
		SYMERROR("cannot connect to %s: %s\n", dest,
			STRERROR_R(errno));
		goto errout;
	} else {
		SYMDEBUG("connected to %s:%u\n", addrstr, port);
	}
	ctx = new_conn_ctx(role);
	if (!ctx) {
		close(sockfd);
		goto errout;
	}
	ctx->socket = true;
	ctx->cmdrfd = sockfd;
	ctx->cmdwfd = sockfd;
	if (role) set_role(ctx);
errout:
	if (addrstr) free(addrstr);
	return ctx;
}


/* %%>connect:<role>[:<id>][:<type>] 
 * <role> - role of this connection: global, channel, play, record, playrec
 * <id> - channel id to connect this socket to
 * <type> - type of data channel, assuming audio if missing
 *
 */

static void set_role(conn_ctx *ctx)
{
	bstring	buffer;
	int	res;
	
	assert(ctx);
	buffer = bfromcstralloc(CMDSTR_LEN, "%%>connect:");
	assert(buffer);
	res = bcatcstr(buffer, ctx->role);	assert(res == BSTR_OK);
	if (ctx->chan_id) {
		res = bconchar(buffer, ':');	assert(res == BSTR_OK);
		res = bcatcstr(buffer, ctx->chan_id); assert(res == BSTR_OK);
	}
	if (ctx->datatype) {
		res = bconchar(buffer, ':');	assert(res == BSTR_OK);
		res = bcatcstr(buffer, ctx->datatype); assert(res == BSTR_OK);
	}
	res = bconchar(buffer, '\n');	assert(res == BSTR_OK);
	res = bwrite(ctx->cmdwfd, buffer);
	assert(res > 0);
	bdestroy(buffer);
}


conn_ctx *yxt_conn_fds(void)
{
	conn_ctx *ctx = NULL;
	struct stat buf;
	int	res;
	
	ctx = new_conn_ctx(NULL);
	if (!ctx) goto errout;
	ctx->socket = false;
	ctx->cmdrfd = 0;
	ctx->cmdwfd = 1;
	ctx->logfd = 2;
	memset(&buf, 0, sizeof(buf));
	
	res = fstat(3, &buf);
	if (!res) ctx->brfd = 3;
	res = fstat(4, &buf);
	if (!res) ctx->bwfd = 4;
errout:
	return ctx;
}
/*
 * Caller must be sure to suspend all active operations 
 * before calling this. Otherwise it will start pulling
 * resources from under execution threads feets.
 */
void yxt_disconnect(conn_ctx *ctx)
{
	int	res;
	int	i;
	struct qblock_s	*q, *qtmp;
	char	*name;
	
	assert(ctx);
	mutex_lock(&ctx->spec_op);
	ctx->shutdown = true;
	mutex_unlock(&ctx->spec_op);
	/* uninstall all handlers and watchers */
	for(;;) {
		mutex_lock(&ctx->hndlr_mutex);
		if (ctx->handlers) {
			if (ctx->handlers->name) name = strdup(ctx->handlers->name);
			else name = NULL;
			mutex_unlock(&ctx->hndlr_mutex);
			(void)del_hndlr_wtchr(ctx, name);
		} else {
			mutex_unlock(&ctx->hndlr_mutex);
			break;
		}
	};
	/* 1. cancel the writer thread */
	if (ctx->writer_thread) {
		res = pthread_cancel(ctx->writer_thread);
		assert(res == 0);
		res = pthread_join(ctx->writer_thread, NULL);
		assert(res == 0);
	}
	/* 2. cancel reader threads  */
	for (i = 0; i < ctx->nreaders; i++) {
		if ((ctx->reader_threads)[i]) {
			res = pthread_cancel((ctx->reader_threads)[i]);
			assert(res == 0);
			res = pthread_join((ctx->reader_threads)[i], NULL);
			assert(res == 0);
		}
	}
	
	/* 3. walk the transaction table and cancel all transactions */
	mutex_lock(&ctx->trtab_mutex);
	for (i = 0; i < TRANS_TABLE_SIZE; i++) {
		q = ctx->trtab[i];
		while (q) {
			qtmp = q->next;
			if (q->want_reply) { 
				res = sem_post(&q->reply_sem);
			} else {
				if (q->msg) free_message(q->msg);
				free(q);
			}
			q = qtmp;
		}
	}
	mutex_unlock(&ctx->trtab_mutex);
	pthread_mutex_destroy(&ctx->trtab_mutex);
	/* 4. cancel any pending spec op. */
	mutex_lock(&ctx->spec_op);
	pthread_mutex_lock(&ctx->sop_info_mutex);
	if (ctx->sop_value) free(ctx->sop_value);
	pthread_cond_destroy(&ctx->sop_info_cond);
	mutex_unlock(&ctx->sop_info_mutex);
	pthread_mutex_destroy(&ctx->sop_info_mutex);
	mutex_unlock(&ctx->spec_op);
	pthread_mutex_destroy(&ctx->spec_op);
	/* 5. clean up the structures :*/
	/* cleanup request queue */
	mutex_lock(&ctx->queue_mutex);
	q = ctx->queue;
	while (q) {
		if (q->msg) free_message(q->msg);
		if (q->reply) free_message(q->reply);
		qtmp = q->next;
		free(q);
		q = qtmp;
	}
	mutex_unlock(&ctx->queue_mutex);
	pthread_mutex_destroy(&ctx->queue_mutex);
	/* free transaction id prefix */
	mutex_lock(&ctx->id_mutex);
	if (ctx->id_pfx) free(ctx->id_pfx);
	mutex_unlock(&ctx->id_mutex);
	pthread_mutex_destroy(&ctx->id_mutex);
	/* free role & other connect params string */
	if (ctx->role) free(ctx->role);
	if (ctx->chan_id) free(ctx->chan_id);
	if (ctx->datatype) free(ctx->datatype);
	/* 6. clean up interlock primitives */
	sem_destroy(&ctx->work_available);
	pthread_mutex_destroy(&ctx->wr_mutex);
	pthread_mutex_destroy(&ctx->rd_mutex);
	pthread_mutex_destroy(&ctx->hndlr_mutex);
	/* 7. close file descriptors if it's a socket link */
	if (ctx->socket) {
		if (ctx->cmdrfd >= 0) close(ctx->cmdrfd);
		if (ctx->cmdwfd >= 0) close(ctx->cmdwfd);
		if (ctx->logfd >= 0) close(ctx->logfd);
		if (ctx->brfd >= 0) close(ctx->brfd);
		if (ctx->bwfd >= 0) close(ctx->bwfd);
	}
	/* 8. destroy the context */
	free(ctx);
}

#define PTHREAD_NAME_LEN	16
int yxt_run(conn_ctx *ctx, int nreaders)
{
	int	res, i;
	pthread_attr_t	attrs;
	char	thread_name[PTHREAD_NAME_LEN + 1];
	
	assert(ctx);
	canrun(ctx);
	if (nreaders < 1) nreaders = YXT_DEF_NREADERS;
	else if (nreaders > YXT_MAX_NREADERS) nreaders = YXT_MAX_NREADERS;
	res = pthread_attr_init(&attrs);
	eassert(res == 0);
	for (i = 0; i < nreaders; i++) {
		res = pthread_create(&((ctx->reader_threads)[i]), &attrs, reader_thread, ctx);
		eassert(res == 0);
		memset(thread_name, 0, PTHREAD_NAME_LEN);
		snprintf(thread_name, PTHREAD_NAME_LEN, "yxtcrdr_%d", i);
		res = pthread_setname_np((ctx->reader_threads)[i], thread_name);
		if (!(res == 0)) SYMFATAL("res == %d [%s], errno = %d [%s]\n", res, STRERROR_R(res), errno, STRERROR_R(errno));
	}
	ctx->nreaders = nreaders;
	res = pthread_create(&ctx->writer_thread, &attrs, writer_thread, ctx);
	eassert(res == 0);
	res = pthread_setname_np(ctx->writer_thread, "yxt_cmdwr");
	eassert(res == 0);
	SYMDEBUG("yate socket reader and writer threads started\n");
	return YXT_OK;
}
#define MAX_ID_SIZE	256
#define TRANS_ID_SIZE	12
char *gen_id(conn_ctx *ctx, int *id)
{
	char *prefix = NULL;
	int	len, res;
	int	transaction;
	assert(ctx);
	prefix = malloc(MAX_ID_SIZE+1);
	if (!prefix) {
		SYMERROR("cannot allocate memory");
		goto	errout;
	}
	memset(prefix, 0, MAX_ID_SIZE+1);
	mutex_lock(&ctx->id_mutex);
	if (ctx->id_pfx) {
		strncpy(prefix, ctx->id_pfx, (MAX_ID_SIZE - TRANS_ID_SIZE));
	} else {
		res = gethostname(prefix, MAX_ID_SIZE);
		if (res) {
			mutex_unlock(&ctx->id_mutex);
			SYMERROR("gethostname() error: %s\n", STRERROR_R(errno));
			goto errout;
		}
		len = strlen(prefix);
		snprintf(prefix + len, (MAX_ID_SIZE - len), "%x.%lx.", 
			getpid(), (time(NULL) & 0xffffffff));
		ctx->id_pfx = strndup(prefix, MAX_ID_SIZE);
	}
	len = strlen(prefix);
	ctx->transaction++;
	transaction = ctx->transaction;
	mutex_unlock(&ctx->id_mutex);
	snprintf(prefix + len, (MAX_ID_SIZE - len), "%x", transaction);
	SYMDEBUGHARD("Transaction #%d, id=%s\n", transaction, prefix);
	if (id) *id = transaction;
	return prefix;
	
errout:	if (prefix) free(prefix);
	return NULL;
}



static void *reader_thread(void *context)
{
	int	res;
	bstring line = NULL;
	yatemsg *msg = NULL;
	char	*tmp;
	conn_ctx *ctx = (conn_ctx *)context;
	
	assert(ctx);
	mutex_lock(&ctx->rd_mutex);
	if (!ctx->rbs) ctx->rbs = bsopen(fdread, (void *)(&ctx->cmdrfd));
	assert(ctx->rbs);
	mutex_unlock(&ctx->rd_mutex);
	
	for (;;) {
		if (line) {
			bdestroy(line);
			line = NULL;
		}
		line = bfromcstralloc(MIN_BUFFER_LEN, "");
		(void)pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
		mutex_lock(&ctx->rd_mutex);
		res = bsreadln(line, ctx->rbs, '\n');
		mutex_unlock(&ctx->rd_mutex);
		(void)pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
#warning EOF is not handled - everything will go bananas if yate will close the link
	/* TODO - add EOF processing */
		if (res != BSTR_OK) continue;
		tmp = bstr2cstr(line, '_');
		SYMDEBUG("Response from yate:%s\n", tmp);
		bcstrfree(tmp);
		res = lsx(line);
		switch (res) {
			case LSX_MSGREQ:	/* a request from engine */
				msg = decode_request(line);
				if (!msg) continue;
				run_handlers(ctx, msg);
				break;
			case LSX_MSGREP:	/* reply from engine to our request */
				msg = decode_reply(line);
				if (!msg) continue;
				process_reply(ctx, msg);
				break;
			case LSX_MSGWATCH:	/* message is sent to watcher - reply is not needed */
				msg = decode_reply(line);
				if (!msg) continue;
				run_watchers(ctx, msg);
				break;
			case LSX_CMDREP:	/* reply to a command (install/uninstall) etc */
				finalize_command(ctx, line);
				break;
			case LSX_PARAM:		/* reply to a param request/set */
				finalize_param(ctx, line);
				break;
			case LSX_ERROR:		/* syntax error report from engine */
				tmp = bstr2cstr(line, '_');
				SYMERROR("Error message from yate: %s\n", tmp);
				bcstrfree(tmp);
				break;
			case LSX_CNXREP:	/* reply to a connect command */
			case LSX_UNKNOWN:
			default:
				tmp = bstr2cstr(line, '_');
				SYMERROR("unrecognized string from yate: %s\n", tmp);
				bcstrfree(tmp);
		}
		if (msg) free_message(msg);
		msg = NULL;
	}
	return NULL;
}

static void *writer_thread(void *context)
{
	int	res;
	struct qblock_s *item;
	conn_ctx *ctx = (conn_ctx *)context;
	
	assert(ctx);
	for (;;) {
		(void)pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
		res = sem_wait(&ctx->work_available);
		(void)pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
		if (res) {
			SYMERROR("sem_wait() error: %s\n", STRERROR_R(errno));
			continue;
		}
		item = dequeue_one(ctx);
		if (!item) continue;
		assert(item->msg);
		assert((item->want_reply != item->is_reply) 
			|| (!item->want_reply && !item->is_reply));
		if (item->want_reply) save_transaction(ctx, item);
		mutex_lock(&ctx->wr_mutex);
		res = send_message(ctx->cmdwfd, item->msg, !item->is_reply);
		mutex_unlock(&ctx->wr_mutex);
		if (item->want_reply) {
			if (res != YXT_OK) end_transaction(ctx, item);
		} else {
			free_message(item->msg);
			free(item);	/* item is freed by waiting dispatch() in case it's a transaction */
		}
	}
	return NULL;
}

/* returns unescaped, ':' delimited part of the message line 
 * start points to beginning ':'
 */
static unsigned char *msgstr_item(bstring line, int start, int end)
{
	bstring tmp1, tmp2;
	unsigned char	*res;

	tmp1 = bmidstr(line, start, end - start);
	tmp2 = bstr_unescape(tmp1);
	res = (unsigned char *)bstr2cstr(tmp2, '_');
	SYMDEBUGHARD("start=%d, end=%d, line=\"%s\"\n", start, end, res);
	bdestroy(tmp1);
	bdestroy(tmp2);
	return res;
}

/*
 * %%>message:<id>:<time>:<name>:<retvalue>[:<key>=<value>...]
 *
 */
static yatemsg *decode_request(bstring line)
{
	int	start, end;
	yatemsg	*msg = NULL;
	char	*timestr = NULL;
	
	assert(line);
	/* extracting id */
	start = bstrchrp(line, ':', 0);
	if (start == BSTR_ERR) goto errout;
	start++;
	end = bstrchrp(line, ':', start);
	if (end == BSTR_ERR) goto errout;
	msg = alloc_message(NULL);
	if (!msg) goto errout;
	msg->id = (char *)msgstr_item(line, start, end);
	
	/* extracting time */
	start = end + 1;
	end = bstrchrp(line, ':', start);
	if (end == BSTR_ERR) goto errout;
	timestr = (char *)msgstr_item(line, start, end);
	assert(timestr);
	msg->time = strtoull(timestr, NULL, 10);
	bcstrfree(timestr);
	
	/* extracting name */
	start = end + 1;
	end = bstrchrp(line, ':', start);
	if (end == BSTR_ERR) goto errout;
	msg->name = (char *)msgstr_item(line, start, end);

	/* extracting retvalue */
	start = end + 1;
	end = bstrchrp(line, ':', start);
	if (end == BSTR_ERR) goto errout;
	msg->retvalue = (char *)msgstr_item(line, start, end);
	
	decode_kvps(msg, line, end+1);

errout:	
	return msg;
}

/*
 * %%<message:<id>:<processed>:[<name>]:<retvalue>[:<key>=<value>...]
 *
 */
static yatemsg *decode_reply(bstring line)
{
	int	start, end;
	yatemsg	*msg = NULL;
	char	*prcstr = NULL;
	
	assert(line);
	/* extracting id */
	start = bstrchrp(line, ':', 0);
	if (start == BSTR_ERR) goto errout;
	start++;
	end = bstrchrp(line, ':', start);
	if (end == BSTR_ERR) goto errout;
	msg = alloc_message(NULL);
	if (!msg) goto errout;
	msg->id = (char *)msgstr_item(line, start, end);
	
	/* extracting processed flag*/
	start = end + 1;
	end = bstrchrp(line, ':', start);
	if (end == BSTR_ERR) goto errout;
	prcstr = (char *)msgstr_item(line, start, end);
	assert(prcstr);
	msg->processed = is_true(prcstr);
	bcstrfree(prcstr);

	/* extracting name */
	start = end + 1;
	end = bstrchrp(line, ':', start);
	if (end == BSTR_ERR) goto errout;
	msg->name = (char *)msgstr_item(line, start, end);

	/* extracting retvalue */
	start = end + 1;
	end = bstrchrp(line, ':', start);
	if (end == BSTR_ERR) goto errout;
	msg->retvalue = (char *)msgstr_item(line, start, end);
	
	decode_kvps(msg, line, end+1);
	SYMDEBUGHARD("at exit, msg=%p\n", msg);
errout:	
	return msg;
}


static void decode_kvps(yatemsg *m, bstring line, int start)
{
	kvp_list	*kl;
	int	end, equal;
	int	len;
	unsigned char *name;
	unsigned char *value;

	assert(m);
	kl = m->params;
	len = blength(line);
	assert(len > 0);
	while (start < (len - 1)) {
		name = NULL;
		value = NULL;
		end = bstrchrp(line, ':', start);
		if (end == BSTR_ERR) end = len - 1;
		equal = bstrchrp(line, '=', start);
		if (equal == BSTR_ERR) {
			name = msgstr_item(line, start, end);
		} else {
			name = msgstr_item(line, start, equal);
			value = msgstr_item(line, equal + 1, end);
		};
		kl = append_kvp(kl, (char *)name, (char *)value);
		if (name) bcstrfree((char *)name);
		if (value) bcstrfree((char *)value);
		start = end + 1;
	}
}

static int id2hash(char *id)
{
	int	res;
	char	*ptr;
	
	assert(id);
	ptr = strrchr(id, '.');
	if (ptr) {
		res = strtol(ptr + 1, NULL, 16);
		SYMDEBUGHARD("hash=%d for trans=%d\n", (res & TRANS_IDX_MASK), res);
	} else res = 0;
	res &= TRANS_IDX_MASK;
	return res;
}

static void save_transaction(conn_ctx *ctx, struct qblock_s *item)
{
	int	id;
	struct  qblock_s *qptr;
	
	assert(ctx);
	assert(item);
	assert(item->msg);
	assert(item->reply == NULL);
	assert(item->msg->id);
	id = id2hash(item->msg->id);
	mutex_lock(&ctx->trtab_mutex);
	qptr = ctx->trtab[id];
	SYMDEBUGHARD("ctx->trtab[%d]=%p, item=%p\n", id, qptr, item);
	item->prev = NULL;
	item->next = qptr;
	ctx->trtab[id] = item;
	mutex_unlock(&ctx->trtab_mutex);
}

static struct qblock_s *find_transaction(conn_ctx *ctx, char *id)
{
	struct qblock_s *qptr;
	int	hash;
	int	res;
	
	assert(ctx);
	assert(id);
	hash = id2hash(id);
	mutex_lock(&ctx->trtab_mutex);
	qptr = ctx->trtab[hash];
	while (qptr) {
		assert(qptr->msg);
		assert(qptr->msg->id);
		SYMDEBUGHARD("qptr=%p, stored id=%s, wanted id=%s\n", qptr, qptr->msg->id, id);
		res = strcmp(qptr->msg->id, id);
		if (res == 0) {
			SYMDEBUGHARD("pending transaction found\n");
			break;
		} else {
			qptr = qptr->next;
		}
	}
	mutex_unlock(&ctx->trtab_mutex);
	return qptr;
}


static void process_reply(conn_ctx *ctx, yatemsg *msg)
{
	struct qblock_s *qptr;

	assert(ctx);
	assert(msg);
	SYMDEBUGHARD("processing reply with id=\"%s\"\n", msg->id);
	qptr = find_transaction(ctx, msg->id);
	if (!qptr) {
		SYMDEBUG("no reply expected for id=%s, reply discarded\n", msg->id);
		return;
	}
	assert(qptr->want_reply);
	qptr->reply = copy_message(msg);
	end_transaction(ctx, qptr);
}

static void end_transaction(conn_ctx *ctx, struct qblock_s *item)
{
	int	id, res;
	
	assert(ctx);
	assert(item);
	assert(item->msg);
	assert(item->msg->id);
	assert(item->want_reply);

	id = id2hash(item->msg->id);
	mutex_lock(&ctx->trtab_mutex);
	SYMDEBUGHARD("ctx->trtab[%d]=%p, item=%p, item->prev=%p, item->next=%p, want_reply=%d\n",
			 id, ctx->trtab[id], item, item->prev, item->next, item->want_reply);
	if (item->prev == NULL) {
		ctx->trtab[id] = item->next;
	} else {
		item->prev->next = item->next;
	} 
	if (item->next) item->next->prev = item->prev;
	mutex_unlock(&ctx->trtab_mutex);
	item->next = NULL;
	item->prev = NULL;
	if (item->want_reply) {
		res = sem_post(&item->reply_sem);
		assert(!res);
	}
	SYMDEBUGHARD("at exit\n");
}

/* dequeue one qblock from the work queue */
static struct qblock_s *dequeue_one(conn_ctx *ctx)
{
	struct qblock_s	*qptr;

	assert(ctx);
	mutex_lock(&ctx->queue_mutex);
	qptr = ctx->queue;
	if (qptr) {
		if (qptr->next) qptr->next->prev = NULL;
		ctx->queue = qptr->next;
		qptr->next = NULL;
		qptr->prev = NULL;
	} 
	mutex_unlock(&ctx->queue_mutex);
	return qptr;
}

/* put one qblock to the work queue */
static void enqueue_request(conn_ctx *ctx, struct qblock_s *item)
{
	struct qblock_s *qptr;

	assert(ctx);
	assert(item);
	assert(item->next == NULL);
	assert(item->prev == NULL);
	mutex_lock(&ctx->queue_mutex);
	qptr = ctx->queue;
	if (qptr) {
		while (qptr->next) qptr = qptr->next;
		qptr->next = item;
		item->prev = qptr;
	} else {
		ctx->queue = item;
	}
	mutex_unlock(&ctx->queue_mutex);
}


/* put one qblock to the work queue after the last reply */
static void enqueue_reply(conn_ctx *ctx, struct qblock_s *item)
{
	struct qblock_s *qptr;

	assert(ctx);
	assert(item);
	assert(item->next == NULL);
	assert(item->prev == NULL);
	assert(item->is_reply);
	mutex_lock(&ctx->queue_mutex);
	qptr = ctx->queue;
	if (qptr) {
		while (qptr->next && qptr->is_reply) qptr = qptr->next;
		if (qptr->next) {
			qptr->next->prev=item;
			item->next = qptr->next;
		}
		qptr->next = item;
		item->prev = qptr;
	} else {
		ctx->queue = item;
	}
	mutex_unlock(&ctx->queue_mutex);
}

/* submit one message to the yate engine without waiting for reply */
int yxt_enqueue(conn_ctx *ctx, yatemsg *m)
{
	canrun(ctx);
	return (dispatch_or_enqueue(ctx, m, NULL, false));
}

int yxt_dispatch(conn_ctx *ctx, yatemsg *m, yatemsg **reply)
{
	assert(reply);
	canrun(ctx);
	return (dispatch_or_enqueue(ctx, m, reply, false));
}

/* dispatch or enqueue one message to the yate engine and wait for reply */
static int dispatch_or_enqueue(conn_ctx *ctx, yatemsg *m, 
		yatemsg **reply, bool is_reply)
{
	struct qblock_s *qptr;
	int res;

	assert(ctx);
	assert(m);
	qptr = malloc(sizeof(struct qblock_s));
	if (!qptr) {
		SYMERROR("cannot allocate memory\n");
		return YXT_FAIL;
	}
	memset(qptr, 0, sizeof(struct qblock_s));
	qptr->msg = copy_message(m);
	qptr->is_reply = is_reply;
	if (!m->id) qptr->msg->id = gen_id(ctx, NULL);
	if (reply && (!is_reply)) {
		res = sem_init(&qptr->reply_sem, 0, 0);
		assert(res == 0);
		qptr->want_reply = true;
	}
	SYMDEBUGHARD("qptr about to be stored at %p, want_reply=%d\n", qptr, qptr->want_reply);
	if (is_reply) enqueue_reply(ctx, qptr);
	else enqueue_request(ctx, qptr);
	do {
		res = sem_post(&ctx->work_available);
		if (res) {
			if (errno == EOVERFLOW) {
				SYMERROR("Whooaa! Queue length is too big. work_available semaphore overflowed! Will usleep() and retry.\n");
				usleep(SEM_POST_RETRY_SLEEP);
			} else {
				SYMFATAL("sem_post error: %s\n", STRERROR_R(errno));
				abort();
			}
		}
	} while (res);
	/* WARNING! if qptr->want_reply was set to false then qptr doesn't
	 * exists at this time! It gets freed by writer_thread.
	 */
	if (reply && (!is_reply)) {
		sem_wait(&qptr->reply_sem);
		SYMDEBUGHARD("reply received\n");
		*reply = qptr->reply;
		sem_destroy(&qptr->reply_sem);
		free_message(qptr->msg);
		free(qptr);
	}
	return YXT_OK;
}

static handler_list *find_handler(conn_ctx *ctx, char *name)
{
	int	res;
	handler_list *hl;
	bool	exists;
	
	exists = false;
	for (hl = ctx->handlers; hl; hl = hl->next) {
		if ((hl->name) && (name)) {
			res = strcmp(hl->name, name);
			if (!res) {
				exists = true;
				break;
			}
		} else {
			if (!hl->name && !name) {
				exists = true;
				break;
			}
		}
	}
	return (exists ? hl : NULL);
}

/* remove the handler or watcher  from handler list only */
static int remove_handler(conn_ctx *ctx, char *name)
{
	handler_list	*hl;
	
	assert(ctx);
	
	mutex_lock(&ctx->hndlr_mutex);
	hl = find_handler(ctx, name);
	if (hl) {
		if (hl->next) hl->next->prev = hl->prev;
		if (hl->prev) {
			hl->prev->next = hl->next;
		} else {
			ctx->handlers = hl->next;
		}
		if (hl->name) free(hl->name);
		free(hl);
	}
	mutex_unlock(&ctx->hndlr_mutex);
	if (hl) return YXT_OK;
	else return YXT_FAIL;
}


static int insert_handler(conn_ctx *ctx, handler hdlr, char *name, 
		int prio, void *arg, bool is_watcher)
{
	handler_list *hl;
	handler_list *hl_item = NULL;
	bool	exists;
	int	res;

	assert(ctx);
	assert(hdlr);

	hl_item = malloc(sizeof(handler_list));
	if (!hl_item) goto errout;

	memset(hl_item, 0, sizeof(handler_list));
	hl_item->hdlr = hdlr;
	hl_item->is_watcher = is_watcher;
	hl_item->arg = arg;
	hl_item->prio = prio;
	if (name) {
		hl_item->name = strdup(name);
		hl_item->is_catchall = (strcmp(name, CATCHALL_NAME) == 0);
	}
	exists = false;
	mutex_lock(&ctx->hndlr_mutex);
	for (hl = ctx->handlers; hl; hl = hl->next) {
		if ((hl->name) && (hl_item->name)) {
			res = strcmp(hl->name, hl_item->name);
			if (!res) {
				exists = true;
				break;
			}
		} else {
			if (!hl->name && !hl_item->name) {
				exists = true;
				break;
			}
		}
	}
	if (exists) {
		mutex_unlock(&ctx->hndlr_mutex);
		SYMERROR("%s %s is already installed\n", 
			is_watcher ? "watcher" : "handler", hl_item->name);
		goto errout;
	}
	if (ctx->handlers && hl_item->is_catchall) {
		mutex_unlock(&ctx->hndlr_mutex);
		SYMERROR("default CATCHALL handler can only be installed first\n");
		goto errout;
	}
	hl_item->next = ctx->handlers;
	hl_item->prev = NULL;
	if (hl_item->next) {
		hl_item->next->prev = hl_item;
	}
	ctx->handlers = hl_item;
	mutex_unlock(&ctx->hndlr_mutex);
	if (hl_item->is_catchall) SYMDEBUG("catch-all handler installed\n");
	return YXT_OK;

errout:
	if (hl_item) {
		if (hl_item->name) free(hl_item->name);
		free(hl_item);
	}
	return YXT_FAIL;
}

static void spec_op_init(conn_ctx *ctx)
{
	assert(ctx);
	mutex_lock(&ctx->sop_info_mutex);
	if (ctx->sop_value) {
		SYMDEBUG("sop_value is not cleaned up (this is not an error)\n");
		free(ctx->sop_value);
		ctx->sop_value = NULL;
	}
	ctx->sop_status = SOP_STARTED;
	mutex_unlock(&ctx->sop_info_mutex);
}

static int spec_op_wait(conn_ctx *ctx, int *result)
{
	int	res;
	struct timespec	ts;
	
	assert(ctx);

	mutex_lock(&ctx->sop_info_mutex);
	res = clock_gettime(CLOCK_REALTIME, &ts);
	assert(res == 0);
	ts.tv_sec += SPEC_OP_TIMEOUT;
	res = 0;
	while (!((ctx->sop_status == SOP_OK) || 
		 (ctx->sop_status == SOP_FAIL)) && (res == 0)) {
		res = pthread_cond_timedwait(&ctx->sop_info_cond, &ctx->sop_info_mutex, &ts);
//		SYMDEBUGHARD("res=%d, ctx->sop_status=%d\n", res, ctx->sop_status);
	}
	*result = ctx->sop_status;
	ctx->sop_status = SOP_NONE;
	mutex_unlock(&ctx->sop_info_mutex);
	return ((res == 0) ? YXT_OK : YXT_FAIL);
}

static void finalize_command(conn_ctx *ctx, bstring line)
{
	bstring	status;
	int	res, tail;
	char	*str = NULL;
	bool	opc_status = false;
	bool	spurious = false;
	
	assert(ctx);
	tail = blength(line);
	res = bstrrchr(line, ':');
	if ((res != BSTR_ERR) && (res + 1 < tail)) {
		status = bmidstr(line, res + 1, tail - (res + 1));
		res = brtrimws(status);
		assert(res == BSTR_OK);
		str = bstr2cstr(status, '_');
		SYMDEBUGHARD("res=%d, tail=%d, str=\"%s\"\n", res, tail, str);
		if (is_bool(str) && is_true(str)) opc_status = true;
		bdestroy(status);
		bcstrfree(str);
	}
	mutex_lock(&ctx->sop_info_mutex);
	if (ctx->sop_status != SOP_STARTED) {
		str = bstr2cstr(line, '_');
		SYMERROR("spurious command response: %s\n", str);
		bcstrfree(str);
		spurious = true;
	}
	ctx->sop_status = (opc_status ? SOP_OK : SOP_FAIL);
	mutex_unlock(&ctx->sop_info_mutex);
	if (!spurious) pthread_cond_broadcast(&ctx->sop_info_cond);
}

static void finalize_param(conn_ctx *ctx, bstring line)
{
	bstring	status, tmp;
	bstring	value = NULL;
	int	res, res2, tail;
	char	*str = NULL;
	bool	opc_status = false;
	bool	spurious = false;
	
	assert(ctx);
	tail = blength(line);
	res = bstrrchr(line, ':');
	if ((res != BSTR_ERR) && (res + 1 < tail)) {
		status = bmidstr(line, res + 1, tail - (res + 1));
		res2 = brtrimws(status);
		assert(res2 == BSTR_OK);
		str = bstr2cstr(status, '_');
		SYMDEBUGHARD("res=%d, tail=%d, str=\"%s\"\n", res, tail, str);
		if (is_bool(str) && is_true(str)) opc_status = true;
		bdestroy(status);
		bcstrfree(str);
		tail = res;
		if (--res) {
			res = bstrrchrp(line, ':', res);
			if (res != BSTR_ERR) {
				tmp = bmidstr(line, res + 1, (tail - res) - 1);
				value = bstr_unescape(tmp);
				bdestroy(tmp);
			}
		}
	}
	mutex_lock(&ctx->sop_info_mutex);
	if (ctx->sop_status != SOP_STARTED) {
		str = bstr2cstr(line, '_');
		SYMERROR("spurious command response: %s\n", str);
		bcstrfree(str);
		spurious = true;
	}
	ctx->sop_status = (opc_status ? SOP_OK : SOP_FAIL);
	if (ctx->sop_value) free(ctx->sop_value);
	if (value) {
		ctx->sop_value = (unsigned char*)bstr2cstr(value, '_');
		SYMDEBUGHARD("special opc ret value=\"%s\"\n", ctx->sop_value);
		bdestroy(value);
	} else ctx->sop_value = NULL;
	mutex_unlock(&ctx->sop_info_mutex);
	if (!spurious) pthread_cond_broadcast(&ctx->sop_info_cond);
}

int yxt_add_default_handler(conn_ctx *ctx, handler hdlr, void *arg)
{
	int	res;
	
	res = insert_handler(ctx, hdlr, CATCHALL_NAME, 0, arg, false);
	return res;
}

/* add handler or watcher */
static int add_hndlr_wtchr(conn_ctx *ctx, handler hdlr,
		char *name, int prio, char *fname, 
		char *fvalue, void *arg, bool is_watcher)
{
	int	res;
	int	sop_status;
	bstring	cmdstr;

	mutex_lock(&ctx->spec_op);
	res = insert_handler(ctx, hdlr, name, prio, arg, is_watcher);
	if (res != YXT_OK) goto errout;
	
	if (is_watcher) {
		cmdstr = bfromcstralloc(CMDSTR_LEN, "%%>watch:" );
		assert(cmdstr);
		if (name) {
			res = bcat_escapecstr(cmdstr, name); 
			assert(res == BSTR_OK);
		}
	} else {
		cmdstr = bformat("%%%%>install:%d:", prio);
		assert(cmdstr);
		res = bcat_escapecstr(cmdstr, name); 
		assert(res == BSTR_OK);
		if (fname) {
			res = bconchar(cmdstr, ':'); assert(res == BSTR_OK);
			res = bcat_escapecstr(cmdstr, fname); 
			assert(res == BSTR_OK);
			if (fvalue) {
				res = bconchar(cmdstr, ':'); 
				assert(res == BSTR_OK);
				res = bcat_escapecstr(cmdstr, fvalue); 
				assert(res == BSTR_OK);
			}
		}
	}
	res = bconchar(cmdstr, '\n');

	mutex_lock(&ctx->wr_mutex);
	spec_op_init(ctx);
	res = bwrite(ctx->cmdwfd, cmdstr);
	mutex_unlock(&ctx->wr_mutex);
	bdestroy(cmdstr);
	res = spec_op_wait(ctx, &sop_status);
	if ((res != YXT_OK) || (sop_status != SOP_OK)) {
		SYMERROR("unable to install handler/watcher. res=%d, sop_status=%d\n",
			res, sop_status);
		remove_handler(ctx, name);
		res = YXT_FAIL;
	} else res = YXT_OK;
errout:
	mutex_unlock(&ctx->spec_op);
	return res;
}

static int del_hndlr_wtchr(conn_ctx *ctx, char *name)
{
	handler_list *hl;
	int	res;
	int	sop_status;
	bstring cmdstr;
	
	mutex_lock(&ctx->spec_op);
	mutex_lock(&ctx->hndlr_mutex);
	hl = find_handler(ctx, name);
	mutex_unlock(&ctx->hndlr_mutex);
	if (!hl) {
		SYMERROR("nonexisting handler/watcher %s - cannot remove\n", name);
		res = YXT_FAIL;
		goto errout;
	}
	cmdstr = bfromcstralloc(CMDSTR_LEN, ((hl->is_watcher) ? "%%>unwatch:" : "%%>uninstall:" ));
	assert(cmdstr);
	if (name) {
		res = bcat_escapecstr(cmdstr, name); 
		assert(res == BSTR_OK);
	}
	res = bconchar(cmdstr, '\n');

	mutex_lock(&ctx->wr_mutex);
	spec_op_init(ctx);
	res = bwrite(ctx->cmdwfd, cmdstr);
	mutex_unlock(&ctx->wr_mutex);
	bdestroy(cmdstr);
	res = spec_op_wait(ctx, &sop_status);
	if ((res != YXT_OK) || (sop_status != SOP_OK)) {
		SYMERROR("unable to uninstall handler/watcher. res=%d, sop_status=%d\n",
			res, sop_status);
		res = YXT_FAIL;
	} else res = remove_handler(ctx, name);
errout:	
	mutex_unlock(&ctx->spec_op);
	return res;
}

int yxt_add_handler(conn_ctx *ctx, handler hdlr,
		char *name, int prio, void *arg)
{
	canrun(ctx);
	return (add_hndlr_wtchr(ctx, hdlr, name, prio, 
				NULL, NULL, arg, false));
}

int yxt_add_handler_filtered(conn_ctx *ctx, handler hdlr,
		char *name, int prio, char *fname, 
		char *fvalue, void *arg) 
{
	canrun(ctx);
	return (add_hndlr_wtchr(ctx, hdlr, name, prio, 
				fname, fvalue, arg, false));
}

int yxt_add_watcher(conn_ctx *ctx, handler hdlr, char *name, void *arg)
{
	canrun(ctx);
	return (add_hndlr_wtchr(ctx, hdlr, name, 100, 
				NULL, NULL, arg, true));
}


static void run_handlers(conn_ctx *ctx, yatemsg *msg)
{
	handler_list *hl;
	int	res;
	struct dwhook dwhs;
	bool	run_hook = false;
	
	assert(ctx);
	assert(msg);
	assert(msg->name);

	mutex_lock(&ctx->hndlr_mutex);
	for (hl = ctx->handlers; hl; hl = hl->next) {
		if (!hl->hdlr) continue;
		if (hl->is_watcher) continue;
		if (!hl->name) continue;
		if (!hl->is_catchall) res = strcmp(msg->name, hl->name);
		else res = 0;
		if (!res) {
			memset(&dwhs, 0, sizeof(struct dwhook));
			res = hl->hdlr(ctx, msg, hl->arg, &dwhs);
			if ((res == YXT_PROCESSED) || (res == YXT_CHANGED)) {
				if (res == YXT_PROCESSED) msg->processed = true;
				if (dwhs.runner) run_hook = true;
//				SYMDEBUGHARD("msg=%p, processed=%d, res=%d, arg=%p\n", msg, msg->processed, res, hl->arg);
				break;
			}
		}
	}
	mutex_unlock(&ctx->hndlr_mutex);
	(void)dispatch_or_enqueue(ctx, msg, NULL, true);
	if (run_hook) dwhs.runner(dwhs.arg);
}


static void run_watchers(conn_ctx *ctx, yatemsg *msg)
{
	handler_list *hl;
	int	res;
	
	assert(ctx);
	assert(msg);
	assert(msg->name);
	mutex_lock(&ctx->hndlr_mutex);
	for (hl = ctx->handlers; hl; hl = hl->next) {
		if (!hl->hdlr) continue;
		if (!hl->is_watcher) continue;
		if (hl->name) res = strcmp(msg->name, hl->name);
		else res = 0;
		if (!res) {
			assert(hl->hdlr);
			(void)hl->hdlr(ctx, msg, hl->arg, NULL);
		}
	}
	mutex_unlock(&ctx->hndlr_mutex);
}

int yxt_remove_handler(conn_ctx *ctx, char *name, int prio)
{
	canrun(ctx);
	return (del_hndlr_wtchr(ctx, name));
}

int yxt_remove_watcher(conn_ctx *ctx, char *name)
{
	canrun(ctx);
	return (del_hndlr_wtchr(ctx, name));
}

int yxt_set_param(conn_ctx *ctx, char *name, char *value)
{
	int	res;
	int	sop_status;
	bstring	cmdstr;

	assert(ctx);
	assert(name);
	assert(value);

	canrun(ctx);
	mutex_lock(&ctx->spec_op);
	
	cmdstr = bfromcstralloc(CMDSTR_LEN, "%%>setlocal:" );
	assert(cmdstr);
	res = bcat_escapecstr(cmdstr, name); assert(res == BSTR_OK);
	res = bconchar(cmdstr, ':');	assert(res == BSTR_OK);
	res = bcat_escapecstr(cmdstr, value); assert(res == BSTR_OK);
	res = bconchar(cmdstr, '\n');	assert(res == BSTR_OK);
	
	mutex_lock(&ctx->wr_mutex);
	spec_op_init(ctx);
	res = bwrite(ctx->cmdwfd, cmdstr);
	mutex_unlock(&ctx->wr_mutex);
	bdestroy(cmdstr);
	res = spec_op_wait(ctx, &sop_status);
	if ((res != YXT_OK) || (sop_status != SOP_OK)) {
		SYMERROR("unable to request parameter from yate, res=%d, sop_status=%d\n",
			res, sop_status);
		res = YXT_FAIL;
	} else res = YXT_OK;
	mutex_unlock(&ctx->spec_op);
	return res;
}


int yxt_get_param(conn_ctx *ctx, char *name, char **value)
{
	int	res;
	int	sop_status;
	bstring	cmdstr;

	assert(ctx);
	assert(name);
	assert(value);
	
	canrun(ctx);
	mutex_lock(&ctx->spec_op);
	
	cmdstr = bfromcstralloc(CMDSTR_LEN, "%%>setlocal:" );
	assert(cmdstr);
	res = bcat_escapecstr(cmdstr, name); 
	assert(res == BSTR_OK);
	res = bconchar(cmdstr, ':');	
	assert(res == BSTR_OK);
	res = bconchar(cmdstr, '\n');
	assert(res == BSTR_OK);
	
	mutex_lock(&ctx->wr_mutex);
	spec_op_init(ctx);
	res = bwrite(ctx->cmdwfd, cmdstr);
	mutex_unlock(&ctx->wr_mutex);
	bdestroy(cmdstr);
	res = spec_op_wait(ctx, &sop_status);
	if ((res != YXT_OK) || (sop_status != SOP_OK)) {
		SYMERROR("unable to request parameter from yate, res=%d, sop_status=%d\n",
			res, sop_status);
		res = YXT_FAIL;
	} else res = YXT_OK;
	mutex_lock(&ctx->sop_info_mutex);
	if (ctx->sop_value) *value = strdup((char *)(ctx->sop_value));
	else *value = NULL;
	free(ctx->sop_value);
	ctx->sop_value = NULL;
	mutex_unlock(&ctx->sop_info_mutex);
	mutex_unlock(&ctx->spec_op);
	return res;
}

int yxt_log(conn_ctx *ctx, char *text)
{
	int	res, fd;
	int	len;
	bstring	txtstr;
	
	assert(ctx);
	assert(text);
	canrun(ctx);
	len = strlen(text);
	if (len > 0) {
		mutex_lock(&ctx->spec_op);
		txtstr = bfromcstralloc(CMDSTR_LEN, (ctx->logfd < 0) ? "%%>output:" : "" );
		assert(txtstr);
		res = bcatcstr(txtstr, text);
		assert(res == BSTR_OK);
		if (text[len - 1] != '\n') {
			res = bconchar(txtstr, '\n');
			assert(res == BSTR_OK);
		}
		if (ctx->logfd < 0) fd = ctx->cmdwfd;
		else fd = ctx->logfd;
		mutex_lock(&ctx->wr_mutex);
		(void)bwrite(fd, txtstr);
		mutex_unlock(&ctx->wr_mutex);
		bdestroy(txtstr);
		mutex_unlock(&ctx->spec_op);
	}
	return YXT_OK;
}

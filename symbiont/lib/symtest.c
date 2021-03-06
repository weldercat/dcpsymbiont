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
#include <stdio.h>
#include <errno.h>
#include <assert.h>
#include <unistd.h>

#include <symbiont/symbiont.h>

#define SOCKPATH	"/var/run/yate/ysock"
yatemsg *new_symbiont_test(void);
yatemsg *new_call_route(void);
int test_watcher(conn_ctx *ctx, yatemsg *msg, void *arg, struct dwhook *dwh);
int call_route_hndlr(conn_ctx *ctx, yatemsg *msg, void *arg, struct dwhook *dwh);
int call_exec_hndlr(conn_ctx *ctx, yatemsg *msg, void *arg, struct dwhook *dwh);


int test_watcher(conn_ctx *ctx, yatemsg *msg, void *arg, struct dwhook *dwh)
{
	assert(msg);
//	SYMINFO("at enter, msg=%p, ctx=%p, arg=%p\n", msg, ctx, arg);
//	SYMINFO("arg=%p,\n   message name=\"%s\", time=%s\n", arg, msg->name, get_msg_param(msg, "time"));
//	dump_message(msg);
	return YXT_OK;
}

int call_route_hndlr(conn_ctx *ctx, yatemsg *msg, void *arg, struct dwhook *dwh)
{
	SYMINFO("at enter, msg=%p, ctx=%p, arg=%p\n", msg, ctx, arg);
	SYMINFO("arg=%p,\n   message name=\"%s\", time=%s\n", arg, msg->name, get_msg_param(msg, "time"));
	dump_message(msg);
	set_msg_retvalue(msg, "tone/outoforder");
	set_msg_param(msg, "called", "gombo");
	dump_message(msg);
	return YXT_PROCESSED;
}

int call_exec_hndlr(conn_ctx *ctx, yatemsg *msg, void *arg, struct dwhook *dwh)
{
	char	*callto;
	
	SYMINFO("at enter, msg=%p, ctx=%p, arg=%p\n", msg, ctx, arg);
	SYMINFO("arg=%p,\n   message name=\"%s\", time=%s\n", arg, msg->name, get_msg_param(msg, "time"));
	dump_message(msg);
	callto = get_msg_param(msg, "callto");
	if (callto) {
		if (strcmp(callto, "dcp/test") == 0) {
			remove_msg_param(msg, "callto");
			set_msg_param(msg, "callto", "imt/dcp");
			SYMINFO("callto replaced\n");
		}
	}
	dump_message(msg);
	return YXT_OK;
}


yatemsg *new_symbiont_test(void)
{
	yatemsg	*msg;

	msg = alloc_message("symbiont.test");
	assert(msg);
	set_msg_param(msg, "name1", "val1");
	set_msg_param(msg, "name2", "val2");
	set_msg_param(msg, "sex_variety", "in the pussy");
	set_msg_param(msg, "sex_variety2", "in the ass");
	return msg;
}



yatemsg *new_call_route(void)
{
	yatemsg	*msg;

	msg = alloc_message("call.route");
	assert(msg);
	set_msg_param(msg, "id", "dcp/1");
	set_msg_param(msg, "module", "dcp");
	set_msg_param(msg, "status", "incoming");
	set_msg_param(msg, "direction", "incoming");
	set_msg_param(msg, "caller", "barsyatka");
	set_msg_param(msg, "called", "99991008");
	set_msg_param(msg, "antiloop", "19");
	return msg;
}


int main(int argc, char **argv)
{
	int	res, i;
	conn_ctx *ctx;
	yatemsg	 *msg = NULL;
	yatemsg	*reply = NULL;
//	char	*pvalue = NULL;
//	char	tmpbuf[256];
	
	SYMINFO("about to start symbiont connection...\n");
	ctx = yxt_conn_unix(SOCKPATH, "global");
	if (!ctx) {
		SYMFATAL("cannot create context\n");
		goto out;
	}
	sleep(1);
	res = yxt_run(ctx, 3);
	assert(res == YXT_OK);
	res = yxt_add_watcher(ctx, test_watcher, "engine.timer", (void *)0x1000);
	assert(res == YXT_OK);
	yxt_log(ctx, "test log 1");
	SYMINFO("Watcher installed...\n");
	res = yxt_add_handler_filtered(ctx, call_route_hndlr, "call.route", 10, 
				"called", "99991066", (void *)0x2000);
	assert(res == YXT_OK);
	SYMINFO("Handler installed\n");
	res = yxt_add_handler(ctx, call_exec_hndlr, "call.execute", 10, NULL);
	assert(res == YXT_OK);
	SYMINFO("call.execute handler installed\n");
	
	for(;;) {
		sleep(3);
	}
	return 0;

	for(i = 0; i < 2; i++) {
//		printf("\033[H");
		if (msg) free_message(msg);
#if 0
		msg = new_symbiont_test();
#endif
		msg = new_call_route();
//		dump_message(msg);
		res = yxt_dispatch(ctx, msg, &reply);
		SYMINFO("message dispatched, res=%d\n", res);
		if (reply) {
			SYMINFO(" - reply received\n");
			dump_message(reply);
			SYMINFO("Handlers=%s\n", get_msg_param(reply, "handlers"));
			SYMINFO("Shooshpan=%s\n", get_msg_param(reply, "shooshpan"));
				free_message(reply);
			reply = NULL;
		} else {
			SYMINFO(" - no reply recieved\n");
		}
		sleep(5);
//		if (i == 10) {
//			res = yxt_remove_watcher(ctx, "engine.timer");
//			assert(res == YXT_OK);
//		}
	}

// trackparam test
#if 0
	for (i=0; i < 10000; i++) {
		memset(tmpbuf, 0, 256);
		snprintf(tmpbuf, 255, "dcp_%d", i);
		res = yxt_set_param(ctx, "trackparam", tmpbuf);
		assert(res == YXT_OK);

		res = yxt_get_param(ctx, "engine.nodename", &pvalue);
		assert(res == YXT_OK);
		SYMINFO("engine.nodename=%s\n", pvalue);
		if (pvalue) {
			free(pvalue);
			pvalue = NULL;
		}

		res = yxt_get_param(ctx, "trackparam", &pvalue);
		assert(res == YXT_OK);
		SYMINFO("trackparam=%s\n", pvalue);
		if (pvalue) {
			free(pvalue);
			pvalue = NULL;
		}
//		sleep(2);
	}
#endif 
	SYMINFO("main thread is sleeping - handler is active\n");
	sleep(3);
//	for(i = 0; i < 100; i++)	{
//		snprintf(tmpbuf, 255, "logger test %d", i);
//		if (i == 2) {
//			res = yxt_remove_handler(ctx, "call.route", 10);
//			assert(res == YXT_OK);
//			yxt_log(ctx, "handler removed\n");
//		}
//		yxt_log(ctx, tmpbuf);
//		sleep(10);
//	}
	SYMINFO("Sending chan.masquerade....\n");
	msg = alloc_message("chan.masquerade");
	assert(msg);
	set_msg_param(msg, "id", "dcp/1");
	set_msg_param(msg, "message", "call.execute");
	set_msg_param(msg, "callto", "tone/info");
	set_msg_param(msg, "reason", "divert");
	res = yxt_enqueue(ctx, msg);
	assert(res == YXT_OK);
	if (msg) free_message(msg);
	sleep(10);
	
out:
	return 0;
}


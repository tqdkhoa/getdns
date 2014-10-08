/**
 * \file
 * \brief Public interfaces to getdns, include in your application to use getdns API.
 *
 * This source was taken from the original pseudo-implementation by
 * Paul Hoffman.
 */

/*
 * Copyright (c) 2013, NLNet Labs, Verisign, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * * Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 * * Neither the names of the copyright holders nor the
 *   names of its contributors may be used to endorse or promote products
 *   derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL Verisign, Inc. BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/time.h>
#include <stdio.h>
#include "getdns/getdns_ext_libev.h"
#include "config.h"
#include "types-internal.h"

#ifdef HAVE_LIBEV_EV_H
#include <libev/ev.h>
#else
#include <ev.h>
#endif

#define RETURN_IF_NULL(ptr, code) if(ptr == NULL) return code;

typedef struct getdns_libev {
	getdns_eventloop_vmt *vmt;
	struct ev_loop       *loop;
	struct mem_funcs      mf;
} getdns_libev;

static getdns_return_t getdns_libev_cleanup(getdns_eventloop *loop);
static getdns_return_t getdns_libev_schedule_read(getdns_eventloop *loop,
    int fd, uint64_t timeout, getdns_eventloop_event *ev);
static getdns_return_t getdns_libev_schedule_timeout
    (getdns_eventloop *loop, uint64_t timeout, getdns_eventloop_event *ev);
static getdns_return_t getdns_libev_clear_event
    (getdns_eventloop *loop, getdns_eventloop_event *ev);

static getdns_eventloop_vmt getdns_libev_vmt = {
	getdns_libev_cleanup,
	getdns_libev_schedule_read,
	getdns_libev_clear_event,
	getdns_libev_schedule_timeout,
	getdns_libev_clear_event,
};

getdns_return_t
getdns_extension_set_libev_loop(getdns_context *context,
    struct ev_loop *loop)
{
	getdns_libev *ext;
	getdns_return_t r;

	RETURN_IF_NULL(context, GETDNS_RETURN_BAD_CONTEXT);
	RETURN_IF_NULL(loop, GETDNS_RETURN_INVALID_PARAMETER);

	if ((r = getdns_context_detach_eventloop(context)))
		return r;

	ext = GETDNS_MALLOC(*priv_getdns_context_mf(context), getdns_libev);
	ext->vmt  = &getdns_libev_vmt;
	ext->loop = loop;
	ext->mf   = *priv_getdns_context_mf(context);

	return getdns_context_set_eventloop(context, (getdns_eventloop *)&ext);
}

static getdns_return_t
getdns_libev_cleanup(getdns_eventloop *loop)
{
	getdns_libev *ext = (getdns_libev *)loop;

	GETDNS_FREE(ext->mf, ext);
	return GETDNS_RETURN_GOOD;
}

static void
getdns_libev_read_cb(struct ev_loop *l, struct ev_io *io, int revents)
{
        getdns_eventloop_event *el_ev = (getdns_eventloop_event *)io->data;
        assert(el_ev->read_cb);
        el_ev->read_cb(el_ev->userarg);
}

static void
getdns_libev_timeout_cb(struct ev_loop *l, struct ev_timer *timer, int revent)
{
        getdns_eventloop_event *el_ev = (getdns_eventloop_event *)timer->data;
        assert(el_ev->timeout_cb);
        el_ev->timeout_cb(el_ev->userarg);
}

typedef struct io_timer {
	ev_io    io;
	ev_timer timer;
} io_timer;

static getdns_return_t
getdns_libev_schedule_read(getdns_eventloop *loop,
    int fd, uint64_t timeout, getdns_eventloop_event *el_ev)
{
	getdns_libev *ext = (getdns_libev *)loop;
	io_timer     *my_ev;
	ev_io        *my_io;
	ev_timer     *my_timer;
	ev_tstamp     to = ((ev_tstamp)timeout) / 1000;

	if (fd < 0) el_ev->read_cb = NULL;
	if (timeout == TIMEOUT_FOREVER) el_ev->timeout_cb = NULL;

	if (!el_ev->read_cb && !el_ev->timeout_cb)
		return GETDNS_RETURN_GOOD; /* Nothing to schedule */

	if (!(my_ev = GETDNS_MALLOC(ext->mf, io_timer)))
		return GETDNS_RETURN_MEMORY_ERROR;

	el_ev->ev = my_ev;
	
	if (el_ev->read_cb) {
		my_io = &my_ev->io;
		my_io->data = el_ev;
		ev_io_init(my_io, getdns_libev_read_cb, fd, EV_READ);
		ev_io_start(ext->loop, &my_ev->io);
	}
	if (el_ev->timeout_cb) {
		my_timer = &my_ev->timer;
		my_timer->data = el_ev;
		ev_timer_init(my_timer, getdns_libev_timeout_cb, to, 0);
		ev_timer_start(ext->loop, &my_ev->timer);
	}
	return GETDNS_RETURN_GOOD;
}

static getdns_return_t
getdns_libev_schedule_timeout(getdns_eventloop *loop,
    uint64_t timeout, getdns_eventloop_event *el_ev)
{
	return getdns_libev_schedule_read(loop, -1, timeout, el_ev);
}

static getdns_return_t
getdns_libev_clear_event(getdns_eventloop *loop,
    getdns_eventloop_event *el_ev)
{
	getdns_libev *ext = (getdns_libev *)loop;
	io_timer *my_ev = (io_timer *)el_ev->ev;
	
	assert(my_ev);

	if (el_ev->read_cb)
		ev_io_stop(ext->loop, &my_ev->io);
	if (el_ev->timeout_cb)
		ev_timer_stop(ext->loop, &my_ev->timer);

	GETDNS_FREE(ext->mf, el_ev->ev);
	el_ev->ev = NULL;
	return GETDNS_RETURN_GOOD;
}


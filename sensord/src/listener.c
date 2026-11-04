/* SPDX-License-Identifier: MIT */
/*
 * The FastRPC reverse tunnel: attach to the sensor process on the DSP, register
 * as its listener, then loop fetching requests and returning results.
 * Remote interfaces used: remotectl (handle 0), adsp_listener (handle 3).
 */
#include <errno.h>
#include <fcntl.h>
#include <misc/fastrpc.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "fsrv.h"
#include "listener.h"

#define HANDLE_REMOTECTL 0
#define HANDLE_LISTENER 3
#define INLINE_INBUFS 256
#define MAX_STREAM (16u * 1024 * 1024)

static volatile sig_atomic_t stop_flag;

static void on_signal(int sig)
{
	(void)sig;
	stop_flag = 1;
}

static void arg_set(struct fastrpc_invoke_args *a, void *p, size_t len)
{
	memset(a, 0, sizeof(*a));
	a->ptr = (uintptr_t)p;
	a->length = len;
	a->fd = -1;
}

static int invoke(int fd, uint32_t handle, uint32_t sc, struct fastrpc_invoke_args *args)
{
	struct fastrpc_invoke inv = { .handle = handle, .sc = sc, .args = (uintptr_t)args };

	return ioctl(fd, FASTRPC_IOCTL_INVOKE, &inv);
}

static int remote_open(int fd, const char *name, uint32_t *handle)
{
	uint32_t in[2] = { (uint32_t)strlen(name) + 1, 256 }, out[2] = { 0, 0 };
	char err[256] = "";
	struct fastrpc_invoke_args a[4];
	int ret;

	arg_set(&a[0], in, sizeof(in));
	arg_set(&a[1], (void *)name, in[0]);
	arg_set(&a[2], out, sizeof(out));
	arg_set(&a[3], err, sizeof(err));
	ret = invoke(fd, HANDLE_REMOTECTL, FR_SC(0, 2, 2), a);
	if (ret || out[1]) {
		err[sizeof(err) - 1] = 0;
		fprintf(stderr, "remotectl open %s: ret %d (%s), dlerr %u: %s\n", name, ret,
			ret ? strerror(errno) : "ok", out[1], err);
		return -1;
	}
	*handle = out[0];
	return 0;
}

static void remote_close(int fd, uint32_t handle)
{
	uint32_t in[2] = { handle, 0 }, out = 0;
	struct fastrpc_invoke_args a[3];

	arg_set(&a[0], in, sizeof(in));
	arg_set(&a[1], &out, sizeof(out));
	arg_set(&a[2], NULL, 0);
	invoke(fd, HANDLE_REMOTECTL, FR_SC(1, 1, 2), a);
}

static int register_listener(int fd)
{
	uint32_t h;
	int ret;

	if (remote_open(fd, "adsp_default_listener", &h))
		return -1;
	ret = invoke(fd, h, FR_SC(0, 0, 0), NULL);
	if (ret)
		fprintf(stderr, "default listener register: %s\n", strerror(errno));
	remote_close(fd, h);
	return ret;
}

/* next2: returns the previous result, fetches the next request. */
static int listener_next(int fd, uint32_t *rctx, uint32_t result, void *outs, uint32_t out_len,
			 uint32_t *handle, uint32_t *sc, void *ins, uint32_t in_cap, uint32_t *in_req)
{
	uint32_t pin[4] = { *rctx, result, out_len, in_cap }, pout[4] = { 0, 0, 0, 0 };
	struct fastrpc_invoke_args a[4];
	int ret;

	arg_set(&a[0], pin, sizeof(pin));
	arg_set(&a[1], outs, out_len);
	arg_set(&a[2], pout, sizeof(pout));
	arg_set(&a[3], ins, in_cap);
	ret = invoke(fd, HANDLE_LISTENER, FR_SC(4, 2, 2), a);
	if (ret)
		return ret;
	*rctx = pout[0];
	*handle = pout[1];
	*sc = pout[2];
	*in_req = pout[3];
	return 0;
}

/* get_in_bufs2: the part of the request stream that did not fit inline. */
static int listener_rest(int fd, uint32_t rctx, uint32_t off, void *buf, uint32_t len)
{
	uint32_t pin[3] = { rctx, off, len }, req = 0;
	struct fastrpc_invoke_args a[3];

	arg_set(&a[0], pin, sizeof(pin));
	arg_set(&a[1], &req, sizeof(req));
	arg_set(&a[2], buf, len);
	return invoke(fd, HANDLE_LISTENER, FR_SC(5, 1, 2), a);
}

static void free_outs(struct fr_req *r)
{
	unsigned i;

	for (i = 0; i < r->n_out; i++) {
		free(r->out[i].p);
		r->out[i].p = NULL;
	}
}

static uint32_t handle_request(struct fsrv *s, struct fr_req *r, const void *stream, size_t len,
			       uint32_t handle, uint32_t sc)
{
	uint64_t total = 0;
	unsigned i;

	if (fr_decode(stream, len, handle, sc, r)) {
		printf("[rpc] MALFORMED request: handle %u sc 0x%08x, %zu bytes\n", handle, sc, len);
		memset(r, 0, sizeof(*r));
		r->n_out = FR_SC_OUT(sc) <= FR_MAX_BUFS ? FR_SC_OUT(sc) : 0;
		return 14;
	}
	for (i = 0; i < r->n_out; i++)
		total += r->out[i].len;
	if (total > MAX_STREAM) {
		printf("[rpc] output too large: %llu bytes\n", (unsigned long long)total);
		for (i = 0; i < r->n_out; i++)
			r->out[i].len = 0;
		return 2;
	}
	for (i = 0; i < r->n_out; i++) {
		r->out[i].p = calloc(1, r->out[i].len ? r->out[i].len : 1);
		if (!r->out[i].p) {
			free_outs(r);
			for (i = 0; i < r->n_out; i++)
				r->out[i].len = 0;
			return 2;
		}
	}
	return fsrv_dispatch(s, r);
}

int listener_run(const struct listener_opts *o)
{
	struct sigaction sa = { .sa_handler = on_signal };
	struct fsrv s;
	struct fr_req req;
	uint8_t inl[INLINE_INBUFS] __attribute__((aligned(8)));
	uint8_t *big = NULL, *outs = NULL;
	uint32_t rctx = 0, result = 0xffffffff, handle = 0, sc = 0, in_req = 0, out_len = 0;
	int fd, ret = 1;
	unsigned long requests = 0;

	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	setvbuf(stdout, NULL, _IOLBF, 0);

	fd = open(o->device, O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		fprintf(stderr, "open %s: %s\n", o->device, strerror(errno));
		return 1;
	}
	ret = fsrv_init(&s, o->dir, fd, o->writable, o->verbose);
	if (ret) {
		fprintf(stderr, "open %s: %s\n", o->dir, strerror(-ret));
		close(fd);
		return 1;
	}
	for (int i = 0; i < o->n_props; i++)
		fsrv_add_prop(&s, o->props[i].k, o->props[i].v);
	fsrv_load_props(&s, o->dir);

	if (ioctl(fd, o->root_pd ? FASTRPC_IOCTL_INIT_ATTACH : FASTRPC_IOCTL_INIT_ATTACH_SNS, NULL)) {
		fprintf(stderr, "attach to %s: %s\n", o->root_pd ? "root PD" : "sensor PD", strerror(errno));
		goto out;
	}
	printf("attached to %s on %s; serving %s %s\n", o->root_pd ? "root PD" : "sensor PD", o->device,
	       o->dir, o->writable ? "(registry writable)" : "(read-only)");
	if (register_listener(fd))
		goto out;
	if (invoke(fd, HANDLE_LISTENER, FR_SC(3, 0, 0), NULL)) {
		fprintf(stderr, "listener init: %s\n", strerror(errno));
		goto out;
	}

	memset(&req, 0, sizeof(req));
	ret = 0;
	while (!stop_flag) {
		const void *stream = inl;
		size_t size = fr_encoded_size(req.out, req.n_out);

		free(big);
		big = NULL;
		free(outs);
		outs = size ? malloc(size) : NULL;
		out_len = 0;
		if (size) {
			if (!outs) {
				ret = 1;
				break;
			}
			out_len = (uint32_t)fr_encode(req.out, req.n_out, outs);
		}
		free_outs(&req);

		if (listener_next(fd, &rctx, result, outs, out_len, &handle, &sc, inl, sizeof(inl),
				  &in_req)) {
			if (!stop_flag) {
				fprintf(stderr, "listener next: %s (after %lu requests)\n",
					strerror(errno), requests);
				ret = 1;
			}
			break;
		}
		requests++;

		if (in_req > INLINE_INBUFS) {
			if (in_req > MAX_STREAM || !(big = malloc(in_req))) {
				printf("[rpc] request of %u bytes refused\n", in_req);
				req = (struct fr_req){ .n_out = 0 };
				result = 2;
				continue;
			}
			memcpy(big, inl, INLINE_INBUFS);
			if (listener_rest(fd, rctx, INLINE_INBUFS, big + INLINE_INBUFS,
					  in_req - INLINE_INBUFS)) {
				fprintf(stderr, "listener get_in_bufs: %s\n", strerror(errno));
				ret = 1;
				break;
			}
			stream = big;
		}

		result = handle_request(&s, &req, stream, in_req, handle, sc);
	}
	free_outs(&req);
	free(big);
	free(outs);
out:
	fsrv_fini(&s);
	close(fd);
	return ret;
}

/* SPDX-License-Identifier: MIT */
/* Host side of the calls the DSP makes through the FastRPC reverse tunnel. */
#ifndef FSRV_H
#define FSRV_H
#include <dirent.h>
#include <stdbool.h>

#include "wire.h"

#define FSRV_MAX_OPEN 64
#define FSRV_MAX_PROPS 16

enum {
	FSRV_H_REMOTECTL = 0,
	FSRV_H_APPS_STD,
	FSRV_H_APPS_MEM,
	FSRV_H_SNS_REGISTRY,
};

struct fsrv_open {
	bool used, is_dir, eof;
	int fd;
	DIR *dir;
};

struct fsrv {
	int rootfd;
	int rpc_fd;		/* device, for apps_mem; -1 when absent */
	bool writable;		/* writes allowed under the sensors persist tree */
	bool verbose;		/* log every call, not only failures */
	unsigned n_props;
	struct { const char *k, *v; } props[FSRV_MAX_PROPS];
	struct fsrv_open open[FSRV_MAX_OPEN];
};

int fsrv_init(struct fsrv *s, const char *dir, int rpc_fd, bool writable, bool verbose);
void fsrv_fini(struct fsrv *s);
int fsrv_add_prop(struct fsrv *s, const char *k, const char *v);
/* Read "key=value" lines from DIR/props, if present. */
void fsrv_load_props(struct fsrv *s, const char *dir);

/* Handle one call; req->out[].p must be allocated and zeroed. Returns the AEE result. */
uint32_t fsrv_dispatch(struct fsrv *s, struct fr_req *req);

/* Map a DSP path to one relative to the served root. Exposed for tests. */
int fsrv_normalize(const char *in, char *out, size_t cap);

#endif

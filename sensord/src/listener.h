/* SPDX-License-Identifier: MIT */
#ifndef LISTENER_H
#define LISTENER_H
#include <stdbool.h>

struct listener_prop {
	const char *k, *v;
};

struct listener_opts {
	const char *device;
	const char *dir;
	bool writable, verbose, root_pd;
	const struct listener_prop *props;
	int n_props;
};

/* Runs until signalled or the session ends; returns the exit status. */
int listener_run(const struct listener_opts *o);

#endif

/* SPDX-License-Identifier: MIT */
/* SSC client over a QRTR socket. */
#ifndef CLIENT_H
#define CLIENT_H
#include "ssc.h"

struct client {
	int fd;
	uint32_t node, port;
	uint16_t txn;
	unsigned delivery;
	int timeout_ms;
};

/* Open a socket and wait up to wait_s seconds for the SSC service. */
int client_open(struct client *c, int wait_s);
void client_close(struct client *c);

/* Send a request and wait for its QMI response. */
int client_request(struct client *c, struct ssc_uid uid, uint32_t msg, const void *body,
		   size_t len, bool has_body);

/*
 * Wait for reports, invoking cb for each event. Returns when *done becomes
 * nonzero (0), on timeout (-ETIMEDOUT), when the service goes away (-ENOTCONN)
 * or on failure.
 */
int client_pump(struct client *c, ssc_event_cb cb, void *ctx, const volatile int *done);

/* Look up the sensors of a data type. Returns count or negative errno. */
int client_suid(struct client *c, const char *type, bool only_default, struct ssc_uid *uids, int max);

#endif

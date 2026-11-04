/* SPDX-License-Identifier: MIT */
#include <errno.h>
#include <linux/qrtr.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "client.h"

#ifndef AF_QIPCRTR
#define AF_QIPCRTR 42
#endif

#define RX_CAP 65536

static long now_ms(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1000L + t.tv_nsec / 1000000L;
}

/* Ask the name service for the SSC. Control packets arrive on this socket. */
static int send_lookup(int fd, uint32_t node)
{
	struct sockaddr_qrtr to = { .sq_family = AF_QIPCRTR, .sq_node = node, .sq_port = QRTR_PORT_CTRL };
	struct qrtr_ctrl_pkt pkt;

	memset(&pkt, 0, sizeof(pkt));
	pkt.cmd = QRTR_TYPE_NEW_LOOKUP;
	pkt.server.service = SSC_SERVICE;
	return sendto(fd, &pkt, sizeof(pkt), 0, (struct sockaddr *)&to, sizeof(to)) < 0 ? -errno : 0;
}

int client_open(struct client *c, int wait_s)
{
	struct sockaddr_qrtr me;
	socklen_t mel = sizeof(me);
	long deadline = now_ms() + wait_s * 1000L;
	int ret;

	memset(c, 0, sizeof(*c));
	c->txn = 1;
	c->timeout_ms = 3000;
	c->fd = socket(AF_QIPCRTR, SOCK_DGRAM | SOCK_CLOEXEC, 0);
	if (c->fd < 0) {
		ret = -errno;
		fprintf(stderr, "qrtr socket: %s (is the qrtr module loaded?)\n", strerror(-ret));
		return ret;
	}
	if (getsockname(c->fd, (struct sockaddr *)&me, &mel)) {
		ret = -errno;
		goto fail;
	}
	ret = send_lookup(c->fd, me.sq_node);
	if (ret)
		goto fail;

	for (;;) {
		uint8_t buf[256];
		struct sockaddr_qrtr from;
		socklen_t fl = sizeof(from);
		struct pollfd p = { .fd = c->fd, .events = POLLIN };
		long left = deadline - now_ms();
		struct qrtr_ctrl_pkt pkt;
		ssize_t n;

		if (left <= 0) {
			ret = -ETIMEDOUT;
			goto fail;
		}
		if (poll(&p, 1, (int)left) < 0) {
			ret = -errno;
			goto fail;
		}
		if (!p.revents)
			continue;
		n = recvfrom(c->fd, buf, sizeof(buf), 0, (struct sockaddr *)&from, &fl);
		if (n < (ssize_t)sizeof(pkt) || from.sq_port != QRTR_PORT_CTRL)
			continue;
		memcpy(&pkt, buf, sizeof(pkt));
		/* An all-zero server only marks the end of the current listing. */
		if (pkt.cmd == QRTR_TYPE_NEW_SERVER && pkt.server.service == SSC_SERVICE) {
			c->node = pkt.server.node;
			c->port = pkt.server.port;
			return 0;
		}
	}
fail:
	close(c->fd);
	c->fd = -1;
	return ret;
}

void client_close(struct client *c)
{
	if (c->fd >= 0)
		close(c->fd);
	c->fd = -1;
}

/* Receive one datagram; 1 if QMI data was stored, 0 for timeout, negative on error. */
static int recv_qmi(struct client *c, uint8_t *buf, size_t cap, size_t *len, int timeout)
{
	struct pollfd p = { .fd = c->fd, .events = POLLIN };
	long deadline = now_ms() + timeout;

	for (;;) {
		struct sockaddr_qrtr from;
		socklen_t fl = sizeof(from);
		long left = deadline - now_ms();
		ssize_t n;
		int r;

		if (left <= 0)
			return 0;
		r = poll(&p, 1, (int)left);
		if (r < 0)
			return errno == EINTR ? 0 : -errno;
		if (!r)
			return 0;
		n = recvfrom(c->fd, buf, cap, 0, (struct sockaddr *)&from, &fl);
		if (n < 0)
			return -errno;
		if (from.sq_port == QRTR_PORT_CTRL) {
			struct qrtr_ctrl_pkt pkt;

			if ((size_t)n < sizeof(pkt))
				continue;
			memcpy(&pkt, buf, sizeof(pkt));
			if ((pkt.cmd == QRTR_TYPE_DEL_SERVER && pkt.server.service == SSC_SERVICE) ||
			    (pkt.cmd == QRTR_TYPE_BYE && pkt.client.node == c->node))
				return -ENOTCONN;
			continue;
		}
		if (from.sq_node != c->node || from.sq_port != c->port)
			continue;
		*len = (size_t)n;
		return 1;
	}
}

static void deliver(const struct qmi_msg *m, ssc_event_cb cb, void *ctx)
{
	size_t n;
	const uint8_t *pb;

	if (m->type != QMI_INDICATION ||
	    (m->id != QMI_SSC_REPORT_SMALL && m->id != QMI_SSC_REPORT_LARGE))
		return;
	pb = ssc_report_payload(m, &n);
	if (pb && ssc_parse_report(pb, n, cb, ctx) < 0)
		fprintf(stderr, "malformed report ignored (%zu bytes)\n", n);
}

int client_request(struct client *c, struct ssc_uid uid, uint32_t msg, const void *body,
		   size_t len, bool has_body)
{
	static uint8_t rx[RX_CAP];
	uint8_t tx[1400];
	struct sockaddr_qrtr to = { .sq_family = AF_QIPCRTR, .sq_node = c->node, .sq_port = c->port };
	uint16_t txn = c->txn++;
	size_t n = ssc_build_control(tx, sizeof(tx), txn, uid, msg, c->delivery, body, len, has_body);
	long deadline = now_ms() + c->timeout_ms;

	if (!n)
		return -EMSGSIZE;
	if (sendto(c->fd, tx, n, 0, (struct sockaddr *)&to, sizeof(to)) < 0)
		return -errno;

	for (;;) {
		size_t rn = 0;
		struct qmi_msg m;
		long left = deadline - now_ms();
		int r;

		if (left <= 0)
			return -ETIMEDOUT;
		r = recv_qmi(c, rx, sizeof(rx), &rn, (int)left);
		if (r <= 0) {
			if (r == 0)
				return -ETIMEDOUT;
			return r;
		}
		if (qmi_parse(rx, rn, &m))
			continue;
		if (m.type == QMI_RESPONSE && m.txn == txn && m.id == QMI_SSC_CONTROL) {
			int res = qmi_result(&m);

			if (res) {
				fprintf(stderr, "ssc request %u rejected: qmi error %d\n", msg, res);
				return -EREMOTEIO;
			}
			return 0;
		}
	}
}

int client_pump(struct client *c, ssc_event_cb cb, void *ctx, const volatile int *done)
{
	static uint8_t rx[RX_CAP];
	long deadline = now_ms() + c->timeout_ms;

	while (!*done) {
		size_t rn = 0;
		struct qmi_msg m;
		long left = deadline - now_ms();
		int r;

		if (left <= 0)
			return -ETIMEDOUT;
		r = recv_qmi(c, rx, sizeof(rx), &rn, left > 200 ? 200 : (int)left);
		if (r < 0)
			return r;
		if (!r)
			continue;
		if (qmi_parse(rx, rn, &m))
			continue;
		if (m.type == QMI_INDICATION)
			deadline = now_ms() + c->timeout_ms;
		deliver(&m, cb, ctx);
	}
	return 0;
}

struct suid_wait {
	const char *type;
	struct ssc_uid *uids;
	int max, n;
	int done;
};

static void on_suid(void *ctx, struct ssc_uid uid, uint32_t id, uint64_t ts, const uint8_t *b, size_t l)
{
	struct suid_wait *w = ctx;
	char type[64];
	int n;

	(void)ts;
	if (uid.low != SSC_SUID_UID || uid.high != SSC_SUID_UID || id != SSC_MSG_SUID_RESP)
		return;
	n = ssc_parse_suid(b, l, type, sizeof(type), w->uids, w->max);
	if (n < 0 || strcmp(type, w->type))
		return;
	w->n = n;
	w->done = 1;
}

int client_suid(struct client *c, const char *type, bool only_default, struct ssc_uid *uids, int max)
{
	struct ssc_uid suid = { SSC_SUID_UID, SSC_SUID_UID };
	struct suid_wait w = { .type = type, .uids = uids, .max = max };
	uint8_t body[128];
	size_t n = ssc_body_suid(body, sizeof(body), type, only_default);
	int ret;

	if (!n)
		return -EINVAL;
	ret = client_request(c, suid, SSC_MSG_SUID_REQ, body, n, true);
	if (ret)
		return ret;
	ret = client_pump(c, on_suid, &w, &w.done);
	return ret ? ret : w.n;
}

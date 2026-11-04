/* SPDX-License-Identifier: MIT */
/*
 * Runs the real command-line program against simulated kernel interfaces, linked
 * with --wrap: a fake DSP behind FASTRPC_IOCTL_INVOKE and a fake SSC behind the
 * QRTR socket calls. It checks our reading of the protocol against itself, so a
 * misread protocol would still pass: only the device can settle that.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <linux/qrtr.h>
#include <misc/fastrpc.h>
#include <poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "pb.h"
#include "ssc.h"
#include "wire.h"

int sensord_main(int argc, char **argv);

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

/* ---------- fake SSC over QRTR ---------- */

#define FAKE_FD 777
#define SSC_NODE 5
#define SSC_PORT 9

static struct pkt { uint8_t d[2048]; size_t n; uint32_t port; } queue[256];
static unsigned q_head, q_tail;
static const struct ssc_uid ACCEL = { 0x1111, 0x2222 };

static void push(uint32_t port, const void *d, size_t n)
{
	struct pkt *p = &queue[q_tail++ % 256];

	memcpy(p->d, d, n);
	p->n = n;
	p->port = port;
}

static void push_ctrl(uint32_t cmd, uint32_t svc, uint32_t node, uint32_t port)
{
	struct qrtr_ctrl_pkt c;

	memset(&c, 0, sizeof(c));
	c.cmd = cmd;
	c.server.service = svc;
	c.server.node = node;
	c.server.port = port;
	push(QRTR_PORT_CTRL, &c, sizeof(c));
}

static void push_response(uint16_t txn)
{
	uint8_t m[] = { 2, 0, 0, 0x20, 0, 7, 0, 2, 4, 0, 0, 0, 0, 0 };

	memcpy(m + 1, &txn, 2);
	push(SSC_PORT, m, sizeof(m));
}

/* Report indication with one event. */
static void push_event(struct ssc_uid uid, uint32_t id, const void *body, size_t n)
{
	uint8_t su[32], ev[1024], pb[1200], q[1400];
	struct pb_w w;
	size_t su_n, ev_n, pb_n;
	uint16_t l, tl;

	pb_w_init(&w, su, sizeof(su));
	pb_fixed64(&w, 1, uid.low);
	pb_fixed64(&w, 2, uid.high);
	su_n = w.len;
	pb_w_init(&w, ev, sizeof(ev));
	pb_fixed32(&w, 1, id);
	pb_fixed64(&w, 2, 19200000ULL * 2);
	pb_bytes(&w, 3, body, n);
	ev_n = w.len;
	pb_w_init(&w, pb, sizeof(pb));
	pb_bytes(&w, 1, su, su_n);
	pb_bytes(&w, 2, ev, ev_n);
	pb_n = w.len;

	q[0] = QMI_INDICATION;
	q[1] = q[2] = 0;
	l = QMI_SSC_REPORT_LARGE;
	memcpy(q + 3, &l, 2);
	tl = (uint16_t)(3 + 8 + 3 + 2 + pb_n);
	memcpy(q + 5, &tl, 2);
	q[7] = 1;
	l = 8;
	memcpy(q + 8, &l, 2);
	memset(q + 10, 0, 8);
	q[18] = 2;
	l = (uint16_t)(pb_n + 2);
	memcpy(q + 19, &l, 2);
	l = (uint16_t)pb_n;
	memcpy(q + 21, &l, 2);
	memcpy(q + 23, pb, pb_n);
	push(SSC_PORT, q, 23 + pb_n);
}

static size_t attr_item(uint8_t *out, size_t cap, int id, const uint8_t *val, size_t vl)
{
	uint8_t arr[64];
	struct pb_w w;
	size_t n;

	pb_w_init(&w, arr, sizeof(arr));
	pb_bytes(&w, 1, val, vl);
	n = w.len;
	pb_w_init(&w, out, cap);
	pb_varint(&w, 1, (uint64_t)id);
	pb_bytes(&w, 2, arr, n);
	return w.len;
}

static void ssc_handle(const uint8_t *q, size_t n)
{
	struct qmi_msg m;
	const uint8_t *v;
	size_t vl;
	uint16_t pl;
	struct pb_r r;
	struct pb_field f;
	struct ssc_uid uid = { 0, 0 };
	uint32_t msg = 0;
	const uint8_t *body = NULL;
	size_t body_n = 0;

	CHECK(!qmi_parse(q, n, &m));
	CHECK(m.type == QMI_REQUEST && m.id == QMI_SSC_CONTROL);
	v = qmi_tlv(&m, 1, &vl);
	CHECK(v != NULL);
	memcpy(&pl, v, 2);
	CHECK(pl == vl - 2);
	pb_r_init(&r, v + 2, pl);
	while (pb_next(&r, &f)) {
		if (f.field == 1) {
			struct pb_r u;
			struct pb_field g;

			pb_r_init(&u, f.data, f.len);
			while (pb_next(&u, &g)) {
				if (g.field == 1)
					uid.low = g.num;
				else
					uid.high = g.num;
			}
		} else if (f.field == 2) {
			msg = (uint32_t)f.num;
		} else if (f.field == 4) {
			struct pb_r b;
			struct pb_field g;

			pb_r_init(&b, f.data, f.len);
			while (pb_next(&b, &g)) {
				if (g.field == 2) {
					body = g.data;
					body_n = g.len;
				}
			}
		}
	}
	push_response(m.txn);

	if (msg == SSC_MSG_SUID_REQ) {
		char type[32] = "";
		uint8_t su[32], out[128];
		struct pb_w w;
		size_t su_n;
		struct ssc_uid suid = { SSC_SUID_UID, SSC_SUID_UID };

		CHECK(uid.low == SSC_SUID_UID);
		ssc_parse_suid(body, body_n, type, sizeof(type), NULL, 0);
		pb_w_init(&w, su, sizeof(su));
		pb_fixed64(&w, 1, ACCEL.low);
		pb_fixed64(&w, 2, ACCEL.high);
		su_n = w.len;
		pb_w_init(&w, out, sizeof(out));
		pb_bytes(&w, 1, type, strlen(type));
		if (!strcmp(type, "accel"))
			pb_bytes(&w, 2, su, su_n);
		push_event(suid, SSC_MSG_SUID_RESP, out, w.len);
	} else if (msg == SSC_MSG_GET_ATTRIBUTES) {
		uint8_t out[256], a[64], val[32];
		struct pb_w o, w;
		size_t an;

		pb_w_init(&o, out, sizeof(out));
		pb_w_init(&w, val, sizeof(val));
		pb_bytes(&w, 2, "lsm6dso", 8);
		an = attr_item(a, sizeof(a), SSC_ATTR_NAME, val, w.len);
		pb_bytes(&o, 1, a, an);
		pb_w_init(&w, val, sizeof(val));
		pb_float(&w, 3, 104.0f);
		an = attr_item(a, sizeof(a), SSC_ATTR_RATES, val, w.len);
		pb_bytes(&o, 1, a, an);
		push_event(uid, SSC_MSG_ATTRIBUTES, out, o.len);
	} else if (msg == SSC_MSG_ENABLE_CONTINUOUS) {
		uint8_t s[64];
		struct pb_w w;
		int i;

		CHECK(uid.low == ACCEL.low && body_n == 5);
		push_event(uid, SSC_MSG_SUID_RESP, s, 0);
		for (i = 0; i < 3; i++) {
			pb_w_init(&w, s, sizeof(s));
			pb_float(&w, 1, 0.5f);
			pb_float(&w, 1, -0.25f);
			pb_float(&w, 1, 9.75f);
			pb_varint(&w, 2, 3);
			push_event(uid, SSC_MSG_REPORT, s, w.len);
		}
	}
}

int __wrap_socket(int d, int t, int p)
{
	(void)d; (void)t; (void)p;
	return FAKE_FD;
}

int __wrap_getsockname(int fd, struct sockaddr *a, socklen_t *l)
{
	struct sockaddr_qrtr s = { .sq_family = 42, .sq_node = 1, .sq_port = 1000 };

	(void)fd;
	memcpy(a, &s, sizeof(s));
	*l = sizeof(s);
	return 0;
}

ssize_t __wrap_sendto(int fd, const void *b, size_t n, int fl, const struct sockaddr *a, socklen_t al)
{
	struct sockaddr_qrtr to;

	(void)fd; (void)fl; (void)al;
	memcpy(&to, a, sizeof(to));
	if (to.sq_port == QRTR_PORT_CTRL) {
		push_ctrl(QRTR_TYPE_NEW_SERVER, SSC_SERVICE, SSC_NODE, SSC_PORT);
		push_ctrl(QRTR_TYPE_NEW_SERVER, 0, 0, 0);
	} else {
		ssc_handle(b, n);
	}
	return (ssize_t)n;
}

ssize_t __wrap_recvfrom(int fd, void *b, size_t n, int fl, struct sockaddr *a, socklen_t *al)
{
	struct sockaddr_qrtr src = { .sq_family = 42, .sq_node = SSC_NODE };
	struct pkt *p;

	(void)fd; (void)fl;
	if (q_head == q_tail) {
		errno = EAGAIN;
		return -1;
	}
	p = &queue[q_head++ % 256];
	src.sq_port = p->port;
	memcpy(b, p->d, p->n < n ? p->n : n);
	if (a)
		memcpy(a, &src, sizeof(src));
	if (al)
		*al = sizeof(src);
	return (ssize_t)p->n;
}

int __wrap_poll(struct pollfd *f, nfds_t n, int t)
{
	(void)n;
	if (q_head != q_tail) {
		f->revents = POLLIN;
		return 1;
	}
	usleep(t > 5 ? 5000 : 1000);
	f->revents = 0;
	return 0;
}

/* ---------- fake DSP behind FastRPC ---------- */

struct step {
	uint32_t handle, sc;
	uint8_t stream[2048];
	size_t len;
	/* Result the listener must send back for this step. */
	uint32_t result;
	uint32_t out_lens[4];
	unsigned n_out;
	uint8_t reply[1500];
	size_t reply_len;
};

static struct step script[16];
static unsigned n_steps, step_at;
static int attached, registered, inited, mmaps;
static uint32_t file_handle;
static size_t rest_calls;

static void add_step(uint32_t handle, uint32_t method, const struct fr_buf *in, unsigned n_in,
		     const uint32_t *outl, unsigned n_out)
{
	struct step *s = &script[n_steps++];
	size_t pos = 0;
	unsigned i;

	s->handle = handle;
	s->sc = FR_SC(method, n_in, n_out);
	s->n_out = n_out;
	for (i = 0; i < n_in; i++) {
		memcpy(s->stream + pos, &in[i].len, 4);
		pos += 4;
		if (in[i].len) {
			pos = (pos + 7) & ~(size_t)7;
			memcpy(s->stream + pos, in[i].p, in[i].len);
			pos += in[i].len;
		}
	}
	for (i = 0; i < n_out; i++) {
		memcpy(s->stream + pos, &outl[i], 4);
		pos += 4;
		s->out_lens[i] = outl[i];
	}
	s->len = pos;
}

int __real_ioctl(int fd, unsigned long req, ...);

static int dsp_invoke(struct fastrpc_invoke *inv)
{
	struct fastrpc_invoke_args *a = (void *)(uintptr_t)inv->args;
	unsigned method = FR_SC_METHOD(inv->sc);

	if (inv->handle == 0) {
		if (method == 0) { /* remotectl open */
			uint32_t *out = (void *)(uintptr_t)a[2].ptr;

			CHECK(!strcmp((char *)(uintptr_t)a[1].ptr, "adsp_default_listener"));
			out[0] = 99;
			out[1] = 0;
		}
		return 0;
	}
	if (inv->handle == 99) {
		registered = 1;
		return 0;
	}
	CHECK(inv->handle == 3);
	if (method == 3) {
		inited = 1;
		return 0;
	}
	if (method == 5) { /* get_in_bufs2 */
		uint32_t *in = (void *)(uintptr_t)a[0].ptr;
		struct step *s = &script[step_at - 1];

		CHECK(in[1] == 256 && a[2].length == s->len - 256);
		memcpy((void *)(uintptr_t)a[2].ptr, s->stream + 256, s->len - 256);
		rest_calls++;
		return 0;
	}
	CHECK(method == 4);
	{
		uint32_t *in = (void *)(uintptr_t)a[0].ptr, *out = (void *)(uintptr_t)a[2].ptr;
		struct step *s;

		if (step_at) {
			struct step *prev = &script[step_at - 1];

			CHECK(in[0] == 1000 + step_at - 1);
			prev->result = in[1];
			CHECK(a[1].length == in[2] && in[2] < sizeof(prev->reply));
			if (a[1].length)
				memcpy(prev->reply, (void *)(uintptr_t)a[1].ptr, a[1].length);
			prev->reply_len = a[1].length;
		}
		/* The file handle from fopen feeds the later steps. */
		if (step_at == 2) {
			memcpy(&file_handle, script[1].reply + 8, 4);
			memcpy(script[2].stream + 8, &file_handle, 4);
			memcpy(script[3].stream + 8, &file_handle, 4);
		}
		if (step_at == n_steps) {
			errno = EIO;
			return -1;
		}
		s = &script[step_at];
		if (step_at == 5) {
			uint32_t h;

			memcpy(&h, script[4].reply + 8, 4);
			memcpy(s->stream + 8, &h, 4);
		}
		out[0] = 1000 + step_at;
		out[1] = s->handle;
		out[2] = s->sc;
		out[3] = (uint32_t)s->len;
		memcpy((void *)(uintptr_t)a[3].ptr, s->stream, s->len < a[3].length ? s->len : a[3].length);
		step_at++;
		return 0;
	}
}

int __wrap_ioctl(int fd, unsigned long req, ...)
{
	va_list ap;
	void *arg;

	va_start(ap, req);
	arg = va_arg(ap, void *);
	va_end(ap);
	switch (req) {
	case FASTRPC_IOCTL_INIT_ATTACH_SNS:
		attached = 1;
		return 0;
	case FASTRPC_IOCTL_INVOKE:
		return dsp_invoke(arg);
	case FASTRPC_IOCTL_MMAP: {
		struct fastrpc_req_mmap *m = arg;

		CHECK(m->fd == -1 && m->size == 4096 && (m->flags & 0x1000));
		m->vaddrout = 0xc0000000;
		mmaps++;
		return 0;
	}
	default:
		return __real_ioctl(fd, req, arg);
	}
}

/* ---------- scenarios ---------- */

static void run_capture(const char *file, int argc, char **argv, int *rc)
{
	int saved = dup(1);

	fflush(stdout);
	CHECK(freopen(file, "w", stdout) != NULL);
	optind = 1;
	*rc = sensord_main(argc, argv);
	fflush(stdout);
	dup2(saved, 1);
	close(saved);
	stdout = fdopen(1, "w");
}

static char *slurp(const char *file)
{
	static char buf[65536];
	FILE *f = fopen(file, "r");
	size_t n = 0;

	if (f) {
		n = fread(buf, 1, sizeof(buf) - 1, f);
		fclose(f);
	}
	buf[n] = 0;
	return buf;
}

static void scenario_ssc(const char *log)
{
	char *a1[] = { "x", "--list", "--wait", "1", NULL };
	char *a2[] = { "x", "--stream", "accel", "--count", "2", "--rate", "50", NULL };
	char *a3[] = { "x", "--stream", "gyro", "--count", "1", NULL };
	int rc;
	char *o;

	run_capture(log, 4, a1, &rc);
	o = slurp(log);
	CHECK(rc == 0);
	CHECK(strstr(o, "SSC service at node 5 port 9"));
	CHECK(strstr(o, "accel[0]: uid 00000000000022220000000000001111"));
	CHECK(strstr(o, "name: lsm6dso"));
	CHECK(strstr(o, "rate: 104"));
	CHECK(strstr(o, "gyro: none"));
	CHECK(strstr(o, "1 sensors found"));

	run_capture(log, 7, a2, &rc);
	o = slurp(log);
	CHECK(rc == 0);
	CHECK(strstr(o, "# enabled"));
	CHECK(strstr(o, "accel t=2.000000 0.5000 -0.2500 9.7500 accuracy=3"));

	run_capture(log, 5, a3, &rc);
	CHECK(rc == 1); /* no such sensor is an error, not a hang */
}

static void scenario_fastrpc(const char *dir, const char *log)
{
	char *argv[] = { "x", "--serve", (char *)dir, "--device", "/dev/null", "--verbose", NULL };
	uint32_t p[8], ol[3];
	static uint8_t big[1000];
	static const char wname[] = "/persist/sensors/big", rname[] = "/persist/sensors/big";
	int rc;
	unsigned i;
	uint32_t v;
	char path[256], *o;
	struct stat st;

	for (i = 0; i < sizeof(big); i++)
		big[i] = (uint8_t)i;

	/* 0: remotectl open */
	{
		p[0] = 9; p[1] = 8;
		struct fr_buf in[2] = { { (uint8_t *)p, 8 }, { (uint8_t *)"apps_std", 9 } };

		ol[0] = 8; ol[1] = 8;
		add_step(0, 0, in, 2, ol, 2);
	}
	/* 1: fopen for writing */
	{
		p[0] = sizeof(wname); p[1] = 2;
		struct fr_buf in[3] = { { (uint8_t *)p, 8 }, { (uint8_t *)wname, sizeof(wname) }, { (uint8_t *)"w", 2 } };

		ol[0] = 4;
		add_step(1, 0, in, 3, ol, 1);
	}
	/* 2: fwrite 1000 bytes: longer than the inline 256, handle patched in later */
	{
		p[0] = 0; p[1] = sizeof(big);
		struct fr_buf in[2] = { { (uint8_t *)p, 8 }, { big, sizeof(big) } };

		ol[0] = 8;
		add_step(1, 5, in, 2, ol, 1);
	}
	/* 3: fclose */
	{
		p[0] = 0;
		struct fr_buf in[1] = { { (uint8_t *)p, 4 } };

		add_step(1, 3, in, 1, NULL, 0);
	}
	/* 4: fopen for reading */
	{
		p[0] = sizeof(rname); p[1] = 2;
		struct fr_buf in[3] = { { (uint8_t *)p, 8 }, { (uint8_t *)rname, sizeof(rname) }, { (uint8_t *)"r", 2 } };

		ol[0] = 4;
		add_step(1, 0, in, 3, ol, 1);
	}
	/* 5: fread 300 bytes */
	{
		p[0] = 0; p[1] = 300;
		struct fr_buf in[1] = { { (uint8_t *)p, 8 } };

		ol[0] = 8; ol[1] = 300;
		add_step(1, 4, in, 1, ol, 2);
	}
	/* 6: apps_mem map64 */
	{
		uint64_t w[4];
		uint32_t *u = (uint32_t *)w;

		memset(w, 0, sizeof(w));
		u[0] = 25; u[1] = 0; u[2] = 0x1000;
		w[3] = 4096;
		struct fr_buf in[1] = { { (uint8_t *)w, 32 } };

		ol[0] = 16;
		add_step(2, 2, in, 1, ol, 1);
	}
	/* 7: a method nothing implements still gets a sized reply */
	{
		p[0] = 1;
		struct fr_buf in[1] = { { (uint8_t *)p, 4 } };

		ol[0] = 8;
		add_step(1, 6, in, 1, ol, 1);
	}

	run_capture(log, 6, argv, &rc);
	o = slurp(log);
	CHECK(rc == 1); /* the fake session ends with EIO */
	CHECK(attached && registered && inited);
	CHECK(step_at == n_steps);
	CHECK(rest_calls == 1);
	CHECK(mmaps == 1);
	for (i = 0; i < 7; i++)
		CHECK(script[i].result == 0);
	CHECK(script[7].result == 20);
	CHECK(script[7].reply_len == 16);

	/* Each encoded reply starts: u32 len, pad to 8, data. */
	memcpy(&v, script[0].reply + 8, 4);
	CHECK(v == 1);
	memcpy(&v, script[2].reply + 8, 4);
	CHECK(v == 1000);
	snprintf(path, sizeof(path), "%s/mnt/vendor/persist/sensors/big", dir);
	CHECK(!stat(path, &st) && st.st_size == 1000);
	/* fread reply: prim 8 bytes, then the length, padding, data. */
	{
		uint32_t ln;

		memcpy(&ln, script[5].reply + 16, 4);
		CHECK(ln == 300);
		CHECK(!memcmp(script[5].reply + 24, big, 300));
	}
	CHECK(strstr(o, "[fs] open \"/persist/sensors/big\" mode w"));
}

int main(void)
{
	char dir[] = "/tmp/sensorde2eXXXXXX", p[256], log[256];

	CHECK(mkdtemp(dir) != NULL);
	snprintf(log, sizeof(log), "%s.log", dir);
	snprintf(p, sizeof(p), "mkdir -p %s/mnt/vendor/persist/sensors", dir);
	CHECK(!system(p));

	scenario_ssc(log);
	/* qemu-user before 9.0 lacks openat2; real kernels have had it since 5.6. */
	if (syscall(SYS_openat2, -1, "", NULL, 0) == -1 && errno == ENOSYS)
		printf("SKIP FastRPC scenario: no openat2 here\n");
	else
		scenario_fastrpc(dir, log);

	snprintf(p, sizeof(p), "rm -rf %s %s", dir, log);
	CHECK(!system(p));
	printf("%d checks, %d failures\n", checks, fails);
	return fails ? 1 : 0;
}

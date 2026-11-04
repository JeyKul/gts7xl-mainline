/* SPDX-License-Identifier: MIT */
/* Hardware-free tests: wire format, DSP calls against a temp tree, SSC messages. */
#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "fsrv.h"
#include "pb.h"
#include "ssc.h"

static int fails, checks;

#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

/* Build a stream as the DSP would, using the packing rule. */
static size_t pack(uint8_t *s, const struct fr_buf *in, unsigned n_in, const uint32_t *outl, unsigned n_out)
{
	size_t pos = 0;
	unsigned i;

	for (i = 0; i < n_in; i++) {
		memcpy(s + pos, &in[i].len, 4);
		pos += 4;
		if (in[i].len) {
			pos = (pos + 7) & ~(size_t)7;
			memcpy(s + pos, in[i].p, in[i].len);
			pos += in[i].len;
		}
	}
	for (i = 0; i < n_out; i++) {
		memcpy(s + pos, &outl[i], 4);
		pos += 4;
	}
	return pos;
}

static void test_wire(void)
{
	uint8_t s[256] __attribute__((aligned(8))), e[256];
	uint32_t prim[2] = { 6, 2 }, outl[2] = { 8, 5 };
	struct fr_buf in[3] = { { (uint8_t *)prim, 8 }, { (uint8_t *)"hello", 6 }, { (uint8_t *)"r", 2 } };
	struct fr_req r;
	size_t n = pack(s, in, 3, outl, 2);
	struct fr_buf out[3] = { { (uint8_t *)"abcdefgh", 8 }, { NULL, 0 }, { (uint8_t *)"xyz", 3 } };

	CHECK(!fr_decode(s, n, 1, FR_SC(0, 3, 2), &r));
	CHECK(r.n_in == 3 && r.n_out == 2);
	CHECK(r.in[1].len == 6 && !strcmp((char *)r.in[1].p, "hello"));
	CHECK(r.in[2].len == 2 && !strcmp((char *)r.in[2].p, "r"));
	CHECK(r.out[0].len == 8 && r.out[1].len == 5);
	CHECK(fr_decode(s, n - 1, 1, FR_SC(0, 3, 2), &r) == -1);
	CHECK(fr_decode(s, 2, 1, FR_SC(0, 3, 2), &r) == -1);

	/* Extended method id. */
	{
		uint32_t x[2] = { 31, 7 };
		struct fr_buf i2[1] = { { (uint8_t *)x, 8 } };

		n = pack(s, i2, 1, NULL, 0);
		CHECK(!fr_decode(s, n, 1, FR_SC(31, 1, 0), &r));
		CHECK(r.method == 31 && r.skip == 1);
	}

	/* Output: len, pad to 8, data, next len unaligned, empty buffer has no pad. */
	CHECK(fr_encoded_size(out, 3) == 27);
	n = fr_encode(out, 3, e);
	CHECK(n == 27);
	CHECK(!memcmp(e + 8, "abcdefgh", 8));
	{
		uint32_t l;

		memcpy(&l, e + 16, 4);
		CHECK(l == 0);
		memcpy(&l, e + 20, 4);
		CHECK(l == 3);
	}
	CHECK(!memcmp(e + 24, "xyz", 3));
}

static void test_paths(void)
{
	char o[256];

	CHECK(!fsrv_normalize("/vendor/etc/sensors/", o, sizeof(o)) && !strcmp(o, "vendor/etc/sensors"));
	CHECK(!fsrv_normalize("/persist/sensors/x", o, sizeof(o)) && !strcmp(o, "mnt/vendor/persist/sensors/x"));
	CHECK(!fsrv_normalize("/system/vendor/a", o, sizeof(o)) && !strcmp(o, "vendor/a"));
	CHECK(!fsrv_normalize("//a/./b/../c", o, sizeof(o)) && !strcmp(o, "a/c"));
	CHECK(!fsrv_normalize("/", o, sizeof(o)) && !strcmp(o, "."));
	CHECK(fsrv_normalize("/../etc/passwd", o, sizeof(o)) < 0);
	CHECK(fsrv_normalize("a/../../etc", o, sizeof(o)) < 0);
	CHECK(!fsrv_normalize("/persistent", o, sizeof(o)) && !strcmp(o, "persistent"));
}

/* Call the dispatcher the way the listener does. */
struct call {
	struct fr_req r;
	uint8_t s[1024] __attribute__((aligned(8)));
	uint8_t o[4][512];
	uint32_t res;
};

static void call(struct fsrv *fs, struct call *c, uint32_t handle, unsigned method, const struct fr_buf *in,
		 unsigned n_in, const uint32_t *outl, unsigned n_out)
{
	size_t n = pack(c->s, in, n_in, outl, n_out);
	unsigned i;

	memset(c->o, 0, sizeof(c->o));
	CHECK(!fr_decode(c->s, n, handle, FR_SC(method, n_in, n_out), &c->r));
	for (i = 0; i < n_out; i++)
		c->r.out[i].p = c->o[i];
	c->res = fsrv_dispatch(fs, &c->r);
}

static uint32_t out32(const struct call *c, unsigned o, size_t off)
{
	uint32_t v;

	memcpy(&v, c->o[o] + off, 4);
	return v;
}

static uint32_t d_open(struct fsrv *fs, const char *name, const char *mode, uint32_t *res)
{
	static struct call c;
	uint32_t p[2] = { (uint32_t)strlen(name) + 1, (uint32_t)strlen(mode) + 1 }, ol = 4;
	struct fr_buf in[3] = { { (uint8_t *)p, 8 }, { (uint8_t *)name, p[0] }, { (uint8_t *)mode, p[1] } };

	call(fs, &c, FSRV_H_APPS_STD, 0, in, 3, &ol, 1);
	*res = c.res;
	return out32(&c, 0, 0);
}

static void write_file(const char *p, const char *d)
{
	FILE *f = fopen(p, "w");

	CHECK(f != NULL);
	fputs(d, f);
	fclose(f);
}

static void test_fsrv(void)
{
	static struct call c;
	char dir[] = "/tmp/sensordtXXXXXX", path[512];
	struct fsrv ro, rw;
	uint32_t res, h, p[4], ol[2];
	char secret[600];

	CHECK(mkdtemp(dir) != NULL);
	snprintf(path, sizeof(path), "%s/vendor", dir);
	mkdir(path, 0755);
	snprintf(path, sizeof(path), "%s/vendor/etc", dir);
	mkdir(path, 0755);
	snprintf(path, sizeof(path), "%s/vendor/etc/conf", dir);
	write_file(path, "line one\nline two\n");
	snprintf(path, sizeof(path), "%s/mnt", dir);
	mkdir(path, 0755);
	snprintf(path, sizeof(path), "%s/mnt/vendor", dir);
	mkdir(path, 0755);
	snprintf(path, sizeof(path), "%s/mnt/vendor/persist", dir);
	mkdir(path, 0755);
	snprintf(path, sizeof(path), "%s/mnt/vendor/persist/sensors", dir);
	mkdir(path, 0755);
	snprintf(path, sizeof(path), "%s/mnt/vendor/persist/sensors/registry", dir);
	mkdir(path, 0755);
	snprintf(secret, sizeof(secret), "%s.secret", dir);
	write_file(secret, "outside");
	snprintf(path, sizeof(path), "%s/link", dir);
	CHECK(!symlink(secret, path));

	CHECK(!fsrv_init(&ro, dir, -1, false, false));
	CHECK(!fsrv_init(&rw, dir, -1, true, false));
	CHECK(!fsrv_add_prop(&rw, "ro.revision", "6"));

	/* remotectl */
	{
		p[0] = 13; p[1] = 16;
		struct fr_buf in[2] = { { (uint8_t *)p, 8 }, { (uint8_t *)"sns_registry", 13 } };

		ol[0] = 8; ol[1] = 16;
		call(&rw, &c, FSRV_H_REMOTECTL, 0, in, 2, ol, 2);
		CHECK(c.res == 0 && out32(&c, 0, 0) == FSRV_H_SNS_REGISTRY && out32(&c, 0, 4) == 0);
		in[1].p = (uint8_t *)"nonexistent!";
		call(&rw, &c, FSRV_H_REMOTECTL, 0, in, 2, ol, 2);
		CHECK(c.res == (uint32_t)-5);
	}

	/* sns_registry */
	{
		p[0] = 12; p[1] = 8;
		struct fr_buf in[2] = { { (uint8_t *)p, 8 }, { (uint8_t *)"ro.revision", 12 } };

		ol[0] = 8;
		call(&rw, &c, FSRV_H_SNS_REGISTRY, 0, in, 2, ol, 1);
		CHECK(c.res == 0 && !strcmp((char *)c.o[0], "6"));
		in[1].p = (uint8_t *)"ro.notthere";
		call(&rw, &c, FSRV_H_SNS_REGISTRY, 0, in, 2, ol, 1);
		CHECK(c.res == 0 && c.o[0][0] == 0);
	}

	/* open, read, seek, close */
	h = d_open(&ro, "/vendor/etc/conf", "r", &res);
	CHECK(res == 0 && h > 0);
	{
		p[0] = h; p[1] = 8;
		struct fr_buf in[1] = { { (uint8_t *)p, 8 } };

		ol[0] = 8; ol[1] = 8;
		call(&ro, &c, FSRV_H_APPS_STD, 4, in, 1, ol, 2);
		CHECK(c.res == 0 && out32(&c, 0, 0) == 8 && out32(&c, 0, 4) == 0);
		CHECK(!memcmp(c.o[1], "line one", 8));

		p[1] = 100;
		ol[1] = 100;
		call(&ro, &c, FSRV_H_APPS_STD, 4, in, 1, ol, 2);
		CHECK(c.res == 0 && out32(&c, 0, 0) == 10 && out32(&c, 0, 4) == 1);

		p[0] = h; p[1] = 5; p[2] = 0;
		struct fr_buf sk[1] = { { (uint8_t *)p, 12 } };

		call(&ro, &c, FSRV_H_APPS_STD, 9, sk, 1, NULL, 0);
		CHECK(c.res == 0);
		p[1] = 64;
		ol[0] = 4; ol[1] = 64;
		call(&ro, &c, FSRV_H_APPS_STD, 20, in, 1, ol, 2); /* fgets */
		CHECK(c.res == 0 && !strcmp((char *)c.o[1], "one\n"));
		call(&ro, &c, FSRV_H_APPS_STD, 3, in, 1, NULL, 0);
		CHECK(c.res == 0);
		call(&ro, &c, FSRV_H_APPS_STD, 3, in, 1, NULL, 0);
		CHECK(c.res != 0); /* double close */
	}

	/* confinement */
	d_open(&rw, "/../../etc/passwd", "r", &res);
	CHECK(res != 0);
	d_open(&rw, "/link", "r", &res);
	CHECK(res != 0); /* an absolute symlink out of the tree must not resolve */
	d_open(&ro, "/mnt/vendor/persist/sensors/registry/x", "w", &res);
	CHECK(res == 20); /* read-only mode */
	d_open(&rw, "/vendor/etc/conf", "w", &res);
	CHECK(res == 20); /* outside the writable tree */
	d_open(&rw, "/vendor/etc/conf", "r+", &res);
	CHECK(res == 20);

	/* the write path the firmware uses: temp file, write, rename, remove */
	h = d_open(&rw, "/persist/sensors/registry/fstempfile", "w", &res);
	CHECK(res == 0 && h > 0);
	{
		p[0] = h; p[1] = 5;
		struct fr_buf in[2] = { { (uint8_t *)p, 8 }, { (uint8_t *)"12345", 5 } };

		ol[0] = 8;
		call(&rw, &c, FSRV_H_APPS_STD, 5, in, 2, ol, 1);
		CHECK(c.res == 0 && out32(&c, 0, 0) == 5);
		call(&rw, &c, FSRV_H_APPS_STD, 3, in, 1, NULL, 0);
		CHECK(c.res == 0);
	}
	{
		const char *a = "/persist/sensors/registry/fstempfile", *b = "/persist/sensors/registry/final";
		uint32_t w[3] = { 33, (uint32_t)strlen(a) + 1, (uint32_t)strlen(b) + 1 };
		struct fr_buf in[3] = { { (uint8_t *)w, 12 }, { (uint8_t *)a, w[1] }, { (uint8_t *)b, w[2] } };

		call(&ro, &c, FSRV_H_APPS_STD, 31, in, 3, NULL, 0);
		CHECK(c.r.method == 33 && c.res == 20); /* refused read-only */
		call(&rw, &c, FSRV_H_APPS_STD, 31, in, 3, NULL, 0);
		CHECK(c.res == 0);
		snprintf(path, sizeof(path), "%s/mnt/vendor/persist/sensors/registry/final", dir);
		CHECK(access(path, F_OK) == 0);

		/* stat, an extended call */
		uint32_t st[2] = { 31, (uint32_t)strlen(b) + 1 };
		struct fr_buf si[2] = { { (uint8_t *)st, 8 }, { (uint8_t *)b, st[1] } };
		uint64_t size;

		ol[0] = 96;
		call(&rw, &c, FSRV_H_APPS_STD, 31, si, 2, ol, 1);
		CHECK(c.res == 0);
		memcpy(&size, c.o[0] + 40, 8);
		CHECK(size == 5);

		/* remove */
		uint32_t rm = (uint32_t)strlen(b) + 1;
		struct fr_buf ri[2] = { { (uint8_t *)&rm, 4 }, { (uint8_t *)b, rm } };

		call(&rw, &c, FSRV_H_APPS_STD, 24, ri, 2, NULL, 0);
		CHECK(c.res == 0 && access(path, F_OK) != 0);
	}

	/* directories */
	{
		const char *d = "/vendor/etc";
		uint32_t l = (uint32_t)strlen(d) + 1;
		struct fr_buf in[2] = { { (uint8_t *)&l, 4 }, { (uint8_t *)d, l } };
		uint64_t dh;

		ol[0] = 8;
		call(&ro, &c, FSRV_H_APPS_STD, 26, in, 2, ol, 1);
		CHECK(c.res == 0);
		memcpy(&dh, c.o[0], 8);
		struct fr_buf rd[1] = { { (uint8_t *)&dh, 8 } };

		ol[0] = 264;
		call(&ro, &c, FSRV_H_APPS_STD, 28, rd, 1, ol, 1);
		CHECK(c.res == 0 && !strcmp((char *)c.o[0] + 4, "conf") && out32(&c, 0, 260) == 0);
		call(&ro, &c, FSRV_H_APPS_STD, 28, rd, 1, ol, 1);
		CHECK(c.res == 0 && out32(&c, 0, 260) == 1);
		call(&ro, &c, FSRV_H_APPS_STD, 27, rd, 1, NULL, 0);
		CHECK(c.res == 0);
	}

	/* unknown things fail cleanly, sized outputs stay zeroed */
	ol[0] = 16;
	call(&rw, &c, FSRV_H_APPS_STD, 6, NULL, 0, ol, 1);
	CHECK(c.res == 20);
	call(&rw, &c, 77, 0, NULL, 0, ol, 1);
	CHECK(c.res == 20);
	/* a short primary buffer is a bad parameter, not a crash */
	{
		uint8_t z = 0;
		struct fr_buf in[1] = { { &z, 1 } };

		call(&rw, &c, FSRV_H_APPS_STD, 4, in, 1, NULL, 0);
		CHECK(c.res == 14);
		call(&rw, &c, FSRV_H_APPS_STD, 3, in, 1, NULL, 0);
		CHECK(c.res == 14);
	}
	/* unterminated string */
	{
		uint32_t l = 3;
		struct fr_buf in[2] = { { (uint8_t *)&l, 4 }, { (uint8_t *)"abc", 3 } };

		call(&rw, &c, FSRV_H_APPS_STD, 24, in, 2, NULL, 0);
		CHECK(c.res == 14);
	}

	fsrv_fini(&ro);
	fsrv_fini(&rw);
	unlink(secret);
	{
		char cmd[512];

		snprintf(cmd, sizeof(cmd), "rm -rf %s", dir);
		CHECK(system(cmd) == 0);
	}
}

/* --- SSC --- */

/* A captured indication published in libssc's docs/Qualcomm-Sensorcore-Docs.md (a fact, not code). */
static const uint8_t captured[] = {
	0x04,0x05,0x00,0x22,0x00,0x53,0x00,0x01,0x08,0x00,0xa9,0x19,0x00,0x00,0x00,0x00,0x00,0x00,
	0x02,0x45,0x00,0x43,0x00,0x0a,0x12,0x09,0xab,0xab,0xab,0xab,0xab,0xab,0xab,0xab,0x11,0xab,
	0xab,0xab,0xab,0xab,0xab,0xab,0xab,0x12,0x2d,0x0d,0x00,0x03,0x00,0x00,0x11,0x8a,0xa3,0x6a,
	0x89,0x3c,0x35,0x00,0x00,0x1a,0x1d,0x0a,0x07,0x67,0x72,0x61,0x76,0x69,0x74,0x79,0x12,0x12,
	0x09,0x2e,0xf5,0x5b,0xf6,0xc0,0x98,0x43,0xa7,0x11,0xa5,0xa6,0x50,0x68,0x2d,0x5a,0xd4,0xc5,
};

struct seen {
	int n;
	struct ssc_uid uid;
	uint32_t id;
	uint64_t ts;
	char type[32];
	struct ssc_uid suid[2];
	int nsuid;
};

static void on_ev(void *ctx, struct ssc_uid uid, uint32_t id, uint64_t ts, const uint8_t *b, size_t l)
{
	struct seen *s = ctx;

	s->n++;
	s->uid = uid;
	s->id = id;
	s->ts = ts;
	s->nsuid = ssc_parse_suid(b, l, s->type, sizeof(s->type), s->suid, 2);
}

static void test_ssc_capture(void)
{
	struct qmi_msg m;
	struct seen s;
	size_t n;
	const uint8_t *pb;

	memset(&s, 0, sizeof(s));
	CHECK(!qmi_parse(captured, sizeof(captured), &m));
	CHECK(m.type == QMI_INDICATION && m.id == QMI_SSC_REPORT_LARGE && m.txn == 5);
	pb = ssc_report_payload(&m, &n);
	CHECK(pb && n == 0x43);
	CHECK(ssc_parse_report(pb, n, on_ev, &s) == 1);
	CHECK(s.n == 1 && s.id == SSC_MSG_SUID_RESP);
	CHECK(s.uid.low == SSC_SUID_UID && s.uid.high == SSC_SUID_UID);
	CHECK(s.ts == 0x353c896aa38aULL);
	CHECK(!strcmp(s.type, "gravity"));
	CHECK(s.nsuid == 1);
	CHECK(s.suid[0].low == 0xa74398c0f65bf52eULL && s.suid[0].high == 0xc5d45a2d6850a6a5ULL);
}

static void test_ssc_build(void)
{
	uint8_t q[512], b[64];
	struct ssc_uid uid = { SSC_SUID_UID, SSC_SUID_UID };
	size_t bl = ssc_body_suid(b, sizeof(b), "accel", true);
	size_t n = ssc_build_control(q, sizeof(q), 7, uid, SSC_MSG_SUID_REQ, 0, b, bl, true);
	struct qmi_msg m;
	const uint8_t *v;
	size_t vl;
	static const uint8_t body_expect[] = { 0x0a, 5, 'a','c','c','e','l', 0x10, 0, 0x18, 1 };

	CHECK(bl == sizeof(body_expect) && !memcmp(b, body_expect, bl));
	CHECK(n && !qmi_parse(q, n, &m));
	CHECK(m.type == QMI_REQUEST && m.txn == 7 && m.id == QMI_SSC_CONTROL);
	v = qmi_tlv(&m, 0x10, &vl);
	CHECK(v && vl == 1 && v[0] == 1);
	v = qmi_tlv(&m, 0x01, &vl);
	CHECK(v != NULL);
	if (v) {
		uint16_t l;
		struct pb_r r;
		struct pb_field f;
		int fields = 0;

		memcpy(&l, v, 2);
		CHECK(l == vl - 2);
		pb_r_init(&r, v + 2, l);
		while (pb_next(&r, &f)) {
			fields++;
			if (f.field == 2) {
				CHECK(f.wire == 5 && f.num == SSC_MSG_SUID_REQ);
			} else if (f.field == 3) {
				static const uint8_t cfg[] = { 0x08, 1, 0x10, 0 };

				CHECK(f.len == 4 && !memcmp(f.data, cfg, 4));
			} else if (f.field == 4) {
				/* request { msg: body } */
				CHECK(f.len == bl + 2 && f.data[0] == 0x12 && f.data[1] == bl);
				CHECK(!memcmp(f.data + 2, b, bl));
			}
		}
		CHECK(!r.bad && fields == 4);
	}

	/* Without a body the request submessage is present but empty. */
	n = ssc_build_control(q, sizeof(q), 8, uid, SSC_MSG_DISABLE, 1, NULL, 0, false);
	CHECK(n);
	CHECK(ssc_body_rate(b, sizeof(b), 50.0f) == 5 && b[0] == 0x0d);
}

static void test_samples(void)
{
	uint8_t b[64];
	struct pb_w w;
	float v[4];
	int acc, n;

	/* repeated, unpacked */
	pb_w_init(&w, b, sizeof(b));
	pb_float(&w, 1, 1.5f);
	pb_float(&w, 1, -9.8f);
	pb_float(&w, 1, 0.25f);
	pb_varint(&w, 2, 3);
	n = ssc_parse_sample(b, w.len, v, 4, &acc);
	CHECK(n == 3 && acc == 3 && v[0] == 1.5f && v[1] == -9.8f && v[2] == 0.25f);

	/* packed */
	{
		float x[3] = { 2.0f, 4.0f, 8.0f };

		pb_w_init(&w, b, sizeof(b));
		pb_bytes(&w, 1, x, sizeof(x));
		pb_varint(&w, 2, 1);
		n = ssc_parse_sample(b, w.len, v, 4, &acc);
		CHECK(n == 3 && acc == 1 && v[2] == 8.0f);
	}

	/* truncated */
	CHECK(ssc_parse_sample(b, 2, v, 4, &acc) == -1);
}

struct attrs_seen {
	char name[16];
	float rate;
	int n_rates;
	uint64_t stream;
	int avail;
};

static void on_attr(void *ctx, int id, const struct ssc_attr_value *v)
{
	struct attrs_seen *a = ctx;

	if (id == SSC_ATTR_NAME && v->kind == ATTR_STR)
		snprintf(a->name, sizeof(a->name), "%.*s", (int)v->slen, v->s);
	else if (id == SSC_ATTR_RATES && v->kind == ATTR_FLOAT) {
		a->rate = v->f;
		a->n_rates++;
	} else if (id == SSC_ATTR_STREAM_TYPE && v->kind == ATTR_INT)
		a->stream = v->i;
	else if (id == SSC_ATTR_AVAILABLE && v->kind == ATTR_BOOL)
		a->avail = v->b;
}

static size_t attr_with(uint8_t *out, size_t cap, int id, const uint8_t *val, size_t vl)
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

static void test_attrs(void)
{
	uint8_t msg[256], a[128], v[32];
	struct pb_w m, w;
	struct attrs_seen s;
	size_t n;

	memset(&s, 0, sizeof(s));
	pb_w_init(&m, msg, sizeof(msg));

	pb_w_init(&w, v, sizeof(v));
	pb_bytes(&w, 2, "lsm6dso", 7);
	n = attr_with(a, sizeof(a), SSC_ATTR_NAME, v, w.len);
	pb_bytes(&m, 1, a, n);

	pb_w_init(&w, v, sizeof(v));
	pb_float(&w, 3, 104.0f);
	n = attr_with(a, sizeof(a), SSC_ATTR_RATES, v, w.len);
	pb_bytes(&m, 1, a, n);

	pb_w_init(&w, v, sizeof(v));
	pb_fixed64(&w, 4, 1);
	n = attr_with(a, sizeof(a), SSC_ATTR_STREAM_TYPE, v, w.len);
	pb_bytes(&m, 1, a, n);

	pb_w_init(&w, v, sizeof(v));
	pb_varint(&w, 5, 1);
	n = attr_with(a, sizeof(a), SSC_ATTR_AVAILABLE, v, w.len);
	pb_bytes(&m, 1, a, n);

	CHECK(!m.overflow);
	CHECK(!ssc_parse_attrs(msg, m.len, on_attr, &s));
	CHECK(!strcmp(s.name, "lsm6dso") && s.rate == 104.0f && s.stream == 1 && s.avail == 1);
}

static void test_pb_robust(void)
{
	/* Malformed input must end cleanly. */
	static const uint8_t bad[][6] = {
		{ 0x0a, 0x7f, 0, 0, 0, 0 },		/* length past the end */
		{ 0xff, 0xff, 0xff, 0xff, 0xff, 0xff },	/* endless varint */
		{ 0x0b, 0, 0, 0, 0, 0 },		/* group wire type */
		{ 0x09, 1, 2, 3, 0, 0 },		/* short fixed64 */
	};
	size_t i;

	for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
		struct pb_r r;
		struct pb_field f;
		unsigned g = 0;

		pb_r_init(&r, bad[i], i == 3 ? 4 : 6);
		while (pb_next(&r, &f) && g < 100)
			g++;
		CHECK(r.bad);
	}

	/* Truncated QMI never reads past the buffer. */
	{
		struct qmi_msg m;
		size_t k, vl;

		for (k = 0; k < sizeof(captured); k++) {
			if (!qmi_parse(captured, k, &m))
				(void)qmi_tlv(&m, 0x02, &vl);
		}
		CHECK(qmi_parse(captured, 3, &m) == -1);
	}
}

int main(void)
{
	test_wire();
	test_paths();
	/* qemu-user before 9.0 lacks openat2; real kernels have had it since 5.6. */
	if (syscall(SYS_openat2, -1, "", NULL, 0) == -1 && errno == ENOSYS)
		printf("SKIP file service tests: no openat2 here\n");
	else
		test_fsrv();
	test_ssc_capture();
	test_ssc_build();
	test_samples();
	test_attrs();
	test_pb_robust();
	printf("%d checks, %d failures\n", checks, fails);
	return fails ? 1 : 0;
}

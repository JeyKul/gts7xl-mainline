/* SPDX-License-Identifier: MIT */
#include <string.h>

#include "pb.h"
#include "ssc.h"

/* --- building ---------------------------------------------------------- */

size_t ssc_body_suid(void *buf, size_t cap, const char *data_type, bool only_default)
{
	struct pb_w w;

	pb_w_init(&w, buf, cap);
	pb_bytes(&w, 1, data_type, strlen(data_type));
	pb_varint(&w, 2, 0);			/* enable_updates */
	pb_varint(&w, 3, only_default);		/* only_default_values */
	return w.overflow ? 0 : w.len;
}

size_t ssc_body_attr(void *buf, size_t cap)
{
	struct pb_w w;

	pb_w_init(&w, buf, cap);
	pb_varint(&w, 2, 0);			/* enable_updates */
	return w.overflow ? 0 : w.len;
}

size_t ssc_body_rate(void *buf, size_t cap, float hz)
{
	struct pb_w w;

	pb_w_init(&w, buf, cap);
	pb_float(&w, 1, hz);
	return w.overflow ? 0 : w.len;
}

static void put_tlv_head(uint8_t **d, uint8_t type, size_t len)
{
	uint16_t l = (uint16_t)len;

	*(*d)++ = type;
	memcpy(*d, &l, 2);
	*d += 2;
}

size_t ssc_build_control(void *buf, size_t cap, uint16_t txn, struct ssc_uid uid, uint32_t msg_id,
			 unsigned delivery, const void *body, size_t body_len, bool has_body)
{
	uint8_t suid[32], cfg[16], req[1024], client[1200];
	struct pb_w w;
	size_t suid_n, cfg_n, req_n, client_n, total;
	uint8_t *d = buf;
	uint16_t l;

	pb_w_init(&w, suid, sizeof(suid));
	pb_fixed64(&w, 1, uid.low);
	pb_fixed64(&w, 2, uid.high);
	suid_n = w.len;

	pb_w_init(&w, cfg, sizeof(cfg));
	pb_varint(&w, 1, 1);			/* processor: apps */
	pb_varint(&w, 2, delivery);
	cfg_n = w.len;

	pb_w_init(&w, req, sizeof(req));
	if (has_body)
		pb_bytes(&w, 2, body, body_len);
	if (w.overflow)
		return 0;
	req_n = w.len;

	pb_w_init(&w, client, sizeof(client));
	pb_bytes(&w, 1, suid, suid_n);
	pb_fixed32(&w, 2, msg_id);
	pb_bytes(&w, 3, cfg, cfg_n);
	pb_bytes(&w, 4, req, req_n);
	if (w.overflow)
		return 0;
	client_n = w.len;

	/* Header 7; data TLV 3 + 2 + payload; report-type TLV 3 + 1. */
	total = 7 + 3 + 2 + client_n + 3 + 1;
	if (total > cap)
		return 0;

	*d++ = QMI_REQUEST;
	memcpy(d, &txn, 2);
	d += 2;
	l = QMI_SSC_CONTROL;
	memcpy(d, &l, 2);
	d += 2;
	l = (uint16_t)(total - 7);
	memcpy(d, &l, 2);
	d += 2;

	put_tlv_head(&d, 0x01, client_n + 2);
	l = (uint16_t)client_n;
	memcpy(d, &l, 2);
	d += 2;
	memcpy(d, client, client_n);
	d += client_n;

	put_tlv_head(&d, 0x10, 1);
	*d++ = 1;				/* large reports */
	return total;
}

/* --- QMI parsing ------------------------------------------------------- */

int qmi_parse(const void *buf, size_t len, struct qmi_msg *m)
{
	const uint8_t *b = buf;
	uint16_t l;

	if (len < 7)
		return -1;
	m->type = b[0];
	memcpy(&m->txn, b + 1, 2);
	memcpy(&m->id, b + 3, 2);
	memcpy(&l, b + 5, 2);
	if (l > len - 7)
		return -1;
	m->tlvs = b + 7;
	m->tlv_len = l;
	return 0;
}

const uint8_t *qmi_tlv(const struct qmi_msg *m, uint8_t type, size_t *len)
{
	size_t pos = 0;

	while (m->tlv_len - pos >= 3) {
		uint8_t t = m->tlvs[pos];
		uint16_t l;

		memcpy(&l, m->tlvs + pos + 1, 2);
		pos += 3;
		if (l > m->tlv_len - pos)
			return NULL;
		if (t == type) {
			*len = l;
			return m->tlvs + pos;
		}
		pos += l;
	}
	return NULL;
}

int qmi_result(const struct qmi_msg *m)
{
	size_t n;
	const uint8_t *v = qmi_tlv(m, 0x02, &n);
	uint16_t res, err;

	if (!v || n < 4)
		return -1;
	memcpy(&res, v, 2);
	memcpy(&err, v + 2, 2);
	return res ? (int)err : 0;
}

const uint8_t *ssc_report_payload(const struct qmi_msg *m, size_t *len)
{
	size_t n;
	const uint8_t *v = qmi_tlv(m, 0x02, &n);
	uint16_t l;

	if (!v || n < 2)
		return NULL;
	memcpy(&l, v, 2);
	if (l > n - 2)
		return NULL;
	*len = l;
	return v + 2;
}

/* --- protobuf parsing -------------------------------------------------- */

static bool parse_uid(const uint8_t *b, size_t len, struct ssc_uid *uid)
{
	struct pb_r r;
	struct pb_field f;

	memset(uid, 0, sizeof(*uid));
	pb_r_init(&r, b, len);
	while (pb_next(&r, &f)) {
		if (f.field == 1)
			uid->low = f.num;
		else if (f.field == 2)
			uid->high = f.num;
	}
	return !r.bad;
}

int ssc_parse_report(const void *pb, size_t len, ssc_event_cb cb, void *ctx)
{
	struct pb_r r;
	struct pb_field f;
	struct ssc_uid uid = { 0, 0 };
	const uint8_t *ev[64];
	size_t evl[64];
	unsigned n = 0, i;

	/* The uid field may in principle follow the events: collect, then call. */
	pb_r_init(&r, pb, len);
	while (pb_next(&r, &f)) {
		if (f.wire != 2)
			continue;
		if (f.field == 1) {
			if (!parse_uid(f.data, f.len, &uid))
				return -1;
		} else if (f.field == 2 && n < 64) {
			ev[n] = f.data;
			evl[n] = f.len;
			n++;
		}
	}
	if (r.bad)
		return -1;
	for (i = 0; i < n; i++) {
		struct pb_r e;
		uint32_t id = 0;
		uint64_t ts = 0;
		const uint8_t *body = NULL;
		size_t blen = 0;

		pb_r_init(&e, ev[i], evl[i]);
		while (pb_next(&e, &f)) {
			if (f.field == 1)
				id = (uint32_t)f.num;
			else if (f.field == 2)
				ts = f.num;
			else if (f.field == 3 && f.wire == 2) {
				body = f.data;
				blen = f.len;
			}
		}
		if (e.bad)
			return -1;
		cb(ctx, uid, id, ts, body, blen);
	}
	return (int)n;
}

int ssc_parse_suid(const uint8_t *b, size_t len, char *type, size_t type_cap, struct ssc_uid *uids,
		   int max)
{
	struct pb_r r;
	struct pb_field f;
	int n = 0;

	if (type_cap)
		type[0] = 0;
	pb_r_init(&r, b, len);
	while (pb_next(&r, &f)) {
		if (f.wire != 2)
			continue;
		if (f.field == 1 && type_cap) {
			size_t c = f.len < type_cap - 1 ? f.len : type_cap - 1;

			memcpy(type, f.data, c);
			type[c] = 0;
		} else if (f.field == 2 && n < max) {
			if (!parse_uid(f.data, f.len, &uids[n]))
				return -1;
			n++;
		}
	}
	return r.bad ? -1 : n;
}

int ssc_parse_sample(const uint8_t *b, size_t len, float *v, int max, int *accuracy)
{
	struct pb_r r;
	struct pb_field f;
	int n = 0;

	*accuracy = -1;
	pb_r_init(&r, b, len);
	while (pb_next(&r, &f)) {
		if (f.field == 1 && f.wire == 5) {
			if (n < max)
				v[n++] = pb_as_float(&f);
		} else if (f.field == 1 && f.wire == 2) {
			size_t o;

			for (o = 0; o + 4 <= f.len && n < max; o += 4)
				memcpy(&v[n++], f.data + o, 4);
		} else if (f.field == 2) {
			*accuracy = (int)f.num;
		}
	}
	return r.bad ? -1 : n;
}

static void parse_value(const uint8_t *b, size_t len, struct ssc_attr_value *a)
{
	struct pb_r r;
	struct pb_field f;

	memset(a, 0, sizeof(*a));
	pb_r_init(&r, b, len);
	while (pb_next(&r, &f)) {
		switch (f.field) {
		case 1:
			a->kind = ATTR_ARRAY;
			break;
		case 2:
			a->kind = ATTR_STR;
			a->s = (const char *)f.data;
			a->slen = f.len;
			break;
		case 3:
			a->kind = ATTR_FLOAT;
			a->f = pb_as_float(&f);
			break;
		case 4:
			a->kind = ATTR_INT;
			a->i = f.num;
			break;
		case 5:
			a->kind = ATTR_BOOL;
			a->b = f.num != 0;
			break;
		default:
			break;
		}
	}
}

int ssc_parse_attrs(const uint8_t *b, size_t len, ssc_attr_cb cb, void *ctx)
{
	struct pb_r r;
	struct pb_field f;

	pb_r_init(&r, b, len);
	while (pb_next(&r, &f)) {
		struct pb_r a, va;
		struct pb_field g, v;
		const uint8_t *values = NULL;
		size_t values_len = 0;
		int id = -1;

		if (f.field != 1 || f.wire != 2)
			continue;
		pb_r_init(&a, f.data, f.len);
		while (pb_next(&a, &g)) {
			if (g.field == 1)
				id = (int)g.num;
			else if (g.field == 2 && g.wire == 2) {
				values = g.data;
				values_len = g.len;
			}
		}
		if (a.bad)
			return -1;
		if (!values)
			continue;
		pb_r_init(&va, values, values_len);
		while (pb_next(&va, &v)) {
			struct ssc_attr_value av;

			if (v.field != 1 || v.wire != 2)
				continue;
			parse_value(v.data, v.len, &av);
			cb(ctx, id, &av);
		}
		if (va.bad)
			return -1;
	}
	return r.bad ? -1 : 0;
}

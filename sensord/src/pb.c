/* SPDX-License-Identifier: MIT */
#include <string.h>

#include "pb.h"

void pb_w_init(struct pb_w *w, void *buf, size_t cap)
{
	w->p = buf;
	w->cap = cap;
	w->len = 0;
	w->overflow = false;
}

static void raw(struct pb_w *w, const void *d, size_t n)
{
	if (n > w->cap - w->len) {
		w->overflow = true;
		return;
	}
	if (n)
		memcpy(w->p + w->len, d, n);
	w->len += n;
}

static void put_varint(struct pb_w *w, uint64_t v)
{
	uint8_t b[10];
	size_t n = 0;

	do {
		b[n] = (uint8_t)(v & 0x7f);
		v >>= 7;
		if (v)
			b[n] |= 0x80;
		n++;
	} while (v);
	raw(w, b, n);
}

void pb_varint(struct pb_w *w, unsigned field, uint64_t v)
{
	put_varint(w, (uint64_t)field << 3);
	put_varint(w, v);
}

void pb_fixed32(struct pb_w *w, unsigned field, uint32_t v)
{
	put_varint(w, ((uint64_t)field << 3) | 5);
	raw(w, &v, 4);
}

void pb_fixed64(struct pb_w *w, unsigned field, uint64_t v)
{
	put_varint(w, ((uint64_t)field << 3) | 1);
	raw(w, &v, 8);
}

void pb_float(struct pb_w *w, unsigned field, float v)
{
	uint32_t u;

	memcpy(&u, &v, 4);
	pb_fixed32(w, field, u);
}

void pb_bytes(struct pb_w *w, unsigned field, const void *d, size_t n)
{
	put_varint(w, ((uint64_t)field << 3) | 2);
	put_varint(w, n);
	raw(w, d, n);
}

void pb_r_init(struct pb_r *r, const void *buf, size_t len)
{
	r->p = buf;
	r->left = len;
	r->bad = false;
}

static bool get_varint(struct pb_r *r, uint64_t *v)
{
	unsigned shift = 0;

	*v = 0;
	while (r->left && shift < 64) {
		uint8_t b = *r->p++;

		r->left--;
		*v |= (uint64_t)(b & 0x7f) << shift;
		if (!(b & 0x80))
			return true;
		shift += 7;
	}
	r->bad = true;
	return false;
}

bool pb_next(struct pb_r *r, struct pb_field *f)
{
	uint64_t tag, v;

	if (!r->left)
		return false;
	if (!get_varint(r, &tag))
		return false;
	memset(f, 0, sizeof(*f));
	f->field = (unsigned)(tag >> 3);
	f->wire = (unsigned)(tag & 7);
	switch (f->wire) {
	case 0:
		return get_varint(r, &f->num);
	case 1:
	case 5: {
		size_t n = f->wire == 1 ? 8 : 4;

		if (r->left < n) {
			r->bad = true;
			return false;
		}
		v = 0;
		memcpy(&v, r->p, n);
		f->num = v;
		r->p += n;
		r->left -= n;
		return true;
	}
	case 2:
		if (!get_varint(r, &v))
			return false;
		if (v > r->left) {
			r->bad = true;
			return false;
		}
		f->data = r->p;
		f->len = (size_t)v;
		r->p += v;
		r->left -= (size_t)v;
		return true;
	default:
		r->bad = true;
		return false;
	}
}

float pb_as_float(const struct pb_field *f)
{
	uint32_t u = (uint32_t)f->num;
	float v;

	memcpy(&v, &u, 4);
	return v;
}

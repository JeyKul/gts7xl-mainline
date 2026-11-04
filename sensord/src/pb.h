/* SPDX-License-Identifier: MIT */
/* Just enough protobuf wire format, written against the public encoding spec. */
#ifndef PB_H
#define PB_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct pb_w {
	uint8_t *p;
	size_t cap, len;
	bool overflow;
};

void pb_w_init(struct pb_w *w, void *buf, size_t cap);
void pb_varint(struct pb_w *w, unsigned field, uint64_t v);
void pb_fixed32(struct pb_w *w, unsigned field, uint32_t v);
void pb_fixed64(struct pb_w *w, unsigned field, uint64_t v);
void pb_float(struct pb_w *w, unsigned field, float v);
void pb_bytes(struct pb_w *w, unsigned field, const void *d, size_t n);

struct pb_field {
	unsigned field, wire;
	uint64_t num;		/* wire 0, 1, 5 */
	const uint8_t *data;	/* wire 2 */
	size_t len;
};

struct pb_r {
	const uint8_t *p;
	size_t left;
	bool bad;
};

void pb_r_init(struct pb_r *r, const void *buf, size_t len);
/* Next field; false at the end or on malformed input (r->bad set). */
bool pb_next(struct pb_r *r, struct pb_field *f);
float pb_as_float(const struct pb_field *f);

#endif

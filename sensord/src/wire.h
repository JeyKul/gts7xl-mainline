/* SPDX-License-Identifier: MIT */
/* FastRPC reverse-tunnel stream format, as packed by the DSP's listener. */
#ifndef WIRE_H
#define WIRE_H
#include <stddef.h>
#include <stdint.h>

#define FR_MAX_BUFS 32

/* sc: [31:29] attr, [28:24] method, [23:16] in, [15:8] out, [7:4] in handles, [3:0] out handles */
#define FR_SC(m, in, out) \
	((((uint32_t)(m) & 0x1f) << 24) | (((uint32_t)(in) & 0xff) << 16) | (((uint32_t)(out) & 0xff) << 8))
#define FR_SC_METHOD(sc) (((sc) >> 24) & 0x1f)
#define FR_SC_IN(sc) (((sc) >> 16) & 0xff)
#define FR_SC_OUT(sc) (((sc) >> 8) & 0xff)
#define FR_SC_HANDLES(sc) ((sc) & 0xff)

struct fr_buf {
	uint8_t *p;
	uint32_t len;
};

struct fr_req {
	uint32_t handle;
	uint32_t sc;
	uint32_t method;	/* after resolving the extended id */
	unsigned skip;		/* primary-input words used by the extended id */
	unsigned n_in, n_out;
	struct fr_buf in[FR_MAX_BUFS];
	struct fr_buf out[FR_MAX_BUFS];
};

/*
 * Decode a stream. in[] points into stream; out[].len holds the requested
 * output sizes and out[].p is left NULL. Returns 0, or -1 if malformed.
 */
int fr_decode(const void *stream, size_t len, uint32_t handle, uint32_t sc, struct fr_req *req);

/* Bytes needed to encode the n output buffers. */
size_t fr_encoded_size(const struct fr_buf *out, unsigned n);
/* Encode into dst, which must have fr_encoded_size() bytes; returns bytes written. */
size_t fr_encode(const struct fr_buf *out, unsigned n, void *dst);

#endif

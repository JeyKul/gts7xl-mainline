/* SPDX-License-Identifier: MIT */
#include <string.h>

#include "wire.h"

static size_t align8(size_t v)
{
	return (v + 7) & ~(size_t)7;
}

static int read_u32(const uint8_t *s, size_t len, size_t *pos, uint32_t *v)
{
	if (len < 4 || *pos > len - 4)
		return -1;
	memcpy(v, s + *pos, 4);
	*pos += 4;
	return 0;
}

int fr_decode(const void *stream, size_t len, uint32_t handle, uint32_t sc, struct fr_req *req)
{
	const uint8_t *s = stream;
	size_t pos = 0;
	unsigned i;

	memset(req, 0, sizeof(*req));
	req->handle = handle;
	req->sc = sc;
	req->method = FR_SC_METHOD(sc);
	req->n_in = FR_SC_IN(sc);
	req->n_out = FR_SC_OUT(sc);
	if (req->n_in > FR_MAX_BUFS || req->n_out > FR_MAX_BUFS)
		return -1;

	for (i = 0; i < req->n_in; i++) {
		uint32_t n;

		if (read_u32(s, len, &pos, &n))
			return -1;
		req->in[i].len = n;
		if (!n)
			continue;
		pos = align8(pos);
		if (pos > len || n > len - pos)
			return -1;
		req->in[i].p = (uint8_t *)s + pos;
		pos += n;
	}

	/* Sizes of the output buffers follow the inputs, unpadded. */
	for (i = 0; i < req->n_out; i++)
		if (read_u32(s, len, &pos, &req->out[i].len))
			return -1;

	/* Method ids above 30 are carried in the first primary-input word. */
	if (req->method == 31) {
		if (!req->n_in || req->in[0].len < 4)
			return -1;
		memcpy(&req->method, req->in[0].p, 4);
		req->skip = 1;
	}
	return 0;
}

size_t fr_encoded_size(const struct fr_buf *out, unsigned n)
{
	size_t pos = 0;
	unsigned i;

	for (i = 0; i < n; i++) {
		pos += 4;
		if (out[i].len)
			pos = align8(pos) + out[i].len;
	}
	return pos;
}

size_t fr_encode(const struct fr_buf *out, unsigned n, void *dst)
{
	uint8_t *d = dst;
	size_t pos = 0;
	unsigned i;

	for (i = 0; i < n; i++) {
		uint32_t len = out[i].len;

		memcpy(d + pos, &len, 4);
		pos += 4;
		if (!len)
			continue;
		memset(d + pos, 0, align8(pos) - pos);
		pos = align8(pos);
		if (out[i].p)
			memcpy(d + pos, out[i].p, len);
		else
			memset(d + pos, 0, len);
		pos += len;
	}
	return pos;
}

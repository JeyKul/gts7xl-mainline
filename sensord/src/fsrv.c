/* SPDX-License-Identifier: MIT */
/*
 * Serves apps_std (file calls), apps_mem, remotectl and sns_registry to the DSP.
 * The served directory mirrors the absolute paths the firmware asks for, so
 * /vendor/etc/sensors/sns_reg_config is DIR/vendor/etc/sensors/sns_reg_config.
 * Argument layout rule: "in" scalars and the lengths of every sequence go in
 * primary-input words in declaration order; sequence bodies get their own
 * buffers; "rout" scalars go in a primary output buffer, which comes first.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/openat2.h>
#include <misc/fastrpc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "fsrv.h"

/* Only nonzero matters to the DSP; values follow AEEStdErr without the offset. */
#define AEE_EFAILED 1
#define AEE_ENOMEMORY 2
#define AEE_EBADPARM 14
#define AEE_EUNSUPPORTED 20

#define PATH_CAP 512
#define RW_PREFIX "mnt/vendor/persist/sensors"
#define ADSP_MMAP_ADD_PAGES 0x1000

#define LOG(s, ...) do { if ((s)->verbose) printf(__VA_ARGS__); } while (0)

/* --- paths ------------------------------------------------------------- */

static const struct { const char *from, *to; } aliases[] = {
	{ "persist", "mnt/vendor/persist" },
	{ "system/vendor", "vendor" },
};

int fsrv_normalize(const char *in, char *out, size_t cap)
{
	char tmp[PATH_CAP];
	size_t n = 0, i;
	const char *p = in;

	if (strlen(in) >= sizeof(tmp))
		return -ENAMETOOLONG;

	/* Collapse "//" and ".", resolve ".." without ever rising above the root. */
	while (*p) {
		const char *e;
		size_t l;

		while (*p == '/')
			p++;
		e = p;
		while (*e && *e != '/')
			e++;
		l = (size_t)(e - p);
		if (l == 1 && p[0] == '.') {
			/* skip */
		} else if (l == 2 && p[0] == '.' && p[1] == '.') {
			if (!n)
				return -EACCES;
			while (n && tmp[n - 1] != '/')
				n--;
			if (n)
				n--;
		} else if (l) {
			if (n)
				tmp[n++] = '/';
			memcpy(tmp + n, p, l);
			n += l;
		}
		p = e;
	}
	tmp[n] = 0;

	for (i = 0; i < sizeof(aliases) / sizeof(aliases[0]); i++) {
		size_t fl = strlen(aliases[i].from);

		if (!strncmp(tmp, aliases[i].from, fl) && (tmp[fl] == 0 || tmp[fl] == '/')) {
			if ((size_t)snprintf(out, cap, "%s%s", aliases[i].to, tmp + fl) >= cap)
				return -ENAMETOOLONG;
			return 0;
		}
	}
	if ((size_t)snprintf(out, cap, "%s", n ? tmp : ".") >= cap)
		return -ENAMETOOLONG;
	return 0;
}

static int beneath_open(struct fsrv *s, const char *rel, int flags, int mode)
{
	struct open_how how = {
		.flags = (uint64_t)flags | O_CLOEXEC,
		.mode = (flags & O_CREAT) ? (uint64_t)mode : 0,
		.resolve = RESOLVE_BENEATH | RESOLVE_NO_MAGICLINKS,
	};
	long fd = syscall(SYS_openat2, s->rootfd, rel, &how, sizeof(how));

	return fd < 0 ? -errno : (int)fd;
}

static bool may_write(const struct fsrv *s, const char *rel)
{
	size_t l = strlen(RW_PREFIX);

	return s->writable && !strncmp(rel, RW_PREFIX, l) && (rel[l] == 0 || rel[l] == '/');
}

/* Open the parent of rel for *at() calls; *base points at the last component. */
static int open_parent(struct fsrv *s, const char *rel, const char **base)
{
	const char *slash = strrchr(rel, '/');
	char par[PATH_CAP];

	if (!slash) {
		*base = rel;
		return beneath_open(s, ".", O_RDONLY | O_DIRECTORY, 0);
	}
	if ((size_t)(slash - rel) >= sizeof(par))
		return -ENAMETOOLONG;
	memcpy(par, rel, (size_t)(slash - rel));
	par[slash - rel] = 0;
	*base = slash + 1;
	return beneath_open(s, par, O_RDONLY | O_DIRECTORY, 0);
}

static int alloc_slot(struct fsrv *s)
{
	int i;

	for (i = 0; i < FSRV_MAX_OPEN; i++)
		if (!s->open[i].used)
			return i;
	return -1;
}

/* Handles seen by the DSP are slot + 1, so that 0 is never valid. */
static struct fsrv_open *slot_of(struct fsrv *s, uint64_t h)
{
	if (!h || h > FSRV_MAX_OPEN || !s->open[h - 1].used)
		return NULL;
	return &s->open[h - 1];
}

static void slot_close(struct fsrv_open *o)
{
	if (o->dir)
		closedir(o->dir);
	else if (o->fd >= 0)
		close(o->fd);
	memset(o, 0, sizeof(*o));
	o->fd = -1;
}

/* --- argument access --------------------------------------------------- */

static bool word(const struct fr_req *r, unsigned i, uint32_t *v)
{
	size_t off = (size_t)(i + r->skip) * 4;

	if (!r->n_in || r->in[0].len < off + 4)
		return false;
	memcpy(v, r->in[0].p + off, 4);
	return true;
}

static bool word64(const struct fr_req *r, unsigned i, uint64_t *v)
{
	size_t off = (size_t)(i + r->skip) * 4;

	if (!r->n_in || r->in[0].len < off + 8)
		return false;
	memcpy(v, r->in[0].p + off, 8);
	return true;
}

/* NUL-terminated string in input buffer i. */
static const char *str(const struct fr_req *r, unsigned i)
{
	if (i >= r->n_in || !r->in[i].len || r->in[i].p[r->in[i].len - 1] != 0)
		return NULL;
	return (const char *)r->in[i].p;
}

static bool put(struct fr_req *r, unsigned out, size_t off, const void *v, size_t n)
{
	if (out >= r->n_out || !r->out[out].p || r->out[out].len < off + n)
		return false;
	memcpy(r->out[out].p + off, v, n);
	return true;
}

static bool put32(struct fr_req *r, unsigned out, size_t off, uint32_t v)
{
	return put(r, out, off, &v, 4);
}

/* --- apps_std ---------------------------------------------------------- */

/* Returns a slot handle (>0) or a negative errno. */
static int open_file(struct fsrv *s, const char *name, const char *mode)
{
	char rel[PATH_CAP];
	bool wr = false;
	int flags = 0, ret, slot;
	const char *m;

	for (m = mode; *m; m++) {
		if (*m == 'w') {
			wr = true;
			flags |= O_CREAT | O_TRUNC;
		} else if (*m == 'a') {
			wr = true;
			flags |= O_CREAT | O_APPEND;
		} else if (*m == '+') {
			wr = true;
		}
	}

	ret = fsrv_normalize(name, rel, sizeof(rel));
	if (ret)
		return ret;
	if (wr && !may_write(s, rel)) {
		printf("[fs] DENY-WRITE open \"%s\" mode %s\n", name, mode);
		return -EROFS;
	}

	slot = alloc_slot(s);
	if (slot < 0)
		return -EMFILE;
	ret = beneath_open(s, rel, (wr ? O_RDWR : O_RDONLY) | flags, 0644);
	printf("[fs] open \"%s\" mode %s -> %s%s\n", name, mode, rel,
	       ret < 0 ? " FAILED" : "");
	if (ret < 0) {
		printf("[fs]   error: %s\n", strerror(-ret));
		return ret;
	}
	s->open[slot] = (struct fsrv_open){ .used = true, .fd = ret };
	return slot + 1;
}

static uint32_t std_fopen(struct fsrv *s, struct fr_req *r)
{
	const char *name = str(r, 1), *mode = str(r, 2);
	int h;

	if (!name || !mode)
		return AEE_EBADPARM;
	h = open_file(s, name, mode);
	if (h < 0)
		return h == -EROFS ? AEE_EUNSUPPORTED : AEE_EFAILED;
	put32(r, 0, 0, (uint32_t)h);
	return 0;
}

static uint32_t std_fopen_env(struct fsrv *s, struct fr_req *r)
{
	static const struct { const char *var; const char *dirs[4]; } search[] = {
		{ "ADSP_LIBRARY_PATH",
		  { "/vendor/dsp/sdsp", "/vendor/dsp", "/vendor/lib/rfsa/adsp", NULL } },
		{ "ADSP_AVS_CFG_PATH", { "/vendor/etc/acdbdata", NULL } },
	};
	const char *var = str(r, 1), *name = str(r, 3), *mode = str(r, 4);
	int h = -ENOENT;
	size_t i;

	if (!var || !name || !mode)
		return AEE_EBADPARM;

	if (name[0] != '/') {
		for (i = 0; i < sizeof(search) / sizeof(search[0]); i++) {
			unsigned d;

			if (strcmp(var, search[i].var))
				continue;
			for (d = 0; search[i].dirs[d]; d++) {
				char full[PATH_CAP];

				snprintf(full, sizeof(full), "%s/%s", search[i].dirs[d], name);
				h = open_file(s, full, mode);
				if (h > 0)
					goto done;
			}
		}
		if (h == -ENOENT)
			printf("[fs] env %s, relative \"%s\": trying the root\n", var, name);
	}
	h = open_file(s, name, mode);
done:
	if (h < 0)
		return h == -EROFS ? AEE_EUNSUPPORTED : AEE_EFAILED;
	put32(r, 0, 0, (uint32_t)h);
	return 0;
}

static uint32_t std_fclose(struct fsrv *s, struct fr_req *r)
{
	uint32_t h;
	struct fsrv_open *o;

	if (!word(r, 0, &h) || !(o = slot_of(s, h)) || o->is_dir)
		return AEE_EBADPARM;
	slot_close(o);
	return 0;
}

static uint32_t std_fread(struct fsrv *s, struct fr_req *r)
{
	uint32_t h, want;
	struct fsrv_open *o;
	ssize_t n;

	if (!word(r, 0, &h) || !word(r, 1, &want) || r->n_out < 2 || !(o = slot_of(s, h)) || o->is_dir)
		return AEE_EBADPARM;
	if (want > r->out[1].len)
		want = r->out[1].len;
	n = read(o->fd, r->out[1].p, want);
	if (n < 0)
		return AEE_EFAILED;
	o->eof = (uint32_t)n < want;
	put32(r, 0, 0, (uint32_t)n);
	put32(r, 0, 4, o->eof);
	return 0;
}

static uint32_t std_fgets(struct fsrv *s, struct fr_req *r)
{
	uint32_t h, want, got = 0;
	struct fsrv_open *o;

	if (!word(r, 0, &h) || !word(r, 1, &want) || r->n_out < 2 || !(o = slot_of(s, h)) || o->is_dir)
		return AEE_EBADPARM;
	if (want > r->out[1].len)
		want = r->out[1].len;
	while (want && got + 1 < want) {
		char c;
		ssize_t n = read(o->fd, &c, 1);

		if (n < 0)
			return AEE_EFAILED;
		if (n == 0) {
			o->eof = true;
			break;
		}
		r->out[1].p[got++] = (uint8_t)c;
		if (c == '\n')
			break;
	}
	put32(r, 0, 0, o->eof);
	return 0;
}

static uint32_t std_fwrite(struct fsrv *s, struct fr_req *r)
{
	uint32_t h;
	struct fsrv_open *o;
	ssize_t n;

	if (!word(r, 0, &h) || r->n_in < 2 || !(o = slot_of(s, h)) || o->is_dir)
		return AEE_EBADPARM;
	n = r->in[1].len ? write(o->fd, r->in[1].p, r->in[1].len) : 0;
	if (n < 0) {
		/* A read-only descriptor lands here. */
		printf("[fs] write failed: %s\n", strerror(errno));
		return AEE_EFAILED;
	}
	LOG(s, "[fs] write handle %u: %zd bytes\n", h, n);
	put32(r, 0, 0, (uint32_t)n);
	put32(r, 0, 4, 0);
	return 0;
}

static uint32_t std_fseek(struct fsrv *s, struct fr_req *r)
{
	static const int whence_map[] = { SEEK_SET, SEEK_CUR, SEEK_END };
	uint32_t h, pos, whence;
	struct fsrv_open *o;

	if (!word(r, 0, &h) || !word(r, 1, &pos) || !word(r, 2, &whence) || whence > 2 ||
	    !(o = slot_of(s, h)) || o->is_dir)
		return AEE_EBADPARM;
	if (lseek(o->fd, (off_t)(int32_t)pos, whence_map[whence]) < 0)
		return AEE_EFAILED;
	o->eof = false;
	return 0;
}

static uint32_t std_ftell(struct fsrv *s, struct fr_req *r)
{
	uint32_t h;
	struct fsrv_open *o;
	off_t pos;

	if (!word(r, 0, &h) || !(o = slot_of(s, h)) || o->is_dir)
		return AEE_EBADPARM;
	pos = lseek(o->fd, 0, SEEK_CUR);
	if (pos < 0)
		return AEE_EFAILED;
	put32(r, 0, 0, (uint32_t)pos);
	return 0;
}

static uint32_t std_flen(struct fsrv *s, struct fr_req *r)
{
	uint32_t h;
	struct fsrv_open *o;
	struct stat st;
	uint64_t len;

	if (!word(r, 0, &h) || !(o = slot_of(s, h)) || o->is_dir || fstat(o->fd, &st))
		return AEE_EBADPARM;
	len = (uint64_t)st.st_size;
	put(r, 0, 0, &len, 8);
	return 0;
}

static uint32_t std_rewind(struct fsrv *s, struct fr_req *r)
{
	uint32_t h;
	struct fsrv_open *o;

	if (!word(r, 0, &h) || !(o = slot_of(s, h)) || o->is_dir)
		return AEE_EBADPARM;
	if (lseek(o->fd, 0, SEEK_SET) < 0)
		return AEE_EFAILED;
	o->eof = false;
	return 0;
}

static uint32_t std_feof(struct fsrv *s, struct fr_req *r)
{
	uint32_t h;
	struct fsrv_open *o;

	if (!word(r, 0, &h) || !(o = slot_of(s, h)) || o->is_dir)
		return AEE_EBADPARM;
	put32(r, 0, 0, o->eof);
	return 0;
}

static uint32_t std_fsync(struct fsrv *s, struct fr_req *r)
{
	uint32_t h;
	struct fsrv_open *o;

	if (!word(r, 0, &h) || !(o = slot_of(s, h)) || o->is_dir)
		return AEE_EBADPARM;
	fsync(o->fd);
	return 0;
}

static uint32_t std_ftrunc(struct fsrv *s, struct fr_req *r)
{
	uint32_t h, len;
	struct fsrv_open *o;

	if (!word(r, 0, &h) || !word(r, 1, &len) || !(o = slot_of(s, h)) || o->is_dir)
		return AEE_EBADPARM;
	return ftruncate(o->fd, len) ? AEE_EFAILED : 0;
}

static uint32_t std_exists(struct fsrv *s, struct fr_req *r)
{
	const char *name = str(r, 1);
	char rel[PATH_CAP];
	int fd;
	uint8_t yes = 0;

	if (!name)
		return AEE_EBADPARM;
	if (!fsrv_normalize(name, rel, sizeof(rel))) {
		fd = beneath_open(s, rel, O_PATH, 0);
		if (fd >= 0) {
			yes = 1;
			close(fd);
		}
	}
	printf("[fs] exists \"%s\" -> %d\n", name, yes);
	put(r, 0, 0, &yes, 1);
	return 0;
}

/* Shared body of the path-changing calls, all confined to the writable tree. */
enum path_op { OP_REMOVE, OP_MKDIR, OP_RMDIR };

static uint32_t std_pathop(struct fsrv *s, struct fr_req *r, enum path_op op)
{
	const char *name = str(r, 1), *base;
	char rel[PATH_CAP];
	int dfd, ret;

	if (!name)
		return AEE_EBADPARM;
	if (fsrv_normalize(name, rel, sizeof(rel)))
		return AEE_EBADPARM;
	if (!may_write(s, rel)) {
		printf("[fs] DENY-WRITE op %d \"%s\"\n", op, name);
		return AEE_EUNSUPPORTED;
	}
	dfd = open_parent(s, rel, &base);
	if (dfd < 0)
		return AEE_EFAILED;
	if (op == OP_MKDIR)
		ret = mkdirat(dfd, base, 0755);
	else
		ret = unlinkat(dfd, base, op == OP_RMDIR ? AT_REMOVEDIR : 0);
	if (ret)
		ret = errno;
	close(dfd);
	printf("[fs] op %d \"%s\" -> %s\n", op, name, ret ? strerror(ret) : "ok");
	/* Removing what is already gone is what the firmware wants anyway. */
	return (ret && ret != ENOENT && ret != EEXIST) ? AEE_EFAILED : 0;
}

static uint32_t std_frename(struct fsrv *s, struct fr_req *r)
{
	const char *from = str(r, 1), *to = str(r, 2), *fb, *tb;
	char frel[PATH_CAP], trel[PATH_CAP];
	int fd1, fd2, ret;

	if (!from || !to || fsrv_normalize(from, frel, sizeof(frel)) ||
	    fsrv_normalize(to, trel, sizeof(trel)))
		return AEE_EBADPARM;
	if (!may_write(s, frel) || !may_write(s, trel)) {
		printf("[fs] DENY-WRITE rename \"%s\" \"%s\"\n", from, to);
		return AEE_EUNSUPPORTED;
	}
	fd1 = open_parent(s, frel, &fb);
	if (fd1 < 0)
		return AEE_EFAILED;
	fd2 = open_parent(s, trel, &tb);
	if (fd2 < 0) {
		close(fd1);
		return AEE_EFAILED;
	}
	ret = renameat(fd1, fb, fd2, tb) ? errno : 0;
	close(fd1);
	close(fd2);
	printf("[fs] rename \"%s\" -> \"%s\": %s\n", from, to, ret ? strerror(ret) : "ok");
	return ret ? AEE_EFAILED : 0;
}

static uint32_t std_opendir(struct fsrv *s, struct fr_req *r)
{
	const char *name = str(r, 1);
	char rel[PATH_CAP];
	int slot, fd;
	DIR *d;
	uint64_t h;

	if (!name || fsrv_normalize(name, rel, sizeof(rel)))
		return AEE_EBADPARM;
	slot = alloc_slot(s);
	if (slot < 0)
		return AEE_ENOMEMORY;
	fd = beneath_open(s, rel, O_RDONLY | O_DIRECTORY, 0);
	printf("[fs] opendir \"%s\" -> %s%s\n", name, rel, fd < 0 ? " FAILED" : "");
	if (fd < 0)
		return AEE_EFAILED;
	d = fdopendir(fd);
	if (!d) {
		close(fd);
		return AEE_EFAILED;
	}
	s->open[slot] = (struct fsrv_open){ .used = true, .is_dir = true, .fd = -1, .dir = d };
	h = (uint64_t)slot + 1;
	put(r, 0, 0, &h, 8);
	return 0;
}

static uint32_t std_closedir(struct fsrv *s, struct fr_req *r)
{
	uint64_t h;
	struct fsrv_open *o;

	if (!word64(r, 0, &h) || !(o = slot_of(s, h)) || !o->is_dir)
		return AEE_EBADPARM;
	slot_close(o);
	return 0;
}

/* Output: u32 inode, char name[256], u32 eof. */
static uint32_t std_readdir(struct fsrv *s, struct fr_req *r)
{
	uint64_t h;
	struct fsrv_open *o;
	struct dirent *e;

	if (!word64(r, 0, &h) || !(o = slot_of(s, h)) || !o->is_dir || !r->n_out || r->out[0].len < 264)
		return AEE_EBADPARM;
	do {
		errno = 0;
		e = readdir(o->dir);
	} while (e && (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")));
	if (!e) {
		if (errno)
			return AEE_EFAILED;
		put32(r, 0, 260, 1);
		return 0;
	}
	put32(r, 0, 0, (uint32_t)e->d_ino);
	snprintf((char *)r->out[0].p + 4, 256, "%s", e->d_name);
	put32(r, 0, 260, 0);
	return 0;
}

/* Output (96 bytes): tsz, dev, ino (u64); mode, nlink (u32); rdev, size, then
 * atime, atimensec, mtime, mtimensec, ctime, ctimensec (u64). */
static uint32_t std_stat(struct fsrv *s, struct fr_req *r)
{
	const char *name = str(r, 1);
	char rel[PATH_CAP];
	struct stat st;
	int fd;
	uint64_t q[12];
	uint32_t w[2];

	if (!name || fsrv_normalize(name, rel, sizeof(rel)) || !r->n_out || r->out[0].len < 96)
		return AEE_EBADPARM;
	fd = beneath_open(s, rel, O_PATH, 0);
	printf("[fs] stat \"%s\" -> %s%s\n", name, rel, fd < 0 ? " FAILED" : "");
	if (fd < 0)
		return AEE_EFAILED;
	if (fstat(fd, &st)) {
		close(fd);
		return AEE_EFAILED;
	}
	close(fd);
	q[0] = 0;
	q[1] = st.st_dev;
	q[2] = st.st_ino;
	w[0] = st.st_mode;
	w[1] = (uint32_t)st.st_nlink;
	q[3] = st.st_rdev;
	q[4] = (uint64_t)st.st_size;
	q[5] = (uint64_t)st.st_atim.tv_sec;
	q[6] = (uint64_t)st.st_atim.tv_nsec;
	q[7] = (uint64_t)st.st_mtim.tv_sec;
	q[8] = (uint64_t)st.st_mtim.tv_nsec;
	q[9] = (uint64_t)st.st_ctim.tv_sec;
	q[10] = (uint64_t)st.st_ctim.tv_nsec;
	put(r, 0, 0, q, 24);
	put(r, 0, 24, w, 8);
	put(r, 0, 32, q + 3, 64);
	return 0;
}

static uint32_t apps_std(struct fsrv *s, struct fr_req *r)
{
	switch (r->method) {
	case 0:  return std_fopen(s, r);
	case 2:  return 0; /* fflush: writes are unbuffered here */
	case 3:  return std_fclose(s, r);
	case 4:  return std_fread(s, r);
	case 5:  return std_fwrite(s, r);
	case 8:  return std_ftell(s, r);
	case 9:  return std_fseek(s, r);
	case 10: return std_flen(s, r);
	case 11: return std_rewind(s, r);
	case 12: return std_feof(s, r);
	case 13: return 0; /* ferror: output stays zero */
	case 14: return 0; /* clearerr */
	case 15: {
		const char *m = str(r, 1);

		printf("[dsp] %s\n", m ? m : "(bad string)");
		return 0;
	}
	case 19: return std_fopen_env(s, r);
	case 20: return std_fgets(s, r);
	case 22: return std_exists(s, r);
	case 23: return std_fsync(s, r);
	case 24: return std_pathop(s, r, OP_REMOVE);
	case 26: return std_opendir(s, r);
	case 27: return std_closedir(s, r);
	case 28: return std_readdir(s, r);
	case 29: return std_pathop(s, r, OP_MKDIR);
	case 30: return std_pathop(s, r, OP_RMDIR);
	case 31: return std_stat(s, r);
	case 32: return std_ftrunc(s, r);
	case 33: return std_frename(s, r);
	default: return AEE_EUNSUPPORTED;
	}
}

/* --- apps_mem ---------------------------------------------------------- */

/* Primary input: heapid, lflags, rflags, pad, vin (u64), len (u64). */
static uint32_t mem_map64(struct fsrv *s, struct fr_req *r)
{
	uint32_t rflags;
	uint64_t len, vapps = 0, vadsp;
	struct fastrpc_req_mmap m;

	if (!word(r, 2, &rflags) || !word64(r, 6, &len))
		return AEE_EBADPARM;
	if (!(rflags & ADSP_MMAP_ADD_PAGES)) {
		printf("[mem] unsupported map flags 0x%x\n", rflags);
		return AEE_EUNSUPPORTED;
	}
	memset(&m, 0, sizeof(m));
	m.fd = -1;
	m.flags = rflags;
	m.size = len;
	if (s->rpc_fd < 0 || ioctl(s->rpc_fd, FASTRPC_IOCTL_MMAP, &m)) {
		printf("[mem] map of %llu bytes failed: %s\n", (unsigned long long)len, strerror(errno));
		return AEE_EFAILED;
	}
	vadsp = m.vaddrout;
	printf("[mem] mapped %llu bytes at 0x%llx\n", (unsigned long long)len, (unsigned long long)vadsp);
	put(r, 0, 0, &vapps, 8);
	put(r, 0, 8, &vadsp, 8);
	return 0;
}

static uint32_t mem_unmap64(struct fsrv *s, struct fr_req *r)
{
	struct fastrpc_req_munmap m;
	uint64_t addr, len;

	if (!word64(r, 0, &addr) || !word64(r, 2, &len))
		return AEE_EBADPARM;
	m.vaddrout = addr;
	m.size = len;
	if (s->rpc_fd < 0 || ioctl(s->rpc_fd, FASTRPC_IOCTL_MUNMAP, &m))
		return AEE_EFAILED;
	return 0;
}

/* --- remotectl, sns_registry ------------------------------------------- */

/* open: in {name_len, err_len}, name; out {handle, dlerr}, err. */
static uint32_t ctl_open(struct fsrv *s, struct fr_req *r)
{
	static const struct { const char *name; uint32_t h; } ifaces[] = {
		{ "apps_std", FSRV_H_APPS_STD },
		{ "apps_mem", FSRV_H_APPS_MEM },
		{ "sns_registry", FSRV_H_SNS_REGISTRY },
	};
	const char *name = str(r, 1);
	size_t i;

	if (!name)
		return AEE_EBADPARM;
	for (i = 0; i < sizeof(ifaces) / sizeof(ifaces[0]); i++) {
		if (!strcmp(name, ifaces[i].name)) {
			LOG(s, "[ctl] open interface \"%s\" -> %u\n", name, ifaces[i].h);
			put32(r, 0, 0, ifaces[i].h);
			put32(r, 0, 4, 0);
			return 0;
		}
	}
	printf("[ctl] UNKNOWN interface \"%s\"\n", name);
	put32(r, 0, 4, (uint32_t)-5);
	return (uint32_t)-5;
}

/* in {name_len, value_len}, name; the only output buffer takes the value. */
static uint32_t sns_get_property(struct fsrv *s, struct fr_req *r)
{
	const char *name = str(r, 1), *val = "";
	unsigned i;

	if (!name || !r->n_out)
		return AEE_EBADPARM;
	for (i = 0; i < s->n_props; i++)
		if (!strcmp(s->props[i].k, name))
			val = s->props[i].v;
	printf("[sns_registry] get_property \"%s\" -> \"%s\"\n", name, val);
	if (r->out[0].len)
		snprintf((char *)r->out[0].p, r->out[0].len, "%s", val);
	return 0;
}

/* --- dispatch ---------------------------------------------------------- */

static void hexdump(const struct fr_buf *b)
{
	uint32_t i, n = b->len > 64 ? 64 : b->len;

	for (i = 0; i < n; i++)
		printf("%02x", b->p[i]);
	printf(b->len > 64 ? "...\n" : "\n");
}

uint32_t fsrv_dispatch(struct fsrv *s, struct fr_req *r)
{
	uint32_t res = AEE_EUNSUPPORTED;
	unsigned i;

	if (FR_SC_HANDLES(r->sc)) {
		res = AEE_EBADPARM;
	} else {
		switch (r->handle) {
		case FSRV_H_REMOTECTL:
			if (r->method == 0)
				res = ctl_open(s, r);
			else if (r->method == 1)
				res = 0; /* close: interfaces are static */
			break;
		case FSRV_H_APPS_STD:
			res = apps_std(s, r);
			break;
		case FSRV_H_APPS_MEM:
			if (r->method == 2)
				res = mem_map64(s, r);
			else if (r->method == 3)
				res = mem_unmap64(s, r);
			break;
		case FSRV_H_SNS_REGISTRY:
			if (r->method == 0)
				res = sns_get_property(s, r);
			break;
		default:
			break;
		}
	}

	if (res == AEE_EUNSUPPORTED || res == AEE_EBADPARM || s->verbose) {
		printf("[rpc] handle %u method %u in %u out %u -> %d\n", r->handle, r->method,
		       r->n_in, r->n_out, (int)res);
		if (res == AEE_EUNSUPPORTED || res == AEE_EBADPARM)
			for (i = 0; i < r->n_in; i++) {
				printf("[rpc]   in[%u] %u bytes: ", i, r->in[i].len);
				hexdump(&r->in[i]);
			}
	}
	return res;
}

/* --- setup ------------------------------------------------------------- */

int fsrv_init(struct fsrv *s, const char *dir, int rpc_fd, bool writable, bool verbose)
{
	unsigned i;

	memset(s, 0, sizeof(*s));
	for (i = 0; i < FSRV_MAX_OPEN; i++)
		s->open[i].fd = -1;
	s->rpc_fd = rpc_fd;
	s->writable = writable;
	s->verbose = verbose;
	s->rootfd = open(dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	return s->rootfd < 0 ? -errno : 0;
}

void fsrv_fini(struct fsrv *s)
{
	unsigned i;

	for (i = 0; i < FSRV_MAX_OPEN; i++)
		if (s->open[i].used)
			slot_close(&s->open[i]);
	if (s->rootfd >= 0)
		close(s->rootfd);
	s->rootfd = -1;
}

int fsrv_add_prop(struct fsrv *s, const char *k, const char *v)
{
	unsigned i;

	for (i = 0; i < s->n_props; i++) {
		if (!strcmp(s->props[i].k, k)) {
			s->props[i].v = v;
			return 0;
		}
	}
	if (s->n_props == FSRV_MAX_PROPS)
		return -1;
	s->props[s->n_props].k = k;
	s->props[s->n_props].v = v;
	s->n_props++;
	return 0;
}

void fsrv_load_props(struct fsrv *s, const char *dir)
{
	char path[PATH_CAP], line[256];
	FILE *f;

	snprintf(path, sizeof(path), "%s/props", dir);
	f = fopen(path, "r");
	if (!f)
		return;
	while (fgets(line, sizeof(line), f)) {
		char *eq = strchr(line, '=');
		char *k, *v;

		if (!eq || line[0] == '#')
			continue;
		line[strcspn(line, "\r\n")] = 0;
		*eq = 0;
		k = strdup(line);
		v = strdup(eq + 1);
		if (!k || !v || fsrv_add_prop(s, k, v)) {
			free(k);
			free(v);
		}
	}
	fclose(f);
}

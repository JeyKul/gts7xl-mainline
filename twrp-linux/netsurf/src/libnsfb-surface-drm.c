/*
 * DRM/KMS surface with Linux input events, for a phone/tablet recovery image:
 * draws into a dumb buffer (the panel may be command mode, so every update is
 * pushed with DIRTYFB) and reads touchscreens, mice and keyboards from evdev.
 * A finger taps to click and drags to scroll; a mouse moves the pointer.
 *
 * Licenced under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <libdrm/drm.h>
#include <libdrm/drm_mode.h>
#include <linux/input.h>

#include "libnsfb.h"
#include "libnsfb_event.h"
#include "libnsfb_plot.h"
#include "libnsfb_plot_util.h"
#include "nsfb.h"
#include "surface.h"
#include "plot.h"
#include "cursor.h"

#define MAXDEV 16
#define SCROLL_STEP 40		/* finger travel per wheel step, pixels */
#define TAP_MOVE 30		/* a finger moving less than this is a tap */

enum devkind { DEV_TOUCH, DEV_MOUSE, DEV_KEYS };

struct dev {
	int fd;
	enum devkind kind;
	int xmin, xmax, ymin, ymax;
	bool portrait;		/* raw axes portrait on a landscape screen */
};

struct drm_priv {
	int fd;
	uint32_t fb_id, handle;
	uint8_t *map;
	size_t size;
	struct dev dev[MAXDEV];
	int ndev;
	/* pending events, so one input read can produce several */
	nsfb_event_t queue[16];
	int qhead, qtail;
	/* touch state */
	int tx, ty, sx, sy, lasty, moved;
	bool down, scrolling;
	/* mouse pointer */
	int px, py;
};

static bool bit(const unsigned long *a, int n)
{
	return a[n / (8 * sizeof(long))] & (1UL << (n % (8 * sizeof(long))));
}

static void push(struct drm_priv *p, enum nsfb_event_type_e type, int v, int x, int y)
{
	nsfb_event_t *e = &p->queue[p->qtail];
	int next = (p->qtail + 1) % 16;

	if (next == p->qhead)
		return;
	e->type = type;
	if (type == NSFB_EVENT_MOVE_ABSOLUTE) {
		e->value.vector.x = x;
		e->value.vector.y = y;
		e->value.vector.z = 0;
	} else {
		e->value.keycode = v;
	}
	p->qtail = next;
}

static int card_mode(struct drm_priv *p, int fd, nsfb_t *nsfb, bool set)
{
	struct drm_mode_card_res res = { 0 };
	uint32_t conns[8], crtcs[8], encs[8];

	if (ioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &res) || !res.count_connectors ||
	    res.count_connectors > 8 || res.count_crtcs > 8 || res.count_encoders > 8)
		return -1;
	res.connector_id_ptr = (uintptr_t)conns;
	res.crtc_id_ptr = (uintptr_t)crtcs;
	res.encoder_id_ptr = (uintptr_t)encs;
	res.count_fbs = 0;
	if (ioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &res))
		return -1;
	for (unsigned i = 0; i < res.count_connectors; i++) {
		struct drm_mode_get_connector c = { .connector_id = conns[i] };
		struct drm_mode_modeinfo modes[8], *m;
		unsigned nm;

		if (ioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, &c) || !c.count_modes || c.connection != 1)
			continue;
		nm = c.count_modes > 8 ? 8 : c.count_modes;
		c.count_modes = nm;
		c.modes_ptr = (uintptr_t)modes;
		c.count_props = c.count_encoders = 0;
		if (ioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, &c) || c.count_modes < nm)
			continue;
		m = &modes[0];
		for (unsigned j = 0; j < nm; j++)
			if (modes[j].vrefresh == 60)
				m = &modes[j];
		if (!set) {
			nsfb->width = m->hdisplay;
			nsfb->height = m->vdisplay;
			return 0;
		}
		struct drm_mode_create_dumb cr = { .height = m->vdisplay, .width = m->hdisplay, .bpp = 32 };
		if (ioctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &cr))
			return -1;
		struct drm_mode_fb_cmd fc = { .width = cr.width, .height = cr.height, .pitch = cr.pitch,
					      .bpp = 32, .depth = 24, .handle = cr.handle };
		struct drm_mode_map_dumb mp = { .handle = cr.handle };
		if (ioctl(fd, DRM_IOCTL_MODE_ADDFB, &fc) || ioctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &mp))
			return -1;
		p->map = mmap(NULL, cr.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, mp.offset);
		if (p->map == MAP_FAILED)
			return -1;
		memset(p->map, 0, cr.size);
		struct drm_mode_crtc sc = { .set_connectors_ptr = (uintptr_t)&conns[i], .count_connectors = 1,
					    .crtc_id = crtcs[0], .fb_id = fc.fb_id, .mode_valid = 1, .mode = *m };
		if (ioctl(fd, DRM_IOCTL_MODE_SETCRTC, &sc))
			return -1;
		p->fb_id = fc.fb_id;
		p->handle = cr.handle;
		p->size = cr.size;
		nsfb->width = cr.width;
		nsfb->height = cr.height;
		nsfb->ptr = p->map;
		nsfb->linelen = cr.pitch;
		return 0;
	}
	return -1;
}

/* The first card with a connected display; GPU-only cards have no connectors */
static int open_card(struct drm_priv *p, nsfb_t *nsfb, bool set)
{
	for (int i = 0; i < 8; i++) {
		char path[32];
		snprintf(path, sizeof(path), "/dev/dri/card%d", i);
		int fd = open(path, O_RDWR | O_CLOEXEC);
		if (fd < 0)
			continue;
		if (set)
			ioctl(fd, DRM_IOCTL_SET_MASTER, 0);
		if (!card_mode(p, fd, nsfb, set)) {
			if (set)
				p->fd = fd;
			else
				close(fd);
			return 0;
		}
		close(fd);
	}
	return -1;
}

static void open_inputs(struct drm_priv *p, nsfb_t *nsfb)
{
	for (int i = 0; i < 32 && p->ndev < MAXDEV; i++) {
		char path[32];
		unsigned long ev[1] = { 0 }, keys[KEY_MAX / (8 * sizeof(long)) + 1] = { 0 };
		unsigned long abs[1] = { 0 }, rel[1] = { 0 }, props[1] = { 0 };
		struct dev *d = &p->dev[p->ndev];

		snprintf(path, sizeof(path), "/dev/input/event%d", i);
		d->fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
		if (d->fd < 0)
			continue;
		ioctl(d->fd, EVIOCGBIT(0, sizeof(ev)), ev);
		ioctl(d->fd, EVIOCGBIT(EV_KEY, sizeof(keys)), keys);
		ioctl(d->fd, EVIOCGBIT(EV_ABS, sizeof(abs)), abs);
		ioctl(d->fd, EVIOCGBIT(EV_REL, sizeof(rel)), rel);
		ioctl(d->fd, EVIOCGPROP(sizeof(props)), props);
		if (bit(abs, ABS_MT_POSITION_X) && bit(props, INPUT_PROP_DIRECT)) {
			struct input_absinfo ax, ay;
			ioctl(d->fd, EVIOCGABS(ABS_MT_POSITION_X), &ax);
			ioctl(d->fd, EVIOCGABS(ABS_MT_POSITION_Y), &ay);
			d->kind = DEV_TOUCH;
			d->xmin = ax.minimum; d->xmax = ax.maximum;
			d->ymin = ay.minimum; d->ymax = ay.maximum;
			d->portrait = (d->xmax - d->xmin < d->ymax - d->ymin) != (nsfb->width < nsfb->height);
		} else if (bit(rel, REL_X) && bit(keys, BTN_LEFT)) {
			d->kind = DEV_MOUSE;
		} else if ((bit(keys, KEY_A) && bit(keys, KEY_ENTER)) || bit(keys, KEY_POWER)) {
			d->kind = DEV_KEYS;
		} else {
			close(d->fd);
			continue;
		}
		p->ndev++;
	}
}

static int keymap(int code)
{
	static const char row1[] = "1234567890-=", row2[] = "qwertyuiop[]", row3[] = "asdfghjkl;'`", row4[] = "zxcvbnm,./";
	if (code >= KEY_1 && code <= KEY_EQUAL) return row1[code - KEY_1];
	if (code >= KEY_Q && code <= KEY_RIGHTBRACE) return row2[code - KEY_Q];
	if (code >= KEY_A && code <= KEY_GRAVE) return row3[code - KEY_A];
	if (code >= KEY_Z && code <= KEY_SLASH) return row4[code - KEY_Z];
	switch (code) {
	case KEY_BACKSLASH: return '\\';
	case KEY_SPACE: return NSFB_KEY_SPACE;
	case KEY_ENTER: case KEY_KPENTER: return NSFB_KEY_RETURN;
	case KEY_BACKSPACE: return NSFB_KEY_BACKSPACE;
	case KEY_TAB: return NSFB_KEY_TAB;
	case KEY_ESC: return NSFB_KEY_ESCAPE;
	case KEY_DELETE: return NSFB_KEY_DELETE;
	case KEY_UP: return NSFB_KEY_UP;
	case KEY_DOWN: return NSFB_KEY_DOWN;
	case KEY_LEFT: return NSFB_KEY_LEFT;
	case KEY_RIGHT: return NSFB_KEY_RIGHT;
	case KEY_PAGEUP: return NSFB_KEY_PAGEUP;
	case KEY_PAGEDOWN: return NSFB_KEY_PAGEDOWN;
	case KEY_HOME: return NSFB_KEY_HOME;
	case KEY_END: return NSFB_KEY_END;
	case KEY_LEFTSHIFT: return NSFB_KEY_LSHIFT;
	case KEY_RIGHTSHIFT: return NSFB_KEY_RSHIFT;
	case KEY_LEFTCTRL: return NSFB_KEY_LCTRL;
	case KEY_RIGHTCTRL: return NSFB_KEY_RCTRL;
	case KEY_LEFTALT: return NSFB_KEY_LALT;
	case KEY_RIGHTALT: return NSFB_KEY_RALT;
	}
	return NSFB_KEY_UNKNOWN;
}

static void touch_event(struct drm_priv *p, nsfb_t *nsfb, struct dev *d, struct input_event *e)
{
	if (e->type == EV_ABS && (e->code == ABS_MT_POSITION_X || e->code == ABS_MT_POSITION_Y)) {
		bool isx = e->code == ABS_MT_POSITION_X;
		/* Portrait raw axes on a landscape screen: the screen's x is the
		 * raw y, its y the mirrored raw x (as the Linux DT maps it) */
		if (d->portrait) {
			if (isx)
				p->ty = (long)(d->xmax - e->value) * nsfb->height / (d->xmax - d->xmin + 1);
			else
				p->tx = (long)(e->value - d->ymin) * nsfb->width / (d->ymax - d->ymin + 1);
		} else if (isx) {
			p->tx = (long)(e->value - d->xmin) * nsfb->width / (d->xmax - d->xmin + 1);
		} else {
			p->ty = (long)(e->value - d->ymin) * nsfb->height / (d->ymax - d->ymin + 1);
		}
	} else if (e->type == EV_KEY && e->code == BTN_TOUCH) {
		if (e->value) {
			p->down = true;
			p->scrolling = false;
			p->moved = 0;
			p->sx = p->tx; p->sy = p->ty; p->lasty = p->ty;
		} else if (p->down) {
			p->down = false;
			if (!p->scrolling) {
				push(p, NSFB_EVENT_MOVE_ABSOLUTE, 0, p->sx, p->sy);
				push(p, NSFB_EVENT_KEY_DOWN, NSFB_KEY_MOUSE_1, 0, 0);
				push(p, NSFB_EVENT_KEY_UP, NSFB_KEY_MOUSE_1, 0, 0);
			}
		}
	} else if (e->type == EV_SYN && e->code == SYN_REPORT && p->down) {
		p->moved = abs(p->tx - p->sx) + abs(p->ty - p->sy);
		if (p->moved > TAP_MOVE) {
			if (!p->scrolling)
				/* wheel events go to what is under the finger */
				push(p, NSFB_EVENT_MOVE_ABSOLUTE, 0, p->sx, p->sy);
			p->scrolling = true;
		}
		while (p->scrolling && p->ty - p->lasty >= SCROLL_STEP) {
			push(p, NSFB_EVENT_KEY_DOWN, NSFB_KEY_MOUSE_4, 0, 0);
			p->lasty += SCROLL_STEP;
		}
		while (p->scrolling && p->lasty - p->ty >= SCROLL_STEP) {
			push(p, NSFB_EVENT_KEY_DOWN, NSFB_KEY_MOUSE_5, 0, 0);
			p->lasty -= SCROLL_STEP;
		}
	}
}

static void mouse_event(struct drm_priv *p, nsfb_t *nsfb, struct input_event *e)
{
	if (e->type == EV_REL && (e->code == REL_X || e->code == REL_Y)) {
		if (e->code == REL_X)
			p->px += e->value;
		else
			p->py += e->value;
		p->px = p->px < 0 ? 0 : p->px >= nsfb->width ? nsfb->width - 1 : p->px;
		p->py = p->py < 0 ? 0 : p->py >= nsfb->height ? nsfb->height - 1 : p->py;
		push(p, NSFB_EVENT_MOVE_ABSOLUTE, 0, p->px, p->py);
	} else if (e->type == EV_REL && e->code == REL_WHEEL) {
		push(p, NSFB_EVENT_KEY_DOWN, e->value > 0 ? NSFB_KEY_MOUSE_4 : NSFB_KEY_MOUSE_5, 0, 0);
	} else if (e->type == EV_KEY && (e->code == BTN_LEFT || e->code == BTN_RIGHT || e->code == BTN_MIDDLE)) {
		int k = e->code == BTN_LEFT ? NSFB_KEY_MOUSE_1 : e->code == BTN_MIDDLE ? NSFB_KEY_MOUSE_2 : NSFB_KEY_MOUSE_3;
		push(p, e->value ? NSFB_EVENT_KEY_DOWN : NSFB_EVENT_KEY_UP, k, 0, 0);
	}
}

static int drm_defaults(nsfb_t *nsfb)
{
	struct drm_priv probe = { 0 };

	nsfb->format = NSFB_FMT_XRGB8888;
	if (open_card(&probe, nsfb, false)) {
		nsfb->width = 800;
		nsfb->height = 600;
	}
	select_plotters(nsfb);
	return 0;
}

static int drm_initialise(nsfb_t *nsfb)
{
	struct drm_priv *p = calloc(1, sizeof(*p));

	if (!p)
		return -1;
	p->fd = -1;
	if (open_card(p, nsfb, true)) {
		fprintf(stderr, "drm surface: no display\n");
		free(p);
		return -1;
	}
	nsfb->format = NSFB_FMT_XRGB8888;
	select_plotters(nsfb);
	open_inputs(p, nsfb);
	p->px = nsfb->width / 2;
	p->py = nsfb->height / 2;
	nsfb->surface_priv = p;
	return 0;
}

static int drm_finalise(nsfb_t *nsfb)
{
	struct drm_priv *p = nsfb->surface_priv;

	if (!p)
		return 0;
	for (int i = 0; i < p->ndev; i++)
		close(p->dev[i].fd);
	munmap(p->map, p->size);
	struct drm_mode_destroy_dumb dd = { .handle = p->handle };
	ioctl(p->fd, DRM_IOCTL_MODE_RMFB, &p->fb_id);
	ioctl(p->fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dd);
	ioctl(p->fd, DRM_IOCTL_DROP_MASTER, 0);
	close(p->fd);
	free(p);
	nsfb->surface_priv = NULL;
	return 0;
}

static int drm_geometry(nsfb_t *nsfb, int width, int height, enum nsfb_format_e format)
{
	(void)width; (void)height; (void)format;
	/* The panel's mode decides the size */
	if (nsfb->surface_priv == NULL) {
		if (width > 0) nsfb->width = width;
		if (height > 0) nsfb->height = height;
	}
	nsfb->format = NSFB_FMT_XRGB8888;
	select_plotters(nsfb);
	return 0;
}

static bool drm_input(nsfb_t *nsfb, nsfb_event_t *event, int timeout)
{
	struct drm_priv *p = nsfb->surface_priv;
	struct pollfd pfd[MAXDEV];

	if (!p)
		return false;
	for (;;) {
		if (p->qhead != p->qtail) {
			*event = p->queue[p->qhead];
			p->qhead = (p->qhead + 1) % 16;
			return true;
		}
		for (int i = 0; i < p->ndev; i++) {
			pfd[i].fd = p->dev[i].fd;
			pfd[i].events = POLLIN;
		}
		int r = poll(pfd, p->ndev, timeout < 0 ? -1 : timeout);
		if (r <= 0) {
			if (timeout != 0) {
				event->type = NSFB_EVENT_CONTROL;
				event->value.controlcode = NSFB_CONTROL_TIMEOUT;
				return true;
			}
			return false;
		}
		for (int i = 0; i < p->ndev; i++) {
			struct input_event e;
			if (!(pfd[i].revents & POLLIN))
				continue;
			while (read(p->dev[i].fd, &e, sizeof(e)) == sizeof(e)) {
				struct dev *d = &p->dev[i];
				if (d->kind == DEV_TOUCH)
					touch_event(p, nsfb, d, &e);
				else if (d->kind == DEV_MOUSE)
					mouse_event(p, nsfb, &e);
				else if (e.type == EV_KEY && e.code == KEY_POWER && e.value == 1) {
					/* the power key leaves the browser */
					nsfb_event_t *q = &p->queue[p->qtail];
					q->type = NSFB_EVENT_CONTROL;
					q->value.controlcode = NSFB_CONTROL_QUIT;
					p->qtail = (p->qtail + 1) % 16;
				} else if (e.type == EV_KEY && e.value != 2) {
					int k = keymap(e.code);
					if (k != NSFB_KEY_UNKNOWN)
						push(p, e.value ? NSFB_EVENT_KEY_DOWN : NSFB_EVENT_KEY_UP, k, 0, 0);
				}
			}
		}
		/* nothing usable came in: only wait once */
		if (p->qhead == p->qtail && timeout >= 0)
			timeout = 0;
	}
}

static int drm_claim(nsfb_t *nsfb, nsfb_bbox_t *box)
{
	struct nsfb_cursor_s *cursor = nsfb->cursor;

	if (cursor && cursor->plotted && nsfb_plot_bbox_intersect(box, &cursor->loc))
		nsfb_cursor_clear(nsfb, cursor);
	return 0;
}

static void dirty(nsfb_t *nsfb, nsfb_bbox_t *box)
{
	struct drm_priv *p = nsfb->surface_priv;
	struct drm_clip_rect clip = { box->x0 < 0 ? 0 : box->x0, box->y0 < 0 ? 0 : box->y0,
				      box->x1 > nsfb->width ? nsfb->width : box->x1,
				      box->y1 > nsfb->height ? nsfb->height : box->y1 };
	struct drm_mode_fb_dirty_cmd d = { .fb_id = p->fb_id, .num_clips = 1, .clips_ptr = (uintptr_t)&clip };

	if (clip.x2 > clip.x1 && clip.y2 > clip.y1)
		ioctl(p->fd, DRM_IOCTL_MODE_DIRTYFB, &d);
}

static int drm_cursor(nsfb_t *nsfb, struct nsfb_cursor_s *cursor)
{
	nsfb_bbox_t redraw;
	nsfb_bbox_t fbarea = { 0, 0, nsfb->width, nsfb->height };

	if (cursor && cursor->plotted) {
		nsfb_bbox_t shift = cursor->loc;
		shift.x0 -= cursor->hotspot_x; shift.y0 -= cursor->hotspot_y;
		shift.x1 -= cursor->hotspot_x; shift.y1 -= cursor->hotspot_y;
		nsfb_plot_add_rect(&cursor->savloc, &shift, &redraw);
		nsfb_plot_clip(&fbarea, &redraw);
		nsfb_cursor_clear(nsfb, cursor);
		nsfb_cursor_plot(nsfb, cursor);
		dirty(nsfb, &redraw);
	}
	return true;
}

static int drm_update(nsfb_t *nsfb, nsfb_bbox_t *box)
{
	struct nsfb_cursor_s *cursor = nsfb->cursor;

	if (cursor && !cursor->plotted)
		nsfb_cursor_plot(nsfb, cursor);
	dirty(nsfb, box);
	return 0;
}

const nsfb_surface_rtns_t drm_rtns = {
	.defaults = drm_defaults,
	.initialise = drm_initialise,
	.finalise = drm_finalise,
	.input = drm_input,
	.claim = drm_claim,
	.update = drm_update,
	.cursor = drm_cursor,
	.geometry = drm_geometry,
};

NSFB_SURFACE_DEF(drm, NSFB_SURFACE_LINUX, &drm_rtns)

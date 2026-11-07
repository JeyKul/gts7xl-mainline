// SPDX-License-Identifier: MIT
/*
 * Stage 1 of the Tab S7+ installer: choose a Wi-Fi network by touch.
 * Draws on the firmware framebuffer, reads the touchscreen, drives wpa_supplicant.
 */
#include <dirent.h>
#include <fcntl.h>
#include <drm/drm.h>
#include <drm/drm_mode.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "font8x16.h"

#define SCALE 3
#define CW (8 * SCALE)
#define CH (16 * SCALE)
#define BG 0x101820
#define CARD 0x1b2a35
#define FG 0xf0f0f0
#define BTN 0x2f4858
#define ACC 0x1f8a70
#define WARN 0xd05050
#define MAXNET 8
#define IFACE "wlan0"

static unsigned char *fb;
static int fbw, fbh, stride;
static int touch_fd = -1, ax_min, ax_max, ay_min, ay_max;
static char status[160];

struct btn { int x, y, w, h; };
static int hit(const struct btn *b, int x, int y)
{
	return x >= b->x && x < b->x + b->w && y >= b->y && y < b->y + b->h;
}

static int fb_size;
static int drm_fd = -1;
static unsigned char *scanout;
static unsigned int fb_id;

/* Drawing happens in private memory; only finished pictures reach the screen */
static void flush_frame(void)
{
	struct drm_mode_fb_dirty_cmd d = { .fb_id = fb_id };

	ioctl(drm_fd, DRM_IOCTL_MODE_DIRTYFB, &d);
}

static void present(void)
{
	memcpy(scanout, fb, fb_size);
	flush_frame();
}

static void rect(int x, int y, int w, int h, unsigned c)
{
	for (int j = y; j < y + h && j < fbh; j++) {
		unsigned *p = (unsigned *)(fb + j * stride);
		for (int i = x; i < x + w && i < fbw; i++)
			p[i] = c;
	}
}

static void text(int x, int y, const char *s, unsigned c)
{
	for (; *s; s++, x += CW) {
		const unsigned char *g = font_8x16 + (unsigned char)*s * 16;
		for (int r = 0; r < 16; r++)
			for (int b = 0; b < 8; b++)
				if (g[r] & (0x80 >> b))
					rect(x + b * SCALE, y + r * SCALE, SCALE, SCALE, c);
	}
}

/* Filled rectangle with round corners, drawn against the background colour */
static void rrect(int x, int y, int w, int h, int r, unsigned c)
{
	if (r > h / 2) r = h / 2;
	rect(x, y + r, w, h - 2 * r, c);
	for (int j = 0; j < r; j++) {
		int dy = r - j, in = 0;
		while ((in * in + dy * dy) < r * r) in++;
		int cut = r - in;
		rect(x + cut, y + j, w - 2 * cut, 1, c);
		rect(x + cut, y + h - 1 - j, w - 2 * cut, 1, c);
	}
}

static void button(const struct btn *b, const char *s, unsigned c)
{
	rrect(b->x, b->y, b->w, b->h, 28, c);
	int len = strlen(s);
	text(b->x + (b->w - len * CW) / 2, b->y + (b->h - CH) / 2, s, FG);
}

static int run(char *const argv[])
{
	pid_t p = fork();
	int st = -1;
	if (!p) {
		int n = open("/dev/null", O_RDWR);
		dup2(n, 0); dup2(n, 1); dup2(n, 2);
		execvp(argv[0], argv);
		_exit(127);
	}
	waitpid(p, &st, 0);
	return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

/* Run a command and keep its output, with no shell involved */
static int capture(char *const argv[], char *out, int max)
{
	int fd[2];
	if (pipe(fd))
		return -1;
	pid_t p = fork();
	if (!p) {
		dup2(fd[1], 1); close(fd[0]); close(fd[1]);
		execvp(argv[0], argv);
		_exit(127);
	}
	close(fd[1]);
	int n = 0, r;
	while (n < max - 1 && (r = read(fd[0], out + n, max - 1 - n)) > 0)
		n += r;
	out[n] = 0;
	close(fd[0]);
	waitpid(p, NULL, 0);
	return n;
}

/* Drive the panel through DRM, the same path a desktop uses */
static int setup_card(const char *path)
{
	struct drm_mode_card_res res = { 0 };
	unsigned int conns[8], crtcs[8], encs[8];
	/* Child processes must not keep the screen, or the next program cannot take it */
	int fd = open(path, O_RDWR | O_CLOEXEC);

	if (fd < 0 || ioctl(fd, DRM_IOCTL_SET_MASTER, 0) || ioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &res) ||
	    !res.count_connectors || res.count_connectors > 8 || res.count_crtcs > 8 || res.count_encoders > 8)
		goto fail;
	res.connector_id_ptr = (unsigned long)conns;
	res.crtc_id_ptr = (unsigned long)crtcs;
	res.encoder_id_ptr = (unsigned long)encs;
	res.count_fbs = 0;
	if (ioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &res))
		goto fail;

	for (unsigned i = 0; i < res.count_connectors; i++) {
		struct drm_mode_get_connector c = { .connector_id = conns[i] };
		struct drm_mode_modeinfo modes[8];
		if (ioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, &c) || !c.count_modes || c.connection != 1)
			continue;
		if (c.count_modes > 8)
			c.count_modes = 8;
		unsigned nm = c.count_modes;
		c.modes_ptr = (unsigned long)modes;
		c.count_props = 0;
		c.count_encoders = 0;
		if (ioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, &c) || c.count_modes < nm)
			continue;
		/* 60Hz is the mode tested the most on this panel */
		struct drm_mode_modeinfo *m = &modes[0];
		for (unsigned j = 0; j < nm; j++)
			if (modes[j].vrefresh == 60)
				m = &modes[j];
		struct drm_mode_create_dumb cr = { .height = m->vdisplay, .width = m->hdisplay, .bpp = 32 };
		if (ioctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &cr))
			goto fail;
		struct drm_mode_fb_cmd fc = { .width = cr.width, .height = cr.height, .pitch = cr.pitch,
					      .bpp = 32, .depth = 24, .handle = cr.handle };
		if (ioctl(fd, DRM_IOCTL_MODE_ADDFB, &fc))
			goto fail;
		struct drm_mode_map_dumb mp = { .handle = cr.handle };
		if (ioctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &mp))
			goto fail;
		scanout = mmap(NULL, cr.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, mp.offset);
		if (scanout == MAP_FAILED)
			goto fail;
		memset(scanout, 0, cr.size);
		struct drm_mode_crtc sc = { .set_connectors_ptr = (unsigned long)&conns[i], .count_connectors = 1,
					    .crtc_id = crtcs[0], .fb_id = fc.fb_id, .mode_valid = 1, .mode = *m };
		if (ioctl(fd, DRM_IOCTL_MODE_SETCRTC, &sc))
			goto fail;
		drm_fd = fd; fb_id = fc.fb_id;
		fbw = cr.width; fbh = cr.height; stride = cr.pitch;
		return 1;
	}
fail:
	if (fd >= 0)
		close(fd);
	return 0;
}

static int has_connector(int card)
{
	char want[16];
	int n = snprintf(want, sizeof(want), "card%d-", card), found = 0;
	DIR *d = opendir("/sys/class/drm");
	struct dirent *e;

	if (!d)
		return 0;
	while ((e = readdir(d)))
		if (!strncmp(e->d_name, want, n))
			found = 1;
	closedir(d);
	return found;
}

static void open_fb(void)
{
	/* The driver can arrive a little after this program starts */
	for (int t = 0; t < 20; t++) {
		for (int i = 0; i < 4; i++) {
			char p[32];
			snprintf(p, sizeof(p), "/dev/dri/card%d", i);
			/* Opening the GPU-only card would start its firmware load */
			if (has_connector(i) && setup_card(p))
				goto found;
		}
		sleep(1);
	}
	exit(10);
found:
	fb_size = stride * fbh;
	fb = malloc(fb_size);
	if (!fb)
		exit(11);
}

static void open_touch(void)
{
	for (int i = 0; i < 32; i++) {
		char p[40], n[80] = "";
		struct input_absinfo a;
		snprintf(p, sizeof(p), "/dev/input/event%d", i);
		int fd = open(p, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
		if (fd < 0)
			continue;
		ioctl(fd, EVIOCGNAME(sizeof(n)), n);
		if (strstr(n, "Touchscreen") && !ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &a)) {
			ax_min = a.minimum; ax_max = a.maximum;
			ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), &a);
			ay_min = a.minimum; ay_max = a.maximum;
			touch_fd = fd;
			return;
		}
		close(fd);
	}
}

/* A finger lifted since the last call: 1 with screen coordinates */
static int read_tap(int *sx, int *sy)
{
	static int rx, ry;
	struct input_event e;

	while (read(touch_fd, &e, sizeof(e)) == sizeof(e)) {
		if (e.type == EV_ABS && e.code == ABS_MT_POSITION_X)
			rx = e.value;
		else if (e.type == EV_ABS && e.code == ABS_MT_POSITION_Y)
			ry = e.value;
		else if (e.type == EV_KEY && e.code == BTN_TOUCH && !e.value) {
			*sx = (long)(rx - ax_min) * fbw / (ax_max - ax_min + 1);
			*sy = (long)(ry - ay_min) * fbh / (ay_max - ay_min + 1);
			return 1;
		}
	}
	return 0;
}

/* Wait for a finger to lift. Returns 1 with screen coordinates, 0 on timeout */
static int wait_tap(int *sx, int *sy, int timeout_ms)
{
	int waited = 0;

	present();
	for (;;) {
		if (read_tap(sx, sy))
			return 1;
		flush_frame();
		usleep(30000);
		waited += 30;
		if (timeout_ms && waited >= timeout_ms)
			return 0;
	}
}

/* Hardware keyboards (pogo cover, USB), rescanned while typing so a late plug works */
#define MAXKBD 4
static int kbd_fd[MAXKBD], nkbd, kbd_shift;

static void open_keyboards(void)
{
	for (int i = 0; i < nkbd; i++)
		close(kbd_fd[i]);
	nkbd = 0;
	for (int i = 0; i < 32 && nkbd < MAXKBD; i++) {
		char p[40];
		unsigned char keys[KEY_MAX / 8 + 1] = { 0 };
		snprintf(p, sizeof(p), "/dev/input/event%d", i);
		int fd = open(p, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
		if (fd < 0)
			continue;
		if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keys)), keys) >= 0 &&
		    (keys[KEY_A / 8] & (1 << (KEY_A % 8))) && (keys[KEY_ENTER / 8] & (1 << (KEY_ENTER % 8))))
			kbd_fd[nkbd++] = fd;
		else
			close(fd);
	}
}

/* US layout, indexed by key code; second string is with shift */
static const char *const keymap[2] = {
	"\0\0331234567890-=\b\tqwertyuiop[]\n\0asdfghjkl;'`\0\\zxcvbnm,./\0*\0 ",
	"\0\033!@#$%^&*()_+\b\tQWERTYUIOP{}\n\0ASDFGHJKL:\"~\0|ZXCVBNM<>?\0*\0 ",
};

/* A character typed on a hardware keyboard, or 0 */
static int read_key(void)
{
	struct input_event e;

	for (int i = 0; i < nkbd; i++) {
		while (read(kbd_fd[i], &e, sizeof(e)) == sizeof(e)) {
			if (e.type != EV_KEY)
				continue;
			if (e.code == KEY_LEFTSHIFT || e.code == KEY_RIGHTSHIFT) {
				kbd_shift = !!e.value;
				continue;
			}
			if (!e.value || e.code > KEY_SPACE)
				continue;
			int c = (unsigned char)keymap[kbd_shift][e.code];
			if (c)
				return c;
		}
	}
	return 0;
}

/* Wait for a tap (returns 1) or a typed character (returns 2, in *key) */
static int wait_input(int *sx, int *sy, int *key)
{
	present();
	open_keyboards();
	for (int n = 0;; n++) {
		if ((*key = read_key()))
			return 2;
		if (read_tap(sx, sy))
			return 1;
		flush_frame();
		usleep(30000);
		if (n % 33 == 32)
			open_keyboards();
	}
}

/* Is volume up held right now? Used at boot to open the installer on request */
static int volup_held(void)
{
	for (int i = 0; i < 32; i++) {
		char p[40];
		unsigned char keys[KEY_MAX / 8 + 1] = { 0 };
		snprintf(p, sizeof(p), "/dev/input/event%d", i);
		int fd = open(p, O_RDONLY | O_CLOEXEC);
		if (fd < 0)
			continue;
		int r = ioctl(fd, EVIOCGKEY(sizeof(keys)), keys);
		close(fd);
		if (r >= 0 && (keys[KEY_VOLUMEUP / 8] & (1 << (KEY_VOLUMEUP % 8))))
			return 1;
	}
	return 0;
}

static void wifi_start(void)
{
	static char *up[] = { "ip", "link", "set", IFACE, "up", NULL };
	static char *wpa[] = { "wpa_supplicant", "-B", "-i", IFACE, "-c", "/tmp/wpa.conf", NULL };
	FILE *f = fopen("/tmp/wpa.conf", "w");
	if (f) {
		fputs("ctrl_interface=/run/wpa\nupdate_config=0\n", f);
		fclose(f);
	}
	mkdir("/run", 0755);
	run(up);
	run(wpa);
}

struct net { char ssid[64]; int level; int open; };
static struct net nets[MAXNET];
static int nnets;

static void wifi_scan(void)
{
	static char *scan[] = { "wpa_cli", "-p", "/run/wpa", "-i", IFACE, "scan", NULL };
	static char *res[] = { "wpa_cli", "-p", "/run/wpa", "-i", IFACE, "scan_results", NULL };
	static char buf[16384];
	char *line, *save;

	run(scan);
	sleep(4);
	capture(res, buf, sizeof(buf));
	nnets = 0;
	line = strtok_r(buf, "\n", &save); /* header */
	while ((line = strtok_r(NULL, "\n", &save))) {
		char *bssid = line, *freq = strchr(bssid, '\t'), *lvl, *flags, *ssid;
		if (!freq) continue;
		lvl = strchr(freq + 1, '\t'); if (!lvl) continue;
		flags = strchr(lvl + 1, '\t'); if (!flags) continue;
		ssid = strchr(flags + 1, '\t'); if (!ssid) continue;
		*flags = 0; *ssid++ = 0;
		if (!*ssid) continue;
		int lv = atoi(lvl + 1), dup = 0;
		for (int i = 0; i < nnets; i++)
			if (!strcmp(nets[i].ssid, ssid)) {
				dup = 1;
				if (lv > nets[i].level) nets[i].level = lv;
			}
		if (dup || nnets == MAXNET) continue;
		snprintf(nets[nnets].ssid, sizeof(nets[0].ssid), "%s", ssid);
		nets[nnets].level = lv;
		nets[nnets].open = !strstr(flags + 1, "WPA") && !strstr(flags + 1, "RSN") && !strstr(flags + 1, "SAE");
		nnets++;
	}
}

/*
 * Saved networks, "<SSID hex> <passphrase hex>" per line in /tmp/saved-wifi.
 * The initramfs fills it from userdata (read only); a successful join updates
 * it, and stage 2 writes it back after formatting. Passphrases are never logged.
 */
#define MAXSAVED 16
struct saved { char ssid[64]; char pw[64]; int bad; };
static struct saved saved[MAXSAVED];
static int nsaved;

static int unhex(const char *h, char *out, int max)
{
	int n = 0;
	unsigned v;

	while (h[0] && h[1] && n < max - 1) {
		if (sscanf(h, "%2x", &v) != 1)
			return -1;
		out[n++] = (char)v;
		h += 2;
	}
	out[n] = 0;
	return *h ? -1 : n;
}

static void put_hex(FILE *f, const char *s)
{
	for (; *s; s++)
		fprintf(f, "%02x", (unsigned char)*s);
}

static void saved_load(void)
{
	FILE *f = fopen("/tmp/saved-wifi", "r");
	char l[400];

	nsaved = 0;
	if (!f)
		return;
	while (nsaved < MAXSAVED && fgets(l, sizeof(l), f)) {
		char *sp, *a = strtok_r(l, " \n", &sp), *b = strtok_r(NULL, " \n", &sp);
		struct saved *w = &saved[nsaved];

		if (!a || unhex(a, w->ssid, sizeof(w->ssid)) <= 0)
			continue;
		w->pw[0] = 0;
		if (b && unhex(b, w->pw, sizeof(w->pw)) < 0)
			continue;
		w->bad = 0;
		nsaved++;
	}
	fclose(f);
}

static void saved_store(void)
{
	mode_t old = umask(077);
	FILE *f = fopen("/tmp/saved-wifi", "w");

	umask(old);
	if (!f)
		return;
	for (int i = 0; i < nsaved; i++) {
		put_hex(f, saved[i].ssid);
		fputc(' ', f);
		put_hex(f, saved[i].pw);
		fputc('\n', f);
	}
	fclose(f);
}

static struct saved *saved_find(const char *ssid)
{
	for (int i = 0; i < nsaved; i++)
		if (!strcmp(saved[i].ssid, ssid))
			return &saved[i];
	return NULL;
}

/* The network that just worked goes first, so it is tried first next time */
static void saved_put(const char *ssid, const char *pw)
{
	struct saved w = { 0 };
	int i;

	snprintf(w.ssid, sizeof(w.ssid), "%s", ssid);
	snprintf(w.pw, sizeof(w.pw), "%s", pw);
	for (i = 0; i < nsaved && strcmp(saved[i].ssid, ssid); i++)
		;
	if (i == nsaved && nsaved < MAXSAVED)
		nsaved++;
	if (i == MAXSAVED)
		i = MAXSAVED - 1;
	memmove(&saved[1], &saved[0], i * sizeof(saved[0]));
	saved[0] = w;
	saved_store();
	memset(&w, 0, sizeof(w));
}

static const char *const rows[2][4] = {
	{ "1234567890-=", "qwertyuiop[]", "asdfghjkl;'", "zxcvbnm,./\\" },
	{ "!@#$%^&*()_+", "QWERTYUIOP{}", "ASDFGHJKL:\"", "ZXCVBNM<>?|" },
};

/* Returns 1 with the text in out, 0 if cancelled. Characters are never logged. */
static int keyboard(const char *title, const char *ok, int show, char *out, int max)
{
	int shift = 0, len = 0;
	const int kw = 200, kh = 170, x0 = 100, y0 = 380;
	struct btn bshift = { x0, y0 + 4 * kh, 300, kh - 10 }, bspace = { x0 + 310, y0 + 4 * kh, 800, kh - 10 };
	struct btn bback = { x0 + 1120, y0 + 4 * kh, 300, kh - 10 }, bshow = { x0 + 1430, y0 + 4 * kh, 300, kh - 10 };
	struct btn bok = { fbw - 500, 60, 400, 140 }, bcancel = { fbw - 950, 60, 400, 140 };
	out[0] = 0;
	for (;;) {
		char shown[128];
		rect(0, 0, fbw, fbh, BG);
		text(100, 60, title, FG);
		for (int i = 0; i < len && i < 100; i++)
			shown[i] = show ? out[i] : '*';
		shown[len < 100 ? len : 100] = 0;
		rect(90, 220, fbw - 180, 110, BTN);
		text(120, 250, shown, FG);
		button(&bok, ok, ACC);
		button(&bcancel, "Back", BTN);
		for (int r = 0; r < 4; r++) {
			const char *row = rows[shift][r];
			for (int c = 0; row[c]; c++) {
				struct btn k = { x0 + c * kw, y0 + r * kh, kw - 10, kh - 10 };
				char t[2] = { row[c], 0 };
				button(&k, t, BTN);
			}
		}
		button(&bshift, shift ? "abc" : "Shift", BTN);
		button(&bspace, "Space", BTN);
		button(&bback, "Delete", WARN);
		button(&bshow, show ? "Hide" : "Show", BTN);

		int x, y, key;
		if (wait_input(&x, &y, &key) == 2) {
			if (key == '\n') return 1;
			if (key == 033) return 0;
			if (key == '\b') { if (len) out[--len] = 0; }
			else if (key >= ' ' && len < max - 1) { out[len++] = key; out[len] = 0; }
			continue;
		}
		if (hit(&bok, x, y)) return 1;
		if (hit(&bcancel, x, y)) return 0;
		if (hit(&bshift, x, y)) { shift = !shift; continue; }
		if (hit(&bshow, x, y)) { show = !show; continue; }
		if (hit(&bback, x, y)) { if (len) out[--len] = 0; continue; }
		if (hit(&bspace, x, y)) { if (len < max - 1) { out[len++] = ' '; out[len] = 0; } continue; }
		for (int r = 0; r < 4; r++) {
			const char *row = rows[shift][r];
			if (y < y0 + r * kh || y >= y0 + (r + 1) * kh)
				continue;
			int c = (x - x0) / kw;
			if (x >= x0 && c < (int)strlen(row) && len < max - 1) {
				out[len++] = row[c];
				out[len] = 0;
			}
		}
	}
}

static int wifi_connect(const struct net *n, const char *pw)
{
	static char *down[] = { "killall", "wpa_supplicant", NULL };
	static char *wpa[] = { "wpa_supplicant", "-B", "-i", IFACE, "-c", "/tmp/wpa.conf", NULL };
	static char *st[] = { "wpa_cli", "-p", "/run/wpa", "-i", IFACE, "status", NULL };
	static char *dh[] = { "udhcpc", "-n", "-q", "-t", "6", "-i", IFACE, NULL };
	static char *addr[] = { "ip", "-4", "-o", "addr", "show", IFACE, NULL };
	static char buf[2048];
	FILE *f;

	run(down);
	usleep(500000);
	/* The network name is stored as hex, so it needs no escaping */
	f = fopen("/tmp/wpa.conf", "w");
	if (!f) return -1;
	chmod("/tmp/wpa.conf", 0600);
	fputs("ctrl_interface=/run/wpa\nupdate_config=0\nnetwork={\n  ssid=", f);
	for (const char *s = n->ssid; *s; s++) fprintf(f, "%02x", (unsigned char)*s);
	fputs("\n  scan_ssid=1\n", f);
	if (n->open) {
		fputs("  key_mgmt=NONE\n", f);
	} else {
		fputs("  key_mgmt=WPA-PSK SAE\n  ieee80211w=1\n  psk=\"", f);
		fputs(pw, f);
		fputs("\"\n", f);
	}
	fputs("}\n", f);
	fclose(f);
	run(wpa);
	for (int i = 0; i < 30; i++) {
		sleep(1);
		capture(st, buf, sizeof(buf));
		if (strstr(buf, "wpa_state=COMPLETED"))
			break;
		if (i == 29)
			return -2;
	}
	if (run(dh))
		return -3;
	capture(addr, buf, sizeof(buf));
	char *a = strstr(buf, "inet ");
	if (a) {
		a += 5;
		char *e = strchr(a, ' ');
		if (e) *e = 0;
		snprintf(status, sizeof(status), "Connected: %s", a);
	}
	return 0;
}

static const char *const REBOOT[] = { "reboot", "-f", NULL };

static void header(const char *title)
{
	rect(0, 0, fbw, fbh, BG);
	text(100, 60, title, FG);
	rect(100, 125, fbw - 200, 4, ACC);
}

static void progress_bar(int y, int percent)
{
	char t[16];
	if (percent < 0) percent = 0;
	if (percent > 100) percent = 100;
	rrect(100, y, fbw - 200, 70, 35, CARD);
	if (percent)
		rrect(100, y, 70 + (fbw - 270) * percent / 100, 70, 35, ACC);
	snprintf(t, sizeof(t), "%d%%", percent);
	text(fbw / 2 - (int)strlen(t) * CW / 2, y + 11, t, FG);
}

static void close_screen(void)
{
	ioctl(drm_fd, DRM_IOCTL_DROP_MASTER, 0);
	close(drm_fd);
	drm_fd = -1;
	if (touch_fd >= 0)
		close(touch_fd);
	touch_fd = -1;
}

/* Download the installer built with this boot image, then hand the screen to it */
static int fetch_installer(void)
{
	static char *fetch[] = { "sh", "/fetch.sh", NULL };
	static char *go[] = { "sh", "/s2start.sh", NULL };

	long want = 0;
	FILE *sz = fopen("/stage2.size", "r");
	if (sz) {
		if (fscanf(sz, "%ld", &want) != 1)
			want = 0;
		fclose(sz);
	}
	pid_t pid = fork();
	if (!pid) {
		int n = open("/tmp/fetch.log", O_WRONLY | O_CREAT | O_TRUNC, 0600);
		if (n >= 0) {
			dup2(n, 1);
			dup2(n, 2);
			close(n);
		}
		execvp(fetch[0], fetch);
		_exit(127);
	}
	int st = 0, r = -1;
	for (;;) {
		struct stat f;
		long have = stat("/s2/stage2.tar.gz", &f) ? 0 : f.st_size;
		header("Galaxy Tab S7+ Linux installer");
		text(100, 220, "Downloading the installer", FG);
		progress_bar(340, want ? (int)(have * 100 / want) : 0);
		present();
		usleep(300000);
		if (waitpid(pid, &st, WNOHANG) == pid) {
			r = WIFEXITED(st) ? WEXITSTATUS(st) : 1;
			break;
		}
	}
	if (r) {
		char why[120] = "";
		FILE *e = fopen("/tmp/fetch.err", "r");
		if (e) {
			if (!fgets(why, sizeof(why), e))
				why[0] = 0;
			fclose(e);
			why[strcspn(why, "\n")] = 0;
		}
		snprintf(status, sizeof(status), "%s%s%.110s", r == 2 ? "Installer download is damaged" : "Could not download the installer",
			 why[0] ? ": " : "", why);
		return r;
	}
	close_screen();
	run(go);
	open_fb();
	open_touch();
	snprintf(status, sizeof(status), "Installer stopped");
	return 1;
}

static int ask(const char *a, const char *b, const char *c, const char *yes, const char *no, unsigned yes_col)
{
	const struct btn by = { fbw - 900, fbh - 220, 800, 170 }, bn = { 100, fbh - 220, 600, 170 };

	header("Galaxy Tab S7+ Linux installer");
	if (a) text(100, 220, a, FG);
	if (b) text(100, 320, b, FG);
	if (c) text(100, 420, c, FG);
	button(&by, yes, yes_col);
	if (no) button(&bn, no, BTN);
	for (;;) {
		int x, y;
		wait_tap(&x, &y, 0);
		if (hit(&by, x, y)) return 1;
		if (no && hit(&bn, x, y)) return 0;
	}
}

static int valid_user(const char *u)
{
	if (!*u || !((*u >= 'a' && *u <= 'z') || *u == '_') || strlen(u) > 31)
		return 0;
	for (; *u; u++)
		if (!((*u >= 'a' && *u <= 'z') || (*u >= '0' && *u <= '9') || *u == '_' || *u == '-'))
			return 0;
	return 1;
}

static const struct { const char *id, *name, *about; } desktops[] = {
	{ "gnome", "GNOME", "Tested on this tablet. Recommended." },
	{ "kde", "KDE Plasma", "Classic desktop." },
	{ "kde-mobile", "KDE Plasma Mobile", "Touch first. Can feel slow." },
	{ NULL, "Phosh", "GNOME based, touch first. Crashes on this tablet for now." },
	{ NULL, "ztOS", "Windows Phone style, on Arch. Port not ready yet." },
};

static const char *choose_desktop(void)
{
	const int n = sizeof(desktops) / sizeof(desktops[0]);
	struct btn b[5];

	header("Choose a desktop");
	for (int i = 0; i < n; i++) {
		char l[120];
		unsigned name = desktops[i].id ? FG : 0x6c7a85;
		b[i] = (struct btn){ 100, 160 + i * 310, fbw - 200, 285 };
		rrect(b[i].x, b[i].y, b[i].w, b[i].h, 36, CARD);
		text(150, b[i].y + 70, desktops[i].name, name);
		snprintf(l, sizeof(l), "%s", desktops[i].about);
		text(150, b[i].y + 160, l, desktops[i].id ? 0xa8b8c4 : 0x6c7a85);
	}
	for (;;) {
		int x, y;
		wait_tap(&x, &y, 0);
		for (int i = 0; i < n; i++)
			if (desktops[i].id && hit(&b[i], x, y))
				return desktops[i].id;
	}
}

static int stage2(void)
{
	static char user[33], p1[64], p2[64], line[160];

	open_fb();
	open_touch();
	if (touch_fd < 0)
		return 2;
	for (;;) {
		if (ask("This installs Arch Linux on this tablet.", "Your firmware is copied from this tablet on first boot.",
			"You need Wi-Fi for the whole install, about 2 GB.", "Continue", "Reboot", ACC))
			break;
		sync();
		run((char *const *)REBOOT);
	}
	const char *desktop = choose_desktop();

	for (;;) {
		if (!keyboard("Choose a user name: lowercase and digits", "Next", 1, user, sizeof(user)))
			continue;
		char copy[33];
		snprintf(copy, sizeof(copy), "%s", user);
		if (strcmp(copy, "root") && valid_user(copy))
			break;
		ask("That name is not allowed.", "Use lowercase letters, digits, - and _.", "It must start with a letter.", "Try again", NULL, ACC);
	}
	for (;;) {
		if (!keyboard("Choose a password", "Next", 0, p1, sizeof(p1)) || !*p1)
			continue;
		if (!keyboard("Type the password again", "Next", 0, p2, sizeof(p2)))
			continue;
		if (!strcmp(p1, p2))
			break;
		ask("The passwords did not match.", NULL, NULL, "Try again", NULL, ACC);
	}
	memset(p2, 0, sizeof(p2));
	if (access("/dry", F_OK)) {
		snprintf(line, sizeof(line), "User: %s", user);
		if (!ask("THIS ERASES EVERYTHING ON THIS TABLET'S DATA PARTITION.", "Android apps, photos and files on it will be gone.", line,
			 "Erase and install", "Reboot", WARN)) {
			sync();
			run((char *const *)REBOOT);
		}
	}
	FILE *f = fopen("/tmp/choices", "w");
	if (!f)
		return 3;
	chmod("/tmp/choices", 0600);
	fprintf(f, "DESKTOP=%s\nNEW_USER=%s\nNEW_PASS='", desktop, user);
	for (const char *c = p1; *c; c++) {
		if (*c == '\'')
			fputs("'\\''", f);
		else
			fputc(*c, f);
	}
	fputs("'\n", f);
	fclose(f);
	memset(p1, 0, sizeof(p1));

	pid_t pid = fork();
	if (!pid) {
		int n = open("/tmp/install.log", O_WRONLY | O_CREAT | O_APPEND, 0600);
		dup2(n, 1); dup2(n, 2);
		execl("/bin/sh", "sh", "/install.sh", NULL);
		_exit(127);
	}
	int st = 0, done = 0;
	while (!done) {
		char step[120] = "Starting";
		int percent = 0;
		f = fopen("/tmp/step", "r");
		if (f) {
			if (!fgets(step, sizeof(step), f))
				step[0] = 0;
			fclose(f);
			step[strcspn(step, "\n")] = 0;
		}
		f = fopen("/tmp/percent", "r");
		if (f) {
			if (fscanf(f, "%d", &percent) != 1)
				percent = 0;
			fclose(f);
		}
		header("Installing");
		text(100, 220, step, FG);
		progress_bar(340, percent);
		text(100, 470, "You can leave the tablet alone. Keep it charged if you can.", 0xa8b8c4);
		present();
		for (int i = 0; i < 15; i++) {
			flush_frame();
			usleep(33000);
		}
		if (waitpid(pid, &st, WNOHANG) == pid)
			done = 1;
	}
	if (WIFEXITED(st) && !WEXITSTATUS(st)) {
		ask("Done. The tablet will start Arch after you reboot.", "The first start copies firmware and takes longer.", NULL, "Reboot", NULL, ACC);
	} else {
		char step[120] = "Install failed";
		f = fopen("/tmp/step", "r");
		if (f) {
			if (!fgets(step, sizeof(step), f))
				step[0] = 0;
			fclose(f);
			step[strcspn(step, "\n")] = 0;
		}
		ask(step, "Log: /tmp/install.log in the USB shell.", "Rebooting returns to this installer.", "Reboot", NULL, WARN);
	}
	sync();
	run((char *const *)REBOOT);
	return 0;
}

/* Join a network from the list, remember it, then fetch the installer */
static void join(int i, const char *pw)
{
	struct saved *w = saved_find(nets[i].ssid);

	rect(0, 0, fbw, fbh, BG);
	text(100, 160, "Connecting...", FG);
	present();
	int r = wifi_connect(&nets[i], pw);
	if (r == -2 && w && !strcmp(w->pw, pw)) {
		w->bad = 1;
		snprintf(status, sizeof(status), "Saved password for %.40s failed. Tap it to enter it again", nets[i].ssid);
	} else if (r == -2) {
		snprintf(status, sizeof(status), "Could not join. Wrong password?");
	} else if (r == -3) {
		snprintf(status, sizeof(status), "Joined, but got no address");
	} else if (r) {
		snprintf(status, sizeof(status), "Connection failed (%d)", r);
	} else {
		saved_put(nets[i].ssid, pw);
		fetch_installer();
	}
}

int main(int argc, char **argv)
{
	/*
	 * Started in the background without stdio. The display and touch fds are
	 * close-on-exec, so children would then run with 0-2 closed, and busybox
	 * wget's TLS helper mixes up its socket and reads ciphertext as headers
	 * ("bad header line"). Keep 0-2 on /dev/null.
	 */
	for (int n; (n = open("/dev/null", O_RDWR)) >= 0;)
		if (n > 2) {
			close(n);
			break;
		}
	setenv("PATH", "/usr/sbin:/usr/bin:/sbin:/bin", 1);
	if (argc > 1 && !strcmp(argv[1], "--stage2"))
		return stage2();
	if (argc > 1 && !strcmp(argv[1], "--volup"))
		return volup_held() ? 0 : 1;
	static char pw[64];
	static char *rb[] = { "reboot", "-f", NULL };

	open_fb();
	const struct btn bscan = { 100, fbh - 200, 500, 150 }, breboot = { fbw - 600, fbh - 200, 500, 150 };
	open_touch();
	rect(0, 0, fbw, fbh, BG);
	text(100, 60, "Galaxy Tab S7+ Linux installer", FG);
	if (touch_fd < 0) {
		text(100, 160, "No touchscreen found", WARN);
		present();
		sleep(600);
		return 2;
	}
	text(100, 160, "Starting Wi-Fi...", FG);
	present();
	wifi_start();
	saved_load();
	snprintf(status, sizeof(status), "Choose a network");
	for (int need_scan = 1, first = 1;;) {
		if (need_scan) {
			rect(100, 150, fbw - 200, 70, BG);
			text(100, 160, "Scanning...", FG);
			present();
			wifi_scan();
			need_scan = 0;
		}
		/* Once per start: join the first saved network in range unless tapped */
		for (int i = 0; first && i < nnets; i++) {
			struct saved *w = saved_find(nets[i].ssid);
			if (!w || w->bad)
				continue;
			first = 0;
			int x, y, tapped = 0;
			for (int left = 5; left > 0 && !tapped; left--) {
				char l[120];
				rect(0, 0, fbw, fbh, BG);
				text(100, 60, "Galaxy Tab S7+ Linux installer", FG);
				snprintf(l, sizeof(l), "Connecting to %.60s in %d", w->ssid, left);
				text(100, 220, l, FG);
				text(100, 300, "Tap anywhere to choose another network", FG);
				tapped = wait_tap(&x, &y, 1000);
			}
			if (!tapped) {
				snprintf(pw, sizeof(pw), "%s", w->pw);
				join(i, pw);
				memset(pw, 0, sizeof(pw));
			}
			break;
		}
		first = 0;
		rect(0, 0, fbw, fbh, BG);
		text(100, 60, "Galaxy Tab S7+ Linux installer", FG);
		text(100, 160, status, FG);
		struct btn rows_b[MAXNET];
		for (int i = 0; i < nnets; i++) {
			char l[100];
			rows_b[i] = (struct btn){ 100, 260 + i * 140, fbw - 200, 125 };
			struct saved *w = saved_find(nets[i].ssid);
			snprintf(l, sizeof(l), "%-40.40s %4d dBm%s%s", nets[i].ssid, nets[i].level,
				 nets[i].open ? "  open" : "", w && !w->bad ? "  saved" : "");
			button(&rows_b[i], l, BTN);
		}
		if (!nnets)
			text(100, 300, "No networks found", FG);
		button(&bscan, "Scan again", ACC);
		button(&breboot, "Reboot", WARN);

		int x, y;
		wait_tap(&x, &y, 0);
		if (hit(&breboot, x, y)) {
			sync();
			run(rb);
		}
		if (hit(&bscan, x, y)) {
			need_scan = 1;
			continue;
		}
		for (int i = 0; i < nnets; i++) {
			if (!hit(&rows_b[i], x, y))
				continue;
			char title[100];
			struct saved *w = saved_find(nets[i].ssid);
			pw[0] = 0;
			snprintf(title, sizeof(title), "Password: %.60s", nets[i].ssid);
			if (w && !w->bad)
				snprintf(pw, sizeof(pw), "%s", w->pw);
			else if (!nets[i].open && !keyboard(title, "Connect", 0, pw, sizeof(pw)))
				break;
			join(i, pw);
			memset(pw, 0, sizeof(pw));
			break;
		}
	}
}

// SPDX-License-Identifier: MIT
/*
 * Stage 1 of the Tab S7+ installer: choose a Wi-Fi network by touch.
 * Draws on the firmware framebuffer, reads the touchscreen, drives wpa_supplicant.
 */
#include <dirent.h>
#include <fcntl.h>
#include <linux/fb.h>
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

static void button(const struct btn *b, const char *s, unsigned c)
{
	rect(b->x, b->y, b->w, b->h, c);
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

static void open_fb(void)
{
	struct fb_var_screeninfo v;
	struct fb_fix_screeninfo f;
	int fd = open("/dev/fb0", O_RDWR);
	if (fd < 0 || ioctl(fd, FBIOGET_VSCREENINFO, &v) || ioctl(fd, FBIOGET_FSCREENINFO, &f))
		exit(10);
	fbw = v.xres; fbh = v.yres; stride = f.line_length;
	fb = mmap(NULL, f.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (fb == MAP_FAILED)
		exit(11);
}

static void open_touch(void)
{
	for (int i = 0; i < 32; i++) {
		char p[40], n[80] = "";
		struct input_absinfo a;
		snprintf(p, sizeof(p), "/dev/input/event%d", i);
		int fd = open(p, O_RDONLY | O_NONBLOCK);
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

/* Wait for a finger to lift. Returns 1 with screen coordinates, 0 on timeout */
static int wait_tap(int *sx, int *sy, int timeout_ms)
{
	static int rx, ry;
	struct input_event e;
	int waited = 0;

	for (;;) {
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
		usleep(10000);
		waited += 10;
		if (timeout_ms && waited >= timeout_ms)
			return 0;
	}
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

static const char *const rows[2][4] = {
	{ "1234567890-=", "qwertyuiop[]", "asdfghjkl;'", "zxcvbnm,./\\" },
	{ "!@#$%^&*()_+", "QWERTYUIOP{}", "ASDFGHJKL:\"", "ZXCVBNM<>?|" },
};

/* Returns 1 with the text in out, 0 if cancelled. Characters are never logged. */
static int keyboard(const char *title, char *out, int max)
{
	int shift = 0, show = 0, len = 0;
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
		button(&bok, "Connect", ACC);
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

		int x, y;
		wait_tap(&x, &y, 0);
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
	/* Stored as hex, so no character in a name or password needs escaping */
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

int main(void)
{
	static char pw[64];
	const struct btn bscan = { 100, fbh - 200, 500, 150 }, breboot = { fbw - 600, fbh - 200, 500, 150 };
	static char *rb[] = { "reboot", "-f", NULL };

	open_fb();
	open_touch();
	rect(0, 0, fbw, fbh, BG);
	text(100, 60, "Galaxy Tab S7+ Linux installer", FG);
	if (touch_fd < 0) {
		text(100, 160, "No touchscreen found", WARN);
		sleep(600);
		return 2;
	}
	text(100, 160, "Starting Wi-Fi...", FG);
	wifi_start();
	snprintf(status, sizeof(status), "Choose a network");
	for (int need_scan = 1;;) {
		if (need_scan) {
			rect(100, 150, fbw - 200, 70, BG);
			text(100, 160, "Scanning...", FG);
			wifi_scan();
			need_scan = 0;
		}
		rect(0, 0, fbw, fbh, BG);
		text(100, 60, "Galaxy Tab S7+ Linux installer", FG);
		text(100, 160, status, FG);
		struct btn rows_b[MAXNET];
		for (int i = 0; i < nnets; i++) {
			char l[100];
			rows_b[i] = (struct btn){ 100, 260 + i * 140, fbw - 200, 125 };
			snprintf(l, sizeof(l), "%-40.40s %4d dBm%s", nets[i].ssid, nets[i].level, nets[i].open ? "  open" : "");
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
			pw[0] = 0;
			snprintf(title, sizeof(title), "Password: %.60s", nets[i].ssid);
			if (!nets[i].open && !keyboard(title, pw, sizeof(pw)))
				break;
			rect(0, 0, fbw, fbh, BG);
			text(100, 160, "Connecting...", FG);
			int r = wifi_connect(&nets[i], pw);
			memset(pw, 0, sizeof(pw));
			if (r == -2) snprintf(status, sizeof(status), "Could not join. Wrong password?");
			else if (r == -3) snprintf(status, sizeof(status), "Joined, but got no address");
			else if (r) snprintf(status, sizeof(status), "Connection failed (%d)", r);
			break;
		}
	}
}

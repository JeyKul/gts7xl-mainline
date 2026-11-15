// SPDX-License-Identifier: MIT
/*
 * The Book Cover keyboard's touchpad as a mouse for TWRP. The pad reports
 * absolute multitouch, so TWRP took it for a second touchscreen. This grabs it
 * and feeds a uinput mouse instead: one finger moves the cursor, a short tap or
 * the pad's own button clicks, and holding the button while moving drags.
 * Waits for the cover and picks it up again after a reattach.
 */
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#define PAD_NAME "Samsung Book Cover Touchpad"
#define SPEED 1.6	/* pad units to screen pixels */
#define TAP_MS 180
#define TAP_MOVE 25

static int out = -1;

static long now_ms(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

static void emit(int type, int code, int value)
{
	struct input_event e = { .type = type, .code = code, .value = value };
	if (write(out, &e, sizeof(e)) < 0)
		perror("uinput write");
}

static int make_mouse(void)
{
	struct uinput_setup s = { .id = { .bustype = BUS_VIRTUAL, .vendor = 0x1, .product = 0x1 } };
	int fd = open("/dev/uinput", O_WRONLY | O_CLOEXEC);

	if (fd < 0)
		return -1;
	ioctl(fd, UI_SET_EVBIT, EV_KEY);
	ioctl(fd, UI_SET_KEYBIT, BTN_LEFT);
	ioctl(fd, UI_SET_KEYBIT, BTN_RIGHT);
	ioctl(fd, UI_SET_EVBIT, EV_REL);
	ioctl(fd, UI_SET_RELBIT, REL_X);
	ioctl(fd, UI_SET_RELBIT, REL_Y);
	snprintf(s.name, sizeof(s.name), "Book Cover Touchpad Mouse");
	if (ioctl(fd, UI_DEV_SETUP, &s) || ioctl(fd, UI_DEV_CREATE)) {
		close(fd);
		return -1;
	}
	return fd;
}

static int find_pad(void)
{
	for (int i = 0; i < 32; i++) {
		char p[40], n[128] = "";
		snprintf(p, sizeof(p), "/dev/input/event%d", i);
		int fd = open(p, O_RDONLY | O_CLOEXEC);
		if (fd < 0)
			continue;
		ioctl(fd, EVIOCGNAME(sizeof(n)), n);
		if (!strcmp(n, PAD_NAME) && !ioctl(fd, EVIOCGRAB, 1))
			return fd;
		close(fd);
	}
	return -1;
}

int main(void)
{
	while ((out = make_mouse()) < 0)
		sleep(1);
	for (;;) {
		int pad;
		while ((pad = find_pad()) < 0)
			sleep(2);

		struct input_event e;
		int touching = 0, x = -1, y = -1, lx = -1, ly = -1, moved = 0, button = 0;
		double fx = 0, fy = 0;
		long down = 0;
		while (read(pad, &e, sizeof(e)) == sizeof(e)) {
			if (e.type == EV_ABS && e.code == ABS_MT_POSITION_X)
				x = e.value;
			else if (e.type == EV_ABS && e.code == ABS_MT_POSITION_Y)
				y = e.value;
			else if (e.type == EV_KEY && e.code == BTN_TOUCH) {
				touching = e.value;
				if (touching) {
					down = now_ms();
					moved = 0;
					lx = ly = -1;
				} else if (!button && moved < TAP_MOVE && now_ms() - down < TAP_MS) {
					emit(EV_KEY, BTN_LEFT, 1);
					emit(EV_SYN, SYN_REPORT, 0);
					emit(EV_KEY, BTN_LEFT, 0);
					emit(EV_SYN, SYN_REPORT, 0);
				}
			} else if (e.type == EV_KEY && (e.code == BTN_LEFT || e.code == BTN_RIGHT)) {
				button = e.value;
				emit(EV_KEY, e.code, e.value);
			} else if (e.type == EV_SYN && e.code == SYN_REPORT) {
				if (touching && x >= 0 && y >= 0) {
					if (lx >= 0) {
						fx += (x - lx) * SPEED;
						fy += (y - ly) * SPEED;
						moved += abs(x - lx) + abs(y - ly);
						int dx = (int)fx, dy = (int)fy;
						fx -= dx;
						fy -= dy;
						if (dx)
							emit(EV_REL, REL_X, dx);
						if (dy)
							emit(EV_REL, REL_Y, dy);
					}
					lx = x;
					ly = y;
				}
				emit(EV_SYN, SYN_REPORT, 0);
			}
		}
		/* Cover detached: look for it again */
		close(pad);
	}
}

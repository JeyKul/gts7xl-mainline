// SPDX-License-Identifier: MIT
/*
 * Stand-in for Android's libhardware_legacy, for gpsd outside Android.
 * The real acquire_wake_lock() looks up servicemanager by waiting for a system
 * property that never appears without Android's init, and spins forever.
 * There is no suspend service here, so wake locks succeed and do nothing.
 */
#include <stdint.h>

int socket(int, int, int);
int bind(int, const void *, unsigned int);
long recv(int, void *, unsigned long, int);
int close(int);

#define PF_NETLINK		16
#define SOCK_DGRAM		2
#define SOCK_CLOEXEC		02000000
#define NETLINK_KOBJECT_UEVENT	15

struct sockaddr_nl {
	uint16_t nl_family;
	uint16_t nl_pad;
	uint32_t nl_pid;
	uint32_t nl_groups;
};

int acquire_wake_lock(int lock, const char *id) { (void)lock; (void)id; return 0; }
int release_wake_lock(const char *id) { (void)id; return 0; }

static int fd = -1;

int uevent_init(void)
{
	struct sockaddr_nl a = { .nl_family = PF_NETLINK, .nl_groups = 0xffffffff };
	int s = socket(PF_NETLINK, SOCK_DGRAM | SOCK_CLOEXEC, NETLINK_KOBJECT_UEVENT);

	if (s < 0)
		return 0;
	if (bind(s, &a, sizeof(a)) < 0) {
		close(s);
		return 0;
	}
	fd = s;
	return 1;
}

int uevent_get_fd(void) { return fd; }

int uevent_next_event(char *buf, int len)
{
	long n = recv(fd, buf, len - 1, 0);

	if (n <= 0)
		return 0;
	buf[n] = 0;
	return n;
}

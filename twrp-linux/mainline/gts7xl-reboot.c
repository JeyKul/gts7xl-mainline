// SPDX-License-Identifier: MIT
/*
 * Reboot for TWRP on the mainline kernel. Android's init takes sys.powerctl but
 * its shutdown sequence never reaches the kernel here, so an rc trigger hands the
 * value to this: "reboot,<reason>" restarts with that reason (recovery,
 * bootloader, download), "shutdown,..." powers off.
 */
#include <string.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <linux/reboot.h>

int main(int argc, char **argv)
{
	const char *v = argc > 1 ? argv[1] : "reboot";
	const char *reason = strchr(v, ',');

	reason = reason ? reason + 1 : "";
	sync();
	if (!strncmp(v, "shutdown", 8))
		return syscall(SYS_reboot, LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2, LINUX_REBOOT_CMD_POWER_OFF, 0);
	if (*reason)
		return syscall(SYS_reboot, LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2, LINUX_REBOOT_CMD_RESTART2, reason);
	return syscall(SYS_reboot, LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2, LINUX_REBOOT_CMD_RESTART, 0);
}

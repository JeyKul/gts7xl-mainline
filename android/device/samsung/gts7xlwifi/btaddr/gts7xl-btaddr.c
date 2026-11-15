// Sets hci0's public address from the tablet's efs (bluetooth/bt_addr).
// Without it the QCA6390 stays "unconfigured" and the Bluetooth HAL waits forever.
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <sys/socket.h>

#define BTPROTO_HCI 1
#define HCI_DEV_NONE 0xffff
#define HCI_CHANNEL_CONTROL 3
#define MGMT_OP_SET_PUBLIC_ADDRESS 0x0039

struct sockaddr_hci {
	sa_family_t hci_family;
	unsigned short hci_dev;
	unsigned short hci_channel;
};

int main(int argc, char **argv)
{
	const char *path = argc > 1 ? argv[1] : "/mnt/vendor/efs/bluetooth/bt_addr";
	unsigned int b[6];
	unsigned char pkt[12];
	FILE *f = fopen(path, "r");

	if (!f || fscanf(f, "%2x:%2x:%2x:%2x:%2x:%2x", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) != 6) {
		fprintf(stderr, "gts7xl-btaddr: no address in %s\n", path);
		return 1;
	}
	fclose(f);

	int fd = socket(AF_BLUETOOTH, SOCK_RAW | SOCK_CLOEXEC, BTPROTO_HCI);
	struct sockaddr_hci a = { AF_BLUETOOTH, HCI_DEV_NONE, HCI_CHANNEL_CONTROL };
	if (fd < 0 || bind(fd, (struct sockaddr *)&a, sizeof(a)) < 0) {
		perror("gts7xl-btaddr: mgmt socket");
		return 1;
	}

	// opcode, index 0, length 6, address little endian
	pkt[0] = MGMT_OP_SET_PUBLIC_ADDRESS; pkt[1] = 0;
	pkt[2] = 0; pkt[3] = 0;
	pkt[4] = 6; pkt[5] = 0;
	for (int i = 0; i < 6; i++)
		pkt[6 + i] = b[5 - i];
	// hci0 may not exist yet or still be in its first setup: retry for ~20 s
	for (int tries = 0; tries < 40; tries++, usleep(500000)) {
		// EINVAL until hci0 is registered
		if (write(fd, pkt, sizeof(pkt)) != sizeof(pkt))
			continue;
		// The reply: cmd complete 0x0001 / status 0x0002, then opcode, status
		struct pollfd p = { fd, POLLIN, 0 };
		unsigned char ev[64] = { 0 };
		while (poll(&p, 1, 2000) > 0) {
			ssize_t n = read(fd, ev, sizeof(ev));
			if (n >= 9 && (ev[0] == 1 || ev[0] == 2) && ev[6] == MGMT_OP_SET_PUBLIC_ADDRESS)
				break;
		}
		if (ev[6] == MGMT_OP_SET_PUBLIC_ADDRESS && ev[8] == 0) {
			printf("gts7xl-btaddr: set\n");
			return 0;
		}
	}
	fprintf(stderr, "gts7xl-btaddr: hci0 never accepted the address\n");
	return 1;
}

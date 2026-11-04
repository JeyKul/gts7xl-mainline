/* SPDX-License-Identifier: MIT */
/*
 * gts7xl-sensord: sensor stack for the Galaxy Tab S7+ (SM8250) on mainline Linux.
 *
 * FastRPC file service for the sensor DSP, SSC client, uinput accelerometer.
 * Original code; see LICENSE and docs/RESEARCH.md for what informed it.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <linux/uinput.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "client.h"
#include "fsrv.h"
#include "listener.h"

#define DEFAULT_DEVICE "/dev/fastrpc-sdsp"
#define MAX_UIDS 8
#define G 9.80665
#define UINPUT_NAME "gts7xl-sensord accelerometer"
#define UINPUT_COUNTS_PER_G 256 /* iio-sensor-proxy's input-accel scale */
#define UINPUT_RANGE (16 * UINPUT_COUNTS_PER_G)

static volatile int stop_flag;

static void on_signal(int sig)
{
	(void)sig;
	stop_flag = 1;
}

static void install_signals(void)
{
	struct sigaction sa = { .sa_handler = on_signal };

	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
}

/* --- --list ------------------------------------------------------------ */

static void print_attr(void *ctx, int id, const struct ssc_attr_value *v)
{
	static const struct { int id; const char *name; } names[] = {
		{ SSC_ATTR_NAME, "name" }, { SSC_ATTR_VENDOR, "vendor" },
		{ SSC_ATTR_TYPE, "type" }, { SSC_ATTR_AVAILABLE, "available" },
		{ SSC_ATTR_RATES, "rate" }, { SSC_ATTR_STREAM_TYPE, "stream_type" },
		{ SSC_ATTR_PHYSICAL, "physical" },
	};
	size_t i;

	(void)ctx;
	for (i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
		if (names[i].id != id)
			continue;
		switch (v->kind) {
		case ATTR_STR:
			printf("    %s: %.*s\n", names[i].name, (int)strnlen(v->s, v->slen), v->s);
			break;
		case ATTR_FLOAT:
			printf("    %s: %g\n", names[i].name, v->f);
			break;
		case ATTR_INT:
			printf("    %s: %llu\n", names[i].name, (unsigned long long)v->i);
			break;
		case ATTR_BOOL:
			printf("    %s: %s\n", names[i].name, v->b ? "true" : "false");
			break;
		default:
			break;
		}
	}
}

struct attr_wait {
	struct ssc_uid uid;
	ssc_attr_cb cb;
	void *ctx;
	int done;
};

static void on_attr_event(void *ctx, struct ssc_uid uid, uint32_t id, uint64_t ts, const uint8_t *b,
			  size_t l)
{
	struct attr_wait *w = ctx;

	(void)ts;
	if (uid.low != w->uid.low || uid.high != w->uid.high || id != SSC_MSG_ATTRIBUTES)
		return;
	ssc_parse_attrs(b, l, w->cb, w->ctx);
	w->done = 1;
}

static int get_attrs(struct client *c, struct ssc_uid uid, ssc_attr_cb cb, void *ctx)
{
	struct attr_wait w = { .uid = uid, .cb = cb, .ctx = ctx };
	uint8_t body[16];
	size_t n = ssc_body_attr(body, sizeof(body));
	int ret = client_request(c, uid, SSC_MSG_GET_ATTRIBUTES, body, n, true);

	return ret ? ret : client_pump(c, on_attr_event, &w, &w.done);
}

static int cmd_list(int wait_s)
{
	static const char *const types[] = {
		"registry", "accel", "gyro", "mag", "ambient_light", "proximity", "pressure", "rgb",
		"humidity", "ambient_temperature", "hall", "gravity", "gyro_cal", "mag_cal",
		"game_rv", "rotv", "device_orient", "sig_motion", "amd", "pedometer", "tilt",
	};
	struct client c;
	size_t i;
	int found = 0;
	int ret = client_open(&c, wait_s);

	if (ret) {
		fprintf(stderr, "SSC service %d not found: %s\n", SSC_SERVICE, strerror(-ret));
		return 1;
	}
	printf("SSC service at node %u port %u\n", c.node, c.port);
	c.timeout_ms = 1500;
	for (i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
		struct ssc_uid uids[MAX_UIDS];
		int n = client_suid(&c, types[i], false, uids, MAX_UIDS), j;

		if (n < 0) {
			printf("%s: lookup failed: %s\n", types[i], strerror(-n));
			continue;
		}
		if (!n) {
			printf("%s: none\n", types[i]);
			continue;
		}
		found += n;
		for (j = 0; j < n; j++) {
			printf("%s[%d]: uid %016llx%016llx\n", types[i], j,
			       (unsigned long long)uids[j].high, (unsigned long long)uids[j].low);
			ret = get_attrs(&c, uids[j], print_attr, NULL);
			if (ret)
				printf("    attributes: %s\n", strerror(-ret));
		}
	}
	client_close(&c);
	printf("%d sensors found\n", found);
	return found ? 0 : 2;
}

/* --- streaming --------------------------------------------------------- */

struct stream {
	struct ssc_uid uid;
	const char *type;
	int ui_fd;
	unsigned long count, limit;
	int done;
};

static void ui_event(int fd, unsigned type, unsigned code, int value)
{
	struct input_event e;

	memset(&e, 0, sizeof(e));
	e.type = (uint16_t)type;
	e.code = (uint16_t)code;
	e.value = value;
	if (write(fd, &e, sizeof(e)) < 0)
		perror("uinput write");
}

static int to_counts(float ms2)
{
	double v = ms2 * UINPUT_COUNTS_PER_G / G;

	if (v > UINPUT_RANGE)
		return UINPUT_RANGE;
	if (v < -UINPUT_RANGE)
		return -UINPUT_RANGE;
	return (int)(v < 0 ? v - 0.5 : v + 0.5);
}

static void on_sample(void *ctx, struct ssc_uid uid, uint32_t id, uint64_t ts, const uint8_t *b,
		      size_t l)
{
	struct stream *s = ctx;
	float v[8];
	int acc, n, i;

	if (uid.low != s->uid.low || uid.high != s->uid.high)
		return;
	if (id == SSC_MSG_SUID_RESP) { /* enable acknowledgement */
		printf("# enabled\n");
		return;
	}
	if (id != SSC_MSG_REPORT) {
		printf("# event %u, %zu bytes\n", id, l);
		return;
	}
	n = ssc_parse_sample(b, l, v, 8, &acc);
	if (n < 0) {
		printf("# malformed sample\n");
		return;
	}
	if (s->ui_fd >= 0) {
		if (n >= 3) {
			ui_event(s->ui_fd, EV_ABS, ABS_X, to_counts(v[0]));
			ui_event(s->ui_fd, EV_ABS, ABS_Y, to_counts(v[1]));
			ui_event(s->ui_fd, EV_ABS, ABS_Z, to_counts(v[2]));
			ui_event(s->ui_fd, EV_SYN, SYN_REPORT, 0);
		}
	} else {
		printf("%s t=%.6f", s->type, (double)ts / 19200000.0);
		for (i = 0; i < n; i++)
			printf(" %.4f", (double)v[i]);
		printf(" accuracy=%d\n", acc);
		fflush(stdout);
	}
	if (s->limit && ++s->count >= s->limit) {
		s->done = 1;
		stop_flag = 1;
	}
}

struct stream_attrs {
	int on_change;
	float max_rate, min_rate;
	int available;
};

static void read_stream_attrs(void *ctx, int id, const struct ssc_attr_value *v)
{
	struct stream_attrs *a = ctx;

	if (id == SSC_ATTR_STREAM_TYPE && v->kind == ATTR_INT)
		a->on_change = v->i == 1;
	else if (id == SSC_ATTR_AVAILABLE && v->kind == ATTR_BOOL)
		a->available = v->b;
	else if (id == SSC_ATTR_RATES && v->kind == ATTR_FLOAT) {
		if (!a->min_rate || v->f < a->min_rate)
			a->min_rate = v->f;
		if (v->f > a->max_rate)
			a->max_rate = v->f;
	}
}

static int run_stream(const char *type, float rate, unsigned long limit, int wait_s, int ui_fd,
		      unsigned delivery)
{
	struct client c;
	struct ssc_uid uids[MAX_UIDS];
	struct stream s = { .type = type, .ui_fd = ui_fd, .limit = limit };
	struct stream_attrs a = { .available = -1 };
	uint8_t body[32];
	size_t n;
	int ret, status = 1;

	ret = client_open(&c, wait_s);
	if (ret) {
		fprintf(stderr, "SSC service %d not found: %s\n", SSC_SERVICE, strerror(-ret));
		return 1;
	}
	c.delivery = delivery;
	ret = client_suid(&c, type, true, uids, MAX_UIDS);
	if (ret <= 0) {
		fprintf(stderr, "no %s sensor: %s\n", type, ret ? strerror(-ret) : "SUID returned none");
		goto out;
	}
	s.uid = uids[0];

	ret = get_attrs(&c, s.uid, read_stream_attrs, &a);
	if (ret)
		fprintf(stderr, "attributes: %s (carrying on)\n", strerror(-ret));
	if (a.available == 0)
		fprintf(stderr, "warning: sensor reports available=false\n");
	if (a.min_rate && rate < a.min_rate) {
		fprintf(stderr, "raising rate from %g to the sensor minimum %g Hz\n", rate, a.min_rate);
		rate = a.min_rate;
	}

	if (a.on_change) {
		ret = client_request(&c, s.uid, SSC_MSG_ENABLE_ON_CHANGE, NULL, 0, false);
	} else {
		n = ssc_body_rate(body, sizeof(body), rate);
		ret = client_request(&c, s.uid, SSC_MSG_ENABLE_CONTINUOUS, body, n, true);
	}
	if (ret) {
		fprintf(stderr, "enable %s: %s\n", type, strerror(-ret));
		goto out;
	}

	/* The SSC may take a moment to spin a sensor up; on-change can be quiet forever. */
	c.timeout_ms = a.on_change ? 86400000 : 10000;
	install_signals();
	ret = client_pump(&c, on_sample, &s, &stop_flag);
	if (ret)
		fprintf(stderr, "stream ended: %s\n", strerror(-ret));
	else
		status = 0;

	c.timeout_ms = 1000;
	client_request(&c, s.uid, SSC_MSG_DISABLE, NULL, 0, false);
out:
	client_close(&c);
	return status;
}

/* Keeps the sensor device stable across SSC restarts, so readers never lose it. */
static int cmd_uinput(float rate, int wait_s)
{
	struct uinput_setup setup;
	struct uinput_abs_setup abs;
	unsigned axes[] = { ABS_X, ABS_Y, ABS_Z };
	unsigned i;
	int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
	int ret;

	if (fd < 0) {
		fprintf(stderr, "/dev/uinput: %s\n", strerror(errno));
		return 1;
	}
	if (ioctl(fd, UI_SET_EVBIT, EV_ABS) || ioctl(fd, UI_SET_EVBIT, EV_SYN) ||
	    ioctl(fd, UI_SET_PROPBIT, INPUT_PROP_ACCELEROMETER)) {
		perror("uinput setup");
		return 1;
	}
	for (i = 0; i < 3; i++) {
		if (ioctl(fd, UI_SET_ABSBIT, axes[i])) {
			perror("uinput axis");
			return 1;
		}
		memset(&abs, 0, sizeof(abs));
		abs.code = (uint16_t)axes[i];
		abs.absinfo.minimum = -UINPUT_RANGE;
		abs.absinfo.maximum = UINPUT_RANGE;
		abs.absinfo.resolution = UINPUT_COUNTS_PER_G;
		if (ioctl(fd, UI_ABS_SETUP, &abs)) {
			perror("uinput abs");
			return 1;
		}
	}
	memset(&setup, 0, sizeof(setup));
	setup.id.bustype = BUS_VIRTUAL;
	setup.id.vendor = 0x1209;
	setup.id.product = 0x7970;
	setup.id.version = 1;
	snprintf(setup.name, sizeof(setup.name), UINPUT_NAME);
	if (ioctl(fd, UI_DEV_SETUP, &setup) || ioctl(fd, UI_DEV_CREATE)) {
		perror("uinput create");
		return 1;
	}
	printf("uinput device \"%s\" created\n", UINPUT_NAME);
	fflush(stdout);

	do {
		ret = run_stream("accel", rate, 0, wait_s, fd, SSC_DELIVERY_NO_WAKEUP);
		if (!stop_flag)
			sleep(3);
	} while (!stop_flag);
	ioctl(fd, UI_DEV_DESTROY);
	close(fd);
	return ret;
}

/* --- main -------------------------------------------------------------- */

static void usage(FILE *f)
{
	fprintf(f,
		"usage: gts7xl-sensord MODE [options]\n"
		"modes:\n"
		"  --trace-fastrpc DIR  serve DIR read-only, log every call\n"
		"  --serve DIR          serve DIR; only mnt/vendor/persist/sensors is writable\n"
		"  --list               print the sensors the SSC knows\n"
		"  --stream TYPE        print samples (accel, gyro; also mag, ambient_light)\n"
		"  --uinput             publish the accelerometer as an input device\n"
		"options:\n"
		"  --device PATH        FastRPC node (default " DEFAULT_DEVICE ")\n"
		"  --root-pd            attach to the DSP root PD, not the sensor PD\n"
		"  --prop KEY=VALUE     answer for sns_registry get_property (also DIR/props)\n"
		"  --rate HZ            stream rate (default 50; 20 with --uinput)\n"
		"  --count N            stop --stream after N samples\n"
		"  --wait S             seconds to wait for the SSC service (default 30)\n"
		"  --verbose            log every call in --serve\n");
}

int main(int argc, char **argv)
{
	enum { M_NONE, M_TRACE, M_SERVE, M_LIST, M_STREAM, M_UINPUT } mode = M_NONE;
	enum { O_DEVICE = 256, O_ROOT, O_PROP, O_RATE, O_COUNT, O_WAIT, O_TRACE, O_SERVE, O_LIST,
	       O_STREAM, O_UINPUT };
	static const struct option lo[] = {
		{ "trace-fastrpc", required_argument, NULL, O_TRACE },
		{ "serve", required_argument, NULL, O_SERVE },
		{ "list", no_argument, NULL, O_LIST },
		{ "stream", required_argument, NULL, O_STREAM },
		{ "uinput", no_argument, NULL, O_UINPUT },
		{ "device", required_argument, NULL, O_DEVICE },
		{ "root-pd", no_argument, NULL, O_ROOT },
		{ "prop", required_argument, NULL, O_PROP },
		{ "rate", required_argument, NULL, O_RATE },
		{ "count", required_argument, NULL, O_COUNT },
		{ "wait", required_argument, NULL, O_WAIT },
		{ "verbose", no_argument, NULL, 'v' },
		{ "help", no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 },
	};
	struct listener_prop props[FSRV_MAX_PROPS];
	struct listener_opts lopts = { .device = DEFAULT_DEVICE, .props = props };
	const char *arg = NULL;
	float rate = 0;
	unsigned long count = 0;
	int wait_s = 30, opt;

	setvbuf(stdout, NULL, _IOLBF, 0);
	while ((opt = getopt_long(argc, argv, "vh", lo, NULL)) != -1) {
		switch (opt) {
		case O_TRACE:
			mode = M_TRACE;
			arg = optarg;
			break;
		case O_SERVE:
			mode = M_SERVE;
			arg = optarg;
			break;
		case O_LIST:
			mode = M_LIST;
			break;
		case O_STREAM:
			mode = M_STREAM;
			arg = optarg;
			break;
		case O_UINPUT:
			mode = M_UINPUT;
			break;
		case O_DEVICE:
			lopts.device = optarg;
			break;
		case O_ROOT:
			lopts.root_pd = true;
			break;
		case O_PROP: {
			char *eq = strchr(optarg, '=');

			if (!eq || lopts.n_props == FSRV_MAX_PROPS) {
				fprintf(stderr, "bad --prop %s\n", optarg);
				return 2;
			}
			*eq = 0;
			props[lopts.n_props].k = optarg;
			props[lopts.n_props].v = eq + 1;
			lopts.n_props++;
			break;
		}
		case O_RATE:
			rate = strtof(optarg, NULL);
			break;
		case O_COUNT:
			count = strtoul(optarg, NULL, 10);
			break;
		case O_WAIT:
			wait_s = atoi(optarg);
			break;
		case 'v':
			lopts.verbose = true;
			break;
		case 'h':
			usage(stdout);
			return 0;
		default:
			usage(stderr);
			return 2;
		}
	}

	switch (mode) {
	case M_TRACE:
		lopts.dir = arg;
		lopts.verbose = true;
		return listener_run(&lopts);
	case M_SERVE:
		lopts.dir = arg;
		lopts.writable = true;
		return listener_run(&lopts);
	case M_LIST:
		return cmd_list(wait_s);
	case M_STREAM:
		return run_stream(arg, rate > 0 ? rate : 50, count, wait_s, -1, SSC_DELIVERY_WAKEUP);
	case M_UINPUT:
		install_signals();
		return cmd_uinput(rate > 0 ? rate : 20, wait_s);
	default:
		usage(stderr);
		return 2;
	}
}

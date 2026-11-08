// SPDX-License-Identifier: MIT
/*
 * gnss-hal: drive the vendor's legacy Android GPS HAL (gps.default.so) without
 * the Android framework. NMEA goes to stdout as it arrives; lines starting with
 * '#' are diagnostics. Runs in the bionic root built by gts7xl-gnss-extract.
 */
#include <stddef.h>
#include <stdint.h>

int printf(const char *, ...);
int fflush(void *);
extern void *stdout;
unsigned int sleep(unsigned int);
void *dlopen(const char *, int);
void *dlsym(void *, const char *);
char *dlerror(void);
typedef unsigned long pthread_t;
int pthread_create(pthread_t *, const void *, void *(*)(void *), void *);

/* hardware/libhardware/include/hardware/hardware.h */
struct hw_module_t;
struct hw_device_t;
struct hw_module_methods_t {
	int (*open)(const struct hw_module_t *, const char *, struct hw_device_t **);
};
struct hw_module_t {
	uint32_t tag;
	uint16_t module_api_version, hal_api_version;
	const char *id, *name, *author;
	struct hw_module_methods_t *methods;
	void *dso;
	uint64_t reserved[32 - 7];
};
struct hw_device_t {
	uint32_t tag, version;
	struct hw_module_t *module;
	uint64_t reserved[12];
	int (*close)(struct hw_device_t *);
};

/* hardware/libhardware/include/hardware/gps.h */
typedef int64_t GpsUtcTime;
typedef struct {
	size_t size;
	uint16_t flags;
	double latitude, longitude, altitude;
	float speed, bearing, accuracy;
	GpsUtcTime timestamp;
} GpsLocation;
typedef struct {
	size_t size;
	void (*location_cb)(GpsLocation *);
	void (*status_cb)(void *);
	void (*sv_status_cb)(void *);
	void (*nmea_cb)(GpsUtcTime, const char *, int);
	void (*set_capabilities_cb)(uint32_t);
	void (*acquire_wakelock_cb)(void);
	void (*release_wakelock_cb)(void);
	pthread_t (*create_thread_cb)(const char *, void (*)(void *), void *);
	void (*request_utc_time_cb)(void);
	void (*set_system_info_cb)(const void *);
	void (*gnss_sv_status_cb)(void *);
} GpsCallbacks;
typedef struct {
	size_t size;
	int (*init)(GpsCallbacks *);
	int (*start)(void);
	int (*stop)(void);
	void (*cleanup)(void);
	int (*inject_time)(GpsUtcTime, int64_t, int);
	int (*inject_location)(double, double, float);
	void (*delete_aiding_data)(uint16_t);
	int (*set_position_mode)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
	const void *(*get_extension)(const char *);
} GpsInterface;
struct gps_device_t {
	struct hw_device_t common;
	const GpsInterface *(*get_gps_interface)(struct gps_device_t *);
};

/* gps.h extensions, initialized by the real HAL right after gps_init */
/* AOSP gps.h: the AGPS callback structs have no size field, unlike their interfaces */
typedef struct { void (*status_cb)(void *); pthread_t (*create_thread_cb)(const char *, void (*)(void *), void *); } AGpsCallbacks;
typedef struct { size_t size; void (*init)(AGpsCallbacks *); } AGpsInterface;
typedef struct { void (*request_setid)(uint32_t); void (*request_refloc)(uint32_t); pthread_t (*create_thread_cb)(const char *, void (*)(void *), void *); } AGpsRilCallbacks;
typedef struct { size_t size; void (*init)(AGpsRilCallbacks *); } AGpsRilInterface;
typedef struct { void (*download_request_cb)(void); pthread_t (*create_thread_cb)(const char *, void (*)(void *), void *); } GpsXtraCallbacks;
typedef struct { size_t size; int (*init)(GpsXtraCallbacks *); } GpsXtraInterface;
typedef struct { void (*notify_cb)(void *); pthread_t (*create_thread_cb)(const char *, void (*)(void *), void *); } GpsNiCallbacks;
typedef struct { size_t size; void (*init)(GpsNiCallbacks *); } GpsNiInterface;
typedef struct { void (*transition)(void); void (*status)(void); void (*add)(void); void (*remove)(void); void (*pause)(void); void (*resume)(void); pthread_t (*create_thread_cb)(const char *, void (*)(void *), void *); } GpsGeofenceCallbacks;
typedef struct { size_t size; void (*init)(GpsGeofenceCallbacks *); } GpsGeofencingInterface;
typedef struct { size_t size; void (*configuration_update)(const char *, int32_t); } GnssConfigurationInterface;

static void on_location(GpsLocation *l)
{
	printf("#FIX %.7f %.7f alt %.1f acc %.1f\n", l->latitude, l->longitude,
	       l->altitude, l->accuracy);
	fflush(stdout);
}
static void on_nmea(GpsUtcTime t, const char *s, int n)
{
	printf("%.*s", n, s);
	if (n && s[n - 1] != '\n')
		printf("\n");
	fflush(stdout);
}
static void on_status(void *p) { printf("#STATUS %u\n", p ? *(uint16_t *)((char *)p + sizeof(size_t)) : 0); fflush(stdout); }
static void on_nop(void *p) { }
static void on_caps(uint32_t c) { printf("#CAPS 0x%x\n", c); fflush(stdout); }
static void on_void(void) { }
static void on_sysinfo(const void *p) { }

struct tramp { void (*fn)(void *); void *arg; };
static struct tramp tramps[16];
static int ntramps;
static void *run_tramp(void *p) { struct tramp *t = p; t->fn(t->arg); return 0; }
static pthread_t on_thread(const char *name, void (*fn)(void *), void *arg)
{
	pthread_t th = 0;
	struct tramp *t = &tramps[ntramps++ & 15];
	t->fn = fn;
	t->arg = arg;
	pthread_create(&th, 0, run_tramp, t);
	printf("#THREAD %s\n", name);
	fflush(stdout);
	return th;
}

static GpsCallbacks cb = {
	.size = sizeof(GpsCallbacks),
	.location_cb = on_location,
	.status_cb = on_status,
	.sv_status_cb = on_nop,
	.nmea_cb = on_nmea,
	.set_capabilities_cb = on_caps,
	.acquire_wakelock_cb = on_void,
	.release_wakelock_cb = on_void,
	.create_thread_cb = on_thread,
	.request_utc_time_cb = on_void,
	.set_system_info_cb = on_sysinfo,
	.gnss_sv_status_cb = on_nop,
};

static void on_u32(uint32_t v) { }

static void init_extensions(const GpsInterface *gps)
{
	static AGpsCallbacks agps_cb = { on_nop, on_thread };
	static AGpsRilCallbacks ril_cb = { on_u32, on_u32, on_thread };
	static GpsXtraCallbacks xtra_cb = { on_void, on_thread };
	static GpsNiCallbacks ni_cb = { on_nop, on_thread };
	static GpsGeofenceCallbacks geo_cb = { on_void, on_void, on_void, on_void, on_void, on_void, on_thread };
	/* What Samsung's HAL sends on this tablet (gpsd log: AGPS_TYPE=3) */
	static const char conf[] = "AGPS_TYPE=3\n";
	const GpsXtraInterface *xtra = gps->get_extension("gps-xtra");
	const GpsNiInterface *ni = gps->get_extension("gps-ni");
	const GpsGeofencingInterface *geo = gps->get_extension("gps_geofencing");
	const AGpsInterface *agps = gps->get_extension("agps");
	const AGpsRilInterface *ril = gps->get_extension("agps_ril");
	const GnssConfigurationInterface *cfg = gps->get_extension("gnss_configuration");

	printf("#ext xtra=%p ni=%p geo=%p agps=%p ril=%p cfg=%p\n", xtra, ni, geo, agps, ril, cfg);
	if (xtra) xtra->init(&xtra_cb);
	if (ni) ni->init(&ni_cb);
	if (geo) geo->init(&geo_cb);
	if (agps) agps->init(&agps_cb);
	if (ril) ril->init(&ril_cb);
	if (cfg) cfg->configuration_update(conf, sizeof(conf) - 1);
	fflush(stdout);
}

int main(int argc, char **argv)
{
	const char *path = argc > 1 ? argv[1] : "/vendor/lib64/hw/gps.default.so";
	void *h = dlopen(path, 2 /* RTLD_NOW */);
	struct hw_module_t *m;
	struct gps_device_t *dev = 0;
	const GpsInterface *gps;
	int ret;

	if (!h) { printf("#ERR dlopen: %s\n", dlerror()); return 1; }
	m = dlsym(h, "HMI");
	if (!m) { printf("#ERR no HMI\n"); return 1; }
	printf("#MODULE %s / %s / %s\n", m->id, m->name, m->author);
	ret = m->methods->open(m, "gps", (struct hw_device_t **)&dev);
	if (ret || !dev) { printf("#ERR open %d\n", ret); return 1; }
	gps = dev->get_gps_interface(dev);
	if (!gps) { printf("#ERR no interface\n"); return 1; }
	printf("#IFACE size %zu\n", gps->size);
	fflush(stdout);
	printf("#init %d\n", gps->init(&cb));
	fflush(stdout);
	init_extensions(gps);
	sleep(3);	/* the HAL's IPC link to gpsd comes up asynchronously */
	printf("#mode %d\n", gps->set_position_mode(0 /* standalone */, 0 /* periodic */, 1000, 0, 0));
	printf("#start %d\n", gps->start());
	fflush(stdout);
	for (;;)
		sleep(60);
}

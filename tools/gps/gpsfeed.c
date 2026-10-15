/*
 * gpsfeed: drive the stock MTK legacy GPS HAL (gps.default.so) against a running
 * mnld inside the camroot and print the NMEA it delivers on stdout.
 *   gpsfeed [seconds]      (default: until SIGTERM/SIGINT, then stops the engine)
 * Built like camshot: gcc, hand-declared prototypes, own _start -> bionic __libc_init.
 * GpsCallbacks is the 15-slot vendor variant (size 0x78, checked by the HAL).
 */
typedef unsigned long size_t;
typedef int int32_t;
typedef unsigned int uint32_t;
typedef long int64_t;
typedef unsigned char uint8_t;

int dprintf(int fd, const char *format, ...);
long write(int fd, const void *buffer, size_t count);
unsigned int sleep(unsigned int sec);
void (*signal(int sig, void (*handler)(int)))(int);
int atoi(const char *string);
void *dlopen(const char *file, int flags);
void *dlsym(void *handle, const char *name);
char *dlerror(void);
int pthread_create(unsigned long *thread, const void *attr, void *(*fn)(void *), void *arg);
int pthread_detach(unsigned long thread);

typedef struct {
	void *preinit_array, *init_array, *fini_array;
	void *preinit_array_count, *init_array_count;
} structors_array_t_unused;
typedef struct {
	void (**preinit_array)(void);
	void (**init_array)(void);
	void (**fini_array)(void);
	void *unused;
} structors_array_t;
void __libc_init(void *raw_args, void (*onexit)(void), int (*slingshot)(int, char **, char **),
	const structors_array_t *structors) __attribute__((noreturn));
int main(int argc, char **argv, char **envp);

static void (*fini_list[])(void) = { (void (*)(void))-1, 0 };

__attribute__((used, visibility("hidden"))) void _start_main(void *raw_args)
{
	static const structors_array_t structors = { 0, 0, fini_list };
	__libc_init(raw_args, 0, main, &structors);
}

__asm__(".globl _start\n_start:\n\tmov x0, sp\n\tb _start_main\n");

/* ------------------------------------------------------------------------ */

typedef struct {
	size_t size;
	void *slot[14];
} GpsCallbacks;

typedef struct {
	size_t size;
	int (*init)(GpsCallbacks *cbs);
	int (*start)(void);
	int (*stop)(void);
	void (*cleanup)(void);
	int (*inject_time)(int64_t time, int64_t ref, int uncertainty);
	int (*inject_location)(double lat, double lon, float acc);
	void (*delete_aiding_data)(uint32_t flags);
	int (*set_position_mode)(uint32_t mode, uint32_t recurrence, uint32_t min_interval,
		uint32_t preferred_accuracy, uint32_t preferred_time);
	const void *(*get_extension)(const char *name);
} GpsInterface;

typedef struct {
	uint32_t tag, version;
	void *module;
	uint32_t reserved[12];
	int (*close)(void *);
	const GpsInterface *(*get_gps_interface)(void *);
} gps_device_t;

typedef struct {
	uint32_t tag;
	uint32_t api;
	const char *id, *name, *author;
	struct { int (*open)(const void *module, const char *id, gps_device_t **device); } *methods;
} hw_module_t;

static volatile int quit;
static void on_signal(int sig) { quit = 1; }

static void cb_nmea(int64_t ts, const char *nmea, int len)
{
	write(1, nmea, len);
}

static void cb_status(void *s) { }
static void cb_location(void *l) { }
static void cb_sv(void *s) { }
static void cb_caps(uint32_t caps) { dprintf(2, "capabilities 0x%x\n", caps); }
static void cb_wake(void) { }
static void cb_utc(void) { }
static void cb_noop(void) { }
static void cb_name(const char *name, size_t len) { dprintf(2, "gnss name %s\n", name); }

static unsigned long create_thread(const char *name, void (*fn)(void *), void *arg)
{
	unsigned long t;
	if (pthread_create(&t, 0, (void *(*)(void *))fn, arg))
		return 0;
	pthread_detach(t);
	return t;
}

int main(int argc, char **argv, char **envp)
{
	int secs = argc > 1 ? atoi(argv[1]) : 0;
	void *lib = dlopen("/vendor/lib64/hw/gps.default.so", 2);
	if (!lib) { dprintf(2, "dlopen: %s\n", dlerror()); return 1; }
	hw_module_t *mod = dlsym(lib, "HMI");
	if (!mod) { dprintf(2, "no HMI\n"); return 1; }
	gps_device_t *dev = 0;
	int r = mod->methods->open(mod, "gps", &dev);
	if (r || !dev) { dprintf(2, "open failed %d\n", r); return 1; }
	const GpsInterface *g = ((const GpsInterface *(*)(void *))((unsigned long *)dev)[15])(dev);
	if (!g) { dprintf(2, "no interface\n"); return 1; }
	static GpsCallbacks cbs;
	cbs.size = 0x78;
	for (int i = 0; i < 14; i++)
		cbs.slot[i] = (void *)cb_noop;
	cbs.slot[0] = (void *)cb_location;
	cbs.slot[1] = (void *)cb_status;
	cbs.slot[2] = (void *)cb_sv;
	cbs.slot[3] = (void *)cb_nmea;
	cbs.slot[4] = (void *)cb_caps;
	cbs.slot[5] = (void *)cb_wake;
	cbs.slot[6] = (void *)cb_wake;
	cbs.slot[7] = (void *)create_thread;
	cbs.slot[8] = (void *)cb_utc;
	cbs.slot[11] = (void *)cb_name;
	dprintf(2, "init -> %d\n", g->init(&cbs));
	dprintf(2, "set_position_mode -> %d\n", g->set_position_mode(1, 0, 1000, 0, 0));
	dprintf(2, "start -> %d\n", g->start());
	signal(15, on_signal);
	signal(2, on_signal);
	for (int i = 0; !quit && (!secs || i < secs); i++)
		sleep(1);
	g->stop();
	g->cleanup();
	return 0;
}

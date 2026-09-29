/*
 * camproxy name=descriptor...: stand-ins for system_server services that
 * cameraserver blocks on or reads policy from. Every method they have that
 * returns something wants 0 here (ICameraServiceProxy: isCameraDisabled=false,
 * rotate-and-crop NONE, autoframing OFF; ISensorPrivacyManager: privacy off,
 * empty allowlist), so each transaction is answered with status OK + int32 0.
 * Built like camshot: gcc against bionic + libbinder_ndk, no NDK headers.
 */
typedef unsigned int uint32_t;
typedef int int32_t;
typedef struct AIBinder AIBinder;
typedef struct AIBinder_Class AIBinder_Class;
typedef struct AParcel AParcel;

int dprintf(int fd, const char *format, ...);

AIBinder_Class *AIBinder_Class_define(const char *descriptor, void *(*on_create)(void *args),
	void (*on_destroy)(void *user_data),
	int32_t (*on_transact)(AIBinder *binder, uint32_t code, const AParcel *in, AParcel *out));
AIBinder *AIBinder_new(const AIBinder_Class *binder_class, void *args);
int32_t AServiceManager_addService(AIBinder *binder, const char *instance);
void ABinderProcess_setThreadPoolMaxThreadCount(uint32_t count);
void ABinderProcess_joinThreadPool(void);
int32_t AParcel_writeInt32(AParcel *parcel, int32_t value);
void *AIBinder_getUserData(AIBinder *binder);
int32_t AParcel_readInt32(const AParcel *parcel, int32_t *value);
void AIBinder_markVintfStability(AIBinder *binder);

/* bionic entry point, as in camshot.c */
typedef struct {
	void (**preinit_array)(void);
	void (**init_array)(void);
	void (**fini_array)(void);
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

char *strchr(const char *string, int character);
int strcmp(const char *left, const char *right);

static void *on_create(void *args)
{
	return args;
}

static void on_destroy(void *user_data)
{
	(void)user_data;
}

/* android.os.IProcessInfoService ("processinfo", hand-written parcel format in
 * libbinder): 1 = getProcessStatesFromPids(int[] pids, out int[] states),
 * 2 = getProcessStatesAndOomScoresFromPids(int[] pids, out int[] states, out int[] scores).
 * Every pid is reported as PROCESS_STATE_TOP (2) with oom score 0. */
static int32_t process_info(uint32_t code, const AParcel *in, AParcel *out)
{
	int32_t count = 0, pid;
	AParcel_readInt32(in, &count);
	for (int32_t index = 0; index < count; index++)
		AParcel_readInt32(in, &pid);
	AParcel_writeInt32(out, 0);
	AParcel_writeInt32(out, count);
	for (int32_t index = 0; index < count; index++)
		AParcel_writeInt32(out, 2);
	if (code == 2) {
		AParcel_writeInt32(out, count);
		for (int32_t index = 0; index < count; index++)
			AParcel_writeInt32(out, 0);
	}
	return 0;
}

static int32_t on_transact(AIBinder *binder, uint32_t code, const AParcel *in, AParcel *out)
{
	const char *name = AIBinder_getUserData(binder);
	dprintf(2, "camproxy: %s transaction %u\n", name, code);
	if (strcmp(name, "processinfo") == 0)
		return process_info(code, in, out);
	AParcel_writeInt32(out, 0); /* status header: EX_NONE */
	AParcel_writeInt32(out, 0); /* return value */
	return 0;
}

int main(int argc, char **argv, char **envp)
{
	(void)envp;
	for (int index = 1; index < argc; index++) {
		char *descriptor = strchr(argv[index], '=');
		if (!descriptor)
			continue;
		*descriptor++ = '\0';
		AIBinder_Class *stub_class = AIBinder_Class_define(descriptor,
			on_create, on_destroy, on_transact);
		AIBinder *binder = AIBinder_new(stub_class, argv[index]);
		/* "pkg.IFoo/instance" names are VINTF services that vendor processes call */
		if (strchr(argv[index], '/'))
			AIBinder_markVintfStability(binder);
		int32_t status = AServiceManager_addService(binder, argv[index]);
		dprintf(2, "camproxy: %s (%s): %d\n", argv[index], descriptor, status);
		if (status)
			return 1;
	}
	ABinderProcess_setThreadPoolMaxThreadCount(0);
	ABinderProcess_joinThreadPool();
	return 0;
}

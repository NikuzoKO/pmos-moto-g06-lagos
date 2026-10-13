/*
 * camstream: stream a camera's preview through the stock Android NDK camera API
 * (cameraserver + MTK camera HAL in the camroot) into a shared file, for
 * Android Translation Layer's android.hardware.Camera (lagos-camera starts it).
 *   camstream <camera-id> <width> <height> <out-file>
 * out-file = 32-byte header + one NV21 frame, written under a seqlock:
 *   u32 magic "LCAM", version 1, width, height, format 17 (NV21),
 *   seq (odd while a frame is being written), frame size, frames written.
 * Readers copy the frame while seq is even and unchanged across the copy.
 * Runs until killed or the camera goes away (exit 1). Same build as camshot.
 *
 * Control channel (pro camera): <out-file>.ctl is a FIFO read as text lines,
 * <out-file>.ev is truncated at start and receives one reply line per event.
 *   set key=value ...   change the live preview request (and later shots)
 *   shot <path> [key=value ...]   still JPEG to <path> with one-off overrides
 *   af                  one autofocus run (AF mode auto + trigger)
 *   reset               back to auto exposure / continuous AF / auto WB
 * keys: ae=auto|manual  shutter=<seconds>  iso=<n>  ev=<half stops, -8..8>
 *       focus=auto|cont|<diopters>  awb=<0-8>  wb=<r>,<b> (manual gains)
 *       q=<jpeg quality>  nr=<0-4>  edge=<0-3>
 *       zoom=<1..max digital zoom> (centre crop)  flash=off|auto|on|torch
 * events: "ready <maxjpeg w> <h>", "ok set", "ok shot <path> <bytes>",
 *         "err <what>". Preview exposure is capped at 100 ms (frame rate);
 *         the still gets the full shutter time.
 */
typedef unsigned long size_t;
typedef int int32_t;
typedef unsigned int uint32_t;
typedef long int64_t;
typedef unsigned char uint8_t;

int dprintf(int fd, const char *format, ...);
int open(const char *path, int flags, ...);
long write(int fd, const void *buffer, size_t count);
int close(int fd);
int usleep(unsigned int usec);
int unlink(const char *path);
int strncmp(const char *left, const char *right, size_t count);
void *memmove(void *destination, const void *source, size_t count);
int atoi(const char *string);
int strcmp(const char *left, const char *right);
void *mmap(void *address, size_t length, int protection, int flags, int fd, long offset);
int ftruncate(int fd, long length);
void *memcpy(void *destination, const void *source, size_t count);
void exit(int status) __attribute__((noreturn));

/* --- NDK camera / media declarations (camera/Ndk*.h, media/NdkImage*.h) --- */
typedef struct ACameraManager ACameraManager;
typedef struct ACameraDevice ACameraDevice;
typedef struct ACameraMetadata ACameraMetadata;
typedef struct ACaptureRequest ACaptureRequest;
typedef struct ACameraCaptureSession ACameraCaptureSession;
typedef struct ACaptureSessionOutput ACaptureSessionOutput;
typedef struct ACaptureSessionOutputContainer ACaptureSessionOutputContainer;
typedef struct ACameraOutputTarget ACameraOutputTarget;
typedef struct ANativeWindow ANativeWindow;
typedef struct AImageReader AImageReader;
typedef struct AImage AImage;

typedef struct {
	int numCameras;
	const char **cameraIds;
} ACameraIdList;

typedef struct {
	void *context;
	void (*onDisconnected)(void *context, ACameraDevice *device);
	void (*onError)(void *context, ACameraDevice *device, int error);
} ACameraDevice_StateCallbacks;

typedef struct {
	void *context;
	void (*onClosed)(void *context, ACameraCaptureSession *session);
	void (*onReady)(void *context, ACameraCaptureSession *session);
	void (*onActive)(void *context, ACameraCaptureSession *session);
} ACameraCaptureSession_stateCallbacks;

typedef struct {
	void *context;
	void (*onImageAvailable)(void *context, AImageReader *reader);
} AImageReader_ImageListener;

typedef struct {
	uint32_t tag;
	uint8_t type;
	uint32_t count;
	union {
		const uint8_t *u8;
		const int32_t *i32;
	} data;
} ACameraMetadata_const_entry;

#define ACAMERA_LENS_FACING 0x80005
#define ACAMERA_SCALER_AVAILABLE_STREAM_CONFIGURATIONS 0xd000a
#define AIMAGE_FORMAT_JPEG 0x100
#define AIMAGE_FORMAT_YUV_420_888 0x23
#define TEMPLATE_PREVIEW 1
#define TEMPLATE_STILL_CAPTURE 2

ACameraManager *ACameraManager_create(void);
int ACameraManager_getCameraIdList(ACameraManager *manager, ACameraIdList **list);
int ACameraManager_getCameraCharacteristics(ACameraManager *manager, const char *id,
	ACameraMetadata **characteristics);
int ACameraManager_openCamera(ACameraManager *manager, const char *id,
	ACameraDevice_StateCallbacks *callbacks, ACameraDevice **device);
int ACameraMetadata_getConstEntry(const ACameraMetadata *metadata, uint32_t tag,
	ACameraMetadata_const_entry *entry);
int ACameraDevice_createCaptureRequest(const ACameraDevice *device, int templateId,
	ACaptureRequest **request);
int ACameraDevice_createCaptureSession(ACameraDevice *device,
	const ACaptureSessionOutputContainer *outputs,
	const ACameraCaptureSession_stateCallbacks *callbacks, ACameraCaptureSession **session);
int ACameraDevice_close(ACameraDevice *device);
int ACaptureSessionOutputContainer_create(ACaptureSessionOutputContainer **container);
int ACaptureSessionOutputContainer_add(ACaptureSessionOutputContainer *container,
	const ACaptureSessionOutput *output);
int ACaptureSessionOutput_create(ANativeWindow *window, ACaptureSessionOutput **output);
int ACameraOutputTarget_create(ANativeWindow *window, ACameraOutputTarget **target);
int ACaptureRequest_addTarget(ACaptureRequest *request, const ACameraOutputTarget *target);
int ACameraCaptureSession_setRepeatingRequest(ACameraCaptureSession *session, void *callbacks,
	int count, ACaptureRequest **requests, int *sequenceId);
int ACameraCaptureSession_stopRepeating(ACameraCaptureSession *session);
int ACameraCaptureSession_capture(ACameraCaptureSession *session, void *callbacks,
	int count, ACaptureRequest **requests, int *sequenceId);
void ACameraCaptureSession_close(ACameraCaptureSession *session);

int AImageReader_new(int32_t width, int32_t height, int32_t format, int32_t maxImages,
	AImageReader **reader);
int AImageReader_getWindow(AImageReader *reader, ANativeWindow **window);
int AImageReader_setImageListener(AImageReader *reader, AImageReader_ImageListener *listener);
int AImageReader_acquireLatestImage(AImageReader *reader, AImage **image);
int AImage_getPlaneData(const AImage *image, int planeIdx, uint8_t **data, int *length);
void AImage_delete(AImage *image);
int AImage_getPlaneRowStride(const AImage *image, int planeIdx, int32_t *rowStride);
int AImage_getPlanePixelStride(const AImage *image, int planeIdx, int32_t *pixelStride);

int ACaptureRequest_setEntry_u8(ACaptureRequest *request, uint32_t tag, uint32_t count, const uint8_t *data);
int ACaptureRequest_setEntry_i32(ACaptureRequest *request, uint32_t tag, uint32_t count, const int32_t *data);
int ACaptureRequest_setEntry_i64(ACaptureRequest *request, uint32_t tag, uint32_t count, const int64_t *data);
int ACaptureRequest_setEntry_float(ACaptureRequest *request, uint32_t tag, uint32_t count, const float *data);
void ACaptureRequest_free(ACaptureRequest *request);
int mkfifo(const char *path, unsigned int mode);
long read(int fd, void *buffer, size_t count);
double strtod(const char *string, char **end);
long strtol(const char *string, char **end, int base);
int snprintf(char *buffer, size_t size, const char *format, ...);
size_t strlen(const char *string);
char *strchr(const char *string, int c);
int chmod(const char *path, unsigned int mode);

#define ACAMERA_CONTROL_AE_EXPOSURE_COMPENSATION 0x10001
#define ACAMERA_CONTROL_AE_MODE 0x10003
#define ACAMERA_CONTROL_AF_MODE 0x10007
#define ACAMERA_CONTROL_AF_TRIGGER 0x10009
#define ACAMERA_CONTROL_AWB_MODE 0x1000b
#define ACAMERA_COLOR_CORRECTION_MODE 0x00000
#define ACAMERA_COLOR_CORRECTION_GAINS 0x00002
#define ACAMERA_EDGE_MODE 0x30000
#define ACAMERA_JPEG_QUALITY 0x70004
#define ACAMERA_LENS_FOCUS_DISTANCE 0x80003
#define ACAMERA_NOISE_REDUCTION_MODE 0xa0000
#define ACAMERA_SENSOR_EXPOSURE_TIME 0xe0000
#define ACAMERA_SENSOR_FRAME_DURATION 0xe0001
#define ACAMERA_SENSOR_SENSITIVITY 0xe0002
#define ACAMERA_SCALER_CROP_REGION 0xd0005
#define ACAMERA_FLASH_MODE 0x40002
#define ACAMERA_CONTROL_ZOOM_RATIO 0x1002f
#define ACAMERA_SENSOR_INFO_ACTIVE_ARRAY_SIZE 0xf0000

/* cameraserver dequeues our ImageReader buffers through binder calls into this
 * process: without a binder thread pool nobody answers them */
void ABinderProcess_startThreadPool(void);

/* --- bionic entry point: what crtbegin_dynamic.o would provide --- */
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

/* ------------------------------------------------------------------------ */

#define O_RDWR 02
#define O_CREAT 0100
#define O_TRUNC 01000
#define PROT_READ 1
#define PROT_WRITE 2
#define MAP_SHARED 1
#define HEADER 32

struct header {
	uint32_t magic, version, width, height, format;
	volatile uint32_t seq;
	uint32_t size, frames;
};

static struct header *shared;
static uint8_t *frame;
static int width, height;

static void on_disconnected(void *context, ACameraDevice *device)
{
	(void)context, (void)device;
	dprintf(2, "camstream: device disconnected\n");
	exit(1);
}

static void on_error(void *context, ACameraDevice *device, int error)
{
	(void)context, (void)device;
	dprintf(2, "camstream: device error %d\n", error);
	exit(1);
}

static void on_session_event(void *context, ACameraCaptureSession *session)
{
	(void)session;
	dprintf(2, "camstream: session %s\n", (const char *)context);
}

/* YUV_420_888 (any strides) -> NV21: full Y plane, then interleaved V,U at half resolution */
static void on_preview(void *context, AImageReader *reader)
{
	AImage *image;
	uint8_t *y, *u, *v;
	int y_length, u_length, v_length;
	int32_t y_row, u_row, v_row, u_pixel, v_pixel;
	(void)context;
	if (AImageReader_acquireLatestImage(reader, &image) != 0)
		return;
	if (AImage_getPlaneData(image, 0, &y, &y_length) || AImage_getPlaneData(image, 1, &u, &u_length) ||
			AImage_getPlaneData(image, 2, &v, &v_length) ||
			AImage_getPlaneRowStride(image, 0, &y_row) || AImage_getPlaneRowStride(image, 1, &u_row) ||
			AImage_getPlaneRowStride(image, 2, &v_row) || AImage_getPlanePixelStride(image, 1, &u_pixel) ||
			AImage_getPlanePixelStride(image, 2, &v_pixel)) {
		AImage_delete(image);
		return;
	}
	__atomic_store_n(&shared->seq, shared->seq + 1, __ATOMIC_RELEASE); /* odd: writing */
	__atomic_thread_fence(__ATOMIC_SEQ_CST);
	for (int row = 0; row < height; row++)
		memcpy(frame + row * width, y + row * y_row, width);
	uint8_t *vu = frame + width * height;
	for (int row = 0; row < height / 2; row++)
		for (int column = 0; column < width / 2; column++) {
			*vu++ = v[row * v_row + column * v_pixel];
			*vu++ = u[row * u_row + column * u_pixel];
		}
	shared->frames++;
	__atomic_thread_fence(__ATOMIC_SEQ_CST);
	__atomic_store_n(&shared->seq, shared->seq + 1, __ATOMIC_RELEASE); /* even: complete */
	AImage_delete(image);
}

/* largest output size the camera offers for a format */
static int largest_size(ACameraMetadata *characteristics, int format, int *width, int *height)
{
	ACameraMetadata_const_entry entry;
	*width = *height = 0;
	if (ACameraMetadata_getConstEntry(characteristics,
			ACAMERA_SCALER_AVAILABLE_STREAM_CONFIGURATIONS, &entry) != 0)
		return -1;
	for (uint32_t index = 0; index + 3 < entry.count; index += 4) {
		const int32_t *config = entry.data.i32 + index;
		if (config[0] == format && !config[3] && config[1] * config[2] > *width * *height) {
			*width = config[1];
			*height = config[2];
		}
	}
	return *width ? 0 : -1;
}

#define CHECK(call) do { int status_ = (call); if (status_) { \
	dprintf(2, "camstream: %s failed: %d\n", #call, status_); return 1; } } while (0)

#define O_WRONLY 01
#define O_NONBLOCK 04000
#define O_APPEND 02000

/* --- pro controls --- */
struct params {
	int ae_manual;
	double shutter;       /* seconds */
	int iso;
	int ev;               /* half stops */
	int focus_mode;       /* 0 manual, 1 auto, 3 continuous picture */
	double focus;         /* diopters (manual) */
	int awb;              /* 0-8; 0 with wb_manual = manual gains */
	int wb_manual;
	double wb_r, wb_b;
	int quality, nr, edge;
	double zoom;
	int flash;            /* 0 off, 1 auto, 2 on, 3 torch */
};

static const struct params defaults = { 0, 0.01, 400, 0, 3, 0.0, 1, 0, 1.0, 1.0, 95, -1, -1, 1.0, 0 };
static struct params current;
static int32_t array_size[4]; /* active array: left, top, width, height */
static int event_fd = -1;
static int jpeg_done, jpeg_bytes;
static const char *jpeg_path;

static void event(const char *format, const char *a, int b, int c)
{
	char line[256];
	int n = snprintf(line, sizeof line, format, a, b, c);
	write(event_fd, line, n);
}

static void on_jpeg(void *context, AImageReader *reader)
{
	AImage *image;
	uint8_t *data;
	int length;
	(void)context;
	if (AImageReader_acquireLatestImage(reader, &image) != 0)
		return;
	if (AImage_getPlaneData(image, 0, &data, &length) == 0 && jpeg_path) {
		/* the blob buffer is padded; the JPEG ends at the last FFD9 */
		int end = length;
		for (int index = length - 2; index > 1; index--)
			if (data[index] == 0xff && data[index + 1] == 0xd9) {
				end = index + 2;
				break;
			}
		int out = open(jpeg_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
		if (out >= 0) {
			write(out, data, end);
			close(out);
			chmod(jpeg_path, 0666);
			jpeg_bytes = end;
		}
	}
	AImage_delete(image);
	jpeg_done = 1;
}

static int64_t to_ns(double seconds)
{
	return (int64_t)(seconds * 1e9);
}

/* write the settings into a request; preview=1 caps the exposure for frame rate */
static void apply(ACaptureRequest *request, const struct params *p, int preview)
{
	uint8_t u8;
	int32_t i32;
	int64_t i64;
	float f;

	u8 = p->ae_manual ? 0 : 1;
	if (!preview && !p->ae_manual && (p->flash == 1 || p->flash == 2))
		u8 = p->flash == 1 ? 2 : 3; /* ON_AUTO_FLASH / ON_ALWAYS_FLASH, stills only */
	ACaptureRequest_setEntry_u8(request, ACAMERA_CONTROL_AE_MODE, 1, &u8);
	u8 = 0;
	if (p->flash == 3)
		u8 = 2; /* TORCH */
	else if (!preview && p->ae_manual && p->flash == 2)
		u8 = 1; /* SINGLE: with AE off the flash is requested directly */
	ACaptureRequest_setEntry_u8(request, ACAMERA_FLASH_MODE, 1, &u8);
	if (p->zoom > 1.01) {
		f = p->zoom;
		ACaptureRequest_setEntry_float(request, ACAMERA_CONTROL_ZOOM_RATIO, 1, &f);
	}
	if (p->ae_manual) {
		double shutter = p->shutter;
		if (preview && shutter > 0.1)
			shutter = 0.1;
		i64 = to_ns(shutter);
		ACaptureRequest_setEntry_i64(request, ACAMERA_SENSOR_EXPOSURE_TIME, 1, &i64);
		i64 = to_ns(shutter) + 1000000;
		if (i64 < 33333333)
			i64 = 33333333;
		ACaptureRequest_setEntry_i64(request, ACAMERA_SENSOR_FRAME_DURATION, 1, &i64);
		i32 = p->iso;
		ACaptureRequest_setEntry_i32(request, ACAMERA_SENSOR_SENSITIVITY, 1, &i32);
	} else {
		i32 = p->ev;
		ACaptureRequest_setEntry_i32(request, ACAMERA_CONTROL_AE_EXPOSURE_COMPENSATION, 1, &i32);
	}

	u8 = p->focus_mode;
	ACaptureRequest_setEntry_u8(request, ACAMERA_CONTROL_AF_MODE, 1, &u8);
	if (p->focus_mode == 0) {
		f = p->focus;
		ACaptureRequest_setEntry_float(request, ACAMERA_LENS_FOCUS_DISTANCE, 1, &f);
	}

	if (p->wb_manual) {
		float gains[4] = { p->wb_r, 1.0f, 1.0f, p->wb_b };
		u8 = 0;
		ACaptureRequest_setEntry_u8(request, ACAMERA_CONTROL_AWB_MODE, 1, &u8);
		ACaptureRequest_setEntry_u8(request, ACAMERA_COLOR_CORRECTION_MODE, 1, &u8);
		ACaptureRequest_setEntry_float(request, ACAMERA_COLOR_CORRECTION_GAINS, 4, gains);
	} else {
		u8 = p->awb;
		ACaptureRequest_setEntry_u8(request, ACAMERA_CONTROL_AWB_MODE, 1, &u8);
	}
	if (!preview) {
		u8 = p->quality;
		ACaptureRequest_setEntry_u8(request, ACAMERA_JPEG_QUALITY, 1, &u8);
		if (p->nr >= 0) {
			u8 = p->nr;
			ACaptureRequest_setEntry_u8(request, ACAMERA_NOISE_REDUCTION_MODE, 1, &u8);
		}
		if (p->edge >= 0) {
			u8 = p->edge;
			ACaptureRequest_setEntry_u8(request, ACAMERA_EDGE_MODE, 1, &u8);
		}
	}
}

/* parse "key=value" tokens in place; returns 0 or -1 (bad key/value) */
static int parse_params(struct params *p, char *tokens)
{
	for (char *token = tokens; token && *token; ) {
		char *next = strchr(token, ' ');
		if (next)
			*next++ = 0;
		char *value = strchr(token, '=');
		if (!value) {
			if (*token)
				return -1;
		} else {
			*value++ = 0;
			if (!strcmp(token, "ae"))
				p->ae_manual = !strcmp(value, "manual");
			else if (!strcmp(token, "shutter"))
				p->shutter = strtod(value, 0), p->ae_manual = 1;
			else if (!strcmp(token, "iso"))
				p->iso = atoi(value), p->ae_manual = 1;
			else if (!strcmp(token, "ev"))
				p->ev = atoi(value);
			else if (!strcmp(token, "focus")) {
				if (!strcmp(value, "auto"))
					p->focus_mode = 1;
				else if (!strcmp(value, "cont"))
					p->focus_mode = 3;
				else
					p->focus_mode = 0, p->focus = strtod(value, 0);
			} else if (!strcmp(token, "awb"))
				p->awb = atoi(value), p->wb_manual = 0;
			else if (!strcmp(token, "wb")) {
				char *rest;
				p->wb_r = strtod(value, &rest);
				p->wb_b = *rest == ',' ? strtod(rest + 1, 0) : 1.0;
				p->wb_manual = 1;
			} else if (!strcmp(token, "q"))
				p->quality = atoi(value);
			else if (!strcmp(token, "nr"))
				p->nr = atoi(value);
			else if (!strcmp(token, "edge"))
				p->edge = atoi(value);
			else if (!strcmp(token, "zoom"))
				p->zoom = strtod(value, 0);
			else if (!strcmp(token, "flash"))
				p->flash = !strcmp(value, "auto") ? 1 : !strcmp(value, "on") ? 2 :
					!strcmp(value, "torch") ? 3 : 0;
			else
				return -1;
		}
		token = next;
		while (token && *token == ' ')
			token++;
	}
	return 0;
}

static ACameraDevice *device;
static ACameraCaptureSession *session;
static ACaptureRequest *preview_request;
static ACameraOutputTarget *jpeg_target;

static void restart_preview(void)
{
	apply(preview_request, &current, 1);
	ACameraCaptureSession_setRepeatingRequest(session, 0, 1, &preview_request, 0);
}

static void do_shot(const char *path, struct params *p)
{
	ACaptureRequest *still;
	jpeg_path = path;
	jpeg_done = jpeg_bytes = 0;
	if (ACameraDevice_createCaptureRequest(device, TEMPLATE_STILL_CAPTURE, &still)) {
		event("err %s %d %d\n", "createCaptureRequest", 0, 0);
		return;
	}
	ACaptureRequest_addTarget(still, jpeg_target);
	apply(still, p, 0);
	/* the preview target is not in the still request: stop the stream so the
	 * ISP is free for the (possibly very long) exposure */
	ACameraCaptureSession_stopRepeating(session);
	int status = ACameraCaptureSession_capture(session, 0, 1, &still, 0);
	if (status) {
		event("err %s %d %d\n", "capture", status, 0);
	} else {
		int waits = 150 + (p->ae_manual ? (int)(p->shutter * 30) : 0); /* x100 ms */
		for (int waited = 0; !jpeg_done && waited < waits; waited++)
			usleep(100000);
		if (jpeg_done && jpeg_bytes)
			event("ok shot %s %d %d\n", path, jpeg_bytes, 0);
		else
			event("err %s %d %d\n", jpeg_done ? "shot-write" : "shot-timeout", 0, 0);
	}
	ACaptureRequest_free(still);
	jpeg_path = 0;
	restart_preview();
}

static void handle_line(char *line)
{
	if (!strcmp(line, "reset")) {
		current = defaults;
		restart_preview();
		event("ok %s %d %d\n", "reset", 0, 0);
	} else if (!strncmp(line, "set ", 4)) {
		struct params next = current;
		if (parse_params(&next, line + 4)) {
			event("err %s %d %d\n", "bad-param", 0, 0);
			return;
		}
		current = next;
		restart_preview();
		event("ok %s %d %d\n", "set", 0, 0);
	} else if (!strncmp(line, "shot ", 5)) {
		char *path = line + 5, *rest = strchr(path, ' ');
		struct params next = current;
		if (rest)
			*rest++ = 0;
		if (parse_params(&next, rest ? rest : (char *)"")) {
			event("err %s %d %d\n", "bad-param", 0, 0);
			return;
		}
		do_shot(path, &next);
	} else if (!strcmp(line, "af")) {
		uint8_t u8 = 1;
		struct params af = current;
		af.focus_mode = 1;
		apply(preview_request, &af, 1);
		ACaptureRequest_setEntry_u8(preview_request, ACAMERA_CONTROL_AF_TRIGGER, 1, &u8);
		ACameraCaptureSession_capture(session, 0, 1, &preview_request, 0);
		u8 = 0;
		ACaptureRequest_setEntry_u8(preview_request, ACAMERA_CONTROL_AF_TRIGGER, 1, &u8);
		restart_preview();
		event("ok %s %d %d\n", "af", 0, 0);
	} else {
		event("err %s %d %d\n", "unknown-command", 0, 0);
	}
}

int main(int argc, char **argv, char **envp)
{
	(void)envp;
	if (argc < 5) {
		dprintf(2, "usage: camstream <camera-id> <width> <height> <out-file>\n");
		return 2;
	}
	const char *camera_id = argv[1];
	width = atoi(argv[2]);
	height = atoi(argv[3]);
	int size = width * height * 3 / 2;
	current = defaults;

	int fd = open(argv[4], O_RDWR | O_CREAT | O_TRUNC, 0644);
	if (fd < 0 || ftruncate(fd, HEADER + size)) {
		dprintf(2, "camstream: can't create %s\n", argv[4]);
		return 1;
	}
	shared = mmap(0, HEADER + size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (shared == (void *)-1L) {
		dprintf(2, "camstream: mmap failed\n");
		return 1;
	}
	frame = (uint8_t *)shared + HEADER;
	shared->version = 1;
	shared->width = width;
	shared->height = height;
	shared->format = 17; /* ImageFormat.NV21 */
	shared->size = size;
	shared->seq = 0;
	shared->frames = 0;
	__atomic_store_n(&shared->magic, 0x4d41434c, __ATOMIC_RELEASE); /* "LCAM": header valid */

	char ctl_path[512], ev_path[512];
	snprintf(ctl_path, sizeof ctl_path, "%s.ctl", argv[4]);
	snprintf(ev_path, sizeof ev_path, "%s.ev", argv[4]);
	unlink(ctl_path);
	mkfifo(ctl_path, 0666);
	chmod(ctl_path, 0666);
	/* O_RDWR keeps the FIFO open without a writer (no EOF); non-blocking for the poll loop */
	int ctl = open(ctl_path, O_RDWR | O_NONBLOCK);
	event_fd = open(ev_path, O_WRONLY | O_CREAT | O_TRUNC | O_APPEND, 0666);
	chmod(ev_path, 0666);

	ABinderProcess_startThreadPool();
	ACameraManager *manager = ACameraManager_create();
	ACameraMetadata *characteristics;
	int jpeg_width = 0, jpeg_height = 0;
	CHECK(ACameraManager_getCameraCharacteristics(manager, camera_id, &characteristics));
	largest_size(characteristics, AIMAGE_FORMAT_JPEG, &jpeg_width, &jpeg_height);
	{
		ACameraMetadata_const_entry entry;
		if (ACameraMetadata_getConstEntry(characteristics, ACAMERA_SENSOR_INFO_ACTIVE_ARRAY_SIZE, &entry) == 0 &&
				entry.count >= 4)
			memcpy(array_size, entry.data.i32, sizeof array_size);
	}
	ACameraDevice_StateCallbacks device_callbacks = { 0, on_disconnected, on_error };
	CHECK(ACameraManager_openCamera(manager, camera_id, &device_callbacks, &device));

	AImageReader *reader, *jpeg_reader;
	ANativeWindow *window, *jpeg_window;
	CHECK(AImageReader_new(width, height, AIMAGE_FORMAT_YUV_420_888, 4, &reader));
	AImageReader_ImageListener listener = { 0, on_preview };
	CHECK(AImageReader_setImageListener(reader, &listener));
	CHECK(AImageReader_getWindow(reader, &window));
	CHECK(AImageReader_new(jpeg_width, jpeg_height, AIMAGE_FORMAT_JPEG, 2, &jpeg_reader));
	AImageReader_ImageListener jpeg_listener = { 0, on_jpeg };
	CHECK(AImageReader_setImageListener(jpeg_reader, &jpeg_listener));
	CHECK(AImageReader_getWindow(jpeg_reader, &jpeg_window));

	ACaptureSessionOutputContainer *outputs;
	ACaptureSessionOutput *output, *jpeg_output;
	CHECK(ACaptureSessionOutputContainer_create(&outputs));
	CHECK(ACaptureSessionOutput_create(window, &output));
	CHECK(ACaptureSessionOutputContainer_add(outputs, output));
	CHECK(ACaptureSessionOutput_create(jpeg_window, &jpeg_output));
	CHECK(ACaptureSessionOutputContainer_add(outputs, jpeg_output));
	ACameraCaptureSession_stateCallbacks session_callbacks = {
		"event", on_session_event, on_session_event, on_session_event };
	CHECK(ACameraDevice_createCaptureSession(device, outputs, &session_callbacks, &session));

	ACameraOutputTarget *target;
	CHECK(ACameraOutputTarget_create(window, &target));
	CHECK(ACameraOutputTarget_create(jpeg_window, &jpeg_target));
	CHECK(ACameraDevice_createCaptureRequest(device, TEMPLATE_PREVIEW, &preview_request));
	CHECK(ACaptureRequest_addTarget(preview_request, target));
	CHECK(ACameraCaptureSession_setRepeatingRequest(session, 0, 1, &preview_request, 0));
	dprintf(2, "camstream: camera %s streaming %dx%d NV21 to %s (jpeg %dx%d, control %s)\n",
		camera_id, width, height, argv[4], jpeg_width, jpeg_height, ctl_path);
	event("ready %s %d %d\n", "", jpeg_width, jpeg_height);

	char buffer[1024];
	int fill = 0, ticks = 0;
	uint32_t last = 0;
	for (;; ) {
		long got = read(ctl, buffer + fill, sizeof buffer - 1 - fill);
		if (got > 0)
			fill += got;
		for (;;) {
			buffer[fill] = 0;
			char *newline = strchr(buffer, '\n');
			if (!newline) {
				if (fill >= (int)sizeof buffer - 1)
					fill = 0; /* overlong line: drop */
				break;
			}
			*newline = 0;
			handle_line(buffer);
			fill -= newline + 1 - buffer;
			memmove(buffer, newline + 1, fill);
		}
		usleep(20000);
		if (++ticks >= 250) {
			ticks = 0;
			dprintf(2, "camstream: %u frames (%u in the last 5 s)\n", shared->frames, shared->frames - last);
			last = shared->frames;
		}
	}
}

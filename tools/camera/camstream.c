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

#define CHECK(call) do { int status_ = (call); if (status_) { \
	dprintf(2, "camstream: %s failed: %d\n", #call, status_); return 1; } } while (0)

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

	ABinderProcess_startThreadPool();
	ACameraManager *manager = ACameraManager_create();
	ACameraDevice_StateCallbacks device_callbacks = { 0, on_disconnected, on_error };
	ACameraDevice *device;
	CHECK(ACameraManager_openCamera(manager, camera_id, &device_callbacks, &device));

	AImageReader *reader;
	ANativeWindow *window;
	CHECK(AImageReader_new(width, height, AIMAGE_FORMAT_YUV_420_888, 4, &reader));
	AImageReader_ImageListener listener = { 0, on_preview };
	CHECK(AImageReader_setImageListener(reader, &listener));
	CHECK(AImageReader_getWindow(reader, &window));

	ACaptureSessionOutputContainer *outputs;
	ACaptureSessionOutput *output;
	CHECK(ACaptureSessionOutputContainer_create(&outputs));
	CHECK(ACaptureSessionOutput_create(window, &output));
	CHECK(ACaptureSessionOutputContainer_add(outputs, output));
	ACameraCaptureSession_stateCallbacks session_callbacks = {
		"event", on_session_event, on_session_event, on_session_event };
	ACameraCaptureSession *session;
	CHECK(ACameraDevice_createCaptureSession(device, outputs, &session_callbacks, &session));

	ACameraOutputTarget *target;
	ACaptureRequest *request;
	CHECK(ACameraOutputTarget_create(window, &target));
	CHECK(ACameraDevice_createCaptureRequest(device, TEMPLATE_PREVIEW, &request));
	CHECK(ACaptureRequest_addTarget(request, target));
	CHECK(ACameraCaptureSession_setRepeatingRequest(session, 0, 1, &request, 0));
	dprintf(2, "camstream: camera %s streaming %dx%d NV21 to %s\n", camera_id, width, height, argv[4]);

	for (uint32_t last = 0;; ) {
		usleep(5000000);
		dprintf(2, "camstream: %u frames (%u in the last 5 s)\n", shared->frames, shared->frames - last);
		last = shared->frames;
	}
}

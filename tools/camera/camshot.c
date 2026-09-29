/*
 * camshot: grab a JPEG through the stock Android NDK camera API
 * (cameraserver + MTK camera HAL in the camroot).
 *   camshot [camera-id] [out.jpg] [preview-ms]
 * Built with gcc against bionic and the stock libs, no NDK headers:
 * the few NDK declarations needed are spelled out below.
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

static volatile int preview_frames;
static volatile int jpeg_done;
static const char *out_path = "/tmp/camshot.jpg";

static void on_disconnected(void *context, ACameraDevice *device)
{
	(void)context, (void)device;
	dprintf(2, "camshot: device disconnected\n");
}

static void on_error(void *context, ACameraDevice *device, int error)
{
	(void)context, (void)device;
	dprintf(2, "camshot: device error %d\n", error);
}

static void on_session_event(void *context, ACameraCaptureSession *session)
{
	(void)session;
	dprintf(2, "camshot: session %s\n", (const char *)context);
}

static void on_preview(void *context, AImageReader *reader)
{
	AImage *image;
	(void)context;
	if (AImageReader_acquireLatestImage(reader, &image) == 0) {
		preview_frames++;
		AImage_delete(image);
	}
}

static void on_jpeg(void *context, AImageReader *reader)
{
	AImage *image;
	uint8_t *data;
	int length;
	(void)context;
	if (AImageReader_acquireLatestImage(reader, &image) != 0)
		return;
	if (AImage_getPlaneData(image, 0, &data, &length) == 0) {
		/* the blob buffer is padded; the JPEG ends at the last FFD9 */
		int end = length;
		for (int index = length - 2; index > 1; index--)
			if (data[index] == 0xff && data[index + 1] == 0xd9) {
				end = index + 2;
				break;
			}
		int fd = open(out_path, 01 | 0100 | 01000, 0644); /* O_WRONLY|O_CREAT|O_TRUNC */
		if (fd >= 0) {
			write(fd, data, end);
			close(fd);
		}
		dprintf(2, "camshot: wrote %s (%d bytes)\n", out_path, end);
	}
	AImage_delete(image);
	jpeg_done = 1;
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
	dprintf(2, "camshot: %s failed: %d\n", #call, status_); return 1; } } while (0)

int main(int argc, char **argv, char **envp)
{
	(void)envp;
	const char *camera_id = argc > 1 ? argv[1] : "0";
	int preview_ms = argc > 3 ? atoi(argv[3]) : 2000;
	if (argc > 2)
		out_path = argv[2];

	ABinderProcess_startThreadPool();
	ACameraManager *manager = ACameraManager_create();
	ACameraIdList *ids;
	CHECK(ACameraManager_getCameraIdList(manager, &ids));
	for (int index = 0; index < ids->numCameras; index++) {
		ACameraMetadata *characteristics;
		ACameraMetadata_const_entry facing;
		int width, height;
		if (ACameraManager_getCameraCharacteristics(manager, ids->cameraIds[index],
				&characteristics))
			continue;
		ACameraMetadata_getConstEntry(characteristics, ACAMERA_LENS_FACING, &facing);
		largest_size(characteristics, AIMAGE_FORMAT_JPEG, &width, &height);
		dprintf(2, "camshot: camera %s facing %s, max jpeg %dx%d\n", ids->cameraIds[index],
			facing.data.u8[0] == 0 ? "front" : "back", width, height);
	}
	if (argc > 1 && strcmp(argv[1], "list") == 0)
		return 0;

	ACameraMetadata *characteristics;
	int jpeg_width, jpeg_height, preview_width = 640, preview_height = 480;
	CHECK(ACameraManager_getCameraCharacteristics(manager, camera_id, &characteristics));
	CHECK(largest_size(characteristics, AIMAGE_FORMAT_JPEG, &jpeg_width, &jpeg_height));

	ACameraDevice_StateCallbacks device_callbacks = { 0, on_disconnected, on_error };
	ACameraDevice *device;
	CHECK(ACameraManager_openCamera(manager, camera_id, &device_callbacks, &device));

	AImageReader *preview_reader, *jpeg_reader;
	ANativeWindow *preview_window, *jpeg_window;
	CHECK(AImageReader_new(preview_width, preview_height, AIMAGE_FORMAT_YUV_420_888, 4,
		&preview_reader));
	CHECK(AImageReader_new(jpeg_width, jpeg_height, AIMAGE_FORMAT_JPEG, 2, &jpeg_reader));
	AImageReader_ImageListener preview_listener = { 0, on_preview };
	AImageReader_ImageListener jpeg_listener = { 0, on_jpeg };
	CHECK(AImageReader_setImageListener(preview_reader, &preview_listener));
	CHECK(AImageReader_setImageListener(jpeg_reader, &jpeg_listener));
	CHECK(AImageReader_getWindow(preview_reader, &preview_window));
	CHECK(AImageReader_getWindow(jpeg_reader, &jpeg_window));

	ACaptureSessionOutputContainer *outputs;
	ACaptureSessionOutput *preview_output, *jpeg_output;
	CHECK(ACaptureSessionOutputContainer_create(&outputs));
	CHECK(ACaptureSessionOutput_create(preview_window, &preview_output));
	CHECK(ACaptureSessionOutput_create(jpeg_window, &jpeg_output));
	CHECK(ACaptureSessionOutputContainer_add(outputs, preview_output));
	CHECK(ACaptureSessionOutputContainer_add(outputs, jpeg_output));

	ACameraCaptureSession_stateCallbacks session_callbacks = {
		"event", on_session_event, on_session_event, on_session_event };
	ACameraCaptureSession *session;
	CHECK(ACameraDevice_createCaptureSession(device, outputs, &session_callbacks, &session));

	ACameraOutputTarget *preview_target, *jpeg_target;
	ACaptureRequest *preview_request, *still_request;
	CHECK(ACameraOutputTarget_create(preview_window, &preview_target));
	CHECK(ACameraOutputTarget_create(jpeg_window, &jpeg_target));
	CHECK(ACameraDevice_createCaptureRequest(device, TEMPLATE_PREVIEW, &preview_request));
	CHECK(ACaptureRequest_addTarget(preview_request, preview_target));
	CHECK(ACameraDevice_createCaptureRequest(device, TEMPLATE_STILL_CAPTURE, &still_request));
	CHECK(ACaptureRequest_addTarget(still_request, jpeg_target));

	CHECK(ACameraCaptureSession_setRepeatingRequest(session, 0, 1, &preview_request, 0));
	usleep(preview_ms * 1000);
	dprintf(2, "camshot: %d preview frames in %d ms\n", preview_frames, preview_ms);
	CHECK(ACameraCaptureSession_stopRepeating(session));
	CHECK(ACameraCaptureSession_capture(session, 0, 1, &still_request, 0));
	for (int waited = 0; !jpeg_done && waited < 100; waited++)
		usleep(100000);
	if (!jpeg_done)
		dprintf(2, "camshot: no JPEG after 10 s\n");
	ACameraCaptureSession_close(session);
	ACameraDevice_close(device);
	return jpeg_done ? 0 : 1;
}

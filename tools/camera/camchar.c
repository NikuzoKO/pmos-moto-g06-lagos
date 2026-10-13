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

/* camchar: dump every camera characteristics tag (tag type count values) */
int ACameraMetadata_getAllTags(const ACameraMetadata *metadata, int32_t *numEntries, const uint32_t **tags);
int dprintf(int, const char *, ...);

int main(int argc, char **argv, char **envp)
{
	(void)envp;
	ABinderProcess_startThreadPool();
	ACameraManager *manager = ACameraManager_create();
	ACameraIdList *ids;
	if (ACameraManager_getCameraIdList(manager, &ids))
		return 1;
	for (int index = 0; index < ids->numCameras; index++) {
		if (argc > 1 && strcmp(argv[1], ids->cameraIds[index]))
			continue;
		ACameraMetadata *md;
		int32_t count;
		const uint32_t *tags;
		if (ACameraManager_getCameraCharacteristics(manager, ids->cameraIds[index], &md))
			continue;
		ACameraMetadata_getAllTags(md, &count, &tags);
		for (int t = 0; t < count; t++) {
			ACameraMetadata_const_entry e;
			if (ACameraMetadata_getConstEntry(md, tags[t], &e))
				continue;
			dprintf(1, "cam%s %x t%d n%d:", ids->cameraIds[index], tags[t], e.type, e.count);
			for (uint32_t k = 0; k < e.count && k < 24; k++) {
				switch (e.type) {
				case 0: dprintf(1, " %d", e.data.u8[k]); break;
				case 1: dprintf(1, " %d", e.data.i32[k]); break;
				case 2: { float f = ((const float *)e.data.i32)[k]; dprintf(1, " %d.%03d", (int)f, (int)((f-(int)f)*1000)); break; }
				case 3: dprintf(1, " %ld", ((const int64_t *)e.data.i32)[k]); break;
				case 5: dprintf(1, " %d/%d", e.data.i32[2*k], e.data.i32[2*k+1]); break;
				default: dprintf(1, " ?");
				}
			}
			dprintf(1, "\n");
		}
	}
	return 0;
}

#include <stdio.h>
#include <fcntl.h>
#include <gbm.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
int main(int c, char **v) {
	int fd = open(v[1], O_RDWR | O_CLOEXEC);
	drmVersionPtr ver = drmGetVersion(fd);
	printf("drm name=%s\n", ver->name);
	struct gbm_device *g = gbm_create_device(fd);
	printf("gbm backend=%s\n", gbm_device_get_backend_name(g));
	PFNEGLGETPLATFORMDISPLAYEXTPROC gpd = (void *)eglGetProcAddress("eglGetPlatformDisplayEXT");
	EGLDisplay d = gpd(EGL_PLATFORM_GBM_KHR, g, NULL);
	EGLint ma, mi;
	if (!eglInitialize(d, &ma, &mi)) { printf("eglInitialize failed 0x%x\n", eglGetError()); return 1; }
	eglBindAPI(EGL_OPENGL_ES_API);
	EGLint ca[] = { EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE };
	EGLConfig cfgs[64]; EGLint n; eglChooseConfig(d, ca, cfgs, 64, &n);
	EGLConfig cfg = 0;
	for (int i = 0; i < n; i++) { EGLint id; eglGetConfigAttrib(d, cfgs[i], EGL_NATIVE_VISUAL_ID, &id); if (id == GBM_FORMAT_XRGB8888) { cfg = cfgs[i]; break; } }
	EGLint cta[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	EGLContext ctx = eglCreateContext(d, cfg, EGL_NO_CONTEXT, cta);
	struct gbm_surface *gs = gbm_surface_create(g, 720, 1600, GBM_FORMAT_XRGB8888, GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
	printf("gbm_surface=%p\n", (void *)gs);
	EGLSurface es = eglCreateWindowSurface(d, cfg, (EGLNativeWindowType)gs, NULL);
	eglMakeCurrent(d, es, es, ctx);
	printf("GL_RENDERER=%s\n", glGetString(GL_RENDERER));
	glClearColor(0.2, 0.4, 0.8, 1); glClear(GL_COLOR_BUFFER_BIT);
	eglSwapBuffers(d, es);
	struct gbm_bo *bo = gbm_surface_lock_front_buffer(gs);
	uint32_t h[4] = { gbm_bo_get_handle(bo).u32 }, p[4] = { gbm_bo_get_stride(bo) }, o[4] = { 0 }, fb = 0;
	uint64_t mod[4] = { gbm_bo_get_modifier(bo) };
	printf("bo stride=%u modifier=0x%llx\n", p[0], (unsigned long long)mod[0]);
	int r = drmModeAddFB2(fd, 720, 1600, GBM_FORMAT_XRGB8888, h, p, o, &fb, 0);
	printf("AddFB2 r=%d fb=%u\n", r, fb);
	glFinish();
	return 0;
}

#include <camera/NdkCameraManager.h>
#include <camera/NdkCameraMetadata.h>
#include <camera/NdkCameraDevice.h>
#include <camera/NdkCameraCaptureSession.h>
#include <camera/NdkCaptureRequest.h>
#include <media/NdkImageReader.h>
#include <media/NdkImage.h>
#include <dlfcn.h>
#include <limits.h>
#include <stdatomic.h>
#include <stdio.h>
#include <jpeglib.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <stdint.h>

static atomic_int frame_claimed = 0;
static atomic_int capture_result = -1;
static atomic_int session_ready = 0;
static atomic_int frames_written = 0;
static volatile sig_atomic_t stopping = 0;
static int warmup_frames = 0;
static int64_t last_encoded_ns = 0;
static const char *output_path;

static int64_t monotonic_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static void request_stop(int signal_number) {
    (void)signal_number;
    stopping = 1;
}

static void device_disconnected(void *ctx, ACameraDevice *dev) { (void)ctx; (void)dev; fprintf(stderr, "device disconnected\n"); }
static void device_error(void *ctx, ACameraDevice *dev, int err) { (void)ctx; (void)dev; fprintf(stderr, "device error=%d\n", err); }
static void session_closed(void *ctx, ACameraCaptureSession *s) { (void)ctx; (void)s; fprintf(stderr, "session closed\n"); }
static void session_ready_cb(void *ctx, ACameraCaptureSession *s) { (void)ctx; (void)s; atomic_store(&session_ready, 1); fprintf(stderr, "session ready\n"); }
static void session_active(void *ctx, ACameraCaptureSession *s) { (void)ctx; (void)s; fprintf(stderr, "session active\n"); }
static unsigned char clamp(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }
static void image_available(void *ctx, AImageReader *reader) {
    (void)ctx;
    if (stopping || atomic_load(&capture_result) == 0) return;
    if (atomic_exchange(&frame_claimed, 1)) return;
    AImage *img = NULL;
    media_status_t rc = AImageReader_acquireLatestImage(reader, &img);
    if (rc != AMEDIA_OK || !img) { atomic_store(&frame_claimed, 0); return; }
    const int64_t now = monotonic_ns();
    if (++warmup_frames <= 5 || (last_encoded_ns && now - last_encoded_ns < 200000000LL)) {
        AImage_delete(img);
        atomic_store(&frame_claimed, 0);
        return;
    }
    uint8_t *plane[3] = {0};
    int length[3], row[3], pixel[3];
    int32_t width = 0, height = 0;
    AImage_getWidth(img, &width);
    AImage_getHeight(img, &height);
    for (int p = 0; p < 3; ++p) {
        if (AImage_getPlaneData(img, p, &plane[p], &length[p]) ||
            AImage_getPlaneRowStride(img, p, &row[p]) ||
            AImage_getPlanePixelStride(img, p, &pixel[p])) {
            fprintf(stderr, "plane %d unavailable\n", p);
            AImage_delete(img); atomic_store(&capture_result, 0); return;
        }
    }
    if (width < 1 || height < 1 || width > 4096 || height > 4096 ||
        row[0] <= 0 || row[1] <= 0 || row[2] <= 0 ||
        pixel[0] <= 0 || pixel[1] <= 0 || pixel[2] <= 0 ||
        (int64_t)(height - 1) * row[0] + (int64_t)(width - 1) * pixel[0] >= length[0] ||
        (int64_t)((height - 1) / 2) * row[1] + (int64_t)((width - 1) / 2) * pixel[1] >= length[1] ||
        (int64_t)((height - 1) / 2) * row[2] + (int64_t)((width - 1) / 2) * pixel[2] >= length[2]) {
        fprintf(stderr, "invalid image plane dimensions\n");
        AImage_delete(img); atomic_store(&capture_result, 0); return;
    }
    unsigned char *rgb = malloc((size_t)width * 3);
    if (!rgb) { AImage_delete(img); atomic_store(&capture_result, 0); return; }
    struct jpeg_compress_struct c;
    struct jpeg_error_mgr e;
    c.err = jpeg_std_error(&e);
    jpeg_create_compress(&c);
    char temporary[PATH_MAX];
    if (snprintf(temporary, sizeof(temporary), "%s.tmp", output_path) >= (int)sizeof(temporary)) {
        jpeg_destroy_compress(&c); free(rgb); AImage_delete(img); atomic_store(&capture_result, 0); return;
    }
    FILE *out = fopen(temporary, "wb");
    if (!out) { jpeg_destroy_compress(&c); free(rgb); AImage_delete(img); atomic_store(&capture_result, 0); return; }
    jpeg_stdio_dest(&c, out);
    c.image_width = width; c.image_height = height;
    c.input_components = 3; c.in_color_space = JCS_RGB;
    jpeg_set_defaults(&c); jpeg_set_quality(&c, 75, TRUE);
    jpeg_start_compress(&c, TRUE);
    while (c.next_scanline < (JDIMENSION)height) {
        int y = c.next_scanline;
        for (int x = 0; x < width; x++) {
            int yy = plane[0][y * row[0] + x * pixel[0]] - 16;
            int u = plane[1][(y / 2) * row[1] + (x / 2) * pixel[1]] - 128;
            int v = plane[2][(y / 2) * row[2] + (x / 2) * pixel[2]] - 128;
            int cy = 298 * (yy < 0 ? 0 : yy);
            rgb[3*x] = clamp((cy + 409*v + 128) >> 8);
            rgb[3*x+1] = clamp((cy - 100*u - 208*v + 128) >> 8);
            rgb[3*x+2] = clamp((cy + 516*u + 128) >> 8);
        }
        JSAMPROW r = rgb;
        jpeg_write_scanlines(&c, &r, 1);
    }
    jpeg_finish_compress(&c); jpeg_destroy_compress(&c);
    int ok = fclose(out) == 0 && rename(temporary, output_path) == 0;
    if (!ok) unlink(temporary);
    free(rgb); AImage_delete(img);
    if (ok) {
        last_encoded_ns = monotonic_ns();
        int count = atomic_fetch_add(&frames_written, 1) + 1;
        if (count == 1 || count % 30 == 0) fprintf(stderr, "live frames=%d size=%dx%d\n", count, width, height);
        atomic_store(&capture_result, 1);
        atomic_store(&frame_claimed, 0);
    } else {
        atomic_store(&capture_result, 0);
    }
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: camera-stream <camera-id> <absolute-jpeg-path>\n");
        return 2;
    }
    const char *id = argv[1];
    output_path = argv[2];
    signal(SIGTERM, request_stop);
    signal(SIGINT, request_stop);
    int image_format = AIMAGE_FORMAT_YUV_420_888;
    void *binder = dlopen("libbinder_ndk.so", RTLD_NOW);
    if (!binder) { fprintf(stderr, "binder dlopen: %s\n", dlerror()); return 10; }
    void (*set_max)(unsigned int) = dlsym(binder, "ABinderProcess_setThreadPoolMaxThreadCount");
    void (*start_pool)(void) = dlsym(binder, "ABinderProcess_startThreadPool");
    if (!set_max || !start_pool) { fprintf(stderr, "binder dlsym: %s\n", dlerror()); return 11; }
    set_max(8);
    start_pool();
    fprintf(stderr, "binder thread pool started\n");
    ACameraManager *mgr = ACameraManager_create();
    ACameraMetadata *meta = NULL;
    camera_status_t rc = ACameraManager_getCameraCharacteristics(mgr, id, &meta);
    fprintf(stderr, "characteristics=%d\n", rc);
    if (rc != ACAMERA_OK) return 2;
    ACameraMetadata_const_entry entry;
    int width = 0, height = 0;
    rc = ACameraMetadata_getConstEntry(meta, ACAMERA_SCALER_AVAILABLE_STREAM_CONFIGURATIONS, &entry);
    fprintf(stderr, "stream configs=%d count=%u\n", rc, rc == ACAMERA_OK ? entry.count : 0);
    if (rc != ACAMERA_OK) return 3;
    for (uint32_t i = 0; i + 3 < entry.count; i += 4) {
        int fmt = entry.data.i32[i], w = entry.data.i32[i+1], h = entry.data.i32[i+2], input = entry.data.i32[i+3];
        if (fmt == image_format && input == 0) {
            fprintf(stderr, "image size=%dx%d\n", w, h);
            if (!width || (w >= 640 && h >= 480 && (long long)w*h < (long long)width*height)) width = w, height = h;
        }
    }
    if (!width) { fprintf(stderr, "no matching stream\n"); return 4; }
    fprintf(stderr, "selected=%dx%d\n", width, height);
    ACameraDevice *dev = NULL;
    ACameraDevice_StateCallbacks dev_cb = { .context=NULL, .onDisconnected=device_disconnected, .onError=device_error };
    rc = ACameraManager_openCamera(mgr, id, &dev_cb, &dev);
    fprintf(stderr, "open=%d dev=%p\n", rc, dev);
    if (rc != ACAMERA_OK) return 5;
    AImageReader *reader = NULL;
    media_status_t mr = AImageReader_new(width, height, image_format, 4, &reader);
    fprintf(stderr, "reader=%d ptr=%p\n", mr, reader);
    if (mr != AMEDIA_OK) return 6;
    AImageReader_ImageListener listener = { .context=NULL, .onImageAvailable=image_available };
    mr = AImageReader_setImageListener(reader, &listener);
    fprintf(stderr, "listener=%d\n", mr);
    ANativeWindow *window = NULL;
    mr = AImageReader_getWindow(reader, &window);
    fprintf(stderr, "window=%d ptr=%p\n", mr, window);
    ACaptureSessionOutput *session_output = NULL;
    rc = ACaptureSessionOutput_create(window, &session_output);
    fprintf(stderr, "output=%d ptr=%p\n", rc, session_output);
    ACaptureSessionOutputContainer *container = NULL;
    rc = ACaptureSessionOutputContainer_create(&container);
    fprintf(stderr, "container=%d ptr=%p\n", rc, container);
    rc = ACaptureSessionOutputContainer_add(container, session_output);
    fprintf(stderr, "container add=%d\n", rc);
    ACameraCaptureSession_stateCallbacks sess_cb = { .context=NULL, .onClosed=session_closed, .onReady=session_ready_cb, .onActive=session_active };
    ACameraCaptureSession *session = NULL;
    rc = ACameraDevice_createCaptureSession(dev, container, &sess_cb, &session);
    fprintf(stderr, "create session=%d ptr=%p\n", rc, session);
    if (rc != ACAMERA_OK) return 7;
    ACaptureRequest *request = NULL;
    rc = ACameraDevice_createCaptureRequest(dev,
            image_format == AIMAGE_FORMAT_JPEG ? TEMPLATE_STILL_CAPTURE : TEMPLATE_PREVIEW, &request);
    fprintf(stderr, "request=%d ptr=%p\n", rc, request);
    if (rc != ACAMERA_OK) return 8;
    ACameraOutputTarget *target = NULL;
    ACameraOutputTarget_create(window, &target);
    ACaptureRequest_addTarget(request, target);
    for (int i=0; i<100 && !atomic_load(&session_ready); i++) usleep(100000);
    int seq = -1;
    rc = ACameraCaptureSession_setRepeatingRequest(session, NULL, 1, &request, &seq);
    fprintf(stderr, "capture=%d sequence=%d\n", rc, seq);
    if (rc == ACAMERA_OK) {
        const int64_t start_ns = monotonic_ns();
        while (!stopping && atomic_load(&capture_result) != 0) {
            if (!atomic_load(&frames_written) && monotonic_ns() - start_ns > 20000000000LL) {
                fprintf(stderr, "first frame timeout\n");
                atomic_store(&capture_result, 0);
                break;
            }
            usleep(100000);
        }
    }
    fprintf(stderr, "stream stopped frames=%d result=%d\n", atomic_load(&frames_written), atomic_load(&capture_result));
    ACameraCaptureSession_stopRepeating(session);
    ACameraCaptureSession_close(session);
    for (int i = 0; i < 100 && atomic_load(&frame_claimed); i++) usleep(10000);
    ACameraDevice_close(dev);
    AImageReader_delete(reader);
    ACameraMetadata_free(meta);
    ACameraManager_delete(mgr);
    unlink(output_path);
    return rc == ACAMERA_OK && atomic_load(&capture_result) != 0 ? 0 : 9;
}

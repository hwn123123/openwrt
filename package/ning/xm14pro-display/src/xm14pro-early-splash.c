// SPDX-License-Identifier: GPL-2.0-only
#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>

#include "xm14pro-boot-scene.h"

#define BUFFER_COUNT 2
#define MAX_PLANES 2
#define STATE_DIRECTORY "/run/xm14display"
#define STARTING_FILE STATE_DIRECTORY "/early-splash-starting"
#define ACTIVE_FILE STATE_DIRECTORY "/early-splash-active"
#define START_FILE STATE_DIRECTORY "/splash-start"
#define TAKEOVER_REQUEST_FILE STATE_DIRECTORY "/takeover-request"
#define MASTER_RELEASED_FILE STATE_DIRECTORY "/master-released"
#define TAKEOVER_COMPLETE_FILE STATE_DIRECTORY "/takeover-complete"
#define STOP_FILE STATE_DIRECTORY "/early-splash-stop"

struct splash_buffer {
    uint32_t handle;
    uint32_t pitch;
    uint64_t size;
    uint32_t framebuffer_id;
    uint32_t *pixels;
};

struct splash_display {
    int descriptor;
    int width;
    int height;
    uint32_t connector_id;
    uint32_t crtc_id;
    uint32_t plane_ids[MAX_PLANES];
    int plane_count;
    uint32_t mode_blob_id;
    drmModeModeInfo mode;
    struct splash_buffer buffers[BUFFER_COUNT];
    int visible;
};

static volatile sig_atomic_t splash_running = 1;
static const char *open_failure_stage = "not-started";
static int open_failure_errno;

static void splash_log(const char *format, ...) {
    char buffer[384];
    va_list arguments;
    va_start(arguments, format);
    int length = vsnprintf(buffer, sizeof(buffer), format, arguments);
    va_end(arguments);
    if (length < 0) return;
    if ((size_t)length >= sizeof(buffer)) length = (int)sizeof(buffer) - 1;
    int descriptor = open("/dev/kmsg", O_WRONLY | O_CLOEXEC);
    if (descriptor < 0) return;
    dprintf(descriptor, "<6>xm14-early-splash: %.*s\n", length, buffer);
    close(descriptor);
}

static void stop_handler(int signal_number) {
    (void)signal_number;
    splash_running = 0;
}

static double seconds_now(void) {
    struct timespec value;
    clock_gettime(CLOCK_BOOTTIME, &value);
    return value.tv_sec + value.tv_nsec / 1000000000.0;
}

static int write_state_file(const char *path, const char *value) {
    int descriptor = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (descriptor < 0) return -1;
    if (value && *value) (void)write(descriptor, value, strlen(value));
    close(descriptor);
    return 0;
}

static uint32_t object_property(int descriptor, uint32_t object_id,
                                uint32_t object_type, const char *name) {
    uint32_t result = 0;
    drmModeObjectProperties *properties = drmModeObjectGetProperties(
        descriptor, object_id, object_type);
    if (!properties) return 0;
    for (uint32_t index = 0; index < properties->count_props; ++index) {
        drmModePropertyRes *property = drmModeGetProperty(
            descriptor, properties->props[index]);
        if (!property) continue;
        if (!strcmp(property->name, name)) result = property->prop_id;
        drmModeFreeProperty(property);
        if (result) break;
    }
    drmModeFreeObjectProperties(properties);
    return result;
}

static int atomic_add(struct splash_display *display,
                      drmModeAtomicReq *request, uint32_t object_id,
                      uint32_t object_type, const char *name,
                      uint64_t value) {
    uint32_t property = object_property(display->descriptor, object_id,
                                        object_type, name);
    if (!property) { errno = ENOENT; return -1; }
    return drmModeAtomicAddProperty(request, object_id, property, value) < 0 ?
        -1 : 0;
}

static int choose_crtc(int descriptor, drmModeRes *resources,
                       drmModeConnector *connector) {
    if (connector->encoder_id) {
        drmModeEncoder *encoder = drmModeGetEncoder(descriptor,
                                                    connector->encoder_id);
        if (encoder) {
            uint32_t identifier = encoder->crtc_id;
            drmModeFreeEncoder(encoder);
            if (identifier) return (int)identifier;
        }
    }
    for (int encoder_index = 0;
         encoder_index < connector->count_encoders; ++encoder_index) {
        drmModeEncoder *encoder = drmModeGetEncoder(
            descriptor, connector->encoders[encoder_index]);
        if (!encoder) continue;
        for (int crtc = 0; crtc < resources->count_crtcs; ++crtc) {
            if (encoder->possible_crtcs & (1U << crtc)) {
                int identifier = (int)resources->crtcs[crtc];
                drmModeFreeEncoder(encoder);
                return identifier;
            }
        }
        drmModeFreeEncoder(encoder);
    }
    return -1;
}

static int crtc_index(drmModeRes *resources, uint32_t crtc_id) {
    for (int index = 0; index < resources->count_crtcs; ++index)
        if (resources->crtcs[index] == crtc_id) return index;
    return -1;
}

static bool plane_has_format(drmModePlane *plane, uint32_t format) {
    for (uint32_t index = 0; index < plane->count_formats; ++index)
        if (plane->formats[index] == format) return true;
    return false;
}

static int select_planes(struct splash_display *display,
                         drmModeRes *resources) {
    int selected_crtc = crtc_index(resources, display->crtc_id);
    if (selected_crtc < 0) return -1;
    drmModePlaneRes *plane_resources = drmModeGetPlaneResources(
        display->descriptor);
    if (!plane_resources) return -1;
    for (int pass = 0; pass < 2 && display->plane_count < MAX_PLANES; ++pass) {
        for (uint32_t index = 0;
             index < plane_resources->count_planes &&
             display->plane_count < MAX_PLANES; ++index) {
            drmModePlane *plane = drmModeGetPlane(
                display->descriptor, plane_resources->planes[index]);
            uint64_t type = 0;
            bool duplicate = false;
            if (!plane) continue;
            for (int selected = 0; selected < display->plane_count; ++selected)
                if (display->plane_ids[selected] == plane->plane_id)
                    duplicate = true;
            uint32_t type_property = object_property(
                display->descriptor, plane->plane_id,
                DRM_MODE_OBJECT_PLANE, "type");
            if (type_property) {
                drmModeObjectProperties *properties =
                    drmModeObjectGetProperties(display->descriptor,
                        plane->plane_id, DRM_MODE_OBJECT_PLANE);
                if (properties) {
                    for (uint32_t property = 0;
                         property < properties->count_props; ++property)
                        if (properties->props[property] == type_property)
                            type = properties->prop_values[property];
                    drmModeFreeObjectProperties(properties);
                }
            }
            bool attached = plane->crtc_id == display->crtc_id;
            bool compatible =
                (plane->possible_crtcs & (1U << selected_crtc)) != 0;
            bool suitable = compatible && type == DRM_PLANE_TYPE_PRIMARY &&
                plane_has_format(plane, DRM_FORMAT_XBGR8888) && !duplicate &&
                ((pass == 0 && attached) || (pass == 1 && !attached));
            if (suitable)
                display->plane_ids[display->plane_count++] = plane->plane_id;
            drmModeFreePlane(plane);
        }
    }
    drmModeFreePlaneResources(plane_resources);
    return display->plane_count == MAX_PLANES ? 0 : -1;
}

static int create_buffer(struct splash_display *display,
                         struct splash_buffer *buffer) {
    struct drm_mode_create_dumb create = {0};
    create.width = (uint32_t)display->width;
    create.height = (uint32_t)display->height;
    create.bpp = 32;
    if (drmIoctl(display->descriptor, DRM_IOCTL_MODE_CREATE_DUMB,
                 &create) != 0) return -1;
    buffer->handle = create.handle;
    buffer->pitch = create.pitch;
    buffer->size = create.size;
    uint32_t handles[4] = {buffer->handle, 0, 0, 0};
    uint32_t pitches[4] = {buffer->pitch, 0, 0, 0};
    uint32_t offsets[4] = {0, 0, 0, 0};
    if (drmModeAddFB2(display->descriptor, (uint32_t)display->width,
                      (uint32_t)display->height, DRM_FORMAT_XBGR8888,
                      handles, pitches, offsets, &buffer->framebuffer_id,
                      0) != 0) return -1;
    struct drm_mode_map_dumb map = {.handle = buffer->handle};
    if (drmIoctl(display->descriptor, DRM_IOCTL_MODE_MAP_DUMB,
                 &map) != 0) return -1;
    buffer->pixels = mmap(NULL, buffer->size, PROT_READ | PROT_WRITE,
                          MAP_SHARED, display->descriptor, map.offset);
    if (buffer->pixels == MAP_FAILED) {
        buffer->pixels = NULL;
        return -1;
    }
    memset(buffer->pixels, 0, (size_t)buffer->size);
    return 0;
}

static void close_display(struct splash_display *display);

static int configure_plane(struct splash_display *display,
                           drmModeAtomicReq *request, int index,
                           uint32_t framebuffer_id) {
    int x = index == 0 ? 0 : display->width / 2;
    int width = index == 0 ? display->width / 2 :
                             display->width - display->width / 2;
    uint32_t plane = display->plane_ids[index];
    if (atomic_add(display, request, plane, DRM_MODE_OBJECT_PLANE,
                   "FB_ID", framebuffer_id) ||
        atomic_add(display, request, plane, DRM_MODE_OBJECT_PLANE,
                   "CRTC_ID", display->crtc_id) ||
        atomic_add(display, request, plane, DRM_MODE_OBJECT_PLANE,
                   "CRTC_X", (uint32_t)x) ||
        atomic_add(display, request, plane, DRM_MODE_OBJECT_PLANE,
                   "CRTC_Y", 0) ||
        atomic_add(display, request, plane, DRM_MODE_OBJECT_PLANE,
                   "CRTC_W", (uint32_t)width) ||
        atomic_add(display, request, plane, DRM_MODE_OBJECT_PLANE,
                   "CRTC_H", (uint32_t)display->height) ||
        atomic_add(display, request, plane, DRM_MODE_OBJECT_PLANE,
                   "SRC_X", (uint64_t)(uint32_t)x << 16) ||
        atomic_add(display, request, plane, DRM_MODE_OBJECT_PLANE,
                   "SRC_Y", 0) ||
        atomic_add(display, request, plane, DRM_MODE_OBJECT_PLANE,
                   "SRC_W", (uint64_t)(uint32_t)width << 16) ||
        atomic_add(display, request, plane, DRM_MODE_OBJECT_PLANE,
                   "SRC_H", (uint64_t)(uint32_t)display->height << 16))
        return -1;
    return 0;
}

static int present(struct splash_display *display, int index, bool modeset) {
    drmModeAtomicReq *request = drmModeAtomicAlloc();
    uint32_t flags = 0;
    if (!request) return -1;
    if (modeset) {
        if (drmModeCreatePropertyBlob(display->descriptor, &display->mode,
                                      sizeof(display->mode),
                                      &display->mode_blob_id) != 0 ||
            atomic_add(display, request, display->connector_id,
                       DRM_MODE_OBJECT_CONNECTOR, "CRTC_ID",
                       display->crtc_id) ||
            atomic_add(display, request, display->crtc_id,
                       DRM_MODE_OBJECT_CRTC, "MODE_ID",
                       display->mode_blob_id) ||
            atomic_add(display, request, display->crtc_id,
                       DRM_MODE_OBJECT_CRTC, "ACTIVE", 1)) {
            drmModeAtomicFree(request);
            return -1;
        }
        flags = DRM_MODE_ATOMIC_ALLOW_MODESET;
    }
    for (int plane = 0; plane < display->plane_count; ++plane) {
        if (configure_plane(display, request, plane,
                            display->buffers[index].framebuffer_id)) {
            drmModeAtomicFree(request);
            return -1;
        }
    }
    if (drmModeAtomicCommit(display->descriptor, request, flags, NULL) != 0) {
        drmModeAtomicFree(request);
        return -1;
    }
    drmModeAtomicFree(request);
    display->visible = index;
    return 0;
}

static int open_display(struct splash_display *display) {
    memset(display, 0, sizeof(*display));
    display->descriptor = -1;
    open_failure_stage = "open-card0";
    open_failure_errno = 0;
    display->descriptor = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
    if (display->descriptor < 0) goto fail;
    open_failure_stage = "drm-master";
    if (drmSetMaster(display->descriptor) != 0) goto fail;
    open_failure_stage = "universal-planes";
    if (drmSetClientCap(display->descriptor,
                        DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1) != 0) goto fail;
    open_failure_stage = "atomic-capability";
    if (drmSetClientCap(display->descriptor,
                        DRM_CLIENT_CAP_ATOMIC, 1) != 0) goto fail;
    open_failure_stage = "drm-resources";
    drmModeRes *resources = drmModeGetResources(display->descriptor);
    if (!resources) goto fail;
    drmModeConnector *connector = NULL;
    open_failure_stage = "connected-dsi";
    for (int index = 0; index < resources->count_connectors; ++index) {
        drmModeConnector *candidate = drmModeGetConnector(
            display->descriptor, resources->connectors[index]);
        if (candidate && candidate->connector_type == DRM_MODE_CONNECTOR_DSI &&
            candidate->connection == DRM_MODE_CONNECTED &&
            candidate->count_modes > 0) {
            connector = candidate;
            break;
        }
        if (candidate) drmModeFreeConnector(candidate);
    }
    if (!connector) {
        drmModeFreeResources(resources);
        errno = ENODEV;
        goto fail;
    }
    int mode_index = 0;
    for (int index = 0; index < connector->count_modes; ++index) {
        if (!strcmp(connector->modes[index].name, "1440x3200x60cmd")) {
            mode_index = index;
            break;
        }
        if (connector->modes[index].type & DRM_MODE_TYPE_PREFERRED)
            mode_index = index;
    }
    display->mode = connector->modes[mode_index];
    display->width = display->mode.hdisplay;
    display->height = display->mode.vdisplay;
    display->connector_id = connector->connector_id;
    int selected_crtc = choose_crtc(display->descriptor,
                                    resources, connector);
    if (selected_crtc < 0) {
        drmModeFreeConnector(connector);
        drmModeFreeResources(resources);
        errno = ENODEV;
        open_failure_stage = "crtc";
        goto fail;
    }
    display->crtc_id = (uint32_t)selected_crtc;
    open_failure_stage = "primary-planes";
    if (select_planes(display, resources) != 0) {
        drmModeFreeConnector(connector);
        drmModeFreeResources(resources);
        errno = ENODEV;
        goto fail;
    }
    drmModeFreeConnector(connector);
    drmModeFreeResources(resources);
    open_failure_stage = "dumb-buffers";
    for (int index = 0; index < BUFFER_COUNT; ++index)
        if (create_buffer(display, &display->buffers[index]) != 0) goto fail;
    open_failure_stage = "ready";
    return 0;

fail:
    open_failure_errno = errno ? errno : EIO;
    close_display(display);
    return -1;
}

static void close_display(struct splash_display *display) {
    if (display->descriptor < 0) return;
    for (int index = 0; index < BUFFER_COUNT; ++index) {
        struct splash_buffer *buffer = &display->buffers[index];
        if (buffer->pixels) munmap(buffer->pixels, buffer->size);
        if (buffer->framebuffer_id)
            drmModeRmFB(display->descriptor, buffer->framebuffer_id);
        if (buffer->handle) {
            struct drm_mode_destroy_dumb destroy = {.handle = buffer->handle};
            drmIoctl(display->descriptor, DRM_IOCTL_MODE_DESTROY_DUMB,
                     &destroy);
        }
    }
    if (display->mode_blob_id)
        drmModeDestroyPropertyBlob(display->descriptor,
                                   display->mode_blob_id);
    close(display->descriptor);
    display->descriptor = -1;
}

static void set_early_backlight(void) {
    const char *maximum_path =
        "/sys/class/backlight/panel0-backlight/max_brightness";
    const char *brightness_path =
        "/sys/class/backlight/panel0-backlight/brightness";
    FILE *maximum_file = fopen(maximum_path, "re");
    int maximum = 0;
    if (maximum_file) {
        (void)fscanf(maximum_file, "%d", &maximum);
        fclose(maximum_file);
    }
    if (maximum <= 0) return;
    FILE *brightness_file = fopen(brightness_path, "we");
    if (!brightness_file) return;
    fprintf(brightness_file, "%d\n", maximum * 60 / 100);
    fclose(brightness_file);
}

static void cleanup_state(void) {
    unlink(STARTING_FILE);
    unlink(ACTIVE_FILE);
    unlink(MASTER_RELEASED_FILE);
    unlink(TAKEOVER_REQUEST_FILE);
    unlink(TAKEOVER_COMPLETE_FILE);
    unlink(STOP_FILE);
}

int main(void) {
    signal(SIGINT, stop_handler);
    signal(SIGTERM, stop_handler);
    signal(SIGHUP, SIG_IGN);
    (void)mkdir(STATE_DIRECTORY, 0755);
    cleanup_state();
    (void)write_state_file(STARTING_FILE, "1\n");
    splash_log("started and waiting for the DSI panel");

    struct splash_display display;
    bool opened = false;
    const char *reported_stage = NULL;
    int reported_errno = -1;
    for (int attempt = 0; attempt < 1500 && splash_running &&
         access(STOP_FILE, F_OK) != 0; ++attempt) {
        if (open_display(&display) == 0) {
            opened = true;
            break;
        }
        if (open_failure_stage != reported_stage ||
            open_failure_errno != reported_errno || attempt % 250 == 249) {
            splash_log("DRM not ready at %s: %s (attempt %d)",
                       open_failure_stage, strerror(open_failure_errno),
                       attempt + 1);
            reported_stage = open_failure_stage;
            reported_errno = open_failure_errno;
        }
        usleep(20000);
    }
    if (!opened || display.width != 1440 || display.height != 3200) {
        if (opened) close_display(&display);
        splash_log("gave up before first frame%s",
                   splash_running ? "" : " after stop signal");
        cleanup_state();
        return 1;
    }

    double started = seconds_now();
    char started_text[64];
    snprintf(started_text, sizeof(started_text), "%.6f\n", started);
    (void)write_state_file(START_FILE, started_text);

    struct xm14_splash_canvas initial = {
        display.width, display.height, display.buffers[0].pitch,
        display.buffers[0].pixels
    };
    xm14_splash_render(&initial, 0.0);
    if (present(&display, 0, true) != 0) {
        splash_log("first atomic frame failed: %s", strerror(errno));
        close_display(&display);
        cleanup_state();
        return 1;
    }
    set_early_backlight();
    (void)write_state_file(ACTIVE_FILE, "1\n");
    unlink(STARTING_FILE);
    splash_log("first frame visible at %.3f seconds", seconds_now());

    bool master_released = false;
    double released_at = 0.0;
    double last_frame = 0.0;
    while (splash_running && access(STOP_FILE, F_OK) != 0 &&
           seconds_now() - started < 45.0) {
        double now = seconds_now();
        if (!master_released && access(TAKEOVER_REQUEST_FILE, F_OK) == 0) {
            if (drmDropMaster(display.descriptor) == 0) {
                master_released = true;
                released_at = now;
                (void)write_state_file(MASTER_RELEASED_FILE, "1\n");
            }
        }
        if (master_released) {
            if (access(TAKEOVER_COMPLETE_FILE, F_OK) == 0) break;
            if (now - released_at > 3.0) {
                unlink(MASTER_RELEASED_FILE);
                unlink(TAKEOVER_REQUEST_FILE);
                unlink(TAKEOVER_COMPLETE_FILE);
                if (drmSetMaster(display.descriptor) == 0) {
                    master_released = false;
                    last_frame = 0.0;
                } else {
                    released_at = now;
                }
            }
            usleep(10000);
            continue;
        }
        if (now - last_frame >= 1.0 / 20.0) {
            int next = display.visible ? 0 : 1;
            struct xm14_splash_canvas canvas = {
                display.width, display.height, display.buffers[next].pitch,
                display.buffers[next].pixels
            };
            xm14_splash_render(&canvas, now - started);
            if (present(&display, next, false) != 0) break;
            last_frame = now;
        }
        usleep(6000);
    }

    close_display(&display);
    cleanup_state();
    unlink(START_FILE);
    splash_log("display ownership handed over");
    return 0;
}

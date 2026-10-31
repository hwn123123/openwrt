// SPDX-License-Identifier: GPL-2.0-only
#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <signal.h>
#include <stdbool.h>
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
#include <qrencode.h>

#include "xm14-font-zh.h"
#include "xm14-font-cjk32.h"
#include "xm14pro-boot-scene.h"

#define BUFFER_COUNT 2
#define MAX_PLANES 2
#define HISTORY 36
#define PI 3.14159265358979323846
#define DASHBOARD_READY_FILE "/var/run/xm14display/dashboard-ready"
#define EARLY_SPLASH_STARTING_FILE "/run/xm14display/early-splash-starting"
#define EARLY_SPLASH_ACTIVE_FILE "/run/xm14display/early-splash-active"
#define SPLASH_START_FILE "/run/xm14display/splash-start"
#define TAKEOVER_REQUEST_FILE "/run/xm14display/takeover-request"
#define MASTER_RELEASED_FILE "/run/xm14display/master-released"
#define TAKEOVER_COMPLETE_FILE "/run/xm14display/takeover-complete"
#define EARLY_SPLASH_STOP_FILE "/run/xm14display/early-splash-stop"
#define VOLUME_OVERLAY_FILE "/var/run/xm14display/volume-overlay"
#define EXTERNAL_REFRESH_FILE "/var/run/xm14display/refresh"
#define BOOT_LOG_LINES 74
#define BOOT_LOG_LENGTH 112
#define FOOTER_TOP 2910
#define FOOTER_ITEM_WIDTH 288
#define BASE_PAGE_COUNT 10
#define PAGE_COUNT 12
#define FOOTER_VISIBLE_ITEMS 5

struct buffer {
    uint32_t handle;
    uint32_t pitch;
    uint64_t size;
    uint32_t fb_id;
    uint32_t *pixels;
};

struct display {
    int fd;
    int width;
    int height;
    uint32_t connector_id;
    uint32_t crtc_id;
    uint32_t plane_ids[MAX_PLANES];
    int plane_count;
    uint32_t mode_blob_id;
    drmModeModeInfo mode;
    struct buffer buffers[BUFFER_COUNT];
    int visible;
};

struct canvas {
    int width;
    int height;
    uint32_t pitch;
    uint32_t *pixels;
};

struct live_data {
    bool connected;
    char operator_name[96];
    char technology[64];
    char operator_code[24];
    char signal[48];
    int signal_quality;
    char band[64];
    char rsrq[48];
    char sinr[48];
    char pci[32];
    char cell_id[48];
    char tac[48];
    char slot[16];
    bool sim1_present;
    bool sim2_present;
    bool airplane_mode;
    bool roaming;
    char allowed_mode[64];
    char preferred_mode[32];
    char interface_name[48];
    char apn[96];
    char ipv4[64];
    char ipv6[128];
    char gateway[64];
    char dns[192];
    char mtu[24];
    char wifi_ssid[96];
    char wifi_key[128];
    char wifi_encryption[32];
    char wifi_ipv4[64];
    char usb_ipv4[64];
    bool wifi_up;
    bool usb_up;
    int wifi_clients;
    int cpu;
    int memory;
    char cpu_frequency[64];
    double cpu_temp;
    double modem_temp;
    uint64_t rx_bytes;
    uint64_t tx_bytes;
    uint64_t rx_packets;
    uint64_t tx_packets;
    uint64_t rx_errors;
    uint64_t tx_errors;
    uint64_t uptime;
    int storage;
    char kernel[128];
    char hardware_model[128];
    char cpu_architecture[64];
    char target_platform[64];
    char firmware_version[128];
    char soc_model[64];
    char modem_model[64];
    char modem_revision[96];
    char imei[32];
    char carrier_aggregation[96];
    char carrier_aggregation_state[24];
    char carrier_aggregation_capability[24];
    int carrier_count;
    char carrier_components[192];
    char active_channels[96];
    char channel_bandwidths[96];
    char arfcn[32];
    char nr_arfcn[32];
    bool bonding_enabled;
    char bonding_phase[48];
    int bonding_paths;
    bool bonding_balance_active;
    bool bonding_sim1_connected;
    char bonding_sim1_device[32];
    char bonding_sim1_address[64];
    bool bonding_sim2_connected;
    char bonding_sim2_device[32];
    char bonding_sim2_address[64];
    char ping_latency[32];
    int qrtr_services;
    bool voice_ready;
    bool sms_ready;
    bool telephony_pages;
    bool cell_show_imei;
    bool cell_show_cell;
    bool cell_show_aggregation;
    bool cell_show_signal;
    char sms_latest_number[48];
    char sms_latest_text[256];
    char sms_latest_direction[12];
    char sms_latest_time[40];
    char sms_latest_delivery[20];
    char sms_draft_number[48];
    char sms_draft_text[256];
    char sms_focus[16];
    char sms_result[160];
    int active_call_count;
    int active_call_id;
    char active_call_state[32];
    char active_call_number[48];
    bool voice_audio_active;
    char voice_audio_route[20];
    bool voice_mic_muted;
    int voice_call_volume;
    char phone_draft_number[48];
    char phone_result[160];
    bool nfc_enabled;
    bool nfc_node;
    bool nfc_hal_running;
    bool nfc_service_running;
    bool nfc_ndef_available;
    char nfc_mode[16];
    char nfc_stage[32];
    char nfc_detail[128];
    int nfc_source_bytes;
    int nfc_saved;
    char nfc_selected_name[96];
    bool infrared_enabled;
    bool infrared_node;
    bool infrared_hal_running;
    int infrared_saved;
    int infrared_catalog;
    char infrared_selected_name[96];
    bool infrared_selected_ac;
    bool infrared_ac_power;
    int infrared_ac_temp;
    char infrared_ac_mode[24];
    char infrared_ac_fan[24];
    bool infrared_ac_swingv;
    bool infrared_ac_swingh;
    char nfc_last_result[96];
    char infrared_last_result[96];
    bool battery_available;
    int battery;
    char battery_status[48];
    char battery_charge_type[32];
    char battery_protocol[32];
    double battery_temp;
    double connector_temp;
    double input_power;
    int input_online;
    int battery_fastchg_mode;
    int battery_quick_charge_type;
    char battery_health[32];
    char battery_technology[32];
    char battery_model[64];
    char battery_cell_vendor[32];
    char battery_pack_vendor[32];
    int battery_soh;
    int battery_cycle_count;
    int battery_moisture;
    int battery_anti_burn;
    int battery_input_suspend;
    int battery_night_charging;
    int battery_reverse_mode;
    int battery_reverse_state;
    int battery_cp_master;
    int battery_cp_slave;
    int64_t battery_voltage;
    int64_t battery_current;
    int64_t battery_charge_full;
    int64_t battery_charge_design;
    int64_t input_voltage;
    int64_t input_current;
    int64_t input_current_limit;
    char typec_data_role[48];
    char typec_power_role[48];
    char typec_power_mode[48];
    char typec_pd_revision[24];
    char typec_revision[24];
    char speed_state[24];
    char speed_phase[24];
    char speed_message[192];
    double speed_download_mbps;
    double speed_upload_mbps;
    int speed_servers;
    int speed_threads;
    int speed_duration;
    int brightness;
    int screen_timeout;
    int refresh_interval;
    bool always_on;
};

static void fit_text(struct canvas *c, int x, int y, int width,
                     const char *value, int size, uint32_t color);

static volatile sig_atomic_t running = 1;
static bool early_handoff_requested;
static int footer_scroll_px;
static int active_page_count = BASE_PAGE_COUNT;
static uint64_t previous_rx;
static uint64_t previous_tx;
static time_t previous_sample;
static double rx_history[HISTORY];
static double tx_history[HISTORY];
static double speed_dial_position;
static double speed_dial_target_position;
static double speed_dial_value;
static double speed_download_peak;
static double speed_upload_peak;
static double speed_download_display;
static double speed_upload_display;
static struct timespec speed_animation_sample;
static bool speed_animation_initialized;
static char speed_previous_state[24];
static uint64_t fast_cpu_previous_total;
static uint64_t fast_cpu_previous_idle;

struct volume_overlay_state {
    bool initialized;
    bool active;
    unsigned long long sequence;
    char context[16];
    int volume;
    double shown_at;
    double last_changed_at;
};

static struct volume_overlay_state volume_overlay;

struct fast_rate_sampler {
    bool initialized;
    char interface_name[48];
    uint64_t rx_bytes;
    uint64_t tx_bytes;
    struct timespec sampled_at;
    double smooth_rx;
    double smooth_tx;
};

static const uint32_t C_BLACK = 0xff000000U;
static const uint32_t C_CARD_2 = 0xff101719U;
static const uint32_t C_BORDER = 0xff174b51U;
static const uint32_t C_WHITE = 0xfff5f7faU;
static const uint32_t C_MUTED = 0xff8c949fU;
static const uint32_t C_CYAN = 0xff36dfd0U;
static const uint32_t C_GREEN = 0xff28dc72U;
static const uint32_t C_BLUE = 0xffff941fU;
static const uint32_t C_TRACK = 0xff26363cU;

static uint32_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return 0xff000000U | ((uint32_t)b << 16) | ((uint32_t)g << 8) | r;
}

static uint32_t mix(uint32_t a, uint32_t b, int amount) {
    int ar = a & 255, ag = (a >> 8) & 255, ab = (a >> 16) & 255;
    int br = b & 255, bg = (b >> 8) & 255, bb = (b >> 16) & 255;
    return rgb((uint8_t)((ar * (100 - amount) + br * amount) / 100),
               (uint8_t)((ag * (100 - amount) + bg * amount) / 100),
               (uint8_t)((ab * (100 - amount) + bb * amount) / 100));
}

static void blend_pixel(struct canvas *c, int x, int y, uint32_t color,
                        uint8_t alpha) {
    if (x < 0 || y < 0 || x >= c->width || y >= c->height || !alpha) return;
    uint32_t *pixel = (uint32_t *)((uint8_t *)c->pixels + (size_t)y * c->pitch) + x;
    if (alpha == 255) { *pixel = color; return; }
    uint32_t old = *pixel;
    int inv = 255 - alpha;
    *pixel = rgb((uint8_t)((((color & 255) * alpha) + ((old & 255) * inv)) / 255),
                 (uint8_t)(((((color >> 8) & 255) * alpha) + (((old >> 8) & 255) * inv)) / 255),
                 (uint8_t)(((((color >> 16) & 255) * alpha) + (((old >> 16) & 255) * inv)) / 255));
}

static void fill_rect(struct canvas *c, int x, int y, int w, int h,
                      uint32_t color) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > c->width) w = c->width - x;
    if (y + h > c->height) h = c->height - y;
    if (w <= 0 || h <= 0) return;
    for (int row = y; row < y + h; ++row) {
        uint32_t *dst = (uint32_t *)((uint8_t *)c->pixels + (size_t)row * c->pitch) + x;
        for (int col = 0; col < w; ++col) dst[col] = color;
    }
}

static int isqrt_int(int value) {
    int result = 0;
    while ((result + 1) * (result + 1) <= value) ++result;
    return result;
}

static void round_rect(struct canvas *c, int x, int y, int w, int h,
                       int radius, uint32_t color) {
    if (radius < 1) { fill_rect(c, x, y, w, h, color); return; }
    if (radius * 2 > w) radius = w / 2;
    if (radius * 2 > h) radius = h / 2;
    for (int row = 0; row < h; ++row) {
        int inset = 0;
        if (row < radius) {
            int dy = radius - row - 1;
            inset = radius - isqrt_int(radius * radius - dy * dy);
        } else if (row >= h - radius) {
            int dy = row - (h - radius);
            inset = radius - isqrt_int(radius * radius - dy * dy);
        }
        fill_rect(c, x + inset, y + row, w - inset * 2, 1, color);
    }
}

static void framed_round_rect(struct canvas *c, int x, int y, int w, int h,
                              int radius, int border, uint32_t frame,
                              uint32_t inside) {
    round_rect(c, x, y, w, h, radius, frame);
    round_rect(c, x + border, y + border, w - border * 2, h - border * 2,
               radius - border, inside);
}

static void circle(struct canvas *c, int cx, int cy, int radius,
                   uint32_t color) {
    for (int y = -radius; y <= radius; ++y) {
        int half = isqrt_int(radius * radius - y * y);
        fill_rect(c, cx - half, cy + y, half * 2 + 1, 1, color);
    }
}

static void line(struct canvas *c, int x0, int y0, int x1, int y1,
                 int thickness, uint32_t color) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    for (;;) {
        circle(c, x0, y0, thickness / 2, color);
        if (x0 == x1 && y0 == y1) break;
        int twice = error * 2;
        if (twice >= dy) { error += dy; x0 += sx; }
        if (twice <= dx) { error += dx; y0 += sy; }
    }
}

static void arc(struct canvas *c, int cx, int cy, int radius, double start,
                double end, int thickness, uint32_t color) {
    int steps = (int)(fabs(end - start) * radius / 3.0);
    if (steps < 12) steps = 12;
    int px = cx + (int)(cos(start) * radius);
    int py = cy + (int)(sin(start) * radius);
    for (int i = 1; i <= steps; ++i) {
        double angle = start + (end - start) * i / steps;
        int nx = cx + (int)(cos(angle) * radius);
        int ny = cy + (int)(sin(angle) * radius);
        line(c, px, py, nx, ny, thickness, color);
        px = nx; py = ny;
    }
}

static uint32_t next_codepoint(const char **cursor) {
    const unsigned char *p = (const unsigned char *)*cursor;
    uint32_t value;
    if (p[0] < 0x80) { value = p[0]; *cursor += 1; }
    else if ((p[0] & 0xe0) == 0xc0 && p[1]) {
        value = ((uint32_t)(p[0] & 0x1f) << 6) | (p[1] & 0x3f); *cursor += 2;
    } else if ((p[0] & 0xf0) == 0xe0 && p[1] && p[2]) {
        value = ((uint32_t)(p[0] & 15) << 12) |
                ((uint32_t)(p[1] & 63) << 6) | (p[2] & 63); *cursor += 3;
    } else { value = '?'; *cursor += 1; }
    return value;
}

static const struct ui_font_glyph *glyph_exact(uint32_t codepoint) {
    size_t low = 0, high = UI_FONT_GLYPH_COUNT;
    while (low < high) {
        size_t middle = low + (high - low) / 2;
        if (ui_font_glyphs[middle].codepoint == codepoint) return &ui_font_glyphs[middle];
        if (ui_font_glyphs[middle].codepoint < codepoint) low = middle + 1;
        else high = middle;
    }
    return NULL;
}

static bool has_cjk32(uint32_t codepoint) {
    return codepoint >= UI_CJK32_FIRST && codepoint <= UI_CJK32_LAST;
}

static void draw_cjk32(struct canvas *c, int x, int y, uint32_t codepoint,
                       int size, uint32_t color) {
    size_t base = (size_t)(codepoint - UI_CJK32_FIRST) * UI_CJK32_SIZE;
    for (int dy = 0; dy < size; ++dy) {
        unsigned int source_y = (unsigned int)dy * UI_CJK32_SIZE /
                                (unsigned int)size;
        uint32_t row = ui_cjk32_rows[base + source_y];
        for (int dx = 0; dx < size; ++dx) {
            unsigned int source_x = (unsigned int)dx * UI_CJK32_SIZE /
                                    (unsigned int)size;
            if (row & (1U << (UI_CJK32_SIZE - 1U - source_x)))
                blend_pixel(c, x + dx, y + dy, color, 255);
        }
    }
}

static int text_width(const char *text, int size) {
    int width = 0;
    const char *cursor = text;
    while (*cursor) {
        uint32_t codepoint = next_codepoint(&cursor);
        const struct ui_font_glyph *glyph = glyph_exact(codepoint);
        if (glyph)
            width += ((int)glyph->advance * size + UI_FONT_EM / 2) / UI_FONT_EM;
        else if (has_cjk32(codepoint))
            width += size;
        else {
            glyph = glyph_exact('?');
            if (glyph)
                width += ((int)glyph->advance * size + UI_FONT_EM / 2) / UI_FONT_EM;
        }
    }
    return width;
}

static void text(struct canvas *c, int x, int y, const char *value, int size,
                 uint32_t color) {
    const char *cursor = value;
    int baseline = y + (UI_FONT_ASCENT * size) / UI_FONT_EM;
    while (*cursor) {
        uint32_t codepoint = next_codepoint(&cursor);
        const struct ui_font_glyph *glyph = glyph_exact(codepoint);
        if (!glyph && has_cjk32(codepoint)) {
            draw_cjk32(c, x, baseline - size * 7 / 8, codepoint, size, color);
            x += size;
            continue;
        }
        if (!glyph) glyph = glyph_exact('?');
        if (!glyph) continue;
        int left = x + ((int)glyph->bearing_x * size) / UI_FONT_EM;
        int top = baseline + ((int)glyph->bearing_y * size) / UI_FONT_EM;
        int width = ((int)glyph->width * size + UI_FONT_EM - 1) / UI_FONT_EM;
        int height = ((int)glyph->height * size + UI_FONT_EM - 1) / UI_FONT_EM;
        for (int dy = 0; dy < height; ++dy) {
            int source_y = dy * glyph->height / height;
            for (int dx = 0; dx < width; ++dx) {
                int source_x = dx * glyph->width / width;
                uint8_t alpha = ui_font_bitmap[glyph->offset +
                    (uint32_t)source_y * glyph->width + (uint32_t)source_x];
                blend_pixel(c, left + dx, top + dy, color, alpha);
            }
        }
        x += ((int)glyph->advance * size + UI_FONT_EM / 2) / UI_FONT_EM;
    }
}

static void bold_text(struct canvas *c, int x, int y, const char *value,
                      int size, uint32_t color) {
    text(c, x, y, value, size, color);
    text(c, x + 2, y, value, size, color);
}

static void centered_text(struct canvas *c, int cx, int y, const char *value,
                          int size, uint32_t color) {
    text(c, cx - text_width(value, size) / 2, y, value, size, color);
}

static uint32_t object_property(int fd, uint32_t object_id, uint32_t object_type,
                                const char *name) {
    uint32_t result = 0;
    drmModeObjectProperties *properties =
        drmModeObjectGetProperties(fd, object_id, object_type);
    if (!properties) return 0;
    for (uint32_t i = 0; i < properties->count_props; ++i) {
        drmModePropertyRes *property = drmModeGetProperty(fd, properties->props[i]);
        if (!property) continue;
        if (!strcmp(property->name, name)) result = property->prop_id;
        drmModeFreeProperty(property);
        if (result) break;
    }
    drmModeFreeObjectProperties(properties);
    return result;
}

static int atomic_add(struct display *d, drmModeAtomicReq *request,
                      uint32_t object_id, uint32_t object_type,
                      const char *name, uint64_t value) {
    uint32_t property = object_property(d->fd, object_id, object_type, name);
    if (!property) { errno = ENOENT; return -1; }
    return drmModeAtomicAddProperty(request, object_id, property, value) < 0 ? -1 : 0;
}

static int choose_crtc(int fd, drmModeRes *resources,
                       drmModeConnector *connector) {
    if (connector->encoder_id) {
        drmModeEncoder *encoder = drmModeGetEncoder(fd, connector->encoder_id);
        if (encoder) {
            uint32_t id = encoder->crtc_id;
            drmModeFreeEncoder(encoder);
            if (id) return (int)id;
        }
    }
    for (int e = 0; e < connector->count_encoders; ++e) {
        drmModeEncoder *encoder = drmModeGetEncoder(fd, connector->encoders[e]);
        if (!encoder) continue;
        for (int c = 0; c < resources->count_crtcs; ++c) {
            if (encoder->possible_crtcs & (1U << c)) {
                int id = (int)resources->crtcs[c];
                drmModeFreeEncoder(encoder);
                return id;
            }
        }
        drmModeFreeEncoder(encoder);
    }
    return -1;
}

static int crtc_index(drmModeRes *resources, uint32_t crtc_id) {
    for (int i = 0; i < resources->count_crtcs; ++i)
        if (resources->crtcs[i] == crtc_id) return i;
    return -1;
}

static bool plane_has_format(drmModePlane *plane, uint32_t format) {
    for (uint32_t i = 0; i < plane->count_formats; ++i)
        if (plane->formats[i] == format) return true;
    return false;
}

static int select_planes(struct display *d, drmModeRes *resources) {
    int index = crtc_index(resources, d->crtc_id);
    drmModePlaneRes *plane_resources;
    if (index < 0) return -1;
    plane_resources = drmModeGetPlaneResources(d->fd);
    if (!plane_resources) return -1;
    for (int pass = 0; pass < 2 && d->plane_count < MAX_PLANES; ++pass) {
        for (uint32_t i = 0; i < plane_resources->count_planes &&
                             d->plane_count < MAX_PLANES; ++i) {
            drmModePlane *plane = drmModeGetPlane(d->fd, plane_resources->planes[i]);
            uint64_t type = 0;
            bool duplicate = false;
            if (!plane) continue;
            for (int p = 0; p < d->plane_count; ++p)
                if (d->plane_ids[p] == plane->plane_id) duplicate = true;
            uint32_t type_property = object_property(d->fd, plane->plane_id,
                                                     DRM_MODE_OBJECT_PLANE, "type");
            if (type_property) {
                drmModeObjectProperties *props = drmModeObjectGetProperties(
                    d->fd, plane->plane_id, DRM_MODE_OBJECT_PLANE);
                if (props) {
                    for (uint32_t p = 0; p < props->count_props; ++p)
                        if (props->props[p] == type_property) type = props->prop_values[p];
                    drmModeFreeObjectProperties(props);
                }
            }
            bool attached = plane->crtc_id == d->crtc_id;
            bool compatible = (plane->possible_crtcs & (1U << index)) != 0;
            bool suitable = compatible && type == DRM_PLANE_TYPE_PRIMARY &&
                plane_has_format(plane, DRM_FORMAT_XBGR8888) && !duplicate &&
                ((pass == 0 && attached) || (pass == 1 && !attached));
            if (suitable) d->plane_ids[d->plane_count++] = plane->plane_id;
            drmModeFreePlane(plane);
        }
    }
    drmModeFreePlaneResources(plane_resources);
    return d->plane_count == MAX_PLANES ? 0 : -1;
}

static int create_buffer(struct display *d, struct buffer *buffer) {
    struct drm_mode_create_dumb create = {0};
    create.width = (uint32_t)d->width;
    create.height = (uint32_t)d->height;
    create.bpp = 32;
    if (drmIoctl(d->fd, DRM_IOCTL_MODE_CREATE_DUMB, &create) != 0) return -1;
    buffer->handle = create.handle;
    buffer->pitch = create.pitch;
    buffer->size = create.size;
    uint32_t handles[4] = {buffer->handle, 0, 0, 0};
    uint32_t pitches[4] = {buffer->pitch, 0, 0, 0};
    uint32_t offsets[4] = {0, 0, 0, 0};
    if (drmModeAddFB2(d->fd, (uint32_t)d->width, (uint32_t)d->height,
                      DRM_FORMAT_XBGR8888, handles, pitches, offsets,
                      &buffer->fb_id, 0) != 0) return -1;
    struct drm_mode_map_dumb map = {.handle = buffer->handle};
    if (drmIoctl(d->fd, DRM_IOCTL_MODE_MAP_DUMB, &map) != 0) return -1;
    buffer->pixels = mmap(NULL, buffer->size, PROT_READ | PROT_WRITE,
                          MAP_SHARED, d->fd, map.offset);
    if (buffer->pixels == MAP_FAILED) { buffer->pixels = NULL; return -1; }
    memset(buffer->pixels, 0, (size_t)buffer->size);
    return 0;
}

static int configure_plane(struct display *d, drmModeAtomicReq *request,
                           int index, uint32_t fb_id) {
    int x = index == 0 ? 0 : d->width / 2;
    int width = index == 0 ? d->width / 2 : d->width - d->width / 2;
    uint32_t plane = d->plane_ids[index];
    if (atomic_add(d, request, plane, DRM_MODE_OBJECT_PLANE, "FB_ID", fb_id) ||
        atomic_add(d, request, plane, DRM_MODE_OBJECT_PLANE, "CRTC_ID", d->crtc_id) ||
        atomic_add(d, request, plane, DRM_MODE_OBJECT_PLANE, "CRTC_X", (uint32_t)x) ||
        atomic_add(d, request, plane, DRM_MODE_OBJECT_PLANE, "CRTC_Y", 0) ||
        atomic_add(d, request, plane, DRM_MODE_OBJECT_PLANE, "CRTC_W", (uint32_t)width) ||
        atomic_add(d, request, plane, DRM_MODE_OBJECT_PLANE, "CRTC_H", (uint32_t)d->height) ||
        atomic_add(d, request, plane, DRM_MODE_OBJECT_PLANE, "SRC_X", (uint64_t)(uint32_t)x << 16) ||
        atomic_add(d, request, plane, DRM_MODE_OBJECT_PLANE, "SRC_Y", 0) ||
        atomic_add(d, request, plane, DRM_MODE_OBJECT_PLANE, "SRC_W", (uint64_t)(uint32_t)width << 16) ||
        atomic_add(d, request, plane, DRM_MODE_OBJECT_PLANE, "SRC_H", (uint64_t)(uint32_t)d->height << 16))
        return -1;
    return 0;
}

static int present(struct display *d, int index, bool modeset) {
    drmModeAtomicReq *request = drmModeAtomicAlloc();
    uint32_t flags = 0;
    if (!request) return -1;
    if (modeset) {
        if (drmModeCreatePropertyBlob(d->fd, &d->mode, sizeof(d->mode),
                                      &d->mode_blob_id) != 0 ||
            atomic_add(d, request, d->connector_id, DRM_MODE_OBJECT_CONNECTOR,
                       "CRTC_ID", d->crtc_id) ||
            atomic_add(d, request, d->crtc_id, DRM_MODE_OBJECT_CRTC,
                       "MODE_ID", d->mode_blob_id) ||
            atomic_add(d, request, d->crtc_id, DRM_MODE_OBJECT_CRTC,
                       "ACTIVE", 1)) {
            drmModeAtomicFree(request); return -1;
        }
        flags = DRM_MODE_ATOMIC_ALLOW_MODESET;
    }
    for (int plane = 0; plane < d->plane_count; ++plane) {
        if (configure_plane(d, request, plane, d->buffers[index].fb_id)) {
            drmModeAtomicFree(request); return -1;
        }
    }
    if (drmModeAtomicCommit(d->fd, request, flags, NULL) != 0) {
        drmModeAtomicFree(request); return -1;
    }
    drmModeAtomicFree(request);
    d->visible = index;
    return 0;
}

static int open_display(struct display *d) {
    memset(d, 0, sizeof(*d));
    d->fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
    if (d->fd < 0) return -1;
    if (drmSetMaster(d->fd) != 0 ||
        drmSetClientCap(d->fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1) != 0 ||
        drmSetClientCap(d->fd, DRM_CLIENT_CAP_ATOMIC, 1) != 0) return -1;
    drmModeRes *resources = drmModeGetResources(d->fd);
    if (!resources) return -1;
    drmModeConnector *connector = NULL;
    for (int i = 0; i < resources->count_connectors; ++i) {
        drmModeConnector *candidate = drmModeGetConnector(d->fd, resources->connectors[i]);
        if (candidate && candidate->connector_type == DRM_MODE_CONNECTOR_DSI &&
            candidate->connection == DRM_MODE_CONNECTED && candidate->count_modes > 0) {
            connector = candidate; break;
        }
        if (candidate) drmModeFreeConnector(candidate);
    }
    if (!connector) { drmModeFreeResources(resources); return -1; }
    int mode_index = 0;
    for (int i = 0; i < connector->count_modes; ++i) {
        if (!strcmp(connector->modes[i].name, "1440x3200x60cmd")) {
            mode_index = i; break;
        }
        if (connector->modes[i].type & DRM_MODE_TYPE_PREFERRED) mode_index = i;
    }
    d->mode = connector->modes[mode_index];
    d->width = d->mode.hdisplay;
    d->height = d->mode.vdisplay;
    d->connector_id = connector->connector_id;
    int crtc = choose_crtc(d->fd, resources, connector);
    if (crtc < 0) { drmModeFreeConnector(connector); drmModeFreeResources(resources); return -1; }
    d->crtc_id = (uint32_t)crtc;
    if (select_planes(d, resources) != 0) {
        drmModeFreeConnector(connector); drmModeFreeResources(resources); return -1;
    }
    drmModeFreeConnector(connector);
    drmModeFreeResources(resources);
    for (int i = 0; i < BUFFER_COUNT; ++i)
        if (create_buffer(d, &d->buffers[i]) != 0) return -1;
    return 0;
}

static void close_display(struct display *d) {
    for (int i = 0; i < BUFFER_COUNT; ++i) {
        struct buffer *buffer = &d->buffers[i];
        if (buffer->pixels) munmap(buffer->pixels, buffer->size);
        if (buffer->fb_id) drmModeRmFB(d->fd, buffer->fb_id);
        if (buffer->handle) {
            struct drm_mode_destroy_dumb destroy = {.handle = buffer->handle};
            drmIoctl(d->fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
        }
    }
    if (d->mode_blob_id) drmModeDestroyPropertyBlob(d->fd, d->mode_blob_id);
    if (d->fd >= 0) close(d->fd);
}

static void copy_value(char *target, size_t size, const char *value) {
    snprintf(target, size, "%s", value ? value : "");
}

static bool parse_bool(const char *value) {
    return value && (!strcmp(value, "1") || !strcmp(value, "true") ||
                     !strcmp(value, "yes") || !strcmp(value, "up"));
}

static void data_defaults(struct live_data *data) {
    memset(data, 0, sizeof(*data));
    copy_value(data->operator_name, sizeof(data->operator_name), "等待网络");
    copy_value(data->technology, sizeof(data->technology), "移动网络");
    copy_value(data->operator_code, sizeof(data->operator_code), "--");
    copy_value(data->signal, sizeof(data->signal), "-- dBm");
    data->signal_quality = -1;
    copy_value(data->band, sizeof(data->band), "--");
    copy_value(data->rsrq, sizeof(data->rsrq), "--");
    copy_value(data->sinr, sizeof(data->sinr), "--");
    copy_value(data->pci, sizeof(data->pci), "--");
    copy_value(data->cell_id, sizeof(data->cell_id), "--");
    copy_value(data->tac, sizeof(data->tac), "--");
    copy_value(data->slot, sizeof(data->slot), "自动");
    copy_value(data->interface_name, sizeof(data->interface_name), "--");
    copy_value(data->apn, sizeof(data->apn), "未获取");
    copy_value(data->ipv4, sizeof(data->ipv4), "等待地址");
    copy_value(data->ipv6, sizeof(data->ipv6), "等待地址");
    copy_value(data->gateway, sizeof(data->gateway), "--");
    copy_value(data->dns, sizeof(data->dns), "--");
    copy_value(data->mtu, sizeof(data->mtu), "--");
    copy_value(data->wifi_ssid, sizeof(data->wifi_ssid), "未获取");
    copy_value(data->wifi_encryption, sizeof(data->wifi_encryption), "WPA");
    copy_value(data->wifi_ipv4, sizeof(data->wifi_ipv4), "未获取");
    copy_value(data->usb_ipv4, sizeof(data->usb_ipv4), "未获取");
    copy_value(data->cpu_frequency, sizeof(data->cpu_frequency), "--");
    copy_value(data->hardware_model, sizeof(data->hardware_model), "--");
    copy_value(data->cpu_architecture, sizeof(data->cpu_architecture), "--");
    copy_value(data->target_platform, sizeof(data->target_platform), "--");
    copy_value(data->firmware_version, sizeof(data->firmware_version), "--");
    copy_value(data->soc_model, sizeof(data->soc_model), "--");
    copy_value(data->modem_model, sizeof(data->modem_model), "--");
    copy_value(data->modem_revision, sizeof(data->modem_revision), "--");
    copy_value(data->imei, sizeof(data->imei), "正在读取");
    copy_value(data->carrier_aggregation, sizeof(data->carrier_aggregation), "暂未获得实时分量");
    copy_value(data->carrier_aggregation_state, sizeof(data->carrier_aggregation_state), "unavailable");
    copy_value(data->carrier_aggregation_capability, sizeof(data->carrier_aggregation_capability), "supported");
    copy_value(data->carrier_components, sizeof(data->carrier_components), "--");
    copy_value(data->active_channels, sizeof(data->active_channels), "--");
    copy_value(data->channel_bandwidths, sizeof(data->channel_bandwidths), "--");
    copy_value(data->arfcn, sizeof(data->arfcn), "--");
    copy_value(data->nr_arfcn, sizeof(data->nr_arfcn), "--");
    copy_value(data->bonding_phase, sizeof(data->bonding_phase), "未启用");
    copy_value(data->bonding_sim1_device, sizeof(data->bonding_sim1_device), "--");
    copy_value(data->bonding_sim1_address, sizeof(data->bonding_sim1_address), "--");
    copy_value(data->bonding_sim2_device, sizeof(data->bonding_sim2_device), "--");
    copy_value(data->bonding_sim2_address, sizeof(data->bonding_sim2_address), "--");
    copy_value(data->ping_latency, sizeof(data->ping_latency), "--");
    copy_value(data->sms_latest_number, sizeof(data->sms_latest_number), "暂无短信");
    copy_value(data->sms_latest_text, sizeof(data->sms_latest_text), "LuCI 开启可在本机收发短信");
    copy_value(data->sms_latest_direction, sizeof(data->sms_latest_direction), "in");
    copy_value(data->sms_latest_delivery, sizeof(data->sms_latest_delivery), "pending");
    copy_value(data->sms_focus, sizeof(data->sms_focus), "number");
    copy_value(data->active_call_state, sizeof(data->active_call_state), "idle");
    copy_value(data->voice_audio_route, sizeof(data->voice_audio_route), "earpiece");
    data->voice_call_volume = 50;
    copy_value(data->nfc_last_result, sizeof(data->nfc_last_result), "尚未执行硬件自检");
    copy_value(data->infrared_last_result, sizeof(data->infrared_last_result), "尚未执行发射测试");
    copy_value(data->nfc_selected_name, sizeof(data->nfc_selected_name), "未选择");
    copy_value(data->nfc_mode, sizeof(data->nfc_mode), "share");
    copy_value(data->nfc_stage, sizeof(data->nfc_stage), "starting");
    copy_value(data->nfc_detail, sizeof(data->nfc_detail), "正在启动 NFC 服务");
    copy_value(data->infrared_selected_name, sizeof(data->infrared_selected_name), "未选择");
    data->infrared_ac_temp = 26;
    copy_value(data->infrared_ac_mode, sizeof(data->infrared_ac_mode), "cool");
    copy_value(data->infrared_ac_fan, sizeof(data->infrared_ac_fan), "auto");
    copy_value(data->battery_health, sizeof(data->battery_health), "--");
    copy_value(data->battery_technology, sizeof(data->battery_technology), "--");
    copy_value(data->battery_model, sizeof(data->battery_model), "--");
    copy_value(data->battery_cell_vendor, sizeof(data->battery_cell_vendor), "--");
    copy_value(data->battery_pack_vendor, sizeof(data->battery_pack_vendor), "--");
    copy_value(data->typec_data_role, sizeof(data->typec_data_role), "--");
    copy_value(data->typec_power_role, sizeof(data->typec_power_role), "--");
    copy_value(data->typec_power_mode, sizeof(data->typec_power_mode), "--");
    copy_value(data->typec_pd_revision, sizeof(data->typec_pd_revision), "--");
    copy_value(data->typec_revision, sizeof(data->typec_revision), "--");
    copy_value(data->speed_state, sizeof(data->speed_state), "idle");
    copy_value(data->speed_phase, sizeof(data->speed_phase), "idle");
    copy_value(data->speed_message, sizeof(data->speed_message), "点击表盘开始聚合测速");
    data->speed_duration = 5;
    data->brightness = 72;
    data->screen_timeout = 120;
    data->refresh_interval = 10;
    data->cell_show_imei = true;
    data->cell_show_cell = true;
    data->cell_show_aggregation = true;
    data->cell_show_signal = true;
}

static void assign_field(struct live_data *data, const char *key,
                         const char *value) {
    if (!strcmp(key, "connected")) data->connected = parse_bool(value);
    else if (!strcmp(key, "operator")) copy_value(data->operator_name, sizeof(data->operator_name), value);
    else if (!strcmp(key, "technology")) copy_value(data->technology, sizeof(data->technology), value);
    else if (!strcmp(key, "operator_code")) copy_value(data->operator_code, sizeof(data->operator_code), value);
    else if (!strcmp(key, "signal")) copy_value(data->signal, sizeof(data->signal), value);
    else if (!strcmp(key, "signal_quality"))
        data->signal_quality = value && *value ? atoi(value) : -1;
    else if (!strcmp(key, "band")) copy_value(data->band, sizeof(data->band), value);
    else if (!strcmp(key, "rsrq")) copy_value(data->rsrq, sizeof(data->rsrq), value);
    else if (!strcmp(key, "sinr")) copy_value(data->sinr, sizeof(data->sinr), value);
    else if (!strcmp(key, "pci")) copy_value(data->pci, sizeof(data->pci), value);
    else if (!strcmp(key, "cell_id")) copy_value(data->cell_id, sizeof(data->cell_id), value);
    else if (!strcmp(key, "tac")) copy_value(data->tac, sizeof(data->tac), value);
    else if (!strcmp(key, "slot")) copy_value(data->slot, sizeof(data->slot), value);
    else if (!strcmp(key, "sim1_present")) data->sim1_present = parse_bool(value);
    else if (!strcmp(key, "sim2_present")) data->sim2_present = parse_bool(value);
    else if (!strcmp(key, "airplane_mode")) data->airplane_mode = parse_bool(value);
    else if (!strcmp(key, "roaming")) data->roaming = parse_bool(value);
    else if (!strcmp(key, "allowed_mode")) copy_value(data->allowed_mode, sizeof(data->allowed_mode), value);
    else if (!strcmp(key, "preferred_mode")) copy_value(data->preferred_mode, sizeof(data->preferred_mode), value);
    else if (!strcmp(key, "interface")) copy_value(data->interface_name, sizeof(data->interface_name), value);
    else if (!strcmp(key, "apn")) copy_value(data->apn, sizeof(data->apn), value);
    else if (!strcmp(key, "ipv4")) copy_value(data->ipv4, sizeof(data->ipv4), value);
    else if (!strcmp(key, "ipv6")) copy_value(data->ipv6, sizeof(data->ipv6), value);
    else if (!strcmp(key, "gateway")) copy_value(data->gateway, sizeof(data->gateway), value);
    else if (!strcmp(key, "dns")) copy_value(data->dns, sizeof(data->dns), value);
    else if (!strcmp(key, "mtu")) copy_value(data->mtu, sizeof(data->mtu), value);
    else if (!strcmp(key, "wifi_ssid")) copy_value(data->wifi_ssid, sizeof(data->wifi_ssid), value);
    else if (!strcmp(key, "wifi_key")) copy_value(data->wifi_key, sizeof(data->wifi_key), value);
    else if (!strcmp(key, "wifi_encryption")) copy_value(data->wifi_encryption, sizeof(data->wifi_encryption), value);
    else if (!strcmp(key, "wifi_ipv4")) copy_value(data->wifi_ipv4, sizeof(data->wifi_ipv4), value);
    else if (!strcmp(key, "usb_ipv4")) copy_value(data->usb_ipv4, sizeof(data->usb_ipv4), value);
    else if (!strcmp(key, "wifi_up")) data->wifi_up = parse_bool(value);
    else if (!strcmp(key, "usb_up")) data->usb_up = parse_bool(value);
    else if (!strcmp(key, "wifi_clients")) data->wifi_clients = atoi(value);
    else if (!strcmp(key, "cpu")) data->cpu = atoi(value);
    else if (!strcmp(key, "memory")) data->memory = atoi(value);
    else if (!strcmp(key, "cpu_frequency")) copy_value(data->cpu_frequency, sizeof(data->cpu_frequency), value);
    else if (!strcmp(key, "cpu_temp")) data->cpu_temp = strtod(value, NULL);
    else if (!strcmp(key, "modem_temp")) data->modem_temp = strtod(value, NULL);
    else if (!strcmp(key, "rx_bytes")) data->rx_bytes = strtoull(value, NULL, 10);
    else if (!strcmp(key, "tx_bytes")) data->tx_bytes = strtoull(value, NULL, 10);
    else if (!strcmp(key, "rx_packets")) data->rx_packets = strtoull(value, NULL, 10);
    else if (!strcmp(key, "tx_packets")) data->tx_packets = strtoull(value, NULL, 10);
    else if (!strcmp(key, "rx_errors")) data->rx_errors = strtoull(value, NULL, 10);
    else if (!strcmp(key, "tx_errors")) data->tx_errors = strtoull(value, NULL, 10);
    else if (!strcmp(key, "uptime")) data->uptime = strtoull(value, NULL, 10);
    else if (!strcmp(key, "storage")) data->storage = atoi(value);
    else if (!strcmp(key, "kernel")) copy_value(data->kernel, sizeof(data->kernel), value);
    else if (!strcmp(key, "hardware_model")) copy_value(data->hardware_model, sizeof(data->hardware_model), value);
    else if (!strcmp(key, "cpu_architecture")) copy_value(data->cpu_architecture, sizeof(data->cpu_architecture), value);
    else if (!strcmp(key, "target_platform")) copy_value(data->target_platform, sizeof(data->target_platform), value);
    else if (!strcmp(key, "firmware_version")) copy_value(data->firmware_version, sizeof(data->firmware_version), value);
    else if (!strcmp(key, "soc_model")) copy_value(data->soc_model, sizeof(data->soc_model), value);
    else if (!strcmp(key, "modem_model")) copy_value(data->modem_model, sizeof(data->modem_model), value);
    else if (!strcmp(key, "modem_revision")) copy_value(data->modem_revision, sizeof(data->modem_revision), value);
    else if (!strcmp(key, "imei")) copy_value(data->imei, sizeof(data->imei), value);
    else if (!strcmp(key, "carrier_aggregation")) copy_value(data->carrier_aggregation, sizeof(data->carrier_aggregation), value);
    else if (!strcmp(key, "carrier_aggregation_state")) copy_value(data->carrier_aggregation_state, sizeof(data->carrier_aggregation_state), value);
    else if (!strcmp(key, "carrier_aggregation_capability")) copy_value(data->carrier_aggregation_capability, sizeof(data->carrier_aggregation_capability), value);
    else if (!strcmp(key, "carrier_count")) data->carrier_count = atoi(value);
    else if (!strcmp(key, "carrier_components")) copy_value(data->carrier_components, sizeof(data->carrier_components), value);
    else if (!strcmp(key, "active_channels")) copy_value(data->active_channels, sizeof(data->active_channels), value);
    else if (!strcmp(key, "channel_bandwidths")) copy_value(data->channel_bandwidths, sizeof(data->channel_bandwidths), value);
    else if (!strcmp(key, "arfcn")) copy_value(data->arfcn, sizeof(data->arfcn), value);
    else if (!strcmp(key, "nr_arfcn")) copy_value(data->nr_arfcn, sizeof(data->nr_arfcn), value);
    else if (!strcmp(key, "bonding_enabled")) data->bonding_enabled = parse_bool(value);
    else if (!strcmp(key, "bonding_phase")) copy_value(data->bonding_phase, sizeof(data->bonding_phase), value);
    else if (!strcmp(key, "bonding_paths")) data->bonding_paths = atoi(value);
    else if (!strcmp(key, "bonding_balance_active")) data->bonding_balance_active = parse_bool(value);
    else if (!strcmp(key, "bonding_sim1_connected")) data->bonding_sim1_connected = parse_bool(value);
    else if (!strcmp(key, "bonding_sim1_device")) copy_value(data->bonding_sim1_device, sizeof(data->bonding_sim1_device), value);
    else if (!strcmp(key, "bonding_sim1_address")) copy_value(data->bonding_sim1_address, sizeof(data->bonding_sim1_address), value);
    else if (!strcmp(key, "bonding_sim2_connected")) data->bonding_sim2_connected = parse_bool(value);
    else if (!strcmp(key, "bonding_sim2_device")) copy_value(data->bonding_sim2_device, sizeof(data->bonding_sim2_device), value);
    else if (!strcmp(key, "bonding_sim2_address")) copy_value(data->bonding_sim2_address, sizeof(data->bonding_sim2_address), value);
    else if (!strcmp(key, "ping_latency")) copy_value(data->ping_latency, sizeof(data->ping_latency), value);
    else if (!strcmp(key, "qrtr_services")) data->qrtr_services = atoi(value);
    else if (!strcmp(key, "voice_ready")) data->voice_ready = parse_bool(value);
    else if (!strcmp(key, "sms_ready")) data->sms_ready = parse_bool(value);
    else if (!strcmp(key, "telephony_pages")) data->telephony_pages = parse_bool(value);
    else if (!strcmp(key, "cell_show_imei")) data->cell_show_imei = parse_bool(value);
    else if (!strcmp(key, "cell_show_cell")) data->cell_show_cell = parse_bool(value);
    else if (!strcmp(key, "cell_show_aggregation")) data->cell_show_aggregation = parse_bool(value);
    else if (!strcmp(key, "cell_show_signal")) data->cell_show_signal = parse_bool(value);
    else if (!strcmp(key, "sms_latest_number")) copy_value(data->sms_latest_number, sizeof(data->sms_latest_number), value);
    else if (!strcmp(key, "sms_latest_text")) copy_value(data->sms_latest_text, sizeof(data->sms_latest_text), value);
    else if (!strcmp(key, "sms_latest_direction")) copy_value(data->sms_latest_direction, sizeof(data->sms_latest_direction), value);
    else if (!strcmp(key, "sms_latest_time")) copy_value(data->sms_latest_time, sizeof(data->sms_latest_time), value);
    else if (!strcmp(key, "sms_latest_delivery")) copy_value(data->sms_latest_delivery, sizeof(data->sms_latest_delivery), value);
    else if (!strcmp(key, "sms_draft_number")) copy_value(data->sms_draft_number, sizeof(data->sms_draft_number), value);
    else if (!strcmp(key, "sms_draft_text")) copy_value(data->sms_draft_text, sizeof(data->sms_draft_text), value);
    else if (!strcmp(key, "sms_focus")) copy_value(data->sms_focus, sizeof(data->sms_focus), value);
    else if (!strcmp(key, "sms_result")) copy_value(data->sms_result, sizeof(data->sms_result), value);
    else if (!strcmp(key, "active_call_count")) data->active_call_count = atoi(value);
    else if (!strcmp(key, "active_call_id")) data->active_call_id = atoi(value);
    else if (!strcmp(key, "active_call_state")) copy_value(data->active_call_state, sizeof(data->active_call_state), value);
    else if (!strcmp(key, "active_call_number")) copy_value(data->active_call_number, sizeof(data->active_call_number), value);
    else if (!strcmp(key, "voice_audio_active")) data->voice_audio_active = parse_bool(value);
    else if (!strcmp(key, "voice_audio_route")) copy_value(data->voice_audio_route, sizeof(data->voice_audio_route), value);
    else if (!strcmp(key, "voice_mic_muted")) data->voice_mic_muted = parse_bool(value);
    else if (!strcmp(key, "voice_call_volume")) data->voice_call_volume = atoi(value);
    else if (!strcmp(key, "phone_draft_number")) copy_value(data->phone_draft_number, sizeof(data->phone_draft_number), value);
    else if (!strcmp(key, "phone_result")) copy_value(data->phone_result, sizeof(data->phone_result), value);
    else if (!strcmp(key, "nfc_enabled")) data->nfc_enabled = parse_bool(value);
    else if (!strcmp(key, "nfc_node")) data->nfc_node = parse_bool(value);
    else if (!strcmp(key, "nfc_hal_running")) data->nfc_hal_running = parse_bool(value);
    else if (!strcmp(key, "nfc_service_running")) data->nfc_service_running = parse_bool(value);
    else if (!strcmp(key, "nfc_ndef_available")) data->nfc_ndef_available = parse_bool(value);
    else if (!strcmp(key, "nfc_mode")) copy_value(data->nfc_mode, sizeof(data->nfc_mode), value);
    else if (!strcmp(key, "nfc_stage")) copy_value(data->nfc_stage, sizeof(data->nfc_stage), value);
    else if (!strcmp(key, "nfc_detail")) copy_value(data->nfc_detail, sizeof(data->nfc_detail), value);
    else if (!strcmp(key, "nfc_source_bytes")) data->nfc_source_bytes = atoi(value);
    else if (!strcmp(key, "nfc_saved")) data->nfc_saved = atoi(value);
    else if (!strcmp(key, "nfc_selected_name")) copy_value(data->nfc_selected_name, sizeof(data->nfc_selected_name), value);
    else if (!strcmp(key, "infrared_enabled")) data->infrared_enabled = parse_bool(value);
    else if (!strcmp(key, "infrared_node")) data->infrared_node = parse_bool(value);
    else if (!strcmp(key, "infrared_hal_running")) data->infrared_hal_running = parse_bool(value);
    else if (!strcmp(key, "infrared_saved")) data->infrared_saved = atoi(value);
    else if (!strcmp(key, "infrared_catalog")) data->infrared_catalog = atoi(value);
    else if (!strcmp(key, "infrared_selected_name")) copy_value(data->infrared_selected_name, sizeof(data->infrared_selected_name), value);
    else if (!strcmp(key, "infrared_selected_ac")) data->infrared_selected_ac = parse_bool(value);
    else if (!strcmp(key, "infrared_ac_power")) data->infrared_ac_power = parse_bool(value);
    else if (!strcmp(key, "infrared_ac_temp")) data->infrared_ac_temp = atoi(value);
    else if (!strcmp(key, "infrared_ac_mode")) copy_value(data->infrared_ac_mode, sizeof(data->infrared_ac_mode), value);
    else if (!strcmp(key, "infrared_ac_fan")) copy_value(data->infrared_ac_fan, sizeof(data->infrared_ac_fan), value);
    else if (!strcmp(key, "infrared_ac_swingv")) data->infrared_ac_swingv = !strcmp(value, "auto") || parse_bool(value);
    else if (!strcmp(key, "infrared_ac_swingh")) data->infrared_ac_swingh = !strcmp(value, "auto") || parse_bool(value);
    else if (!strcmp(key, "nfc_last_result")) copy_value(data->nfc_last_result, sizeof(data->nfc_last_result), value);
    else if (!strcmp(key, "infrared_last_result")) copy_value(data->infrared_last_result, sizeof(data->infrared_last_result), value);
    else if (!strcmp(key, "battery_available")) data->battery_available = parse_bool(value);
    else if (!strcmp(key, "battery")) data->battery = atoi(value);
    else if (!strcmp(key, "battery_status")) copy_value(data->battery_status, sizeof(data->battery_status), value);
    else if (!strcmp(key, "battery_charge_type")) copy_value(data->battery_charge_type, sizeof(data->battery_charge_type), value);
    else if (!strcmp(key, "battery_protocol")) copy_value(data->battery_protocol, sizeof(data->battery_protocol), value);
    else if (!strcmp(key, "battery_temp")) data->battery_temp = strtod(value, NULL);
    else if (!strcmp(key, "connector_temp")) data->connector_temp = strtod(value, NULL);
    else if (!strcmp(key, "input_power")) data->input_power = strtod(value, NULL);
    else if (!strcmp(key, "input_online")) data->input_online = atoi(value);
    else if (!strcmp(key, "battery_fastchg_mode")) data->battery_fastchg_mode = atoi(value);
    else if (!strcmp(key, "battery_quick_charge_type")) data->battery_quick_charge_type = atoi(value);
    else if (!strcmp(key, "battery_health")) copy_value(data->battery_health, sizeof(data->battery_health), value);
    else if (!strcmp(key, "battery_technology")) copy_value(data->battery_technology, sizeof(data->battery_technology), value);
    else if (!strcmp(key, "battery_model")) copy_value(data->battery_model, sizeof(data->battery_model), value);
    else if (!strcmp(key, "battery_cell_vendor")) copy_value(data->battery_cell_vendor, sizeof(data->battery_cell_vendor), value);
    else if (!strcmp(key, "battery_pack_vendor")) copy_value(data->battery_pack_vendor, sizeof(data->battery_pack_vendor), value);
    else if (!strcmp(key, "battery_soh")) data->battery_soh = atoi(value);
    else if (!strcmp(key, "battery_cycle_count")) data->battery_cycle_count = atoi(value);
    else if (!strcmp(key, "battery_moisture")) data->battery_moisture = atoi(value);
    else if (!strcmp(key, "battery_anti_burn")) data->battery_anti_burn = atoi(value);
    else if (!strcmp(key, "battery_input_suspend")) data->battery_input_suspend = atoi(value);
    else if (!strcmp(key, "battery_night_charging")) data->battery_night_charging = atoi(value);
    else if (!strcmp(key, "battery_reverse_mode")) data->battery_reverse_mode = atoi(value);
    else if (!strcmp(key, "battery_reverse_state")) data->battery_reverse_state = atoi(value);
    else if (!strcmp(key, "battery_cp_master")) data->battery_cp_master = atoi(value);
    else if (!strcmp(key, "battery_cp_slave")) data->battery_cp_slave = atoi(value);
    else if (!strcmp(key, "battery_voltage")) data->battery_voltage = strtoll(value, NULL, 10);
    else if (!strcmp(key, "battery_current")) data->battery_current = strtoll(value, NULL, 10);
    else if (!strcmp(key, "battery_charge_full")) data->battery_charge_full = strtoll(value, NULL, 10);
    else if (!strcmp(key, "battery_charge_design")) data->battery_charge_design = strtoll(value, NULL, 10);
    else if (!strcmp(key, "input_voltage")) data->input_voltage = strtoll(value, NULL, 10);
    else if (!strcmp(key, "input_current")) data->input_current = strtoll(value, NULL, 10);
    else if (!strcmp(key, "input_current_limit")) data->input_current_limit = strtoll(value, NULL, 10);
    else if (!strcmp(key, "typec_data_role")) copy_value(data->typec_data_role, sizeof(data->typec_data_role), value);
    else if (!strcmp(key, "typec_power_role")) copy_value(data->typec_power_role, sizeof(data->typec_power_role), value);
    else if (!strcmp(key, "typec_power_mode")) copy_value(data->typec_power_mode, sizeof(data->typec_power_mode), value);
    else if (!strcmp(key, "typec_pd_revision")) copy_value(data->typec_pd_revision, sizeof(data->typec_pd_revision), value);
    else if (!strcmp(key, "typec_revision")) copy_value(data->typec_revision, sizeof(data->typec_revision), value);
    else if (!strcmp(key, "speed_state")) copy_value(data->speed_state, sizeof(data->speed_state), value);
    else if (!strcmp(key, "speed_phase")) copy_value(data->speed_phase, sizeof(data->speed_phase), value);
    else if (!strcmp(key, "speed_message")) copy_value(data->speed_message, sizeof(data->speed_message), value);
    else if (!strcmp(key, "speed_download_mbps")) data->speed_download_mbps = strtod(value, NULL);
    else if (!strcmp(key, "speed_upload_mbps")) data->speed_upload_mbps = strtod(value, NULL);
    else if (!strcmp(key, "speed_servers")) data->speed_servers = atoi(value);
    else if (!strcmp(key, "speed_threads")) data->speed_threads = atoi(value);
    else if (!strcmp(key, "speed_duration")) data->speed_duration = atoi(value);
    else if (!strcmp(key, "brightness")) data->brightness = atoi(value);
    else if (!strcmp(key, "screen_timeout")) data->screen_timeout = atoi(value);
    else if (!strcmp(key, "refresh_interval")) data->refresh_interval = atoi(value);
    else if (!strcmp(key, "always_on")) data->always_on = parse_bool(value);
}

static int load_data(struct live_data *data) {
    data_defaults(data);
    FILE *pipe = popen("/usr/libexec/xm14pro-screen-data", "r");
    if (!pipe) return -1;
    char line_buffer[320];
    while (fgets(line_buffer, sizeof(line_buffer), pipe)) {
        char *newline = strpbrk(line_buffer, "\r\n");
        if (newline) *newline = 0;
        char *separator = strchr(line_buffer, '=');
        if (!separator) continue;
        *separator = 0;
        assign_field(data, line_buffer, separator + 1);
    }
    int status = pclose(pipe);
    if (data->cpu < 0) data->cpu = 0;
    if (data->cpu > 100) data->cpu = 100;
    if (data->memory < 0) data->memory = 0;
    if (data->memory > 100) data->memory = 100;
    if (data->refresh_interval < 1 || data->refresh_interval > 30)
        data->refresh_interval = 10;
    if (data->speed_threads < 2 || data->speed_threads > 16)
        data->speed_threads = 8;
    if (data->speed_duration < 3 || data->speed_duration > 30)
        data->speed_duration = 5;
    if (data->voice_call_volume < 0) data->voice_call_volume = 0;
    if (data->voice_call_volume > 100) data->voice_call_volume = 100;
    active_page_count = data->telephony_pages ? PAGE_COUNT : BASE_PAGE_COUNT;
    return status;
}

static void read_runtime_text(const char *path, char *value, size_t size) {
    FILE *file = fopen(path, "re");
    if (!file) return;
    if (fgets(value, size, file)) value[strcspn(value, "\r\n")] = 0;
    fclose(file);
}

static void refresh_telephony_drafts(struct live_data *data) {
    read_runtime_text("/var/run/xm14display/sms-to",
                      data->sms_draft_number, sizeof(data->sms_draft_number));
    read_runtime_text("/var/run/xm14display/sms-text",
                      data->sms_draft_text, sizeof(data->sms_draft_text));
    read_runtime_text("/var/run/xm14display/sms-focus",
                      data->sms_focus, sizeof(data->sms_focus));
    read_runtime_text("/var/run/xm14display/phone-number",
                      data->phone_draft_number, sizeof(data->phone_draft_number));
}

static void load_speed_state_only(struct live_data *data) {
    FILE *file = fopen("/var/run/xm14display/speedtest", "r");
    if (!file) return;
    char line_buffer[320];
    while (fgets(line_buffer, sizeof(line_buffer), file)) {
        char *newline = strpbrk(line_buffer, "\r\n");
        if (newline) *newline = 0;
        char *separator = strchr(line_buffer, '=');
        if (!separator) continue;
        *separator = 0;
        char key[96];
        snprintf(key, sizeof(key), "speed_%s", line_buffer);
        assign_field(data, key, separator + 1);
    }
    fclose(file);
}

static bool safe_interface_name(const char *name) {
    if (!name || !*name || strlen(name) >= 48) return false;
    for (const unsigned char *p = (const unsigned char *)name; *p; ++p) {
        if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
            (*p >= '0' && *p <= '9') || *p == '_' || *p == '-' ||
            *p == '.' || *p == ':') continue;
        return false;
    }
    return true;
}

static bool read_u64_file(const char *path, uint64_t *value) {
    FILE *file = fopen(path, "r");
    if (!file) return false;
    unsigned long long parsed = 0;
    bool valid = fscanf(file, "%llu", &parsed) == 1;
    fclose(file);
    if (valid) *value = (uint64_t)parsed;
    return valid;
}

static double monotonic_seconds(const struct timespec *value) {
    return value->tv_sec + value->tv_nsec / 1000000000.0;
}

static bool update_volume_overlay(const struct timespec *now) {
    FILE *file = fopen(VOLUME_OVERLAY_FILE, "r");
    unsigned long long sequence = 0;
    char context[16] = "media";
    int volume = 0;

    if (file) {
        bool valid = fscanf(file, "%llu %15s %d", &sequence, context,
                            &volume) == 3;
        fclose(file);
        if (valid) {
            if (!volume_overlay.initialized) {
                volume_overlay.initialized = true;
                volume_overlay.sequence = sequence;
            } else if (sequence != volume_overlay.sequence) {
                double changed_at = monotonic_seconds(now);
                volume_overlay.sequence = sequence;
                snprintf(volume_overlay.context,
                         sizeof(volume_overlay.context), "%s", context);
                volume_overlay.volume = volume < 0 ? 0 :
                    (volume > 100 ? 100 : volume);
                if (!volume_overlay.active)
                    volume_overlay.shown_at = changed_at;
                volume_overlay.last_changed_at = changed_at;
                volume_overlay.active = true;
                return true;
            }
        }
    } else {
        volume_overlay.initialized = true;
    }
    if (volume_overlay.active &&
        monotonic_seconds(now) - volume_overlay.last_changed_at >= 1.65) {
        volume_overlay.active = false;
        return true;
    }
    return false;
}

static void blend_round_rect(struct canvas *c, int x, int y, int w, int h,
                             int radius, uint32_t color, uint8_t alpha) {
    if (radius < 1) {
        for (int row = y; row < y + h; ++row)
            for (int col = x; col < x + w; ++col)
                blend_pixel(c, col, row, color, alpha);
        return;
    }
    if (radius * 2 > w) radius = w / 2;
    if (radius * 2 > h) radius = h / 2;
    for (int row = 0; row < h; ++row) {
        int inset = 0;
        if (row < radius) {
            int dy = radius - row - 1;
            inset = radius - isqrt_int(radius * radius - dy * dy);
        } else if (row >= h - radius) {
            int dy = row - (h - radius);
            inset = radius - isqrt_int(radius * radius - dy * dy);
        }
        for (int col = inset; col < w - inset; ++col)
            blend_pixel(c, x + col, y + row, color, alpha);
    }
}

static double smooth_step(double value) {
    if (value < 0.0) value = 0.0;
    if (value > 1.0) value = 1.0;
    return value * value * (3.0 - 2.0 * value);
}

static void draw_volume_overlay(struct canvas *c, double now) {
    double shown_elapsed = now - volume_overlay.shown_at;
    double idle_elapsed = now - volume_overlay.last_changed_at;
    double entrance = shown_elapsed < 0.14 ?
        smooth_step(shown_elapsed / 0.14) : 1.0;
    double visibility = entrance;
    if (idle_elapsed > 1.25)
        visibility *= 1.0 - smooth_step((idle_elapsed - 1.25) / 0.40);
    if (visibility <= 0.0) return;

    int y = 170 + (int)lround((1.0 - entrance) * -34.0);
    int alpha = (int)lround(visibility * 238.0);
    int text_mix = (int)lround(visibility * 100.0);
    uint32_t accent = !strcmp(volume_overlay.context, "call") ?
        C_GREEN : C_CYAN;
    uint32_t label_color = mix(C_CARD_2, C_WHITE, text_mix);
    uint32_t muted_color = mix(C_CARD_2, C_MUTED, text_mix);
    char percentage[24];

    blend_round_rect(c, 160, y, 1120, 250, 62, C_CYAN,
                     (uint8_t)(alpha / 3));
    blend_round_rect(c, 164, y + 4, 1112, 242, 58, rgb(7, 13, 16),
                     (uint8_t)alpha);

    int icon_x = 270;
    int icon_y = y + 125;
    uint32_t icon_color = volume_overlay.volume == 0 ?
        rgb(255, 91, 78) : accent;
    fill_rect(c, icon_x - 45, icon_y - 26, 30, 52, icon_color);
    line(c, icon_x - 15, icon_y - 26, icon_x + 15, icon_y - 53,
         11, icon_color);
    line(c, icon_x - 15, icon_y + 26, icon_x + 15, icon_y + 53,
         11, icon_color);
    line(c, icon_x + 15, icon_y - 53, icon_x + 15, icon_y + 53,
         11, icon_color);
    if (volume_overlay.volume == 0) {
        line(c, icon_x + 40, icon_y - 32, icon_x + 104, icon_y + 32,
             9, icon_color);
        line(c, icon_x + 104, icon_y - 32, icon_x + 40, icon_y + 32,
             9, icon_color);
    } else {
        arc(c, icon_x + 8, icon_y, 58, -0.72, 0.72, 8, icon_color);
        if (volume_overlay.volume >= 55)
            arc(c, icon_x + 8, icon_y, 88, -0.70, 0.70, 8,
                icon_color);
    }

    bold_text(c, 430, y + 48,
              !strcmp(volume_overlay.context, "call") ?
              "通话音量" : "媒体音量", 40, label_color);
    snprintf(percentage, sizeof(percentage), "%d%%", volume_overlay.volume);
    bold_text(c, 1115 - text_width(percentage, 48), y + 44,
              percentage, 48, label_color);
    round_rect(c, 430, y + 144, 685, 18, 9, C_TRACK);
    int filled = 685 * volume_overlay.volume / 100;
    if (filled > 0)
        round_rect(c, 430, y + 144, filled < 18 ? 18 : filled, 18, 9,
                   icon_color);
    text(c, 430, y + 179,
         volume_overlay.volume == 0 ? "静音" :
         (!strcmp(volume_overlay.context, "call") ? "通话" : "媒体"),
         28, volume_overlay.volume == 0 ? icon_color : muted_color);
}

static bool sample_fast_rates(struct fast_rate_sampler *sampler,
                              const char *interface_name,
                              double *rx_rate, double *tx_rate) {
    if (!safe_interface_name(interface_name)) return false;
    char path[160];
    uint64_t rx_bytes = 0, tx_bytes = 0;
    snprintf(path, sizeof(path), "/sys/class/net/%s/statistics/rx_bytes",
             interface_name);
    if (!read_u64_file(path, &rx_bytes)) return false;
    snprintf(path, sizeof(path), "/sys/class/net/%s/statistics/tx_bytes",
             interface_name);
    if (!read_u64_file(path, &tx_bytes)) return false;
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return false;

    if (!sampler->initialized ||
        strcmp(sampler->interface_name, interface_name) != 0 ||
        rx_bytes < sampler->rx_bytes || tx_bytes < sampler->tx_bytes) {
        sampler->initialized = true;
        copy_value(sampler->interface_name, sizeof(sampler->interface_name),
                   interface_name);
        sampler->rx_bytes = rx_bytes;
        sampler->tx_bytes = tx_bytes;
        sampler->sampled_at = now;
        sampler->smooth_rx = 0.0;
        sampler->smooth_tx = 0.0;
        *rx_rate = 0.0;
        *tx_rate = 0.0;
        return true;
    }

    double elapsed = monotonic_seconds(&now) -
                     monotonic_seconds(&sampler->sampled_at);
    if (elapsed <= 0.005) return false;
    double raw_rx = (rx_bytes - sampler->rx_bytes) / elapsed;
    double raw_tx = (tx_bytes - sampler->tx_bytes) / elapsed;
    /* Time-aware damping keeps the needle fluid without hiding short peaks. */
    double alpha = 1.0 - exp(-elapsed / 0.20);
    if (alpha < 0.08) alpha = 0.08;
    if (alpha > 0.55) alpha = 0.55;
    sampler->smooth_rx += (raw_rx - sampler->smooth_rx) * alpha;
    sampler->smooth_tx += (raw_tx - sampler->smooth_tx) * alpha;
    sampler->rx_bytes = rx_bytes;
    sampler->tx_bytes = tx_bytes;
    sampler->sampled_at = now;
    *rx_rate = sampler->smooth_rx;
    *tx_rate = sampler->smooth_tx;
    return true;
}

static int signal_dbm(const struct live_data *data) {
    const char *minus = strchr(data->signal, '-');
    return minus ? atoi(minus) : -120;
}

static int signal_percent(const struct live_data *data) {
    /* Keep the physical display identical to LuCI: use the modem-reported
     * quality first, then apply the same NR/LTE RSRP fallback formula. */
    int result = data->signal_quality;
    if (result < 0 || result > 100)
        result = (int)lround((signal_dbm(data) + 120) * 2.5);
    if (result < 0) result = 0;
    if (result > 100) result = 100;
    return result;
}

static const char *operator_label(const struct live_data *data) {
    const char *name = data->operator_name;
    const char *code = data->operator_code;
    if (!strcmp(code, "46000") || !strcmp(code, "46002") ||
        !strcmp(code, "46004") || !strcmp(code, "46007") ||
        !strcmp(code, "46008")) return "中国移动";
    if (!strcmp(code, "46001") || !strcmp(code, "46006") ||
        !strcmp(code, "46009")) return "中国联通";
    if (!strcmp(code, "46003") || !strcmp(code, "46005") ||
        !strcmp(code, "46011")) return "中国电信";
    if (!strcmp(code, "46015")) return "中国广电";
    if (!strcasecmp(name, "CMCC") || strstr(name, "CHINA MOBILE")) return "中国移动";
    if (!strcasecmp(name, "CUCC") || strstr(name, "UNICOM")) return "中国联通";
    if (!strcasecmp(name, "CTCC") || strstr(name, "TELECOM")) return "中国电信";
    if (!strcasecmp(name, "CBN") || strstr(name, "BROADNET")) return "中国广电";
    return name && *name ? name : "等待网络";
}

static void human_rate(double bytes, char *output, size_t size) {
    if (bytes >= 1048576.0)
        snprintf(output, size, "%.1f", bytes / 1048576.0);
    else if (bytes >= 1024.0)
        snprintf(output, size, "%.0f", bytes / 1024.0);
    else snprintf(output, size, "%.0f", bytes);
}

static const char *rate_unit(double bytes) {
    if (bytes >= 1048576.0) return "MB/s";
    if (bytes >= 1024.0) return "KB/s";
    return "B/s";
}

static void human_bytes(uint64_t bytes, char *output, size_t size) {
    if (bytes >= 1073741824ULL)
        snprintf(output, size, "%.2f GB", (double)bytes / 1073741824.0);
    else if (bytes >= 1048576ULL)
        snprintf(output, size, "%.1f MB", (double)bytes / 1048576.0);
    else if (bytes >= 1024ULL)
        snprintf(output, size, "%.1f KB", (double)bytes / 1024.0);
    else snprintf(output, size, "%llu B", (unsigned long long)bytes);
}

static void push_history(double *history, double value) {
    memmove(history, history + 1, sizeof(*history) * (HISTORY - 1));
    history[HISTORY - 1] = value;
}

static void update_rates(const struct live_data *data, double *rx_rate,
                         double *tx_rate) {
    time_t now = time(NULL);
    *rx_rate = 0.0;
    *tx_rate = 0.0;
    if (previous_sample && now > previous_sample &&
        data->rx_bytes >= previous_rx && data->tx_bytes >= previous_tx) {
        double elapsed = difftime(now, previous_sample);
        *rx_rate = (data->rx_bytes - previous_rx) / elapsed;
        *tx_rate = (data->tx_bytes - previous_tx) / elapsed;
    }
    previous_rx = data->rx_bytes;
    previous_tx = data->tx_bytes;
    previous_sample = now;
    push_history(rx_history, *rx_rate);
    push_history(tx_history, *tx_rate);
}

static void background(struct canvas *c) {
    for (int y = 0; y < c->height; ++y) {
        int green = y < 2000 ? 8 - y * 4 / 2000 : 4;
        fill_rect(c, 0, y, c->width, 1, rgb(0, (uint8_t)green, (uint8_t)(green + 2)));
    }
    uint32_t grid = rgb(4, 24, 27);
    for (int x = 0; x < c->width; x += 96) fill_rect(c, x, 0, 1, c->height, grid);
    for (int y = 0; y < c->height; y += 96) fill_rect(c, 0, y, c->width, 1, grid);
}

static void card(struct canvas *c, int x, int y, int w, int h) {
    framed_round_rect(c, x, y, w, h, 30, 3, C_BORDER, C_CARD_2);
}

static void chevron(struct canvas *c, int x, int y) {
    line(c, x - 10, y - 18, x + 7, y, 5, C_WHITE);
    line(c, x + 7, y, x - 10, y + 18, 5, C_WHITE);
}

static void status_pill(struct canvas *c, int x, int y, bool online) {
    uint32_t color = online ? C_GREEN : rgb(255, 159, 10);
    framed_round_rect(c, x, y, 244, 88, 44, 3, color, mix(C_CARD_2, color, 12));
    circle(c, x + 45, y + 44, 12, color);
    text(c, x + 75, y + 18, online ? "已连接" : "等待连接", 42, color);
}

static void signal_bars(struct canvas *c, int x, int y, int percent) {
    for (int i = 0; i < 4; ++i) {
        int h = 30 + i * 28;
        uint32_t color = percent >= (i + 1) * 20 ? C_CYAN : C_TRACK;
        round_rect(c, x + i * 38, y + 115 - h, 20, h, 10, color);
    }
}

static void graph(struct canvas *c, int x, int y, int w, int h,
                  const double *history, uint32_t color) {
    double maximum = 1.0;
    for (int i = 0; i < HISTORY; ++i) if (history[i] > maximum) maximum = history[i];
    for (int i = 1; i < HISTORY; ++i) {
        int x0 = x + (i - 1) * w / (HISTORY - 1);
        int x1 = x + i * w / (HISTORY - 1);
        int y0 = y + h - (int)(history[i - 1] / maximum * (h - 15));
        int y1 = y + h - (int)(history[i] / maximum * (h - 15));
        line(c, x0, y0, x1, y1, 7, color);
        for (int fill_y = y0; fill_y < y + h; fill_y += 8)
            blend_pixel(c, x0, fill_y, color, (uint8_t)(55 * (y + h - fill_y) / h));
    }
}

static void gauge(struct canvas *c, int cx, int cy, int radius, int percent,
                  const char *value, const char *label, uint32_t color) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    double start = PI * 0.85;
    double end = PI * 2.15;
    arc(c, cx, cy, radius, start, end, 25, C_TRACK);
    arc(c, cx, cy, radius, start, start + (end - start) * percent / 100.0,
        25, color);
    centered_text(c, cx, cy - 38, value, 52, C_WHITE);
    centered_text(c, cx, cy + 30, label, 36, C_MUTED);
}

static void icon_download(struct canvas *c, int x, int y, uint32_t color,
                          bool up) {
    framed_round_rect(c, x, y, 88, 88, 44, 3, color, mix(C_CARD_2, color, 12));
    int direction = up ? -1 : 1;
    line(c, x + 44, y + 22, x + 44, y + 64, 7, color);
    line(c, x + 44, y + (up ? 21 : 66), x + 27, y + (up ? 39 : 49), 7, color);
    line(c, x + 44, y + (up ? 21 : 66), x + 61, y + (up ? 39 : 49), 7, color);
    (void)direction;
}

static void icon_wifi(struct canvas *c, int x, int y, uint32_t color) {
    arc(c, x, y, 58, PI * 1.15, PI * 1.85, 10, color);
    arc(c, x, y, 38, PI * 1.15, PI * 1.85, 10, color);
    circle(c, x, y + 8, 8, color);
}

static void icon_usb(struct canvas *c, int x, int y, uint32_t color) {
    line(c, x, y - 50, x, y + 48, 9, color);
    line(c, x, y - 20, x - 30, y - 3, 8, color);
    line(c, x - 30, y - 3, x - 30, y + 18, 8, color);
    line(c, x, y + 12, x + 31, y - 7, 8, color);
    circle(c, x - 30, y + 25, 9, color);
    framed_round_rect(c, x + 23, y - 25, 18, 18, 3, 3, color, C_CARD_2);
    line(c, x, y - 50, x - 13, y - 30, 7, color);
    line(c, x, y - 50, x + 13, y - 30, 7, color);
}

static void icon_gear(struct canvas *c, int x, int y, uint32_t color) {
    circle(c, x, y, 43, color);
    circle(c, x, y, 30, C_CARD_2);
    circle(c, x, y, 12, color);
}

static void draw_header(struct canvas *c, const struct live_data *data) {
    char clock_text[16];
    char battery_text[16];
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    strftime(clock_text, sizeof(clock_text), "%H:%M", &tm_now);
    bold_text(c, 55, 30, "nwrt", 58, C_GREEN);
    centered_text(c, 430, 42, "随身 Wi-Fi", 38, C_GREEN);
    bold_text(c, 1010, 36, clock_text, 44, C_WHITE);
    if (data->battery_available) {
        int level = data->battery;
        if (level < 0) level = 0;
        if (level > 100) level = 100;
        snprintf(battery_text, sizeof(battery_text), "%d%%", level);
        text(c, 1160, 46, battery_text, 31, C_WHITE);
        framed_round_rect(c, 1260, 43, 108, 50, 12, 4, C_WHITE, C_BLACK);
        int fill = 92 * level / 100;
        if (fill > 0)
            round_rect(c, 1268, 51, fill, 34, 7,
                       level <= 20 ? rgb(255, 69, 58) : C_GREEN);
        round_rect(c, 1371, 57, 13, 22, 4, C_WHITE);
        if (!strcasecmp(data->battery_status, "Charging")) {
            line(c, 1317, 53, 1304, 70, 5, C_WHITE);
            line(c, 1304, 70, 1320, 70, 5, C_WHITE);
            line(c, 1320, 70, 1308, 85, 5, C_WHITE);
        }
    } else {
        text(c, 1205, 46, "--%", 31, C_MUTED);
        framed_round_rect(c, 1285, 43, 83, 50, 12, 4, C_MUTED, C_BLACK);
        round_rect(c, 1371, 57, 13, 22, 4, C_MUTED);
    }
}

static void draw_hero(struct canvas *c, const struct live_data *data) {
    char slot_text[32];
    char signal_text[24];
    char aggregation_text[96];
    int percent = signal_percent(data);
    uint32_t live_color = data->connected ? C_GREEN : rgb(255, 159, 10);
    uint32_t aggregation_color = data->bonding_balance_active ? C_GREEN :
                                 data->bonding_enabled ? C_CYAN : C_MUTED;

    if (!strcmp(data->slot, "1") || !strcmp(data->slot, "2"))
        snprintf(slot_text, sizeof(slot_text), "卡槽 %s", data->slot);
    else
        snprintf(slot_text, sizeof(slot_text), "等待卡槽");
    snprintf(signal_text, sizeof(signal_text), "%d%%", percent);
    if (data->bonding_balance_active)
        snprintf(aggregation_text, sizeof(aggregation_text), "运行中 · %d/2 路",
                 data->bonding_paths);
    else if (data->bonding_enabled)
        snprintf(aggregation_text, sizeof(aggregation_text), "%s",
                 data->bonding_phase);
    else
        snprintf(aggregation_text, sizeof(aggregation_text), "已关闭");

    card(c, 44, 145, 1352, 585);

    /* One continuous card with two dividers: no nested panels and no repeated
     * operator or signal presentation. */
    fill_rect(c, 480, 205, 2, 465, C_BORDER);
    fill_rect(c, 1010, 205, 2, 465, C_BORDER);

    text(c, 100, 205, "当前蜂窝网络", 32, C_MUTED);
    bold_text(c, 100, 270, slot_text, 70,
              data->connected ? C_WHITE : C_MUTED);
    fit_text(c, 100, 375, 335, operator_label(data), 54, C_CYAN);
    fit_text(c, 100, 475, 335, data->technology, 39,
             data->connected ? C_WHITE : C_MUTED);
    fit_text(c, 100, 535, 335, data->band, 31,
             data->connected ? live_color : C_MUTED);
    text(c, 100, 620, "数据接口", 27, C_MUTED);
    fit_text(c, 250, 616, 185, data->interface_name, 31, C_WHITE);

    centered_text(c, 746, 205, "信号质量", 32, C_MUTED);
    gauge(c, 746, 430, 142, percent, signal_text, "蜂窝信号", C_CYAN);
    centered_text(c, 746, 620, data->signal, 32,
                  data->connected ? C_WHITE : C_MUTED);

    text(c, 1060, 205, "连接状态", 30, C_MUTED);
    circle(c, 1072, 285, 11, live_color);
    bold_text(c, 1105, 258, data->connected ? "已连接" : "等待连接",
              42, live_color);
    fill_rect(c, 1060, 340, 280, 2, C_BORDER);
    text(c, 1060, 380, "双卡聚合", 30, C_MUTED);
    fit_text(c, 1060, 430, 280, aggregation_text, 39, aggregation_color);
    fill_rect(c, 1060, 505, 280, 2, C_BORDER);
    text(c, 1060, 545, "网络制式", 30, C_MUTED);
    fit_text(c, 1060, 595, 280, data->technology, 38,
             data->connected ? C_WHITE : C_MUTED);
    chevron(c, 1355, 430);
}

static void draw_rate_card(struct canvas *c, int x, int y, int w,
                           const char *label, double rate,
                           const double *history, uint32_t color, bool up) {
    card(c, x, y, w, 385);
    icon_download(c, x + 65, y + 55, color, up);
    bold_text(c, x + 180, y + 70, label, 44, C_WHITE);
    char value[40];
    human_rate(rate, value, sizeof(value));
    bold_text(c, x + 190, y + 143, value, 86, C_WHITE);
    text(c, x + 410, y + 180, rate_unit(rate), 38, C_MUTED);
    graph(c, x + 55, y + 265, w - 110, 82, history, color);
    chevron(c, x + w - 55, y + 90);
}

static void draw_traffic(struct canvas *c, const struct live_data *data) {
    card(c, 44, 1185, 1352, 390);
    circle(c, 115, 1265, 34, C_CYAN);
    circle(c, 115, 1265, 23, C_CARD_2);
    fill_rect(c, 90, 1239, 50, 7, C_CYAN);
    fill_rect(c, 90, 1258, 50, 7, C_CYAN);
    fill_rect(c, 90, 1277, 50, 7, C_CYAN);
    bold_text(c, 175, 1230, "流量", 49, C_WHITE);
    char rx[48], tx[48];
    human_bytes(data->rx_bytes, rx, sizeof(rx));
    human_bytes(data->tx_bytes, tx, sizeof(tx));
    text(c, 115, 1330, "下载累计", 37, C_MUTED);
    bold_text(c, 115, 1390, rx, 67, C_WHITE);
    fill_rect(c, 470, 1320, 2, 170, C_TRACK);
    text(c, 545, 1330, "上传累计", 37, C_MUTED);
    bold_text(c, 545, 1390, tx, 67, C_WHITE);

    uint64_t total = data->rx_bytes + data->tx_bytes;
    int ratio = total ? (int)(data->rx_bytes * 100ULL / total) : 0;
    arc(c, 1145, 1380, 118, 0, PI * 2.0, 30, C_TRACK);
    arc(c, 1145, 1380, 118, -PI / 2.0,
        -PI / 2.0 + PI * 2.0 * ratio / 100.0, 30, C_CYAN);
    char ratio_text[20];
    snprintf(ratio_text, sizeof(ratio_text), "%d%%", ratio);
    centered_text(c, 1145, 1337, ratio_text, 54, C_WHITE);
    centered_text(c, 1145, 1405, "下载占比", 30, C_MUTED);
    chevron(c, 1340, 1380);
}

static void draw_small_cards(struct canvas *c, const struct live_data *data) {
    int y = 1615;
    card(c, 44, y, 420, 300);
    icon_wifi(c, 115, y + 100, C_CYAN);
    bold_text(c, 190, y + 65, "Wi-Fi", 45, C_WHITE);
    text(c, 190, y + 135, data->wifi_ssid, 35, C_WHITE);
    char clients[48];
    snprintf(clients, sizeof(clients), "%d 台设备", data->wifi_clients);
    text(c, 190, y + 195, clients, 33, data->wifi_up ? C_GREEN : C_MUTED);
    chevron(c, 420, y + 105);

    card(c, 485, y, 420, 300);
    icon_usb(c, 560, y + 110, C_CYAN);
    bold_text(c, 635, y + 65, "USB 网络", 42, C_WHITE);
    text(c, 635, y + 145, data->usb_up ? "活动" : "未连接", 40,
         data->usb_up ? C_GREEN : C_MUTED);
    chevron(c, 860, y + 105);

    card(c, 926, y, 470, 300);
    icon_gear(c, 1005, y + 110, C_CYAN);
    bold_text(c, 1080, y + 65, "管理地址", 42, C_WHITE);
    text(c, 1080, y + 145, data->usb_ipv4, 34, C_MUTED);
    chevron(c, 1350, y + 105);
}

static void draw_device(struct canvas *c, const struct live_data *data) {
    int y = 1955;
    card(c, 44, y, 1352, 480);
    framed_round_rect(c, 100, y + 55, 70, 70, 18, 4, C_CYAN, C_CARD_2);
    for (int i = 0; i < 4; ++i) {
        fill_rect(c, 82, y + 68 + i * 16, 18, 7, C_CYAN);
        fill_rect(c, 170, y + 68 + i * 16, 18, 7, C_CYAN);
    }
    bold_text(c, 210, y + 65, "设备状态", 49, C_WHITE);
    chevron(c, 1340, y + 95);
    char cpu[24], memory[24], temp[32];
    snprintf(cpu, sizeof(cpu), "%d%%", data->cpu);
    snprintf(memory, sizeof(memory), "%d%%", data->memory);
    snprintf(temp, sizeof(temp), "%.0f°C", data->cpu_temp);
    int temperature_percent = (int)data->cpu_temp;
    gauge(c, 310, y + 330, 120, data->cpu, cpu, "CPU", C_CYAN);
    gauge(c, 720, y + 330, 120, data->memory, memory, "内存", C_BLUE);
    gauge(c, 1130, y + 330, 120, temperature_percent, temp, "温度", C_GREEN);
}

static void qr_icon(struct canvas *c, int x, int y, uint32_t color) {
    line(c, x, y + 32, x, y, 8, color); line(c, x, y, x + 32, y, 8, color);
    line(c, x + 82, y, x + 114, y, 8, color); line(c, x + 114, y, x + 114, y + 32, 8, color);
    line(c, x, y + 82, x, y + 114, 8, color); line(c, x, y + 114, x + 32, y + 114, 8, color);
    line(c, x + 82, y + 114, x + 114, y + 114, 8, color); line(c, x + 114, y + 114, x + 114, y + 82, 8, color);
    framed_round_rect(c, x + 41, y + 41, 32, 32, 5, 5, color, C_CARD_2);
}

static void draw_qr_card(struct canvas *c) {
    int y = 2475;
    card(c, 44, y, 1352, 300);
    qr_icon(c, 100, y + 90, C_CYAN);
    bold_text(c, 260, y + 38, "连接二维码", 46, C_WHITE);
    text(c, 260, y + 105, "DEFAULT SSID  nwrt", 34, C_CYAN);
    text(c, 260, y + 158, "DEFAULT KEY   1234567890", 34, C_WHITE);
    text(c, 260, y + 215, "扫码连接 Wi-Fi", 30, C_MUTED);
    chevron(c, 1340, y + 150);
}

static void draw_footer_icon(struct canvas *c, int cx, int y, int index,
                             uint32_t color) {
    if (index == 0) {
        line(c, cx - 30, y + 20, cx, y - 5, 8, color);
        line(c, cx, y - 5, cx + 30, y + 20, 8, color);
        round_rect(c, cx - 23, y + 17, 46, 42, 8, color);
    } else if (index == 1) icon_wifi(c, cx, y + 42, color);
    else if (index == 2) {
        arc(c, cx, y + 42, 35, PI * 0.82, PI * 2.18, 8, color);
        line(c, cx, y + 42, cx + 23, y + 18, 7, color);
        circle(c, cx, y + 42, 7, color);
    } else if (index == 3) {
        fill_rect(c, cx - 28, y + 35, 10, 28, color);
        fill_rect(c, cx - 5, y + 20, 10, 43, color);
        fill_rect(c, cx + 18, y + 2, 10, 61, color);
    } else if (index == 4) {
        framed_round_rect(c, cx - 38, y + 7, 76, 52, 5, 6, color, C_BLACK);
        fill_rect(c, cx - 52, y + 63, 104, 7, color);
    } else if (index == 5) {
        arc(c, cx - 14, y + 30, 30, -PI / 4, PI / 4, 10, color);
        arc(c, cx + 14, y + 30, 30, PI * 3 / 4, PI * 5 / 4, 10, color);
        line(c, cx - 20, y + 50, cx + 20, y + 10, 9, color);
    } else if (index == 6) {
        framed_round_rect(c, cx - 40, y + 11, 72, 48, 10, 6, color, C_BLACK);
        round_rect(c, cx + 34, y + 25, 11, 20, 3, color);
        round_rect(c, cx - 31, y + 20, 50, 30, 5, color);
    } else if (index == 7) {
        round_rect(c, cx - 28, y + 8, 56, 56, 13, color);
        arc(c, cx, y + 36, 43, -PI * 0.72, PI * 0.72, 7, color);
        arc(c, cx, y + 36, 60, -PI * 0.68, PI * 0.68, 6, color);
    } else if (index == 8) {
        circle(c, cx - 20, y + 36, 13, color);
        line(c, cx - 2, y + 36, cx + 42, y + 36, 9, color);
        line(c, cx + 20, y + 21, cx + 42, y + 5, 7, color);
        line(c, cx + 20, y + 51, cx + 42, y + 67, 7, color);
    } else if (index == 9) {
        fill_rect(c, cx - 35, y + 50, 10, 18, color);
        fill_rect(c, cx - 12, y + 36, 10, 32, color);
        fill_rect(c, cx + 11, y + 21, 10, 47, color);
        fill_rect(c, cx + 34, y + 5, 10, 63, color);
    } else if (index == 10) {
        framed_round_rect(c, cx - 43, y + 7, 86, 57, 14, 6, color, C_BLACK);
        line(c, cx - 18, y + 64, cx - 35, y + 78, 6, color);
        line(c, cx - 18, y + 64, cx - 3, y + 64, 6, color);
        circle(c, cx - 21, y + 35, 4, color);
        circle(c, cx, y + 35, 4, color);
        circle(c, cx + 21, y + 35, 4, color);
    } else {
        arc(c, cx, y + 36, 42, PI * 0.18, PI * 0.82, 12, color);
        arc(c, cx, y + 36, 42, PI * 1.18, PI * 1.82, 12, color);
        line(c, cx - 35, y + 55, cx - 47, y + 42, 11, color);
        line(c, cx + 35, y + 55, cx + 47, y + 42, 11, color);
    }
}

static void draw_footer(struct canvas *c, int active) {
    fill_rect(c, 0, FOOTER_TOP, c->width, c->height - FOOTER_TOP,
              rgb(2, 10, 12));
    const char *labels[] = {"概览", "网络", "测速", "流量", "设备", "连接", "电池", "NFC", "红外", "蜂窝", "短信", "电话"};
    for (int index = 0; index < active_page_count; ++index) {
        int cx = 144 + index * FOOTER_ITEM_WIDTH - footer_scroll_px;
        if (cx < -144 || cx > c->width + 144) continue;
        uint32_t color = index == active ? C_CYAN : rgb(174, 185, 205);
        if (index == active) round_rect(c, cx - 100, 2940, 200, 215, 48,
                               mix(C_CARD_2, C_CYAN, 18));
        draw_footer_icon(c, cx, 2970, index, color);
        centered_text(c, cx, 3075, labels[index], 39, color);
    }
}

static void page_title(struct canvas *c, const struct live_data *data,
                       const char *title,
                       const char *subtitle) {
    draw_header(c, data);
    bold_text(c, 55, 125, title, 75, C_WHITE);
    text(c, 55, 220, subtitle, 35, C_MUTED);
}

static void label_value(struct canvas *c, int x, int y, int width,
                        const char *label, const char *value,
                        uint32_t value_color) {
    text(c, x, y, label, 36, C_MUTED);
    int value_width = text_width(value, 38);
    if (value_width > width - 20) {
        text(c, x, y + 53, value, 31, value_color);
    } else {
        text(c, x + width - value_width, y, value, 38, value_color);
    }
}

static void draw_network_page(struct canvas *c, const struct live_data *data) {
    char sim1_link[112];
    char sim2_link[112];
    char bonding_paths[64];

    snprintf(sim1_link, sizeof(sim1_link), "%s%s%s",
             data->bonding_sim1_device,
             data->bonding_sim1_address[0] && strcmp(data->bonding_sim1_address, "--") ? " · " : "",
             data->bonding_sim1_address[0] && strcmp(data->bonding_sim1_address, "--") ? data->bonding_sim1_address : "");
    snprintf(sim2_link, sizeof(sim2_link), "%s%s%s",
             data->bonding_sim2_device,
             data->bonding_sim2_address[0] && strcmp(data->bonding_sim2_address, "--") ? " · " : "",
             data->bonding_sim2_address[0] && strcmp(data->bonding_sim2_address, "--") ? data->bonding_sim2_address : "");
    snprintf(bonding_paths, sizeof(bonding_paths), "%d / 2 条有效路径", data->bonding_paths);

    background(c);
    page_title(c, data, "蜂窝网络", "双卡、无线注册与数据承载");
    card(c, 44, 300, 1352, 510);
    bold_text(c, 90, 345, data->technology, 88,
              data->connected ? C_WHITE : C_MUTED);
    bold_text(c, 90, 450, operator_label(data), 50, C_WHITE);
    status_pill(c, 90, 555, data->connected);
    signal_bars(c, 1120, 385, signal_percent(data));
    bold_text(c, 1080, 555, data->signal, 50, C_WHITE);
    text(c, 1080, 630, data->band, 35, C_CYAN);
    text(c, 90, 700, data->operator_code, 34, C_MUTED);

    card(c, 44, 850, 650, 355);
    bold_text(c, 90, 900, "SIM 卡槽 1", 45, C_WHITE);
    text(c, 90, 985, data->bonding_sim1_connected ? "数据链路已连接" :
         data->sim1_present ? "数据链路待命" : "未插卡", 42,
         data->bonding_sim1_connected ? C_GREEN : data->sim1_present ? C_CYAN : C_MUTED);
    fit_text(c, 90, 1070, 550, data->bonding_sim1_connected ? sim1_link :
             !strcmp(data->slot, "1") ? "当前上网卡" : "备用卡槽", 31, C_MUTED);
    card(c, 746, 850, 650, 355);
    bold_text(c, 792, 900, "SIM 卡槽 2", 45, C_WHITE);
    text(c, 792, 985, data->bonding_sim2_connected ? "数据链路已连接" :
         data->sim2_present ? "数据链路待命" : "未插卡", 42,
         data->bonding_sim2_connected ? C_GREEN : data->sim2_present ? C_CYAN : C_MUTED);
    fit_text(c, 792, 1070, 550, data->bonding_sim2_connected ? sim2_link :
             !strcmp(data->slot, "2") ? "当前上网卡" : "备用卡槽", 31, C_MUTED);

    card(c, 44, 1245, 1352, 650);
    bold_text(c, 90, 1290, "无线与小区", 47, C_WHITE);
    label_value(c, 90, 1380, 570, "RSRP", data->signal, C_WHITE);
    label_value(c, 750, 1380, 560, "RSRQ", data->rsrq, C_WHITE);
    label_value(c, 90, 1475, 570, "SINR", data->sinr, C_WHITE);
    label_value(c, 750, 1475, 560, "PCI", data->pci, C_WHITE);
    label_value(c, 90, 1570, 570, "Cell ID", data->cell_id, C_WHITE);
    label_value(c, 750, 1570, 560, "TAC", data->tac, C_WHITE);
    label_value(c, 90, 1665, 570, "允许制式", data->allowed_mode, C_CYAN);
    label_value(c, 750, 1665, 560, "首选制式", data->preferred_mode, C_CYAN);
    label_value(c, 90, 1760, 570, "飞行模式", data->airplane_mode ? "已开启" : "已关闭",
                data->airplane_mode ? rgb(255, 159, 10) : C_GREEN);
    label_value(c, 750, 1760, 560, "数据漫游", data->roaming ? "允许" : "禁止",
                data->roaming ? rgb(255, 159, 10) : C_GREEN);

    card(c, 44, 1935, 1352, 350);
    bold_text(c, 90, 1980, "双卡宽带聚合", 47, C_WHITE);
    bold_text(c, 90, 2070, data->bonding_phase, 53,
              data->bonding_balance_active ? C_GREEN : data->bonding_enabled ? C_CYAN : C_MUTED);
    text(c, 90, 2160, bonding_paths, 34,
         data->bonding_paths == 2 ? C_GREEN : C_MUTED);
    label_value(c, 750, 2160, 560, "负载均衡",
                data->bonding_balance_active ? "两路已接管" : "未接管", C_CYAN);

    card(c, 44, 2330, 1352, 410);
    bold_text(c, 90, 2375, "当前数据出口", 47, C_WHITE);
    label_value(c, 90, 2470, 1220, "接口", data->interface_name, C_CYAN);
    label_value(c, 90, 2560, 1220, "IPv4", data->ipv4, C_WHITE);
    label_value(c, 90, 2650, 1220, "APN", data->apn, C_WHITE);
    draw_footer(c, 1);
}

static void draw_cellular_details_page(struct canvas *c,
                                       const struct live_data *data) {
    background(c);
    page_title(c, data, "蜂窝详情", "服务小区、载波聚合与设备标识");

    card(c, 44, 300, 1352, 400);
    bold_text(c, 90, 345, operator_label(data), 51, C_WHITE);
    bold_text(c, 90, 430, data->technology, 78,
              data->connected ? C_CYAN : C_MUTED);
    text(c, 90, 555, data->band, 38, C_WHITE);
    if (data->cell_show_signal) {
        signal_bars(c, 1160, 410, signal_percent(data));
        fit_text(c, 870, 545, 450, data->signal, 38, C_GREEN);
    } else {
        text(c, 1045, 545, "信号已隐藏", 31, C_MUTED);
    }

    card(c, 44, 750, 1352, 570);
    bold_text(c, 90, 795, "服务小区", 47, C_WHITE);
    if (data->cell_show_cell) {
        label_value(c, 90, 900, 570, "小区 ID / NCI", data->cell_id, C_CYAN);
        label_value(c, 750, 900, 560, "PCI", data->pci, C_WHITE);
        label_value(c, 90, 1015, 570, "TAC", data->tac, C_WHITE);
        label_value(c, 750, 1015, 560, "NR ARFCN", data->nr_arfcn, C_WHITE);
        label_value(c, 90, 1130, 570, "RSRQ", data->rsrq, C_WHITE);
        label_value(c, 750, 1130, 560, "SINR", data->sinr, C_WHITE);
    } else {
        centered_text(c, 720, 1010, "小区信息已在 LuCI 中隐藏", 38, C_MUTED);
    }

    card(c, 44, 1370, 1352, 640);
    bold_text(c, 90, 1415, "载波聚合", 47, C_WHITE);
    if (data->cell_show_aggregation) {
        const char *capability =
            !strcmp(data->carrier_aggregation_capability, "supported") ?
            "X75 硬件支持 · 5G Sub-6 最多 5CC" : "硬件能力未知";
        bold_text(c, 90, 1515, data->carrier_aggregation, 57,
                  !strcmp(data->carrier_aggregation_state, "active") ? C_GREEN : C_CYAN);
        text(c, 90, 1605, capability, 34, C_MUTED);
        char count[48];
        if (data->carrier_count > 0)
            snprintf(count, sizeof(count), "%d 个当前分量载波", data->carrier_count);
        else
            copy_value(count, sizeof(count), "暂无实时分量");
        label_value(c, 90, 1715, 570, "分量数", count, C_WHITE);
        label_value(c, 750, 1715, 560, "带宽", data->channel_bandwidths, C_WHITE);
        text(c, 90, 1830, "载波明细", 34, C_MUTED);
        fit_text(c, 90, 1890, 1220, data->carrier_components, 34, C_WHITE);
    } else {
        centered_text(c, 720, 1660, "聚合信息已在 LuCI 中隐藏", 38, C_MUTED);
    }

    card(c, 44, 2060, 1352, 680);
    bold_text(c, 90, 2105, "设备标识", 47, C_WHITE);
    if (data->cell_show_imei) {
        text(c, 90, 2220, "IMEI", 34, C_MUTED);
        fit_text(c, 90, 2285, 1220, data->imei, 55, C_CYAN);
    } else {
        text(c, 90, 2285, "IMEI 已隐藏", 43, C_MUTED);
    }
    label_value(c, 90, 2415, 1220, "基带", data->modem_model, C_WHITE);
    text(c, 90, 2530, "基带版本", 34, C_MUTED);
    fit_text(c, 90, 2590, 1220, data->modem_revision, 31, C_WHITE);
    draw_footer(c, 9);
}

static void draw_traffic_page(struct canvas *c, const struct live_data *data,
                              double rx_rate, double tx_rate) {
    background(c);
    page_title(c, data, "实时流量", "当前速率、趋势与累计统计");
    draw_rate_card(c, 44, 310, 650, "下载", rx_rate, rx_history, C_BLUE, false);
    draw_rate_card(c, 746, 310, 650, "上传", tx_rate, tx_history, C_GREEN, true);
    card(c, 44, 735, 1352, 720);
    bold_text(c, 90, 785, "速率趋势", 47, C_WHITE);
    graph(c, 100, 930, 1240, 390, rx_history, C_BLUE);
    graph(c, 100, 930, 1240, 390, tx_history, C_GREEN);
    text(c, 90, 1360, "蓝色 下载", 32, C_BLUE);
    text(c, 330, 1360, "绿色 上传", 32, C_GREEN);

    card(c, 44, 1495, 1352, 530);
    bold_text(c, 90, 1545, "累计流量", 47, C_WHITE);
    char rx[48], tx[48];
    human_bytes(data->rx_bytes, rx, sizeof(rx));
    human_bytes(data->tx_bytes, tx, sizeof(tx));
    label_value(c, 90, 1645, 1220, "下载", rx, C_BLUE);
    label_value(c, 90, 1745, 1220, "上传", tx, C_GREEN);
    char total[48];
    human_bytes(data->rx_bytes + data->tx_bytes, total, sizeof(total));
    label_value(c, 90, 1845, 1220, "合计", total, C_WHITE);

    card(c, 44, 2065, 1352, 675);
    bold_text(c, 90, 2115, "链路统计", 47, C_WHITE);
    char value[64];
    snprintf(value, sizeof(value), "%llu", (unsigned long long)data->rx_packets);
    label_value(c, 90, 2215, 570, "接收包", value, C_WHITE);
    snprintf(value, sizeof(value), "%llu", (unsigned long long)data->tx_packets);
    label_value(c, 750, 2215, 560, "发送包", value, C_WHITE);
    snprintf(value, sizeof(value), "%llu", (unsigned long long)data->rx_errors);
    label_value(c, 90, 2320, 570, "接收错误", value,
                data->rx_errors ? rgb(255, 69, 58) : C_GREEN);
    snprintf(value, sizeof(value), "%llu", (unsigned long long)data->tx_errors);
    label_value(c, 750, 2320, 560, "发送错误", value,
                data->tx_errors ? rgb(255, 69, 58) : C_GREEN);
    label_value(c, 90, 2425, 1220, "接口", data->interface_name, C_CYAN);
    label_value(c, 90, 2530, 1220, "IPv4", data->ipv4, C_WHITE);
    label_value(c, 90, 2635, 1220, "MTU", data->mtu, C_WHITE);
    draw_footer(c, 3);
}

static void draw_device_page(struct canvas *c, const struct live_data *data) {
    background(c);
    page_title(c, data, "设备状态", "系统资源、温度、电池与硬件参数");
    card(c, 44, 310, 1352, 580);
    bold_text(c, 90, 360, "运行状态", 47, C_WHITE);
    char cpu[24], memory[24], temperature[32];
    snprintf(cpu, sizeof(cpu), "%d%%", data->cpu);
    snprintf(memory, sizeof(memory), "%d%%", data->memory);
    snprintf(temperature, sizeof(temperature), "%.0f°C", data->cpu_temp);
    gauge(c, 310, 690, 135, data->cpu, cpu, "CPU", C_CYAN);
    gauge(c, 720, 690, 135, data->memory, memory, "内存", C_BLUE);
    gauge(c, 1130, 690, 135, (int)data->cpu_temp, temperature, "温度", C_GREEN);

    card(c, 44, 930, 650, 450);
    bold_text(c, 90, 980, "温度", 47, C_WHITE);
    char value[64];
    snprintf(value, sizeof(value), "%.1f°C", data->cpu_temp);
    label_value(c, 90, 1080, 520, "CPU", value, C_WHITE);
    snprintf(value, sizeof(value), "%.1f°C", data->modem_temp);
    label_value(c, 90, 1180, 520, "基带", value, C_WHITE);
    label_value(c, 90, 1280, 520, "状态", data->cpu_temp < 75.0 ? "正常" : "温度较高",
                data->cpu_temp < 75.0 ? C_GREEN : rgb(255, 159, 10));

    card(c, 746, 930, 650, 450);
    bold_text(c, 792, 980, "电池", 47, C_WHITE);
    if (data->battery_available) {
        snprintf(value, sizeof(value), "%d%%", data->battery);
        label_value(c, 792, 1055, 520, "电量", value, C_GREEN);
        char state[96], protocol[96], thermals[96];
        snprintf(state, sizeof(state), "%s%s%s", data->battery_status,
                 data->battery_charge_type[0] ? " · " : "",
                 data->battery_charge_type);
        label_value(c, 792, 1135, 520, "状态", state, C_WHITE);
        snprintf(protocol, sizeof(protocol), "%s%s%.2f W",
                 data->battery_protocol[0] ? data->battery_protocol : "--",
                 data->input_power > 0.0 ? " · " : "",
                 data->input_power > 0.0 ? data->input_power : 0.0);
        if (data->input_power <= 0.0)
            snprintf(protocol, sizeof(protocol), "%s",
                     data->battery_protocol[0] ? data->battery_protocol : "--");
        label_value(c, 792, 1215, 520, "协议", protocol, C_CYAN);
        if (data->battery_temp > 0.0 && data->connector_temp > 0.0)
            snprintf(thermals, sizeof(thermals), "%.1f°C · 接口 %.1f°C",
                     data->battery_temp, data->connector_temp);
        else if (data->battery_temp > 0.0)
            snprintf(thermals, sizeof(thermals), "%.1f°C", data->battery_temp);
        else
            snprintf(thermals, sizeof(thermals), "--");
        label_value(c, 792, 1295, 520, "温度", thermals, C_WHITE);
    } else {
        text(c, 792, 1090, "驱动尚未接入", 43, C_MUTED);
        text(c, 792, 1170, "不显示伪造电量", 33, C_MUTED);
    }

    card(c, 44, 1420, 1352, 610);
    bold_text(c, 90, 1470, "硬件参数", 47, C_WHITE);
    label_value(c, 90, 1570, 1220, "型号", data->hardware_model, C_WHITE);
    label_value(c, 90, 1670, 1220, "架构", data->cpu_architecture, C_WHITE);
    label_value(c, 90, 1770, 1220, "目标平台", data->target_platform, C_CYAN);
    label_value(c, 90, 1870, 1220, "固件版本", data->firmware_version, C_WHITE);
    label_value(c, 90, 1970, 1220, "内核版本", data->kernel, C_MUTED);

    card(c, 44, 2070, 1352, 670);
    bold_text(c, 90, 2120, "系统", 47, C_WHITE);
    snprintf(value, sizeof(value), "%llu 小时 %llu 分钟",
             (unsigned long long)(data->uptime / 3600),
             (unsigned long long)((data->uptime % 3600) / 60));
    label_value(c, 90, 2220, 1220, "运行时间", value, C_WHITE);
    snprintf(value, sizeof(value), "%d%%", data->storage);
    label_value(c, 90, 2320, 1220, "存储使用", value, C_WHITE);
    snprintf(value, sizeof(value), "%d", data->qrtr_services);
    label_value(c, 90, 2420, 1220, "基带服务", value, C_CYAN);
    label_value(c, 90, 2520, 1220, "通话服务", data->voice_ready ? "可用" : "未就绪",
                data->voice_ready ? C_GREEN : rgb(255, 159, 10));
    label_value(c, 90, 2620, 1220, "短信服务", data->sms_ready ? "可用" : "未就绪",
                data->sms_ready ? C_GREEN : rgb(255, 159, 10));
    draw_footer(c, 4);
}

static const char *speed_phase_label(const struct live_data *data) {
    if (!strcmp(data->speed_state, "error")) return "节点不可达";
    if (!strcmp(data->speed_state, "done")) return "测速完成";
    if (!strcmp(data->speed_phase, "probe")) return "正在聚合探测";
    if (!strcmp(data->speed_phase, "download")) return "正在多线程下载";
    if (!strcmp(data->speed_phase, "reset")) return "准备上行";
    if (!strcmp(data->speed_phase, "upload")) return "正在多线程上传";
    if (!strcmp(data->speed_phase, "finish")) return "测速完成";
    return "准备就绪";
}

static double speed_value_to_percent(double mbps) {
    static const double stops[] = {0.0, 1.0, 5.0, 10.0, 50.0,
                                   100.0, 500.0, 1000.0};
    const int count = (int)(sizeof(stops) / sizeof(stops[0]));
    if (mbps <= stops[0]) return 0.0;
    if (mbps >= stops[count - 1]) return 1.0;
    for (int i = 0; i < count - 1; ++i) {
        if (mbps <= stops[i + 1]) {
            double within = (mbps - stops[i]) / (stops[i + 1] - stops[i]);
            return (i + within) / (count - 1);
        }
    }
    return 1.0;
}

static uint32_t speed_color_for_percent(double percent) {
    const uint32_t yellow = rgb(255, 204, 20);
    const uint32_t orange = rgb(255, 122, 20);
    const uint32_t red = rgb(255, 69, 58);
    if (percent < 0.0) percent = 0.0;
    if (percent > 1.0) percent = 1.0;
    if (percent < 0.25)
        return mix(C_CYAN, C_GREEN, (int)(percent / 0.25 * 100.0));
    if (percent < 0.58)
        return mix(C_GREEN, yellow, (int)((percent - 0.25) / 0.33 * 100.0));
    if (percent < 0.80)
        return mix(yellow, orange, (int)((percent - 0.58) / 0.22 * 100.0));
    return mix(orange, red, (int)((percent - 0.80) / 0.20 * 100.0));
}

static void draw_scifi_dial(struct canvas *c, int cx, int cy, int radius,
                            double mbps, double percent, uint32_t color) {
    double start = PI * 0.72;
    double sweep = PI * 1.56;
    if (percent < 0.0) percent = 0.0;
    if (percent > 1.0) percent = 1.0;
    arc(c, cx, cy, radius + 42, start, start + sweep, 3, mix(C_BORDER, C_CYAN, 35));
    arc(c, cx, cy, radius + 28, start, start + sweep * percent, 7,
        mix(color, C_WHITE, 30));
    for (int segment = 0; segment < 32; ++segment) {
        double t0 = (double)segment / 32.0;
        double t1 = (double)(segment + 1) / 32.0;
        uint32_t zone = speed_color_for_percent(t0);
        uint32_t segment_color = t0 <= percent ? mix(zone, C_WHITE, 18) : mix(C_TRACK, zone, 24);
        arc(c, cx, cy, radius, start + sweep * t0 + 0.012,
            start + sweep * t1 - 0.012, 38, segment_color);
    }
    arc(c, cx, cy, radius - 54, start, start + sweep, 5, mix(C_BORDER, C_CYAN, 32));
    arc(c, cx, cy, radius - 78, start, start + sweep, 2, C_BORDER);
    for (int i = 0; i <= 40; ++i) {
        double a = start + sweep * i / 40.0;
        int outer = radius + (i % 5 == 0 ? 5 : -4);
        int inner = radius - (i % 5 == 0 ? 35 : 22);
        line(c, cx + (int)(cos(a) * inner), cy + (int)(sin(a) * inner),
             cx + (int)(cos(a) * outer), cy + (int)(sin(a) * outer),
             i % 5 == 0 ? 5 : 3, i / 40.0 <= percent ? color : C_MUTED);
    }
    static const char *labels[] = {"0", "1", "5", "10", "50", "100", "500", "1000"};
    static const double values[] = {0.0, 1.0, 5.0, 10.0, 50.0, 100.0, 500.0, 1000.0};
    const int label_count = (int)(sizeof(labels) / sizeof(labels[0]));
    for (int i = 0; i < label_count; ++i) {
        double a = start + sweep * i / (label_count - 1);
        centered_text(c, cx + (int)(cos(a) * (radius - 115)),
                      cy + (int)(sin(a) * (radius - 115)) - 15,
                      labels[i], 29, values[i] <= mbps ? C_WHITE : C_MUTED);
    }
    double needle = start + sweep * percent;
    line(c, cx, cy, cx + (int)(cos(needle) * (radius - 92)),
         cy + (int)(sin(needle) * (radius - 92)), 16, C_WHITE);
    line(c, cx, cy, cx + (int)(cos(needle) * (radius - 110)),
         cy + (int)(sin(needle) * (radius - 110)), 7, color);
    circle(c, cx, cy, 43, mix(C_CARD_2, color, 28));
    circle(c, cx, cy, 25, C_WHITE);
    circle(c, cx, cy, 12, color);
}

static void csv_item(const char *input, int wanted, char *output, size_t size) {
    output[0] = 0;
    if (!input || !*input) { copy_value(output, size, "--"); return; }
    const char *start = input;
    for (int index = 0;; ++index) {
        while (*start == ' ') ++start;
        const char *end = strchr(start, ',');
        if (!end) end = start + strlen(start);
        if (index == wanted) {
            while (end > start && end[-1] == ' ') --end;
            size_t length = (size_t)(end - start);
            if (length >= size) length = size - 1;
            memcpy(output, start, length);
            output[length] = 0;
            if (!*output) copy_value(output, size, "--");
            return;
        }
        if (!*end) break;
        start = end + 1;
    }
    copy_value(output, size, "--");
}

static void telemetry_cell(struct canvas *c, int x, int y, int width,
                           const char *label, const char *value,
                           uint32_t color) {
    text(c, x, y, label, 22, C_MUTED);
    const char *shown = value && *value ? value : "--";
    int size = 29;
    while (size > 19 && text_width(shown, size) > width) size -= 2;
    bold_text(c, x, y + 29, shown, size, color);
}

static void draw_speed_telemetry(struct canvas *c,
                                 const struct live_data *data) {
    bold_text(c, 78, 2125, "实时链路与硬件", 43, C_WHITE);
    char platform[160];
    snprintf(platform, sizeof(platform), "%s · QRTR %d",
             data->soc_model, data->qrtr_services);
    int platform_width = text_width(platform, 24);
    text(c, 1360 - platform_width, 2138, platform, 24, C_MUTED);
    char cpu[24], memory[24], cpu_temp[32], modem_temp[32];
    char dns_primary[80], dns_secondary[80], interface_apn[128], band_detail[96];
    snprintf(cpu, sizeof(cpu), "%d%%", data->cpu);
    snprintf(memory, sizeof(memory), "%d%%", data->memory);
    snprintf(cpu_temp, sizeof(cpu_temp), "%.1f°C", data->cpu_temp);
    snprintf(modem_temp, sizeof(modem_temp), "%.1f°C", data->modem_temp);
    csv_item(data->dns, 0, dns_primary, sizeof(dns_primary));
    csv_item(data->dns, 1, dns_secondary, sizeof(dns_secondary));
    snprintf(interface_apn, sizeof(interface_apn), "%s / %s",
             data->interface_name, data->apn);
    if (strcmp(data->nr_arfcn, "--") && data->nr_arfcn[0])
        snprintf(band_detail, sizeof(band_detail), "%s / %s", data->band,
                 data->nr_arfcn);
    else if (strcmp(data->arfcn, "--") && data->arfcn[0])
        snprintf(band_detail, sizeof(band_detail), "%s / %s", data->band,
                 data->arfcn);
    else
        copy_value(band_detail, sizeof(band_detail), data->band);

    const int xs[] = {78, 520, 962};
    const int width = 385;
    const int y0 = 2190;
    const int step = 76;
    telemetry_cell(c, xs[0], y0, width, "CPU 频率", data->cpu_frequency, C_CYAN);
    telemetry_cell(c, xs[1], y0, width, "CPU 使用率", cpu, C_WHITE);
    telemetry_cell(c, xs[2], y0, width, "CPU 温度", cpu_temp, C_GREEN);

    telemetry_cell(c, xs[0], y0 + step, width, "内存使用率", memory, C_WHITE);
    telemetry_cell(c, xs[1], y0 + step, width, "基带实例", data->modem_model, C_CYAN);
    telemetry_cell(c, xs[2], y0 + step, width, "基带温度", modem_temp, C_GREEN);

    telemetry_cell(c, xs[0], y0 + step * 2, width, "接入制式", data->technology, C_CYAN);
    telemetry_cell(c, xs[1], y0 + step * 2, width, "频段 / ARFCN", band_detail, C_WHITE);
    telemetry_cell(c, xs[2], y0 + step * 2, width, "载波聚合", data->carrier_aggregation, C_WHITE);

    telemetry_cell(c, xs[0], y0 + step * 3, width, "信号强度", data->signal, C_GREEN);
    telemetry_cell(c, xs[1], y0 + step * 3, width, "RSRQ", data->rsrq, C_WHITE);
    telemetry_cell(c, xs[2], y0 + step * 3, width, "SINR", data->sinr, C_WHITE);

    telemetry_cell(c, xs[0], y0 + step * 4, width, "当前小区", data->cell_id, C_WHITE);
    telemetry_cell(c, xs[1], y0 + step * 4, width, "PCI", data->pci, C_WHITE);
    telemetry_cell(c, xs[2], y0 + step * 4, width, "TAC", data->tac, C_WHITE);

    telemetry_cell(c, xs[0], y0 + step * 5, width, "Ping", data->ping_latency, C_GREEN);
    telemetry_cell(c, xs[1], y0 + step * 5, width, "IPv4", data->ipv4, C_WHITE);
    telemetry_cell(c, xs[2], y0 + step * 5, width, "网关", data->gateway, C_WHITE);

    telemetry_cell(c, xs[0], y0 + step * 6, width, "DNS", dns_primary, C_WHITE);
    telemetry_cell(c, xs[1], y0 + step * 6, width, "备用 DNS", dns_secondary, C_WHITE);
    telemetry_cell(c, xs[2], y0 + step * 6, width, "接口 / APN", interface_apn, C_CYAN);
}

static void draw_speedtest_page(struct canvas *c, const struct live_data *data,
                                double rx_rate, double tx_rate) {
    background(c);
    char subtitle[128];
    snprintf(subtitle, sizeof(subtitle), "每方向 %d 秒 · %d 线程 · 国内 IPv4",
             data->speed_duration, data->speed_threads);
    page_title(c, data, "聚合测速", subtitle);
    bool running_test = !strcmp(data->speed_state, "running");
    double instant_download = data->speed_download_mbps;
    double instant_upload = data->speed_upload_mbps;
    if (running_test && !strcmp(data->speed_phase, "download"))
        instant_download = rx_rate * 8.0 / 1000000.0;
    if (running_test && !strcmp(data->speed_phase, "upload"))
        instant_upload = tx_rate * 8.0 / 1000000.0;

    bool new_test = strcmp(speed_previous_state, "running") != 0 && running_test;
    if (new_test) {
        speed_dial_position = 0.0;
        speed_dial_target_position = 0.0;
        speed_dial_value = 0.0;
        speed_download_peak = 0.0;
        speed_upload_peak = 0.0;
        speed_download_display = 0.0;
        speed_upload_display = 0.0;
        speed_animation_initialized = false;
    }
    copy_value(speed_previous_state, sizeof(speed_previous_state), data->speed_state);

    if (data->speed_download_mbps > speed_download_peak)
        speed_download_peak = data->speed_download_mbps;
    if (data->speed_upload_mbps > speed_upload_peak)
        speed_upload_peak = data->speed_upload_mbps;

    double dial_target = !strcmp(data->speed_phase, "download") ? instant_download :
                         !strcmp(data->speed_phase, "upload") ? instant_upload : 0.0;
    speed_dial_target_position = speed_value_to_percent(dial_target);
    struct timespec animation_now;
    clock_gettime(CLOCK_MONOTONIC, &animation_now);
    double elapsed = 1.0 / 60.0;
    if (speed_animation_initialized) {
        elapsed = monotonic_seconds(&animation_now) -
                  monotonic_seconds(&speed_animation_sample);
        if (elapsed < 0.001) elapsed = 0.001;
        if (elapsed > 0.10) elapsed = 0.10;
    }
    speed_animation_sample = animation_now;
    speed_animation_initialized = true;
    bool returning = speed_dial_target_position < speed_dial_position;
    double time_constant = returning ? 0.24 : 0.16;
    double animation_factor = 1.0 - exp(-elapsed / time_constant);
    double result_factor = 1.0 - exp(-elapsed / 0.55);
    speed_dial_position +=
        (speed_dial_target_position - speed_dial_position) * animation_factor;
    speed_dial_value += (dial_target - speed_dial_value) * animation_factor;
    /* Keep the highest value reached by the already-smoothed real sample.
     * This rejects one-frame interface-rate spikes while preserving the
     * fastest visible result until the next test starts. */
    if (running_test && !strcmp(data->speed_phase, "download") &&
        speed_dial_value > speed_download_peak)
        speed_download_peak = speed_dial_value;
    if (running_test && !strcmp(data->speed_phase, "upload") &&
        speed_dial_value > speed_upload_peak)
        speed_upload_peak = speed_dial_value;
    speed_download_display +=
        (speed_download_peak - speed_download_display) * result_factor;
    speed_upload_display +=
        (speed_upload_peak - speed_upload_display) * result_factor;
    if (fabs(speed_dial_position - speed_dial_target_position) < 0.0005)
        speed_dial_position = speed_dial_target_position;
    if (fabs(speed_dial_value - dial_target) < 0.05)
        speed_dial_value = dial_target;
    if (fabs(speed_download_display - speed_download_peak) < 0.05)
        speed_download_display = speed_download_peak;
    if (fabs(speed_upload_display - speed_upload_peak) < 0.05)
        speed_upload_display = speed_upload_peak;
    double dial_value = speed_dial_value;

    card(c, 44, 300, 1352, 1320);
    for (int x = 110; x < 1340; x += 80)
        line(c, x, 360, x, 1510, 1, rgb(4, 32, 34));
    for (int y = 360; y < 1510; y += 80)
        line(c, 100, y, 1340, y, 1, rgb(4, 32, 34));
    centered_text(c, 720, 365, speed_phase_label(data), 39,
                  !strcmp(data->speed_state, "error") ? rgb(255, 69, 58) : C_CYAN);
    uint32_t dial_color = speed_color_for_percent(speed_dial_position);
    draw_scifi_dial(c, 720, 930, 445, dial_value,
                    speed_dial_position, dial_color);
    char value[48];
    snprintf(value, sizeof(value), "%.1f", dial_value);
    centered_text(c, 720, 745, value, 145, C_WHITE);
    centered_text(c, 720, 900, "Mbps", 47, C_MUTED);
    framed_round_rect(c, 320, 1390, 800, 130, 65, 3,
                      running_test ? C_BORDER : C_CYAN,
                      mix(C_CARD_2, running_test ? C_BORDER : C_CYAN, 18));
    centered_text(c, 720, 1427, running_test ? "测速进行中…" : "开始聚合测速",
                  45, running_test ? C_MUTED : C_CYAN);

    card(c, 44, 1660, 650, 390);
    icon_download(c, 100, 1720, C_BLUE, false);
    bold_text(c, 215, 1735, "下载", 43, C_MUTED);
    snprintf(value, sizeof(value), "%.1f", speed_download_display);
    bold_text(c, 100, 1840, value, 86, C_WHITE);
    text(c, 430, 1880, "Mbps", 37, C_MUTED);
    text(c, 100, 1970, "本轮最高 · 聚合下行", 31, C_BLUE);

    card(c, 746, 1660, 650, 390);
    icon_download(c, 802, 1720, C_GREEN, true);
    bold_text(c, 917, 1735, "上传", 43, C_MUTED);
    snprintf(value, sizeof(value), "%.1f", speed_upload_display);
    bold_text(c, 802, 1840, value, 86, C_WHITE);
    text(c, 1132, 1880, "Mbps", 37, C_MUTED);
    text(c, 802, 1970, "本轮最高 · 聚合上行", 31, C_GREEN);

    card(c, 44, 2090, 1352, 650);
    draw_speed_telemetry(c, data);
    draw_footer(c, 2);
}

static const char *battery_status_label(const char *status) {
    if (!strcasecmp(status, "Charging")) return "充电中";
    if (!strcasecmp(status, "Full")) return "已充满";
    if (!strcasecmp(status, "Discharging")) return "放电中";
    if (!strcasecmp(status, "Not charging")) return "未充电";
    return status && *status ? status : "--";
}

static void draw_battery_page(struct canvas *c, const struct live_data *data) {
    background(c);
    page_title(c, data, "电池与供电", "容量、快充、Type-C 与安全保护");
    card(c, 44, 300, 1352, 570);
    char value[128];
    snprintf(value, sizeof(value), "%d%%", data->battery);
    gauge(c, 300, 640, 175, data->battery, value, "剩余电量", C_GREEN);
    snprintf(value, sizeof(value), "%d%%", data->battery_soh);
    gauge(c, 720, 640, 175, data->battery_soh, value, "健康度", C_CYAN);
    snprintf(value, sizeof(value), "%.1f°C", data->battery_temp);
    gauge(c, 1140, 640, 175, (int)data->battery_temp, value, "电池温度", C_BLUE);
	centered_text(c, 270, 790, battery_status_label(data->battery_status), 39, C_GREEN);
    snprintf(value, sizeof(value), "充电模式 %s", data->input_online ?
             (data->battery_protocol[0] ? data->battery_protocol : "未识别") : "未充电");
	centered_text(c, 720, 790, value, 39, C_CYAN);
    snprintf(value, sizeof(value), "快充 %s", data->input_online ?
             ((data->battery_fastchg_mode > 0 || data->battery_quick_charge_type > 0) ? "已激活" : "待机") : "未充电");
	centered_text(c, 1170, 790, value, 39,
	              (data->battery_fastchg_mode > 0 || data->battery_quick_charge_type > 0) ? C_GREEN : C_WHITE);
	line(c, 495, 780, 495, 838, 2, C_BORDER);
	line(c, 945, 780, 945, 838, 2, C_BORDER);

    card(c, 44, 910, 650, 530);
    bold_text(c, 90, 960, "充电输入", 47, C_WHITE);
    snprintf(value, sizeof(value), "%.2f W", data->input_power);
    label_value(c, 90, 1060, 520, "当前充电功率", data->input_online ? value : "未充电", C_GREEN);
    snprintf(value, sizeof(value), "%.3f V", data->input_voltage / 1000000.0);
    label_value(c, 90, 1150, 520, "输入电压", value, C_WHITE);
    snprintf(value, sizeof(value), "%.3f A", data->input_current / 1000000.0);
    label_value(c, 90, 1240, 520, "输入电流", value, C_WHITE);
    snprintf(value, sizeof(value), "%.2f A", data->input_current_limit / 1000000.0);
    label_value(c, 90, 1330, 520, "电流上限", value, C_MUTED);

    card(c, 746, 910, 650, 530);
    bold_text(c, 792, 960, "Type-C / PD", 47, C_WHITE);
    label_value(c, 792, 1060, 520, "运行模式", data->typec_power_mode, C_CYAN);
    label_value(c, 792, 1150, 520, "数据角色", data->typec_data_role, C_WHITE);
    label_value(c, 792, 1240, 520, "供电角色", data->typec_power_role, C_WHITE);
    snprintf(value, sizeof(value), "PD %s · Type-C %s",
             data->typec_pd_revision, data->typec_revision);
    label_value(c, 792, 1330, 520, "协议版本", value, C_MUTED);

    card(c, 44, 1480, 1352, 580);
    bold_text(c, 90, 1530, "电芯与容量", 47, C_WHITE);
    label_value(c, 90, 1635, 570, "型号", data->battery_model, C_WHITE);
    snprintf(value, sizeof(value), "%s / %s", data->battery_cell_vendor,
             data->battery_pack_vendor);
    label_value(c, 750, 1635, 560, "电芯 / 包厂", value, C_WHITE);
    snprintf(value, sizeof(value), "%d", data->battery_cycle_count);
    label_value(c, 90, 1735, 570, "循环次数", value, C_CYAN);
    label_value(c, 750, 1735, 560, "健康状态", data->battery_health, C_GREEN);
    snprintf(value, sizeof(value), "%.0f / %.0f mAh",
             data->battery_charge_full / 1000.0,
             data->battery_charge_design / 1000.0);
    label_value(c, 90, 1835, 1220, "满充 / 设计容量", value, C_WHITE);
    snprintf(value, sizeof(value), "%.3f V · %.0f mA",
             data->battery_voltage / 1000000.0,
             data->battery_current / 1000.0);
    label_value(c, 90, 1935, 1220, "电池电参数", value, C_WHITE);

    card(c, 44, 2100, 1352, 640);
    bold_text(c, 90, 2150, "保护与功能", 47, C_WHITE);
    label_value(c, 90, 2255, 570, "接口温度", data->connector_temp > 0 ? "实时监测" : "--", C_GREEN);
    snprintf(value, sizeof(value), "%.1f°C", data->connector_temp);
    label_value(c, 750, 2255, 560, "温度值", value, C_WHITE);
    label_value(c, 90, 2355, 570, "进水检测", data->battery_moisture ? "检测到" : "正常",
                data->battery_moisture ? rgb(255, 69, 58) : C_GREEN);
    label_value(c, 750, 2355, 560, "防烧保护", data->battery_anti_burn ? "已启用" : "未启用", C_GREEN);
    label_value(c, 90, 2455, 570, "夜间充电", data->battery_night_charging ? "已启用" : "已关闭", C_WHITE);
    label_value(c, 750, 2455, 560, "输入暂停", data->battery_input_suspend ? "已暂停" : "未暂停", C_WHITE);
    label_value(c, 90, 2555, 570, "反向充电", (data->battery_reverse_mode || data->battery_reverse_state) ? "工作中" : "已关闭", C_WHITE);
    label_value(c, 750, 2555, 560, "充电泵", (data->battery_cp_master && data->battery_cp_slave) ? "主从正常" : "检查中", C_GREEN);
    label_value(c, 90, 2655, 1220, "电池材料", data->battery_technology, C_MUTED);
    draw_footer(c, 6);
}

static void state_badge(struct canvas *c, int x, int y, bool ready,
                        const char *ready_text, const char *down_text) {
    uint32_t color = ready ? C_GREEN : rgb(255, 174, 54);
    round_rect(c, x, y, 250, 76, 38, mix(C_CARD_2, color, 22));
    circle(c, x + 38, y + 38, 10, color);
    text(c, x + 65, y + 19, ready ? ready_text : down_text, 34, color);
}

static void draw_nfc_mark(struct canvas *c, int cx, int cy, uint32_t color) {
    framed_round_rect(c, cx - 76, cy - 76, 152, 152, 36, 8, color,
                      mix(C_CARD_2, color, 8));
    arc(c, cx - 8, cy, 48, -PI * 0.72, PI * 0.72, 9, color);
    arc(c, cx - 8, cy, 82, -PI * 0.68, PI * 0.68, 8, color);
    circle(c, cx - 26, cy, 10, color);
}

static void draw_nfc_page(struct canvas *c, const struct live_data *data) {
    bool ready = data->nfc_enabled && data->nfc_node && data->nfc_hal_running &&
                 data->nfc_service_running;
    bool share = strcmp(data->nfc_mode, "copy") != 0;
    const char *stage = "启动中";
    char source[32];
    if (!strcmp(data->nfc_stage, "share_active")) stage = "正在分享";
    else if (!strcmp(data->nfc_stage, "await_source")) stage = "等待源标签";
    else if (!strcmp(data->nfc_stage, "reading_source")) stage = "正在读取";
    else if (!strcmp(data->nfc_stage, "await_target")) stage = "等待目标标签";
    else if (!strcmp(data->nfc_stage, "writing_target")) stage = "正在写入";
    else if (!strcmp(data->nfc_stage, "copy_complete")) stage = "复制完成";
    else if (!strcmp(data->nfc_stage, "mct_detected")) stage = "识别到 Classic";
    else if (!strcmp(data->nfc_stage, "mct_reading_source")) stage = "读取 Classic";
    else if (!strcmp(data->nfc_stage, "mct_await_target")) stage = "等待目标卡";
    else if (!strcmp(data->nfc_stage, "mct_writing_target")) stage = "写入 Classic";
    else if (!strcmp(data->nfc_stage, "error")) stage = "异常";
    snprintf(source, sizeof(source), "%d B", data->nfc_source_bytes);
    background(c);
    page_title(c, data, "NFC", "热点分享与复制标签");

    card(c, 44, 300, 1352, 520);
    draw_nfc_mark(c, 220, 525, ready ? C_CYAN : C_MUTED);
    text(c, 360, 370, "NEAR FIELD COMMUNICATION", 30, C_CYAN);
    bold_text(c, 360, 430, share ? "热点碰一碰" : "复制标签", 82, C_WHITE);
    text(c, 360, 545, share ? "Android 碰一碰 · iPhone 扫屏幕二维码" : "分享已停止 · 复制模式独占 NFC", 37, C_MUTED);
    state_badge(c, 360, 655, ready, "可用", data->nfc_enabled ? "启动中" : "已关闭");
    round_rect(c, 1050, 650, 260, 84, 42,
               data->nfc_enabled ? mix(C_CARD_2, C_GREEN, 28) : C_TRACK);
    centered_text(c, 1180, 671, data->nfc_enabled ? "关闭 NFC" : "开启 NFC", 34,
                  data->nfc_enabled ? C_GREEN : C_WHITE);

    card(c, 44, 860, 430, 310);
    text(c, 88, 910, "工作模式", 33, C_MUTED);
    bold_text(c, 88, 985, share ? "热点分享" : "复制标签", 55,
              ready ? C_GREEN : C_MUTED);
    text(c, 88, 1080, "两种模式互斥", 30, C_CYAN);
    card(c, 505, 860, 430, 310);
    text(c, 549, 910, "当前阶段", 33, C_MUTED);
    bold_text(c, 549, 985, stage, 48, C_WHITE);
    text(c, 549, 1080, ready ? "协议栈在线" : "服务未就绪", 30, C_CYAN);
    card(c, 966, 860, 430, 310);
    text(c, 1010, 910, "源数据", 33, C_MUTED);
    bold_text(c, 1010, 985, share ? "WSC" : source, 55,
              data->nfc_ndef_available ? C_GREEN : C_MUTED);
    text(c, 1010, 1080, share ? "Android Wi-Fi NDEF" : "标签缓存", 30, C_CYAN);

	card(c, 44, 1210, 1352, 410);
	text(c, 90, 1260, "默认模式", 32, C_MUTED);
	bold_text(c, 90, 1325, "热点分享", 55, share ? C_GREEN : C_WHITE);
	text(c, 90, 1420, "Android 贴近读取；iPhone 打开连接二维码扫码", 32, C_MUTED);
	round_rect(c, 1030, 1365, 300, 90, 45, share ? C_TRACK : mix(C_CARD_2, C_CYAN, 25));
	centered_text(c, 1180, 1390, share ? "当前模式" : "切换到分享", 32, share ? C_MUTED : C_CYAN);

	card(c, 44, 1660, 1352, 500);
	text(c, 90, 1710, "独占模式", 32, C_MUTED);
	bold_text(c, 90, 1775, "复制标签", 55, !share ? C_GREEN : C_WHITE);
	text(c, 90, 1870, "先贴源标签，再贴可写目标标签", 32, C_MUTED);
	text(c, 90, 1935, "标准 NDEF · MIFARE Classic / MCT", 31, C_CYAN);
	round_rect(c, 1030, 1815, 300, 90, 45, !share ? C_TRACK : mix(C_CARD_2, C_CYAN, 25));
	centered_text(c, 1180, 1840, !share ? "当前模式" : "进入复制", 32, !share ? C_MUTED : C_CYAN);
	if (!share) {
		round_rect(c, 1030, 1950, 300, 90, 45, mix(C_CARD_2, C_WHITE, 10));
		centered_text(c, 1180, 1975, "重新读取源卡", 30, C_WHITE);
	}

	card(c, 44, 2200, 1352, 510);
    bold_text(c, 90, 2360, "运行状态", 46, C_WHITE);
    label_value(c, 90, 2470, 1220, "NFC 服务", ready ? "运行中" : "未运行",
                ready ? C_GREEN : C_MUTED);
	label_value(c, 90, 2580, 1220, "当前操作", data->nfc_detail,
                ready ? C_GREEN : C_MUTED);
    draw_footer(c, 7);
}

static void draw_ir_emitter(struct canvas *c, int cx, int cy,
                            uint32_t color) {
    circle(c, cx - 55, cy, 25, color);
    line(c, cx - 20, cy, cx + 65, cy, 16, color);
    arc(c, cx + 40, cy, 70, -PI / 3, PI / 3, 10, color);
    arc(c, cx + 40, cy, 115, -PI / 3, PI / 3, 8, color);
}

static void draw_infrared_page(struct canvas *c, const struct live_data *data) {
    bool ready = data->infrared_enabled && data->infrared_node &&
                 data->infrared_hal_running;
    char catalog[24], saved[24];
    snprintf(catalog, sizeof(catalog), "%d", data->infrared_catalog);
    snprintf(saved, sizeof(saved), "%d", data->infrared_saved);
    background(c);
    page_title(c, data, "万能遥控", "电视、机顶盒与家电");

    card(c, 44, 300, 1352, 500);
    draw_ir_emitter(c, 240, 520, ready ? C_CYAN : C_MUTED);
    text(c, 430, 370, "INFRARED REMOTE", 30, C_CYAN);
    bold_text(c, 430, 430, "万能遥控器", 74, C_WHITE);
    text(c, 430, 545, "本地码库  ·  不依赖互联网", 36, C_MUTED);
    state_badge(c, 430, 650, ready, "发射就绪", "已停止");

    card(c, 44, 840, 430, 300);
    text(c, 88, 890, "遥控型号", 33, C_MUTED);
    bold_text(c, 88, 965, catalog, 60, C_WHITE);
    text(c, 88, 1055, "离线码库", 30, C_CYAN);
    card(c, 505, 840, 430, 300);
    text(c, 549, 890, "我的遥控器", 33, C_MUTED);
    bold_text(c, 549, 965, saved, 60, C_WHITE);
    text(c, 549, 1055, "已保存设备", 30, C_CYAN);
    card(c, 966, 840, 430, 300);
    text(c, 1010, 890, "载波范围", 33, C_MUTED);
    bold_text(c, 1010, 965, "30–56", 55, C_WHITE);
    text(c, 1010, 1055, "kHz 自动匹配", 30, C_CYAN);

    card(c, 44, 1180, 1352, 1030);
    text(c, 90, 1230, "当前遥控器", 33, C_MUTED);
    bold_text(c, 90, 1295, data->infrared_selected_name, 58,
              data->infrared_saved > 0 ? C_WHITE : C_MUTED);
    round_rect(c, 1110, 1240, 210, 78, 39,
               ready ? mix(C_CARD_2, C_GREEN, 24) : C_TRACK);
    centered_text(c, 1215, 1260, ready ? "已连接" : "未连接", 31,
                  ready ? C_GREEN : C_MUTED);

    const char *standard_keys[] = {"电源", "向上", "菜单", "向左", "OK", "向右", "音量 -", "向下", "音量 +"};
    char temperature[24], mode[32], fan[32], swing_v[32], swing_h[32];
    snprintf(temperature, sizeof(temperature), "%d°C", data->infrared_ac_temp);
    snprintf(mode, sizeof(mode), "模式 · %s",
             !strcmp(data->infrared_ac_mode, "cool") ? "制冷" :
             !strcmp(data->infrared_ac_mode, "heat") ? "制热" :
             !strcmp(data->infrared_ac_mode, "dry") ? "除湿" :
             !strcmp(data->infrared_ac_mode, "fan") ? "送风" : "自动");
    snprintf(fan, sizeof(fan), "风速 · %s",
             !strcmp(data->infrared_ac_fan, "low") ? "低" :
             !strcmp(data->infrared_ac_fan, "medium") ? "中" :
             !strcmp(data->infrared_ac_fan, "high") ? "高" :
             !strcmp(data->infrared_ac_fan, "max") ? "强" : "自动");
    snprintf(swing_v, sizeof(swing_v), "上下扫风 · %s", data->infrared_ac_swingv ? "开" : "关");
    snprintf(swing_h, sizeof(swing_h), "左右扫风 · %s", data->infrared_ac_swingh ? "开" : "关");
    const char *ac_keys[] = {data->infrared_ac_power ? "关闭空调" : "开启空调", mode, fan,
                             "温度 -", temperature, "温度 +", swing_v, swing_h, "更多设置"};
    const char **keys = data->infrared_selected_ac ? ac_keys : standard_keys;
    for (int i = 0; i < 9; ++i) {
        int col = i % 3, row = i / 3;
        int x = 90 + col * 415, y = 1430 + row * 220;
        uint32_t accent = i == 0 ? rgb(255, 93, 84) :
                          i == 4 ? C_CYAN : C_WHITE;
        framed_round_rect(c, x, y, 365, 165, 38, 3,
                          mix(C_BORDER, accent, 35), C_CARD_2);
        centered_text(c, x + 182, y + 55, keys[i], 38,
                      data->infrared_saved > 0 ? accent : C_MUTED);
    }

    card(c, 44, 2250, 1352, 460);
    bold_text(c, 90, 2300, "设备库", 46, C_WHITE);
    label_value(c, 90, 2410, 1220, "支持类型",
                data->infrared_selected_ac ? "空调完整状态 · 模式 · 温度 · 风速 · 扫风" :
                "电视 · 机顶盒 · 音响 · 投影 · 家电", C_CYAN);
    label_value(c, 90, 2520, 1220, "最近发射", data->infrared_last_result,
                ready ? C_GREEN : C_MUTED);
    text(c, 90, 2630, "在管理页面添加、重命名并选择遥控器", 31, C_MUTED);
    draw_footer(c, 8);
}

static void wifi_escape(const char *input, char *output, size_t size) {
    size_t used = 0;
    for (const char *p = input; p && *p && used + 2 < size; ++p) {
        if (*p == '\\' || *p == ';' || *p == ',' || *p == ':') output[used++] = '\\';
        output[used++] = *p;
    }
    output[used] = 0;
}

static void draw_wifi_qr(struct canvas *c, int x, int y, int size,
                         const struct live_data *data) {
    char ssid[196], key[260], payload[560];
    wifi_escape(data->wifi_ssid, ssid, sizeof(ssid));
    wifi_escape(data->wifi_key, key, sizeof(key));
    const char *type = strstr(data->wifi_encryption, "sae") ? "WPA" :
                       strstr(data->wifi_encryption, "psk") ? "WPA" : "nopass";
    snprintf(payload, sizeof(payload), "WIFI:T:%s;S:%s;P:%s;;", type, ssid, key);
    QRcode *qr = QRcode_encodeString8bit(payload, 0, QR_ECLEVEL_M);
    round_rect(c, x, y, size, size, 36, C_WHITE);
    if (!qr) {
        centered_text(c, x + size / 2, y + size / 2 - 30, "二维码生成失败",
                      38, rgb(255, 69, 58));
        return;
    }
    int quiet = 4;
    int modules = qr->width + quiet * 2;
    int scale = (size - 70) / modules;
    if (scale < 1) scale = 1;
    int actual = modules * scale;
    int origin_x = x + (size - actual) / 2 + quiet * scale;
    int origin_y = y + (size - actual) / 2 + quiet * scale;
    for (int row = 0; row < qr->width; ++row) {
        for (int col = 0; col < qr->width; ++col) {
            if (qr->data[row * qr->width + col] & 1)
                fill_rect(c, origin_x + col * scale, origin_y + row * scale,
                          scale, scale, C_BLACK);
        }
    }
    QRcode_free(qr);
}

static void draw_connect_page(struct canvas *c, const struct live_data *data) {
    background(c);
    page_title(c, data, "快速连接", "扫描二维码加入随身 Wi-Fi");
    card(c, 44, 310, 1352, 1640);
    draw_wifi_qr(c, 210, 435, 1020, data);
    centered_text(c, 720, 1520, data->wifi_ssid, 60, C_WHITE);
    centered_text(c, 720, 1605, data->wifi_up ? "热点运行中" : "热点已停止",
                  38, data->wifi_up ? C_GREEN : C_MUTED);
    centered_text(c, 720, 1680, "使用手机相机扫码连接", 35, C_MUTED);

    card(c, 44, 1990, 1352, 750);
    bold_text(c, 90, 2040, "管理地址", 47, C_WHITE);
    label_value(c, 90, 2150, 1220, "USB", data->usb_ipv4, C_CYAN);
    label_value(c, 90, 2260, 1220, "Wi-Fi", data->wifi_ipv4, C_CYAN);
    label_value(c, 90, 2370, 1220, "热点", data->wifi_ssid, C_WHITE);
    char clients[48];
    snprintf(clients, sizeof(clients), "%d 台", data->wifi_clients);
    label_value(c, 90, 2480, 1220, "已连接设备", clients, C_WHITE);
    label_value(c, 90, 2590, 1220, "USB 网络", data->usb_up ? "活动" : "未连接",
                data->usb_up ? C_GREEN : C_MUTED);
    draw_footer(c, 5);
}

static void fit_text(struct canvas *c, int x, int y, int width,
                     const char *value, int preferred, uint32_t color) {
    int size = preferred;
    while (size > 20 && text_width(value, size) > width) size -= 2;
    text(c, x, y, value && *value ? value : "--", size, color);
}

static void touch_key(struct canvas *c, int x, int y, int w, int h,
                      const char *label, uint32_t color, bool active) {
    uint32_t frame = active ? color : mix(C_BORDER, color, 28);
    uint32_t fill = active ? mix(C_CARD_2, color, 18) : C_CARD_2;
    framed_round_rect(c, x, y, w, h, 30, 3, frame, fill);
    centered_text(c, x + w / 2, y + (h - 48) / 2, label, 42, color);
}

static const char *delivery_label(const char *delivery) {
    if (!strcmp(delivery, "delivered")) return "已送达";
    if (!strcmp(delivery, "failed")) return "送达失败";
    if (!strcmp(delivery, "unknown")) return "已收到回执";
    return "等待回执";
}

static void draw_sms_keyboard(struct canvas *c, const struct live_data *data) {
    const char *rows[] = {"1234567890", "qwertyuiop", "asdfghjkl", "zxcvbnm"};
    const int counts[] = {10, 10, 9, 7};
    const int ys[] = {1400, 1580, 1760, 1940};
    for (int row = 0; row < 4; ++row) {
        int count = counts[row], gap = 10;
        int width = row < 2 ? 125 : (row == 2 ? 132 : 150);
        int total = count * width + (count - 1) * gap;
        int start = (1440 - total) / 2;
        for (int col = 0; col < count; ++col) {
            char label[2] = {rows[row][col], 0};
            touch_key(c, start + col * (width + gap), ys[row], width, 140,
                      label, C_WHITE, false);
        }
    }
    touch_key(c, 60, 2140, 300, 150, "DEL", C_WHITE, false);
    touch_key(c, 390, 2140, 460, 150, "空格", C_WHITE, false);
    touch_key(c, 880, 2140, 500, 150, "发送", C_GREEN,
              data->sms_ready);
}

static void draw_sms_page(struct canvas *c, const struct live_data *data) {
    bool number_focus = strcmp(data->sms_focus, "message") != 0;
    const char *latest_kind = !strcmp(data->sms_latest_direction, "out") ?
                              "我发送" : "收到短信";
    background(c);
    page_title(c, data, "短信", "收发、双卡选择与送达回执");

    card(c, 44, 300, 1352, 200);
    text(c, 88, 340, "收件方", 31, C_MUTED);
    fit_text(c, 88, 400, 1190, data->sms_draft_number, 52,
             number_focus ? C_CYAN : C_WHITE);
    framed_round_rect(c, 1270, 340, 80, 110, 24, 3,
                      number_focus ? C_CYAN : C_BORDER, C_CARD_2);
    centered_text(c, 1310, 370, number_focus ? "1" : "A", 42,
                  number_focus ? C_CYAN : C_MUTED);

    card(c, 44, 530, 1352, 340);
    text(c, 88, 570, "短信内容", 31, C_MUTED);
    fit_text(c, 88, 640, 1240, data->sms_draft_text, 42,
             number_focus ? C_WHITE : C_CYAN);
    text(c, 88, 790, number_focus ? "点击短信内容输入" :
         "当前键盘输入短信内容", 29, C_MUTED);

    card(c, 44, 900, 1352, 440);
    text(c, 88, 945, latest_kind, 31, C_CYAN);
    fit_text(c, 88, 1005, 820, data->sms_latest_number, 43, C_WHITE);
    text(c, 980, 1015, data->sms_latest_time, 27, C_MUTED);
    fit_text(c, 88, 1100, 1240, data->sms_latest_text, 39, C_WHITE);
    if (!strcmp(data->sms_latest_direction, "out"))
        text(c, 88, 1240, delivery_label(data->sms_latest_delivery), 30,
             !strcmp(data->sms_latest_delivery, "delivered") ? C_GREEN :
             !strcmp(data->sms_latest_delivery, "failed") ? rgb(255, 69, 58) : C_MUTED);

    draw_sms_keyboard(c, data);
    card(c, 44, 2330, 1352, 410);
    bold_text(c, 88, 2380, data->sms_ready ? "短信服务已就绪" : "短信服务未就绪",
              43, data->sms_ready ? C_GREEN : rgb(255, 159, 10));
    fit_text(c, 88, 2470, 1240, data->sms_result, 31, C_MUTED);
    text(c, 88, 2630, "支持 GSM-7 / Unicode 与移动网络送达回执", 29, C_MUTED);
    draw_footer(c, 10);
}

static const char *call_state_label(const char *state) {
    if (!strcmp(state, "incoming") || !strcmp(state, "waiting")) return "收到电话";
    if (!strcmp(state, "originating")) return "正在打电话";
    if (!strcmp(state, "alerting")) return "等待接通";
    if (!strcmp(state, "conversation")) return "通话中";
    if (!strcmp(state, "hold")) return "已保持";
    return "可打电话";
}

static void draw_phone_page(struct canvas *c, const struct live_data *data) {
    const char *keys[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "*", "0", "#"};
    bool active = data->active_call_count > 0;
    const char *number = active && data->active_call_number[0] ?
                         data->active_call_number : data->phone_draft_number;
    background(c);
    page_title(c, data, "电话", "数字键盘、接通与完整通话音频");

    card(c, 44, 300, 1352, 280);
    text(c, 88, 345, call_state_label(data->active_call_state), 34,
         active ? C_GREEN : C_MUTED);
    fit_text(c, 88, 420, 1240, number, 66, C_WHITE);
    text(c, 1040, 350, data->voice_ready ? "IMS 已就绪" : "IMS 未就绪",
         28, data->voice_ready ? C_GREEN : rgb(255, 159, 10));

    for (int i = 0; i < 12; ++i) {
        int col = i % 3, row = i / 3;
        int x = 120 + col * 410, y = 640 + row * 245;
        touch_key(c, x, y, 340, 190, keys[i], C_WHITE, false);
    }

    if (active) {
        bool incoming = !strcmp(data->active_call_state, "incoming") ||
                        !strcmp(data->active_call_state, "waiting");
        if (incoming) touch_key(c, 90, 1665, 580, 165, "接通", C_GREEN, true);
        touch_key(c, incoming ? 770 : 300, 1665, incoming ? 580 : 840, 165,
                  "挂断", rgb(255, 69, 58), true);
    } else {
        touch_key(c, 90, 1665, 930, 165, "打电话", C_GREEN,
                  data->voice_ready);
        touch_key(c, 1050, 1665, 300, 165, "DEL", C_WHITE, false);
    }

    card(c, 44, 1880, 1352, 690);
    bold_text(c, 88, 1930, "通话音频", 44, C_WHITE);
    text(c, 1000, 1940, data->voice_audio_active ? "PAL 音频活动" : "PAL 音频待机",
         29, data->voice_audio_active ? C_GREEN : C_MUTED);
    touch_key(c, 90, 2040, 390, 150, "接收器", C_CYAN,
              !strcmp(data->voice_audio_route, "earpiece"));
    touch_key(c, 525, 2040, 390, 150, "外放", C_CYAN,
              !strcmp(data->voice_audio_route, "speaker"));
    touch_key(c, 960, 2040, 390, 150,
              data->voice_mic_muted ? "打开输入" : "静音输入",
              data->voice_mic_muted ? rgb(255, 69, 58) : C_WHITE,
              data->voice_mic_muted);
    char volume[48];
    snprintf(volume, sizeof(volume), "通话音量  %d%%", data->voice_call_volume);
    bold_text(c, 300, 2285, volume, 40, C_WHITE);
    touch_key(c, 90, 2250, 160, 140, "-", C_WHITE, false);
    touch_key(c, 1190, 2250, 160, 140, "+", C_WHITE, false);
    round_rect(c, 300, 2415, 840, 22, 11, C_TRACK);
    round_rect(c, 300, 2415, 840 * data->voice_call_volume / 100, 22, 11, C_CYAN);
    fit_text(c, 88, 2490, 1240, data->phone_result, 27, C_MUTED);
    draw_footer(c, 11);
}

static int current_page(void) {
    FILE *file = fopen("/var/run/xm14display/page", "r");
    int page = 0;
    if (file) { if (fscanf(file, "%d", &page) != 1) page = 0; fclose(file); }
    if (page < 0 || page >= active_page_count) page = 0;
    return page;
}

static int current_footer_start(void) {
    FILE *file = fopen("/var/run/xm14display/footer-start", "r");
    int start = 0;
    if (file) {
        if (fscanf(file, "%d", &start) != 1) start = 0;
        fclose(file);
    }
    if (start < 0) start = 0;
    if (start > active_page_count - FOOTER_VISIBLE_ITEMS)
        start = active_page_count - FOOTER_VISIBLE_ITEMS;
    return start;
}

static bool footer_is_dragging(void) {
    return access("/var/run/xm14display/footer-dragging", F_OK) == 0;
}

static int current_footer_position(int start, bool dragging) {
    int position = start * FOOTER_ITEM_WIDTH;
    if (dragging) {
        FILE *file = fopen("/var/run/xm14display/footer-position", "r");
        if (file) {
            if (fscanf(file, "%d", &position) != 1)
                position = start * FOOTER_ITEM_WIDTH;
            fclose(file);
        }
    }
    int maximum = (active_page_count - FOOTER_VISIBLE_ITEMS) * FOOTER_ITEM_WIDTH;
    if (position < 0) position = 0;
    if (position > maximum) position = maximum;
    return position;
}

static void render(struct canvas *c, const struct live_data *data,
                   double rx_rate, double tx_rate, int page) {
    if (page == 1) { draw_network_page(c, data); return; }
    if (page == 2) { draw_speedtest_page(c, data, rx_rate, tx_rate); return; }
    if (page == 3) { draw_traffic_page(c, data, rx_rate, tx_rate); return; }
    if (page == 4) { draw_device_page(c, data); return; }
    if (page == 5) { draw_connect_page(c, data); return; }
    if (page == 6) { draw_battery_page(c, data); return; }
    if (page == 7) { draw_nfc_page(c, data); return; }
    if (page == 8) { draw_infrared_page(c, data); return; }
    if (page == 9) { draw_cellular_details_page(c, data); return; }
    if (page == 10 && data->telephony_pages) { draw_sms_page(c, data); return; }
    if (page == 11 && data->telephony_pages) { draw_phone_page(c, data); return; }
    background(c);
    draw_header(c, data);
    draw_hero(c, data);
    draw_rate_card(c, 44, 760, 650, "下载", rx_rate, rx_history, C_BLUE, false);
    draw_rate_card(c, 746, 760, 650, "上传", tx_rate, tx_history, C_GREEN, true);
    draw_traffic(c, data);
    draw_small_cards(c, data);
    draw_device(c, data);
    draw_qr_card(c);
    draw_footer(c, 0);
}

static int read_integer_file(const char *path) {
    FILE *file = fopen(path, "r");
    int value = 0;
    if (file) { if (fscanf(file, "%d", &value) != 1) value = 0; fclose(file); }
    return value;
}

static int64_t read_i64_file(const char *path) {
    FILE *file = fopen(path, "r");
    long long value = 0;
    if (file) {
        if (fscanf(file, "%lld", &value) != 1) value = 0;
        fclose(file);
    }
    return (int64_t)value;
}

static void refresh_fast_hardware(struct live_data *data) {
    uint64_t minimum = 0, maximum = 0;
    for (int cpu = 0; cpu < 16; ++cpu) {
        char path[128];
        snprintf(path, sizeof(path),
                 "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_cur_freq", cpu);
        uint64_t frequency = 0;
        if (!read_u64_file(path, &frequency)) continue;
        if (!minimum || frequency < minimum) minimum = frequency;
        if (frequency > maximum) maximum = frequency;
    }
    if (minimum && maximum)
        snprintf(data->cpu_frequency, sizeof(data->cpu_frequency),
                 "%.2f-%.2f GHz", minimum / 1000000.0, maximum / 1000000.0);

    FILE *stat = fopen("/proc/stat", "r");
    if (stat) {
        unsigned long long user = 0, nice = 0, system = 0, idle = 0;
        unsigned long long iowait = 0, irq = 0, softirq = 0, steal = 0;
        if (fscanf(stat, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
                   &user, &nice, &system, &idle, &iowait, &irq, &softirq,
                   &steal) == 8) {
            uint64_t current_idle = idle + iowait;
            uint64_t current_total = user + nice + system + idle + iowait +
                                     irq + softirq + steal;
            if (fast_cpu_previous_total && current_total > fast_cpu_previous_total) {
                uint64_t total_delta = current_total - fast_cpu_previous_total;
                uint64_t idle_delta = current_idle - fast_cpu_previous_idle;
                data->cpu = (int)((total_delta - idle_delta) * 100 / total_delta);
            }
            fast_cpu_previous_total = current_total;
            fast_cpu_previous_idle = current_idle;
        }
        fclose(stat);
    }

    FILE *memory = fopen("/proc/meminfo", "r");
    if (memory) {
        unsigned long long total = 0, available = 0;
        char line_buffer[160];
        while (fgets(line_buffer, sizeof(line_buffer), memory)) {
            if (sscanf(line_buffer, "MemTotal: %llu kB", &total) == 1) continue;
            if (sscanf(line_buffer, "MemAvailable: %llu kB", &available) == 1) continue;
        }
        fclose(memory);
        if (total && available <= total)
            data->memory = (int)((total - available) * 100 / total);
    }

    int temperature = read_integer_file("/sys/class/thermal/thermal_zone57/temp");
    if (temperature > 0) data->cpu_temp = temperature / 1000.0;
    int modem_maximum = 0;
    for (int zone = 40; zone <= 43; ++zone) {
        char path[96];
        snprintf(path, sizeof(path), "/sys/class/thermal/thermal_zone%d/temp", zone);
        temperature = read_integer_file(path);
        if (temperature > modem_maximum) modem_maximum = temperature;
    }
    if (modem_maximum > 0) data->modem_temp = modem_maximum / 1000.0;

    FILE *radio = fopen("/var/run/xm14pro/radio-state", "r");
    if (radio) {
        char line_buffer[256];
        while (fgets(line_buffer, sizeof(line_buffer), radio)) {
            char *newline = strpbrk(line_buffer, "\r\n");
            if (newline) *newline = 0;
            char *separator = strchr(line_buffer, '=');
            if (!separator) continue;
            *separator = 0;
            const char *key = line_buffer;
            const char *value = separator + 1;
            if (!strcmp(key, "nr_rsrp")) assign_field(data, "signal", value);
            else if (!strcmp(key, "nr_rsrq")) assign_field(data, "rsrq", value);
            else if (!strcmp(key, "nr_sinr")) assign_field(data, "sinr", value);
            else if (!strcmp(key, "cell_id")) assign_field(data, "cell_id", value);
            else if (!strcmp(key, "tac")) assign_field(data, "tac", value);
            else if (!strcmp(key, "pci")) assign_field(data, "pci", value);
            else if (!strcmp(key, "arfcn")) assign_field(data, "arfcn", value);
            else if (!strcmp(key, "nr_arfcn")) assign_field(data, "nr_arfcn", value);
            else if (!strcmp(key, "operator_name")) assign_field(data, "operator", value);
            else if (!strcmp(key, "operator_code")) assign_field(data, "operator_code", value);
            else if (!strcmp(key, "access_technologies")) assign_field(data, "technology", value);
            else if (!strcmp(key, "active_bands")) assign_field(data, "band", value);
        }
        fclose(radio);
    }

    FILE *uim = fopen("/var/run/xm14pro/uim-state", "r");
    if (uim) {
        char line_buffer[128];
        while (fgets(line_buffer, sizeof(line_buffer), uim)) {
            char slot[16];
            if (sscanf(line_buffer, "active_data_slot=%15s", slot) == 1) {
                assign_field(data, "slot", slot);
                break;
            }
        }
        fclose(uim);
    }

    FILE *bonding = fopen("/var/run/xm14pro-bonding/runtime", "r");
    if (bonding) {
        char line_buffer[160];
        while (fgets(line_buffer, sizeof(line_buffer), bonding)) {
            char *newline = strpbrk(line_buffer, "\r\n");
            if (newline) *newline = 0;
            char *separator = strchr(line_buffer, '=');
            if (!separator) continue;
            *separator = 0;
            const char *key = line_buffer;
            const char *value = separator + 1;
            if (!strcmp(key, "phase")) {
                if (!strcmp(value, "starting")) value = "正在启动";
                else if (!strcmp(value, "waiting_primary")) value = "等待主卡";
                else if (!strcmp(value, "waiting_secondary")) value = "正在连接辅卡";
                else if (!strcmp(value, "configuring")) value = "正在建立负载均衡";
                else if (!strcmp(value, "online")) value = "双卡聚合运行中";
                else if (!strcmp(value, "stopped")) value = "已停止";
                else if (!strcmp(value, "error")) value = "运行错误";
                assign_field(data, "bonding_phase", value);
            } else if (!strcmp(key, "balance_active")) {
                assign_field(data, "bonding_balance_active", value);
            }
        }
        fclose(bonding);
    }

    if (data->interface_name[0] && strcmp(data->interface_name, "--")) {
        char path[160];
        snprintf(path, sizeof(path), "/sys/class/net/%s/statistics/rx_bytes",
                 data->interface_name);
        data->rx_bytes = read_i64_file(path);
        snprintf(path, sizeof(path), "/sys/class/net/%s/statistics/tx_bytes",
                 data->interface_name);
        data->tx_bytes = read_i64_file(path);
        snprintf(path, sizeof(path), "/sys/class/net/%s/statistics/rx_packets",
                 data->interface_name);
        data->rx_packets = read_i64_file(path);
        snprintf(path, sizeof(path), "/sys/class/net/%s/statistics/tx_packets",
                 data->interface_name);
        data->tx_packets = read_i64_file(path);
        snprintf(path, sizeof(path), "/sys/class/net/%s/statistics/rx_errors",
                 data->interface_name);
        data->rx_errors = read_i64_file(path);
        snprintf(path, sizeof(path), "/sys/class/net/%s/statistics/tx_errors",
                 data->interface_name);
        data->tx_errors = read_i64_file(path);
    }

    FILE *uptime = fopen("/proc/uptime", "r");
    double uptime_seconds = 0.0;
    if (uptime) {
        if (fscanf(uptime, "%lf", &uptime_seconds) == 1 && uptime_seconds >= 0.0)
            data->uptime = (uint64_t)uptime_seconds;
        fclose(uptime);
    }

    const char *battery = "/sys/class/power_supply/battery";
    char path[160];
    snprintf(path, sizeof(path), "%s/capacity", battery);
    int capacity = read_integer_file(path);
    if (capacity >= 0 && capacity <= 100) data->battery = capacity;
    snprintf(path, sizeof(path), "%s/temp", battery);
    int battery_temperature = read_integer_file(path);
    if (battery_temperature) data->battery_temp = battery_temperature / 10.0;
    data->battery_voltage = read_i64_file("/sys/class/power_supply/battery/voltage_now");
    data->battery_current = read_i64_file("/sys/class/power_supply/battery/current_now");
    data->input_voltage = read_i64_file("/sys/class/power_supply/usb/voltage_now");
    data->input_current = read_i64_file("/sys/class/power_supply/usb/current_now");
    data->input_current_limit =
        read_i64_file("/sys/class/power_supply/usb/input_current_limit");
    data->input_online = read_integer_file("/sys/class/power_supply/usb/online");
    data->battery_fastchg_mode =
        read_integer_file("/sys/class/qcom-battery/fastchg_mode");
    data->battery_quick_charge_type =
        read_integer_file("/sys/class/qcom-battery/quick_charge_type");
    int connector_temperature =
        read_integer_file("/sys/class/qcom-battery/connector_temp");
    if (connector_temperature)
        data->connector_temp = connector_temperature / 10.0;
    if (data->input_voltage && data->input_current)
        data->input_power =
            (double)data->input_voltage * data->input_current / 1000000000000.0;
}

static bool speed_animation_pending(void) {
    return fabs(speed_dial_position - speed_dial_target_position) >= 0.0005 ||
           fabs(speed_dial_value) >= 0.05 ||
           fabs(speed_download_display - speed_download_peak) >= 0.05 ||
           fabs(speed_upload_display - speed_upload_peak) >= 0.05;
}

static void set_backlight(int percent) {
    const char *base = "/sys/class/backlight/panel0-backlight";
    char path[128];
    snprintf(path, sizeof(path), "%s/max_brightness", base);
    int maximum = read_integer_file(path);
    if (maximum <= 0) return;
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    int value = maximum * percent / 100;
    if (percent && !value) value = 1;
    snprintf(path, sizeof(path), "%s/brightness", base);
	if (read_integer_file(path) == value) return;
    FILE *file = fopen(path, "w");
    if (file) { fprintf(file, "%d\n", value); fclose(file); }
}

static bool screen_is_awake(const struct live_data *data) {
    struct stat wake;
    time_t now = time(NULL);
    if (access("/var/run/xm14display/off", F_OK) == 0) return false;
    if (!data->always_on && stat("/var/run/xm14display/wake", &wake) == 0 &&
        now - wake.st_mtime >= data->screen_timeout) return false;
    return true;
}

static void apply_backlight(const struct live_data *data) {
    set_backlight(screen_is_awake(data) ? data->brightness : 0);
}

static void stop_handler(int signal_number) {
    (void)signal_number;
    running = 0;
}

static int create_state_file(const char *path) {
    int descriptor = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (descriptor < 0) return -1;
    (void)write(descriptor, "1\n", 2);
    close(descriptor);
    return 0;
}

static bool request_early_handoff(void) {
    if (access(EARLY_SPLASH_ACTIVE_FILE, F_OK) != 0 &&
        access(EARLY_SPLASH_STARTING_FILE, F_OK) == 0) {
        /* card0 is registered several seconds before the Xiaomi DSI panel is
         * ready.  Let the init-ramdisk renderer finish its first modeset so
         * the two renderers cannot race for DRM master at procd startup. */
        for (int attempt = 0; attempt < 100 && running; ++attempt) {
            if (access(EARLY_SPLASH_ACTIVE_FILE, F_OK) == 0) break;
            if (access(EARLY_SPLASH_STARTING_FILE, F_OK) != 0) return false;
            usleep(10000);
        }
    }
    if (access(EARLY_SPLASH_ACTIVE_FILE, F_OK) != 0) {
        /* Fail open: a broken early renderer must never hold back the normal
         * screen service.  Ask it to leave and wait only for state cleanup. */
        if (access(EARLY_SPLASH_STARTING_FILE, F_OK) == 0) {
            (void)create_state_file(EARLY_SPLASH_STOP_FILE);
            for (int attempt = 0; attempt < 100 && running; ++attempt) {
                if (access(EARLY_SPLASH_STARTING_FILE, F_OK) != 0) break;
                usleep(10000);
            }
        }
        return false;
    }
    unlink(TAKEOVER_COMPLETE_FILE);
    if (create_state_file(TAKEOVER_REQUEST_FILE) != 0) return false;
    early_handoff_requested = true;
    for (int attempt = 0; attempt < 300 && running; ++attempt) {
        if (access(MASTER_RELEASED_FILE, F_OK) == 0) return true;
        if (access(EARLY_SPLASH_ACTIVE_FILE, F_OK) != 0) break;
        usleep(10000);
    }
    early_handoff_requested = false;
    return false;
}

static void complete_early_handoff(void) {
    if (!early_handoff_requested) return;
    (void)create_state_file(TAKEOVER_COMPLETE_FILE);
    early_handoff_requested = false;
}

static double boottime_seconds(void) {
    struct timespec value;
    clock_gettime(CLOCK_BOOTTIME, &value);
    return value.tv_sec + value.tv_nsec / 1000000000.0;
}

static double splash_start_seconds(void) {
    FILE *file = fopen(SPLASH_START_FILE, "re");
    double value = 0.0;
    if (file) {
        if (fscanf(file, "%lf", &value) != 1) value = 0.0;
        fclose(file);
    }
    if (value <= 0.0 || value > boottime_seconds())
        value = boottime_seconds();
    return value;
}

struct boot_log {
    char lines[BOOT_LOG_LINES][BOOT_LOG_LENGTH];
    int first;
    int count;
};

static void add_boot_line(struct boot_log *log, const char *record) {
    const char *message = strchr(record, ';');
    const char *first_comma = strchr(record, ',');
    const char *second_comma = first_comma ? strchr(first_comma + 1, ',') : NULL;
    unsigned long long timestamp_us = second_comma ?
        strtoull(second_comma + 1, NULL, 10) : 0ULL;
    char clean[BOOT_LOG_LENGTH];
    size_t output = 0;

    if (message) message++;
    else message = record;
    while (*message && output + 1 < sizeof(clean)) {
        unsigned char value = (unsigned char)*message++;
        if (value == '\n' || value == '\r') break;
        if (value == '\t') value = ' ';
        if (value >= 32 && value < 127) clean[output++] = (char)value;
    }
    clean[output] = 0;
    if (!*clean) return;

    int slot;
    if (log->count < BOOT_LOG_LINES) {
        slot = (log->first + log->count) % BOOT_LOG_LINES;
        log->count++;
    } else {
        slot = log->first;
        log->first = (log->first + 1) % BOOT_LOG_LINES;
    }
    snprintf(log->lines[slot], sizeof(log->lines[slot]),
             "[%6llu.%03llu] %s", timestamp_us / 1000000ULL,
             (timestamp_us / 1000ULL) % 1000ULL, clean);
}

static uint32_t boot_line_color(const char *line_value) {
    if (strstr(line_value, "Kernel panic") || strstr(line_value, "BUG:") ||
        strstr(line_value, "Internal error") ||
        strstr(line_value, "Unable to handle") ||
        strstr(line_value, "FATAL") || strstr(line_value, "fatal"))
        return rgb(255, 116, 96);
    if (strstr(line_value, "ERROR") || strstr(line_value, "error") ||
        strstr(line_value, "failed") || strstr(line_value, "Failed") ||
        strstr(line_value, "WARN") || strstr(line_value, "warn") ||
        strstr(line_value, "timeout"))
        return rgb(255, 184, 72);
    if (strstr(line_value, "nwrt") || strstr(line_value, "procd") ||
        strstr(line_value, "xm14pro"))
        return C_CYAN;
    return rgb(190, 204, 207);
}

static void render_boot_console(struct canvas *c, const struct boot_log *log) {
    fill_rect(c, 0, 0, c->width, c->height, C_BLACK);
    bold_text(c, 36, 28, "NWRT · BOOT", 42, C_GREEN);
    text(c, 1020, 38, "系统启动日志", 30, C_CYAN);
    fill_rect(c, 36, 100, c->width - 72, 2, C_BORDER);
    if (!log->count) {
        text(c, 36, 140, "正在接管显示并读取内核日志...", 28, C_MUTED);
        return;
    }
    for (int row = 0; row < log->count; ++row) {
        int slot = (log->first + row) % BOOT_LOG_LINES;
        text(c, 36, 125 + row * 41, log->lines[slot], 27,
             boot_line_color(log->lines[slot]));
    }
}

static int run_boot_console(struct display *display) {
    struct boot_log log = {0};
    int kmsg = open("/dev/kmsg", O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (kmsg >= 0) (void)lseek(kmsg, 0, SEEK_SET);

    struct canvas initial = {display->width, display->height,
                             display->buffers[0].pitch,
                             display->buffers[0].pixels};
    render_boot_console(&initial, &log);
    if (present(display, 0, true) != 0) {
        if (kmsg >= 0) close(kmsg);
        return -1;
    }
    complete_early_handoff();

    struct timespec last_frame = {0};
    while (running && access(DASHBOARD_READY_FILE, F_OK) != 0) {
        bool changed = false;
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        bool frame_due = last_frame.tv_sec == 0 ||
            monotonic_seconds(&now) - monotonic_seconds(&last_frame) >= 1.0 / 30.0;
        if (kmsg >= 0 && frame_due) {
            char record[2048];
            ssize_t length = read(kmsg, record, sizeof(record) - 1);
            if (length > 0) {
                record[length] = 0;
                add_boot_line(&log, record);
                changed = true;
            }
            if (length < 0 && errno != EAGAIN && errno != EINTR && errno != EPIPE) {
                close(kmsg);
                kmsg = -1;
            }
        }
        if (changed) {
            int next = display->visible ? 0 : 1;
            struct canvas canvas = {display->width, display->height,
                                    display->buffers[next].pitch,
                                    display->buffers[next].pixels};
            render_boot_console(&canvas, &log);
            if (present(display, next, false) != 0)
                fprintf(stderr, "xm14pro-screen: boot console frame failed: %s\n",
                        strerror(errno));
            last_frame = now;
        }
        usleep(8000);
    }
    if (kmsg >= 0) close(kmsg);
    return running ? 0 : -1;
}

static int run_boot_animation(struct display *display) {
    double started = splash_start_seconds();
    struct xm14_splash_canvas initial = {
        display->width, display->height, display->buffers[0].pitch,
        display->buffers[0].pixels
    };
    xm14_splash_render(&initial, boottime_seconds() - started);
    if (present(display, 0, true) != 0) return -1;
    complete_early_handoff();

    double last_frame = 0.0;
    while (running && access(DASHBOARD_READY_FILE, F_OK) != 0) {
        double now = boottime_seconds();
        if (now - last_frame >= 1.0 / 20.0) {
            int next = display->visible ? 0 : 1;
            struct xm14_splash_canvas canvas = {
                display->width, display->height,
                display->buffers[next].pitch,
                display->buffers[next].pixels
            };
            xm14_splash_render(&canvas, now - started);
            if (present(display, next, false) != 0) return -1;
            last_frame = now;
        }
        usleep(6000);
    }
    return running ? 0 : -1;
}

static int animate_page_transition(struct display *display,
                                   const struct live_data *data,
                                   double rx_rate, double tx_rate,
                                   int old_page, int new_page) {
    const int frames = 10;
    size_t frame_size = (size_t)display->buffers[0].pitch * display->height;
    uint32_t *old_frame = malloc(frame_size);
    uint32_t *new_frame = malloc(frame_size);
    if (!old_frame || !new_frame) {
        free(old_frame);
        free(new_frame);
        return -1;
    }

    memcpy(old_frame, display->buffers[display->visible].pixels, frame_size);
    int work = display->visible ? 0 : 1;
    struct canvas target = {display->width, display->height,
                            display->buffers[work].pitch,
                            display->buffers[work].pixels};
    render(&target, data, rx_rate, tx_rate, new_page);
    memcpy(new_frame, display->buffers[work].pixels, frame_size);

    int direction = new_page >= old_page ? 1 : -1;
    int stride = (int)(display->buffers[0].pitch / sizeof(uint32_t));
    for (int frame = 1; frame <= frames && running; ++frame) {
        double progress = (double)frame / frames;
        double eased = progress * progress * (3.0 - 2.0 * progress);
        int shift = (int)lround(eased * display->width);
        int output = display->visible ? 0 : 1;
        uint32_t *destination = display->buffers[output].pixels;
        for (int y = 0; y < display->height; ++y) {
            uint32_t *dst = destination + (size_t)y * stride;
            const uint32_t *from = old_frame + (size_t)y * stride;
            const uint32_t *to = new_frame + (size_t)y * stride;
            if (y >= FOOTER_TOP) {
                memcpy(dst, frame == frames ? to : from,
                       (size_t)display->width * sizeof(uint32_t));
                continue;
            }
            if (direction > 0) {
                int remaining = display->width - shift;
                if (remaining > 0)
                    memcpy(dst, from + shift,
                           (size_t)remaining * sizeof(uint32_t));
                if (shift > 0)
                    memcpy(dst + remaining, to,
                           (size_t)shift * sizeof(uint32_t));
            } else {
                if (shift > 0)
                    memcpy(dst, to + display->width - shift,
                           (size_t)shift * sizeof(uint32_t));
                int remaining = display->width - shift;
                if (remaining > 0)
                    memcpy(dst + shift, from,
                           (size_t)remaining * sizeof(uint32_t));
            }
        }
        if (present(display, output, false) != 0) {
            free(old_frame);
            free(new_frame);
            return -1;
        }
        if (frame != frames) usleep(12000);
    }
    free(old_frame);
    free(new_frame);
    return 0;
}

static int run_dashboard(struct display *display, bool already_modeset) {

    struct live_data data;
    struct live_data page_cache[PAGE_COUNT];
    bool page_cache_valid[PAGE_COUNT] = {false};
    struct fast_rate_sampler fast_sampler = {0};
    size_t frame_size = (size_t)display->buffers[0].pitch * display->height;
    uint32_t *overlay_base = malloc(frame_size);
    bool overlay_visible = false;
    char sampled_speed_phase[24] = "";
    double rx_rate = 0.0, tx_rate = 0.0;
    load_data(&data);
    copy_value(sampled_speed_phase, sizeof(sampled_speed_phase),
               data.speed_phase);
    update_rates(&data, &rx_rate, &tx_rate);
    int previous_page = current_page();
    if (previous_page != 2 || !strcmp(data.speed_state, "running"))
        sample_fast_rates(&fast_sampler, data.interface_name,
                          &rx_rate, &tx_rate);
    bool page_refresh_pending = false;
    struct timespec last_full_sample;
    struct timespec last_dynamic_sample;
    clock_gettime(CLOCK_MONOTONIC, &last_full_sample);
    last_dynamic_sample = last_full_sample;
    bool was_awake = screen_is_awake(&data);
    if (previous_page >= 0 && previous_page < PAGE_COUNT) {
        page_cache[previous_page] = data;
        page_cache_valid[previous_page] = true;
    }
    int initial_footer_start = current_footer_start();
    bool previous_footer_dragging = footer_is_dragging();
    footer_scroll_px = current_footer_position(initial_footer_start,
                                               previous_footer_dragging);
    int previous_footer_position = footer_scroll_px;
    int first_buffer = already_modeset ? (display->visible ? 0 : 1) : 0;
    struct canvas first = {display->width, display->height,
                           display->buffers[first_buffer].pitch,
                           display->buffers[first_buffer].pixels};
    render(&first, &data, rx_rate, tx_rate, previous_page);
    if (present(display, first_buffer, !already_modeset) != 0) {
        fprintf(stderr, "xm14pro-screen: initial atomic commit failed: %s\n", strerror(errno));
        free(overlay_base);
        return 1;
    }
    complete_early_handoff();
    struct timespec overlay_initial_time;
    clock_gettime(CLOCK_MONOTONIC, &overlay_initial_time);
    update_volume_overlay(&overlay_initial_time);
    apply_backlight(&data);

    while (running) {
        bool loop_awake = screen_is_awake(&data);
        int page = previous_page;
        bool speed_page = page == 2;
        bool speed_running = speed_page && !strcmp(data.speed_state, "running");
        bool speed_animating = speed_page && speed_animation_pending();
        /*
         * Keep touch navigation responsive even while the speed-test page is
         * idle.  Rendering is still conditional below, so this only checks
         * the tmpfs page selector more frequently and does not redraw at
         * 50 Hz.
         */
        useconds_t wait_us = !loop_awake ? 250000U :
            (volume_overlay.active ? 33333U :
             (speed_running || speed_animating || previous_footer_dragging ?
              16667U : 100000U));
        usleep(wait_us);
        if (!running) break;

        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        bool awake = screen_is_awake(&data);
        if (!awake) {
            if (was_awake) {
                set_backlight(0);
                fast_sampler.initialized = false;
                fast_cpu_previous_total = 0;
                fast_cpu_previous_idle = 0;
                rx_rate = 0.0;
                tx_rate = 0.0;
            }
            was_awake = false;
            volume_overlay.active = false;
            overlay_visible = false;
            continue;
        }

        bool overlay_changed = update_volume_overlay(&now);
        if (volume_overlay.active && overlay_base) {
            if (!overlay_visible)
                memcpy(overlay_base,
                       display->buffers[display->visible].pixels, frame_size);
            int next = display->visible ? 0 : 1;
            memcpy(display->buffers[next].pixels, overlay_base, frame_size);
            struct canvas overlay_canvas = {
                display->width, display->height,
                display->buffers[next].pitch,
                display->buffers[next].pixels
            };
            draw_volume_overlay(&overlay_canvas, monotonic_seconds(&now));
            if (present(display, next, false) != 0)
                fprintf(stderr,
                        "xm14pro-screen: volume overlay frame failed: %s\n",
                        strerror(errno));
            overlay_visible = true;
            continue;
        }
        if (overlay_visible && overlay_base && overlay_changed) {
            int next = display->visible ? 0 : 1;
            memcpy(display->buffers[next].pixels, overlay_base, frame_size);
            if (present(display, next, false) != 0)
                fprintf(stderr,
                        "xm14pro-screen: volume overlay restore failed: %s\n",
                        strerror(errno));
            overlay_visible = false;
            continue;
        }

        page = current_page();
        bool page_changed = page != previous_page;
        if (page_changed && page >= 0 && page < PAGE_COUNT &&
            page_cache_valid[page])
            data = page_cache[page];
        int next_footer_start = current_footer_start();
        bool footer_dragging = footer_is_dragging();
        int next_footer_position = current_footer_position(next_footer_start,
                                                           footer_dragging);
        bool footer_changed = next_footer_position != previous_footer_position;
        bool footer_drag_started = footer_dragging &&
                                   !previous_footer_dragging;
        bool footer_refresh = footer_changed || footer_drag_started;
        if (!was_awake) {
            load_data(&data);
            refresh_fast_hardware(&data);
            fast_sampler.initialized = false;
            sample_fast_rates(&fast_sampler, data.interface_name,
                              &rx_rate, &tx_rate);
            rx_rate = 0.0;
            tx_rate = 0.0;
            last_full_sample = now;
            last_dynamic_sample = now;
            page_refresh_pending = false;
            footer_scroll_px = next_footer_position;
            int next = display->visible ? 0 : 1;
            struct canvas canvas = {display->width, display->height,
                                    display->buffers[next].pitch,
                                    display->buffers[next].pixels};
            render(&canvas, &data, rx_rate, tx_rate, page);
            if (present(display, next, false) != 0)
                fprintf(stderr, "xm14pro-screen: wake frame failed: %s\n",
                        strerror(errno));
            set_backlight(data.brightness);
            if (page >= 0 && page < PAGE_COUNT) {
                page_cache[page] = data;
                page_cache_valid[page] = true;
            }
            was_awake = true;
            previous_page = page;
            previous_footer_position = next_footer_position;
            previous_footer_dragging = footer_dragging;
            continue;
        }
        /*
         * Pages are snapshot-driven.  Their last frame/data stay cached while
         * hidden and there is no all-page telemetry poll in the background.
         * Entering a page schedules exactly one fresh snapshot; a running
         * speed test is the only high-rate exception.
         */
        double sample_time = monotonic_seconds(&now);
        bool external_refresh = access(EXTERNAL_REFRESH_FILE, F_OK) == 0;
        bool telephony_fast_refresh = false;
        if (external_refresh) {
            unlink(EXTERNAL_REFRESH_FILE);
            if (page == 10 || page == 11) {
                refresh_telephony_drafts(&data);
                telephony_fast_refresh = true;
            } else {
                page_refresh_pending = true;
            }
        }
        bool full_sample_due = page_refresh_pending ||
            sample_time - monotonic_seconds(&last_full_sample) >=
                data.refresh_interval;
        bool active_rate_page = page == 0 || page == 3;
        bool dynamic_sample_due =
            sample_time - monotonic_seconds(&last_dynamic_sample) >= 1.0;

        if (footer_dragging && !page_changed && !footer_refresh) {
            /*
             * Freeze the page snapshot while a finger is resting on the
             * footer.  A periodic telemetry refresh here would update only
             * one DRM buffer, so the following drag frame could alternate
             * between two different page snapshots and visibly flash.
             */
            previous_page = page;
            previous_footer_position = next_footer_position;
            previous_footer_dragging = footer_dragging;
            continue;
        }

        if (page == 2 && !footer_refresh) {
            char previous_speed_state[sizeof(data.speed_state)];
            char previous_speed_phase[sizeof(data.speed_phase)];
            copy_value(previous_speed_state, sizeof(previous_speed_state),
                       data.speed_state);
            copy_value(previous_speed_phase, sizeof(previous_speed_phase),
                       data.speed_phase);
            double previous_download = data.speed_download_mbps;
            double previous_upload = data.speed_upload_mbps;
            load_speed_state_only(&data);
            bool speed_state_changed =
                strcmp(previous_speed_state, data.speed_state) != 0 ||
                strcmp(previous_speed_phase, data.speed_phase) != 0 ||
                previous_download != data.speed_download_mbps ||
                previous_upload != data.speed_upload_mbps;
            speed_running = !strcmp(data.speed_state, "running");
            if (strcmp(sampled_speed_phase, data.speed_phase) != 0) {
                fast_sampler.initialized = false;
                rx_rate = 0.0;
                tx_rate = 0.0;
                copy_value(sampled_speed_phase, sizeof(sampled_speed_phase),
                           data.speed_phase);
            }
            bool transfer_phase = speed_running &&
                (!strcmp(data.speed_phase, "download") ||
                 !strcmp(data.speed_phase, "upload"));
            if (transfer_phase)
                sample_fast_rates(&fast_sampler, data.interface_name,
                                  &rx_rate, &tx_rate);
            else {
                fast_sampler.initialized = false;
                rx_rate = 0.0;
                tx_rate = 0.0;
            }
            bool full_sample_completed = false;
            if (!speed_running && !page_changed && full_sample_due) {
                load_data(&data);
                page_refresh_pending = false;
                clock_gettime(CLOCK_MONOTONIC, &last_full_sample);
                full_sample_completed = true;
            }
            bool dynamic_sample_completed = false;
            if (!page_changed && dynamic_sample_due) {
                refresh_fast_hardware(&data);
                last_dynamic_sample = now;
                dynamic_sample_completed = true;
            }
            speed_animating = speed_animation_pending();
            if (!page_changed && !speed_state_changed && !speed_running &&
                !speed_animating && !full_sample_completed &&
                !dynamic_sample_completed) {
                previous_page = page;
                previous_footer_position = next_footer_position;
                previous_footer_dragging = footer_dragging;
                continue;
            }
        } else if (page_changed || footer_refresh) {
            /*
             * Draw the newly selected page from the last live snapshot
             * immediately.  Running the complete shell data collector here
             * made a valid 120 Hz touch feel delayed; the normal periodic
             * refresh below replaces the cached values shortly afterwards.
             */
        } else if (telephony_fast_refresh) {
            /* Draft edits are local tmpfs reads. Keep the touch keyboard
             * responsive without rerunning the full modem collector. */
        } else if (full_sample_due) {
            load_data(&data);
            refresh_fast_hardware(&data);
            if (active_rate_page &&
                sample_fast_rates(&fast_sampler, data.interface_name,
                                  &rx_rate, &tx_rate)) {
                push_history(rx_history, rx_rate);
                push_history(tx_history, tx_rate);
            }
            page_refresh_pending = false;
            clock_gettime(CLOCK_MONOTONIC, &last_full_sample);
            last_dynamic_sample = now;
        } else if (dynamic_sample_due) {
            refresh_fast_hardware(&data);
            if (active_rate_page) {
                if (sample_fast_rates(&fast_sampler, data.interface_name,
                                      &rx_rate, &tx_rate)) {
                    push_history(rx_history, rx_rate);
                    push_history(tx_history, tx_rate);
                }
            }
            last_dynamic_sample = now;
        } else {
            previous_page = page;
            previous_footer_position = next_footer_position;
            previous_footer_dragging = footer_dragging;
            continue;
        }

        if (page_changed) {
            footer_scroll_px = next_footer_position;
            if (animate_page_transition(display, &data, rx_rate, tx_rate,
                                        previous_page, page) != 0) {
                int next = display->visible ? 0 : 1;
                struct canvas canvas = {display->width, display->height,
                                        display->buffers[next].pitch,
                                        display->buffers[next].pixels};
                render(&canvas, &data, rx_rate, tx_rate, page);
                if (present(display, next, false) != 0)
                    fprintf(stderr,
                            "xm14pro-screen: page transition failed: %s\n",
                            strerror(errno));
            }
            apply_backlight(&data);
            fast_sampler.initialized = false;
            rx_rate = 0.0;
            tx_rate = 0.0;
            last_dynamic_sample = now;
            page_refresh_pending = true;
            previous_page = page;
            previous_footer_position = next_footer_position;
            previous_footer_dragging = footer_dragging;
            continue;
        }

        if (footer_refresh) {
            int next = display->visible ? 0 : 1;
            size_t frame_size = (size_t)display->buffers[0].pitch *
                                display->height;
            if (footer_drag_started)
                memcpy(display->buffers[next].pixels,
                       display->buffers[display->visible].pixels, frame_size);
            footer_scroll_px = next_footer_position;
            struct canvas canvas = {display->width, display->height,
                                    display->buffers[next].pitch,
                                    display->buffers[next].pixels};
            draw_footer(&canvas, page);
            if (present(display, next, false) != 0)
                fprintf(stderr, "xm14pro-screen: footer frame failed: %s\n",
                        strerror(errno));
            apply_backlight(&data);
            previous_footer_position = next_footer_position;
            previous_footer_dragging = footer_dragging;
            continue;
        }

        footer_scroll_px = next_footer_position;
        int next = display->visible ? 0 : 1;
        struct canvas canvas = {display->width, display->height,
                                display->buffers[next].pitch,
                                display->buffers[next].pixels};
        render(&canvas, &data, rx_rate, tx_rate, page);
        if (present(display, next, false) != 0)
            fprintf(stderr, "xm14pro-screen: atomic frame failed: %s\n", strerror(errno));
        apply_backlight(&data);
            if (page >= 0 && page < PAGE_COUNT) {
            page_cache[page] = data;
            page_cache_valid[page] = true;
        }
        previous_page = page;
        previous_footer_position = next_footer_position;
        previous_footer_dragging = footer_dragging;
    }
    free(overlay_base);
    return 0;
}

int main(int argc, char **argv) {
    signal(SIGINT, stop_handler);
    signal(SIGTERM, stop_handler);
    signal(SIGHUP, SIG_IGN);
    mkdir("/var/run/xm14display", 0755);
    int wake = open("/var/run/xm14display/wake", O_WRONLY | O_CREAT, 0644);
    if (wake >= 0) { futimens(wake, NULL); close(wake); }

    bool boot_console = argc == 2 && !strcmp(argv[1], "--boot-console");
    bool boot_animation = argc == 2 && !strcmp(argv[1], "--boot-animation");
    if (argc > 2 || (argc == 2 && !boot_console && !boot_animation)) {
        fprintf(stderr, "usage: %s [--boot-animation|--boot-console]\n", argv[0]);
        return 2;
    }

    (void)request_early_handoff();
    struct display display;
    if (open_display(&display) != 0) {
        fprintf(stderr, "xm14pro-screen: DRM initialization failed: %s\n", strerror(errno));
        return 1;
    }
    if (display.width != 1440 || display.height != 3200) {
        fprintf(stderr, "xm14pro-screen: unexpected mode %dx%d\n",
                display.width, display.height);
        close_display(&display);
        return 1;
    }

    bool already_modeset = false;
    if (boot_animation) {
        if (run_boot_animation(&display) != 0) {
            close_display(&display);
            return running ? 1 : 0;
        }
        already_modeset = true;
    } else if (boot_console) {
        if (run_boot_console(&display) != 0) {
            close_display(&display);
            return running ? 1 : 0;
        }
        already_modeset = true;
    }
    int result = run_dashboard(&display, already_modeset);
    close_display(&display);
    return result;
}

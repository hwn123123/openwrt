// SPDX-License-Identifier: GPL-2.0-only
#ifndef XM14PRO_BOOT_SCENE_H
#define XM14PRO_BOOT_SCENE_H

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#define XM14_SPLASH_PI 3.14159265358979323846

struct xm14_splash_canvas {
    int width;
    int height;
    uint32_t pitch;
    uint32_t *pixels;
};

static inline uint32_t xm14_splash_rgb(uint8_t red, uint8_t green,
                                       uint8_t blue) {
    return 0xff000000U | ((uint32_t)blue << 16) |
           ((uint32_t)green << 8) | red;
}

static inline uint32_t xm14_splash_mix(uint32_t first, uint32_t second,
                                       int amount) {
    int first_red = first & 255;
    int first_green = (first >> 8) & 255;
    int first_blue = (first >> 16) & 255;
    int second_red = second & 255;
    int second_green = (second >> 8) & 255;
    int second_blue = (second >> 16) & 255;
    return xm14_splash_rgb(
        (uint8_t)((first_red * (100 - amount) + second_red * amount) / 100),
        (uint8_t)((first_green * (100 - amount) + second_green * amount) / 100),
        (uint8_t)((first_blue * (100 - amount) + second_blue * amount) / 100));
}

static inline void xm14_splash_fill(struct xm14_splash_canvas *canvas,
                                    int x, int y, int width, int height,
                                    uint32_t color) {
    if (x < 0) { width += x; x = 0; }
    if (y < 0) { height += y; y = 0; }
    if (x + width > canvas->width) width = canvas->width - x;
    if (y + height > canvas->height) height = canvas->height - y;
    if (width <= 0 || height <= 0) return;
    for (int row = y; row < y + height; ++row) {
        uint32_t *destination = (uint32_t *)((uint8_t *)canvas->pixels +
            (size_t)row * canvas->pitch) + x;
        for (int column = 0; column < width; ++column)
            destination[column] = color;
    }
}

static inline int xm14_splash_isqrt(int value) {
    int result = 0;
    while ((result + 1) * (result + 1) <= value) ++result;
    return result;
}

static inline void xm14_splash_round_rect(struct xm14_splash_canvas *canvas,
                                          int x, int y, int width, int height,
                                          int radius, uint32_t color) {
    if (radius < 1) {
        xm14_splash_fill(canvas, x, y, width, height, color);
        return;
    }
    if (radius * 2 > width) radius = width / 2;
    if (radius * 2 > height) radius = height / 2;
    for (int row = 0; row < height; ++row) {
        int inset = 0;
        if (row < radius) {
            int vertical = radius - row - 1;
            inset = radius - xm14_splash_isqrt(radius * radius -
                                                vertical * vertical);
        } else if (row >= height - radius) {
            int vertical = row - (height - radius);
            inset = radius - xm14_splash_isqrt(radius * radius -
                                                vertical * vertical);
        }
        xm14_splash_fill(canvas, x + inset, y + row,
                         width - inset * 2, 1, color);
    }
}

static inline void xm14_splash_circle(struct xm14_splash_canvas *canvas,
                                      int center_x, int center_y, int radius,
                                      uint32_t color) {
    for (int y = -radius; y <= radius; ++y) {
        int half = xm14_splash_isqrt(radius * radius - y * y);
        xm14_splash_fill(canvas, center_x - half, center_y + y,
                         half * 2 + 1, 1, color);
    }
}

static inline void xm14_splash_line(struct xm14_splash_canvas *canvas,
                                    int x0, int y0, int x1, int y1,
                                    int thickness, uint32_t color) {
    int delta_x = abs(x1 - x0);
    int step_x = x0 < x1 ? 1 : -1;
    int delta_y = -abs(y1 - y0);
    int step_y = y0 < y1 ? 1 : -1;
    int error = delta_x + delta_y;
    for (;;) {
        xm14_splash_circle(canvas, x0, y0, thickness / 2, color);
        if (x0 == x1 && y0 == y1) break;
        int twice = error * 2;
        if (twice >= delta_y) { error += delta_y; x0 += step_x; }
        if (twice <= delta_x) { error += delta_x; y0 += step_y; }
    }
}

static inline void xm14_splash_arc(struct xm14_splash_canvas *canvas,
                                   int center_x, int center_y, int radius,
                                   double start, double end, int thickness,
                                   uint32_t color) {
    int steps = (int)(fabs(end - start) * radius / 4.0);
    if (steps < 12) steps = 12;
    int previous_x = center_x + (int)lround(cos(start) * radius);
    int previous_y = center_y + (int)lround(sin(start) * radius);
    for (int step = 1; step <= steps; ++step) {
        double angle = start + (end - start) * step / steps;
        int x = center_x + (int)lround(cos(angle) * radius);
        int y = center_y + (int)lround(sin(angle) * radius);
        xm14_splash_line(canvas, previous_x, previous_y, x, y,
                         thickness, color);
        previous_x = x;
        previous_y = y;
    }
}

#define XM14_GLYPH(a, b, c, d, e, f, g) \
    (((uint64_t)(a) << 30) | ((uint64_t)(b) << 25) | \
     ((uint64_t)(c) << 20) | ((uint64_t)(d) << 15) | \
     ((uint64_t)(e) << 10) | ((uint64_t)(f) << 5) | (uint64_t)(g))

static inline uint64_t xm14_splash_glyph(char character) {
    if (character >= 'a' && character <= 'z' && character != 'n' &&
        character != 'w' && character != 'r' && character != 't')
        character = (char)(character - 'a' + 'A');
    switch (character) {
    case 'A': return XM14_GLYPH(14,17,17,31,17,17,17);
    case 'B': return XM14_GLYPH(30,17,17,30,17,17,30);
    case 'C': return XM14_GLYPH(14,17,16,16,16,17,14);
    case 'D': return XM14_GLYPH(30,17,17,17,17,17,30);
    case 'E': return XM14_GLYPH(31,16,16,30,16,16,31);
    case 'F': return XM14_GLYPH(31,16,16,30,16,16,16);
    case 'G': return XM14_GLYPH(14,17,16,23,17,17,15);
    case 'H': return XM14_GLYPH(17,17,17,31,17,17,17);
    case 'I': return XM14_GLYPH(31,4,4,4,4,4,31);
    case 'J': return XM14_GLYPH(7,2,2,2,18,18,12);
    case 'K': return XM14_GLYPH(17,18,20,24,20,18,17);
    case 'L': return XM14_GLYPH(16,16,16,16,16,16,31);
    case 'M': return XM14_GLYPH(17,27,21,21,17,17,17);
    case 'N': return XM14_GLYPH(17,25,21,19,17,17,17);
    case 'O': return XM14_GLYPH(14,17,17,17,17,17,14);
    case 'P': return XM14_GLYPH(30,17,17,30,16,16,16);
    case 'Q': return XM14_GLYPH(14,17,17,17,21,18,13);
    case 'R': return XM14_GLYPH(30,17,17,30,20,18,17);
    case 'S': return XM14_GLYPH(15,16,16,14,1,1,30);
    case 'T': return XM14_GLYPH(31,4,4,4,4,4,4);
    case 'U': return XM14_GLYPH(17,17,17,17,17,17,14);
    case 'V': return XM14_GLYPH(17,17,17,17,17,10,4);
    case 'W': return XM14_GLYPH(17,17,17,21,21,21,10);
    case 'X': return XM14_GLYPH(17,17,10,4,10,17,17);
    case 'Y': return XM14_GLYPH(17,17,10,4,4,4,4);
    case 'Z': return XM14_GLYPH(31,1,2,4,8,16,31);
    case 'n': return XM14_GLYPH(0,0,30,17,17,17,17);
    case 'w': return XM14_GLYPH(0,0,17,17,21,21,10);
    case 'r': return XM14_GLYPH(0,0,22,25,16,16,16);
    case 't': return XM14_GLYPH(4,4,31,4,4,5,2);
    case '0': return XM14_GLYPH(14,17,19,21,25,17,14);
    case '1': return XM14_GLYPH(4,12,4,4,4,4,14);
    case '2': return XM14_GLYPH(14,17,1,2,4,8,31);
    case '3': return XM14_GLYPH(30,1,1,14,1,1,30);
    case '4': return XM14_GLYPH(2,6,10,18,31,2,2);
    case '5': return XM14_GLYPH(31,16,16,30,1,1,30);
    case '6': return XM14_GLYPH(14,16,16,30,17,17,14);
    case '7': return XM14_GLYPH(31,1,2,4,8,8,8);
    case '8': return XM14_GLYPH(14,17,17,14,17,17,14);
    case '9': return XM14_GLYPH(14,17,17,15,1,1,14);
    case '/': return XM14_GLYPH(1,1,2,4,8,16,16);
    case ':': return XM14_GLYPH(0,4,4,0,4,4,0);
    case '.': return XM14_GLYPH(0,0,0,0,0,6,6);
    case '-': return XM14_GLYPH(0,0,0,31,0,0,0);
    case '>': return XM14_GLYPH(16,8,4,2,4,8,16);
    case ' ': return 0;
    default: return XM14_GLYPH(14,17,2,4,4,0,4);
    }
}

static inline int xm14_splash_text_width(const char *text, int scale) {
    int length = 0;
    while (text[length]) ++length;
    return length > 0 ? length * scale * 6 - scale : 0;
}

static inline void xm14_splash_text(struct xm14_splash_canvas *canvas,
                                    int x, int y, const char *text, int scale,
                                    uint32_t color, bool glow) {
    uint32_t glow_color = xm14_splash_mix(xm14_splash_rgb(0, 8, 18),
                                          color, 30);
    for (const char *cursor = text; *cursor; ++cursor, x += scale * 6) {
        uint64_t glyph = xm14_splash_glyph(*cursor);
        for (int row = 0; row < 7; ++row) {
            uint8_t bits = (uint8_t)((glyph >> ((6 - row) * 5)) & 31U);
            for (int column = 0; column < 5; ++column) {
                if (!(bits & (1U << (4 - column)))) continue;
                int left = x + column * scale;
                int top = y + row * scale;
                if (glow)
                    xm14_splash_round_rect(canvas, left - scale / 7,
                        top - scale / 7, scale + scale * 2 / 7,
                        scale + scale * 2 / 7, scale / 3, glow_color);
                xm14_splash_round_rect(canvas, left, top,
                    scale * 4 / 5, scale * 4 / 5, scale / 4, color);
            }
        }
    }
}

static const uint32_t xm14_splash_hanzi_developer[2][32] = {
    {
        0x00000000U,0x00000000U,0x00000000U,0x00000080U,
        0x000001c0U,0x000003e0U,0x0ffffff0U,0x007c3c00U,
        0x00783c00U,0x00783c00U,0x00783c00U,0x00783c00U,
        0x00783c60U,0x00783cf0U,0x00783cf8U,0x3ffffffcU,
        0x00783c00U,0x00783c00U,0x00783c00U,0x00783c00U,
        0x00783c00U,0x00783c00U,0x00703c00U,0x00f03c00U,
        0x00e03c00U,0x01c03c00U,0x03c03c00U,0x07003c00U,
        0x0c003c00U,0x30003c00U,0x00000000U,0x00000000U
    },
    {
        0x00000000U,0x00000000U,0x00038000U,0x0007e000U,
        0x01c7fe00U,0x03e7cf80U,0x03e7c780U,0x03c78780U,
        0x03c78740U,0x07878070U,0x078f80f8U,0x0ffffffcU,
        0x070f0000U,0x000f0200U,0x001e0780U,0x001fffe0U,
        0x001e07e0U,0x003f07c0U,0x003f0f80U,0x00798f00U,
        0x0071df00U,0x00f0fe00U,0x00e0fc00U,0x01c07c00U,
        0x0380ff80U,0x0603fff0U,0x0c078ffcU,0x181e07f8U,
        0x00f801f0U,0x03800060U,0x00000000U,0x00000000U
    }
};

static inline void xm14_splash_hanzi(struct xm14_splash_canvas *canvas,
                                     int x, int y, int glyph_index, int size,
                                     uint32_t color) {
    for (int destination_y = 0; destination_y < size; ++destination_y) {
        int source_y = destination_y * 32 / size;
        uint32_t row = xm14_splash_hanzi_developer[glyph_index][source_y];
        for (int destination_x = 0; destination_x < size; ++destination_x) {
            int source_x = destination_x * 32 / size;
            if (row & (1U << (31 - source_x)))
                xm14_splash_fill(canvas, x + destination_x,
                                 y + destination_y, 1, 1, color);
        }
    }
}

static inline void xm14_splash_background(struct xm14_splash_canvas *canvas) {
    /* Keep the boot canvas byte-for-byte in the dashboard's colour family.
     * The main UI uses rgb(0, 8, 10) -> rgb(0, 4, 6) and a 96 px grid. */
    for (int y = 0; y < canvas->height; ++y) {
        int green = y < 2000 ? 8 - y * 4 / 2000 : 4;
        xm14_splash_fill(canvas, 0, y, canvas->width, 1,
                         xm14_splash_rgb(0, (uint8_t)green,
                                         (uint8_t)(green + 2)));
    }
    uint32_t grid = xm14_splash_rgb(4, 24, 27);
    for (int x = 0; x < canvas->width; x += 96)
        xm14_splash_fill(canvas, x, 0, 1, canvas->height, grid);
    for (int y = 0; y < canvas->height; y += 96)
        xm14_splash_fill(canvas, 0, y, canvas->width, 1, grid);
}

static inline void xm14_splash_router(struct xm14_splash_canvas *canvas,
                                      double elapsed, uint32_t blue,
                                      uint32_t cyan) {
    int center_x = canvas->width / 2;
    int center_y = 1430;
    double pulse_value = (sin(elapsed * 3.1) + 1.0) * 0.5;
    int pulse = (int)lround(pulse_value * 28.0);
    uint32_t card = xm14_splash_rgb(25, 23, 16);
    uint32_t border = xm14_splash_rgb(81, 75, 23);
    uint32_t track = xm14_splash_rgb(60, 54, 38);
    uint32_t white = xm14_splash_rgb(250, 247, 245);
    uint32_t muted = xm14_splash_rgb(159, 148, 140);
    uint32_t green = xm14_splash_rgb(114, 220, 40);
    uint32_t dim_blue = xm14_splash_mix(card, blue, 28);

    /* The same framed-card geometry used throughout the live dashboard. */
    xm14_splash_round_rect(canvas, 44, 1010, canvas->width - 88,
                           1005, 32, border);
    xm14_splash_round_rect(canvas, 48, 1014, canvas->width - 96,
                           997, 29, card);

    xm14_splash_text(canvas, 112, 1095, "5G", 28, white, false);
    xm14_splash_text(canvas, 112, 1328, "MOBILE NETWORK", 7,
                     muted, false);
    xm14_splash_round_rect(canvas, 1030, 1100, 250, 82, 41,
                           xm14_splash_mix(card, green, 18));
    xm14_splash_circle(canvas, 1072, 1141, 10, green);
    xm14_splash_text(canvas, 1105, 1118, "STARTING", 6, green, false);

    /* Live signal bars mirror the dashboard hero card. */
    for (int bar = 0; bar < 4; ++bar) {
        int height = 34 + bar * 32;
        uint32_t color = bar <= ((int)(elapsed * 2.0) % 5) ? cyan : track;
        xm14_splash_round_rect(canvas, 1080 + bar * 46,
                               1415 - height, 24, height, 12, color);
    }

    for (int ring = 0; ring < 3; ++ring) {
        int radius = 105 + ring * 86 + ((pulse + ring * 9) % 28);
        uint32_t color = xm14_splash_mix(dim_blue, cyan,
                                         18 + ring * 13);
        xm14_splash_arc(canvas, center_x, center_y - 250, radius,
                        XM14_SPLASH_PI * 1.18, XM14_SPLASH_PI * 1.82,
                        11 - ring * 2, color);
    }
    xm14_splash_circle(canvas, center_x, center_y - 250, 22, cyan);
    xm14_splash_circle(canvas, center_x, center_y - 250, 9,
                       xm14_splash_rgb(225, 250, 255));

    xm14_splash_round_rect(canvas, center_x - 290, center_y - 90,
                           580, 510, 64, border);
    xm14_splash_round_rect(canvas, center_x - 270, center_y - 70,
                           540, 470, 52, xm14_splash_rgb(0, 8, 10));
    xm14_splash_round_rect(canvas, center_x - 205, center_y + 38,
                           410, 205, 45, xm14_splash_mix(card, blue, 14));
    xm14_splash_round_rect(canvas, center_x - 190, center_y + 53,
                           380, 175, 35, card);

    const char *network = "5G";
    int network_scale = 31;
    int network_width = xm14_splash_text_width(network, network_scale);
    xm14_splash_text(canvas, center_x - network_width / 2,
                     center_y + 56, network, network_scale,
                     blue, true);
    const char *device_label = "MOBILE WIFI";
    int device_label_scale = 6;
    xm14_splash_text(canvas,
                     center_x - xm14_splash_text_width(
                         device_label, device_label_scale) / 2,
                     center_y + 292, device_label, device_label_scale,
                     muted, false);
    for (int light = 0; light < 4; ++light) {
        int active = ((int)(elapsed * 5.0) + light) % 4;
        xm14_splash_circle(canvas, center_x - 63 + light * 42,
                           center_y + 350, 7,
                           active == 0 ? cyan : dim_blue);
    }

    for (int packet = 0; packet < 8; ++packet) {
        double phase = fmod(elapsed * 0.42 + packet / 8.0, 1.0);
        double angle = phase * XM14_SPLASH_PI * 2.0;
        int x = center_x + (int)lround(cos(angle) * 455.0);
        int y = center_y + 130 + (int)lround(sin(angle) * 315.0);
        int radius = packet & 1 ? 7 : 10;
        xm14_splash_circle(canvas, x, y, radius,
                           packet & 1 ? cyan : blue);
    }
}

static inline void xm14_splash_code_panel(struct xm14_splash_canvas *canvas,
                                          double elapsed, uint32_t blue,
                                          uint32_t cyan) {
    static const char *lines[] = {
        "5G MODEM LINK", "WIFI HOTSPOT", "NWRT SERVICES", "SYSTEM READY"
    };
    int completed = (int)(elapsed / 1.15);
    if (completed > 4) completed = 4;
    int left = 105;
    int top = 2100;
    uint32_t card = xm14_splash_rgb(25, 23, 16);
    uint32_t border = xm14_splash_rgb(81, 75, 23);
    uint32_t muted = xm14_splash_rgb(159, 148, 140);
    uint32_t green = xm14_splash_rgb(114, 220, 40);
    uint32_t track = xm14_splash_rgb(60, 54, 38);
    xm14_splash_round_rect(canvas, left, top, canvas->width - left * 2,
                           690, 42, border);
    xm14_splash_round_rect(canvas, left + 3, top + 3,
                           canvas->width - left * 2 - 6, 684, 39, card);
    xm14_splash_text(canvas, left + 55, top + 42, "NWRT SYSTEM START",
                     10, green, false);
    for (int index = 0; index < 4; ++index) {
        int y = top + 148 + index * 84;
        bool done = index < completed;
        bool active = index == completed && completed < 4;
        uint32_t color = done ? cyan :
            (active ? xm14_splash_mix(blue, cyan,
                45 + (int)((sin(elapsed * 5.0) + 1.0) * 20.0)) :
             muted);
        xm14_splash_circle(canvas, left + 64, y + 24, done ? 10 : 8, color);
        if (done) {
            xm14_splash_line(canvas, left + 57, y + 24,
                             left + 63, y + 31, 4,
                             xm14_splash_rgb(231, 255, 255));
            xm14_splash_line(canvas, left + 63, y + 31,
                             left + 74, y + 16, 4,
                             xm14_splash_rgb(231, 255, 255));
        }
        xm14_splash_text(canvas, left + 112, y, lines[index], 8,
                         color, false);
        if (active) {
            int cursor = left + 112 + xm14_splash_text_width(lines[index], 8) + 16;
            if (((int)(elapsed * 3.0) & 1) == 0)
                xm14_splash_fill(canvas, cursor, y + 4, 7, 48, blue);
        }
    }
    int progress_width = canvas->width - left * 2 - 110;
    int progress = 12 + (int)(elapsed * 12.0);
    if (progress > 94) progress = 94 + ((int)(elapsed * 2.0) & 1);
    xm14_splash_round_rect(canvas, left + 55, top + 594,
                           progress_width, 18, 9,
                           track);
    xm14_splash_round_rect(canvas, left + 55, top + 594,
                           progress_width * progress / 100, 18, 9, cyan);
    xm14_splash_text(canvas, left + 55, top + 630, "LOADING DASHBOARD",
                     6, muted, false);
}

static inline void xm14_splash_developer(struct xm14_splash_canvas *canvas,
                                         uint32_t color) {
    const char *numbers = "450843633/1562703382";
    int hanzi_size = 50;
    int scale = 7;
    int colon_width = 24;
    int width = hanzi_size * 2 + 22 + colon_width +
                xm14_splash_text_width(numbers, scale);
    int x = (canvas->width - width) / 2;
    int y = 3045;
    xm14_splash_hanzi(canvas, x, y, 0, hanzi_size, color);
    x += hanzi_size + 11;
    xm14_splash_hanzi(canvas, x, y, 1, hanzi_size, color);
    x += hanzi_size + 11;
    xm14_splash_circle(canvas, x + 6, y + 15, 4, color);
    xm14_splash_circle(canvas, x + 6, y + 37, 4, color);
    x += colon_width;
    xm14_splash_text(canvas, x, y, numbers, scale, color, false);
}

static inline void xm14_splash_render(struct xm14_splash_canvas *canvas,
                                      double elapsed) {
    if (elapsed < 0.0) elapsed = 0.0;
    /* Exact dashboard accent words in XBGR byte order. */
    uint32_t blue = xm14_splash_rgb(31, 148, 255);
    uint32_t cyan = xm14_splash_rgb(208, 223, 54);
    uint32_t green = xm14_splash_rgb(114, 220, 40);
    uint32_t muted = xm14_splash_rgb(159, 148, 140);
    xm14_splash_background(canvas);

    const char *logo = "nwrt";
    int logo_scale = 46;
    int logo_width = xm14_splash_text_width(logo, logo_scale);
    xm14_splash_text(canvas, (canvas->width - logo_width) / 2,
                     260, logo, logo_scale, green, true);
    const char *tagline = "5G MOBILE WIFI SYSTEM";
    int tagline_scale = 10;
    int tagline_width = xm14_splash_text_width(tagline, tagline_scale);
    xm14_splash_text(canvas, (canvas->width - tagline_width) / 2,
                     685, tagline, tagline_scale, cyan, false);

    int sweep_x = (int)fmod(elapsed * 240.0, (double)(canvas->width + 360)) - 180;
    xm14_splash_fill(canvas, sweep_x, 850, 180, 3, blue);
    xm14_splash_fill(canvas, 100, 850, canvas->width - 200, 1,
                     xm14_splash_rgb(81, 75, 23));

    xm14_splash_router(canvas, elapsed, blue, cyan);
    xm14_splash_code_panel(canvas, elapsed, blue, cyan);
    xm14_splash_developer(canvas, muted);
}

#undef XM14_GLYPH

#endif

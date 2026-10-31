// SPDX-License-Identifier: GPL-2.0-only
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <limits.h>
#include <stdbool.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define RUN_DIR "/var/run/xm14display"
#define PAGE_FILE RUN_DIR "/page"
#define FOOTER_START_FILE RUN_DIR "/footer-start"
#define FOOTER_POSITION_FILE RUN_DIR "/footer-position"
#define FOOTER_DRAG_FILE RUN_DIR "/footer-dragging"
#define WAKE_FILE RUN_DIR "/wake"
#define OFF_FILE RUN_DIR "/off"
#define POWER_SLEEP_LOCK RUN_DIR "/power-sleep-lock"
#define REFRESH_FILE RUN_DIR "/refresh"
#define SMS_TO_FILE RUN_DIR "/sms-to"
#define SMS_TEXT_FILE RUN_DIR "/sms-text"
#define SMS_FOCUS_FILE RUN_DIR "/sms-focus"
#define SMS_RESULT_FILE RUN_DIR "/sms-result"
#define PHONE_NUMBER_FILE RUN_DIR "/phone-number"
#define PHONE_RESULT_FILE RUN_DIR "/phone-result"
#define BASE_PAGE_COUNT 10
#define PAGE_COUNT 12
#define SCREEN_WIDTH 1440
#define SCREEN_HEIGHT 3200
#define FOOTER_ITEM_WIDTH 288
#define FOOTER_VISIBLE_ITEMS 5

static bool has_abs_bit(const unsigned long *bits, unsigned int code) {
    const unsigned int width = sizeof(unsigned long) * 8;
    return (bits[code / width] & (1UL << (code % width))) != 0;
}

static int configured_page_count(void) {
    static time_t checked;
    static int count = BASE_PAGE_COUNT;
    time_t now = time(NULL);
    if (now == checked) return count;
    checked = now;
    FILE *pipe = popen("uci -q get xm14display.main.telephony_pages 2>/dev/null", "r");
    char value[16] = "0";
    if (pipe) {
        if (!fgets(value, sizeof(value), pipe)) value[0] = '0';
        pclose(pipe);
    }
    count = value[0] == '1' ? PAGE_COUNT : BASE_PAGE_COUNT;
    return count;
}

static int find_touch(char *path, size_t size, struct input_absinfo *xinfo,
                      struct input_absinfo *yinfo) {
    DIR *dir = opendir("/dev/input");
    if (!dir) return -1;
    struct dirent *entry;
    int result = -1;
    while ((entry = readdir(dir))) {
        if (strncmp(entry->d_name, "event", 5) != 0) continue;
        char candidate[PATH_MAX], name[128] = "";
        unsigned long bits[(ABS_MAX + sizeof(unsigned long) * 8) / (sizeof(unsigned long) * 8)] = {0};
        snprintf(candidate, sizeof(candidate), "/dev/input/%s", entry->d_name);
        int fd = open(candidate, O_RDONLY | O_CLOEXEC);
        if (fd < 0) continue;
        ioctl(fd, EVIOCGNAME(sizeof(name)), name);
        if (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(bits)), bits) >= 0 &&
            has_abs_bit(bits, ABS_MT_POSITION_X) && has_abs_bit(bits, ABS_MT_POSITION_Y) &&
            ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), xinfo) == 0 &&
            ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), yinfo) == 0) {
            snprintf(path, size, "%s", candidate);
            result = fd;
            fprintf(stderr, "xm14-touch: using %s (%s)\n", candidate, name);
            break;
        }
        close(fd);
    }
    closedir(dir);
    return result;
}

static void touch_file(const char *path) {
    int fd = open(path, O_WRONLY | O_CREAT | O_CLOEXEC, 0644);
    if (fd >= 0) { futimens(fd, NULL); close(fd); }
}

static bool suppress_power_sleep_touch(const struct input_event *event) {
    struct stat state;
    struct timespec now;
    static bool suppressed_contact;

    if (stat(POWER_SLEEP_LOCK, &state) != 0) {
        suppressed_contact = false;
        return false;
    }
    clock_gettime(CLOCK_REALTIME, &now);
    double age = (double)(now.tv_sec - state.st_mtim.tv_sec) +
                 (double)(now.tv_nsec - state.st_mtim.tv_nsec) /
                 1000000000.0;

    /* A power-key blank may arrive in the middle of a touch packet.  Never
     * let its remaining coordinates or release wake the panel again.  After
     * the guard interval, only a brand-new contact may release the lock. */
    if (event->type == EV_ABS && event->code == ABS_MT_TRACKING_ID) {
        if (event->value < 0) {
            suppressed_contact = false;
            return true;
        }
        if (age >= 1.2 && !suppressed_contact) {
            unlink(POWER_SLEEP_LOCK);
            return false;
        }
        suppressed_contact = true;
    }
    return true;
}

static bool read_touch_wake(void) {
    FILE *pipe = popen(
        "uci -q get xm14display.main.touch_wake 2>/dev/null", "r");
    char value[16] = "0";

    if (!pipe) return false;
    if (!fgets(value, sizeof(value), pipe)) value[0] = '0';
    pclose(pipe);
    return value[0] == '1';
}

static bool panel_is_lit(void) {
    FILE *file;
    int brightness = 0;
    static int always_on;
    static int timeout = 120;
    static time_t config_checked;
    struct stat wake;
    time_t now;

    if (access(OFF_FILE, F_OK) == 0) return false;
    file = fopen("/sys/class/backlight/panel0-backlight/brightness", "r");
    if (!file) return true;
    if (fscanf(file, "%d", &brightness) != 1) brightness = 0;
    fclose(file);
    if (brightness <= 0) return false;

    now = time(NULL);
    if (now != config_checked) {
        config_checked = now;
        file = popen("uci -q get xm14display.main.always_on 2>/dev/null", "r");
        if (file) {
            if (fscanf(file, "%d", &always_on) != 1) always_on = 0;
            pclose(file);
        }
        file = popen("uci -q get xm14display.main.screen_timeout 2>/dev/null", "r");
        if (file) {
            if (fscanf(file, "%d", &timeout) != 1 ||
                timeout < 5 || timeout > 3600)
                timeout = 120;
            pclose(file);
        }
    }
    if (always_on) return true;
    if (stat(WAKE_FILE, &wake) == 0 && now - wake.st_mtime >= timeout)
        return false;
    return true;
}

static bool suppress_disabled_touch_wake(const struct input_event *event) {
    static bool blocked_contact;
    static bool contact_known;

    if (event->type == EV_ABS && event->code == ABS_MT_TRACKING_ID) {
        if (event->value < 0) {
            bool blocked = blocked_contact;
            blocked_contact = false;
            contact_known = false;
            return blocked;
        }
        contact_known = true;
        blocked_contact = !panel_is_lit() && !read_touch_wake();
        return blocked_contact;
    }
    if (blocked_contact) return true;

    /* A coordinate without a fresh tracking ID can only be a remainder from
     * the contact which blanked the panel.  It must never wake the display. */
    if (!contact_known && !panel_is_lit()) return true;
    return false;
}

static int read_page(void) {
    FILE *file = fopen(PAGE_FILE, "re");
    int page = 0;
    if (file) { if (fscanf(file, "%d", &page) != 1) page = 0; fclose(file); }
    return page;
}

static void write_page(int page) {
    char temporary[PATH_MAX];
    int count = configured_page_count();
    snprintf(temporary, sizeof(temporary), "%s.%ld", PAGE_FILE, (long)getpid());
    FILE *file = fopen(temporary, "we");
    if (!file) return;
    page %= count;
    if (page < 0) page += count;
    fprintf(file, "%d\n", page);
    if (fclose(file) == 0) rename(temporary, PAGE_FILE); else unlink(temporary);
}

static int read_footer_start(void) {
    FILE *file = fopen(FOOTER_START_FILE, "re");
    int start = 0;
    if (file) {
        if (fscanf(file, "%d", &start) != 1) start = 0;
        fclose(file);
    }
    if (start < 0) start = 0;
    int count = configured_page_count();
    if (start > count - FOOTER_VISIBLE_ITEMS)
        start = count - FOOTER_VISIBLE_ITEMS;
    return start;
}

static void write_footer_start(int start) {
    char temporary[PATH_MAX];
    int count = configured_page_count();
    if (start < 0) start = 0;
    if (start > count - FOOTER_VISIBLE_ITEMS)
        start = count - FOOTER_VISIBLE_ITEMS;
    snprintf(temporary, sizeof(temporary), "%s.%ld", FOOTER_START_FILE,
             (long)getpid());
    FILE *file = fopen(temporary, "we");
    if (!file) return;
    fprintf(file, "%d\n", start);
    if (fclose(file) == 0)
        rename(temporary, FOOTER_START_FILE);
    else
        unlink(temporary);
}

static int clamp_footer_position(int position) {
    int maximum = (configured_page_count() - FOOTER_VISIBLE_ITEMS) * FOOTER_ITEM_WIDTH;
    if (position < 0) position = 0;
    if (position > maximum) position = maximum;
    return position;
}

static void write_footer_position(int position) {
    char temporary[PATH_MAX];
    position = clamp_footer_position(position);
    snprintf(temporary, sizeof(temporary), "%s.%ld", FOOTER_POSITION_FILE,
             (long)getpid());
    FILE *file = fopen(temporary, "we");
    if (!file) return;
    fprintf(file, "%d\n", position);
    if (fclose(file) == 0)
        rename(temporary, FOOTER_POSITION_FILE);
    else
        unlink(temporary);
}

static void settle_footer(int from, int target) {
    const int frames = 12;
    for (int frame = 1; frame <= frames; ++frame) {
        double progress = (double)frame / frames;
        double eased = progress * progress * (3.0 - 2.0 * progress);
        int position = from + (int)((target - from) * eased);
        write_footer_position(position);
        usleep(12000);
    }
    write_footer_position(target);
}

static void start_speedtest(void) {
    pid_t child = fork();
    if (child == 0) {
        execl("/usr/libexec/xm14pro-speedtest", "xm14pro-speedtest", "start", NULL);
        _exit(127);
    }
}

static bool feature_enabled(const char *feature) {
    char command[128], value[16] = "0";
    snprintf(command, sizeof(command), "uci -q get xm14peripherals.main.%s", feature);
    FILE *pipe = popen(command, "r");
    if (!pipe) return false;
    if (!fgets(value, sizeof(value), pipe)) value[0] = '0';
    pclose(pipe);
    return value[0] == '1';
}

static void set_peripheral(const char *command, const char *value) {
    pid_t child = fork();
    if (child == 0) {
        execl("/usr/libexec/xm14pro-peripherals/control", "control",
              command, value, NULL);
        _exit(127);
    }
}

static void toggle_nfc(void) {
    bool enabled = feature_enabled("nfc");
    pid_t child = fork();
    if (child == 0) {
        execl("/usr/libexec/xm14pro-peripherals/control", "control", "set",
              "nfc", enabled ? "0" : "1", NULL);
        _exit(127);
    }
}

static void set_nfc_mode(const char *mode) {
    set_peripheral("set-nfc-mode", mode);
}

static void send_infrared_key(const char *key) {
    set_peripheral("infrared-send-saved", key);
}

static bool selected_remote_is_ac(void) {
    char source_path[256], selected[96], source[32] = "";
    FILE *file = fopen("/etc/xm14pro-peripherals/infrared-selected", "r");
    if (!file) return false;
    if (!fgets(selected, sizeof(selected), file)) { fclose(file); return false; }
    fclose(file);
    selected[strcspn(selected, "\r\n")] = 0;
    if (!selected[0] || strspn(selected, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-") != strlen(selected))
        return false;
    snprintf(source_path, sizeof(source_path),
             "/etc/xm14pro-peripherals/remotes/%s.remote.source", selected);
    file = fopen(source_path, "r");
    if (!file) return false;
    if (!fgets(source, sizeof(source), file)) source[0] = 0;
    fclose(file);
    return !strncmp(source, "ac:", 3);
}

static void read_text_file(const char *path, char *value, size_t size) {
    FILE *file = fopen(path, "re");
    value[0] = 0;
    if (!file) return;
    if (!fgets(value, size, file)) value[0] = 0;
    fclose(file);
    value[strcspn(value, "\r\n")] = 0;
}

static void write_text_file(const char *path, const char *value) {
    char temporary[PATH_MAX];
    snprintf(temporary, sizeof(temporary), "%s.%ld", path, (long)getpid());
    FILE *file = fopen(temporary, "we");
    if (!file) return;
    fprintf(file, "%s\n", value ? value : "");
    if (fclose(file) == 0) rename(temporary, path); else unlink(temporary);
    touch_file(REFRESH_FILE);
}

static void edit_text(const char *path, size_t maximum, const char *append,
                      bool erase) {
    char value[512];
    read_text_file(path, value, sizeof(value));
    size_t length = strlen(value);
    if (erase) {
        if (length) value[length - 1] = 0;
    } else if (append && length + strlen(append) < maximum &&
               length + strlen(append) < sizeof(value)) {
        strncat(value, append, sizeof(value) - length - 1);
    }
    write_text_file(path, value);
}

static int modem_integer(const char *field) {
    char command[256], value[32] = "0";
    snprintf(command, sizeof(command),
             "/usr/libexec/xm14pro-modem/control status 2>/dev/null | jsonfilter -e '@.%s' 2>/dev/null",
             field);
    FILE *pipe = popen(command, "r");
    if (pipe) {
        if (!fgets(value, sizeof(value), pipe)) value[0] = '0';
        pclose(pipe);
    }
    if (!strncmp(value, "true", 4)) return 1;
    return atoi(value);
}

static void modem_string(const char *field, char *value, size_t size) {
    char command[256];
    value[0] = 0;
    snprintf(command, sizeof(command),
             "/usr/libexec/xm14pro-modem/control status 2>/dev/null | jsonfilter -e '@.%s' 2>/dev/null",
             field);
    FILE *pipe = popen(command, "r");
    if (!pipe) return;
    if (!fgets(value, size, pipe)) value[0] = 0;
    pclose(pipe);
    value[strcspn(value, "\r\n")] = 0;
}

static bool runtime_value(const char *field, char *value, size_t size) {
    FILE *file = fopen(RUN_DIR "/telephony-state", "re");
    char line[96];
    size_t field_length = strlen(field);
    value[0] = 0;
    if (!file) return false;
    while (fgets(line, sizeof(line), file)) {
        if (strncmp(line, field, field_length) || line[field_length] != '=') continue;
        snprintf(value, size, "%s", line + field_length + 1);
        value[strcspn(value, "\r\n")] = 0;
        fclose(file);
        return true;
    }
    fclose(file);
    return false;
}

static int runtime_integer(const char *field) {
    char value[32];
    if (!runtime_value(field, value, sizeof(value))) return modem_integer(field);
    if (!strcmp(value, "true")) return 1;
    return atoi(value);
}

static void run_modem(const char *result_path, const char *action,
                      const char *first, const char *second) {
    write_text_file(result_path, "正在处理…");
    pid_t child = fork();
    if (child != 0) return;
    int output = open(result_path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (output >= 0) {
        dup2(output, STDOUT_FILENO);
        dup2(output, STDERR_FILENO);
        close(output);
    }
    if (second)
        execl("/usr/libexec/xm14pro-modem/control", "control", action,
              first ? first : "", second, NULL);
    else if (first)
        execl("/usr/libexec/xm14pro-modem/control", "control", action,
              first, NULL);
    else
        execl("/usr/libexec/xm14pro-modem/control", "control", action, NULL);
    _exit(127);
}

static void handle_sms_touch(int x, int y) {
    char focus[16] = "number";
    read_text_file(SMS_FOCUS_FILE, focus, sizeof(focus));
    bool number = strcmp(focus, "message") != 0;
    if (y >= 300 && y <= 500) {
        write_text_file(SMS_FOCUS_FILE, "number");
        return;
    }
    if (y >= 530 && y <= 870) {
        write_text_file(SMS_FOCUS_FILE, "message");
        return;
    }

    const char *rows[] = {"1234567890", "qwertyuiop", "asdfghjkl", "zxcvbnm"};
    const int counts[] = {10, 10, 9, 7};
    const int ys[] = {1400, 1580, 1760, 1940};
    for (int row = 0; row < 4; ++row) {
        int count = counts[row], gap = 10;
        int width = row < 2 ? 125 : (row == 2 ? 132 : 150);
        int total = count * width + (count - 1) * gap;
        int start = (SCREEN_WIDTH - total) / 2;
        if (y < ys[row] || y > ys[row] + 140) continue;
        for (int col = 0; col < count; ++col) {
            int left = start + col * (width + gap);
            if (x < left || x > left + width) continue;
            char character[2] = {rows[row][col], 0};
            if (!number || row == 0)
                edit_text(number ? SMS_TO_FILE : SMS_TEXT_FILE,
                          number ? 40 : 240, character, false);
            return;
        }
    }
    if (y >= 2140 && y <= 2290) {
        if (x >= 60 && x <= 360) {
            edit_text(number ? SMS_TO_FILE : SMS_TEXT_FILE,
                      number ? 40 : 240, NULL, true);
        } else if (x >= 390 && x <= 850) {
            edit_text(number ? SMS_TO_FILE : SMS_TEXT_FILE,
                      number ? 40 : 240, number ? "+" : " ", false);
        } else if (x >= 880 && x <= 1380) {
            char target[48], body[256];
            read_text_file(SMS_TO_FILE, target, sizeof(target));
            read_text_file(SMS_TEXT_FILE, body, sizeof(body));
            if (!target[0] || !body[0])
                write_text_file(SMS_RESULT_FILE, "请先填写收件人和短信内容");
            else
                run_modem(SMS_RESULT_FILE, "sms-send", target, body);
        }
    }
}

static void handle_phone_touch(int x, int y) {
    int call_count = runtime_integer("active_call_count");
    int call_id = call_count > 0 ? runtime_integer("active_call_id") : 0;
    const char *keys[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "*", "0", "#"};
    for (int i = 0; i < 12; ++i) {
        int col = i % 3, row = i / 3;
        int left = 120 + col * 410, top = 640 + row * 245;
        if (x < left || x > left + 340 || y < top || y > top + 190) continue;
        if (call_id > 0) {
            char id[16];
            snprintf(id, sizeof(id), "%d", call_id);
            run_modem(PHONE_RESULT_FILE, "call-dtmf", id, keys[i]);
        } else {
            edit_text(PHONE_NUMBER_FILE, 40, keys[i], false);
        }
        return;
    }
    if (y >= 1665 && y <= 1830) {
        if (call_id > 0) {
            char id[16];
            char state[32];
            snprintf(id, sizeof(id), "%d", call_id);
            if (!runtime_value("active_call_state", state, sizeof(state)))
                modem_string("active_call_state", state, sizeof(state));
            bool incoming = !strcmp(state, "incoming") || !strcmp(state, "waiting");
            if (incoming && x <= 670)
                run_modem(PHONE_RESULT_FILE, "call-answer", id, NULL);
            else
                run_modem(PHONE_RESULT_FILE, "call-end", id, NULL);
        } else if (x >= 90 && x <= 1020) {
            char number[48];
            read_text_file(PHONE_NUMBER_FILE, number, sizeof(number));
            if (number[0]) run_modem(PHONE_RESULT_FILE, "call-dial", number, NULL);
            else write_text_file(PHONE_RESULT_FILE, "请先输入电话号码");
        } else if (x >= 1050) {
            edit_text(PHONE_NUMBER_FILE, 40, NULL, true);
        }
        return;
    }
    if (y >= 2040 && y <= 2190) {
        if (x >= 90 && x <= 480)
            run_modem(PHONE_RESULT_FILE, "call-audio-route", "earpiece", NULL);
        else if (x >= 525 && x <= 915)
            run_modem(PHONE_RESULT_FILE, "call-audio-route", "speaker", NULL);
        else if (x >= 960 && x <= 1350) {
            int muted = runtime_integer("voice_mic_muted");
            run_modem(PHONE_RESULT_FILE, "call-audio-mute", muted ? "0" : "1", NULL);
        }
        return;
    }
    if (y >= 2250 && y <= 2390 && (x <= 250 || x >= 1190)) {
        int volume = runtime_integer("voice_call_volume");
        volume += x <= 250 ? -10 : 10;
        if (volume < 0) volume = 0;
        if (volume > 100) volume = 100;
        char value[8];
        snprintf(value, sizeof(value), "%d", volume);
        run_modem(PHONE_RESULT_FILE, "call-audio-volume", value, NULL);
    }
}

int main(void) {
    signal(SIGCHLD, SIG_IGN);
    mkdir(RUN_DIR, 0755);
    for (;;) {
        char path[PATH_MAX];
        struct input_absinfo xinfo, yinfo;
        int fd = find_touch(path, sizeof(path), &xinfo, &yinfo);
        if (fd < 0) { sleep(3); continue; }
        struct input_event event;
        int start_x = -1, current_x = -1, start_y = -1, current_y = -1;
        int footer_origin = 0, footer_position = 0;
        bool footer_tracking = false;
        bool touching = false;
        while (read(fd, &event, sizeof(event)) == (ssize_t)sizeof(event)) {
            if (suppress_power_sleep_touch(&event)) {
                if (event.type == EV_ABS &&
                    event.code == ABS_MT_TRACKING_ID && event.value < 0) {
                    start_x = current_x = start_y = current_y = -1;
                    footer_tracking = false;
                    touching = false;
                    unlink(FOOTER_DRAG_FILE);
                }
                continue;
            }
            if (suppress_disabled_touch_wake(&event)) {
                if (event.type == EV_ABS &&
                    event.code == ABS_MT_TRACKING_ID && event.value < 0) {
                    start_x = current_x = start_y = current_y = -1;
                    footer_tracking = false;
                    touching = false;
                    unlink(FOOTER_DRAG_FILE);
                }
                continue;
            }
            if (event.type == EV_ABS && event.code == ABS_MT_POSITION_X) {
                current_x = event.value;
                if (start_x < 0) start_x = current_x;
                touching = true;
                unlink(OFF_FILE);
                touch_file(WAKE_FILE);
            } else if (event.type == EV_ABS && event.code == ABS_MT_POSITION_Y) {
                current_y = event.value;
                if (start_y < 0) start_y = current_y;
                touching = true;
                unlink(OFF_FILE);
                touch_file(WAKE_FILE);
            } else if (event.type == EV_SYN && event.code == SYN_REPORT &&
                       touching && start_x >= 0 && current_x >= 0 &&
                       start_y >= 0 && current_y >= 0) {
                int span = xinfo.maximum - xinfo.minimum;
                int yspan = yinfo.maximum - yinfo.minimum;
                bool began_in_footer = span > 0 && yspan > 0 &&
                    start_y >= yinfo.minimum + yspan * 89 / 100;
                if (began_in_footer) {
                    if (!footer_tracking) {
                        footer_origin = read_footer_start() * FOOTER_ITEM_WIDTH;
                        footer_position = footer_origin;
                        touch_file(FOOTER_DRAG_FILE);
                        write_footer_position(footer_position);
                        footer_tracking = true;
                    }
                    int raw_delta = current_x - start_x;
                    int pixel_delta = raw_delta * SCREEN_WIDTH / span;
                    int next_position = clamp_footer_position(footer_origin - pixel_delta);
                    if (next_position != footer_position) {
                        footer_position = next_position;
                        write_footer_position(footer_position);
                    }
                }
            } else if (event.type == EV_ABS && event.code == ABS_MT_TRACKING_ID && event.value < 0 && touching) {
                if (start_x < 0 || current_x < 0 || start_y < 0 || current_y < 0) {
                    start_x = current_x = start_y = current_y = -1;
                    touching = false;
                    continue;
                }
                int span = xinfo.maximum - xinfo.minimum;
                int yspan = yinfo.maximum - yinfo.minimum;
                int delta = current_x - start_x;
                int page = read_page();
                bool update_page = true;
                bool footer_gesture = span > 0 && yspan > 0 &&
                    start_y >= yinfo.minimum + yspan * 89 / 100 &&
                    current_y >= yinfo.minimum + yspan * 89 / 100;
                if (footer_gesture && footer_tracking &&
                    abs(delta) > abs(current_y - start_y)) {
                    int target_start = (footer_position + FOOTER_ITEM_WIDTH / 2) /
                                       FOOTER_ITEM_WIDTH;
                    if (target_start < 0) target_start = 0;
                    int count = configured_page_count();
                    if (target_start > count - FOOTER_VISIBLE_ITEMS)
                        target_start = count - FOOTER_VISIBLE_ITEMS;
                    int target_position = target_start * FOOTER_ITEM_WIDTH;
                    settle_footer(footer_position, target_position);
                    write_footer_start(target_start);
                    unlink(FOOTER_DRAG_FILE);
                    update_page = false;
                } else if (footer_gesture && current_x >= xinfo.minimum) {
                    int start = read_footer_start();
                    int slot = (current_x - xinfo.minimum) * 5 / (span + 1);
                    if (slot < 0) slot = 0;
                    if (slot > 4) slot = 4;
                    page = start + slot;
                    if (footer_tracking) {
                        write_footer_position(start * FOOTER_ITEM_WIDTH);
                        unlink(FOOTER_DRAG_FILE);
                    }
                } else if (span > 0 && abs(delta) > span / 8) {
                    page += delta < 0 ? 1 : -1;
                } else if (span > 0 && yspan > 0 &&
                           current_y < yinfo.minimum + yspan * 5 / 100 &&
                           current_x > xinfo.minimum + span * 78 / 100) {
                    page = 6;
                } else if (page == 2 && span > 0 && yspan > 0) {
                    int screen_x = (current_x - xinfo.minimum) * SCREEN_WIDTH / span;
                    int screen_y = (current_y - yinfo.minimum) * SCREEN_HEIGHT / yspan;
                    /* Only the explicit button below the dial starts a test.
                     * Entering the page, tapping the dial or reading telemetry
                     * must never consume data in the background. */
                    if (screen_x >= 320 && screen_x <= 1120 &&
                        screen_y >= 1390 && screen_y <= 1520)
                        start_speedtest();
                } else if (page == 7 && span > 0 && yspan > 0) {
                    int screen_x = (current_x - xinfo.minimum) * SCREEN_WIDTH / span;
                    int screen_y = (current_y - yinfo.minimum) * SCREEN_HEIGHT / yspan;
                    if (screen_x >= 1050 && screen_x <= 1310 &&
                        screen_y >= 650 && screen_y <= 734)
                        toggle_nfc();
                    else if (screen_x >= 1030 && screen_x <= 1330 &&
                             screen_y >= 1365 && screen_y <= 1455)
                        set_nfc_mode("share");
                    else if (screen_x >= 1030 && screen_x <= 1330 &&
                             screen_y >= 1815 && screen_y <= 1905)
                        set_nfc_mode("copy");
                    else if (screen_x >= 1030 && screen_x <= 1330 &&
                             screen_y >= 1950 && screen_y <= 2040)
                        set_peripheral("nfc-copy-reset", "");
                } else if (page == 8 && span > 0 && yspan > 0) {
                    int screen_x = (current_x - xinfo.minimum) * SCREEN_WIDTH / span;
                    int screen_y = (current_y - yinfo.minimum) * SCREEN_HEIGHT / yspan;
                    static const char *keys[] = {
                        "power", "up", "menu", "left", "ok", "right",
                        "vol-", "down", "vol+"
                    };
                    static const char *ac_actions[] = {
                        "power", "mode", "fan", "temp-", NULL, "temp+",
                        "swingv", "swingh", NULL
                    };
                    bool is_ac = selected_remote_is_ac();
                    for (int i = 0; i < 9; ++i) {
                        int col = i % 3, row = i / 3;
                        int x = 90 + col * 415, y = 1430 + row * 220;
                        if (screen_x >= x && screen_x <= x + 365 &&
                            screen_y >= y && screen_y <= y + 165) {
                            if (is_ac) {
                                if (ac_actions[i]) set_peripheral("infrared-ac-command", ac_actions[i]);
                            } else {
                                send_infrared_key(keys[i]);
                            }
                            break;
                        }
                    }
                } else if (page == 10 && span > 0 && yspan > 0 &&
                           configured_page_count() == PAGE_COUNT) {
                    int screen_x = (current_x - xinfo.minimum) * SCREEN_WIDTH / span;
                    int screen_y = (current_y - yinfo.minimum) * SCREEN_HEIGHT / yspan;
                    handle_sms_touch(screen_x, screen_y);
                } else if (page == 11 && span > 0 && yspan > 0 &&
                           configured_page_count() == PAGE_COUNT) {
                    int screen_x = (current_x - xinfo.minimum) * SCREEN_WIDTH / span;
                    int screen_y = (current_y - yinfo.minimum) * SCREEN_HEIGHT / yspan;
                    handle_phone_touch(screen_x, screen_y);
                } else if (page == 0 && yspan > 0) {
                    int relative_y = (current_y - yinfo.minimum) * 100 / yspan;
                    int relative_x = (current_x - xinfo.minimum) * 100 / span;
                    if (relative_y < 24) page = 1;
                    else if (relative_y < 50) page = 3;
                    else if (relative_y < 61) page = relative_x < 34 ? 5 :
                                                     relative_x < 65 ? 4 : 5;
                    else if (relative_y < 77) page = 4;
                    else page = 5;
                }
                if (update_page) write_page(page);
                if (footer_tracking) unlink(FOOTER_DRAG_FILE);
                touch_file(WAKE_FILE);
                start_x = current_x = start_y = current_y = -1;
                footer_tracking = false;
                touching = false;
            }
        }
        unlink(FOOTER_DRAG_FILE);
        close(fd);
        sleep(1);
    }
}


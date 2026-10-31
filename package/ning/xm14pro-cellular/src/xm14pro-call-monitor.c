// SPDX-License-Identifier: GPL-2.0-only
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define CALL_STATE "/usr/libexec/xm14pro-qwes/android-root/data/vendor/radio/ims-client/call-state"
#define CALL_ACTION "/var/run/xm14pro/call-local-action"
#define CALL_HISTORY "/var/lib/xm14pro/calls"
#define DISPLAY_RUN "/var/run/xm14display"
#define AUDIO_CONTROL "/usr/libexec/xm14pro-audio/control"

struct call_state {
	int count;
	int id;
	char state[24];
	char slot[16];
	char direction[16];
	char number[72];
	char type[16];
	time_t updated;
};

struct tracked_call {
	bool active;
	int id;
	char slot[16];
	char direction[16];
	char number[72];
	char type[16];
	time_t started;
	time_t answered;
};

static volatile sig_atomic_t running = 1;
static int haptic_fd = -1;
static int haptic_effect = -1;
static pid_t tone_child = -1;
static bool alert_visible;
static int saved_page;
static int saved_footer;

static void stop_handler(int signal_number)
{
	(void)signal_number;
	running = 0;
}

static void copy_text(char *destination, size_t size, const char *source)
{
	if (!size) return;
	snprintf(destination, size, "%s", source ? source : "");
}

static long long monotonic_millis(void)
{
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	return (long long)now.tv_sec * 1000LL + now.tv_nsec / 1000000LL;
}

static bool read_call_state(struct call_state *state)
{
	FILE *file = fopen(CALL_STATE, "re");
	char line[192];

	memset(state, 0, sizeof(*state));
	if (!file) return false;
	while (fgets(line, sizeof(line), file)) {
		char *separator = strchr(line, '=');
		char *value;
		if (!separator) continue;
		*separator = '\0';
		value = separator + 1;
		value[strcspn(value, "\r\n")] = '\0';
		if (!strcmp(line, "active_call_count")) state->count = atoi(value);
		else if (!strcmp(line, "active_call_id")) state->id = atoi(value);
		else if (!strcmp(line, "active_call_state"))
			copy_text(state->state, sizeof(state->state), value);
		else if (!strcmp(line, "active_call_slot"))
			copy_text(state->slot, sizeof(state->slot), value);
		else if (!strcmp(line, "active_call_direction"))
			copy_text(state->direction, sizeof(state->direction), value);
		else if (!strcmp(line, "active_call_number"))
			copy_text(state->number, sizeof(state->number), value);
		else if (!strcmp(line, "active_call_type"))
			copy_text(state->type, sizeof(state->type), value);
		else if (!strcmp(line, "updated_epoch"))
			state->updated = (time_t)strtoll(value, NULL, 10);
	}
	fclose(file);
	return true;
}

static int read_integer(const char *path, int fallback)
{
	FILE *file = fopen(path, "re");
	int value = fallback;
	if (file) {
		if (fscanf(file, "%d", &value) != 1) value = fallback;
		fclose(file);
	}
	return value;
}

static bool write_integer(const char *path, int value)
{
	char temporary[256];
	FILE *file;
	snprintf(temporary, sizeof(temporary), "%s.%ld", path, (long)getpid());
	file = fopen(temporary, "we");
	if (!file) return false;
	fprintf(file, "%d\n", value);
	if (fclose(file) || rename(temporary, path)) {
		unlink(temporary);
		return false;
	}
	return true;
}

static void touch_path(const char *path)
{
	int descriptor = open(path, O_WRONLY | O_CREAT | O_CLOEXEC, 0644);
	if (descriptor >= 0) close(descriptor);
	(void)utimensat(AT_FDCWD, path, NULL, 0);
}

static bool telephony_pages_enabled(void)
{
	FILE *file = fopen("/etc/config/xm14display", "re");
	char line[192];
	bool enabled = false;
	if (!file) return false;
	while (fgets(line, sizeof(line), file)) {
		char *option = strstr(line, "option telephony_pages");
		if (option && (strstr(option, "'1'") || strstr(option, "\"1\""))) {
			enabled = true;
			break;
		}
	}
	fclose(file);
	return enabled;
}

static void show_incoming_call(void)
{
	if (alert_visible || !telephony_pages_enabled()) return;
	mkdir(DISPLAY_RUN, 0755);
	saved_page = read_integer(DISPLAY_RUN "/page", 0);
	saved_footer = read_integer(DISPLAY_RUN "/footer-start", 0);
	write_integer(DISPLAY_RUN "/pre-call-page", saved_page);
	write_integer(DISPLAY_RUN "/page", 10);
	write_integer(DISPLAY_RUN "/footer-start", 6);
	write_integer(DISPLAY_RUN "/footer-position", 6 * 288);
	unlink(DISPLAY_RUN "/off");
	touch_path(DISPLAY_RUN "/wake");
	touch_path(DISPLAY_RUN "/refresh");
	alert_visible = true;
}

static void restore_display(void)
{
	if (!alert_visible) return;
	if (read_integer(DISPLAY_RUN "/page", 10) == 10)
		write_integer(DISPLAY_RUN "/page", saved_page);
	write_integer(DISPLAY_RUN "/footer-start", saved_footer);
	write_integer(DISPLAY_RUN "/footer-position", saved_footer * 288);
	touch_path(DISPLAY_RUN "/refresh");
	unlink(DISPLAY_RUN "/pre-call-page");
	alert_visible = false;
}

static int open_haptic(void)
{
	DIR *directory = opendir("/sys/class/input");
	struct dirent *entry;
	char name_path[256];
	char device_path[64];
	char name[96];
	if (!directory) return -1;
	while ((entry = readdir(directory))) {
		FILE *file;
		if (strncmp(entry->d_name, "event", 5)) continue;
		snprintf(name_path, sizeof(name_path), "/sys/class/input/%s/device/name",
			entry->d_name);
		file = fopen(name_path, "re");
		if (!file) continue;
		name[0] = '\0';
		(void)fgets(name, sizeof(name), file);
		fclose(file);
		if (!strstr(name, "haptic")) continue;
		snprintf(device_path, sizeof(device_path), "/dev/input/%s",
			entry->d_name);
		haptic_fd = open(device_path, O_RDWR | O_CLOEXEC);
		if (haptic_fd >= 0) break;
	}
	closedir(directory);
	return haptic_fd;
}

static void haptic_pulse(void)
{
	struct ff_effect effect;
	struct input_event event;
	if (haptic_fd < 0 && open_haptic() < 0) return;
	memset(&effect, 0, sizeof(effect));
	effect.type = FF_RUMBLE;
	effect.id = haptic_effect;
	effect.u.rumble.strong_magnitude = 0x5800;
	effect.u.rumble.weak_magnitude = 0x2800;
	effect.replay.length = 450;
	if (ioctl(haptic_fd, EVIOCSFF, &effect) < 0) return;
	haptic_effect = effect.id;
	memset(&event, 0, sizeof(event));
	event.type = EV_FF;
	event.code = (uint16_t)haptic_effect;
	event.value = 1;
	(void)write(haptic_fd, &event, sizeof(event));
}

static void stop_haptic(void)
{
	if (haptic_fd < 0) return;
	if (haptic_effect >= 0) {
		struct input_event event;
		memset(&event, 0, sizeof(event));
		event.type = EV_FF;
		event.code = (uint16_t)haptic_effect;
		event.value = 0;
		(void)write(haptic_fd, &event, sizeof(event));
		(void)ioctl(haptic_fd, EVIOCRMFF, haptic_effect);
	}
	close(haptic_fd);
	haptic_fd = -1;
	haptic_effect = -1;
}

static void reap_tone(void)
{
	if (tone_child > 0 && waitpid(tone_child, NULL, WNOHANG) == tone_child)
		tone_child = -1;
}

static void play_ring_pulse(void)
{
	reap_tone();
	if (tone_child > 0 || access(AUDIO_CONTROL, X_OK)) return;
	tone_child = fork();
	if (tone_child == 0) {
		execl(AUDIO_CONTROL, AUDIO_CONTROL, "tone", "speaker", "18", "650",
			(char *)NULL);
		_exit(127);
	}
	if (tone_child < 0) tone_child = -1;
}

static void json_string(FILE *file, const char *value)
{
	const unsigned char *cursor = (const unsigned char *)(value ? value : "");
	fputc('"', file);
	for (; *cursor; ++cursor) {
		if (*cursor == '"' || *cursor == '\\') fputc('\\', file);
		if (*cursor >= 0x20) fputc(*cursor, file);
	}
	fputc('"', file);
}

static void local_action(int call_id, time_t started, char *action, size_t size)
{
	FILE *file = fopen(CALL_ACTION, "re");
	char line[96];
	int stored_id = 0;
	time_t epoch = 0;
	action[0] = '\0';
	if (!file) return;
	while (fgets(line, sizeof(line), file)) {
		char *separator = strchr(line, '=');
		char *value;
		if (!separator) continue;
		*separator = '\0';
		value = separator + 1;
		value[strcspn(value, "\r\n")] = '\0';
		if (!strcmp(line, "call_id")) stored_id = atoi(value);
		else if (!strcmp(line, "action")) copy_text(action, size, value);
		else if (!strcmp(line, "epoch")) epoch = (time_t)strtoll(value, NULL, 10);
	}
	fclose(file);
	if (stored_id != call_id || epoch + 2 < started) action[0] = '\0';
}

static void finish_call(struct tracked_call *call)
{
	char path[320];
	char temporary[336];
	char action[24];
	const char *status;
	time_t ended = time(NULL);
	time_t duration = call->answered ? ended - call->answered : 0;
	FILE *file;
	if (!call->active) return;
	if (duration < 0) duration = 0;
	local_action(call->id, call->started, action, sizeof(action));
	if (!strcmp(call->direction, "incoming")) {
		if (call->answered) status = "completed";
		else if (!strcmp(action, "rejected")) status = "rejected";
		else status = "missed";
	} else {
		if (call->answered) status = "completed";
		else if (!strcmp(action, "cancelled")) status = "cancelled";
		else status = "failed";
	}
	mkdir("/var/lib/xm14pro", 0755);
	mkdir(CALL_HISTORY, 0755);
	snprintf(path, sizeof(path), "%s/%lld-%lld-%s-%d.json", CALL_HISTORY,
		(long long)call->started, (long long)ended,
		call->slot[0] ? call->slot : "slot", call->id);
	snprintf(temporary, sizeof(temporary), "%s.tmp", path);
	file = fopen(temporary, "we");
	if (file) {
		fputs("{\"id\":", file);
		char identifier[96];
		snprintf(identifier, sizeof(identifier), "%lld-%s-%d",
			(long long)call->started, call->slot, call->id);
		json_string(file, identifier);
		fprintf(file, ",\"call_id\":%d,\"number\":", call->id);
		json_string(file, call->number);
		fputs(",\"direction\":", file); json_string(file, call->direction);
		fputs(",\"slot\":", file); json_string(file, call->slot);
		fputs(",\"type\":", file); json_string(file, call->type);
		fprintf(file,
			",\"started_epoch\":%lld,\"answered_epoch\":%lld,"
			"\"ended_epoch\":%lld,\"duration_seconds\":%lld,\"status\":",
			(long long)call->started, (long long)call->answered,
			(long long)ended, (long long)duration);
		json_string(file, status);
		fputs("}\n", file);
		if (fclose(file) || rename(temporary, path)) unlink(temporary);
	}
	unlink(CALL_ACTION);
	memset(call, 0, sizeof(*call));
}

static bool is_incoming_alert(const struct call_state *state)
{
	return state->count > 0 && state->id > 0 &&
		(!strcmp(state->state, "incoming") || !strcmp(state->state, "waiting"));
}

int main(void)
{
	struct tracked_call call = {0};
	long long next_ring = 0;
	bool was_alerting = false;

	signal(SIGINT, stop_handler);
	signal(SIGTERM, stop_handler);
	mkdir("/var/lib/xm14pro", 0755);
	mkdir(CALL_HISTORY, 0755);
	while (running) {
		struct call_state state;
		bool active = read_call_state(&state) && state.count > 0 && state.id > 0;
		if (active) {
			if (!call.active || call.id != state.id ||
			    strcmp(call.slot, state.slot)) {
				finish_call(&call);
				call.active = true;
				call.id = state.id;
				call.started = state.updated > 0 ? state.updated : time(NULL);
			}
			if (state.slot[0]) copy_text(call.slot, sizeof(call.slot), state.slot);
			if (state.direction[0])
				copy_text(call.direction, sizeof(call.direction), state.direction);
			if (state.number[0]) copy_text(call.number, sizeof(call.number), state.number);
			if (state.type[0]) copy_text(call.type, sizeof(call.type), state.type);
			if ((!strcmp(state.state, "conversation") ||
			     !strcmp(state.state, "hold")) && !call.answered)
				call.answered = state.updated > 0 ? state.updated : time(NULL);
		} else {
			finish_call(&call);
		}

		bool alerting = is_incoming_alert(&state);
		if (alerting) {
			long long now = monotonic_millis();
			show_incoming_call();
			if (!was_alerting || now >= next_ring) {
				play_ring_pulse();
				haptic_pulse();
				next_ring = now + 1900;
			}
		} else if (was_alerting) {
			stop_haptic();
			if (!active) restore_display();
		}
		if (!active && alert_visible) restore_display();
		was_alerting = alerting;
		reap_tone();
		usleep(200000);
	}
	finish_call(&call);
	stop_haptic();
	restore_display();
	if (tone_child > 0) (void)waitpid(tone_child, NULL, 0);
	return 0;
}

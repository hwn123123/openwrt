// SPDX-License-Identifier: GPL-2.0-only
#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <linux/input.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define MAX_INPUTS 6
#define DISPLAY_RUN "/var/run/xm14display"
#define AUDIO_RUN "/var/run/xm14audio"
#define OVERLAY_FILE DISPLAY_RUN "/volume-overlay"
#define SCREEN_OFF_FILE DISPLAY_RUN "/off"
#define POWER_SLEEP_LOCK DISPLAY_RUN "/power-sleep-lock"
#define CALL_ACTIVE_FILE "/var/run/xm14telephony/call-active"

struct key_input {
	int fd;
	char name[80];
};

static volatile sig_atomic_t running = 1;
static int media_volume = 60;
static int call_volume = 50;
static int volume_step = 5;
static unsigned long long overlay_sequence;
static bool volume_up_held;
static bool volume_down_held;
static struct timespec volume_up_since;
static struct timespec volume_down_since;
static struct timespec volume_up_repeat;
static struct timespec volume_down_repeat;
static struct timespec power_down;
static struct timespec last_power_action;
static struct timespec last_wake_press;
static bool power_held;
static int wake_press_count;
static bool persist_pending;
static struct timespec persist_after;
static bool power_button_enabled = true;
static bool volume_overlay_enabled = true;
static int power_wake_presses = 2;
static int power_wake_window_ms = 1000;

static void stop_handler(int signal_number)
{
	(void)signal_number;
	running = 0;
}

static double seconds_between(const struct timespec *newer,
	const struct timespec *older)
{
	return (double)(newer->tv_sec - older->tv_sec) +
		(double)(newer->tv_nsec - older->tv_nsec) / 1000000000.0;
}

static int read_uci_integer(const char *option, int fallback, int minimum,
	int maximum)
{
	char command[160];
	char buffer[32];
	FILE *pipe;
	char *end = NULL;
	long value;

	snprintf(command, sizeof(command),
		"uci -q get xm14audio.main.%s 2>/dev/null", option);
	pipe = popen(command, "r");
	if (!pipe)
		return fallback;
	if (!fgets(buffer, sizeof(buffer), pipe)) {
		pclose(pipe);
		return fallback;
	}
	pclose(pipe);
	errno = 0;
	value = strtol(buffer, &end, 10);
	if (errno || end == buffer || value < minimum || value > maximum)
		return fallback;
	return (int)value;
}

static bool read_display_boolean(const char *option, bool fallback)
{
	char command[192];
	char buffer[16];
	FILE *pipe;

	snprintf(command, sizeof(command),
		"uci -q get xm14display.main.%s 2>/dev/null", option);
	pipe = popen(command, "r");
	if (!pipe)
		return fallback;
	if (!fgets(buffer, sizeof(buffer), pipe)) {
		pclose(pipe);
		return fallback;
	}
	pclose(pipe);
	return buffer[0] == '1';
}

static int read_display_integer(const char *option, int fallback, int minimum,
	int maximum)
{
	char command[192];
	char buffer[32];
	FILE *pipe;
	char *end = NULL;
	long value;

	snprintf(command, sizeof(command),
		"uci -q get xm14display.main.%s 2>/dev/null", option);
	pipe = popen(command, "r");
	if (!pipe)
		return fallback;
	if (!fgets(buffer, sizeof(buffer), pipe)) {
		pclose(pipe);
		return fallback;
	}
	pclose(pipe);
	errno = 0;
	value = strtol(buffer, &end, 10);
	if (errno || end == buffer || value < minimum || value > maximum)
		return fallback;
	return (int)value;
}

static void run_detached(const char *program, const char *first,
	const char *second, const char *third)
{
	pid_t child = fork();

	if (child != 0)
		return;
	setsid();
	int null_fd = open("/dev/null", O_RDWR | O_CLOEXEC);
	if (null_fd >= 0) {
		dup2(null_fd, STDIN_FILENO);
		dup2(null_fd, STDOUT_FILENO);
		dup2(null_fd, STDERR_FILENO);
		if (null_fd > STDERR_FILENO)
			close(null_fd);
	}
	if (third)
		execl(program, program, first, second, third, (char *)NULL);
	else if (second)
		execl(program, program, first, second, (char *)NULL);
	else if (first)
		execl(program, program, first, (char *)NULL);
	else
		execl(program, program, (char *)NULL);
	_exit(127);
}

static bool call_is_active(void)
{
	return access(CALL_ACTIVE_FILE, F_OK) == 0 ||
		access("/var/run/xm14pro/voice-active", F_OK) == 0;
}

static bool screen_is_lit(void)
{
	FILE *file = fopen("/sys/class/backlight/panel0-backlight/brightness", "r");
	int brightness = 0;

	if (!file)
		return access(SCREEN_OFF_FILE, F_OK) != 0;
	if (fscanf(file, "%d", &brightness) != 1)
		brightness = 0;
	fclose(file);
	return brightness > 0 && access(SCREEN_OFF_FILE, F_OK) != 0;
}

static void publish_overlay(const char *context, int volume)
{
	char temporary[160];
	FILE *file;

	if (!volume_overlay_enabled || !screen_is_lit())
		return;
	mkdir(DISPLAY_RUN, 0755);
	snprintf(temporary, sizeof(temporary), "%s.tmp.%ld", OVERLAY_FILE,
		(long)getpid());
	file = fopen(temporary, "w");
	if (!file)
		return;
	fprintf(file, "%llu %s %d\n", ++overlay_sequence, context, volume);
	if (fclose(file) == 0)
		rename(temporary, OVERLAY_FILE);
	else
		unlink(temporary);
}

static void send_volume(const char *context, int volume)
{
	char value[16];
	char state_path[128];
	char temporary[160];
	FILE *file;

	mkdir(AUDIO_RUN, 0755);
	snprintf(state_path, sizeof(state_path), "%s/%s-volume", AUDIO_RUN,
		context);
	snprintf(temporary, sizeof(temporary), "%s.tmp.%ld", state_path,
		(long)getpid());
	file = fopen(temporary, "w");
	if (file) {
		fprintf(file, "%d\n", volume);
		if (fclose(file) == 0)
			rename(temporary, state_path);
		else
			unlink(temporary);
	}
	snprintf(value, sizeof(value), "%d", volume);
	run_detached("/usr/libexec/xm14pro-audio/control", "volume", context,
		value);
	publish_overlay(context, volume);
	clock_gettime(CLOCK_MONOTONIC, &persist_after);
	persist_pending = true;
}

static void initialize_audio_volume(const char *context, int volume)
{
	char value[16];

	snprintf(value, sizeof(value), "%d", volume);
	run_detached("/usr/libexec/xm14pro-audio/control", "volume", context,
		value);
}

static void adjust_volume(int direction)
{
	bool call = call_is_active();
	int *value = call ? &call_volume : &media_volume;

	*value += direction * volume_step;
	if (*value < 0)
		*value = 0;
	if (*value > 100)
		*value = 100;
	send_volume(call ? "call" : "media", *value);
}

static void persist_volumes(void)
{
	char media[64];
	char call[64];
	pid_t child;

	snprintf(media, sizeof(media), "xm14audio.main.media_volume=%d",
		media_volume);
	snprintf(call, sizeof(call), "xm14audio.main.call_volume=%d", call_volume);
	child = fork();
	if (child == 0) {
		execl("/sbin/uci", "uci", "-q", "set", media, (char *)NULL);
		_exit(127);
	}
	if (child > 0)
		waitpid(child, NULL, 0);
	child = fork();
	if (child == 0) {
		execl("/sbin/uci", "uci", "-q", "set", call, (char *)NULL);
		_exit(127);
	}
	if (child > 0)
		waitpid(child, NULL, 0);
	child = fork();
	if (child == 0) {
		execl("/sbin/uci", "uci", "-q", "commit", "xm14audio",
			(char *)NULL);
		_exit(127);
	}
	if (child > 0)
		waitpid(child, NULL, 0);
	persist_pending = false;
}

static void toggle_screen(void)
{
	if (!screen_is_lit()) {
		unlink(POWER_SLEEP_LOCK);
		run_detached("/usr/libexec/xm14pro-display", "wake", NULL, NULL);
	} else {
		FILE *lock = fopen(POWER_SLEEP_LOCK, "w");
		if (lock)
			fclose(lock);
		run_detached("/usr/libexec/xm14pro-display", "sleep", NULL, NULL);
	}
}

static void reset_wake_sequence(void)
{
	wake_press_count = 0;
	memset(&last_wake_press, 0, sizeof(last_wake_press));
}

static void expire_wake_sequence(const struct timespec *now)
{
	double elapsed;

	if (wake_press_count <= 0 || !last_wake_press.tv_sec)
		return;
	elapsed = seconds_between(now, &last_wake_press);
	if (elapsed < 0.0 ||
	    elapsed * 1000.0 >= (double)power_wake_window_ms)
		reset_wake_sequence();
}

static void handle_power_short_press(const struct timespec *now)
{
	double since_last;

	if (screen_is_lit()) {
		reset_wake_sequence();
		if (!last_power_action.tv_sec ||
		    seconds_between(now, &last_power_action) >= 0.25) {
			last_power_action = *now;
			toggle_screen();
		}
		return;
	}

	power_wake_presses = read_display_integer("power_wake_presses", 2, 1, 4);
	power_wake_window_ms = read_display_integer("power_wake_window_ms", 1000,
		300, 2000);
	expire_wake_sequence(now);
	if (power_wake_presses <= 1) {
		reset_wake_sequence();
		last_power_action = *now;
		toggle_screen();
		return;
	}

	since_last = last_wake_press.tv_sec ?
		seconds_between(now, &last_wake_press) : 0.0;
	if (!last_wake_press.tv_sec ||
	    since_last * 1000.0 >= (double)power_wake_window_ms)
		wake_press_count = 1;
	else if (since_last >= 0.12)
		wake_press_count++;
	else
		return;
	last_wake_press = *now;

	if (wake_press_count >= power_wake_presses) {
		reset_wake_sequence();
		last_power_action = *now;
		toggle_screen();
	}
}

static bool wanted_input_name(const char *name)
{
	return strcmp(name, "gpio-keys") == 0 ||
		strcmp(name, "pmic_pwrkey") == 0 ||
		strcmp(name, "pmic_resin") == 0;
}

static int open_inputs(struct key_input *inputs)
{
	glob_t paths;
	int count = 0;

	if (glob("/dev/input/event*", 0, NULL, &paths) != 0)
		return 0;
	for (size_t index = 0; index < paths.gl_pathc && count < MAX_INPUTS;
	     index++) {
		char name[80] = "";
		int fd = open(paths.gl_pathv[index], O_RDONLY | O_NONBLOCK |
			O_CLOEXEC);
		if (fd < 0)
			continue;
		if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) < 0 ||
		    !wanted_input_name(name)) {
			close(fd);
			continue;
		}
		inputs[count].fd = fd;
		strncpy(inputs[count].name, name,
			sizeof(inputs[count].name) - 1);
		count++;
	}
	globfree(&paths);
	return count;
}

static void handle_key(unsigned int code, int value,
	const struct timespec *now)
{
	if (code == KEY_VOLUMEUP || code == KEY_VOLUMEDOWN) {
		bool *held = code == KEY_VOLUMEUP ? &volume_up_held :
			&volume_down_held;
		struct timespec *since = code == KEY_VOLUMEUP ? &volume_up_since :
			&volume_down_since;
		struct timespec *repeat = code == KEY_VOLUMEUP ? &volume_up_repeat :
			&volume_down_repeat;
		if (value == 1) {
			volume_overlay_enabled = read_display_boolean(
				"volume_overlay", true);
			*held = true;
			*since = *now;
			*repeat = *now;
			adjust_volume(code == KEY_VOLUMEUP ? 1 : -1);
		} else if (value == 0) {
			*held = false;
		}
		return;
	}
	if (code != KEY_POWER)
		return;
	if (value == 1) {
		power_button_enabled = read_display_boolean("power_button", true);
		if (!power_button_enabled) {
			power_held = false;
			return;
		}
		power_held = true;
		power_down = *now;
	} else if (value == 0 && power_held) {
		power_held = false;
		if (seconds_between(now, &power_down) < 1.2)
			handle_power_short_press(now);
	}
}

static void synthesize_repeats(const struct timespec *now)
{
	if (volume_up_held &&
	    seconds_between(now, &volume_up_since) >= 0.36 &&
	    seconds_between(now, &volume_up_repeat) >= 0.09) {
		volume_up_repeat = *now;
		adjust_volume(1);
	}
	if (volume_down_held &&
	    seconds_between(now, &volume_down_since) >= 0.36 &&
	    seconds_between(now, &volume_down_repeat) >= 0.09) {
		volume_down_repeat = *now;
		adjust_volume(-1);
	}
}

int main(void)
{
	struct key_input inputs[MAX_INPUTS];
	struct pollfd poll_fds[MAX_INPUTS];
	int count;

	signal(SIGINT, stop_handler);
	signal(SIGTERM, stop_handler);
	media_volume = read_uci_integer("media_volume", 60, 0, 100);
	call_volume = read_uci_integer("call_volume", 50, 0, 100);
	volume_step = read_uci_integer("volume_step", 5, 1, 20);
	power_button_enabled = read_display_boolean("power_button", true);
	volume_overlay_enabled = read_display_boolean("volume_overlay", true);
	power_wake_presses = read_display_integer("power_wake_presses", 2, 1, 4);
	power_wake_window_ms = read_display_integer("power_wake_window_ms", 1000,
		300, 2000);
	mkdir(DISPLAY_RUN, 0755);
	mkdir(AUDIO_RUN, 0755);
	initialize_audio_volume("media", media_volume);
	initialize_audio_volume("call", call_volume);

	while (running) {
		memset(inputs, 0, sizeof(inputs));
		count = open_inputs(inputs);
		if (!count) {
			sleep(1);
			continue;
		}
		for (int index = 0; index < count; index++) {
			poll_fds[index].fd = inputs[index].fd;
			poll_fds[index].events = POLLIN;
		}
		while (running) {
			int result = poll(poll_fds, (nfds_t)count, 40);
			struct timespec now;
			while (waitpid(-1, NULL, WNOHANG) > 0)
				;
			clock_gettime(CLOCK_MONOTONIC, &now);
			expire_wake_sequence(&now);
			if (result < 0 && errno != EINTR)
				break;
			for (int index = 0; index < count; index++) {
				if (!(poll_fds[index].revents & POLLIN))
					continue;
				for (;;) {
					struct input_event event;
					ssize_t length = read(inputs[index].fd, &event,
						sizeof(event));
					if (length != (ssize_t)sizeof(event))
						break;
					if (event.type == EV_KEY && event.value != 2)
						handle_key(event.code, event.value, &now);
				}
			}
			synthesize_repeats(&now);
			if (persist_pending &&
			    seconds_between(&now, &persist_after) >= 2.0)
				persist_volumes();
			bool reopen = false;
			for (int index = 0; index < count; index++)
				if (poll_fds[index].revents & (POLLERR | POLLHUP | POLLNVAL))
					reopen = true;
			if (reopen)
				break;
		}
		for (int index = 0; index < count; index++)
			close(inputs[index].fd);
	}
	return 0;
}

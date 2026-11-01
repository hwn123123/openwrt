// SPDX-License-Identifier: GPL-2.0-only
#define _GNU_SOURCE

#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#define LOGDW_PATH "/dev/socket/logdw"
#define ANDROID_LOG_HEADER_SIZE 11
#define ANDROID_LOG_INFO 4
#define ANDROID_LOG_WARN 5
#define SUMMARY_INTERVAL_SEC 60

static char previous[8192];
static unsigned int repeated;
static time_t last_report;

static void report_repeated(void)
{
	if (!repeated)
		return;
	fprintf(stdout, "android-log: previous warning repeated %u times\n", repeated);
	fflush(stdout);
	repeated = 0;
}

static void print_record(const unsigned char *buf, ssize_t length)
{
	const char *tag;
	const char *message;
	size_t payload_length;
	size_t tag_length;
	size_t message_length;
	char current[sizeof(previous)];
	time_t now;
	int priority;

	if (length <= ANDROID_LOG_HEADER_SIZE + 2)
		return;

	/* Writer packet: 11-byte header, priority, NUL tag, NUL message. */
	priority = buf[ANDROID_LOG_HEADER_SIZE];
	buf += ANDROID_LOG_HEADER_SIZE + 1;
	payload_length = (size_t)length - ANDROID_LOG_HEADER_SIZE - 1;
	tag = (const char *)buf;
	tag_length = strnlen(tag, payload_length);
	if (tag_length == payload_length)
		return;
	message = tag + tag_length + 1;
	message_length = strnlen(message, payload_length - tag_length - 1);
	while (message_length &&
	       (message[message_length - 1] == '\n' ||
		message[message_length - 1] == '\r'))
		message_length--;

	/* Android marks missing service waits as INFO. Keep those and all WARN+
	 * records while discarding normal INFO/DEBUG traffic. */
	if (priority < ANDROID_LOG_WARN &&
	    !(priority == ANDROID_LOG_INFO &&
	      memmem(message, message_length,
		     "Waited one second for vendor.qti.data.nwmgr.INwMgr",
		     sizeof("Waited one second for vendor.qti.data.nwmgr.INwMgr") - 1)))
		return;

	if (snprintf(current, sizeof(current), "android-log: %s: %.*s", tag,
		     (int)message_length, message) >= (int)sizeof(current))
		return;
	now = time(NULL);
	if (strcmp(previous, current) == 0) {
		repeated++;
		if (now - last_report >= SUMMARY_INTERVAL_SEC) {
			report_repeated();
			last_report = now;
		}
		return;
	}
	report_repeated();
	strcpy(previous, current);
	last_report = now;
	puts(current);
	fflush(stdout);
}

int main(void)
{
	struct sockaddr_un addr = { .sun_family = AF_UNIX };
	unsigned char buffer[8192];
	int fd;
	ssize_t length;

	(void)signal(SIGPIPE, SIG_IGN);
	fd = socket(AF_UNIX, SOCK_DGRAM, 0);
	if (fd < 0) {
		perror("logdw socket");
		return 2;
	}
	strncpy(addr.sun_path, LOGDW_PATH, sizeof(addr.sun_path) - 1);
	unlink(LOGDW_PATH);
	if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("bind " LOGDW_PATH);
		return 2;
	}
	if (chmod(LOGDW_PATH, 0666) < 0) {
		perror("chmod " LOGDW_PATH);
		return 2;
	}

	for (;;) {
		length = recv(fd, buffer, sizeof(buffer), 0);
		if (length < 0) {
			if (errno == EINTR)
				continue;
			perror("recv logdw");
			return 2;
		}
		print_record(buffer, length);
	}
}

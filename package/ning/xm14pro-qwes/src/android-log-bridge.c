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
#include <unistd.h>

#define LOGDW_PATH "/dev/socket/logdw"
#define ANDROID_LOG_HEADER_SIZE 11

static void print_record(const unsigned char *buf, ssize_t length)
{
	const char *tag;
	const char *message;
	size_t payload_length;
	size_t tag_length;

	if (length <= ANDROID_LOG_HEADER_SIZE + 2)
		return;

	/* Writer packet: 11-byte header, priority, NUL tag, NUL message. */
	buf += ANDROID_LOG_HEADER_SIZE + 1;
	payload_length = (size_t)length - ANDROID_LOG_HEADER_SIZE - 1;
	tag = (const char *)buf;
	tag_length = strnlen(tag, payload_length);
	if (tag_length == payload_length)
		return;
	message = tag + tag_length + 1;
	fprintf(stdout, "android-log: %s: %.*s\n", tag,
		(int)(payload_length - tag_length - 1), message);
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

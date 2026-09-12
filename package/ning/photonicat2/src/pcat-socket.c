/* SPDX-License-Identifier: GPL-3.0-only */
/* Small, dependency-free client for pcat-manager's NUL-framed JSON socket. */
#include <errno.h>
#include <poll.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define PCAT_SOCKET "/tmp/pcat-manager.sock"
#define MAX_REPLY (128 * 1024)

static int write_all(int fd, const char *buf, size_t len)
{
	while (len > 0) {
		ssize_t n = write(fd, buf, len);
		if (n < 0 && errno == EINTR)
			continue;
		if (n <= 0)
			return -1;
		buf += n;
		len -= (size_t)n;
	}
	return 0;
}

int main(int argc, char **argv)
{
	struct sockaddr_un addr;
	struct pollfd pfd;
	char *reply;
	size_t used = 0, request_len;
	int fd, rc = 1;

	if (argc != 2) {
		fprintf(stderr, "usage: %s JSON\n", argv[0]);
		return 2;
	}

	request_len = strlen(argv[1]);
	if (request_len == 0 || request_len > 65535) {
		fprintf(stderr, "invalid request length\n");
		return 2;
	}

	fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0) {
		perror("socket");
		return 1;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, PCAT_SOCKET, sizeof(addr.sun_path) - 1);
	if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("connect");
		goto out;
	}

	if (write_all(fd, argv[1], request_len) < 0 || write_all(fd, "\0", 1) < 0) {
		perror("write");
		goto out;
	}

	reply = malloc(MAX_REPLY + 1);
	if (!reply) {
		fprintf(stderr, "out of memory\n");
		goto out;
	}

	while (used < MAX_REPLY) {
		ssize_t n;
		char *end;

		pfd.fd = fd;
		pfd.events = POLLIN;
		pfd.revents = 0;
		do {
			n = poll(&pfd, 1, 1500);
		} while (n < 0 && errno == EINTR);
		if (n <= 0) {
			fprintf(stderr, "pcat-manager response timeout\n");
			break;
		}

		n = read(fd, reply + used, MAX_REPLY - used);
		if (n < 0 && errno == EINTR)
			continue;
		if (n <= 0)
			break;
		end = memchr(reply + used, '\0', (size_t)n);
		used += (size_t)n;
		if (end) {
			used = (size_t)(end - reply);
			reply[used] = '\0';
			fwrite(reply, 1, used, stdout);
			fputc('\n', stdout);
			rc = 0;
			break;
		}
	}

	if (used == MAX_REPLY)
		fprintf(stderr, "pcat-manager response too large\n");
	free(reply);
out:
	close(fd);
	return rc;
}

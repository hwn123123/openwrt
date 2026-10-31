// SPDX-License-Identifier: GPL-2.0-only
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define CONTROL_SOCKET "/dev/socket/xm14pro_audio_control"

int main(int argc, char **argv)
{
	struct sockaddr_un address = { .sun_family = AF_UNIX };
	char request[256] = "";
	char response[512];
	size_t used = 0;
	int socket_fd;
	int index;
	ssize_t length;

	if (argc < 2) {
		fprintf(stderr, "usage: %s status | volume media|call 0..100 | "
			"tone speaker|earpiece [volume] [duration_ms] | "
			"call-start earpiece|speaker [1|2] | call-stop | "
			"call-route earpiece|speaker | call-mute 0|1\n", argv[0]);
		return 2;
	}
	for (index = 1; index < argc; index++) {
		int written = snprintf(request + used, sizeof(request) - used,
			"%s%s", index == 1 ? "" : " ", argv[index]);
		if (written < 0 || (size_t)written >= sizeof(request) - used) {
			fprintf(stderr, "audio control request is too long\n");
			return 2;
		}
		used += (size_t)written;
	}
	socket_fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
	if (socket_fd < 0) {
		perror("open audio control socket");
		return 1;
	}
	strcpy(address.sun_path, CONTROL_SOCKET);
	if (connect(socket_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
		perror("connect stock audio control");
		close(socket_fd);
		return 1;
	}
	if (write(socket_fd, request, used) != (ssize_t)used) {
		perror("send audio control request");
		close(socket_fd);
		return 1;
	}
	shutdown(socket_fd, SHUT_WR);
	while ((length = read(socket_fd, response, sizeof(response))) > 0)
		if (write(STDOUT_FILENO, response, (size_t)length) != length)
			break;
	close(socket_fd);
	return length < 0 ? 1 : 0;
}

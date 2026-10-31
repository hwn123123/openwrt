// SPDX-License-Identifier: GPL-2.0-only
#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int set_exec_context(const char *context)
{
	int fd;
	ssize_t length = (ssize_t)strlen(context);

	fd = open("/proc/self/attr/exec", O_WRONLY | O_CLOEXEC);
	if (fd < 0)
		return -1;
	if (write(fd, context, (size_t)length) != length) {
		if (errno == 0)
			errno = EIO;
		close(fd);
		return -1;
	}
	close(fd);
	return 0;
}

int main(int argc, char **argv)
{
	char *child_argv[] = {
		"/vendor/bin/xm14pro-dds-control",
		NULL,
		NULL,
	};

	if (argc != 3) {
		fprintf(stderr, "usage: %s ANDROID_ROOT get|primary|secondary\n",
			argv[0]);
		return 2;
	}
	child_argv[1] = argv[2];
	unsetenv("LD_PRELOAD");
	unsetenv("LD_LIBRARY_PATH");
	if (chroot(argv[1]) < 0 || chdir("/") < 0) {
		perror("enter Android compatibility root");
		return 2;
	}
	if (set_exec_context("u:r:vendor_nicmd:s0") < 0) {
		perror("set SELinux exec context");
		return 2;
	}
	execv(child_argv[0], child_argv);
	perror("exec Xiaomi DDS control");
	return 2;
}

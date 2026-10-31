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
	if (fd < 0) {
		perror("open SELinux exec context");
		return -1;
	}
	if (write(fd, context, (size_t)length) != length) {
		if (errno == 0)
			errno = EIO;
		perror("set SELinux exec context");
		close(fd);
		return -1;
	}
	close(fd);
	return 0;
}

int main(int argc, char **argv)
{
	char *child_argv[] = {
		"/system/bin/bootstrap/linker64",
		"/vendor/bin/xm14pro-dsi-probe",
		NULL,
	};

	if (argc != 2) {
		fprintf(stderr, "usage: %s ANDROID_ROOT\n", argv[0]);
		return 2;
	}

	unsetenv("LD_PRELOAD");
	unsetenv("LD_LIBRARY_PATH");
	if (setenv("LD_PRELOAD", "/vendor/lib64/libxm14pro_nicm_compat.so", 1) < 0) {
		perror("set NICM Android compatibility library");
		return 2;
	}

	if (chroot(argv[1]) < 0 || chdir("/") < 0) {
		perror("enter Android compatibility root");
		return 2;
	}

	if (set_exec_context("u:r:vendor_nicmd:s0") < 0)
		return 2;

	execv(child_argv[0], child_argv);
	perror("exec Xiaomi DSI probe");
	return 2;
}

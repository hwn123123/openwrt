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
		"/vendor/bin/xm14pro-dsi-dial",
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
	};

	if (argc != 6 && argc != 7) {
		fprintf(stderr,
			"usage: %s ANDROID_ROOT DSI_SUBSCRIPTION APN IP_VERSION HOLD_SECONDS [STOP_IDENTIFIER]\n",
			argv[0]);
		return 2;
	}

	child_argv[1] = argv[2];
	child_argv[2] = argv[3];
	child_argv[3] = argv[4];
	child_argv[4] = argv[5];
	child_argv[5] = argc == 7 ? argv[6] : "cellular";

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

	/* Execute the Bionic ELF directly. Its PT_INTERP selects Android's
	 * linker64 and preserves argv[0..4]. Invoking linker64 as argv[0] leaves
	 * the linker path visible to this stock-style client and shifts all four
	 * runtime arguments by one. */
	execv(child_argv[0], child_argv);
	perror("exec Xiaomi DSI dial client");
	return 2;
}

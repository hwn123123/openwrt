// SPDX-License-Identifier: GPL-2.0-only
#define _GNU_SOURCE

#include <errno.h>
#include <grp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>

#define STOCK_USB_BINARY "/vendor/bin/hw/android.hardware.usb-service.qti"
#define USB_FRAMEWORK_BINARY "/vendor/bin/xm14pro-usb-framework"
#define STOCK_USB_CONTEXT "u:r:vendor_hal_usb_qti:s0"
#define USB_FRAMEWORK_CONTEXT "u:r:system_server:s0"

/* android_filesystem_config.h: system, mtp and usb. These are the exact
 * supplemental groups in android.hardware.usb-service.qti.rc. */
static const gid_t stock_groups[] = { 1000, 1024, 1018 };

static int set_exec_context(const char *context)
{
	int fd;
	ssize_t written;

	fd = open("/proc/self/attr/exec", O_WRONLY | O_CLOEXEC);
	if (fd < 0)
		return -1;
	written = write(fd, context, strlen(context));
	close(fd);
	return written == (ssize_t)strlen(context) ? 0 : -1;
}

int main(int argc, char **argv)
{
	const char *binary;
	const char *context;
	char *child_argv[] = {
		"/system/bin/bootstrap/linker64",
		NULL,
		NULL,
	};

	if (argc != 3) {
		fprintf(stderr, "usage: %s ANDROID_ROOT port|framework\n", argv[0]);
		return 2;
	}
	if (strcmp(argv[2], "port") == 0) {
		binary = STOCK_USB_BINARY;
		context = STOCK_USB_CONTEXT;
	} else if (strcmp(argv[2], "framework") == 0) {
		binary = USB_FRAMEWORK_BINARY;
		context = USB_FRAMEWORK_CONTEXT;
	} else {
		fprintf(stderr, "unsupported USB sidecar service: %s\n", argv[2]);
		return 2;
	}
	child_argv[1] = (char *)binary;

	/* The three real Binder managers have already survived their startup
	 * checks before this launcher is called. Android property_service is not
	 * present, so reuse the narrow, existing readiness compatibility bridge. */
	setenv("XM14_BINDER_MANAGERS_READY", "1", 1);
	setenv("XM14_PROPERTY_SERVICE_ABSENT", "1", 1);
	if (setenv("LD_PRELOAD",
		   "/vendor/lib64/libxm14pro_hidl_compat.so:"
		   "/vendor/lib64/libxm14pro_telephony_compat.so", 1) < 0) {
		perror("set Android compatibility preload");
		return 2;
	}
	setenv("ANDROID_ROOT", "/system", 1);
	setenv("ANDROID_DATA", "/data", 1);
	setenv("ANDROID_I18N_ROOT", "/apex/com.android.i18n", 1);
	unsetenv("LD_LIBRARY_PATH");

	if (chroot(argv[1]) < 0 || chdir("/") < 0) {
		perror("enter Android compatibility root");
		return 2;
	}
	if (set_exec_context(context) < 0) {
		perror("set stock USB HAL SELinux context");
		return 2;
	}
	if (setgroups(sizeof(stock_groups) / sizeof(stock_groups[0]),
		      stock_groups) < 0 || setgid(1000) < 0 || setuid(1000) < 0) {
		perror("set stock USB HAL identity");
		return 2;
	}

	execv(child_argv[0], child_argv);
	perror("exec stock USB HAL");
	return 2;
}

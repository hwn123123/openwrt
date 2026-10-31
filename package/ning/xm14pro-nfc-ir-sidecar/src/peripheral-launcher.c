// SPDX-License-Identifier: GPL-2.0-only
#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#define NFC_BINARY "/odm/bin/hw/android.hardware.nqnfc-service.nxp"
#define IR_BINARY "/vendor/bin/hw/android.hardware.ir-service.example"
#define CONTROL_BINARY "/vendor/bin/xm14pro-peripheral-control"
#define NFC_STACK_PROBE_BINARY "/vendor/bin/xm14pro-nfc-stack-probe"
#define NFC_SERVICE_BINARY "/vendor/bin/xm14pro-nfc-service"
#define NFC_CONTEXT "u:r:hal_nfc_default:s0"
#define IR_CONTEXT "u:r:hal_ir_default:s0"
#define CONTROL_CONTEXT "u:r:system_server:s0"

static const gid_t nfc_groups[] = { 1027, 2912 };
static const gid_t ir_groups[] = { 1000 };
static const gid_t control_groups[] = { 1000, 1027, 2912 };

struct service_spec {
	const char *binary;
	const char *context;
	uid_t uid;
	gid_t gid;
	const gid_t *groups;
	size_t group_count;
	int passthrough_args;
	int private_nfc_libraries;
};

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
	struct service_spec spec;
	char **child_argv;
	int child_argc;

	if (argc < 3) {
		fprintf(stderr,
			"usage: %s ANDROID_ROOT nfc|infrared|control|nfc-stack-probe|nfc-service ARGS...\n",
			argv[0]);
		return 2;
	}
	memset(&spec, 0, sizeof(spec));
	if (strcmp(argv[2], "nfc") == 0) {
		spec.binary = NFC_BINARY;
		spec.context = NFC_CONTEXT;
		spec.uid = 1027;
		spec.gid = 1027;
		spec.groups = nfc_groups;
		spec.group_count = sizeof(nfc_groups) / sizeof(nfc_groups[0]);
	} else if (strcmp(argv[2], "infrared") == 0) {
		spec.binary = IR_BINARY;
		spec.context = IR_CONTEXT;
		spec.uid = 1000;
		spec.gid = 1000;
		spec.groups = ir_groups;
		spec.group_count = sizeof(ir_groups) / sizeof(ir_groups[0]);
	} else if (strcmp(argv[2], "control") == 0 && argc >= 4) {
		spec.binary = CONTROL_BINARY;
		spec.context = CONTROL_CONTEXT;
		spec.uid = 1000;
		spec.gid = 1000;
		spec.groups = control_groups;
		spec.group_count = sizeof(control_groups) / sizeof(control_groups[0]);
		spec.passthrough_args = 1;
	} else if (strcmp(argv[2], "nfc-stack-probe") == 0) {
		spec.binary = NFC_STACK_PROBE_BINARY;
		spec.context = CONTROL_CONTEXT;
		spec.uid = 1000;
		spec.gid = 1000;
		spec.groups = control_groups;
		spec.group_count = sizeof(control_groups) / sizeof(control_groups[0]);
		spec.private_nfc_libraries = 1;
	} else if (strcmp(argv[2], "nfc-service") == 0 && argc >= 4) {
		spec.binary = NFC_SERVICE_BINARY;
		spec.context = CONTROL_CONTEXT;
		spec.uid = 1000;
		spec.gid = 1000;
		spec.groups = control_groups;
		spec.group_count = sizeof(control_groups) / sizeof(control_groups[0]);
		spec.passthrough_args = 1;
		spec.private_nfc_libraries = 1;
	} else {
		fprintf(stderr, "unsupported peripheral service: %s\n", argv[2]);
		return 2;
	}

	setenv("XM14_BINDER_MANAGERS_READY", "1", 1);
	setenv("XM14_PROPERTY_SERVICE_ABSENT", "1", 1);
	if (setenv("LD_PRELOAD",
		   "/vendor/lib64/libxm14pro_hidl_compat.so:"
		   "/vendor/lib64/libxm14pro_telephony_compat.so", 1) < 0) {
		perror("set peripheral compatibility preload");
		return 2;
	}
	setenv("ANDROID_ROOT", "/system", 1);
	setenv("ANDROID_DATA", "/data", 1);
	setenv("ANDROID_I18N_ROOT", "/apex/com.android.i18n", 1);
	if (spec.private_nfc_libraries)
		setenv("LD_LIBRARY_PATH", "/vendor/lib64/nfc", 1);
	else
		unsetenv("LD_LIBRARY_PATH");

	if (chroot(argv[1]) < 0 || chdir("/") < 0) {
		perror("enter Android compatibility root");
		return 2;
	}
	if (set_exec_context(spec.context) < 0) {
		perror("set stock peripheral SELinux context");
		return 2;
	}
	if (setgroups(spec.group_count, spec.groups) < 0 ||
	    setgid(spec.gid) < 0 || setuid(spec.uid) < 0) {
		perror("set stock peripheral identity");
		return 2;
	}

	child_argc = spec.passthrough_args ? argc - 1 : 2;
	child_argv = calloc((size_t)child_argc + 1, sizeof(*child_argv));
	if (!child_argv) {
		perror("allocate peripheral arguments");
		return 2;
	}
	child_argv[0] = "/system/bin/bootstrap/linker64";
	child_argv[1] = (char *)spec.binary;
	if (spec.passthrough_args)
		for (int i = 3; i < argc; ++i)
			child_argv[i - 1] = argv[i];
	execv(child_argv[0], child_argv);
	perror("exec stock peripheral service");
	return 2;
}

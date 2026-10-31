// SPDX-License-Identifier: GPL-2.0-only
#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <linux/capability.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

static int make_control_socket(const char *path, int target_fd,
			       uid_t uid, gid_t gid)
{
	struct sockaddr_un addr = { .sun_family = AF_UNIX };
	int fd;

	if (strlen(path) >= sizeof(addr.sun_path)) {
		fprintf(stderr, "socket path too long: %s\n", path);
		exit(2);
	}

	fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0) {
		perror("socket");
		exit(2);
	}
	if (fcntl(fd, F_SETFD, 0) < 0) {
		perror("clear FD_CLOEXEC");
		exit(2);
	}

	strcpy(addr.sun_path, path);
	unlink(path);
	if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror(path);
		exit(2);
	}
	if (chmod(path, 0660) < 0) {
		perror("chmod socket");
		exit(2);
	}
	if (chown(path, uid, gid) < 0) {
		perror("chown socket");
		exit(2);
	}
	if (listen(fd, 16) < 0) {
		perror("listen");
		exit(2);
	}

	if (fd != target_fd) {
		if (dup2(fd, target_fd) < 0) {
			perror("dup2 control socket");
			exit(2);
		}
		close(fd);
		fd = target_fd;
	}
	if (fcntl(fd, F_SETFD, 0) < 0) {
		perror("clear target FD_CLOEXEC");
		exit(2);
	}

	return fd;
}

static void export_socket(const char *name, int fd)
{
	char env_name[128];
	char value[32];

	snprintf(env_name, sizeof(env_name), "ANDROID_SOCKET_%s", name);
	snprintf(value, sizeof(value), "%d", fd);
	if (setenv(env_name, value, 1) < 0) {
		perror("setenv");
		exit(2);
	}
}

static void set_android_identity(const char *service)
{
	struct __user_cap_header_struct cap_header = {
		.version = _LINUX_CAPABILITY_VERSION_3,
		.pid = 0,
	};
	struct __user_cap_data_struct cap_data[2] = { { 0 }, { 0 } };
	gid_t groups[2];
	size_t group_count;

	if (strcmp(service, "qseecomd") == 0) {
		/* Android init starts qseecomd as root:root with drmrpc as its
		 * supplementary group.  qseecomd opens the SSD/RPMB device first,
		 * then drops itself to system:system while retaining SYS_RAWIO and
		 * BLOCK_SUSPEND.  Do not pre-drop it in the launcher.
		 */
		groups[0] = 1026;
		if (setgroups(1, groups) < 0) {
			perror("set qseecomd supplementary group");
			exit(2);
		}
		return;
	}

	if (strcmp(service, "ssgtzd") == 0) {
		/* Android init: group system oem_2912 wakelock. */
		groups[0] = 2912;
		groups[1] = 3010;
		group_count = 2;
		if (prctl(PR_SET_KEEPCAPS, 1L) < 0) {
			perror("PR_SET_KEEPCAPS");
			exit(2);
		}
	} else {
		/* Android init: group system inet. */
		groups[0] = 3003;
		group_count = 1;
	}

	if (setgroups(group_count, groups) < 0 || setgid(1000) < 0 ||
	    setuid(1000) < 0) {
		perror("set Android service identity");
		exit(2);
	}

	if (strcmp(service, "ssgtzd") == 0) {
		/* Android init: WAKE_ALARM (35) and BLOCK_SUSPEND (36). */
		cap_data[1].effective = 0x18;
		cap_data[1].permitted = 0x18;
		cap_data[1].inheritable = 0x18;
		if (syscall(SYS_capset, &cap_header, cap_data) < 0) {
			perror("capset WAKE_ALARM/BLOCK_SUSPEND");
			exit(2);
		}
		/* Preserve both capabilities across exec of Android's linker64. */
		if (prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_RAISE,
			  CAP_WAKE_ALARM, 0L, 0L) < 0 ||
		    prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_RAISE,
			  CAP_BLOCK_SUSPEND, 0L, 0L) < 0) {
			perror("raise ambient Android capabilities");
			exit(2);
		}
	}
}

static void set_android_exec_context(const char *service)
{
	const char *context;
	int fd;
	ssize_t written;

	if (strcmp(service, "qseecomd") == 0)
		context = "u:r:tee:s0";
	else if (strcmp(service, "ssgtzd") == 0)
		context = "u:r:vendor_ssgtzd:s0";
	else
		context = "u:r:vendor_qwesd:s0";

	/*
	 * The Qualcomm secure-app loader forwards more than Linux uid/gid to
	 * secure world.  Reproduce Android init's exec domain as well.  The
	 * device policy is loaded permissively by xm14pro-selinux, so it supplies
	 * the credential identity without applying Android access denials to
	 * the rest of nwrt.
	 */
	fd = open("/proc/self/attr/exec", O_WRONLY | O_CLOEXEC);
	if (fd < 0) {
		perror("open SELinux exec context");
		exit(2);
	}
	written = write(fd, context, strlen(context));
	if (written != (ssize_t)strlen(context)) {
		if (written >= 0)
			errno = EIO;
		perror("set SELinux exec context");
		close(fd);
		exit(2);
	}
	close(fd);
}

int main(int argc, char **argv)
{
	int ssgtzd_fd = -1;
	int opener_fd = -1;
	int topology_fd = -1;
	const char *service;
	char *ssgtzd_argv[] = {
		"/system/bin/bootstrap/linker64",
		"/vendor/bin/ssgtzd",
		NULL,
	};
	char *qwesd_argv[] = {
		"/system/bin/bootstrap/linker64",
		"/vendor/bin/qwesd",
		NULL,
	};
	char *qseecomd_argv[] = {
		"/system/bin/bootstrap/linker64",
		"/vendor/bin/qseecomd",
		NULL,
	};
	char **child_argv;

	if (argc < 2 || argc > 3) {
		fprintf(stderr,
			"usage: %s ANDROID_ROOT [qseecomd|ssgtzd|qwesd]\n",
			argv[0]);
		return 2;
	}
	service = argc == 3 ? argv[2] : "ssgtzd";
	if (strcmp(service, "ssgtzd") == 0)
		child_argv = ssgtzd_argv;
	else if (strcmp(service, "qwesd") == 0)
		child_argv = qwesd_argv;
	else if (strcmp(service, "qseecomd") == 0)
		child_argv = qseecomd_argv;
	else {
		fprintf(stderr, "unsupported Android service: %s\n", service);
		return 2;
	}

	if (mkdir("/dev/socket", 0755) < 0 && errno != EEXIST) {
		perror("mkdir /dev/socket");
		return 2;
	}

	if (strcmp(service, "qseecomd") == 0) {
		/* Matches: socket notify-topology stream 660 system drmrpc. */
		topology_fd = make_control_socket("/dev/socket/notify-topology", 24,
					       1000, 1026);
		export_socket("notify_topology", topology_fd);
	} else if (strcmp(service, "ssgtzd") == 0) {
		/* Match Android init's inherited descriptors as well as its env. */
		ssgtzd_fd = make_control_socket("/dev/socket/ssgtzd", 25,
						1000, 2912);
		opener_fd = make_control_socket("/dev/socket/ssgtzd_opener", 26,
						1000, 1000);
		export_socket("ssgtzd", ssgtzd_fd);
		export_socket("ssgtzd_opener", opener_fd);
	} else {
		/* Android starts qwesd in a later init phase. */
		sleep(3);
	}

	/*
	 * procd injects its musl libsetlbf helper when stdout/stderr logging is
	 * enabled.  It must not leak into the Android linker namespace: the
	 * compatibility root contains Android/Bionic binaries, not OpenWrt's
	 * /lib/libsetlbf.so.
	 */
	unsetenv("LD_PRELOAD");

	if (chroot(argv[1]) < 0) {
		perror("chroot");
		return 2;
	}
	if (chdir("/") < 0) {
		perror("chdir");
		return 2;
	}

	set_android_exec_context(service);
	set_android_identity(service);

	execv(child_argv[0], child_argv);
	perror("exec Android QWES service");
	return 2;
}

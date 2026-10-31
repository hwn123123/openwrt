// SPDX-License-Identifier: GPL-2.0-only
#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <linux/capability.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

struct service_spec {
	const char *name;
	const char *path;
	const char *context;
	uid_t uid;
	gid_t gid;
	const gid_t *groups;
	size_t group_count;
	unsigned long long capabilities;
	const char *arg1;
	const char *arg2;
	int passthrough_args;
};

static const gid_t system_groups[] = { 1000, 3009 };
static const gid_t system_suspend_groups[] = { 3010 };
static const gid_t qcril_groups[] = {
	1001, 2001, 3003, 9998, 1005, 1007, 3009, 3010, 2901, 2912,
};
static const gid_t ims_groups[] = { 1001, 1010, 3003, 2901, 1007 };
static const gid_t ims_data_groups[] = { 1001, 3003, 2901, 1007 };
static const gid_t ims_rtp_groups[] = { 1001, 2901, 3003, 1007, 1026 };
static const gid_t peripheral_groups[] = { 1000 };
static const gid_t qti_ims_app_groups[] = { 1001, 1010, 3003, 3009, 2901 };
static const gid_t cnd_groups[] = { 1000, 1010, 3003, 1001, 3010, 3005, 2912 };

static const struct service_spec services[] = {
	{
		.name = "servicemanager",
		.path = "/system/bin/servicemanager",
		.context = "u:r:servicemanager:s0",
		.uid = 1000,
		.gid = 1000,
		.groups = system_groups,
		.group_count = sizeof(system_groups) / sizeof(system_groups[0]),
		.capabilities = (1ULL << CAP_SYS_NICE),
	},
	{
		.name = "hwservicemanager",
		.path = "/system_ext/bin/hwservicemanager",
		.context = "u:r:hwservicemanager:s0",
		.uid = 1000,
		.gid = 1000,
		.groups = system_groups,
		.group_count = sizeof(system_groups) / sizeof(system_groups[0]),
	},
	{
		.name = "vndservicemanager",
		.path = "/vendor/bin/vndservicemanager",
		.context = "u:r:vndservicemanager:s0",
		.uid = 1000,
		.gid = 1000,
		.groups = system_groups,
		.group_count = sizeof(system_groups) / sizeof(system_groups[0]),
		.arg1 = "/dev/vndbinder",
	},
	{
		.name = "system-suspend",
		.path = "/system/bin/hw/android.system.suspend-service",
		.context = "u:r:system_suspend:s0",
		.uid = 1000,
		.gid = 1000,
		.groups = system_suspend_groups,
		.group_count = sizeof(system_suspend_groups) /
			sizeof(system_suspend_groups[0]),
		.capabilities = (1ULL << CAP_BLOCK_SUSPEND),
	},
	{
		.name = "pm-service",
		.path = "/vendor/bin/pm-service",
		.context = "u:r:vendor_per_mgr:s0",
		.uid = 1000,
		.gid = 1000,
		.groups = peripheral_groups,
		.group_count = sizeof(peripheral_groups) /
			sizeof(peripheral_groups[0]),
	},
	{
		.name = "pm-proxy",
		.path = "/vendor/bin/pm-proxy",
		.context = "u:r:vendor_per_proxy:s0",
		.uid = 1000,
		.gid = 1000,
		.groups = peripheral_groups,
		.group_count = sizeof(peripheral_groups) /
			sizeof(peripheral_groups[0]),
	},
	{
		/* Stock cnd owns vendor.qti.data.factoryservice.IFactory/default.
		 * IMS data setup waits on this real service before DSI can finish. */
		.name = "cnd",
		.path = "/vendor/bin/cnd",
		.context = "u:r:vendor_cnd:s0",
		.uid = 1000,
		.gid = 1000,
		.groups = cnd_groups,
		.group_count = sizeof(cnd_groups) / sizeof(cnd_groups[0]),
		.capabilities = (1ULL << CAP_NET_ADMIN) | (1ULL << CAP_NET_RAW),
	},
	{
		.name = "qcril1",
		.path = "/vendor/bin/hw/qcrilNrd",
		.context = "u:r:rild:s0",
		.uid = 1001,
		.gid = 1001,
		.groups = qcril_groups,
		.group_count = sizeof(qcril_groups) / sizeof(qcril_groups[0]),
		.capabilities = (1ULL << CAP_NET_ADMIN) |
			(1ULL << CAP_NET_RAW) | (1ULL << CAP_BLOCK_SUSPEND),
	},
	{
		.name = "qcril2",
		.path = "/vendor/bin/hw/qcrilNrd",
		.context = "u:r:rild:s0",
		.uid = 1001,
		.gid = 1001,
		.groups = qcril_groups,
		.group_count = sizeof(qcril_groups) / sizeof(qcril_groups[0]),
		.capabilities = (1ULL << CAP_NET_ADMIN) |
			(1ULL << CAP_NET_RAW) | (1ULL << CAP_BLOCK_SUSPEND),
		.arg1 = "-c",
		.arg2 = "2",
	},
	{
		.name = "imsdaemon",
		.path = "/vendor/bin/imsdaemon",
		.context = "u:r:vendor_ims_service:s0",
		.uid = 1001,
		.gid = 1001,
		.groups = ims_groups,
		.group_count = sizeof(ims_groups) / sizeof(ims_groups[0]),
	},
	{
		.name = "ims-data",
		.path = "/vendor/bin/ims-dataservice-daemon",
		.context = "u:r:vendor_ims_dcservice:s0",
		.uid = 1001,
		.gid = 1001,
		.groups = ims_data_groups,
		.group_count = sizeof(ims_data_groups) / sizeof(ims_data_groups[0]),
	},
	{
		.name = "ims-rtp",
		.path = "/vendor/bin/ims_rtp_daemon",
		.context = "u:r:vendor_hal_imsrtp:s0",
		.uid = 1001,
		.gid = 1001,
		.groups = ims_rtp_groups,
		.group_count = sizeof(ims_rtp_groups) / sizeof(ims_rtp_groups[0]),
	},
	{
		/* Read-only Binder reachability probe; never started by procd. */
		.name = "radio-probe",
		.path = "/vendor/bin/xm14pro-radio-probe",
		.context = "u:r:rild:s0",
		.uid = 1001,
		.gid = 1001,
		.groups = qcril_groups,
		.group_count = sizeof(qcril_groups) / sizeof(qcril_groups[0]),
	},
	{
		/* Read-only single-service probe used by the cold-boot dependency
		 * gate. Run with the same identity as the stock IMS data daemon. */
		.name = "factory-probe",
		.path = "/vendor/bin/xm14pro-radio-probe",
		.context = "u:r:vendor_ims_dcservice:s0",
		.uid = 1001,
		.gid = 1001,
		.groups = ims_data_groups,
		.group_count = sizeof(ims_data_groups) /
			sizeof(ims_data_groups[0]),
		.passthrough_args = 1,
	},
	{
		/* Long-lived framework-side owner of standard Radio callbacks. */
		.name = "radio-framework",
		.path = "/vendor/bin/xm14pro-radio-framework",
		.context = "u:r:radio:s0",
		.uid = 1001,
		.gid = 1001,
		.groups = qcril_groups,
		.group_count = sizeof(qcril_groups) / sizeof(qcril_groups[0]),
	},
	{
		/* Command client for the persistent IMS owner. */
		.name = "radio-sms",
		.path = "/vendor/bin/xm14pro-radio-sms",
		.context = "u:r:vendor_qtelephony:s0:c230,c256,c512,c768",
		.uid = 10230,
		.gid = 10230,
		.groups = qti_ims_app_groups,
		.group_count = sizeof(qti_ims_app_groups) /
			sizeof(qti_ims_app_groups[0]),
		.passthrough_args = 1,
	},
	{
		/* HyperOS runs org.codeaurora.ims as UID 10230 in vendor_qtelephony.
		 * This device-local native owner mirrors that identity and keeps both
		 * Qualcomm IMS callbacks alive for the whole nwrt boot. */
		.name = "ims-bridge",
		.path = "/vendor/bin/xm14pro-radio-sms",
		.context = "u:r:vendor_qtelephony:s0:c230,c256,c512,c768",
		.uid = 10230,
		.gid = 10230,
		.groups = qti_ims_app_groups,
		.group_count = sizeof(qti_ims_app_groups) /
			sizeof(qti_ims_app_groups[0]),
		.passthrough_args = 1,
	},
};

static const struct service_spec *find_service(const char *name)
{
	size_t i;

	for (i = 0; i < sizeof(services) / sizeof(services[0]); i++)
		if (strcmp(name, services[i].name) == 0)
			return &services[i];
	return NULL;
}

static int make_control_socket(const char *path, int target_fd,
			       uid_t uid, gid_t gid)
{
	struct sockaddr_un addr = { .sun_family = AF_UNIX };
	int fd;

	if (strlen(path) >= sizeof(addr.sun_path)) {
		fprintf(stderr, "socket path too long: %s\n", path);
		return -1;
	}
	fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0)
		return -1;
	if (fcntl(fd, F_SETFD, 0) < 0)
		return -1;
	strcpy(addr.sun_path, path);
	unlink(path);
	if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0 ||
	    chmod(path, 0660) < 0 || chown(path, uid, gid) < 0 ||
	    listen(fd, 16) < 0)
		return -1;
	if (fd != target_fd) {
		if (dup2(fd, target_fd) < 0)
			return -1;
		close(fd);
		fd = target_fd;
	}
	if (fcntl(fd, F_SETFD, 0) < 0)
		return -1;
	return fd;
}

static int export_socket(const char *name, int fd)
{
	char env_name[128];
	char value[32];

	snprintf(env_name, sizeof(env_name), "ANDROID_SOCKET_%s", name);
	snprintf(value, sizeof(value), "%d", fd);
	return setenv(env_name, value, 1);
}

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

static int set_identity(const struct service_spec *spec)
{
	struct __user_cap_header_struct header = {
		.version = _LINUX_CAPABILITY_VERSION_3,
		.pid = 0,
	};
	struct __user_cap_data_struct data[2] = { { 0 }, { 0 } };
	int cap;

	if (spec->capabilities && prctl(PR_SET_KEEPCAPS, 1L) < 0)
		return -1;
	if (setgroups(spec->group_count, spec->groups) < 0 ||
	    setgid(spec->gid) < 0 || setuid(spec->uid) < 0)
		return -1;

	data[0].effective = (uint32_t)spec->capabilities;
	data[0].permitted = (uint32_t)spec->capabilities;
	data[0].inheritable = (uint32_t)spec->capabilities;
	data[1].effective = (uint32_t)(spec->capabilities >> 32);
	data[1].permitted = (uint32_t)(spec->capabilities >> 32);
	data[1].inheritable = (uint32_t)(spec->capabilities >> 32);
	if (spec->capabilities && syscall(SYS_capset, &header, data) < 0)
		return -1;
	for (cap = 0; cap < 64; cap++) {
		if (!(spec->capabilities & (1ULL << cap)))
			continue;
		if (prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_RAISE,
			  cap, 0L, 0L) < 0)
			return -1;
	}
	return 0;
}

int main(int argc, char **argv)
{
	const struct service_spec *spec;
	char **child_argv;
	char socket_dir[108];
	char socket_path[108];
	int ims_socket = -1;

	if (argc < 3) {
		fprintf(stderr, "usage: %s ANDROID_ROOT SERVICE [ARG...]\n", argv[0]);
		return 2;
	}
	spec = find_service(argv[2]);
	if (!spec) {
		fprintf(stderr, "unsupported service: %s\n", argv[2]);
		return 2;
	}
	if ((!spec->passthrough_args && argc != 3) ||
	    (spec->passthrough_args && argc < 4)) {
		fprintf(stderr, "invalid arguments for service: %s\n", spec->name);
		return 2;
	}

	if (snprintf(socket_dir, sizeof(socket_dir), "%s/dev/socket", argv[1]) >=
	    (int)sizeof(socket_dir)) {
		fprintf(stderr, "Android compatibility root path is too long\n");
		return 2;
	}
	if (mkdir(socket_dir, 0755) < 0 && errno != EEXIST) {
		perror("mkdir Android /dev/socket");
		return 2;
	}
	unsetenv("LD_PRELOAD");
	if (strcmp(spec->name, "system-suspend") == 0) {
		/*
		 * The real platform service registers both HIDL and Stable AIDL.
		 * libhidlbase waits for hwservicemanager.ready, which Android init
		 * normally publishes through property_service.  S60 is released only
		 * after S57 has verified the real manager, so expose that one verified
		 * state through the narrow HIDL bridge.  Do not load the QCRIL bridge
		 * or synthesize any Binder service in this platform process.
		 */
		setenv("XM14_BINDER_MANAGERS_READY", "1", 1);
		if (setenv("LD_PRELOAD",
			   "/system/lib64/libxm14pro_hidl_compat.so", 1) < 0) {
			perror("set SystemSuspend HIDL readiness bridge");
			return 2;
		}
	} else if (strcmp(spec->name, "servicemanager") != 0 &&
	    strcmp(spec->name, "hwservicemanager") != 0 &&
	    strcmp(spec->name, "vndservicemanager") != 0) {
		/*
		 * procd starts telephony only after all three real Binder managers
		 * have survived their SELinux/VINTF initialization.  Android init
		 * normally publishes this state through property_service, which is
		 * deliberately absent from the nwrt compatibility root.  Export the
		 * verified state to the narrow libbinder compatibility bridge.
		 */
		setenv("XM14_BINDER_MANAGERS_READY", "1", 1);
		setenv("XM14_PROPERTY_SERVICE_ABSENT", "1", 1);
		if (setenv("LD_PRELOAD",
			   "/vendor/lib64/libxm14pro_hidl_compat.so:"
			   "/vendor/lib64/libxm14pro_telephony_compat.so", 1) < 0) {
			perror("set telephony compatibility preload");
			return 2;
		}
	}
	setenv("ANDROID_ROOT", "/system", 1);
	setenv("ANDROID_DATA", "/data", 1);
	setenv("ANDROID_I18N_ROOT", "/apex/com.android.i18n", 1);
	/*
	 * The stock radio stack deliberately has no LD_LIBRARY_PATH.  Bionic uses
	 * /linkerconfig/ld.config.txt to keep the platform, vendor and VNDK copies
	 * of libbinder/libutils/libc++ in separate namespaces.  Flattening those
	 * paths corrupts RefBase ownership as soon as QCRIL talks to
	 * vndservicemanager, so never inherit or synthesize a global search path.
	 */
	unsetenv("LD_LIBRARY_PATH");

	if (chroot(argv[1]) < 0 || chdir("/") < 0) {
		perror("enter Android compatibility root");
		return 2;
	}
	if (strcmp(spec->name, "imsdaemon") == 0) {
		/*
		 * Android init creates this socket in the service's mount namespace
		 * and exports its inherited descriptor.  Bind it only after chroot so
		 * getsockname() returns the stock /dev/socket/ims_datad path; creating
		 * it through the host-side compatibility-root path is rejected by
		 * android_get_control_socket().  fd 26 matches the stock shennong boot.
		 */
		strcpy(socket_path, "/dev/socket/ims_datad");
		ims_socket = make_control_socket(socket_path, 26, 1000, 1001);
		if (ims_socket < 0 || export_socket("ims_datad", ims_socket) < 0) {
			perror("create ims_datad socket");
			return 2;
		}
	}
	if (set_exec_context(spec->context) < 0) {
		perror("set Android SELinux exec context");
		return 2;
	}
	if (set_identity(spec) < 0) {
		perror("set Android service identity");
		return 2;
	}

	if (spec->passthrough_args) {
		int i;

		child_argv = calloc((size_t)argc, sizeof(*child_argv));
		if (!child_argv) {
			perror("allocate Android service arguments");
			return 2;
		}
		child_argv[0] = "/system/bin/bootstrap/linker64";
		child_argv[1] = (char *)spec->path;
		for (i = 3; i < argc; i++)
			child_argv[i - 1] = argv[i];
	} else {
		child_argv = calloc(5, sizeof(*child_argv));
		if (!child_argv) {
			perror("allocate Android service arguments");
			return 2;
		}
		child_argv[0] = "/system/bin/bootstrap/linker64";
		child_argv[1] = (char *)spec->path;
		child_argv[2] = (char *)spec->arg1;
		child_argv[3] = (char *)spec->arg2;
	}
	execv(child_argv[0], child_argv);
	perror("exec Android telephony service");
	return 2;
}

// SPDX-License-Identifier: GPL-2.0-only
#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <linux/capability.h>
#include <stdint.h>
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

#define AUDIO_HAL_BINARY "/vendor/bin/hw/android.hardware.audio.service_64"
#define AUDIO_ADSPRPC_BINARY "/vendor/bin/audioadsprpcd"
#define AUDIO_HAL_CONTEXT "u:r:hal_audio_default:s0"
#define AUDIO_ADSPRPC_CONTEXT "u:r:vendor_audioadsprpcd:s0"

/* android_filesystem_config.h values and the exact order from
 * android.hardware.audio.service_64.rc in OS3.0.308.0. */
static const gid_t audio_hal_groups[] = {
	1005, /* audio */
	1006, /* camera */
	1019, /* drmrpc */
	3003, /* inet */
	1013, /* media */
	1031, /* mediadrm */
	3002, /* net_bt */
	3001, /* net_bt_admin */
	3006, /* net_bw_acct */
	2901, /* oem_2901 */
	3010, /* wakelock */
	2912, /* oem_2912 */
	1000, /* system */
};
static const gid_t audio_adsprpc_groups[] = { 1013 };

struct service_spec {
	const char *path;
	const char *context;
	uid_t uid;
	gid_t gid;
	const gid_t *groups;
	size_t group_count;
	unsigned long long capabilities;
	const char *arg1;
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

static int set_identity(const struct service_spec *spec)
{
	struct __user_cap_header_struct header = {
		.version = _LINUX_CAPABILITY_VERSION_3,
		.pid = 0,
	};
	struct __user_cap_data_struct data[2] = { { 0 }, { 0 } };
	int capability;

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
	for (capability = 0; capability < 64; capability++) {
		if (!(spec->capabilities & (1ULL << capability)))
			continue;
		if (prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_RAISE,
			  capability, 0L, 0L) < 0)
			return -1;
	}
	return 0;
}

static int make_control_socket(const char *path, int target_fd)
{
	struct sockaddr_un address = { .sun_family = AF_UNIX };
	int fd;

	if (strlen(path) >= sizeof(address.sun_path)) {
		errno = ENAMETOOLONG;
		return -1;
	}
	fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
	if (fd < 0)
		return -1;
	if (fcntl(fd, F_SETFD, 0) < 0)
		goto fail;
	strcpy(address.sun_path, path);
	unlink(path);
	if (bind(fd, (struct sockaddr *)&address, sizeof(address)) < 0 ||
	    chmod(path, 0666) < 0 || chown(path, 1000, 1000) < 0 ||
	    listen(fd, 16) < 0)
		goto fail;
	if (fd != target_fd) {
		if (dup2(fd, target_fd) < 0)
			goto fail;
		close(fd);
		fd = target_fd;
	}
	if (fcntl(fd, F_SETFD, 0) < 0)
		goto fail;
	return fd;

fail:
	close(fd);
	return -1;
}

static int export_socket(const char *name, int fd)
{
	char environment_name[128];
	char value[32];

	snprintf(environment_name, sizeof(environment_name),
		 "ANDROID_SOCKET_%s", name);
	snprintf(value, sizeof(value), "%d", fd);
	return setenv(environment_name, value, 1);
}

static int audio_card_ready(void)
{
	char buffer[16384];
	ssize_t length;
	int fd;
	int links = 0;
	ssize_t index;

	fd = open("/proc/asound/cards", O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return 0;
	length = read(fd, buffer, sizeof(buffer) - 1);
	close(fd);
	if (length <= 0)
		return 0;
	buffer[length] = '\0';
	if (!strstr(buffer, "pineapple-mtp-snd-card"))
		return 0;

	fd = open("/proc/asound/pcm", O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return 0;
	length = read(fd, buffer, sizeof(buffer));
	close(fd);
	if (length <= 0)
		return 0;
	for (index = 0; index < length; index++)
		if (buffer[index] == '\n')
			links++;
	return links == 53;
}

static int wait_for_audio_card(void)
{
	int attempt;

	for (attempt = 0; attempt < 45; attempt++) {
		if (audio_card_ready())
			return 0;
		sleep(1);
	}
	errno = ETIMEDOUT;
	return -1;
}

int main(int argc, char **argv)
{
	struct service_spec spec;
	char socket_directory[256];
	char *child_argv[] = {
		"/system/bin/bootstrap/linker64",
		NULL,
		NULL,
		NULL,
	};

	if (argc != 3) {
		fprintf(stderr, "usage: %s ANDROID_ROOT adsprpc|hal\n", argv[0]);
		return 2;
	}
	memset(&spec, 0, sizeof(spec));
	if (strcmp(argv[2], "adsprpc") == 0) {
		spec.path = AUDIO_ADSPRPC_BINARY;
		spec.context = AUDIO_ADSPRPC_CONTEXT;
		spec.uid = 1013;
		spec.gid = 1013;
		spec.groups = audio_adsprpc_groups;
		spec.group_count = sizeof(audio_adsprpc_groups) /
			sizeof(audio_adsprpc_groups[0]);
		spec.arg1 = "audiopd";
	} else if (strcmp(argv[2], "hal") == 0) {
		spec.path = AUDIO_HAL_BINARY;
		spec.context = AUDIO_HAL_CONTEXT;
		spec.uid = 1041;
		spec.gid = 1041;
		spec.groups = audio_hal_groups;
		spec.group_count = sizeof(audio_hal_groups) /
			sizeof(audio_hal_groups[0]);
		spec.capabilities = (1ULL << CAP_BLOCK_SUSPEND) |
			(1ULL << CAP_SYS_NICE);
	} else {
		fprintf(stderr, "unsupported audio service: %s\n", argv[2]);
		return 2;
	}

	if (snprintf(socket_directory, sizeof(socket_directory), "%s/dev/socket",
		     argv[1]) >= (int)sizeof(socket_directory)) {
		fprintf(stderr, "Android compatibility root path is too long\n");
		return 2;
	}
	if (mkdir(socket_directory, 0755) < 0 && errno != EEXIST) {
		perror("mkdir Android /dev/socket");
		return 2;
	}

	setenv("XM14_BINDER_MANAGERS_READY", "1", 1);
	setenv("XM14_PROPERTY_SERVICE_ABSENT", "1", 1);
	if (setenv("LD_PRELOAD",
		   "/vendor/lib64/libxm14pro_audio_properties.so:"
		   "/vendor/lib64/libxm14pro_audio_control.so:"
		   "/vendor/lib64/libxm14pro_hidl_compat.so:"
		   "/vendor/lib64/libxm14pro_telephony_compat.so", 1) < 0) {
		perror("set audio compatibility preload");
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
	if (strcmp(argv[2], "hal") == 0) {
		if (wait_for_audio_card() < 0) {
			perror("wait for exact pineapple audio card");
			return 2;
		}
	}
	if (strcmp(argv[2], "hal") == 0) {
		int holder;
		int ultrasound;
		int control;

		holder = make_control_socket("/dev/socket/audio_holder_socket_0", 26);
		ultrasound = make_control_socket("/dev/socket/audio_us_socket_0", 27);
		control = make_control_socket("/dev/socket/xm14pro_audio_control", 28);
		if (holder < 0 || ultrasound < 0 || control < 0 ||
		    export_socket("audio_holder_socket_0", holder) < 0 ||
		    export_socket("audio_us_socket_0", ultrasound) < 0 ||
		    export_socket("xm14pro_audio_control", control) < 0) {
			perror("create stock Audio HAL sockets");
			return 2;
		}
	}
	if (set_exec_context(spec.context) < 0) {
		perror("set stock audio SELinux context");
		return 2;
	}
	if (set_identity(&spec) < 0) {
		perror("set stock audio identity");
		return 2;
	}

	child_argv[1] = (char *)spec.path;
	child_argv[2] = (char *)spec.arg1;
	execv(child_argv[0], child_argv);
	perror("exec stock audio service");
	return 2;
}

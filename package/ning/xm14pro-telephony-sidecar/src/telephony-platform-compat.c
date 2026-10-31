// SPDX-License-Identifier: GPL-2.0-only
/*
 * Narrow Android libbinder compatibility bridge for the Xiaomi 14 Pro.
 *
 * Android init starts servicemanager and then publishes
 * servicemanager.ready=true through property_service.  nwrt starts the real
 * stock servicemanager under procd, but intentionally does not run Android
 * init/property_service.  Consequently stock libbinder loops forever in
 * WaitForProperty even though the manager is already alive.
 *
 * The launcher exports XM14_BINDER_MANAGERS_READY only after procd has
 * verified framework, hardware and vendor Binder managers.  Translate that
 * single verified state for both Binder readiness properties here and
 * delegate every other property wait to the unmodified Android
 * implementation.  libbinder waits for servicemanager.ready while
 * libhidlbase independently waits for hwservicemanager.ready before it sends
 * a transaction through /dev/hwbinder.
 *
 * QCRIL also has a shutdown-property monitor built around
 * __system_property_wait_any().  Without Android property_service the Bionic
 * implementation returns immediately with an unchanged serial, whereas its
 * contract is to wait for the property area to change.  That turns QCRIL's
 * otherwise harmless monitor into a full-core loop.  When the launcher marks
 * property_service as absent, preserve the observable API semantics by adding
 * a bounded wait only after the real implementation returned the same serial.
 *
 * HyperOS also publishes a small set of immutable, device-defining radio
 * properties before QCRIL starts.  They are not runtime SIM, operator, APN or
 * radio-power state.  Without Android property_service the two QCRIL
 * instances otherwise see this dual-SIM Xiaomi target as a generic/single-SIM
 * target: the secondary process binds the primary (empty) slot and Qualcomm
 * IMS rejects both SMS and voice registration.  Return only the exact stock
 * values observed on this Xiaomi 14 Pro while the launcher explicitly marks
 * property_service absent.
 *
 * The two stock QCRIL processes additionally coordinate their short-lived
 * pre-initialization and shared database through persist.radio.rild_init_num,
 * vendor.radio.qcril_pre_init_lock_held and vendor.radio.db_state. Android
 * property_service normally makes those writes visible across processes. A
 * failed property_set leaves each nwrt QCRIL instance believing it owns
 * initialization, while the secondary instance waits the full 100-second
 * database timeout. Mirror only these runtime coordination properties through
 * locked files in the already private radio state directory. Every unrelated
 * property read and write is still delegated unchanged.
 */

#include <stddef.h>
#include <stdint.h>

extern char *getenv(const char *name);
extern void *dlsym(void *handle, const char *symbol);
extern int open(const char *path, int flags, ...);
extern int close(int fd);
extern int fcntl(int fd, int command, ...);
extern long read(int fd, void *buffer, unsigned long count);
extern long write(int fd, const void *buffer, unsigned long count);
extern long lseek(int fd, long offset, int whence);
extern int ftruncate(int fd, long length);
struct xm14_timespec {
	long tv_sec;
	long tv_nsec;
};
extern int nanosleep(const struct xm14_timespec *duration,
		     struct xm14_timespec *remaining);

#define RTLD_NEXT ((void *)(intptr_t)-1)
#define XM14_O_RDONLY 0
#define XM14_O_RDWR 2
#define XM14_O_CREAT 0100
#define XM14_O_CLOEXEC 02000000
#define XM14_F_SETLKW 7
#define XM14_F_RDLCK 0
#define XM14_F_WRLCK 1
#define XM14_F_UNLCK 2
#define XM14_SEEK_SET 0
#define XM14_PROP_VALUE_MAX 92

struct xm14_flock {
	short type;
	short whence;
	long start;
	long length;
	int pid;
};

static const char wait_for_property_symbol[] =
	"_ZN7android4base15WaitForPropertyERKNSt3__112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEES9_NS1_6chrono8durationIxNS1_5ratioILl1ELl1000EEEEE";

static size_t literal_length(const char *literal)
{
	size_t length = 0;

	while (literal[length])
		length++;
	return length;
}

static int bytes_equal(const unsigned char *data, const char *literal,
		       size_t length)
{
	size_t index;

	for (index = 0; index < length; index++)
		if (data[index] != (unsigned char)literal[index])
			return 0;
	return 1;
}

static int libcxx_short_string_equals(const void *object, const char *literal)
{
	const unsigned char *bytes = object;
	size_t length = literal_length(literal);

	/*
	 * This firmware's libc++ short-string object stores length * 2 in byte
	 * zero and its inline characters at byte one.  A 22-byte value occupies
	 * the complete inline buffer and therefore has no in-object trailing NUL.
	 * Decode the verified length field instead of relying on padding bytes.
	 * All readiness literals handled here fit in the short representation;
	 * deliberately never interpret object bytes as a pointer.
	 */
	return length <= 22 && bytes[0] == (unsigned char)(length << 1) &&
	       bytes_equal(bytes + 1, literal, length);
}

typedef int (*wait_for_property_fn)(const void *, const void *, int64_t);
typedef uint32_t (*system_property_wait_any_fn)(uint32_t);
typedef int (*system_property_get_fn)(const char *, char *);
typedef int (*property_get_fn)(const char *, char *, const char *);
typedef int (*property_set_fn)(const char *, const char *);
typedef void (*xiaomi_get_property_value_from_int_fn)(const char *, int *, int);

static int string_equals(const char *left, const char *right)
{
	if (!left || !right)
		return 0;
	while (*left && *right && *left == *right) {
		left++;
		right++;
	}
	return *left == '\0' && *right == '\0';
}

static int copy_property_value(char *value, const char *source)
{
	int length = 0;

	if (!value || !source)
		return 0;
	while (source[length]) {
		value[length] = source[length];
		length++;
	}
	value[length] = '\0';
	return length;
}

static int property_service_is_absent(void)
{
	char *property_service_absent = getenv("XM14_PROPERTY_SERVICE_ABSENT");

	return property_service_absent && property_service_absent[0] == '1' &&
	       property_service_absent[1] == '\0';
}

static const char *runtime_radio_property_path(const char *name)
{
	if (!property_service_is_absent())
		return NULL;
	if (string_equals(name, "persist.radio.rild_init_num"))
		return "/data/vendor/radio/.xm14pro-properties/rild_init_num";
	if (string_equals(name, "vendor.radio.qcril_pre_init_lock_held"))
		return "/data/vendor/radio/.xm14pro-properties/"
		       "qcril_pre_init_lock_held";
	if (string_equals(name, "vendor.radio.db_state"))
		return "/data/vendor/radio/.xm14pro-properties/db_state";
	return NULL;
}

static int set_file_lock(int fd, short type)
{
	struct xm14_flock lock = {
		.type = type,
		.whence = XM14_SEEK_SET,
		.start = 0,
		.length = 0,
		.pid = 0,
	};

	/* QCRIL's stock seccomp policy permits fcntl but deliberately blocks the
	 * flock syscall.  POSIX record locks provide the same serialization while
	 * staying inside that unmodified sandbox. */
	return fcntl(fd, XM14_F_SETLKW, &lock);
}

static int read_runtime_radio_property(const char *name, char *value)
{
	const char *path = runtime_radio_property_path(name);
	long length;
	int fd;

	if (!path || !value)
		return -1;
	fd = open(path, XM14_O_RDONLY | XM14_O_CLOEXEC);
	if (fd < 0)
		return -1;
	if (set_file_lock(fd, XM14_F_RDLCK) < 0) {
		close(fd);
		return -1;
	}
	length = read(fd, value, XM14_PROP_VALUE_MAX - 1);
	set_file_lock(fd, XM14_F_UNLCK);
	close(fd);
	if (length <= 0 || length >= XM14_PROP_VALUE_MAX)
		return -1;
	while (length > 0 &&
	       (value[length - 1] == '\n' || value[length - 1] == '\r'))
		length--;
	if (length <= 0)
		return -1;
	value[length] = '\0';
	return (int)length;
}

static int write_runtime_radio_property(const char *name, const char *value)
{
	const char *path = runtime_radio_property_path(name);
	long length = 0;
	long written;
	int fd;

	if (!path || !value)
		return -1;
	while (value[length] && length < XM14_PROP_VALUE_MAX)
		length++;
	if (length >= XM14_PROP_VALUE_MAX)
		return -1;
	fd = open(path, XM14_O_RDWR | XM14_O_CREAT | XM14_O_CLOEXEC, 0660);
	if (fd < 0)
		return -1;
	if (set_file_lock(fd, XM14_F_WRLCK) < 0) {
		close(fd);
		return -1;
	}
	if (lseek(fd, 0, XM14_SEEK_SET) < 0) {
		set_file_lock(fd, XM14_F_UNLCK);
		close(fd);
		return -1;
	}
	written = write(fd, value, (unsigned long)length);
	if (written == length)
		written = ftruncate(fd, length) == 0 ? length : -1;
	set_file_lock(fd, XM14_F_UNLCK);
	close(fd);
	return written == length ? 0 : -1;
}

static int stock_radio_property(const char *name, char *value)
{
	if (!property_service_is_absent())
		return -1;
	if (string_equals(name, "persist.vendor.radio.hidl_dev_service"))
		return copy_property_value(value, "true");
	if (string_equals(name, "persist.radio.multisim.config"))
		return copy_property_value(value, "dsds");
	if (string_equals(name, "ro.vendor.radio.manufacturer_id"))
		return copy_property_value(value, "1");
	/* Exact values read by both stock QCRIL instances on the reference boot. */
	if (string_equals(name, "persist.radio.mi_rild_init"))
		return copy_property_value(value, "1");
	if (string_equals(name, "persist.radio.qril_init_wait_s"))
		return copy_property_value(value, "2");
	if (string_equals(name, "persist.radio.qril_wait_s"))
		return copy_property_value(value, "0");
	/* The complete stock HW/SW MBN trees are installed locally before QCRIL
	 * starts. Keep a bounded window for dual-SIM/operator checks, but do not
	 * inherit Android's 120-second framework recovery grace period: there is
	 * no Android package/update service which could change those files later. */
	if (string_equals(name, "persist.radio.mbn_selfreparing_timerout_s"))
		return copy_property_value(value, "10");
	return -1;
}

int xm14pro_wait_for_property(const void *name_object,
			      const void *expected_object,
			      int64_t timeout_ms)
	__asm__("_ZN7android4base15WaitForPropertyERKNSt3__112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEES9_NS1_6chrono8durationIxNS1_5ratioILl1ELl1000EEEEE");

int xm14pro_wait_for_property(const void *name_object,
			      const void *expected_object,
			      int64_t timeout_ms)
{
	static wait_for_property_fn original;
	char *ready;

	ready = getenv("XM14_BINDER_MANAGERS_READY");
	if (ready && ready[0] == '1' && ready[1] == '\0' &&
	    (libcxx_short_string_equals(name_object, "servicemanager.ready") ||
	     libcxx_short_string_equals(name_object,
					"hwservicemanager.ready")) &&
	    libcxx_short_string_equals(expected_object, "true"))
		return 1;

	if (!original)
		original = (wait_for_property_fn)dlsym(RTLD_NEXT,
			wait_for_property_symbol);
	if (!original)
		return 0;
	return original(name_object, expected_object, timeout_ms);
}

uint32_t __system_property_wait_any(uint32_t old_serial)
{
	static system_property_wait_any_fn original;
	const struct xm14_timespec backoff = { 1, 0 };
	uint32_t serial;
	char *property_service_absent;

	if (!original)
		original = (system_property_wait_any_fn)dlsym(
			RTLD_NEXT, "__system_property_wait_any");
	serial = original ? original(old_serial) : old_serial;

	property_service_absent = getenv("XM14_PROPERTY_SERVICE_ABSENT");
	if (property_service_absent && property_service_absent[0] == '1' &&
	    property_service_absent[1] == '\0' && serial == old_serial)
		nanosleep(&backoff, NULL);

	return serial;
}

int __system_property_get(const char *name, char *value)
{
	static system_property_get_fn original;
	int result = stock_radio_property(name, value);

	if (result < 0)
		result = read_runtime_radio_property(name, value);
	if (result >= 0)
		return result;
	if (!original)
		original = (system_property_get_fn)dlsym(
			RTLD_NEXT, "__system_property_get");
	return original ? original(name, value) : 0;
}

int property_get(const char *name, char *value, const char *default_value)
{
	static property_get_fn original;
	int result = stock_radio_property(name, value);

	if (result < 0)
		result = read_runtime_radio_property(name, value);
	if (result >= 0)
		return result;
	if (!original)
		original = (property_get_fn)dlsym(RTLD_NEXT, "property_get");
	if (original)
		return original(name, value, default_value);
	return copy_property_value(value, default_value ? default_value : "");
}

int property_set(const char *name, const char *value)
{
	static property_set_fn original;
	int result = write_runtime_radio_property(name, value);

	if (result >= 0)
		return result;
	if (!original)
		original = (property_set_fn)dlsym(RTLD_NEXT, "property_set");
	return original ? original(name, value) : -1;
}

void xiaomi_get_property_value_from_int(const char *name, int *value,
					int default_value)
{
	static xiaomi_get_property_value_from_int_fn original;

	/* libxiaomi_qcril resolves this helper through its PLT before it reaches
	 * property_get(). Interpose the MBN recovery timeout so both stock QCRIL
	 * instances receive the same bounded boot-time value. */
	if (property_service_is_absent() && value &&
	    string_equals(name, "persist.radio.mbn_selfreparing_timerout_s")) {
		*value = 10;
		return;
	}
	if (!original)
		original = (xiaomi_get_property_value_from_int_fn)dlsym(
			RTLD_NEXT, "xiaomi_get_property_value_from_int");
	if (original) {
		original(name, value, default_value);
		return;
	}
	if (value)
		*value = default_value;
}

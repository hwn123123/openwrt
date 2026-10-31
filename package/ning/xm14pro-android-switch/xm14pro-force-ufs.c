/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Xiaomi 14 Pro boots from UFS, but Android's property area is not present in
 * the nwrt hardware-service chroot. Qualcomm's recovery GPT helper therefore
 * cannot read ro.boot.bootdevice and incorrectly falls back to the eMMC path.
 * Its mark_boot_successful() path also needs ro.boot.slot_suffix to identify
 * the currently running B slot. Interpose only those two missing facts; slot
 * metadata and UFS ioctls remain in Xiaomi's unmodified boot-control library.
 */

#include <dlfcn.h>
#include <string.h>

#define PROPERTY_VALUE_MAX 92

typedef int (*property_get_fn)(const char *, char *, const char *);

int gpt_utils_is_ufs_device(void)
{
	return 1;
}

int property_get(const char *key, char *value, const char *default_value)
{
	static property_get_fn real_property_get;

	if (key && strcmp(key, "ro.boot.slot_suffix") == 0) {
		memcpy(value, "_b", 3);
		return 2;
	}

	if (!real_property_get)
		real_property_get = (property_get_fn)dlsym(RTLD_NEXT, "property_get");
	if (real_property_get)
		return real_property_get(key, value, default_value);

	if (!default_value)
		default_value = "";
	strncpy(value, default_value, PROPERTY_VALUE_MAX - 1);
	value[PROPERTY_VALUE_MAX - 1] = '\0';
	return (int)strlen(value);
}

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Android/Bionic compatibility shim for Qualcomm NICM on nwrt.
 *
 * NICM has two first-party paths: Android and B2G.  The Android path requires
 * servicemanager/netd over Binder.  nwrt is a Linux router userspace, so report
 * only the B2G service state and pass every other property lookup through to
 * Bionic unchanged.  No modem, SIM, APN or status value is synthesized here.
 */

#define PROPERTY_VALUE_MAX 92

extern int __system_property_get(const char *name, char *value);

static int same_string(const char *left, const char *right)
{
	while (*left && *left == *right) {
		left++;
		right++;
	}
	return *left == *right;
}

static int copy_value(char *destination, const char *source)
{
	int length = 0;

	if (!destination || !source)
		return 0;
	while (source[length] && length < PROPERTY_VALUE_MAX - 1) {
		destination[length] = source[length];
		length++;
	}
	destination[length] = '\0';
	return length;
}

int property_get(const char *key, char *value, const char *default_value)
{
	int length;

	if (key && same_string(key, "init.svc.b2g"))
		return copy_value(value, "running");

	length = __system_property_get(key, value);
	if (length > 0)
		return length;
	return copy_value(value, default_value);
}

/*
 * Qualcomm's nicmd unconditionally asks Android's Binder netd service for a
 * handle before it initializes the NICM/QMI data plane, even after the
 * platform was detected as B2G.  nwrt owns routing through netifd and has no
 * Android netd service, so the stock call waits forever and nicmd never
 * reaches NetDeviceManager::initialize().  Interpose only that initial
 * Android service lookup.  Modem, SIM, APN and QMI operations remain handled
 * by the unmodified Xiaomi/Qualcomm libraries.
 */
void xm14pro_skip_android_netd_wait(void)
	__asm__("_ZN4Nicm9ProxyNetd16getServiceHandleEv");

void xm14pro_skip_android_netd_wait(void)
{
}

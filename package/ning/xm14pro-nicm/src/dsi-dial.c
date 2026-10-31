// SPDX-License-Identifier: GPL-2.0-only
/*
 * Xiaomi 14 Pro stock DSI data-call client for nwrt.
 *
 * Keep this client deliberately small: SIM selection and APN discovery are
 * performed by the nwrt shell wrapper from live modem state.  This process
 * only translates those runtime values into the stock Qualcomm DSI API.  It
 * never invents an APN, subscription or connection result.
 */

typedef unsigned long size_type;
typedef long signed_size_type;
typedef void *dsi_handle_type;

struct kernel_timespec {
	long seconds;
	long nanoseconds;
};

struct dsi_call_param_value {
	char *buffer;
	int number;
};

/* AArch64 Linux/Bionic sockaddr_storage ABI: 128 bytes, 8-byte aligned. */
struct socket_address_storage {
	unsigned short family;
	unsigned char padding[118];
	unsigned long alignment;
};

struct dsi_address {
	char valid;
	struct socket_address_storage address;
};

struct dsi_address_info {
	struct dsi_address interface_address;
	unsigned int interface_mask;
	struct dsi_address gateway_address;
	unsigned int gateway_mask;
	struct dsi_address primary_dns_address;
	struct dsi_address secondary_dns_address;
};

struct dsi_call_end_reason {
	int reason_type;
	int reason_code;
};

struct dsi_data_channel_rate {
	unsigned long current_tx_rate;
	unsigned long current_rx_rate;
	unsigned long max_tx_rate;
	unsigned long max_rx_rate;
};

typedef char socket_address_storage_size_must_be_128[
	(sizeof(struct socket_address_storage) == 128) ? 1 : -1];
typedef char dsi_address_size_must_be_136[
	(sizeof(struct dsi_address) == 136) ? 1 : -1];
typedef char dsi_address_info_size_must_be_560[
	(sizeof(struct dsi_address_info) == 560) ? 1 : -1];

/* Values retained by Qualcomm's public DSI ABI and this stock library. */
enum {
	DSI_CALL_INFO_APN_NAME = 2,
	DSI_CALL_INFO_CALL_TYPE = 10,
	DSI_CALL_INFO_IP_VERSION = 11,
	DSI_CALL_TYPE_EMBEDDED = 1,
	DSI_EVT_NET_IS_CONN = 1,
	DSI_EVT_NET_NO_NET = 2,
	DSI_EVT_QOS_STATUS_IND = 6,
	DSI_EVT_NET_PARTIAL_CONN = 9,
	DSI_EVT_WDS_CONNECTED = 18,
};

extern int dsi_set_modem_subs_id(int subscription);
extern int dsi_init(int mode);
extern int dsi_release(int mode);
extern dsi_handle_type dsi_get_data_srvc_hndl(void *callback, void *user_data);
extern void dsi_rel_data_srvc_hndl(dsi_handle_type handle);
extern int dsi_set_data_call_param(dsi_handle_type handle, int identifier,
	struct dsi_call_param_value *value);
extern int dsi_start_data_call(dsi_handle_type handle);
extern int dsi_stop_data_call(dsi_handle_type handle);
extern int dsi_get_device_name(dsi_handle_type handle, char *buffer, int length);
extern unsigned int dsi_get_ip_addr_count(dsi_handle_type handle);
extern int dsi_get_ip_addr(dsi_handle_type handle,
	struct dsi_address_info *information, int length);
extern int dsi_get_call_end_reason(dsi_handle_type handle,
	struct dsi_call_end_reason *reason, int ip_family);
extern int dsi_get_current_data_channel_rate(dsi_handle_type handle,
	struct dsi_data_channel_rate *rate);
extern volatile unsigned char dsi_inited;

static volatile int last_event;
static volatile int connected;
static volatile int disconnected;
static const char *stop_identifier = "cellular";

static long system_call3(long number, long arg0, long arg1, long arg2)
{
	register long x0 __asm__("x0") = arg0;
	register long x1 __asm__("x1") = arg1;
	register long x2 __asm__("x2") = arg2;
	register long x8 __asm__("x8") = number;

	__asm__ volatile("svc #0"
		: "+r"(x0)
		: "r"(x1), "r"(x2), "r"(x8)
		: "memory");
	return x0;
}

static long system_call4(long number, long arg0, long arg1, long arg2, long arg3)
{
	register long x0 __asm__("x0") = arg0;
	register long x1 __asm__("x1") = arg1;
	register long x2 __asm__("x2") = arg2;
	register long x3 __asm__("x3") = arg3;
	register long x8 __asm__("x8") = number;

	__asm__ volatile("svc #0"
		: "+r"(x0)
		: "r"(x1), "r"(x2), "r"(x3), "r"(x8)
		: "memory");
	return x0;
}

static size_type string_length(const char *text)
{
	size_type length = 0;

	while (text && text[length] != '\0')
		length++;
	return length;
}

static void write_text(const char *text)
{
	size_type length = string_length(text);
	size_type offset = 0;

	while (offset < length) {
		signed_size_type result = system_call3(64, 1,
			(long)(text + offset), (long)(length - offset));
		if (result <= 0)
			return;
		offset += (size_type)result;
	}
}

static void write_number(int value)
{
	char buffer[16];
	unsigned int magnitude;
	int position = (int)sizeof(buffer);

	if (value < 0)
		magnitude = (unsigned int)(-(value + 1)) + 1;
	else
		magnitude = (unsigned int)value;

	do {
		buffer[--position] = (char)('0' + magnitude % 10);
		magnitude /= 10;
	} while (magnitude != 0);

	if (value < 0)
		buffer[--position] = '-';
	system_call3(64, 1, (long)(buffer + position),
		(long)((int)sizeof(buffer) - position));
}

static void write_unsigned_long(unsigned long value)
{
	char buffer[24];
	int position = (int)sizeof(buffer);

	do {
		buffer[--position] = (char)('0' + value % 10);
		value /= 10;
	} while (value != 0);
	system_call3(64, 1, (long)(buffer + position),
		(long)((int)sizeof(buffer) - position));
}

static void write_result(const char *name, int value)
{
	write_text(name);
	write_text("=");
	write_number(value);
	write_text("\n");
}

static void zero_memory(void *memory, size_type length)
{
	unsigned char *bytes = (unsigned char *)memory;
	size_type index;

	for (index = 0; index < length; index++)
		bytes[index] = 0;
}

static void write_hex_word(unsigned int value)
{
	char buffer[4];
	int position = (int)sizeof(buffer);

	do {
		unsigned int digit = value & 15;
		buffer[--position] = (char)(digit < 10 ? '0' + digit : 'a' + digit - 10);
		value >>= 4;
	} while (value != 0);
	system_call3(64, 1, (long)(buffer + position),
		(long)((int)sizeof(buffer) - position));
}

static void write_address_value(struct dsi_address *address)
{
	unsigned char *raw = (unsigned char *)&address->address;
	int index;

	/* Qualcomm DSI stores address bytes at sockaddr_storage.__ss_padding[0],
	 * immediately after ss_family. This is the layout used by its public
	 * dsi_netctrl example for both IPv4 and IPv6. */
	if (address->address.family == 2) {
		for (index = 2; index < 6; index++) {
			if (index != 2)
				write_text(".");
			write_number(raw[index]);
		}
	} else if (address->address.family == 10) {
		for (index = 2; index < 18; index += 2) {
			if (index != 2)
				write_text(":");
			write_hex_word(((unsigned int)raw[index] << 8) |
				raw[index + 1]);
		}
	} else {
		write_text("unsupported-family");
	}
}

static void write_ip_field(int index, const char *name,
	struct dsi_address *address)
{
	write_text("dsi_ip_");
	write_number(index);
	write_text("_");
	write_text(name);
	write_text("_valid=");
	write_number(address->valid);
	write_text("\n");
	write_text("dsi_ip_");
	write_number(index);
	write_text("_");
	write_text(name);
	write_text("_family=");
	write_number(address->address.family);
	write_text("\n");
	write_text("dsi_ip_");
	write_number(index);
	write_text("_");
	write_text(name);
	write_text("=");
	write_address_value(address);
	write_text("\n");
}

static void write_ip_information(dsi_handle_type handle)
{
	struct dsi_address_info information[4];
	unsigned int count = dsi_get_ip_addr_count(handle);
	unsigned int query_count = count > 4 ? 4 : count;
	unsigned int index;
	int result;

	write_result("dsi_get_ip_addr_count", (int)count);
	if (query_count == 0)
		return;
	zero_memory(information, sizeof(information));
	result = dsi_get_ip_addr(handle, information, (int)query_count);
	write_result("dsi_get_ip_addr", result);
	if (result != 0)
		return;
	for (index = 0; index < query_count; index++) {
		write_text("dsi_ip_");
		write_number((int)index);
		write_text("_interface_mask=");
		write_number((int)information[index].interface_mask);
		write_text("\n");
		write_text("dsi_ip_");
		write_number((int)index);
		write_text("_gateway_mask=");
		write_number((int)information[index].gateway_mask);
		write_text("\n");
		write_ip_field((int)index, "interface",
			&information[index].interface_address);
		write_ip_field((int)index, "gateway",
			&information[index].gateway_address);
		write_ip_field((int)index, "dns_primary",
			&information[index].primary_dns_address);
		write_ip_field((int)index, "dns_secondary",
			&information[index].secondary_dns_address);
	}
}

static void write_call_end_reasons(dsi_handle_type handle)
{
	struct dsi_call_end_reason reason;
	int family;
	int result;

	for (family = 0; family < 2; family++) {
		reason.reason_type = -2;
		reason.reason_code = 0;
		result = dsi_get_call_end_reason(handle, &reason, family);
		write_text(family == 0 ? "dsi_call_end_ipv4_result=" :
			"dsi_call_end_ipv6_result=");
		write_number(result);
		write_text("\n");
		write_text(family == 0 ? "dsi_call_end_ipv4_type=" :
			"dsi_call_end_ipv6_type=");
		write_number(reason.reason_type);
		write_text("\n");
		write_text(family == 0 ? "dsi_call_end_ipv4_code=" :
			"dsi_call_end_ipv6_code=");
		write_number(reason.reason_code);
		write_text("\n");
	}
}

static void write_channel_rate(dsi_handle_type handle)
{
	struct dsi_data_channel_rate rate;
	int result;

	zero_memory(&rate, sizeof(rate));
	result = dsi_get_current_data_channel_rate(handle, &rate);
	write_result("dsi_get_channel_rate", result);
	if (result != 0)
		return;
	write_text("dsi_channel_current_tx=");
	write_unsigned_long(rate.current_tx_rate);
	write_text("\n");
	write_text("dsi_channel_current_rx=");
	write_unsigned_long(rate.current_rx_rate);
	write_text("\n");
	write_text("dsi_channel_max_tx=");
	write_unsigned_long(rate.max_tx_rate);
	write_text("\n");
	write_text("dsi_channel_max_rx=");
	write_unsigned_long(rate.max_rx_rate);
	write_text("\n");
}

static void sleep_one_second(void)
{
	struct kernel_timespec delay = { 1, 0 };

	system_call3(101, (long)&delay, 0, 0);
}

static int parse_number(const char *text)
{
	int value = 0;

	if (!text || !*text)
		return -1;
	while (*text) {
		if (*text < '0' || *text > '9')
			return -1;
		value = value * 10 + (*text - '0');
		text++;
	}
	return value;
}

static int valid_identifier(const char *text)
{
	size_type index;
	size_type length = string_length(text);

	if (length == 0 || length > 32)
		return 0;
	for (index = 0; index < length; index++) {
		char value = text[index];

		if ((value >= 'A' && value <= 'Z') ||
		    (value >= 'a' && value <= 'z') ||
		    (value >= '0' && value <= '9') ||
		    value == '_' || value == '.' || value == '-')
			continue;
		return 0;
	}
	return 1;
}

static int stop_requested(void)
{
	static const char prefix[] = "/var/run/xm14pro/dsi-stop-";
	char path[sizeof(prefix) + 32];
	size_type offset;
	size_type index;
	long descriptor;

	for (offset = 0; offset < sizeof(prefix) - 1; offset++)
		path[offset] = prefix[offset];
	for (index = 0; stop_identifier[index] != '\0'; index++)
		path[offset++] = stop_identifier[index];
	path[offset] = '\0';

	/* openat(AT_FDCWD, path, O_RDONLY|O_CLOEXEC, 0) */
	descriptor = system_call4(56, -100,
		(long)path, 02000000, 0);
	if (descriptor < 0)
		return 0;
	system_call3(57, descriptor, 0, 0);
	return 1;
}

static void network_event(dsi_handle_type handle, void *user_data, int event,
	void *payload)
{
	volatile unsigned int *words = (volatile unsigned int *)payload;
	int index;

	(void)handle;
	(void)user_data;
	last_event = event;
	write_text("dsi_event=");
	write_number(event);
	write_text("\n");
	if (event == DSI_EVT_NET_IS_CONN)
		connected = 1;
	else if (event == DSI_EVT_NET_NO_NET)
		disconnected = 1;
	else if (event == DSI_EVT_QOS_STATUS_IND && words != (volatile unsigned int *)0) {
		for (index = 0; index < 4; index++) {
			write_text("dsi_qos_word_");
			write_number(index);
			write_text("=");
			write_number((int)words[index]);
			write_text("\n");
		}
	}
}

int main(int argument_count, char **argument_values)
{
	dsi_handle_type handle = (dsi_handle_type)0;
	struct dsi_call_param_value parameter;
	char device_name[32];
	const char *apn;
	int subscription;
	int ip_version;
	int hold_seconds;
	int attempt;
	int result;
	int stop_result;

	if (argument_count != 5 && argument_count != 6) {
		write_text("usage: xm14pro-dsi-dial DSI_SUBSCRIPTION APN IP_VERSION HOLD_SECONDS [STOP_IDENTIFIER]\n");
		return 2;
	}

	subscription = parse_number(argument_values[1]);
	apn = argument_values[2];
	ip_version = parse_number(argument_values[3]);
	hold_seconds = parse_number(argument_values[4]);
	if (argument_count == 6)
		stop_identifier = argument_values[5];
	if (subscription < 1 || subscription > 3 || string_length(apn) == 0 ||
		string_length(apn) > 100 ||
		(ip_version != 4 && ip_version != 6 && ip_version != 10) ||
		hold_seconds < 1 || !valid_identifier(stop_identifier)) {
		write_text("XM14_DSI_DIAL_INVALID_ARGUMENT\n");
		return 2;
	}

	write_text("XM14_DSI_DIAL_BEGIN\n");
	write_result("dsi_subscription", subscription);
	write_text("apn=");
	write_text(apn);
	write_text("\n");
	write_result("ip_version", ip_version);
	write_text("stop_identifier=");
	write_text(stop_identifier);
	write_text("\n");

	result = dsi_set_modem_subs_id(subscription);
	write_result("dsi_set_modem_subs_id", result);
	if (result != 0)
		return 10;

	result = dsi_init(0);
	write_result("dsi_init", result);
	if (result != 0)
		return 11;

	for (attempt = 1; attempt <= 120 && dsi_inited == 0; attempt++)
		sleep_one_second();
	write_result("dsi_ready_wait_seconds", attempt - 1);
	if (dsi_inited == 0) {
		write_text("XM14_DSI_DIAL_READY_TIMEOUT\n");
		dsi_release(0);
		return 12;
	}

	for (attempt = 1; attempt <= 20; attempt++) {
		handle = dsi_get_data_srvc_hndl((void *)network_event, (void *)0);
		if (handle != (dsi_handle_type)0)
			break;
		sleep_one_second();
	}
	write_result("dsi_handle_attempt", attempt);
	if (handle == (dsi_handle_type)0) {
		write_text("XM14_DSI_DIAL_HANDLE_FAILED\n");
		dsi_release(0);
		return 13;
	}

	parameter.buffer = (char *)apn;
	parameter.number = (int)string_length(apn);
	result = dsi_set_data_call_param(handle, DSI_CALL_INFO_APN_NAME,
		&parameter);
	write_result("dsi_set_apn", result);
	if (result != 0)
		goto parameter_failed;

	parameter.buffer = (char *)0;
	parameter.number = DSI_CALL_TYPE_EMBEDDED;
	result = dsi_set_data_call_param(handle, DSI_CALL_INFO_CALL_TYPE,
		&parameter);
	write_result("dsi_set_call_type_embedded", result);
	if (result != 0)
		goto parameter_failed;

	parameter.buffer = (char *)0;
	parameter.number = ip_version;
	result = dsi_set_data_call_param(handle, DSI_CALL_INFO_IP_VERSION,
		&parameter);
	write_result("dsi_set_ip_version", result);
	if (result != 0)
		goto parameter_failed;

	result = dsi_start_data_call(handle);
	write_result("dsi_start_data_call", result);
	if (result != 0) {
		write_text("XM14_DSI_DIAL_START_REJECTED\n");
		goto release_handle;
	}

	for (attempt = 1; attempt <= 120 && connected == 0 && disconnected == 0;
		attempt++)
		sleep_one_second();
	write_result("dsi_connect_wait_seconds", attempt - 1);
	write_result("dsi_last_event", last_event);
	if (connected == 0) {
		write_text(disconnected ? "XM14_DSI_DIAL_DISCONNECTED\n" :
			"XM14_DSI_DIAL_CONNECT_TIMEOUT\n");
		write_call_end_reasons(handle);
		result = 20;
		goto stop_call;
	}

	write_text("XM14_DSI_DIAL_CONNECTED\n");
	device_name[0] = '\0';
	result = dsi_get_device_name(handle, device_name, (int)sizeof(device_name));
	write_result("dsi_get_device_name", result);
	if (result == 0) {
		write_text("device_name=");
		write_text(device_name);
		write_text("\n");
	}
	write_ip_information(handle);
	write_channel_rate(handle);

	for (attempt = 0; attempt < hold_seconds && !stop_requested(); attempt++) {
		if (disconnected)
			break;
		if (attempt != 0 && (attempt % 10) == 0) {
			write_result("dsi_connected_seconds", attempt);
			write_channel_rate(handle);
		}
		sleep_one_second();
	}
	if (disconnected)
		write_call_end_reasons(handle);
	result = disconnected ? 21 : 0;

stop_call:
	/* DSI stop is asynchronous.  A failed connection has already delivered a
	 * NO_NET event, so clear that old indication and wait for the stop request's
	 * own completion before releasing the handle.  Releasing immediately leaves
	 * the first RMNET mux reserved on this phone and the next call is very slow. */
	disconnected = 0;
	stop_result = dsi_stop_data_call(handle);
	write_result("dsi_stop_data_call", stop_result);
	for (attempt = 0;
		attempt < (stop_result == 0 ? 30 : 3) && disconnected == 0;
		attempt++)
		sleep_one_second();
	write_result("dsi_stop_wait_seconds", attempt);
	goto release_handle;

parameter_failed:
	write_text("XM14_DSI_DIAL_PARAMETER_FAILED\n");
	result = 14;

release_handle:
	dsi_rel_data_srvc_hndl(handle);
	write_result("dsi_release", dsi_release(0));
	write_text("XM14_DSI_DIAL_DONE\n");
	return result;
}

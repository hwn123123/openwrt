// SPDX-License-Identifier: GPL-2.0-only
/*
 * Xiaomi 14 Pro Qualcomm DSD default-data-subscription control.
 *
 * The message identifiers, structure sizes and enum values below are taken
 * from this phone's stock OS3.0.308.0 libqcrilDataModule.so and matching
 * libqmiservices.so.  This is deliberately limited to reading and selecting
 * DDS; it does not alter SIM provisioning, APNs or radio preferences.
 */

typedef unsigned long size_type;
typedef void *qmi_client_type;
typedef void *qmi_idl_service_object_type;

enum {
	QMI_CLIENT_INSTANCE_ANY = 0xffff,
	QMI_DSD_SWITCH_DDS_REQ_V01 = 0x4e,
	QMI_DSD_GET_CURRENT_DDS_REQ_V01 = 0x4f,
	DSD_PRIMARY_SUBS_V01 = 1,
	DSD_SECONDARY_SUBS_V01 = 2,
	DSD_DDS_SWITCH_PERMANENT_V01 = 0,
};

struct qmi_response_type_v01 {
	int result;
	int error;
};

/* Stock request ABI: 20 bytes; only the first optional field is used. */
struct dsd_switch_dds_req_msg_v01 {
	int dds;
	unsigned char dds_switch_type_valid;
	unsigned char padding0[3];
	int dds_switch_type;
	unsigned char reserved[8];
};

struct dsd_switch_dds_resp_msg_v01 {
	struct qmi_response_type_v01 resp;
};

struct dsd_get_current_dds_req_msg_v01 {
	unsigned char placeholder;
};

/* Stock response ABI: validity bytes at +8/+16, values at +12/+20. */
struct dsd_get_current_dds_resp_msg_v01 {
	struct qmi_response_type_v01 resp;
	unsigned char dds_valid;
	unsigned char padding0[3];
	int dds;
	unsigned char dds_switch_type_valid;
	unsigned char padding1[3];
	int dds_switch_type;
};

typedef char switch_request_size_must_be_20[
	(sizeof(struct dsd_switch_dds_req_msg_v01) == 20) ? 1 : -1];
typedef char switch_response_size_must_be_8[
	(sizeof(struct dsd_switch_dds_resp_msg_v01) == 8) ? 1 : -1];
typedef char current_response_size_must_be_24[
	(sizeof(struct dsd_get_current_dds_resp_msg_v01) == 24) ? 1 : -1];

extern qmi_idl_service_object_type dsd_get_service_object_internal_v01(
	int major, int minor, int tool);
extern int qmi_client_init_instance(qmi_idl_service_object_type service,
	unsigned int instance, void *indication_callback,
	void *indication_data, void *os_parameters, unsigned int timeout,
	qmi_client_type *client);
extern int qmi_client_send_msg_sync(qmi_client_type client,
	unsigned int message_id, void *request, unsigned int request_length,
	void *response, unsigned int response_length, unsigned int timeout);
extern int qmi_client_release(qmi_client_type client);

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

static size_type string_length(const char *text)
{
	size_type length = 0;

	while (text && text[length] != '\0')
		length++;
	return length;
}

static void write_text(const char *text)
{
	system_call3(64, 1, (long)text, (long)string_length(text));
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

static void write_key_number(const char *key, int value)
{
	write_text(key);
	write_text("=");
	write_number(value);
	write_text("\n");
}

static void zero_memory(void *memory, size_type length)
{
	unsigned char *bytes = memory;
	size_type index;

	for (index = 0; index < length; index++)
		bytes[index] = 0;
}

static int open_dsd(qmi_client_type *client)
{
	qmi_idl_service_object_type service;
	/* Larger than qmi_cci_os_signal_type; zero initialization is required. */
	unsigned long os_parameters[64];
	int result;

	zero_memory(os_parameters, sizeof(os_parameters));
	service = dsd_get_service_object_internal_v01(1, 85, 6);
	if (!service) {
		write_text("error=dsd-service-object\n");
		return 2;
	}
	result = qmi_client_init_instance(service, QMI_CLIENT_INSTANCE_ANY,
		(void *)0, (void *)0, os_parameters, 10000, client);
	if (result != 0) {
		write_key_number("qmi_init", result);
		return 3;
	}
	return 0;
}

static int get_current_dds(qmi_client_type client, int print)
{
	struct dsd_get_current_dds_req_msg_v01 request;
	struct dsd_get_current_dds_resp_msg_v01 response;
	int result;

	zero_memory(&request, sizeof(request));
	zero_memory(&response, sizeof(response));
	result = qmi_client_send_msg_sync(client,
		QMI_DSD_GET_CURRENT_DDS_REQ_V01,
		&request, sizeof(request), &response, sizeof(response), 10000);
	if (result != 0 || response.resp.result != 0 || !response.dds_valid) {
		write_key_number("qmi_send", result);
		write_key_number("qmi_result", response.resp.result);
		write_key_number("qmi_error", response.resp.error);
		write_key_number("dds_valid", response.dds_valid);
		return -1;
	}
	if (print) {
		write_key_number("dds", response.dds);
		write_key_number("switch_type_valid",
			response.dds_switch_type_valid);
		if (response.dds_switch_type_valid)
			write_key_number("switch_type", response.dds_switch_type);
	}
	return response.dds;
}

static int set_current_dds(qmi_client_type client, int dds)
{
	struct dsd_switch_dds_req_msg_v01 request;
	struct dsd_switch_dds_resp_msg_v01 response;
	int result;

	zero_memory(&request, sizeof(request));
	zero_memory(&response, sizeof(response));
	request.dds = dds;
	request.dds_switch_type_valid = 1;
	request.dds_switch_type = DSD_DDS_SWITCH_PERMANENT_V01;
	result = qmi_client_send_msg_sync(client, QMI_DSD_SWITCH_DDS_REQ_V01,
		&request, sizeof(request), &response, sizeof(response), 10000);
	write_key_number("qmi_send", result);
	write_key_number("qmi_result", response.resp.result);
	write_key_number("qmi_error", response.resp.error);
	return (result == 0 && response.resp.result == 0) ? 0 : 5;
}

int main(int argc, char **argv)
{
	qmi_client_type client = (void *)0;
	int result;
	int requested_dds = 0;

	if (argc != 2 || !argv[1]) {
		write_text("usage: xm14pro-dds-control get|primary|secondary\n");
		return 2;
	}
	if (argv[1][0] == 'g' && argv[1][1] == 'e' && argv[1][2] == 't' &&
		argv[1][3] == '\0') {
		requested_dds = 0;
	} else if (argv[1][0] == 'p' && argv[1][1] == 'r') {
		requested_dds = DSD_PRIMARY_SUBS_V01;
	} else if (argv[1][0] == 's' && argv[1][1] == 'e') {
		requested_dds = DSD_SECONDARY_SUBS_V01;
	} else {
		write_text("error=invalid-command\n");
		return 2;
	}

	result = open_dsd(&client);
	if (result != 0)
		return result;
	if (requested_dds == 0) {
		result = get_current_dds(client, 1) < 0 ? 4 : 0;
	} else {
		int before = get_current_dds(client, 0);
		write_key_number("before", before);
		if (before == requested_dds)
			result = 0;
		else
			result = set_current_dds(client, requested_dds);
		if (result == 0)
			write_key_number("after", get_current_dds(client, 0));
	}
	qmi_client_release(client);
	return result;
}

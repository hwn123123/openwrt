// SPDX-License-Identifier: GPL-2.0-only
/*
 * Minimal Bionic-side probe for Xiaomi's stock DSI/NICM data path.
 *
 * This intentionally does not call dsi_start_data_call(): it only proves that
 * libdsi_netctrl can initialize and exchange the client registration required
 * to obtain a DSI handle from the stock nicmd runtime.
 */

typedef unsigned long size_type;
typedef long signed_size_type;
typedef void *dsi_handle_type;

struct kernel_timespec {
	long seconds;
	long nanoseconds;
};

extern int dsi_init(int mode);
extern int dsi_release(int mode);
extern dsi_handle_type dsi_get_data_srvc_hndl(void *callback, void *user_data);
extern void dsi_rel_data_srvc_hndl(dsi_handle_type handle);
extern volatile unsigned char dsi_inited;
extern volatile int is_nicm_client_init;
extern void *volatile nicm_clnt_hndl;

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

	while (text[length] != '\0')
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

static void sleep_half_second(void)
{
	struct kernel_timespec delay = { 0, 500000000 };

	system_call3(101, (long)&delay, 0, 0);
}

static void write_state(int second)
{
	write_text("dsi_state_second=");
	write_number(second);
	write_text(" dsi_inited=");
	write_number((int)dsi_inited);
	write_text(" nicm_client_init=");
	write_number(is_nicm_client_init);
	write_text(" nicm_handle=");
	write_number(nicm_clnt_hndl != (void *)0);
	write_text("\n");
}

int main(int argument_count, char **argument_values)
{
	dsi_handle_type handle = (dsi_handle_type)0;
	int attempt;
	int result;

	(void)argument_count;
	(void)argument_values;
	write_text("XM14_DSI_PROBE_BEGIN\n");
	result = dsi_init(0);
	write_text("dsi_init=");
	write_number(result);
	write_text("\n");
	if (result != 0) {
		write_text("XM14_DSI_PROBE_INIT_FAILED\n");
		return 10;
	}

	write_state(0);
	for (attempt = 1; attempt <= 240; attempt++) {
		sleep_half_second();
		if ((attempt % 10) == 0 || dsi_inited != 0)
			write_state(attempt / 2);
		if (dsi_inited != 0)
			break;
	}
	if (dsi_inited == 0) {
		write_text("XM14_DSI_PROBE_READY_TIMEOUT\n");
		dsi_release(0);
		return 13;
	}

	for (attempt = 1; attempt <= 20; attempt++) {
		handle = dsi_get_data_srvc_hndl((void *)0, (void *)0);
		if (handle != (dsi_handle_type)0)
			break;
		sleep_half_second();
	}

	write_text("dsi_handle_attempt=");
	write_number(attempt);
	write_text("\n");
	if (handle == (dsi_handle_type)0) {
		write_text("XM14_DSI_PROBE_HANDLE_FAILED\n");
		dsi_release(0);
		return 11;
	}

	write_text("XM14_DSI_PROBE_HANDLE_READY\n");
	dsi_rel_data_srvc_hndl(handle);
	result = dsi_release(0);
	write_text("dsi_release=");
	write_number(result);
	write_text("\nXM14_DSI_PROBE_DONE\n");
	return result == 0 ? 0 : 12;
}

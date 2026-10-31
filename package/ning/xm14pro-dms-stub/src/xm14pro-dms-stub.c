// SPDX-License-Identifier: GPL-2.0-only
#include <errno.h>
#include <libqrtr.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define DMS_SERVICE_ID 0x02
#define DMS_SERVICE_VERSION 0x01
#define DMS_SERVICE_INSTANCE 0
#define DMS_GET_MAC_ADDRESS 0x005c
#define WLAN_MAC_LEN 6

struct qmi_header {
	uint8_t type;
	uint16_t txn_id;
	uint16_t msg_id;
	uint16_t msg_len;
} __attribute__((packed));

static volatile sig_atomic_t running = 1;

static void stop_handler(int signo)
{
	(void)signo;
	running = 0;
}

static int parse_mac(const char *text, uint8_t mac[WLAN_MAC_LEN])
{
	unsigned int value[WLAN_MAC_LEN];
	int i;

	if (sscanf(text, "%2x:%2x:%2x:%2x:%2x:%2x",
		   &value[0], &value[1], &value[2], &value[3],
		   &value[4], &value[5]) != WLAN_MAC_LEN)
		return -1;

	for (i = 0; i < WLAN_MAC_LEN; i++)
		mac[i] = (uint8_t)value[i];

	if ((mac[0] & 1) != 0)
		return -1;

	return 0;
}

static size_t make_response(uint8_t *out, size_t out_size,
			    const struct qmi_header *request,
			    const uint8_t mac[WLAN_MAC_LEN])
{
	struct qmi_header *response;
	uint8_t *p;
	uint16_t value;
	const uint16_t payload_len = 17;

	if (out_size < sizeof(*response) + payload_len)
		return 0;

	response = (struct qmi_header *)out;
	response->type = QMI_RESPONSE;
	response->txn_id = request->txn_id;
	response->msg_id = request->msg_id;
	response->msg_len = payload_len;
	p = out + sizeof(*response);

	/* Mandatory result TLV: success, no error. */
	*p++ = 0x02;
	value = 4;
	memcpy(p, &value, sizeof(value));
	p += sizeof(value);
	value = QMI_RESULT_SUCCESS_V01;
	memcpy(p, &value, sizeof(value));
	p += sizeof(value);
	value = QMI_ERR_NONE_V01;
	memcpy(p, &value, sizeof(value));
	p += sizeof(value);

	/* Optional MAC TLV: one-byte element count followed by six bytes. */
	*p++ = 0x10;
	value = WLAN_MAC_LEN + 1;
	memcpy(p, &value, sizeof(value));
	p += sizeof(value);
	*p++ = WLAN_MAC_LEN;
	memcpy(p, mac, WLAN_MAC_LEN);
	p += WLAN_MAC_LEN;

	return (size_t)(p - out);
}

int main(int argc, char **argv)
{
	uint8_t mac[WLAN_MAC_LEN] = { 0x02, 0x14, 0x14, 0x14, 0x00, 0x01 };
	uint8_t request_buf[256];
	uint8_t response_buf[64];
	uint32_t node, port;
	ssize_t len;
	int sock, opt;

	while ((opt = getopt(argc, argv, "m:h")) != -1) {
		switch (opt) {
		case 'm':
			if (parse_mac(optarg, mac) < 0) {
				fprintf(stderr, "invalid unicast MAC: %s\n", optarg);
				return 2;
			}
			break;
		default:
			fprintf(stderr, "usage: %s [-m xx:xx:xx:xx:xx:xx]\n", argv[0]);
			return opt == 'h' ? 0 : 2;
		}
	}

	signal(SIGINT, stop_handler);
	signal(SIGTERM, stop_handler);

	sock = qrtr_open(0);
	if (sock < 0) {
		fprintf(stderr, "failed to open QRTR socket\n");
		return 1;
	}

	if (qrtr_publish(sock, DMS_SERVICE_ID, DMS_SERVICE_VERSION,
			 DMS_SERVICE_INSTANCE) < 0) {
		fprintf(stderr, "failed to publish QMI DMS service\n");
		qrtr_close(sock);
		return 1;
	}

	fprintf(stderr, "DMS WLAN MAC service ready: %02x:%02x:%02x:%02x:%02x:%02x\n",
		mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

	while (running) {
		const struct qmi_header *request;
		size_t response_len;
		int ready;

		ready = qrtr_poll(sock, 1000);
		if (ready < 0) {
			if (errno == EINTR)
				continue;
			perror("qrtr_poll");
			break;
		}
		if (ready == 0)
			continue;

		len = qrtr_recvfrom(sock, request_buf, sizeof(request_buf),
				    &node, &port);
		if (len < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
				continue;
			perror("qrtr_recvfrom");
			break;
		}
		if ((size_t)len < sizeof(*request))
			continue;

		request = (const struct qmi_header *)request_buf;
		if (request->type != QMI_REQUEST ||
		    request->msg_id != DMS_GET_MAC_ADDRESS)
			continue;

		response_len = make_response(response_buf, sizeof(response_buf),
					     request, mac);
		if (!response_len ||
		    qrtr_sendto(sock, node, port, response_buf, response_len) < 0)
			fprintf(stderr, "failed to send DMS MAC response\n");
		else
			fprintf(stderr, "served WLAN MAC request from %u:%u\n", node, port);
	}

	qrtr_remove_server(sock, DMS_SERVICE_ID, DMS_SERVICE_VERSION,
			   DMS_SERVICE_INSTANCE);
	qrtr_close(sock);
	return 0;
}

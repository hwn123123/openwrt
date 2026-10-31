// SPDX-License-Identifier: Apache-2.0

#include <android/binder_auto_utils.h>
#include <android/binder_ibinder.h>
#include <android/binder_manager.h>
#include <android/binder_parcel.h>
#include <android/binder_stability.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

struct LeakedStrongPointer {
	void *pointer;
	~LeakedStrongPointer() { pointer = nullptr; }
};

extern "C" LeakedStrongPointer init_process_state(const char *driver)
	__asm__("_ZN7android12ProcessState14initWithDriverEPKc");

static void *class_create(void *args)
{
	return args;
}

static void class_destroy(void *)
{
}

static binder_status_t class_transact_unused(AIBinder *, transaction_code_t,
					       const AParcel *, AParcel *)
{
	return STATUS_UNKNOWN_TRANSACTION;
}

static ndk::SpAIBinder find_service(const char *service_name,
				    const char *descriptor)
{
	AIBinder_Class *clazz = AIBinder_Class_define(
		descriptor, class_create, class_destroy, class_transact_unused);
	if (!clazz) {
		std::fprintf(stderr, "cannot define Binder class for %s\n", descriptor);
		return {};
	}

	ndk::SpAIBinder service(AServiceManager_checkService(service_name));
	if (!service.get()) {
		std::fprintf(stderr, "service is unavailable: %s\n", service_name);
		return {};
	}
	if (AIBinder_ping(service.get()) != STATUS_OK) {
		std::fprintf(stderr, "service ping failed: %s\n", service_name);
		return {};
	}
	if (!AIBinder_associateClass(service.get(), clazz)) {
		std::fprintf(stderr, "descriptor mismatch: %s\n", service_name);
		return {};
	}
	return service;
}

static int read_remote_status(const char *service_name, const AParcel *reply)
{
	ndk::ScopedAStatus remote_status;
	binder_status_t status =
		AParcel_readStatusHeader(reply, remote_status.getR());
	if (status != STATUS_OK) {
		std::fprintf(stderr, "cannot read reply for %s: %d\n", service_name,
			     status);
		return 7;
	}
	if (!remote_status.isOk()) {
		std::fprintf(stderr, "remote method failed for %s: %s\n", service_name,
			     remote_status.getDescription().c_str());
		return 8;
	}
	return 0;
}

static int probe_method(const char *service_name, const char *descriptor,
			transaction_code_t code)
{
	ndk::SpAIBinder service = find_service(service_name, descriptor);
	if (!service.get())
		return 3;

	/* A Binder ping is the safe readiness test for NFC. Calling getConfig
	 * before Android's NFC stack opens the HAL can block on this firmware. */
	if (code == 0) {
		std::printf("{\"service\":\"%s\",\"registered\":true,"
			    "\"ping\":true}\n", service_name);
		return 0;
	}

	ndk::ScopedAParcel request;
	ndk::ScopedAParcel reply;
	binder_status_t status =
		AIBinder_prepareTransaction(service.get(), request.getR());
	if (status == STATUS_OK)
		status = AIBinder_transact(service.get(), code, request.getR(),
					    reply.getR(), FLAG_PRIVATE_LOCAL);
	if (status != STATUS_OK) {
		std::fprintf(stderr, "transaction %u failed for %s: %d\n", code,
			     service_name, status);
		return 6;
	}

	int result = read_remote_status(service_name, reply.get());
	if (result != 0)
		return result;

	std::printf("{\"service\":\"%s\",\"registered\":true,"
		    "\"ping\":true,\"method\":%u,\"method_ok\":true}\n",
		    service_name, code);
	return 0;
}

static int infrared_transmit(int carrier_hz, const char *pattern_text)
{
	static constexpr const char *service_name =
		"android.hardware.ir.IConsumerIr/default";
	ndk::SpAIBinder service = find_service(
		service_name, "android.hardware.ir.IConsumerIr");
	if (!service.get())
		return 3;
	if (carrier_hz < 20000 || carrier_hz > 60000) {
		std::fprintf(stderr, "carrier must be between 20000 and 60000 Hz\n");
		return 9;
	}

	char *copy = ::strdup(pattern_text ? pattern_text : "");
	if (!copy)
		return 9;
	std::vector<int32_t> pattern;
	long long total_us = 0;
	char *save = nullptr;
	for (char *item = ::strtok_r(copy, ",", &save); item;
	     item = ::strtok_r(nullptr, ",", &save)) {
		char *end = nullptr;
		long value = std::strtol(item, &end, 10);
		while (end && (*end == ' ' || *end == '\t')) ++end;
		if (!end || *end || value <= 0 || value > 1000000 ||
		    pattern.size() >= 512) {
			std::free(copy);
			std::fprintf(stderr, "invalid infrared pattern\n");
			return 9;
		}
		pattern.push_back(static_cast<int32_t>(value));
		total_us += value;
	}
	std::free(copy);
	if (pattern.size() < 2 || (pattern.size() % 2) != 0 || total_us > 2000000) {
		std::fprintf(stderr,
			     "pattern must contain 2-512 mark/space values and last at most 2 seconds\n");
		return 9;
	}

	ndk::ScopedAParcel request;
	ndk::ScopedAParcel reply;
	binder_status_t status =
		AIBinder_prepareTransaction(service.get(), request.getR());
	if (status == STATUS_OK)
		status = AParcel_writeInt32(request.get(), carrier_hz);
	if (status == STATUS_OK)
		status = AParcel_writeInt32Array(request.get(), pattern.data(),
						 static_cast<int32_t>(pattern.size()));
	if (status == STATUS_OK)
		status = AIBinder_transact(service.get(), FIRST_CALL_TRANSACTION + 1,
					    request.getR(), reply.getR(), FLAG_PRIVATE_LOCAL);
	if (status != STATUS_OK) {
		std::fprintf(stderr, "infrared transmit failed: %d\n", status);
		return 6;
	}
	int result = read_remote_status(service_name, reply.get());
	if (result != 0)
		return result;
	std::printf("{\"ok\":true,\"carrier_hz\":%d,\"pulses\":%zu,"
		    "\"duration_us\":%lld}\n", carrier_hz, pattern.size(), total_us);
	return 0;
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		std::fprintf(stderr, "usage: %s nfc-probe|infrared-probe|infrared-transmit ...\n", argv[0]);
		return 2;
	}

	LeakedStrongPointer process_state = init_process_state("/dev/binder");
	if (!process_state.pointer) {
		std::fprintf(stderr, "cannot initialize libbinder on /dev/binder\n");
		return 2;
	}

	if (std::strcmp(argv[1], "nfc-probe") == 0 && argc == 2) {
		/* Do not open or configure NFC behind Android's upper NFC stack. */
		return probe_method("android.hardware.nfc.INfc/default",
				    "android.hardware.nfc.INfc",
				    0);
	}
	if (std::strcmp(argv[1], "infrared-probe") == 0 && argc == 2) {
		/* Frozen android.hardware.ir.IConsumerIr V1: getCarrierFreqs. */
		return probe_method("android.hardware.ir.IConsumerIr/default",
				    "android.hardware.ir.IConsumerIr",
				    FIRST_CALL_TRANSACTION);
	}
	if (std::strcmp(argv[1], "infrared-transmit") == 0 && argc == 4)
		return infrared_transmit(std::atoi(argv[2]), argv[3]);

	std::fprintf(stderr, "unknown command: %s\n", argv[1]);
	return 2;
}

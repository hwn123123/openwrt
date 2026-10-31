// SPDX-License-Identifier: Apache-2.0

#include <android/binder_auto_utils.h>
#include <android/binder_ibinder.h>
#include <android/binder_manager.h>
#include <android/binder_parcel.h>
#include <android/binder_process.h>
#include <android/binder_stability.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <cstdio>
#include <thread>

/*
 * The stock Qualcomm IUsb service is not self-starting: Android's
 * UsbPortManager registers IUsbCallback and only then does the HAL create its
 * uevent/bus-reset worker.  nwrt deliberately does not run Android framework,
 * so this process owns only that missing callback boundary.  It never changes
 * roles, data policy, power policy or the ConfigFS gadget.
 */

struct LeakedStrongPointer {
	void *pointer;
	~LeakedStrongPointer() { pointer = nullptr; }
};

extern "C" LeakedStrongPointer init_process_state(const char *driver)
	__asm__("_ZN7android12ProcessState14initWithDriverEPKc");

static std::atomic<bool> service_dead{false};

static void *callback_create(void *args)
{
	return args;
}

static void callback_destroy(void *)
{
}

static binder_status_t service_transact_unused(AIBinder *, transaction_code_t,
						const AParcel *, AParcel *)
{
	return STATUS_UNKNOWN_TRANSACTION;
}

static binder_status_t callback_transact(AIBinder *, transaction_code_t code,
					 const AParcel *, AParcel *)
{
	/* All frozen IUsbCallback V1 notifications are one-way.  The real values
	 * remain owned by the HAL and sysfs; consuming a notification is enough to
	 * keep the stock worker and callback lifetime identical to Android. */
	std::fprintf(stderr, "IUsbCallback notification code=%u\n", code);
	return STATUS_OK;
}

static void service_died(void *)
{
	service_dead.store(true);
}

static bool publish_ready_for_this_boot()
{
	static constexpr const char *ready_path =
		"/data/vendor/xm14pro-usb/framework.ready";
	char boot_id[80] = {};
	FILE *source = std::fopen("/proc/sys/kernel/random/boot_id", "r");
	if (!source || !std::fgets(boot_id, sizeof(boot_id), source)) {
		if (source)
			std::fclose(source);
		return false;
	}
	std::fclose(source);
	boot_id[std::strcspn(boot_id, "\r\n")] = '\0';

	FILE *ready = std::fopen(ready_path, "w");
	if (!ready)
		return false;
	const bool written = std::fprintf(ready, "%s\n", boot_id) > 0;
	const bool closed = std::fclose(ready) == 0;
	return written && closed;
}

static binder_status_t send_oneway(AIBinder *service, transaction_code_t code,
				   AParcel **in)
{
	ndk::ScopedAParcel out;
	return AIBinder_transact(service, code, in, out.getR(),
		FLAG_PRIVATE_LOCAL | FLAG_ONEWAY);
}

static binder_status_t register_callback(AIBinder *service, AIBinder *callback)
{
	ndk::ScopedAParcel in;
	binder_status_t status = AIBinder_prepareTransaction(service, in.getR());
	if (status == STATUS_OK)
		status = AParcel_writeStrongBinder(in.get(), callback);
	if (status == STATUS_OK)
		/* Frozen android.hardware.usb.IUsb V1: setCallback. */
		status = send_oneway(service, 5, in.getR());
	return status;
}

static binder_status_t query_port_status(AIBinder *service)
{
	ndk::ScopedAParcel in;
	binder_status_t status = AIBinder_prepareTransaction(service, in.getR());
	if (status == STATUS_OK)
		status = AParcel_writeInt64(in.get(), 1);
	if (status == STATUS_OK)
		/* Frozen android.hardware.usb.IUsb V1: queryPortStatus. */
		status = send_oneway(service, 4, in.getR());
	return status;
}

int main()
{
	static constexpr const char *service_name =
		"android.hardware.usb.IUsb/default";
	static constexpr const char *service_descriptor =
		"android.hardware.usb.IUsb";
	static constexpr const char *callback_descriptor =
		"android.hardware.usb.IUsbCallback";
	LeakedStrongPointer process_state = init_process_state("/dev/binder");

	if (!process_state.pointer) {
		std::fprintf(stderr, "cannot initialize libbinder on /dev/binder\n");
		return 2;
	}
	AIBinder_Class *service_class = AIBinder_Class_define(
		service_descriptor, callback_create, callback_destroy,
		service_transact_unused);
	AIBinder_Class *callback_class = AIBinder_Class_define(
		callback_descriptor, callback_create, callback_destroy,
		callback_transact);
	if (!service_class || !callback_class) {
		std::fprintf(stderr, "cannot define IUsb Binder classes\n");
		return 2;
	}
	ndk::SpAIBinder callback(AIBinder_new(callback_class, nullptr));
	if (!callback.get()) {
		std::fprintf(stderr, "cannot create IUsbCallback Binder object\n");
		return 2;
	}
	AIBinder_markVintfStability(callback.get());
	ABinderProcess_setThreadPoolMaxThreadCount(1);
	ABinderProcess_startThreadPool();

	ndk::SpAIBinder service;
	for (;;) {
		service = ndk::SpAIBinder(AServiceManager_checkService(service_name));
		if (service.get())
			break;
		std::this_thread::sleep_for(std::chrono::seconds(1));
	}
	if (!AIBinder_associateClass(service.get(), service_class)) {
		std::fprintf(stderr, "cannot associate stock IUsb Binder class\n");
		return 2;
	}

	ndk::ScopedAIBinder_DeathRecipient death(
		AIBinder_DeathRecipient_new(service_died));
	if (!death.get() || AIBinder_linkToDeath(service.get(), death.get(), nullptr) !=
		STATUS_OK) {
		std::fprintf(stderr, "cannot monitor stock IUsb service death\n");
		return 2;
	}
	binder_status_t status = register_callback(service.get(), callback.get());
	if (status != STATUS_OK) {
		std::fprintf(stderr, "IUsb setCallback failed: %d\n", status);
		return 2;
	}
	status = query_port_status(service.get());
	if (status != STATUS_OK) {
		std::fprintf(stderr, "IUsb queryPortStatus failed: %d\n", status);
		return 2;
	}
	if (!publish_ready_for_this_boot()) {
		std::fprintf(stderr, "cannot publish IUsb callback readiness\n");
		return 2;
	}
	std::fprintf(stderr, "stock IUsb callback registered; port worker active\n");

	while (!service_dead.load())
		std::this_thread::sleep_for(std::chrono::seconds(1));
	std::fprintf(stderr, "stock IUsb service died; requesting re-registration\n");
	return 1;
}

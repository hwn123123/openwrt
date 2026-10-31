// SPDX-License-Identifier: Apache-2.0

#include <android/binder_auto_utils.h>
#include <android/binder_ibinder.h>
#include <android/binder_manager.h>
#include <android/binder_parcel.h>
#include <android/binder_process.h>
#include <android/binder_stability.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <unistd.h>

/*
 * QCRIL publishes the standard Radio stable-AIDL services, but unlike Android
 * nwrt has no com.android.phone process which attaches the long-lived response
 * and indication objects.  Merely finding the services is not equivalent to
 * initializing the framework side of the Radio HAL.  This deliberately small
 * client reproduces only that ownership boundary.  It does not set radio
 * power, select a SIM, establish a data call, change an APN, or send a message.
 */

struct LeakedStrongPointer {
	void *pointer;
	~LeakedStrongPointer() { pointer = nullptr; }
};

extern "C" LeakedStrongPointer init_process_state(const char *driver)
	__asm__("_ZN7android12ProcessState14initWithDriverEPKc");

struct Domain {
	const char *name;
	const char *service_descriptor;
	const char *response_descriptor;
	const char *indication_descriptor;
	transaction_code_t set_response_functions;
	transaction_code_t response_acknowledgement;
	AIBinder_Class *service_class = nullptr;
	AIBinder_Class *response_class = nullptr;
	AIBinder_Class *indication_class = nullptr;
};

static Domain domains[] = {
	{
		"modem",
		"android.hardware.radio.modem.IRadioModem",
		"android.hardware.radio.modem.IRadioModemResponse",
		"android.hardware.radio.modem.IRadioModemIndication",
		17,
		13,
	},
	{
		"sim",
		"android.hardware.radio.sim.IRadioSim",
		"android.hardware.radio.sim.IRadioSimResponse",
		"android.hardware.radio.sim.IRadioSimIndication",
		28,
		20,
	},
	{
		"network",
		"android.hardware.radio.network.IRadioNetwork",
		"android.hardware.radio.network.IRadioNetworkResponse",
		"android.hardware.radio.network.IRadioNetworkIndication",
		28,
		16,
	},
	{
		"data",
		"android.hardware.radio.data.IRadioData",
		"android.hardware.radio.data.IRadioDataResponse",
		"android.hardware.radio.data.IRadioDataIndication",
		12,
		7,
	},
	{
		"messaging",
		"android.hardware.radio.messaging.IRadioMessaging",
		"android.hardware.radio.messaging.IRadioMessagingResponse",
		"android.hardware.radio.messaging.IRadioMessagingIndication",
		20,
		10,
	},
	{
		"voice",
		"android.hardware.radio.voice.IRadioVoice",
		"android.hardware.radio.voice.IRadioVoiceResponse",
		"android.hardware.radio.voice.IRadioVoiceIndication",
		34,
		23,
	},
};

static Domain config_domain = {
	"config",
	"android.hardware.radio.config.IRadioConfig",
	"android.hardware.radio.config.IRadioConfigResponse",
	"android.hardware.radio.config.IRadioConfigIndication",
	7,
	0,
};

struct Registration {
	Domain *domain;
	std::string instance;
	ndk::SpAIBinder service;
	ndk::SpAIBinder response;
	ndk::SpAIBinder indication;
};

static void *binder_state_create(void *args)
{
	return args;
}

static void binder_state_destroy(void *)
{
}

static binder_status_t service_transact_unused(AIBinder *, transaction_code_t,
						const AParcel *, AParcel *)
{
	return STATUS_UNKNOWN_TRANSACTION;
}

static binder_status_t response_transact(AIBinder *, transaction_code_t,
					 const AParcel *, AParcel *)
{
	/*
	 * This owner does not issue standard Radio requests, so it cannot have a
	 * legitimate solicited response to acknowledge. Keep the endpoint alive
	 * without fabricating a framework response.
	 */
	return STATUS_OK;
}

static void acknowledge_indication(Registration *registration)
{
	if (!registration || !registration->service.get() ||
	    registration->domain->response_acknowledgement == 0)
		return;

	ndk::ScopedAParcel in;
	ndk::ScopedAParcel out;
	binder_status_t status = AIBinder_prepareTransaction(
		registration->service.get(), in.getR());
	if (status == STATUS_OK)
		status = AIBinder_transact(registration->service.get(),
			registration->domain->response_acknowledgement,
			in.getR(), out.getR(), FLAG_PRIVATE_LOCAL | FLAG_ONEWAY);
	if (status == STATUS_OK)
		std::fprintf(stderr, "acknowledged Radio indication on %s\n",
			registration->instance.c_str());
	else
		std::fprintf(stderr, "Radio indication acknowledgement failed on %s: %d\n",
			registration->instance.c_str(), status);
}

static binder_status_t indication_transact(AIBinder *binder,
					    transaction_code_t,
					    const AParcel *in, AParcel *)
{
	/* Every standard Radio indication begins with RadioIndicationType. */
	int32_t type = 0;
	binder_status_t status = AParcel_readInt32(in, &type);
	if (status != STATUS_OK)
		return status;
	/* RadioIndicationType::UNSOLICITED_ACK_EXP is frozen AIDL value 1. */
	if (type == 1)
		acknowledge_indication(static_cast<Registration *>(
			AIBinder_getUserData(binder)));
	return STATUS_OK;
}

static bool prepare_classes(Domain *domain)
{
	domain->service_class = AIBinder_Class_define(domain->service_descriptor,
		binder_state_create, binder_state_destroy, service_transact_unused);
	domain->response_class = AIBinder_Class_define(domain->response_descriptor,
		binder_state_create, binder_state_destroy, response_transact);
	domain->indication_class = AIBinder_Class_define(domain->indication_descriptor,
		binder_state_create, binder_state_destroy, indication_transact);
	return domain->service_class && domain->response_class &&
		domain->indication_class;
}

static bool attach_callbacks(Registration *registration, std::string *error)
{
	Domain *domain = registration->domain;
	registration->response = ndk::SpAIBinder(
		AIBinder_new(domain->response_class, registration));
	registration->indication = ndk::SpAIBinder(
		AIBinder_new(domain->indication_class, registration));
	if (!registration->response.get() || !registration->indication.get()) {
		*error = "cannot create callbacks for " + registration->instance;
		return false;
	}
	AIBinder_markVintfStability(registration->response.get());
	AIBinder_markVintfStability(registration->indication.get());

	ndk::ScopedAParcel in;
	ndk::ScopedAParcel out;
	binder_status_t status = AIBinder_prepareTransaction(
		registration->service.get(), in.getR());
	if (status == STATUS_OK)
		status = AParcel_writeStrongBinder(in.get(),
			registration->response.get());
	if (status == STATUS_OK)
		status = AParcel_writeStrongBinder(in.get(),
			registration->indication.get());
	if (status == STATUS_OK)
		status = AIBinder_transact(registration->service.get(),
			domain->set_response_functions, in.getR(), out.getR(),
			FLAG_PRIVATE_LOCAL | FLAG_ONEWAY);
	if (status == STATUS_OK)
		return true;
	*error = "setResponseFunctions failed for " + registration->instance +
		": " + std::to_string(status);
	return false;
}

static std::string boot_id()
{
	char value[80] = {};
	FILE *file = std::fopen("/proc/sys/kernel/random/boot_id", "r");
	if (!file)
		return {};
	if (!std::fgets(value, sizeof(value), file))
		value[0] = '\0';
	std::fclose(file);
	value[std::strcspn(value, "\r\n")] = '\0';
	return value;
}

static void write_state(bool ready, size_t count, const std::string& error)
{
	static constexpr const char *path =
		"/data/vendor/radio/xm14pro-radio-framework.state";
	static constexpr const char *temporary =
		"/data/vendor/radio/xm14pro-radio-framework.state.tmp";
	FILE *file = std::fopen(temporary, "w");
	if (!file)
		return;
	std::fprintf(file,
		"{\"ready\":%s,\"registered\":%zu,\"expected\":13,"
		"\"boot_id\":\"%s\",\"error\":\"%s\"}\n",
		ready ? "true" : "false", count, boot_id().c_str(), error.c_str());
	if (std::fclose(file) == 0)
		rename(temporary, path);
}

static std::vector<Registration> make_registrations()
{
	std::vector<Registration> registrations;
	/* Callback private data points into this vector, so it must never move. */
	registrations.reserve(13);
	for (Domain& domain : domains) {
		for (const char *slot : { "slot1", "slot2" }) {
			Registration registration;
			registration.domain = &domain;
			registration.instance = std::string(domain.service_descriptor) +
				"/" + slot;
			registrations.push_back(std::move(registration));
		}
	}
	Registration config;
	config.domain = &config_domain;
	config.instance = std::string(config_domain.service_descriptor) +
		"/default";
	registrations.push_back(std::move(config));
	return registrations;
}

int main()
{
	LeakedStrongPointer process_state = init_process_state("/dev/binder");
	if (!process_state.pointer) {
		std::fprintf(stderr, "cannot initialize libbinder on /dev/binder\n");
		return 2;
	}
	ABinderProcess_setThreadPoolMaxThreadCount(4);
	ABinderProcess_startThreadPool();

	for (Domain& domain : domains) {
		if (!prepare_classes(&domain)) {
			std::fprintf(stderr, "cannot define %s Binder classes\n",
				domain.name);
			return 2;
		}
	}
	if (!prepare_classes(&config_domain)) {
		std::fprintf(stderr, "cannot define config Binder classes\n");
		return 2;
	}

	std::vector<Registration> registrations = make_registrations();
	std::string detail;
	size_t attached = 0;
	for (int attempt = 0; attempt < 300 && attached < registrations.size();
	     ++attempt) {
		detail.clear();
		for (Registration& registration : registrations) {
			if (registration.response.get())
				continue;
			if (!registration.service.get()) {
				registration.service = ndk::SpAIBinder(
					AServiceManager_checkService(
						registration.instance.c_str()));
			}
			if (!registration.service.get()) {
				if (detail.empty())
					detail = "waiting for " + registration.instance;
				continue;
			}
			if (!AIBinder_associateClass(registration.service.get(),
				registration.domain->service_class)) {
				detail = "cannot associate descriptor for " +
					registration.instance;
				write_state(false, attached, detail);
				std::fprintf(stderr, "%s\n", detail.c_str());
				return 1;
			}
			if (!attach_callbacks(&registration, &detail)) {
				write_state(false, attached, detail);
				std::fprintf(stderr, "%s\n", detail.c_str());
				return 1;
			}
			++attached;
			std::printf("attached %s\n", registration.instance.c_str());
			std::fflush(stdout);
		}
		write_state(attached == registrations.size(), attached, detail);
		if (attached != registrations.size())
			std::this_thread::sleep_for(std::chrono::seconds(1));
	}
	if (attached != registrations.size()) {
		detail = "Radio services timed out after incremental registration";
		write_state(false, attached, detail);
		std::fprintf(stderr, "%s (%zu/%zu)\n", detail.c_str(), attached,
			registrations.size());
		return 1;
	}
	std::printf("all %zu Radio callback pairs are attached\n", attached);
	std::fflush(stdout);

	/* The callback objects must remain alive for the entire nwrt boot. */
	for (;;)
		std::this_thread::sleep_for(std::chrono::hours(24));
}

// SPDX-License-Identifier: Apache-2.0

#include <aidl/android/hardware/radio/RadioError.h>
#include <aidl/android/hardware/radio/RadioIndicationType.h>
#include <aidl/android/hardware/radio/RadioResponseInfo.h>
#include <aidl/android/hardware/radio/RadioResponseType.h>
#include <aidl/android/hardware/radio/RadioTechnologyFamily.h>
#include <aidl/android/hardware/radio/messaging/BnRadioMessagingIndication.h>
#include <aidl/android/hardware/radio/messaging/BnRadioMessagingResponse.h>
#include <aidl/android/hardware/radio/messaging/IRadioMessaging.h>
#include <aidl/android/hardware/radio/messaging/ImsSmsMessage.h>

#include <android/binder_auto_utils.h>
#include <android/binder_ibinder.h>
#include <android/binder_manager.h>
#include <android/binder_parcel.h>
#include <android/binder_parcel_utils.h>
#include <android/binder_process.h>
#include <android/binder_stability.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#if !defined(__ANDROID_VENDOR__)
extern "C" void AIBinder_markVendorStability(AIBinder *binder);
#endif

namespace radio = aidl::android::hardware::radio;
namespace messaging = aidl::android::hardware::radio::messaging;

/*
 * Stable-AIDL radio services live on /dev/binder.  Keep the same explicit
 * driver selection used by xm14pro-radio-probe; otherwise a process launched
 * in the vendor compatibility root may inherit the vendor Binder driver.
 */
struct LeakedStrongPointer {
	void *pointer;
	~LeakedStrongPointer() { pointer = nullptr; }
};

extern "C" LeakedStrongPointer init_process_state(const char *driver)
	__asm__("_ZN7android12ProcessState14initWithDriverEPKc");

struct SmsReply {
	radio::RadioResponseInfo info;
	messaging::SendSmsResult result;
	std::string smsc;
};

/*
 * HyperOS does not send IMS SMS through the platform
 * android.hardware.radio.messaging interface.  Its ImsService talks to the
 * Qualcomm vendor Stable-AIDL service below.  The interface is proprietary,
 * so keep this client deliberately small and encode only the two transactions
 * used by Android's real send path:
 *
 *   IImsRadio.setCallback()  transaction 1
 *   IImsRadio.sendSms()      transaction 41
 *
 * The transaction numbers and parcel layouts come from the exact
 * vendor.qti.hardware.radio.ims-V12-ndk.so shipped by this device.
 */
static constexpr const char *kImsRadioDescriptor =
	"vendor.qti.hardware.radio.ims.IImsRadio";
static constexpr const char *kImsResponseDescriptor =
	"vendor.qti.hardware.radio.ims.IImsRadioResponse";
static constexpr const char *kImsIndicationDescriptor =
	"vendor.qti.hardware.radio.ims.IImsRadioIndication";
static constexpr transaction_code_t kImsSetCallback = 1;
static constexpr transaction_code_t kImsDial = 2;
static constexpr transaction_code_t kImsGetRegistrationState = 4;
static constexpr transaction_code_t kImsAnswer = 5;
static constexpr transaction_code_t kImsHangup = 6;
static constexpr transaction_code_t kImsSendDtmf = 21;
static constexpr transaction_code_t kImsRequestRegistrationChange = 7;
static constexpr transaction_code_t kImsQueryServiceStatus = 8;
static constexpr transaction_code_t kImsSetServiceStatus = 9;
static constexpr transaction_code_t kImsSetConfig = 12;
static constexpr transaction_code_t kImsSetUiTtyMode = 24;
static constexpr transaction_code_t kImsGetSubConfig = 38;
static constexpr transaction_code_t kImsSendSms = 41;
static constexpr transaction_code_t kImsAcknowledgeSms = 42;
static constexpr transaction_code_t kImsAcknowledgeSmsReport = 43;
static constexpr transaction_code_t kImsGetSmsFormat = 44;
static constexpr transaction_code_t kImsDialResponse = 1;
static constexpr transaction_code_t kImsAnswerResponse = 2;
static constexpr transaction_code_t kImsHangupResponse = 3;
static constexpr transaction_code_t kImsSendDtmfResponse = 19;
static constexpr transaction_code_t kImsRequestRegistrationChangeResponse = 4;
static constexpr transaction_code_t kImsQueryServiceStatusResponse = 5;
static constexpr transaction_code_t kImsSetServiceStatusResponse = 6;
static constexpr transaction_code_t kImsSetConfigResponse = 9;
static constexpr transaction_code_t kImsGetRegistrationStateResponse = 11;
static constexpr transaction_code_t kImsSetUiTtyModeResponse = 22;
static constexpr transaction_code_t kImsGetSubConfigResponse = 34;
static constexpr transaction_code_t kImsSendSmsResponse = 37;
static constexpr transaction_code_t kAidlGetInterfaceHash = 0x00fffffe;
static constexpr transaction_code_t kAidlGetInterfaceVersion = 0x00ffffff;
static constexpr int32_t kImsInterfaceVersion = 20;
static constexpr const char *kImsInterfaceHash =
	"af0c66861f5c5211084d7d1ee436bd8f6040768a";
static constexpr int32_t kImsSuccess = 1;
static constexpr const char *kImsBridgeSocket =
	"/data/vendor/radio/ims-client/control.sock";
static constexpr const char *kImsBridgeState =
	"/data/vendor/radio/ims-client/ims-aidl-state";
static constexpr const char *kImsDeliveryDirectory =
	"/data/vendor/radio/ims-client";
static constexpr const char *kImsCallState =
	"/data/vendor/radio/ims-client/call-state";
static constexpr uint32_t kImsBridgeMagic = 0x58494d53;
static constexpr uint32_t kImsBridgeSend = 1;
static constexpr uint32_t kImsBridgeDial = 2;
static constexpr uint32_t kImsBridgeAnswer = 3;
static constexpr uint32_t kImsBridgeHangup = 4;
static constexpr uint32_t kImsBridgeDtmf = 5;

struct VendorImsReply {
	int32_t token = 0;
	int32_t message_ref = 0;
	int32_t status = 0;
	int32_t reason = 0;
	int32_t network_reason = 0;
	int32_t extra1 = 0;
	int32_t extra2 = 0;
};

struct VendorImsRegistrationReply {
	int32_t token = 0;
	int32_t error = 0;
	int32_t state = -1;
	int32_t radio_tech = -1;
};

struct VendorImsServiceEntry {
	bool valid = false;
	int32_t call_type = -1;
	int32_t status = -1;
	int32_t restrict_cause = -1;
	struct AccessTech {
		int32_t network_mode = -1;
		int32_t status = -1;
		int32_t restrict_cause = -1;
		bool has_registration = false;
		int32_t registration_state = -1;
		int32_t registration_radio_tech = -1;
	};
	std::vector<AccessTech> access;
};

struct VendorImsServiceReply {
	int32_t token = 0;
	int32_t error = 0;
	std::vector<VendorImsServiceEntry> entries;
};

struct VendorImsSubConfigReply {
	int32_t token = 0;
	int32_t error = -1;
	int32_t simult_stack_count = 0;
	std::vector<bool> ims_stack_enabled;
};

class VendorImsState {
public:
	void prepare(int32_t token)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		expected_token_ = token;
		completed_ = false;
		reply_ = VendorImsReply{};
	}

	void complete(const VendorImsReply& reply)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (reply.token != expected_token_)
			return;
		reply_ = reply;
		completed_ = true;
		condition_.notify_all();
	}

	bool wait(VendorImsReply *reply)
	{
		std::unique_lock<std::mutex> lock(mutex_);
		if (!condition_.wait_for(lock, std::chrono::seconds(60),
					 [this] { return completed_; }))
			return false;
		*reply = reply_;
		return true;
	}

private:
	std::mutex mutex_;
	std::condition_variable condition_;
	int32_t expected_token_ = 0;
	bool completed_ = false;
	VendorImsReply reply_;
};

class VendorImsDiagState {
public:
	void prepare_control(int32_t token, transaction_code_t response_code)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		control_token_ = token;
		control_response_code_ = response_code;
		control_error_ = 0;
		control_completed_ = false;
	}

	void complete_control(transaction_code_t response_code, int32_t token,
			      int32_t error)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (token != control_token_ || response_code != control_response_code_)
			return;
		control_error_ = error;
		control_completed_ = true;
		condition_.notify_all();
	}

	bool wait_control(int32_t *error)
	{
		std::unique_lock<std::mutex> lock(mutex_);
		if (!condition_.wait_for(lock, std::chrono::seconds(20), [this] {
			return control_completed_;
		}))
			return false;
		*error = control_error_;
		return true;
	}

	void prepare_registration(int32_t token)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		registration_token_ = token;
		registration_completed_ = false;
		registration_ = VendorImsRegistrationReply{};
	}

	void complete_registration(const VendorImsRegistrationReply& reply)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (reply.token != registration_token_)
			return;
		registration_ = reply;
		registration_completed_ = true;
		condition_.notify_all();
	}

	bool wait_registration(VendorImsRegistrationReply *reply)
	{
		std::unique_lock<std::mutex> lock(mutex_);
		if (!condition_.wait_for(lock, std::chrono::seconds(20), [this] {
			return registration_completed_;
		}))
			return false;
		*reply = registration_;
		return true;
	}

	void prepare_services(int32_t token)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		services_token_ = token;
		services_completed_ = false;
		services_ = VendorImsServiceReply{};
	}

	void complete_services(const VendorImsServiceReply& reply)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (reply.token != services_token_)
			return;
		services_ = reply;
		services_completed_ = true;
		condition_.notify_all();
	}

	bool wait_services(VendorImsServiceReply *reply)
	{
		std::unique_lock<std::mutex> lock(mutex_);
		if (!condition_.wait_for(lock, std::chrono::seconds(20), [this] {
			return services_completed_;
		}))
			return false;
		*reply = services_;
		return true;
	}

	void prepare_subconfig(int32_t token)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		subconfig_token_ = token;
		subconfig_completed_ = false;
		subconfig_ = VendorImsSubConfigReply{};
	}

	void complete_subconfig(const VendorImsSubConfigReply& reply)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (reply.token != subconfig_token_)
			return;
		subconfig_ = reply;
		subconfig_completed_ = true;
		condition_.notify_all();
	}

	bool wait_subconfig(VendorImsSubConfigReply *reply)
	{
		std::unique_lock<std::mutex> lock(mutex_);
		if (!condition_.wait_for(lock, std::chrono::seconds(25), [this] {
			return subconfig_completed_;
		}))
			return false;
		*reply = subconfig_;
		return true;
	}

private:
	std::mutex mutex_;
	std::condition_variable condition_;
	int32_t registration_token_ = 0;
	int32_t services_token_ = 0;
	int32_t subconfig_token_ = 0;
	int32_t control_token_ = 0;
	transaction_code_t control_response_code_ = 0;
	int32_t control_error_ = 0;
	bool registration_completed_ = false;
	bool services_completed_ = false;
	bool subconfig_completed_ = false;
	bool control_completed_ = false;
	VendorImsRegistrationReply registration_;
	VendorImsServiceReply services_;
	VendorImsSubConfigReply subconfig_;
};

class VendorImsCallState {
public:
	void prepare(int32_t token, transaction_code_t response_code)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		expected_token_ = token;
		expected_response_code_ = response_code;
		error_ = 0;
		completed_ = false;
	}

	void complete(transaction_code_t response_code, int32_t token,
		      int32_t error)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (token != expected_token_ ||
		    response_code != expected_response_code_)
			return;
		error_ = error;
		completed_ = true;
		condition_.notify_all();
	}

	bool wait(int32_t *error)
	{
		std::unique_lock<std::mutex> lock(mutex_);
		if (!condition_.wait_for(lock, std::chrono::seconds(30), [this] {
			return completed_;
		}))
			return false;
		*error = error_;
		return true;
	}

private:
	std::mutex mutex_;
	std::condition_variable condition_;
	int32_t expected_token_ = 0;
	transaction_code_t expected_response_code_ = 0;
	int32_t error_ = 0;
	bool completed_ = false;
};

struct VendorImsIndicationCounters {
	std::mutex mutex;
	uint64_t count[2][64] = {};
	transaction_code_t last_code[2] = {};
};

struct VendorImsCallbackContext {
	VendorImsState *sms = nullptr;
	VendorImsDiagState *diag = nullptr;
	VendorImsCallState *call = nullptr;
	VendorImsIndicationCounters *indications = nullptr;
	std::atomic<int32_t> *active_call_id = nullptr;
	std::atomic<int32_t> *active_call_state = nullptr;
	AIBinder *service = nullptr;
	const char *slot_name = nullptr;
	int slot_index = -1;
};

static bool parcel_status_string_allocator(void *data, int32_t length,
					   char **buffer)
{
	auto *value = static_cast<std::string *>(data);
	if (length < 0) {
		value->clear();
		*buffer = nullptr;
		return true;
	}
	value->resize(static_cast<size_t>(length));
	*buffer = length ? value->data() : nullptr;
	return true;
}

static bool parcel_status_bytes_allocator(void *data, int32_t length,
					  int8_t **buffer)
{
	auto *value = static_cast<std::vector<int8_t> *>(data);
	if (length < 0) {
		value->clear();
		*buffer = nullptr;
		return true;
	}
	value->resize(static_cast<size_t>(length));
	*buffer = length ? value->data() : nullptr;
	return true;
}

static int gsm_status_value(const std::vector<int8_t>& raw)
{
	if (raw.empty()) return -1;
	auto byte = [&raw](size_t index) {
		return static_cast<uint8_t>(raw[index]);
	};
	size_t tpdu = 0;
	if ((byte(0) & 0x03) != 0x02) {
		size_t smsc_length = byte(0);
		if (1 + smsc_length >= raw.size()) return -1;
		tpdu = 1 + smsc_length;
	}
	if (tpdu + 4 > raw.size() || (byte(tpdu) & 0x03) != 0x02)
		return -1;
	size_t address_bytes = (byte(tpdu + 2) + 1U) / 2U;
	size_t status_index = tpdu + 4U + address_bytes + 14U;
	if (status_index >= raw.size()) return -1;
	return byte(status_index);
}

static const char *gsm_delivery_state(int status)
{
	if (status < 0) return "unknown";
	if (status <= 0x1f) return "delivered";
	if (status <= 0x3f) return "pending";
	return "failed";
}

static void persist_delivery_report(const VendorImsCallbackContext *context,
				    int32_t message_ref,
				    const std::vector<int8_t>& pdu)
{
	if (!context || !context->slot_name || message_ref < 0) return;
	char path[256];
	char temporary[272];
	std::snprintf(path, sizeof(path), "%s/delivery-%s-%d.json",
		kImsDeliveryDirectory, context->slot_name, message_ref);
	std::snprintf(temporary, sizeof(temporary), "%s.tmp", path);
	FILE *file = std::fopen(temporary, "w");
	if (!file) return;
	int status = gsm_status_value(pdu);
	std::fprintf(file,
		"{\"message_id\":%d,\"slot\":\"%s\",\"delivery\":\"%s\","
		"\"tp_status\":%d,\"timestamp_epoch\":%lld,\"pdu\":\"",
		message_ref, context->slot_name, gsm_delivery_state(status), status,
		static_cast<long long>(std::time(nullptr)));
	static const char hex[] = "0123456789ABCDEF";
	for (int8_t value : pdu) {
		uint8_t octet = static_cast<uint8_t>(value);
		std::fputc(hex[octet >> 4], file);
		std::fputc(hex[octet & 0x0f], file);
	}
	std::fputs("\"}\n", file);
	std::fclose(file);
	std::rename(temporary, path);
}

static void write_hex_file(FILE *file, const std::vector<int8_t>& bytes)
{
	static const char hex[] = "0123456789ABCDEF";
	for (int8_t value : bytes) {
		uint8_t octet = static_cast<uint8_t>(value);
		std::fputc(hex[octet >> 4], file);
		std::fputc(hex[octet & 0x0f], file);
	}
}

static bool persist_incoming_sms(const VendorImsCallbackContext *context,
				 int32_t message_ref,
				 const std::vector<int8_t>& pdu)
{
	if (!context || !context->slot_name || pdu.empty()) return false;
	char path[320];
	char temporary[336];
	auto now = std::chrono::system_clock::now().time_since_epoch();
	auto micros = std::chrono::duration_cast<std::chrono::microseconds>(now).count();
	std::snprintf(path, sizeof(path), "%s/incoming-%s-%lld-%d.pdu",
		kImsDeliveryDirectory, context->slot_name,
		static_cast<long long>(micros), message_ref);
	std::snprintf(temporary, sizeof(temporary), "%s.tmp", path);
	FILE *file = std::fopen(temporary, "wx");
	if (!file) return false;
	write_hex_file(file, pdu);
	std::fputc('\n', file);
	if (std::fclose(file) != 0 || std::rename(temporary, path) != 0) {
		unlink(temporary);
		return false;
	}
	return true;
}

static const char *vendor_call_state_name(int32_t state)
{
	/* QTI's vendor IMS AIDL has its own numbering.  Unlike the AOSP radio IMS
	 * enum it starts with INVALID=0, followed by ACTIVE=1 through END=7.
	 * Keep these names identical to the LuCI/screen model. */
	switch (state) {
	case 1: return "conversation";
	case 2: return "hold";
	case 3: return "originating";
	case 4: return "alerting";
	case 5: return "incoming";
	case 6: return "waiting";
	default: return "";
	}
}

static std::string safe_call_number(const std::string& number)
{
	std::string result;
	result.reserve(number.size());
	for (char value : number) {
		if ((value >= '0' && value <= '9') || value == '+' ||
		    value == '*' || value == '#')
			result.push_back(value);
		if (result.size() == 64) break;
	}
	return result;
}

static void persist_ims_call_state(const VendorImsCallbackContext *context,
				   int32_t call_id, int32_t call_state,
				   const std::string& number = "",
				   const char *direction = "",
				   const char *call_type = "")
{
	static std::mutex state_mutex;
	std::lock_guard<std::mutex> state_lock(state_mutex);
	char temporary[256];
	const bool active = call_id > 0 && call_state > 0 && call_state < 7;
	if (!active && context && context->slot_name) {
		FILE *current = std::fopen(kImsCallState, "r");
		if (current) {
			char line[160];
			bool another_slot_active = false;
			bool current_active = false;
			std::string current_slot;
			while (std::fgets(line, sizeof(line), current)) {
				if (std::strncmp(line, "active_call_count=1", 19) == 0)
					current_active = true;
				else if (std::strncmp(line, "active_call_slot=", 17) == 0) {
					current_slot.assign(line + 17);
					while (!current_slot.empty() &&
					       (current_slot.back() == '\n' || current_slot.back() == '\r'))
						current_slot.pop_back();
				}
			}
			std::fclose(current);
			another_slot_active = current_active &&
				current_slot != context->slot_name;
			if (another_slot_active) return;
		}
	}
	std::snprintf(temporary, sizeof(temporary), "%s.tmp", kImsCallState);
	FILE *file = std::fopen(temporary, "w");
	if (!file) return;
	const std::string safe_number = safe_call_number(number);
	std::fprintf(file,
		"active_call_count=%d\nactive_call_id=%d\nactive_call_state=%s\n"
		"active_call_state_code=%d\nactive_call_slot=%s\n"
		"active_call_direction=%s\nactive_call_number=%s\n"
		"active_call_type=%s\nactive_call_mode=ims\nupdated_epoch=%lld\n",
		active ? 1 : 0, active ? call_id : 0,
		active ? vendor_call_state_name(call_state) : "", call_state,
		context && context->slot_name ? context->slot_name : "",
		active ? direction : "", active ? safe_number.c_str() : "",
		active ? call_type : "",
		static_cast<long long>(std::time(nullptr)));
	std::fclose(file);
	std::rename(temporary, kImsCallState);
}

static void acknowledge_delivery_report(AIBinder *service,
					 int32_t message_ref)
{
	if (!service || message_ref < 0) return;
	ndk::ScopedAParcel in;
	ndk::ScopedAParcel out;
	static std::atomic<int32_t> token{1};
	binder_status_t status = AIBinder_prepareTransaction(service, in.getR());
	if (status != STATUS_OK) return;
	status = AParcel_writeInt32(in.get(), token.fetch_add(1));
	if (status == STATUS_OK) status = AParcel_writeInt32(in.get(), 1);
	if (status != STATUS_OK) return;
	const int32_t start = AParcel_getDataPosition(in.get());
	status = AParcel_writeInt32(in.get(), 0);
	if (status == STATUS_OK) status = AParcel_writeInt32(in.get(), message_ref);
	/* Vendor SmsDeliverStatus.OK is 1, matching the stock QImsService trace. */
	if (status == STATUS_OK) status = AParcel_writeInt32(in.get(), 1);
	if (status != STATUS_OK) return;
	const int32_t end = AParcel_getDataPosition(in.get());
	status = AParcel_setDataPosition(in.get(), start);
	if (status == STATUS_OK)
		status = AParcel_writeInt32(in.get(), end - start);
	if (status == STATUS_OK) status = AParcel_setDataPosition(in.get(), end);
	if (status == STATUS_OK)
		AIBinder_transact(service, kImsAcknowledgeSmsReport, in.getR(),
			out.getR(), FLAG_PRIVATE_VENDOR | FLAG_ONEWAY);
}

static void acknowledge_incoming_sms(AIBinder *service, int32_t message_ref,
				     bool stored)
{
	if (!service || message_ref < 0) return;
	ndk::ScopedAParcel in;
	ndk::ScopedAParcel out;
	static std::atomic<int32_t> token{10001};
	binder_status_t status = AIBinder_prepareTransaction(service, in.getR());
	if (status == STATUS_OK)
		status = AParcel_writeInt32(in.get(), token.fetch_add(1));
	if (status == STATUS_OK) status = AParcel_writeInt32(in.get(), 1);
	const int32_t start = status == STATUS_OK ?
		AParcel_getDataPosition(in.get()) : 0;
	if (status == STATUS_OK) status = AParcel_writeInt32(in.get(), 0);
	if (status == STATUS_OK) status = AParcel_writeInt32(in.get(), message_ref);
	/* Vendor SmsDeliverStatus.OK=1. If persistence failed, return generic
	 * failure (2) so the network can retry instead of silently losing it. */
	if (status == STATUS_OK)
		status = AParcel_writeInt32(in.get(), stored ? 1 : 2);
	if (status == STATUS_OK) {
		const int32_t end = AParcel_getDataPosition(in.get());
		status = AParcel_setDataPosition(in.get(), start);
		if (status == STATUS_OK)
			status = AParcel_writeInt32(in.get(), end - start);
		if (status == STATUS_OK)
			status = AParcel_setDataPosition(in.get(), end);
	}
	if (status == STATUS_OK)
		status = AIBinder_transact(service, kImsAcknowledgeSms, in.getR(),
			out.getR(), FLAG_PRIVATE_VENDOR | FLAG_ONEWAY);
	if (status != STATUS_OK)
		std::fprintf(stderr,
			"IMS incoming SMS acknowledgement failed: status=%d ref=%d stored=%d\n",
			status, message_ref, stored ? 1 : 0);
}

static void *binder_state_create(void *args)
{
	return args;
}

static void binder_state_destroy(void *)
{
}

static binder_status_t remote_binder_unused(AIBinder *, transaction_code_t,
					    const AParcel *, AParcel *)
{
	return STATUS_UNKNOWN_TRANSACTION;
}

static binder_status_t ims_metadata_reply(transaction_code_t code,
					  AParcel *out)
{
	if (code != kAidlGetInterfaceVersion && code != kAidlGetInterfaceHash)
		return STATUS_UNKNOWN_TRANSACTION;
	AStatus *status_header = AStatus_newOk();
	if (!status_header) return STATUS_NO_MEMORY;
	binder_status_t status = AParcel_writeStatusHeader(out, status_header);
	AStatus_delete(status_header);
	if (status != STATUS_OK) return status;
	if (code == kAidlGetInterfaceVersion)
		return AParcel_writeInt32(out, kImsInterfaceVersion);
	return AParcel_writeString(out, kImsInterfaceHash,
		static_cast<int32_t>(std::strlen(kImsInterfaceHash)));
}

static binder_status_t ims_response_transact(AIBinder *binder,
					     transaction_code_t code,
					     const AParcel *in, AParcel *out)
{
	if (code == kAidlGetInterfaceVersion || code == kAidlGetInterfaceHash)
		return ims_metadata_reply(code, out);
	auto *context = static_cast<VendorImsCallbackContext *>(
		AIBinder_getUserData(binder));
	if (!context)
		return STATUS_BAD_VALUE;
	if (code == kImsDialResponse || code == kImsAnswerResponse ||
	    code == kImsHangupResponse || code == kImsSendDtmfResponse) {
		auto *state = context->call;
		if (!state) return STATUS_OK;
		int32_t token = 0;
		int32_t error = 0;
		binder_status_t status = AParcel_readInt32(in, &token);
		if (status != STATUS_OK) return status;
		status = AParcel_readInt32(in, &error);
		if (status != STATUS_OK) return status;
		state->complete(code, token, error);
		return STATUS_OK;
	}
	if (code == kImsRequestRegistrationChangeResponse ||
	    code == kImsSetServiceStatusResponse ||
	    code == kImsSetConfigResponse ||
	    code == kImsSetUiTtyModeResponse) {
		auto *state = context->diag;
		if (!state) return STATUS_OK;
		int32_t token = 0;
		int32_t error = 0;
		binder_status_t status = AParcel_readInt32(in, &token);
		if (status != STATUS_OK) return status;
		status = AParcel_readInt32(in, &error);
		if (status != STATUS_OK) return status;
		state->complete_control(code, token, error);
		return STATUS_OK;
	}

	if (code == kImsGetRegistrationStateResponse) {
		auto *state = context->diag;
		if (!state) return STATUS_OK;
		VendorImsRegistrationReply reply;
		int32_t present = 0;
		int32_t start;
		int32_t size = 0;
		binder_status_t status = AParcel_readInt32(in, &reply.token);
		if (status != STATUS_OK) return status;
		status = AParcel_readInt32(in, &reply.error);
		if (status != STATUS_OK) return status;
		status = AParcel_readInt32(in, &present);
		if (status != STATUS_OK) return status;
		if (present != 1) return STATUS_UNEXPECTED_NULL;
		start = AParcel_getDataPosition(in);
		status = AParcel_readInt32(in, &size);
		if (status != STATUS_OK) return status;
		if (size < 12) return STATUS_BAD_VALUE;
		status = AParcel_readInt32(in, &reply.state);
		if (status != STATUS_OK) return status;
		status = AParcel_readInt32(in, &reply.radio_tech);
		if (status != STATUS_OK) return status;
		status = AParcel_setDataPosition(in, start + size);
		if (status != STATUS_OK) return status;
		state->complete_registration(reply);
		return STATUS_OK;
	}

	if (code == kImsQueryServiceStatusResponse) {
		auto *state = context->diag;
		if (!state) return STATUS_OK;
		VendorImsServiceReply reply;
		int32_t count = 0;
		binder_status_t status = AParcel_readInt32(in, &reply.token);
		if (status != STATUS_OK) return status;
		status = AParcel_readInt32(in, &reply.error);
		if (status != STATUS_OK) return status;
		status = AParcel_readInt32(in, &count);
		if (status != STATUS_OK) return status;
		if (count < 0 || count > 32) return STATUS_BAD_VALUE;
		for (int32_t i = 0; i < count; ++i) {
			VendorImsServiceEntry entry;
			int32_t present = 0;
			int32_t start;
			int32_t size = 0;
			status = AParcel_readInt32(in, &present);
			if (status != STATUS_OK) return status;
			if (present != 1) return STATUS_UNEXPECTED_NULL;
			start = AParcel_getDataPosition(in);
			status = AParcel_readInt32(in, &size);
			if (status != STATUS_OK) return status;
			if (size < 17) return STATUS_BAD_VALUE;
			status = AParcel_readBool(in, &entry.valid);
			if (status != STATUS_OK) return status;
			status = AParcel_readInt32(in, &entry.call_type);
			if (status != STATUS_OK) return status;
			status = AParcel_readInt32(in, &entry.status);
			if (status != STATUS_OK) return status;
			status = AParcel_readInt32(in, &entry.restrict_cause);
			if (status != STATUS_OK) return status;
			int32_t access_count = 0;
			status = AParcel_readInt32(in, &access_count);
			if (status != STATUS_OK) return status;
			if (access_count < 0 || access_count > 16) return STATUS_BAD_VALUE;
			for (int32_t j = 0; j < access_count; ++j) {
				VendorImsServiceEntry::AccessTech access;
				int32_t access_present = 0;
				int32_t access_start;
				int32_t access_size = 0;
				status = AParcel_readInt32(in, &access_present);
				if (status != STATUS_OK) return status;
				if (access_present != 1) return STATUS_UNEXPECTED_NULL;
				access_start = AParcel_getDataPosition(in);
				status = AParcel_readInt32(in, &access_size);
				if (status != STATUS_OK) return status;
				if (access_size < 21) return STATUS_BAD_VALUE;
				status = AParcel_readInt32(in, &access.network_mode);
				if (status != STATUS_OK) return status;
				status = AParcel_readInt32(in, &access.status);
				if (status != STATUS_OK) return status;
				status = AParcel_readInt32(in, &access.restrict_cause);
				if (status != STATUS_OK) return status;
				status = AParcel_readBool(in, &access.has_registration);
				if (status != STATUS_OK) return status;
				int32_t registration_present = 0;
				status = AParcel_readInt32(in, &registration_present);
				if (status != STATUS_OK) return status;
				if (registration_present == 1) {
					int32_t registration_start = AParcel_getDataPosition(in);
					int32_t registration_size = 0;
					status = AParcel_readInt32(in, &registration_size);
					if (status != STATUS_OK) return status;
					if (registration_size < 12) return STATUS_BAD_VALUE;
					status = AParcel_readInt32(in,
						&access.registration_state);
					if (status != STATUS_OK) return status;
					/* Skip the remaining optional registration fields. */
					status = AParcel_setDataPosition(in,
						registration_start + registration_size);
					if (status != STATUS_OK) return status;
				}
				status = AParcel_setDataPosition(in, access_start + access_size);
				if (status != STATUS_OK) return status;
				entry.access.push_back(access);
			}
			status = AParcel_setDataPosition(in, start + size);
			if (status != STATUS_OK) return status;
			reply.entries.push_back(entry);
		}
		state->complete_services(reply);
		return STATUS_OK;
	}

	if (code == kImsGetSubConfigResponse) {
		auto *state = context->diag;
		if (!state) return STATUS_OK;
		VendorImsSubConfigReply reply;
		int32_t present = 0;
		int32_t start;
		int32_t size = 0;
		binder_status_t status = AParcel_readInt32(in, &reply.token);
		if (status != STATUS_OK) return status;
		status = AParcel_readInt32(in, &reply.error);
		if (status != STATUS_OK) return status;
		status = AParcel_readInt32(in, &present);
		if (status != STATUS_OK) return status;
		if (present != 1) return STATUS_UNEXPECTED_NULL;
		start = AParcel_getDataPosition(in);
		status = AParcel_readInt32(in, &size);
		if (status != STATUS_OK) return status;
		if (size < 12) return STATUS_BAD_VALUE;
		status = AParcel_readInt32(in, &reply.simult_stack_count);
		if (status != STATUS_OK) return status;
		status = ::ndk::AParcel_readVector(in, &reply.ims_stack_enabled);
		if (status != STATUS_OK) return status;
		status = AParcel_setDataPosition(in, start + size);
		if (status != STATUS_OK) return status;
		state->complete_subconfig(reply);
		return STATUS_OK;
	}

	/* Other response methods are unrelated to the outstanding request. */
	if (code != kImsSendSmsResponse)
		return STATUS_OK;

	auto *state = context->sms;
	if (!state) return STATUS_OK;
	VendorImsReply reply;
	int32_t present = 0;
	int32_t start;
	int32_t size = 0;
	binder_status_t status;

	status = AParcel_readInt32(in, &reply.token);
	if (status != STATUS_OK)
		return status;
	status = AParcel_readInt32(in, &present);
	if (status != STATUS_OK)
		return status;
	if (present != 1)
		return STATUS_UNEXPECTED_NULL;

	start = AParcel_getDataPosition(in);
	status = AParcel_readInt32(in, &size);
	if (status != STATUS_OK)
		return status;
	/* size includes the parcelable-size word and six int32 fields. */
	if (size < 28)
		return STATUS_BAD_VALUE;
#define READ_IMS_INT(field) do { \
	status = AParcel_readInt32(in, &reply.field); \
	if (status != STATUS_OK) return status; \
} while (0)
	READ_IMS_INT(message_ref);
	READ_IMS_INT(status);
	READ_IMS_INT(reason);
	READ_IMS_INT(network_reason);
	READ_IMS_INT(extra1);
	READ_IMS_INT(extra2);
#undef READ_IMS_INT
	status = AParcel_setDataPosition(in, start + size);
	if (status != STATUS_OK)
		return status;
	state->complete(reply);
	return STATUS_OK;
}

static binder_status_t ims_indication_transact(AIBinder *binder,
					       transaction_code_t code,
					       const AParcel *in, AParcel *out)
{
	if (code == kAidlGetInterfaceVersion || code == kAidlGetInterfaceHash)
		return ims_metadata_reply(code, out);
	auto *context = static_cast<VendorImsCallbackContext *>(
		AIBinder_getUserData(binder));
	if (context && context->indications && context->slot_index >= 0 &&
	    context->slot_index < 2) {
		std::lock_guard<std::mutex> lock(context->indications->mutex);
		context->indications->last_code[context->slot_index] = code;
		if (code < 64)
			context->indications->count[context->slot_index][code]++;
	}
	if (context && code == 22) {
		int32_t present = 0;
		int32_t start = 0;
		int32_t size = 0;
		int32_t message_ref = -1;
		std::string format;
		std::vector<int8_t> pdu;
		binder_status_t status = AParcel_readInt32(in, &present);
		if (status != STATUS_OK) return status;
		if (present != 1) return STATUS_UNEXPECTED_NULL;
		start = AParcel_getDataPosition(in);
		status = AParcel_readInt32(in, &size);
		if (status != STATUS_OK) return status;
		if (size < 8) return STATUS_BAD_VALUE;
		status = AParcel_readInt32(in, &message_ref);
		if (status != STATUS_OK) return status;
		status = AParcel_readString(in, &format,
			parcel_status_string_allocator);
		if (status != STATUS_OK) return status;
		if (!format.empty() && format.back() == '\0') format.pop_back();
		status = AParcel_readByteArray(in, &pdu,
			parcel_status_bytes_allocator);
		if (status != STATUS_OK) return status;
		status = AParcel_setDataPosition(in, start + size);
		if (status != STATUS_OK) return status;
		if (format == "3gpp")
			persist_delivery_report(context, message_ref, pdu);
		acknowledge_delivery_report(context->service, message_ref);
	}
	if (context && code == 23) {
		int32_t present = 0;
		int32_t start = 0;
		int32_t size = 0;
		int32_t verification_status = 0;
		/* QTI IncomingSms carries the sender-verification result after the
		 * PDU, not a GSM message reference.  Stock QImsService acknowledges
		 * an incoming 3GPP SMS with messageRef=0. */
		constexpr int32_t message_ref = 0;
		std::string format;
		std::vector<int8_t> pdu;
		binder_status_t status = AParcel_readInt32(in, &present);
		if (status != STATUS_OK) return status;
		if (present != 1) return STATUS_UNEXPECTED_NULL;
		start = AParcel_getDataPosition(in);
		status = AParcel_readInt32(in, &size);
		if (status != STATUS_OK) return status;
		if (size < 8) return STATUS_BAD_VALUE;
		status = AParcel_readString(in, &format,
			parcel_status_string_allocator);
		if (status != STATUS_OK) return status;
		if (!format.empty() && format.back() == '\0') format.pop_back();
		status = AParcel_readByteArray(in, &pdu,
			parcel_status_bytes_allocator);
		if (status != STATUS_OK) return status;
		status = AParcel_readInt32(in, &verification_status);
		if (status != STATUS_OK) return status;
		status = AParcel_setDataPosition(in, start + size);
		if (status != STATUS_OK) return status;
		bool stored = format == "3gpp" &&
			persist_incoming_sms(context, message_ref, pdu);
		acknowledge_incoming_sms(context->service, message_ref, stored);
	}
	if (context && code == 1 && context->active_call_id &&
	    context->active_call_state) {
		int32_t count = 0;
		binder_status_t status = AParcel_readInt32(in, &count);
		if (status != STATUS_OK) return status;
		if (count < 0 || count > 32) return STATUS_BAD_VALUE;
		int32_t active_id = 0;
		int32_t active_state = 0;
		std::string active_number;
		const char *active_direction = "";
		const char *active_type = "";
		for (int32_t index = 0; index < count; ++index) {
			int32_t present = 0;
			int32_t start = 0;
			int32_t size = 0;
			int32_t call_state = 0;
			int32_t call_id = 0;
			int32_t type_of_address = 0;
			bool is_multiparty = false;
			bool is_mobile_terminated = false;
			int32_t multiline_present = 0;
			int32_t als = 0;
			bool is_voice = false;
			bool is_voice_privacy = false;
			std::string number;
			status = AParcel_readInt32(in, &present);
			if (status != STATUS_OK) return status;
			if (present != 1) return STATUS_UNEXPECTED_NULL;
			start = AParcel_getDataPosition(in);
			status = AParcel_readInt32(in, &size);
			if (status != STATUS_OK) return status;
			if (size < 12 || start > 0x7fffffff - size)
				return STATUS_BAD_VALUE;
			const int32_t end = start + size;
			status = AParcel_readInt32(in, &call_state);
			if (status != STATUS_OK) return status;
			status = AParcel_readInt32(in, &call_id);
			if (status != STATUS_OK) return status;
			/* Exact QTI V12 CallInfo prefix: state, index, TOA, isMpty,
			 * isMT, nullable MultiIdentityLineInfo, ALS, isVoice,
			 * isVoicePrivacy, number.  Decode only this stable prefix and
			 * skip the remaining optional vendor fields by parcel size. */
			status = AParcel_readInt32(in, &type_of_address);
			if (status != STATUS_OK) return status;
			status = AParcel_readBool(in, &is_multiparty);
			if (status != STATUS_OK) return status;
			status = AParcel_readBool(in, &is_mobile_terminated);
			if (status != STATUS_OK) return status;
			status = AParcel_readInt32(in, &multiline_present);
			if (status != STATUS_OK) return status;
			if (multiline_present == 1) {
				const int32_t multiline_start = AParcel_getDataPosition(in);
				int32_t multiline_size = 0;
				status = AParcel_readInt32(in, &multiline_size);
				if (status != STATUS_OK) return status;
				if (multiline_size < 4 || multiline_start > end ||
				    multiline_size > end - multiline_start)
					return STATUS_BAD_VALUE;
				status = AParcel_setDataPosition(in,
					multiline_start + multiline_size);
				if (status != STATUS_OK) return status;
			} else if (multiline_present != 0) {
				return STATUS_BAD_VALUE;
			}
			status = AParcel_readInt32(in, &als);
			if (status != STATUS_OK) return status;
			status = AParcel_readBool(in, &is_voice);
			if (status != STATUS_OK) return status;
			status = AParcel_readBool(in, &is_voice_privacy);
			if (status != STATUS_OK) return status;
			status = AParcel_readString(in, &number,
				parcel_status_string_allocator);
			if (status != STATUS_OK) return status;
			if (!number.empty() && number.back() == '\0') number.pop_back();
			(void)type_of_address;
			(void)is_multiparty;
			(void)als;
			(void)is_voice_privacy;
			status = AParcel_setDataPosition(in, end);
			if (status != STATUS_OK) return status;
			/* Vendor CallState.INVALID=0 and END=7 are not live calls. Prefer
			 * a live call over either record when an indication contains both. */
			if (call_state > 0 && call_state < 7 && active_id == 0) {
				active_id = call_id;
				active_state = call_state;
				active_number = number;
				active_direction = is_mobile_terminated ?
					"incoming" : "outgoing";
				active_type = is_voice ? "voice" : "video";
			}
		}
		context->active_call_id->store(active_id);
		context->active_call_state->store(active_state);
		/* Publish calls from either SIM. The state writer keeps an idle update
		 * from one slot from clearing a live call owned by the other slot. */
		persist_ims_call_state(context, active_id,
			active_id ? active_state : 7, active_number,
			active_direction, active_type);
	}
	/* The persistent owner records every real indication.  Typed decoding of
	 * incoming SMS and calls is added per transaction without inventing state. */
	return STATUS_OK;
}

static const AIBinder_Class *ims_radio_class()
{
	static AIBinder_Class *clazz = AIBinder_Class_define(kImsRadioDescriptor,
		binder_state_create, binder_state_destroy, remote_binder_unused);
	return clazz;
}

static const AIBinder_Class *ims_response_class()
{
	static AIBinder_Class *clazz = AIBinder_Class_define(kImsResponseDescriptor,
		binder_state_create, binder_state_destroy, ims_response_transact);
	return clazz;
}

static const AIBinder_Class *ims_indication_class()
{
	static AIBinder_Class *clazz = AIBinder_Class_define(kImsIndicationDescriptor,
		binder_state_create, binder_state_destroy, ims_indication_transact);
	return clazz;
}

class ResponseState {
public:
	void prepare(int32_t serial, bool smsc = false)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		expected_serial_ = serial;
		expected_smsc_ = smsc;
		completed_ = false;
	}

	void complete(const radio::RadioResponseInfo& info,
		      const messaging::SendSmsResult& result)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (info.serial != expected_serial_ || expected_smsc_)
			return;
		reply_.info = info;
		reply_.result = result;
		completed_ = true;
		condition_.notify_all();
	}

	void complete_smsc(const radio::RadioResponseInfo& info,
			   const std::string& smsc)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (info.serial != expected_serial_ || !expected_smsc_)
			return;
		reply_.info = info;
		reply_.smsc = smsc;
		completed_ = true;
		condition_.notify_all();
	}

	bool wait(SmsReply *reply)
	{
		std::unique_lock<std::mutex> lock(mutex_);
		if (!condition_.wait_for(lock, std::chrono::seconds(30),
					 [this] { return completed_; }))
			return false;
		*reply = reply_;
		return true;
	}

private:
	std::mutex mutex_;
	std::condition_variable condition_;
	int32_t expected_serial_ = 0;
	bool expected_smsc_ = false;
	bool completed_ = false;
	SmsReply reply_;
};

class RadioMessagingResponse final : public messaging::BnRadioMessagingResponse {
public:
	explicit RadioMessagingResponse(std::shared_ptr<ResponseState> state)
		: state_(std::move(state)) {}

	::ndk::ScopedAStatus sendImsSmsResponse(
		const radio::RadioResponseInfo& info,
		const messaging::SendSmsResult& sms) override
	{
		state_->complete(info, sms);
		return ::ndk::ScopedAStatus::ok();
	}

	::ndk::ScopedAStatus sendSmsResponse(
		const radio::RadioResponseInfo& info,
		const messaging::SendSmsResult& sms) override
	{
		state_->complete(info, sms);
		return ::ndk::ScopedAStatus::ok();
	}

#define EMPTY_INFO_METHOD(name) \
	::ndk::ScopedAStatus name(const radio::RadioResponseInfo&) override \
	{ return ::ndk::ScopedAStatus::ok(); }

	EMPTY_INFO_METHOD(acknowledgeIncomingGsmSmsWithPduResponse)
	EMPTY_INFO_METHOD(acknowledgeLastIncomingCdmaSmsResponse)
	EMPTY_INFO_METHOD(acknowledgeLastIncomingGsmSmsResponse)
	EMPTY_INFO_METHOD(deleteSmsOnRuimResponse)
	EMPTY_INFO_METHOD(deleteSmsOnSimResponse)
	EMPTY_INFO_METHOD(reportSmsMemoryStatusResponse)
	EMPTY_INFO_METHOD(setCdmaBroadcastActivationResponse)
	EMPTY_INFO_METHOD(setCdmaBroadcastConfigResponse)
	EMPTY_INFO_METHOD(setGsmBroadcastActivationResponse)
	EMPTY_INFO_METHOD(setGsmBroadcastConfigResponse)
	EMPTY_INFO_METHOD(setSmscAddressResponse)

#undef EMPTY_INFO_METHOD

	::ndk::ScopedAStatus acknowledgeRequest(int32_t) override
	{ return ::ndk::ScopedAStatus::ok(); }

	::ndk::ScopedAStatus getCdmaBroadcastConfigResponse(
		const radio::RadioResponseInfo&,
		const std::vector<messaging::CdmaBroadcastSmsConfigInfo>&) override
	{ return ::ndk::ScopedAStatus::ok(); }

	::ndk::ScopedAStatus getGsmBroadcastConfigResponse(
		const radio::RadioResponseInfo&,
		const std::vector<messaging::GsmBroadcastSmsConfigInfo>&) override
	{ return ::ndk::ScopedAStatus::ok(); }

	::ndk::ScopedAStatus getSmscAddressResponse(
		const radio::RadioResponseInfo& info,
		const std::string& smsc) override
	{
		state_->complete_smsc(info, smsc);
		return ::ndk::ScopedAStatus::ok();
	}

	::ndk::ScopedAStatus sendCdmaSmsExpectMoreResponse(
		const radio::RadioResponseInfo&,
		const messaging::SendSmsResult&) override
	{ return ::ndk::ScopedAStatus::ok(); }

	::ndk::ScopedAStatus sendCdmaSmsResponse(
		const radio::RadioResponseInfo&,
		const messaging::SendSmsResult&) override
	{ return ::ndk::ScopedAStatus::ok(); }

	::ndk::ScopedAStatus sendSmsExpectMoreResponse(
		const radio::RadioResponseInfo&,
		const messaging::SendSmsResult&) override
	{ return ::ndk::ScopedAStatus::ok(); }

	::ndk::ScopedAStatus writeSmsToRuimResponse(
		const radio::RadioResponseInfo&, int32_t) override
	{ return ::ndk::ScopedAStatus::ok(); }

	::ndk::ScopedAStatus writeSmsToSimResponse(
		const radio::RadioResponseInfo&, int32_t) override
	{ return ::ndk::ScopedAStatus::ok(); }

private:
	std::shared_ptr<ResponseState> state_;
};

class RadioMessagingIndication final : public messaging::BnRadioMessagingIndication {
public:
	::ndk::ScopedAStatus cdmaNewSms(
		radio::RadioIndicationType,
		const messaging::CdmaSmsMessage&) override
	{ return ::ndk::ScopedAStatus::ok(); }

	::ndk::ScopedAStatus cdmaRuimSmsStorageFull(
		radio::RadioIndicationType) override
	{ return ::ndk::ScopedAStatus::ok(); }

	::ndk::ScopedAStatus newBroadcastSms(
		radio::RadioIndicationType, const std::vector<uint8_t>&) override
	{ return ::ndk::ScopedAStatus::ok(); }

	::ndk::ScopedAStatus newSms(
		radio::RadioIndicationType, const std::vector<uint8_t>&) override
	{ return ::ndk::ScopedAStatus::ok(); }

	::ndk::ScopedAStatus newSmsOnSim(
		radio::RadioIndicationType, int32_t) override
	{ return ::ndk::ScopedAStatus::ok(); }

	::ndk::ScopedAStatus newSmsStatusReport(
		radio::RadioIndicationType, const std::vector<uint8_t>&) override
	{ return ::ndk::ScopedAStatus::ok(); }

	::ndk::ScopedAStatus simSmsStorageFull(
		radio::RadioIndicationType) override
	{ return ::ndk::ScopedAStatus::ok(); }
};

static bool next_utf8(const unsigned char **cursor, uint32_t *codepoint)
{
	const unsigned char *p = *cursor;
	uint32_t cp;

	if (!*p)
		return false;
	if (*p < 0x80) {
		*codepoint = *p;
		*cursor = p + 1;
		return true;
	}
	if ((*p & 0xe0) == 0xc0 && p[1] && (p[1] & 0xc0) == 0x80) {
		cp = ((*p & 0x1f) << 6) | (p[1] & 0x3f);
		if (cp < 0x80)
			return false;
		*cursor = p + 2;
	} else if ((*p & 0xf0) == 0xe0 && p[1] && p[2] &&
		   (p[1] & 0xc0) == 0x80 && (p[2] & 0xc0) == 0x80) {
		cp = ((*p & 0x0f) << 12) | ((p[1] & 0x3f) << 6) |
			(p[2] & 0x3f);
		if (cp < 0x800)
			return false;
		*cursor = p + 3;
	} else if ((*p & 0xf8) == 0xf0 && p[1] && p[2] && p[3] &&
		   (p[1] & 0xc0) == 0x80 && (p[2] & 0xc0) == 0x80 &&
		   (p[3] & 0xc0) == 0x80) {
		cp = ((*p & 7) << 18) | ((p[1] & 0x3f) << 12) |
			((p[2] & 0x3f) << 6) | (p[3] & 0x3f);
		if (cp < 0x10000)
			return false;
		*cursor = p + 4;
	} else {
		return false;
	}
	if (cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff))
		return false;
	*codepoint = cp;
	return true;
}

static bool encode_utf16(const char *text, std::vector<uint8_t> *encoded,
			 std::string *error)
{
	const unsigned char *cursor =
		reinterpret_cast<const unsigned char *>(text);
	uint32_t cp;

	while (*cursor) {
		if (!next_utf8(&cursor, &cp)) {
			*error = "message is not valid UTF-8";
			return false;
		}
		if (cp <= 0xffff) {
			encoded->push_back(static_cast<uint8_t>(cp >> 8));
			encoded->push_back(static_cast<uint8_t>(cp));
		} else {
			uint32_t value = cp - 0x10000;
			uint16_t high = 0xd800 | (value >> 10);
			uint16_t low = 0xdc00 | (value & 0x3ff);
			encoded->push_back(static_cast<uint8_t>(high >> 8));
			encoded->push_back(static_cast<uint8_t>(high));
			encoded->push_back(static_cast<uint8_t>(low >> 8));
			encoded->push_back(static_cast<uint8_t>(low));
		}
	}
	if (encoded->empty()) {
		*error = "empty SMS text";
		return false;
	}
	return true;
}

static std::string hex_encode(const std::vector<uint8_t>& bytes)
{
	static const char hex[] = "0123456789ABCDEF";
	std::string out;

	out.reserve(bytes.size() * 2);
	for (uint8_t byte : bytes) {
		out.push_back(hex[byte >> 4]);
		out.push_back(hex[byte & 0x0f]);
	}
	return out;
}

static bool encode_smsc_pdu(const std::string& raw, std::string *encoded,
			    std::string *error)
{
	std::vector<uint8_t> digits;
	std::vector<uint8_t> bytes;
	size_t cursor = 0;
	bool international = false;

	while (cursor < raw.size() && raw[cursor] != '+' &&
	       (raw[cursor] < '0' || raw[cursor] > '9'))
		++cursor;
	if (cursor < raw.size() && raw[cursor] == '+') {
		international = true;
		++cursor;
	}
	while (cursor < raw.size() && raw[cursor] >= '0' &&
	       raw[cursor] <= '9') {
		if (digits.size() >= 32) {
			*error = "SMSC address is too long";
			return false;
		}
		digits.push_back(static_cast<uint8_t>(raw[cursor++] - '0'));
	}
	if (digits.empty()) {
		*error = "Radio Messaging returned an empty or invalid SMSC address";
		return false;
	}
	if (raw.find(",145", cursor) != std::string::npos)
		international = true;

	bytes.push_back(static_cast<uint8_t>(1 + (digits.size() + 1) / 2));
	bytes.push_back(international ? 0x91 : 0x81);
	for (size_t i = 0; i < digits.size(); i += 2) {
		uint8_t high = i + 1 < digits.size() ? digits[i + 1] : 0x0f;
		bytes.push_back(digits[i] | (high << 4));
	}
	*encoded = hex_encode(bytes);
	return true;
}

static std::string json_escape(const std::string& value)
{
	std::string escaped;

	for (unsigned char c : value) {
		switch (c) {
		case '\\': escaped += "\\\\"; break;
		case '"': escaped += "\\\""; break;
		case '\n': escaped += "\\n"; break;
		case '\r': escaped += "\\r"; break;
		case '\t': escaped += "\\t"; break;
		default:
			if (c >= 0x20)
				escaped.push_back(static_cast<char>(c));
		}
	}
	return escaped;
}

static bool build_submit_pdus(const char *number, const char *text,
			      std::vector<std::string> *pdus,
			      std::string *error)
{
	std::vector<uint8_t> digits;
	std::vector<uint8_t> utf16;
	std::vector<std::pair<size_t, size_t>> chunks;
	bool international = number[0] == '+';
	const char *digit = international ? number + 1 : number;

	for (; *digit; ++digit) {
		if (*digit < '0' || *digit > '9' || digits.size() >= 32) {
			*error = "invalid recipient number";
			return false;
		}
		digits.push_back(static_cast<uint8_t>(*digit - '0'));
	}
	if (digits.empty()) {
		*error = "empty recipient number";
		return false;
	}
	if (!encode_utf16(text, &utf16, error))
		return false;

	for (size_t offset = 0; offset < utf16.size();) {
		size_t limit = utf16.size() <= 140 ? 140 : 134;
		size_t length = std::min(limit, utf16.size() - offset);

		if (offset + length < utf16.size() && length >= 2 &&
		    utf16[offset + length - 2] >= 0xd8 &&
		    utf16[offset + length - 2] <= 0xdb)
			length -= 2;
		if (!length || chunks.size() >= 10) {
			*error = "SMS needs more than 10 segments";
			return false;
		}
		chunks.emplace_back(offset, length);
		offset += length;
	}

	uint8_t reference = static_cast<uint8_t>(
		std::chrono::steady_clock::now().time_since_epoch().count() ^
		static_cast<unsigned long>(getpid()));
	for (size_t part = 0; part < chunks.size(); ++part) {
		const bool multipart = chunks.size() > 1;
		std::vector<uint8_t> tpdu;
		const size_t offset = chunks[part].first;
		const size_t length = chunks[part].second;

		tpdu.reserve(160);
		/* TP-SRR asks the network for a real delivery report. */
		tpdu.push_back(multipart ? 0x61 : 0x21);
		tpdu.push_back(0x00);
		tpdu.push_back(static_cast<uint8_t>(digits.size()));
		tpdu.push_back(international ? 0x91 : 0x81);
		for (size_t i = 0; i < digits.size(); i += 2) {
			uint8_t high = i + 1 < digits.size() ? digits[i + 1] : 0x0f;
			tpdu.push_back(digits[i] | (high << 4));
		}
		tpdu.push_back(0x00);
		tpdu.push_back(0x08);
		tpdu.push_back(static_cast<uint8_t>(length + (multipart ? 6 : 0)));
		if (multipart) {
			tpdu.insert(tpdu.end(), { 0x05, 0x00, 0x03, reference,
				static_cast<uint8_t>(chunks.size()),
				static_cast<uint8_t>(part + 1) });
		}
		tpdu.insert(tpdu.end(), utf16.begin() + offset,
			    utf16.begin() + offset + length);
		pdus->push_back(hex_encode(tpdu));
	}
	return true;
}

static bool hex_decode(const std::string& text, std::vector<int8_t> *bytes)
{
	auto nibble = [](char c) -> int {
		if (c >= '0' && c <= '9') return c - '0';
		if (c >= 'A' && c <= 'F') return c - 'A' + 10;
		if (c >= 'a' && c <= 'f') return c - 'a' + 10;
		return -1;
	};

	if (text.size() % 2)
		return false;
	bytes->reserve(text.size() / 2);
	for (size_t i = 0; i < text.size(); i += 2) {
		int high = nibble(text[i]);
		int low = nibble(text[i + 1]);
		if (high < 0 || low < 0)
			return false;
		bytes->push_back(static_cast<int8_t>((high << 4) | low));
	}
	return true;
}

static bool parcel_write_string(AParcel *parcel, const std::string& value,
				std::string *error)
{
	binder_status_t status = AParcel_writeString(parcel, value.c_str(),
		static_cast<int32_t>(value.size()));
	if (status == STATUS_OK)
		return true;
	*error = "cannot write IMS string to Binder parcel: " +
		std::to_string(status);
	return false;
}

static bool parcel_write_sms_request(AParcel *parcel,
				     const std::vector<int8_t>& pdu,
				     std::string *error)
{
	const int32_t start = AParcel_getDataPosition(parcel);
	binder_status_t status = AParcel_writeInt32(parcel, 0);
	if (status != STATUS_OK)
		goto failed;
	/* SmsSendRequest: messageRef, format, smsc, retry, pdu. */
	status = AParcel_writeInt32(parcel, 0);
	if (status != STATUS_OK)
		goto failed;
	if (!parcel_write_string(parcel, "3gpp", error))
		return false;
	/* Android passes null when no explicit service centre was requested.  The
	 * Qualcomm parcelable stores a non-null string, whose equivalent is empty. */
	if (!parcel_write_string(parcel, "", error))
		return false;
	status = AParcel_writeBool(parcel, false);
	if (status != STATUS_OK)
		goto failed;
	status = AParcel_writeByteArray(parcel, pdu.data(),
		static_cast<int32_t>(pdu.size()));
	if (status != STATUS_OK)
		goto failed;
	{
		const int32_t end = AParcel_getDataPosition(parcel);
		status = AParcel_setDataPosition(parcel, start);
		if (status != STATUS_OK)
			goto failed;
		status = AParcel_writeInt32(parcel, end - start);
		if (status != STATUS_OK)
			goto failed;
		status = AParcel_setDataPosition(parcel, end);
		if (status != STATUS_OK)
			goto failed;
	}
	return true;

failed:
	*error = "cannot write Qualcomm IMS SMS request: " +
		std::to_string(status);
	return false;
}

static std::string vendor_ims_instance(const char *slot)
{
	std::string instance = kImsRadioDescriptor;
	instance += std::strcmp(slot, "slot1") == 0 ? "/imsradio0" :
		"/imsradio1";
	return instance;
}

static ndk::SpAIBinder get_vendor_ims_service(const char *slot,
					      std::string *error)
{
	std::string instance = vendor_ims_instance(slot);
	ndk::SpAIBinder binder(AServiceManager_checkService(instance.c_str()));

	if (!binder.get()) {
		*error = "Qualcomm IMS AIDL service is unavailable on " +
			std::string(slot);
		return nullptr;
	}
	if (!AIBinder_associateClass(binder.get(), ims_radio_class())) {
		*error = "cannot associate Qualcomm IMS AIDL descriptor on " +
			std::string(slot);
		return nullptr;
	}
	return binder;
}

static bool vendor_ims_set_callbacks(AIBinder *service,
				     AIBinder *response,
				     AIBinder *indication,
				     std::string *error)
{
	ndk::ScopedAParcel in;
	ndk::ScopedAParcel out;
	binder_status_t status = AIBinder_prepareTransaction(service, in.getR());
	if (status != STATUS_OK)
		goto failed;
	status = AParcel_writeStrongBinder(in.get(), response);
	if (status != STATUS_OK)
		goto failed;
	status = AParcel_writeStrongBinder(in.get(), indication);
	if (status != STATUS_OK)
		goto failed;
	status = AIBinder_transact(service, kImsSetCallback, in.getR(), out.getR(),
		FLAG_PRIVATE_VENDOR);
	if (status != STATUS_OK)
		goto failed;
	{
		AStatus *raw_status = nullptr;
		status = AParcel_readStatusHeader(out.get(), &raw_status);
		ndk::ScopedAStatus call_status(raw_status);
		if (status != STATUS_OK)
			goto failed;
		if (!call_status.isOk()) {
			*error = "Qualcomm IMS setCallback rejected: " +
				call_status.getDescription();
			return false;
		}
	}
	return true;

failed:
	*error = "Qualcomm IMS setCallback Binder failure: " +
		std::to_string(status);
	return false;
}

static bool parcel_string_allocator(void *data, int32_t length, char **buffer)
{
	auto *value = static_cast<std::string *>(data);
	if (length < 0) {
		value->clear();
		*buffer = nullptr;
		return true;
	}
	value->resize(static_cast<size_t>(length));
	*buffer = length ? value->data() : nullptr;
	return true;
}

static bool vendor_ims_get_sms_format(AIBinder *service, std::string *format,
				      std::string *error)
{
	ndk::ScopedAParcel in;
	ndk::ScopedAParcel out;
	binder_status_t status = AIBinder_prepareTransaction(service, in.getR());
	if (status != STATUS_OK)
		goto failed;
	status = AIBinder_transact(service, kImsGetSmsFormat, in.getR(), out.getR(),
		FLAG_PRIVATE_VENDOR);
	if (status != STATUS_OK)
		goto failed;
	{
		AStatus *raw_status = nullptr;
		status = AParcel_readStatusHeader(out.get(), &raw_status);
		ndk::ScopedAStatus call_status(raw_status);
		if (status != STATUS_OK)
			goto failed;
		if (!call_status.isOk()) {
			*error = "Qualcomm IMS getSmsFormat rejected: " +
				call_status.getDescription();
			return false;
		}
	}
	status = AParcel_readString(out.get(), format, parcel_string_allocator);
	if (status != STATUS_OK)
		goto failed;
	if (!format->empty() && format->back() == '\0')
		format->pop_back();
	return true;

failed:
	*error = "Qualcomm IMS getSmsFormat Binder failure: " +
		std::to_string(status);
	return false;
}

static bool vendor_ims_token_transaction(AIBinder *service,
					 transaction_code_t code, int32_t token,
					 const char *operation,
					 std::string *error)
{
	ndk::ScopedAParcel in;
	ndk::ScopedAParcel out;
	binder_status_t status = AIBinder_prepareTransaction(service, in.getR());
	if (status != STATUS_OK)
		goto failed;
	status = AParcel_writeInt32(in.get(), token);
	if (status != STATUS_OK)
		goto failed;
	status = AIBinder_transact(service, code, in.getR(), out.getR(),
		FLAG_PRIVATE_VENDOR | FLAG_ONEWAY);
	if (status == STATUS_OK)
		return true;

failed:
	*error = std::string("Qualcomm IMS ") + operation +
		" Binder failure: " + std::to_string(status);
	return false;
}

static bool parcel_finish_sized(AParcel *parcel, int32_t start,
				std::string *error)
{
	const int32_t end = AParcel_getDataPosition(parcel);
	binder_status_t status = AParcel_setDataPosition(parcel, start);
	if (status == STATUS_OK)
		status = AParcel_writeInt32(parcel, end - start);
	if (status == STATUS_OK)
		status = AParcel_setDataPosition(parcel, end);
	if (status == STATUS_OK)
		return true;
	*error = "cannot finish Qualcomm IMS parcelable: " +
		std::to_string(status);
	return false;
}

static bool parcel_write_call_details(AParcel *parcel, std::string *error)
{
	const int32_t start = AParcel_getDataPosition(parcel);
	binder_status_t status = AParcel_writeInt32(parcel, 0);
	/* Exact StableAidl conversion of a normal voice call in the automatic
	 * domain: VOICE=1, AUTOMATIC=4, empty ability/extras arrays. */
	if (status == STATUS_OK) status = AParcel_writeInt32(parcel, 1);
	if (status == STATUS_OK) status = AParcel_writeInt32(parcel, 4);
	if (status == STATUS_OK) status = AParcel_writeInt32(parcel, 0);
	if (status == STATUS_OK) status = AParcel_writeInt32(parcel, 0);
	if (status == STATUS_OK) status = AParcel_writeInt32(parcel, 0);
	if (status == STATUS_OK) status = AParcel_writeInt32(parcel, -1);
	if (status == STATUS_OK) status = AParcel_writeInt32(parcel, -1);
	if (status == STATUS_OK) status = AParcel_writeInt32(parcel, -1);
	/* Framework RTT_MODE_DISABLED maps to vendor RTT_MODE_DISABLED=1. */
	if (status == STATUS_OK) status = AParcel_writeInt32(parcel, 1);
	if (status != STATUS_OK) goto failed;
	if (!parcel_write_string(parcel, "", error)) return false;
	status = AParcel_writeBool(parcel, false);
	if (status == STATUS_OK) status = AParcel_writeBool(parcel, false);
	if (status == STATUS_OK) status = AParcel_writeBool(parcel, false);
	if (status == STATUS_OK) status = AParcel_writeInt32(parcel, 0);
	if (status != STATUS_OK) goto failed;
	return parcel_finish_sized(parcel, start, error);

failed:
	*error = "cannot write Qualcomm IMS CallDetails: " +
		std::to_string(status);
	return false;
}

static bool parcel_write_multi_identity(AParcel *parcel, std::string *error)
{
	const int32_t start = AParcel_getDataPosition(parcel);
	binder_status_t status = AParcel_writeInt32(parcel, 0);
	if (status != STATUS_OK) goto failed;
	if (!parcel_write_string(parcel, "", error)) return false;
	status = AParcel_writeInt32(parcel, 0);
	if (status == STATUS_OK) status = AParcel_writeInt32(parcel, 0);
	if (status != STATUS_OK) goto failed;
	return parcel_finish_sized(parcel, start, error);

failed:
	*error = "cannot write Qualcomm IMS MultiIdentityLineInfo: " +
		std::to_string(status);
	return false;
}

static bool parcel_write_redial_info(AParcel *parcel, std::string *error)
{
	const int32_t start = AParcel_getDataPosition(parcel);
	binder_status_t status = AParcel_writeInt32(parcel, 0);
	/* StableAidl.fromRedialInfo(null) uses MISC=548 and INVALID tech=0. */
	if (status == STATUS_OK) status = AParcel_writeInt32(parcel, 548);
	if (status == STATUS_OK) status = AParcel_writeInt32(parcel, 0);
	if (status != STATUS_OK) {
		*error = "cannot write Qualcomm IMS RedialInfo: " +
			std::to_string(status);
		return false;
	}
	return parcel_finish_sized(parcel, start, error);
}

static bool parcel_write_dial_request(AParcel *parcel, const char *number,
				      std::string *error)
{
	const int32_t start = AParcel_getDataPosition(parcel);
	binder_status_t status = AParcel_writeInt32(parcel, 0);
	if (status != STATUS_OK) goto failed;
	if (!parcel_write_string(parcel, number, error)) return false;
	/* Framework CLIR_DEFAULT=0 maps to vendor CLIR_DEFAULT=1. */
	status = AParcel_writeInt32(parcel, 1);
	if (status == STATUS_OK) status = AParcel_writeInt32(parcel, 1);
	if (status != STATUS_OK) goto failed;
	if (!parcel_write_call_details(parcel, error)) return false;
	status = AParcel_writeBool(parcel, false);
	if (status == STATUS_OK) status = AParcel_writeBool(parcel, false);
	if (status == STATUS_OK) status = AParcel_writeBool(parcel, false);
	/* StableAidl supplies non-null default multi-line and redial objects. */
	if (status == STATUS_OK) status = AParcel_writeInt32(parcel, 1);
	if (status != STATUS_OK) goto failed;
	if (!parcel_write_multi_identity(parcel, error)) return false;
	status = AParcel_writeInt32(parcel, 1);
	if (status != STATUS_OK) goto failed;
	if (!parcel_write_redial_info(parcel, error)) return false;
	return parcel_finish_sized(parcel, start, error);

failed:
	*error = "cannot write Qualcomm IMS DialRequest: " +
		std::to_string(status);
	return false;
}

static bool parcel_write_sip_error(AParcel *parcel, std::string *error)
{
	const int32_t start = AParcel_getDataPosition(parcel);
	binder_status_t status = AParcel_writeInt32(parcel, 0);
	if (status == STATUS_OK) status = AParcel_writeInt32(parcel, 0);
	if (status != STATUS_OK) goto failed;
	if (!parcel_write_string(parcel, "", error)) return false;
	return parcel_finish_sized(parcel, start, error);

failed:
	*error = "cannot write Qualcomm IMS SipErrorInfo: " +
		std::to_string(status);
	return false;
}

static bool parcel_write_call_fail(AParcel *parcel, std::string *error)
{
	const int32_t start = AParcel_getDataPosition(parcel);
	binder_status_t status = AParcel_writeInt32(parcel, 0);
	if (status == STATUS_OK) status = AParcel_writeInt32(parcel, 0);
	if (status == STATUS_OK)
		status = AParcel_writeByteArray(parcel, nullptr, 0);
	if (status != STATUS_OK) goto failed;
	if (!parcel_write_string(parcel, "", error)) return false;
	status = AParcel_writeBool(parcel, false);
	if (status == STATUS_OK) status = AParcel_writeInt32(parcel, 1);
	if (status != STATUS_OK) goto failed;
	if (!parcel_write_sip_error(parcel, error)) return false;
	return parcel_finish_sized(parcel, start, error);

failed:
	*error = "cannot write Qualcomm IMS CallFailCauseResponse: " +
		std::to_string(status);
	return false;
}

static bool parcel_write_hangup_request(AParcel *parcel, int32_t call_id,
					std::string *error)
{
	const int32_t start = AParcel_getDataPosition(parcel);
	binder_status_t status = AParcel_writeInt32(parcel, 0);
	if (status == STATUS_OK) status = AParcel_writeInt32(parcel, call_id);
	if (status == STATUS_OK) status = AParcel_writeBool(parcel, false);
	if (status != STATUS_OK) goto failed;
	if (!parcel_write_string(parcel, "", error)) return false;
	if (status == STATUS_OK) status = AParcel_writeInt32(parcel, 0x7fffffff);
	if (status == STATUS_OK) status = AParcel_writeInt32(parcel, 1);
	if (status != STATUS_OK) goto failed;
	if (!parcel_write_call_fail(parcel, error)) return false;
	return parcel_finish_sized(parcel, start, error);

failed:
	*error = "cannot write Qualcomm IMS HangupRequestInfo: " +
		std::to_string(status);
	return false;
}

static bool parcel_write_answer_request(AParcel *parcel, std::string *error)
{
	const int32_t start = AParcel_getDataPosition(parcel);
	binder_status_t status = AParcel_writeInt32(parcel, 0);
	/* Normal voice, stock default TIR configuration, RTT disabled. */
	if (status == STATUS_OK) status = AParcel_writeInt32(parcel, 1);
	if (status == STATUS_OK) status = AParcel_writeInt32(parcel, 3);
	if (status == STATUS_OK) status = AParcel_writeInt32(parcel, 1);
	if (status != STATUS_OK) {
		*error = "cannot write Qualcomm IMS AnswerRequest: " +
			std::to_string(status);
		return false;
	}
	return parcel_finish_sized(parcel, start, error);
}

static bool parcel_write_dtmf_info(AParcel *parcel, const char *tone,
				   int32_t call_id, std::string *error)
{
	const int32_t start = AParcel_getDataPosition(parcel);
	binder_status_t status = AParcel_writeInt32(parcel, 0);
	if (status != STATUS_OK) goto failed;
	if (!parcel_write_string(parcel, tone, error)) return false;
	status = AParcel_writeInt32(parcel, call_id);
	if (status != STATUS_OK) goto failed;
	return parcel_finish_sized(parcel, start, error);

failed:
	*error = "cannot write Qualcomm IMS DtmfInfo: " +
		std::to_string(status);
	return false;
}

static bool valid_dial_number(const char *number)
{
	size_t length = std::strlen(number);
	if (length < 2 || length > 64) return false;
	for (size_t index = 0; index < length; ++index) {
		char c = number[index];
		if ((c >= '0' && c <= '9') || c == '*' || c == '#') continue;
		if (c == '+' && index == 0) continue;
		return false;
	}
	return true;
}

static bool valid_dtmf_tone(const char *tone)
{
	return tone && tone[0] && !tone[1] &&
		std::strchr("0123456789*#ABCD", tone[0]);
}

static bool vendor_ims_call_transaction(AIBinder *service,
		VendorImsCallState *state, transaction_code_t code,
		transaction_code_t response_code, int32_t token,
		const char *number, int32_t call_id, int32_t *response_error,
		std::string *error)
{
	ndk::ScopedAParcel in;
	ndk::ScopedAParcel out;
	binder_status_t status = AIBinder_prepareTransaction(service, in.getR());
	state->prepare(token, response_code);
	if (status == STATUS_OK) status = AParcel_writeInt32(in.get(), token);
	if (status == STATUS_OK) status = AParcel_writeInt32(in.get(), 1);
	if (status != STATUS_OK) goto failed;
	if (code == kImsDial) {
		if (!parcel_write_dial_request(in.get(), number, error)) return false;
	} else if (code == kImsAnswer) {
		if (!parcel_write_answer_request(in.get(), error)) return false;
	} else if (code == kImsHangup) {
		if (!parcel_write_hangup_request(in.get(), call_id, error)) return false;
	} else if (code == kImsSendDtmf) {
		if (!parcel_write_dtmf_info(in.get(), number, call_id, error))
			return false;
	} else {
		*error = "invalid Qualcomm IMS call transaction";
		return false;
	}
	status = AIBinder_transact(service, code, in.getR(), out.getR(),
		FLAG_PRIVATE_VENDOR | FLAG_ONEWAY);
	if (status != STATUS_OK) goto failed;
	if (state->wait(response_error)) return true;
	*error = "Qualcomm IMS call response timed out";
	return false;

failed:
	*error = "Qualcomm IMS call Binder failure: " +
		std::to_string(status);
	return false;
}

static bool parcel_write_registration_info(AParcel *parcel,
					   std::string *error)
{
	const int32_t start = AParcel_getDataPosition(parcel);
	binder_status_t status = AParcel_writeInt32(parcel, 0);
	if (status == STATUS_OK) status = AParcel_writeInt32(parcel, 0);
	if (status == STATUS_OK) status = AParcel_writeInt32(parcel, 0);
	if (status != STATUS_OK) goto failed;
	if (!parcel_write_string(parcel, "", error)) return false;
	status = AParcel_writeInt32(parcel, 0);
	if (status != STATUS_OK) goto failed;
	if (!parcel_write_string(parcel, "", error)) return false;
	return parcel_finish_sized(parcel, start, error);

failed:
	*error = "cannot write Qualcomm IMS RegistrationInfo: " +
		std::to_string(status);
	return false;
}

static bool parcel_write_access_tech(AParcel *parcel, int32_t network_mode,
				     int32_t service_status,
				     std::string *error)
{
	const int32_t start = AParcel_getDataPosition(parcel);
	binder_status_t status = AParcel_writeInt32(parcel, 0);
	if (status == STATUS_OK) status = AParcel_writeInt32(parcel, network_mode);
	if (status == STATUS_OK) status = AParcel_writeInt32(parcel, service_status);
	if (status == STATUS_OK) status = AParcel_writeInt32(parcel, 0);
	if (status == STATUS_OK) status = AParcel_writeBool(parcel, false);
	/* StableAidl.fromServiceStatusInfo() always supplies a non-null default
	 * RegistrationInfo even when hasRegistration is false. */
	if (status == STATUS_OK) status = AParcel_writeInt32(parcel, 1);
	if (status != STATUS_OK) goto failed;
	if (!parcel_write_registration_info(parcel, error)) return false;
	return parcel_finish_sized(parcel, start, error);

failed:
	*error = "cannot write Qualcomm IMS StatusForAccessTech: " +
		std::to_string(status);
	return false;
}

static bool parcel_write_service_status(AParcel *parcel, int32_t call_type,
					int32_t network_mode,
					int32_t service_status,
					std::string *error)
{
	const int32_t start = AParcel_getDataPosition(parcel);
	binder_status_t status = AParcel_writeInt32(parcel, 0);
	if (status == STATUS_OK) status = AParcel_writeBool(parcel, true);
	if (status == STATUS_OK) status = AParcel_writeInt32(parcel, call_type);
	/* Top-level status and restriction are left INVALID/zero exactly like
	 * HyperOS StableAidl.fromServiceStatusInfo(). */
	if (status == STATUS_OK) status = AParcel_writeInt32(parcel, 0);
	if (status == STATUS_OK) status = AParcel_writeInt32(parcel, 0);
	if (status == STATUS_OK) status = AParcel_writeInt32(parcel, 1);
	if (status == STATUS_OK) status = AParcel_writeInt32(parcel, 1);
	if (status != STATUS_OK) goto failed;
	if (!parcel_write_access_tech(parcel, network_mode, service_status, error))
		return false;
	status = AParcel_writeInt32(parcel, 0); /* rttMode */
	if (status != STATUS_OK) goto failed;
	return parcel_finish_sized(parcel, start, error);

failed:
	*error = "cannot write Qualcomm IMS ServiceStatusInfo: " +
		std::to_string(status);
	return false;
}

static bool vendor_ims_set_stock_service_status(AIBinder *service,
						 int32_t token,
						 std::string *error)
{
	/* Exact conversion of the five CapabilityStatus entries observed during
	 * the stock Android slot-2 enable sequence:
	 *   Voice/LTE=on, Video/LTE=on, Voice/IWLAN=off,
	 *   Voice/Cross-SIM=off, Voice/NR=off.
	 * Values below come from this device's StableAidl implementation, not from
	 * a generic or operator-specific guess. */
	struct Capability {
		int32_t call_type;
		int32_t network_mode;
		int32_t status;
	};
	static constexpr Capability capabilities[] = {
		{ 1, 16, 3 }, { 4, 16, 3 }, { 1, 21, 1 },
		{ 1, 23, 1 }, { 1, 22, 1 },
	};
	ndk::ScopedAParcel in;
	ndk::ScopedAParcel out;
	binder_status_t status = AIBinder_prepareTransaction(service, in.getR());
	if (status == STATUS_OK) status = AParcel_writeInt32(in.get(), token);
	if (status == STATUS_OK)
		status = AParcel_writeInt32(in.get(),
			static_cast<int32_t>(sizeof(capabilities) / sizeof(capabilities[0])));
	if (status != STATUS_OK) goto failed;
	for (const auto& capability : capabilities) {
		status = AParcel_writeInt32(in.get(), 1);
		if (status != STATUS_OK) goto failed;
		if (!parcel_write_service_status(in.get(), capability.call_type,
			capability.network_mode, capability.status, error))
			return false;
	}
	status = AIBinder_transact(service, kImsSetServiceStatus, in.getR(),
		out.getR(), FLAG_PRIVATE_VENDOR | FLAG_ONEWAY);
	if (status == STATUS_OK) return true;

failed:
	*error = "Qualcomm IMS setServiceStatus Binder failure: " +
		std::to_string(status);
	return false;
}

static bool vendor_ims_request_registration(AIBinder *service, int32_t token,
					    std::string *error)
{
	ndk::ScopedAParcel in;
	ndk::ScopedAParcel out;
	binder_status_t status = AIBinder_prepareTransaction(service, in.getR());
	if (status == STATUS_OK) status = AParcel_writeInt32(in.get(), token);
	if (status == STATUS_OK) status = AParcel_writeInt32(in.get(), 1);
	if (status == STATUS_OK)
		status = AIBinder_transact(service,
			kImsRequestRegistrationChange, in.getR(), out.getR(),
			FLAG_PRIVATE_VENDOR | FLAG_ONEWAY);
	if (status == STATUS_OK) return true;
	*error = "Qualcomm IMS requestRegistrationChange Binder failure: " +
		std::to_string(status);
	return false;
}

static bool vendor_ims_query_registration(AIBinder *service,
		VendorImsDiagState *state, int32_t token,
		VendorImsRegistrationReply *reply, std::string *error)
{
	state->prepare_registration(token);
	if (!vendor_ims_token_transaction(service, kImsGetRegistrationState,
		token, "getImsRegistrationState", error))
		return false;
	if (state->wait_registration(reply)) return true;
	*error = "Qualcomm IMS registration response timed out";
	return false;
}

static bool vendor_ims_query_services(AIBinder *service,
		VendorImsDiagState *state, int32_t token,
		VendorImsServiceReply *reply, std::string *error)
{
	state->prepare_services(token);
	if (!vendor_ims_token_transaction(service, kImsQueryServiceStatus,
		token, "queryServiceStatus", error))
		return false;
	if (state->wait_services(reply)) return true;
	*error = "Qualcomm IMS service-status response timed out";
	return false;
}

static bool vendor_ims_query_subconfig(AIBinder *service,
		VendorImsDiagState *state, int32_t token,
		VendorImsSubConfigReply *reply, std::string *error)
{
	state->prepare_subconfig(token);
	if (!vendor_ims_token_transaction(service, kImsGetSubConfig,
		token, "getImsSubConfig", error))
		return false;
	if (state->wait_subconfig(reply)) return true;
	*error = "Qualcomm IMS sub-config response timed out";
	return false;
}

static bool vendor_ims_set_ui_tty_off(AIBinder *service, int32_t token,
		std::string *error)
{
	ndk::ScopedAParcel in;
	ndk::ScopedAParcel out;
	binder_status_t status = AIBinder_prepareTransaction(service, in.getR());
	if (status == STATUS_OK) status = AParcel_writeInt32(in.get(), token);
	if (status == STATUS_OK) status = AParcel_writeInt32(in.get(), 1);
	const int32_t start = status == STATUS_OK ?
		AParcel_getDataPosition(in.get()) : 0;
	if (status == STATUS_OK) status = AParcel_writeInt32(in.get(), 0);
	/* Android UI TTY mode OFF maps to vendor TtyMode.OFF=1. */
	if (status == STATUS_OK) status = AParcel_writeInt32(in.get(), 1);
	if (status == STATUS_OK)
		status = AParcel_writeByteArray(in.get(), nullptr, 0);
	if (status == STATUS_OK) {
		const int32_t end = AParcel_getDataPosition(in.get());
		status = AParcel_setDataPosition(in.get(), start);
		if (status == STATUS_OK) status = AParcel_writeInt32(in.get(), end - start);
		if (status == STATUS_OK) status = AParcel_setDataPosition(in.get(), end);
	}
	if (status == STATUS_OK)
		status = AIBinder_transact(service, kImsSetUiTtyMode, in.getR(),
			out.getR(), FLAG_PRIVATE_VENDOR | FLAG_ONEWAY);
	if (status == STATUS_OK) return true;
	*error = "Qualcomm IMS setUiTtyMode Binder failure: " +
		std::to_string(status);
	return false;
}

static bool vendor_ims_set_rtt_config_off(AIBinder *service, int32_t token,
		std::string *error)
{
	ndk::ScopedAParcel in;
	ndk::ScopedAParcel out;
	binder_status_t status = AIBinder_prepareTransaction(service, in.getR());
	if (status == STATUS_OK) status = AParcel_writeInt32(in.get(), token);
	if (status == STATUS_OK) status = AParcel_writeInt32(in.get(), 1);
	const int32_t start = status == STATUS_OK ?
		AParcel_getDataPosition(in.get()) : 0;
	if (status == STATUS_OK) status = AParcel_writeInt32(in.get(), 0);
	/* Framework item 66 maps to vendor ConfigItem 67. The stock boot sets
	 * integer value 0 immediately before capability and registration enable. */
	if (status == STATUS_OK) status = AParcel_writeInt32(in.get(), 67);
	if (status == STATUS_OK) status = AParcel_writeBool(in.get(), true);
	if (status == STATUS_OK) status = AParcel_writeBool(in.get(), false);
	if (status == STATUS_OK) status = AParcel_writeInt32(in.get(), 0);
	if (status == STATUS_OK && !parcel_write_string(in.get(), "", error))
		return false;
	/* Framework ImsConfig error NONE maps to vendor error cause 1. */
	if (status == STATUS_OK) status = AParcel_writeInt32(in.get(), 1);
	if (status == STATUS_OK) {
		const int32_t end = AParcel_getDataPosition(in.get());
		status = AParcel_setDataPosition(in.get(), start);
		if (status == STATUS_OK) status = AParcel_writeInt32(in.get(), end - start);
		if (status == STATUS_OK) status = AParcel_setDataPosition(in.get(), end);
	}
	if (status == STATUS_OK)
		status = AIBinder_transact(service, kImsSetConfig, in.getR(),
			out.getR(), FLAG_PRIVATE_VENDOR | FLAG_ONEWAY);
	if (status == STATUS_OK) return true;
	*error = "Qualcomm IMS setConfig Binder failure: " +
		std::to_string(status);
	return false;
}

static bool vendor_ims_sms_service_ready(const VendorImsServiceReply& services)
{
	for (const auto& entry : services.entries) {
		if (!entry.valid || entry.call_type != 10)
			continue;
		for (const auto& access : entry.access)
			if (access.status == 3)
				return true;
	}
	return false;
}

static std::string current_boot_id()
{
	char value[80] = {};
	FILE *file = std::fopen("/proc/sys/kernel/random/boot_id", "r");
	if (!file) return {};
	if (!std::fgets(value, sizeof(value), file)) value[0] = '\0';
	std::fclose(file);
	value[std::strcspn(value, "\r\n")] = '\0';
	return value;
}

static int vendor_ims_initialize(const char *slot)
{
	std::string error;
	ABinderProcess_setThreadPoolMaxThreadCount(2);
	ABinderProcess_startThreadPool();
	ndk::SpAIBinder service = get_vendor_ims_service(slot, &error);
	if (!service.get()) {
		std::fprintf(stderr, "%s\n", error.c_str());
		return 1;
	}
	VendorImsDiagState state;
	VendorImsCallbackContext context;
	context.diag = &state;
	ndk::SpAIBinder response(AIBinder_new(ims_response_class(), &context));
	ndk::SpAIBinder indication(AIBinder_new(ims_indication_class(), &context));
	if (!response.get() || !indication.get()) {
		std::fprintf(stderr, "cannot create Qualcomm IMS initialization callbacks\n");
		return 1;
	}
	AIBinder_markVendorStability(response.get());
	AIBinder_markVendorStability(indication.get());
	if (!vendor_ims_set_callbacks(service.get(), response.get(), indication.get(),
		&error)) {
		std::fprintf(stderr, "%s\n", error.c_str());
		return 1;
	}

	int32_t token = static_cast<int32_t>(
		std::chrono::steady_clock::now().time_since_epoch().count() &
		0x3fffffff);
	int32_t set_error = 0;
	int32_t registration_change_error = 0;
	state.prepare_control(++token, kImsSetServiceStatusResponse);
	if (!vendor_ims_set_stock_service_status(service.get(), token, &error) ||
	    !state.wait_control(&set_error)) {
		if (error.empty()) error = "Qualcomm IMS setServiceStatus response timed out";
		std::fprintf(stderr, "%s\n", error.c_str());
		return 1;
	}
	state.prepare_control(++token, kImsRequestRegistrationChangeResponse);
	if (!vendor_ims_request_registration(service.get(), token, &error) ||
	    !state.wait_control(&registration_change_error)) {
		if (error.empty())
			error = "Qualcomm IMS registration-change response timed out";
		std::fprintf(stderr, "%s\n", error.c_str());
		return 1;
	}

	VendorImsRegistrationReply registration;
	VendorImsServiceReply services;
	std::string format;
	bool ready = false;
	for (int attempt = 0; attempt < 60; ++attempt) {
		std::this_thread::sleep_for(std::chrono::milliseconds(
			attempt == 0 ? 1500 : 1000));
		if (!vendor_ims_get_sms_format(service.get(), &format, &error) ||
		    !vendor_ims_query_registration(service.get(), &state, ++token,
			&registration, &error) ||
		    !vendor_ims_query_services(service.get(), &state, ++token,
			&services, &error)) {
			std::fprintf(stderr, "%s\n", error.c_str());
			return 1;
		}
		ready = set_error == kImsSuccess &&
			registration_change_error == kImsSuccess &&
			registration.error == kImsSuccess && registration.state == 1 &&
			services.error == kImsSuccess && format == "3gpp" &&
			vendor_ims_sms_service_ready(services);
		if (ready) break;
	}

	std::printf("{\"ok\":true,\"ready\":%s,\"slot\":\"%s\",\"boot_id\":\"%s\",\"set_service_error\":%d,\"registration_change_error\":%d,\"sms_format\":\"%s\",\"registration_error\":%d,\"registration_state\":%d,\"registration_radio_tech\":%d,\"service_error\":%d,\"sms_service_ready\":%s}\n",
		ready ? "true" : "false", slot, current_boot_id().c_str(),
		set_error, registration_change_error, format.c_str(),
		registration.error, registration.state, registration.radio_tech,
		services.error,
		vendor_ims_sms_service_ready(services) ? "true" : "false");
	return ready ? 0 : 1;
}

static int vendor_ims_diag(const char *slot)
{
	std::string error;
	std::string format;
	VendorImsRegistrationReply registration;
	VendorImsServiceReply services;

	/* Start the callback pool before looking up a remote Binder.  Besides being
	 * the correct NDK ordering, this avoids libbinder's linkToDeath warning. */
	ABinderProcess_setThreadPoolMaxThreadCount(2);
	ABinderProcess_startThreadPool();
	ndk::SpAIBinder service = get_vendor_ims_service(slot, &error);
	if (!service.get()) {
		std::fprintf(stderr, "%s\n", error.c_str());
		return 1;
	}
	VendorImsDiagState state;
	VendorImsCallbackContext context;
	context.diag = &state;
	ndk::SpAIBinder response(AIBinder_new(ims_response_class(), &context));
	ndk::SpAIBinder indication(AIBinder_new(ims_indication_class(), &context));
	if (!response.get() || !indication.get()) {
		std::fprintf(stderr, "cannot create Qualcomm IMS diagnostic callbacks\n");
		return 1;
	}
	AIBinder_markVendorStability(response.get());
	AIBinder_markVendorStability(indication.get());
	if (!vendor_ims_set_callbacks(service.get(), response.get(), indication.get(),
				      &error) ||
	    !vendor_ims_get_sms_format(service.get(), &format, &error)) {
		std::fprintf(stderr, "%s\n", error.c_str());
		return 1;
	}

	int32_t token = static_cast<int32_t>(
		std::chrono::steady_clock::now().time_since_epoch().count() &
		0x3fffffff);
	if (!vendor_ims_query_registration(service.get(), &state, ++token,
		&registration, &error)) {
		std::fprintf(stderr, "%s\n", error.c_str());
		return 1;
	}

	if (!vendor_ims_query_services(service.get(), &state, ++token,
		&services, &error)) {
		std::fprintf(stderr, "%s\n", error.c_str());
		return 1;
	}

	std::printf("{\"ok\":true,\"slot\":\"%s\",\"instance\":\"%s\",\"sms_format\":\"%s\",\"registration\":{\"error\":%d,\"state\":%d,\"radio_tech\":%d},\"services\":[",
		slot, vendor_ims_instance(slot).c_str(), format.c_str(),
		registration.error, registration.state, registration.radio_tech);
	for (size_t i = 0; i < services.entries.size(); ++i) {
		const auto& entry = services.entries[i];
		std::printf("%s{\"valid\":%s,\"call_type\":%d,\"status\":%d,\"restrict_cause\":%d,\"access\":[",
			i ? "," : "", entry.valid ? "true" : "false",
			entry.call_type, entry.status, entry.restrict_cause);
		for (size_t j = 0; j < entry.access.size(); ++j) {
			const auto& access = entry.access[j];
			std::printf("%s{\"network_mode\":%d,\"status\":%d,\"restrict_cause\":%d,\"has_registration\":%s,\"registration_state\":%d}",
				j ? "," : "", access.network_mode, access.status,
				access.restrict_cause,
				access.has_registration ? "true" : "false",
				access.registration_state);
		}
		std::printf("]}");
	}
	std::printf("],\"service_error\":%d}\n", services.error);
	return registration.error == kImsSuccess &&
		services.error == kImsSuccess ? 0 : 1;
}

static bool vendor_ims_send_transaction(AIBinder *service, int32_t token,
					const std::vector<int8_t>& pdu,
					std::string *error)
{
	ndk::ScopedAParcel in;
	ndk::ScopedAParcel out;
	binder_status_t status = AIBinder_prepareTransaction(service, in.getR());
	if (status != STATUS_OK)
		goto failed;
	status = AParcel_writeInt32(in.get(), token);
	if (status != STATUS_OK)
		goto failed;
	/* Non-null SmsSendRequest parcelable. */
	status = AParcel_writeInt32(in.get(), 1);
	if (status != STATUS_OK)
		goto failed;
	if (!parcel_write_sms_request(in.get(), pdu, error))
		return false;
	status = AIBinder_transact(service, kImsSendSms, in.getR(), out.getR(),
		FLAG_PRIVATE_VENDOR | FLAG_ONEWAY);
	if (status == STATUS_OK)
		return true;

failed:
	*error = "Qualcomm IMS sendSms Binder failure: " +
		std::to_string(status);
	return false;
}

static int vendor_ims_send_bound(AIBinder *service, VendorImsState *state,
				 const char *slot, const char *number,
				 const char *text, std::string *result,
				 std::string *error)
{
	std::vector<std::string> encoded_pdus;
	if (!build_submit_pdus(number, text, &encoded_pdus, error))
		return 2;

	int32_t token = static_cast<int32_t>(
		std::chrono::steady_clock::now().time_since_epoch().count() &
		0x3fffffff);
	int32_t last_message_ref = 0;
	std::vector<int32_t> message_refs;
	for (size_t part = 0; part < encoded_pdus.size(); ++part) {
		std::vector<int8_t> pdu;
		VendorImsReply reply;
		if (!hex_decode(encoded_pdus[part], &pdu)) {
			*error = "internal error decoding SMS PDU";
			return 2;
		}
		state->prepare(++token);
		if (!vendor_ims_send_transaction(service, token, pdu, error))
			return 1;
		if (!state->wait(&reply)) {
			*error = "Qualcomm IMS sendSms response timed out on segment " +
				std::to_string(part + 1) + "/" +
				std::to_string(encoded_pdus.size());
			return 1;
		}
		/* Vendor SmsSendStatus.OK and SmsSendFailureReason.NONE are both 1.
		 * QImsService converts the latter to Android framework reason 0. */
		if (reply.status != 1 || reply.reason != 1) {
			*error = "Qualcomm IMS rejected segment " +
				std::to_string(part + 1) + "/" +
				std::to_string(encoded_pdus.size()) + ": status=" +
				std::to_string(reply.status) + ", reason=" +
				std::to_string(reply.reason) + ", network_reason=" +
				std::to_string(reply.network_reason) + ", extra1=" +
				std::to_string(reply.extra1) + ", extra2=" +
				std::to_string(reply.extra2) + ", message_ref=" +
				std::to_string(reply.message_ref);
			return 1;
		}
		last_message_ref = reply.message_ref;
		message_refs.push_back(reply.message_ref);
	}

	*result = "{\"ok\":true,\"transport\":\"qti-ims-aidl\",\"slot\":\"" +
		std::string(slot) + "\",\"message_id\":" +
		std::to_string(last_message_ref) + ",\"message_ids\":[";
	for (size_t index = 0; index < message_refs.size(); ++index) {
		if (index) *result += ",";
		*result += std::to_string(message_refs[index]);
	}
	*result += "],\"delivery\":\"pending\",\"segments\":" +
		std::to_string(encoded_pdus.size()) + "}";
	return 0;
}

struct VendorImsSlot {
	const char *name = nullptr;
	ndk::SpAIBinder service;
	ndk::SpAIBinder response;
	ndk::SpAIBinder indication;
	VendorImsState sms;
	VendorImsDiagState diag;
	VendorImsCallState call;
	std::atomic<int32_t> active_call_id{0};
	std::atomic<int32_t> active_call_state{0};
	VendorImsCallbackContext context;
	bool attached = false;
};

static int32_t next_ims_token()
{
	static std::atomic<uint32_t> sequence{1};
	uint32_t ticks = static_cast<uint32_t>(
		std::chrono::steady_clock::now().time_since_epoch().count());
	return static_cast<int32_t>((ticks + sequence.fetch_add(1)) &
		0x3fffffff);
}

static int vendor_ims_dial_bound(VendorImsSlot *slot, const char *number,
				 std::string *result, std::string *error)
{
	if (!valid_dial_number(number)) {
		*error = "invalid dial number";
		return 2;
	}
	int32_t response_error = 0;
	if (!vendor_ims_call_transaction(slot->service.get(), &slot->call,
		kImsDial, kImsDialResponse, next_ims_token(), number, 0,
		&response_error, error))
		return 1;
	if (response_error != kImsSuccess) {
		*error = "Qualcomm IMS rejected dial: error=" +
			std::to_string(response_error);
		return 1;
	}
	int32_t call_id = 0;
	int32_t call_state = 0;
	for (int attempt = 0; attempt < 80; ++attempt) {
		call_id = slot->active_call_id.load();
		call_state = slot->active_call_state.load();
		if (call_id > 0) break;
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}
	if (call_id <= 0) {
		/* A successful dial response can precede the first call-state
		 * indication. Never leave such an untracked test call ringing: slot
		 * call index 1 is the modem's first live call, so issue a best-effort
		 * IMS hangup before returning the diagnostic failure. */
		int32_t safety_error = 0;
		std::string safety_detail;
		vendor_ims_call_transaction(slot->service.get(), &slot->call,
			kImsHangup, kImsHangupResponse, next_ims_token(), nullptr, 1,
			&safety_error, &safety_detail);
		*error = "Qualcomm IMS accepted dial but no live call ID arrived";
		return 1;
	}
	/* Publish the accepted call before replying to the OpenWrt controller.
	 * This closes the small interval in which a screen/LuCI status poll could
	 * still see the old idle state and stop the newly opened voice route. */
	persist_ims_call_state(&slot->context, call_id, call_state, number,
		"outgoing", "voice");
	*result = "{\"ok\":true,\"transport\":\"qti-ims-aidl\",\"slot\":\"" +
		std::string(slot->name) + "\",\"call_id\":" +
		std::to_string(call_id) + ",\"call_state\":" +
		std::to_string(call_state) + "}";
	return 0;
}

static int vendor_ims_answer_bound(VendorImsSlot *slot, std::string *result,
				   std::string *error)
{
	int32_t response_error = 0;
	if (!vendor_ims_call_transaction(slot->service.get(), &slot->call,
		kImsAnswer, kImsAnswerResponse, next_ims_token(), nullptr, 0,
		&response_error, error))
		return 1;
	if (response_error != kImsSuccess) {
		*error = "Qualcomm IMS rejected answer: error=" +
			std::to_string(response_error);
		return 1;
	}
	*result = "{\"ok\":true,\"transport\":\"qti-ims-aidl\",\"slot\":\"" +
		std::string(slot->name) + "\",\"action\":\"answer\"}";
	return 0;
}

static int vendor_ims_hangup_bound(VendorImsSlot *slot, int32_t call_id,
				   std::string *result, std::string *error)
{
	if (call_id <= 0) {
		*error = "invalid IMS call ID";
		return 2;
	}
	int32_t response_error = 0;
	if (!vendor_ims_call_transaction(slot->service.get(), &slot->call,
		kImsHangup, kImsHangupResponse, next_ims_token(), nullptr, call_id,
		&response_error, error))
		return 1;
	if (response_error != kImsSuccess) {
		*error = "Qualcomm IMS rejected hangup: error=" +
			std::to_string(response_error);
		return 1;
	}
	*result = "{\"ok\":true,\"transport\":\"qti-ims-aidl\",\"slot\":\"" +
		std::string(slot->name) + "\",\"action\":\"hangup\",\"call_id\":" +
		std::to_string(call_id) + "}";
	return 0;
}

static int vendor_ims_dtmf_bound(VendorImsSlot *slot, int32_t call_id,
				 const char *tone, std::string *result,
				 std::string *error)
{
	if (call_id <= 0 || !valid_dtmf_tone(tone)) {
		*error = "invalid IMS DTMF request";
		return 2;
	}
	const int32_t active_call_id = slot->active_call_id.load();
	if (active_call_id <= 0 || active_call_id != call_id) {
		*error = "IMS DTMF call ID is not the active call";
		return 1;
	}
	int32_t response_error = 0;
	if (!vendor_ims_call_transaction(slot->service.get(), &slot->call,
		kImsSendDtmf, kImsSendDtmfResponse, next_ims_token(), tone,
		call_id, &response_error, error))
		return 1;
	if (response_error != kImsSuccess) {
		*error = "Qualcomm IMS rejected DTMF: error=" +
			std::to_string(response_error);
		return 1;
	}
	*result = "{\"ok\":true,\"transport\":\"qti-ims-aidl\",\"slot\":\"" +
		std::string(slot->name) + "\",\"action\":\"dtmf\",\"call_id\":" +
		std::to_string(call_id) + ",\"digit\":\"" + tone + "\"}";
	std::fprintf(stderr, "IMS DTMF sent: slot=%s call_id=%d digit=%c\n",
		slot->name, call_id, tone[0]);
	return 0;
}

struct ImsBridgeRequest {
	uint32_t magic;
	uint32_t command;
	uint32_t slot_index;
	uint32_t number_length;
	uint32_t text_length;
};

struct ImsBridgeResponse {
	int32_t status;
	uint32_t text_length;
};

static bool read_full(int fd, void *buffer, size_t length)
{
	char *cursor = static_cast<char *>(buffer);
	while (length) {
		ssize_t received = read(fd, cursor, length);
		if (received < 0 && errno == EINTR) continue;
		if (received <= 0) return false;
		cursor += received;
		length -= static_cast<size_t>(received);
	}
	return true;
}

static bool write_full(int fd, const void *buffer, size_t length)
{
	const char *cursor = static_cast<const char *>(buffer);
	while (length) {
		ssize_t written = write(fd, cursor, length);
		if (written < 0 && errno == EINTR) continue;
		if (written <= 0) return false;
		cursor += written;
		length -= static_cast<size_t>(written);
	}
	return true;
}

static std::string json_bool_array(const std::vector<bool>& values)
{
	std::string result = "[";
	for (size_t i = 0; i < values.size(); ++i) {
		if (i) result += ",";
		result += values[i] ? "true" : "false";
	}
	result += "]";
	return result;
}

struct ImsBridgeSlotState {
	VendorImsRegistrationReply registration;
	VendorImsServiceReply services;
	std::string format;
	int32_t set_error = -1;
	int32_t registration_change_error = -1;
	int32_t tty_error = -1;
	int32_t config_error = -1;
	bool ready = false;
};

struct ImsBridgeRuntimeState {
	std::mutex mutex;
	const char *preferred_slot = "slot1";
	ImsBridgeSlotState slots[2];
};

static void write_ims_slot_state(FILE *file, const char *name,
		const ImsBridgeSlotState& state)
{
	std::fprintf(file,
		"\"%s\":{\"ready\":%s,\"sms_format\":\"%s\","
		"\"registration_error\":%d,\"registration_state\":%d,"
		"\"registration_radio_tech\":%d,\"service_error\":%d,"
		"\"sms_service_ready\":%s,\"tty_error\":%d,"
		"\"config_error\":%d,\"set_service_error\":%d,"
		"\"registration_change_error\":%d}",
		name, state.ready ? "true" : "false",
		json_escape(state.format).c_str(), state.registration.error,
		state.registration.state, state.registration.radio_tech,
		state.services.error,
		vendor_ims_sms_service_ready(state.services) ? "true" : "false",
		state.tty_error, state.config_error, state.set_error,
		state.registration_change_error);
}

static void write_ims_bridge_state(ImsBridgeRuntimeState *runtime,
		const VendorImsSubConfigReply *subconfigs,
		VendorImsIndicationCounters *indications)
{
	uint64_t counts[2][64] = {};
	transaction_code_t last[2] = {};
	{
		std::lock_guard<std::mutex> lock(indications->mutex);
		std::memcpy(counts, indications->count, sizeof(counts));
		std::memcpy(last, indications->last_code, sizeof(last));
	}
	std::lock_guard<std::mutex> state_lock(runtime->mutex);
	const int preferred_index =
		std::strcmp(runtime->preferred_slot, "slot2") == 0 ? 1 : 0;
	const auto& preferred = runtime->slots[preferred_index];
	std::string temporary = std::string(kImsBridgeState) + ".tmp";
	FILE *file = std::fopen(temporary.c_str(), "w");
	if (!file) return;
	std::fprintf(file,
		"{\"ok\":true,\"daemon\":true,\"ready\":%s,\"slot\":\"%s\",\"boot_id\":\"%s\",\"callbacks\":{\"slot1\":true,\"slot2\":true},\"subconfig\":{\"slot1\":{\"error\":%d,\"simult_stack_count\":%d,\"enabled\":%s},\"slot2\":{\"error\":%d,\"simult_stack_count\":%d,\"enabled\":%s}},\"tty_error\":%d,\"config_error\":%d,\"set_service_error\":%d,\"registration_change_error\":%d,\"sms_format\":\"%s\",\"registration_error\":%d,\"registration_state\":%d,\"registration_radio_tech\":%d,\"service_error\":%d,\"sms_service_ready\":%s,\"slots\":{",
		preferred.ready ? "true" : "false", runtime->preferred_slot,
		current_boot_id().c_str(),
		subconfigs[0].error, subconfigs[0].simult_stack_count,
		json_bool_array(subconfigs[0].ims_stack_enabled).c_str(),
		subconfigs[1].error, subconfigs[1].simult_stack_count,
		json_bool_array(subconfigs[1].ims_stack_enabled).c_str(),
		preferred.tty_error, preferred.config_error,
		preferred.set_error, preferred.registration_change_error,
		json_escape(preferred.format).c_str(), preferred.registration.error,
		preferred.registration.state, preferred.registration.radio_tech,
		preferred.services.error,
		vendor_ims_sms_service_ready(preferred.services) ? "true" : "false");
	write_ims_slot_state(file, "slot1", runtime->slots[0]);
	std::fputc(',', file);
	write_ims_slot_state(file, "slot2", runtime->slots[1]);
	std::fprintf(file,
		"},\"indications\":{\"slot1\":{\"last\":%u,\"call_state\":%llu,\"ring\":%llu,\"registration\":%llu,\"service\":%llu,\"radio\":%llu,\"sms_report\":%llu,\"incoming_sms\":%llu},\"slot2\":{\"last\":%u,\"call_state\":%llu,\"ring\":%llu,\"registration\":%llu,\"service\":%llu,\"radio\":%llu,\"sms_report\":%llu,\"incoming_sms\":%llu}}}\n",
		last[0], (unsigned long long)counts[0][1],
		(unsigned long long)counts[0][2], (unsigned long long)counts[0][4],
		(unsigned long long)counts[0][6], (unsigned long long)counts[0][7],
		(unsigned long long)counts[0][22], (unsigned long long)counts[0][23],
		last[1], (unsigned long long)counts[1][1],
		(unsigned long long)counts[1][2], (unsigned long long)counts[1][4],
		(unsigned long long)counts[1][6], (unsigned long long)counts[1][7],
		(unsigned long long)counts[1][22], (unsigned long long)counts[1][23]);
	std::fclose(file);
	std::rename(temporary.c_str(), kImsBridgeState);
}

static void ims_bridge_monitor(VendorImsSlot *active, int active_index,
		const VendorImsSubConfigReply *subconfigs,
		VendorImsIndicationCounters *indications,
		ImsBridgeRuntimeState *runtime, std::atomic<bool> *ready)
{
	int32_t token = static_cast<int32_t>(
		std::chrono::steady_clock::now().time_since_epoch().count() &
		0x3fffffff);
	int32_t set_error = -1;
	int32_t registration_change_error = -1;
	int32_t tty_error = -1;
	int32_t config_error = -1;
	std::string error;
	/* Mirror the requests between stack discovery and enable in the
	 * stock QImsService trace. These affect only the Qualcomm IMS application
	 * layer; none of them touches DSI, rmnet, routes or radio power. The
	 * getSubConfig enabled vector describes the framework's initial selection,
	 * while simult_stack_count reports that both modem stacks are supported.
	 * Each physical SIM therefore gets the stock enable sequence independently. */
	VendorImsRegistrationReply initial_registration;
	if (!vendor_ims_query_registration(active->service.get(), &active->diag,
		++token, &initial_registration, &error))
		std::fprintf(stderr, "%s\n", error.c_str());
	active->diag.prepare_control(++token, kImsSetUiTtyModeResponse);
	if (!vendor_ims_set_ui_tty_off(active->service.get(), token, &error) ||
	    !active->diag.wait_control(&tty_error))
		tty_error = -1;
	active->diag.prepare_control(++token, kImsSetConfigResponse);
	if (!vendor_ims_set_rtt_config_off(active->service.get(), token, &error) ||
	    !active->diag.wait_control(&config_error))
		config_error = -1;
	active->diag.prepare_control(++token, kImsSetServiceStatusResponse);
	if (!vendor_ims_set_stock_service_status(active->service.get(), token,
		&error) || !active->diag.wait_control(&set_error))
		set_error = -1;
	active->diag.prepare_control(++token,
		kImsRequestRegistrationChangeResponse);
	if (!vendor_ims_request_registration(active->service.get(), token,
		&error) || !active->diag.wait_control(&registration_change_error))
		registration_change_error = -1;

	VendorImsRegistrationReply last_registration;
	VendorImsServiceReply last_services;
	std::string last_format;
	bool last_ready = false;
	for (;;) {
		VendorImsRegistrationReply registration;
		VendorImsServiceReply services;
		std::string format;
		error.clear();
		bool queried = vendor_ims_get_sms_format(active->service.get(),
			&format, &error) &&
			vendor_ims_query_registration(active->service.get(),
				&active->diag, ++token, &registration, &error) &&
			vendor_ims_query_services(active->service.get(),
				&active->diag, ++token, &services, &error);
		/* A query timeout means the proprietary callback path was busy; it
		 * does not mean the modem deregistered. Preserve the last complete
		 * snapshot, while a complete negative response still clears ready. */
		if (queried) {
			last_registration = std::move(registration);
			last_services = std::move(services);
			last_format = std::move(format);
			last_ready = set_error == kImsSuccess &&
				registration_change_error == kImsSuccess &&
				last_registration.error == kImsSuccess &&
				last_registration.state == 1 &&
				last_services.error == kImsSuccess &&
				last_format == "3gpp" &&
				vendor_ims_sms_service_ready(last_services);
		} else if (!error.empty()) {
			std::fprintf(stderr, "IMS monitor kept last state: %s\n",
				error.c_str());
		}
		ready->store(last_ready);
		{
			std::lock_guard<std::mutex> lock(runtime->mutex);
			auto& state = runtime->slots[active_index];
			state.registration = last_registration;
			state.services = last_services;
			state.format = last_format;
			state.set_error = set_error;
			state.registration_change_error = registration_change_error;
			state.tty_error = tty_error;
			state.config_error = config_error;
			state.ready = last_ready;
		}
		write_ims_bridge_state(runtime, subconfigs, indications);
		std::this_thread::sleep_for(std::chrono::seconds(
			last_ready ? 15 : 3));
	}
}

static int create_ims_bridge_socket(std::string *error)
{
	int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	struct sockaddr_un address = {};
	if (fd < 0) {
		*error = "cannot create IMS bridge socket: " +
			std::string(std::strerror(errno));
		return -1;
	}
	address.sun_family = AF_UNIX;
	if (std::strlen(kImsBridgeSocket) >= sizeof(address.sun_path)) {
		close(fd);
		*error = "IMS bridge socket path is too long";
		return -1;
	}
	std::strcpy(address.sun_path, kImsBridgeSocket);
	unlink(kImsBridgeSocket);
	if (bind(fd, reinterpret_cast<struct sockaddr *>(&address),
		sizeof(address)) < 0 || chmod(kImsBridgeSocket, 0660) < 0 ||
		listen(fd, 8) < 0) {
		*error = "cannot publish IMS bridge socket: " +
			std::string(std::strerror(errno));
		close(fd);
		return -1;
	}
	return fd;
}

static int vendor_ims_daemon(const char *active_slot_name)
{
	VendorImsSlot slots[2];
	VendorImsSubConfigReply subconfigs[2];
	VendorImsIndicationCounters indications;
	ImsBridgeRuntimeState runtime;
	std::atomic<bool> ready[2] = { false, false };
	std::mutex action_mutex;
	std::string error;
	slots[0].name = "slot1";
	slots[1].name = "slot2";

	ABinderProcess_setThreadPoolMaxThreadCount(8);
	ABinderProcess_startThreadPool();
	for (int attempt = 0; attempt < 180; ++attempt) {
		for (int index = 0; index < 2; ++index) {
			auto& slot = slots[index];
			if (slot.attached) continue;
			error.clear();
			slot.service = get_vendor_ims_service(slot.name, &error);
			if (!slot.service.get()) continue;
			slot.context.sms = &slot.sms;
			slot.context.diag = &slot.diag;
			slot.context.call = &slot.call;
			slot.context.indications = &indications;
			slot.context.active_call_id = &slot.active_call_id;
			slot.context.active_call_state = &slot.active_call_state;
			slot.context.service = slot.service.get();
			slot.context.slot_name = slot.name;
			slot.context.slot_index = index;
			slot.response = ndk::SpAIBinder(AIBinder_new(
				ims_response_class(), &slot.context));
			slot.indication = ndk::SpAIBinder(AIBinder_new(
				ims_indication_class(), &slot.context));
			if (!slot.response.get() || !slot.indication.get()) continue;
			AIBinder_markVendorStability(slot.response.get());
			AIBinder_markVendorStability(slot.indication.get());
			if (vendor_ims_set_callbacks(slot.service.get(),
				slot.response.get(), slot.indication.get(), &error))
				slot.attached = true;
		}
		if (slots[0].attached && slots[1].attached) break;
		std::this_thread::sleep_for(std::chrono::seconds(1));
	}
	if (!slots[0].attached || !slots[1].attached) {
		std::fprintf(stderr,
			"cannot attach persistent Qualcomm IMS callbacks to both slots\n");
		return 1;
	}
	/* Stock QImsService issues an initial read-only service query for each SIM
	 * before asking which IMS stack is active. */
	VendorImsServiceReply initial_services[2];
	int32_t initial_token = static_cast<int32_t>(
		std::chrono::steady_clock::now().time_since_epoch().count() &
		0x3fffffff);
	for (int index = 0; index < 2; ++index) {
		error.clear();
		if (!vendor_ims_query_services(slots[index].service.get(),
			&slots[index].diag, ++initial_token, &initial_services[index],
			&error))
			std::fprintf(stderr, "%s initial service query: %s\n",
				slots[index].name, error.c_str());
	}
	int32_t subconfig_token = static_cast<int32_t>(
		std::chrono::steady_clock::now().time_since_epoch().count() &
		0x3fffffff);
	for (int index = 0; index < 2; ++index) {
		error.clear();
		if (!vendor_ims_query_subconfig(slots[index].service.get(),
			&slots[index].diag, ++subconfig_token, &subconfigs[index],
			&error))
			std::fprintf(stderr, "%s %s\n", slots[index].name,
				error.c_str());
	}
	runtime.preferred_slot = active_slot_name;
	write_ims_bridge_state(&runtime, subconfigs, &indications);
	for (int index = 0; index < 2; ++index)
		std::thread(ims_bridge_monitor, &slots[index], index, subconfigs,
			&indications, &runtime, &ready[index]).detach();

	int server = create_ims_bridge_socket(&error);
	if (server < 0) {
		std::fprintf(stderr, "%s\n", error.c_str());
		return 1;
	}
	for (;;) {
		int client = accept4(server, nullptr, nullptr, SOCK_CLOEXEC);
		if (client < 0) {
			if (errno == EINTR) continue;
			std::fprintf(stderr, "IMS bridge accept failed: %s\n",
				std::strerror(errno));
			continue;
		}
		ImsBridgeRequest request = {};
		std::string response_text;
		int32_t response_status = 1;
		bool valid_request = read_full(client, &request, sizeof(request)) &&
			request.magic == kImsBridgeMagic && request.slot_index <= 1;
		if (valid_request && request.command == kImsBridgeSend)
			valid_request = request.number_length > 0 &&
				request.number_length <= 64 && request.text_length > 0 &&
				request.text_length <= 4096;
		else if (valid_request && request.command == kImsBridgeDial)
			valid_request = request.number_length > 0 &&
				request.number_length <= 64 && request.text_length == 0;
		else if (valid_request && request.command == kImsBridgeAnswer)
			valid_request = request.number_length == 0 &&
				request.text_length == 0;
		else if (valid_request && request.command == kImsBridgeHangup)
			valid_request = request.number_length > 0 &&
				request.text_length == 0;
		else if (valid_request && request.command == kImsBridgeDtmf) {
			char tone[2] = {
				static_cast<char>(request.text_length & 0xff), '\0'
			};
			valid_request = request.number_length > 0 &&
				request.number_length <= 0x7fffffffU &&
				request.text_length <= 0x7f && valid_dtmf_tone(tone);
		}
		else
			valid_request = false;
		if (!valid_request) {
			response_text = "invalid IMS bridge request";
		} else {
			const bool has_number = request.command == kImsBridgeSend ||
				request.command == kImsBridgeDial;
			const bool has_text = request.command == kImsBridgeSend;
			std::string number(has_number ? request.number_length : 0, '\0');
			std::string text(has_text ? request.text_length : 0, '\0');
			if ((has_number && !read_full(client, number.data(), number.size())) ||
			    (has_text && !read_full(client, text.data(), text.size()))) {
				response_text = "incomplete IMS bridge request";
			} else if (!ready[request.slot_index].load()) {
				response_text = "Qualcomm IMS application layer is not ready";
			} else {
				std::lock_guard<std::mutex> lock(action_mutex);
				auto *slot = &slots[request.slot_index];
				if (request.command == kImsBridgeSend)
					response_status = vendor_ims_send_bound(
						slot->service.get(), &slot->sms, slot->name,
						number.c_str(), text.c_str(),
						&response_text, &error);
				else if (request.command == kImsBridgeDial)
					response_status = vendor_ims_dial_bound(slot,
						number.c_str(), &response_text, &error);
				else if (request.command == kImsBridgeAnswer)
					response_status = vendor_ims_answer_bound(slot,
						&response_text, &error);
				else if (request.command == kImsBridgeHangup)
					response_status = vendor_ims_hangup_bound(slot,
						static_cast<int32_t>(request.number_length),
						&response_text, &error);
				else {
					char tone[2] = {
						static_cast<char>(request.text_length), '\0'
					};
					response_status = vendor_ims_dtmf_bound(slot,
						static_cast<int32_t>(request.number_length),
						tone, &response_text, &error);
				}
				if (response_status != 0) response_text = error;
			}
		}
		ImsBridgeResponse response = {
			response_status, static_cast<uint32_t>(response_text.size())
		};
		write_full(client, &response, sizeof(response));
		write_full(client, response_text.data(), response_text.size());
		close(client);
	}
}

static int vendor_ims_send(const char *slot, const char *number,
			   const char *text)
{
	int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	struct sockaddr_un address = {};
	if (fd < 0) {
		std::fprintf(stderr, "cannot create IMS bridge client: %s\n",
			std::strerror(errno));
		return 1;
	}
	address.sun_family = AF_UNIX;
	std::strcpy(address.sun_path, kImsBridgeSocket);
	if (connect(fd, reinterpret_cast<struct sockaddr *>(&address),
		sizeof(address)) < 0) {
		std::fprintf(stderr, "persistent IMS bridge is unavailable: %s\n",
			std::strerror(errno));
		close(fd);
		return 1;
	}
	ImsBridgeRequest request = {
		kImsBridgeMagic, kImsBridgeSend,
		std::strcmp(slot, "slot1") == 0 ? 0U : 1U,
		static_cast<uint32_t>(std::strlen(number)),
		static_cast<uint32_t>(std::strlen(text)),
	};
	if (!write_full(fd, &request, sizeof(request)) ||
	    !write_full(fd, number, request.number_length) ||
	    !write_full(fd, text, request.text_length)) {
		std::fprintf(stderr, "cannot submit request to persistent IMS bridge\n");
		close(fd);
		return 1;
	}
	ImsBridgeResponse response = {};
	if (!read_full(fd, &response, sizeof(response)) ||
	    response.text_length > 16384) {
		std::fprintf(stderr, "invalid response from persistent IMS bridge\n");
		close(fd);
		return 1;
	}
	std::string response_text(response.text_length, '\0');
	if (!read_full(fd, response_text.data(), response_text.size())) {
		std::fprintf(stderr, "incomplete response from persistent IMS bridge\n");
		close(fd);
		return 1;
	}
	close(fd);
	std::fprintf(response.status == 0 ? stdout : stderr, "%s\n",
		response_text.c_str());
	return response.status;
}

static int vendor_ims_call_command(const char *slot, uint32_t command,
				   const char *value, const char *tone)
{
	int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	struct sockaddr_un address = {};
	if (fd < 0) {
		std::fprintf(stderr, "cannot create IMS bridge client: %s\n",
			std::strerror(errno));
		return 1;
	}
	address.sun_family = AF_UNIX;
	std::strcpy(address.sun_path, kImsBridgeSocket);
	if (connect(fd, reinterpret_cast<struct sockaddr *>(&address),
		sizeof(address)) < 0) {
		std::fprintf(stderr, "persistent IMS bridge is unavailable: %s\n",
			std::strerror(errno));
		close(fd);
		return 1;
	}
	uint32_t argument = 0;
	if (command == kImsBridgeDial) {
		argument = static_cast<uint32_t>(std::strlen(value));
	} else if (command == kImsBridgeHangup || command == kImsBridgeDtmf) {
		char *end = nullptr;
		unsigned long parsed = std::strtoul(value, &end, 10);
		if (!value[0] || !end || *end || parsed == 0 || parsed > 0x7fffffffUL) {
			std::fprintf(stderr, "invalid IMS call ID\n");
			close(fd);
			return 2;
		}
		argument = static_cast<uint32_t>(parsed);
		if (command == kImsBridgeDtmf && !valid_dtmf_tone(tone)) {
			std::fprintf(stderr, "invalid IMS DTMF digit\n");
			close(fd);
			return 2;
		}
	}
	ImsBridgeRequest request = {
		kImsBridgeMagic, command,
		std::strcmp(slot, "slot1") == 0 ? 0U : 1U,
		argument, command == kImsBridgeDtmf ?
			static_cast<uint32_t>(static_cast<unsigned char>(tone[0])) : 0,
	};
	if (!write_full(fd, &request, sizeof(request)) ||
	    (command == kImsBridgeDial &&
	     !write_full(fd, value, request.number_length))) {
		std::fprintf(stderr, "cannot submit call request to IMS bridge\n");
		close(fd);
		return 1;
	}
	ImsBridgeResponse response = {};
	if (!read_full(fd, &response, sizeof(response)) ||
	    response.text_length > 16384) {
		std::fprintf(stderr, "invalid response from persistent IMS bridge\n");
		close(fd);
		return 1;
	}
	std::string response_text(response.text_length, '\0');
	if (!read_full(fd, response_text.data(), response_text.size())) {
		std::fprintf(stderr, "incomplete response from persistent IMS bridge\n");
		close(fd);
		return 1;
	}
	close(fd);
	std::fprintf(response.status == 0 ? stdout : stderr, "%s\n",
		response_text.c_str());
	return response.status;
}

static void print_binder_error(const char *operation,
			       const ::ndk::ScopedAStatus& status)
{
	std::fprintf(stderr, "%s failed: %s\n", operation,
		     status.getDescription().c_str());
}

static int print_ims_bridge_state()
{
	FILE *file = std::fopen(kImsBridgeState, "r");
	if (!file) {
		std::fprintf(stderr, "persistent IMS bridge state is unavailable\n");
		return 1;
	}
	char buffer[4096];
	while (std::fgets(buffer, sizeof(buffer), file))
		std::fputs(buffer, stdout);
	std::fclose(file);
	return 0;
}

int main(int argc, char **argv)
{
	if ((argc != 3 && argc != 4 && argc != 5 && argc != 6) ||
	    (argc == 3 && std::strcmp(argv[1], "probe") != 0 &&
	     std::strcmp(argv[1], "smsc") != 0 &&
	     std::strcmp(argv[1], "ims-probe") != 0 &&
	     std::strcmp(argv[1], "ims-state") != 0 &&
	     std::strcmp(argv[1], "ims-daemon") != 0 &&
	     std::strcmp(argv[1], "call-answer") != 0) ||
	    (argc == 4 && std::strcmp(argv[1], "call-dial") != 0 &&
	     std::strcmp(argv[1], "call-end") != 0) ||
	    (argc == 5 && std::strcmp(argv[1], "call-dtmf") != 0)) {
		std::fprintf(stderr,
			"usage: %s probe|ims-probe|ims-state|ims-daemon|smsc|call-answer slot1|slot2 | call-dial|call-end slot1|slot2 VALUE | call-dtmf slot1|slot2 CALL_ID DIGIT | send slot1|slot2 ims|gsm NUMBER TEXT\n",
			argv[0]);
		return 2;
	}
	if (std::strcmp(argv[2], "slot1") != 0 &&
	    std::strcmp(argv[2], "slot2") != 0) {
		std::fprintf(stderr, "invalid radio slot: %s\n", argv[2]);
		return 2;
	}

	LeakedStrongPointer process_state = init_process_state("/dev/binder");
	if (!process_state.pointer) {
		std::fprintf(stderr, "cannot initialize libbinder on /dev/binder\n");
		return 2;
	}
	if (argc == 3 && std::strcmp(argv[1], "ims-probe") == 0) {
		std::string error;
		std::string instance = vendor_ims_instance(argv[2]);
		ABinderProcess_setThreadPoolMaxThreadCount(1);
		ABinderProcess_startThreadPool();
		ndk::SpAIBinder service = get_vendor_ims_service(argv[2], &error);
		std::printf("{\"ok\":%s,\"slot\":\"%s\",\"instance\":\"%s\",\"declared\":%s,\"present\":%s}\n",
			service.get() ? "true" : "false", argv[2],
			instance.c_str(),
			AServiceManager_isDeclared(instance.c_str()) ? "true" : "false",
			service.get() ? "true" : "false");
		if (!service.get())
			std::fprintf(stderr, "%s\n", error.c_str());
		return service.get() ? 0 : 1;
	}
	if (argc == 3 && std::strcmp(argv[1], "ims-state") == 0)
		return print_ims_bridge_state();
	if (argc == 3 && std::strcmp(argv[1], "ims-daemon") == 0)
		return vendor_ims_daemon(argv[2]);
	if (argc == 3 && std::strcmp(argv[1], "call-answer") == 0)
		return vendor_ims_call_command(argv[2], kImsBridgeAnswer, "", "");
	if (argc == 4 && std::strcmp(argv[1], "call-dial") == 0)
		return vendor_ims_call_command(argv[2], kImsBridgeDial, argv[3], "");
	if (argc == 4 && std::strcmp(argv[1], "call-end") == 0)
		return vendor_ims_call_command(argv[2], kImsBridgeHangup, argv[3], "");
	if (argc == 5 && std::strcmp(argv[1], "call-dtmf") == 0)
		return vendor_ims_call_command(argv[2], kImsBridgeDtmf,
			argv[3], argv[4]);
	if (argc == 6 && std::strcmp(argv[1], "send") == 0 &&
	    std::strcmp(argv[3], "ims") == 0)
		return vendor_ims_send(argv[2], argv[4], argv[5]);

	std::string instance =
		"android.hardware.radio.messaging.IRadioMessaging/";
	instance += argv[2];

	ndk::SpAIBinder binder(AServiceManager_checkService(instance.c_str()));
	if (argc == 3 && std::strcmp(argv[1], "probe") == 0) {
		std::printf("{\"ok\":%s,\"slot\":\"%s\",\"declared\":%s,\"present\":%s}\n",
			binder.get() ? "true" : "false", argv[2],
			AServiceManager_isDeclared(instance.c_str()) ? "true" : "false",
			binder.get() ? "true" : "false");
		return binder.get() ? 0 : 1;
	}
	if (argc == 6 && (std::strcmp(argv[1], "send") != 0 ||
	    (std::strcmp(argv[3], "ims") != 0 &&
	     std::strcmp(argv[3], "gsm") != 0))) {
		std::fprintf(stderr, "invalid radio SMS command\n");
		return 2;
	}
	if (!binder.get()) {
		std::fprintf(stderr, "Radio Messaging AIDL service is unavailable on %s\n",
			     argv[2]);
		return 1;
	}

	auto service = messaging::IRadioMessaging::fromBinder(binder);
	if (!service) {
		std::fprintf(stderr, "cannot bind Radio Messaging service on %s\n",
			     argv[2]);
		return 1;
	}
	ABinderProcess_setThreadPoolMaxThreadCount(1);
	ABinderProcess_startThreadPool();
	auto state = std::make_shared<ResponseState>();
	auto response = ndk::SharedRefBase::make<RadioMessagingResponse>(state);
	auto indication = ndk::SharedRefBase::make<RadioMessagingIndication>();
	auto status = service->setResponseFunctions(response, indication);
	if (!status.isOk()) {
		print_binder_error("setResponseFunctions", status);
		return 1;
	}

	int32_t serial = static_cast<int32_t>(
		std::chrono::steady_clock::now().time_since_epoch().count() &
		0x3fffffff);
	SmsReply smsc_reply;
	state->prepare(++serial, true);
	status = service->getSmscAddress(serial);
	if (!status.isOk()) {
		print_binder_error("getSmscAddress", status);
		return 1;
	}
	if (!state->wait(&smsc_reply)) {
		std::fprintf(stderr, "getSmscAddress response timed out\n");
		return 1;
	}
	if (smsc_reply.info.type == radio::RadioResponseType::SOLICITED_ACK_EXP)
		service->responseAcknowledgement();
	const bool use_network_smsc = argc == 6 &&
		smsc_reply.info.error == radio::RadioError::INVALID_MODEM_STATE;
	if (smsc_reply.info.error != radio::RadioError::NONE &&
	    !use_network_smsc) {
		std::fprintf(stderr, "getSmscAddress failed: RadioError=%s (%d)\n",
			radio::toString(smsc_reply.info.error).c_str(),
			static_cast<int32_t>(smsc_reply.info.error));
		return 1;
	}
	if (argc == 3) {
		std::printf("{\"ok\":true,\"slot\":\"%s\",\"smsc\":\"%s\"}\n",
			argv[2], json_escape(smsc_reply.smsc).c_str());
		return 0;
	}

	std::vector<std::string> pdus;
	std::string pdu_error;
	std::string smsc_pdu;
	/* Qualcomm can reject getSmscAddress while the radio is online and still
	 * accept SEND_SMS with an empty SMSC field.  That is Android's normal
	 * request for the modem/SIM default service centre; it is not an invented
	 * address.  Keep every other SMSC failure fatal. */
	if (!use_network_smsc &&
	    !encode_smsc_pdu(smsc_reply.smsc, &smsc_pdu, &pdu_error)) {
		std::fprintf(stderr, "%s\n", pdu_error.c_str());
		return 1;
	}
	if (!build_submit_pdus(argv[4], argv[5], &pdus, &pdu_error)) {
		std::fprintf(stderr, "%s\n", pdu_error.c_str());
		return 2;
	}

	int32_t last_message_ref = 0;
	std::string last_ack;
	for (size_t part = 0; part < pdus.size(); ++part) {
		messaging::GsmSmsMessage gsm;
		SmsReply reply;

		gsm.smscPdu = smsc_pdu;
		gsm.pdu = pdus[part];
		++serial;
		state->prepare(serial);
		if (std::strcmp(argv[3], "ims") == 0) {
			messaging::ImsSmsMessage ims;
			ims.tech = radio::RadioTechnologyFamily::THREE_GPP;
			ims.retry = false;
			ims.messageRef = 0;
			ims.gsmMessage.push_back(gsm);
			status = service->sendImsSms(serial, ims);
		} else {
			status = service->sendSms(serial, gsm);
		}
		if (!status.isOk()) {
			print_binder_error("Radio Messaging send transaction", status);
			return 1;
		}
		if (!state->wait(&reply)) {
			std::fprintf(stderr,
				     "Radio Messaging response timed out on segment %zu/%zu\n",
				     part + 1, pdus.size());
			return 1;
		}
		if (reply.info.type == radio::RadioResponseType::SOLICITED_ACK_EXP)
			service->responseAcknowledgement();
		if (reply.info.error != radio::RadioError::NONE) {
			std::fprintf(stderr,
				     "Radio Messaging rejected segment %zu/%zu: RadioError=%s (%d), modem_error=%d\n",
				     part + 1, pdus.size(),
				     radio::toString(reply.info.error).c_str(),
				     static_cast<int32_t>(reply.info.error),
				     reply.result.errorCode);
			return 1;
		}
		last_message_ref = reply.result.messageRef;
		last_ack = reply.result.ackPDU;
	}

	std::printf("{\"ok\":true,\"transport\":\"aidl-%s\",\"slot\":\"%s\",\"message_id\":%d,\"segments\":%zu,\"ack_pdu\":\"%s\"}\n",
		    argv[3], argv[2], last_message_ref, pdus.size(),
		    last_ack.c_str());
	return 0;
}

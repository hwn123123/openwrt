// SPDX-License-Identifier: Apache-2.0

#include <android/binder_auto_utils.h>
#include <android/binder_manager.h>

#include <cstdio>

/*
 * The standard android.hardware.radio.* stable-AIDL HALs are registered with
 * the framework servicemanager on /dev/binder. QCRIL also opens /dev/vndbinder
 * for Qualcomm vendor services, but probing IRadio* there produces a false
 * negative. ProcessState::initWithDriver() returns android::sp<T>, whose ABI
 * is one pointer stored through a hidden return slot. Keep that one reference
 * until process exit rather than importing private libbinder C++ headers.
 */
struct LeakedStrongPointer {
	void *pointer;
	~LeakedStrongPointer() { pointer = nullptr; }
};

extern "C" LeakedStrongPointer init_process_state(const char *driver)
	__asm__("_ZN7android12ProcessState14initWithDriverEPKc");

static bool probe(const char *instance)
{
	const bool declared = AServiceManager_isDeclared(instance);
	ndk::SpAIBinder binder(AServiceManager_checkService(instance));
	const bool present = binder.get() != nullptr;
	std::printf("{\"instance\":\"%s\",\"declared\":%s,\"present\":%s}\n",
		    instance, declared ? "true" : "false",
		    present ? "true" : "false");
	std::fflush(stdout);
	return present;
}

int main(int argc, char **argv)
{
	static const char *const instances[] = {
		"android.hardware.radio.messaging.IRadioMessaging/slot1",
		"android.hardware.radio.messaging.IRadioMessaging/slot2",
		"android.hardware.radio.voice.IRadioVoice/slot1",
		"android.hardware.radio.voice.IRadioVoice/slot2",
		"android.hardware.radio.data.IRadioData/slot1",
		"android.hardware.radio.data.IRadioData/slot2",
		"android.hardware.radio.sim.IRadioSim/slot1",
		"android.hardware.radio.sim.IRadioSim/slot2",
		"android.hardware.radio.network.IRadioNetwork/slot1",
		"android.hardware.radio.network.IRadioNetwork/slot2",
		"android.hardware.radio.modem.IRadioModem/slot1",
		"android.hardware.radio.modem.IRadioModem/slot2",
		"android.hardware.radio.config.IRadioConfig/default",
	};
	LeakedStrongPointer process_state = init_process_state("/dev/binder");
	bool present = false;

	if (!process_state.pointer) {
		std::fprintf(stderr, "cannot initialize libbinder on /dev/binder\n");
		return 2;
	}
	if (argc > 1) {
		bool all_present = true;

		for (int i = 1; i < argc; ++i)
			all_present = probe(argv[i]) && all_present;
		return all_present ? 0 : 1;
	}
	for (const char *instance : instances)
		present = probe(instance) || present;
	return present ? 0 : 1;
}

// SPDX-License-Identifier: Apache-2.0

#include <android/binder_status.h>
#include <android/binder_process.h>
#include <cerrno>
#include <cstdio>
#include <ctime>
#include <pthread.h>

#include "nfa_api.h"

// NXP's SN100/SN220 branch widens the polling mask to 16 bits while the
// matching AOSP 14 public header still declares an 8-bit mask.
extern tNFA_STATUS NFA_EnablePolling(uint16_t technology_mask);

struct LeakedStrongPointer {
	void *pointer;
	~LeakedStrongPointer() { pointer = nullptr; }
};

extern "C" LeakedStrongPointer init_process_state(const char *driver)
	__asm__("_ZN7android12ProcessState14initWithDriverEPKc");

class NfcAdaptation {
public:
	static NfcAdaptation &GetInstance();
	void Initialize();
	void Finalize();
	tHAL_NFC_ENTRY *GetHalEntryFuncs();
};

enum class WaitFor {
	none,
	enabled,
	polling,
	discovery,
	discovery_stopped,
	polling_stopped,
	disabled,
};

static pthread_mutex_t event_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t event_cond = PTHREAD_COND_INITIALIZER;
static WaitFor expected_event = WaitFor::none;
static tNFA_STATUS event_status = NFA_STATUS_FAILED;

static void *binder_thread_main(void *)
{
	ABinderProcess_joinThreadPool();
	std::fprintf(stderr, "Binder callback thread returned\n");
	return nullptr;
}

static void notify(WaitFor event, tNFA_STATUS status)
{
	pthread_mutex_lock(&event_lock);
	if (expected_event == event) {
		event_status = status;
		expected_event = WaitFor::none;
		pthread_cond_signal(&event_cond);
	}
	pthread_mutex_unlock(&event_lock);
}

static void device_management_callback(uint8_t event,
				       tNFA_DM_CBACK_DATA *data)
{
	if (event == NFA_DM_ENABLE_EVT)
		notify(WaitFor::enabled, data ? data->status : NFA_STATUS_FAILED);
	else if (event == NFA_DM_DISABLE_EVT)
		notify(WaitFor::disabled, data ? data->status : NFA_STATUS_FAILED);
}

static void connection_callback(uint8_t event, tNFA_CONN_EVT_DATA *data)
{
	tNFA_STATUS status = data ? data->status : NFA_STATUS_FAILED;
	switch (event) {
	case NFA_POLL_ENABLED_EVT:
		notify(WaitFor::polling, status);
		break;
	case NFA_RF_DISCOVERY_STARTED_EVT:
		notify(WaitFor::discovery, status);
		break;
	case NFA_RF_DISCOVERY_STOPPED_EVT:
		notify(WaitFor::discovery_stopped, status);
		break;
	case NFA_POLL_DISABLED_EVT:
		notify(WaitFor::polling_stopped, status);
		break;
	default:
		break;
	}
}

static void prepare_wait(WaitFor event)
{
	pthread_mutex_lock(&event_lock);
	expected_event = event;
	event_status = NFA_STATUS_FAILED;
	pthread_mutex_unlock(&event_lock);
}

static bool wait_for_event(unsigned int seconds)
{
	struct timespec deadline;
	clock_gettime(CLOCK_REALTIME, &deadline);
	deadline.tv_sec += seconds;

	pthread_mutex_lock(&event_lock);
	while (expected_event != WaitFor::none) {
		int result = pthread_cond_timedwait(&event_cond, &event_lock,
						    &deadline);
		if (result == ETIMEDOUT) {
			expected_event = WaitFor::none;
			pthread_mutex_unlock(&event_lock);
			return false;
		}
	}
	bool ok = event_status == NFA_STATUS_OK;
	pthread_mutex_unlock(&event_lock);
	return ok;
}

static bool run_and_wait(WaitFor event, tNFA_STATUS status,
			 unsigned int seconds = 8)
{
	if (status != NFA_STATUS_OK) {
		pthread_mutex_lock(&event_lock);
		expected_event = WaitFor::none;
		pthread_mutex_unlock(&event_lock);
		return false;
	}
	return wait_for_event(seconds);
}

int main()
{
	LeakedStrongPointer process_state = init_process_state("/dev/binder");
	if (!process_state.pointer) {
		std::fprintf(stderr, "cannot initialize Binder on /dev/binder\n");
		return 2;
	}
	if (!ABinderProcess_setThreadPoolMaxThreadCount(1)) {
		std::fprintf(stderr, "cannot configure Binder callback thread\n");
		return 2;
	}
	pthread_t binder_thread;
	if (pthread_create(&binder_thread, nullptr, binder_thread_main, nullptr) != 0) {
		std::fprintf(stderr, "cannot create Binder callback thread\n");
		return 2;
	}
	pthread_detach(binder_thread);

	NfcAdaptation &adaptation = NfcAdaptation::GetInstance();
	adaptation.Initialize();
	NFA_Init(adaptation.GetHalEntryFuncs());

	prepare_wait(WaitFor::enabled);
	bool enabled = run_and_wait(
		WaitFor::enabled,
		NFA_Enable(device_management_callback, connection_callback));
	if (!enabled) {
		std::fprintf(stderr, "NFA enable failed or timed out\n");
		adaptation.Finalize();
		return 3;
	}

	const uint16_t mask =
		NFA_TECHNOLOGY_MASK_A | NFA_TECHNOLOGY_MASK_B |
		NFA_TECHNOLOGY_MASK_F | NFA_TECHNOLOGY_MASK_V;
	prepare_wait(WaitFor::polling);
	bool polling = run_and_wait(WaitFor::polling, NFA_EnablePolling(mask));

	bool discovery = false;
	if (polling) {
		prepare_wait(WaitFor::discovery);
		discovery = run_and_wait(WaitFor::discovery,
					       NFA_StartRfDiscovery());
	}

	std::printf("{\"ok\":%s,\"nfa_enabled\":true,"
		    "\"polling\":%s,\"rf_discovery\":%s}\n",
		    polling && discovery ? "true" : "false",
		    polling ? "true" : "false",
		    discovery ? "true" : "false");
	std::fflush(stdout);

	if (discovery) {
		prepare_wait(WaitFor::discovery_stopped);
		run_and_wait(WaitFor::discovery_stopped, NFA_StopRfDiscovery(), 5);
	}
	if (polling) {
		prepare_wait(WaitFor::polling_stopped);
		run_and_wait(WaitFor::polling_stopped, NFA_DisablePolling(), 5);
	}
	prepare_wait(WaitFor::disabled);
	run_and_wait(WaitFor::disabled, NFA_Disable(true), 5);
	adaptation.Finalize();
	return polling && discovery ? 0 : 4;
}

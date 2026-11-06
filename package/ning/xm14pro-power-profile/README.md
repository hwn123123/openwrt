# NWRT power integration

This package applies device CPU/cluster-idle settings and supplies the framework
power-policy owner absent from OpenWrt. It does not modify Xiaomi's kernel,
SystemSuspend binary, radio stack or WLAN firmware.

`nwrt-power-policy` registers a lifetime Binder token with
`suspend_control_internal` and holds a standard `ISystemSuspend` partial wake
lock while the display is lit, USB data is attached, PCM audio is active, or
there is recent power-key / sustained network activity. Stock userspace and
kernel wake locks still veto suspension. Power-key input is monitored directly
so a wake gesture gives the display process time to light the panel.

After 30 seconds of startup grace, screen-off idle can enter normal automatic
deep sleep. Firmware controls AP beacons and the stock WLAN suspend/resume/WoW
path; idle does not mean stopping hostapd or disconnecting clients. Network
traffic above about 32 kB/s holds the CPU awake for five seconds after the last
busy sample. Reading traffic counters does not generate probes or packets.

Before registering automatic suspend, `prepare-wifi` enables the standard
cfg80211 WoWLAN `any` trigger and the available PCI device wake switch. This
configuration is required: Linux otherwise calls `cfg80211_leave_all()` during
wiphy suspend and stops APs even while hostapd still reports AP-ENABLED.
Readiness records contain the current boot ID and wiphy sysfs inode; the native
policy keeps its wake lock until every current radio has a matching record.
The ieee80211 hotplug hook repeats setup after WLAN driver re-registration.

The service uses `enableAutosuspend` and `acquireWakeLock` only. It never calls
`forceSuspend`, writes `/sys/power/state`, or clears another owner's wake locks.
Stopping it drops the Binder token and disables its autosuspend registration.
`uci set xm14power.main.autosuspend=0; uci commit xm14power` followed by a service
restart disables automatic suspend while preserving CPU/core-idle tuning.

The native client is rebuilt with `android-src/build.sh` using the project's
Android NDK and the same stock Binder libraries as the existing compatibility
services. `--check` verifies service access and reads activity state without
enabling automatic suspend.

Reference interface: AOSP `platform/system/hardware/interfaces`,
`suspend/aidl/android/system/suspend/internal/ISuspendControlServiceInternal.aidl`
and `suspend/aidl/android/system/suspend/ISystemSuspend.aidl`.

Device validation on 2026-11-06: during 113 seconds without USB, the kernel
completed 13 deep suspends. All 21 targeted Mac Wi-Fi scans still found NWRT;
the AP kept its SSID, channel and transmit-power state after resume. Cellular
connectivity also passed a real interface-bound packet check after reconnect.
Two suspend attempts were interrupted at the freezer stage by pending wakeups;
no additional device-suspend or device-resume failures were recorded. Stock
GPU/UBWCP SMMU power-count warnings remain separate from this cfg80211 fix.
This short test does not establish overnight battery life or validate client
data forwarding while the application processor is asleep.

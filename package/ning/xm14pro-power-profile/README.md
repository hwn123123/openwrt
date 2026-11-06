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

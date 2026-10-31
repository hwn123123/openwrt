# Xiaomi 14 Pro Radio HAL bridge

This directory contains the device-local Android/Bionic client for the stock
Xiaomi 14 Pro Radio AIDL services. The frozen AIDL snapshots are the official
Android `android.hardware.radio` V2 and `android.hardware.radio.messaging` V2
interfaces. They remain immutable.

`radio-probe.cpp` is deliberately read-only. It checks the stock platform
`IRadioMessaging`, `IRadioVoice`, `IRadioData`, `IRadioSim`, `IRadioNetwork`,
`IRadioModem` and `IRadioConfig` instances through the framework Binder domain
(`/dev/binder`). It neither registers response callbacks nor sends a message
or places a call. Qualcomm vendor services use `/dev/vndbinder` and are outside
this probe.

Run `./build.sh` on macOS. The default tool paths are kept under this Xiaomi
project's `04-CPE/构建工具` directory. The resulting Bionic executable is staged
at `files/android-root/vendor/bin/xm14pro-radio-probe` and is packaged without
ELF stripping by `xm14pro-telephony-sidecar`.

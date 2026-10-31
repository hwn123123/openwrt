# Xiaomi 14 Pro stock hardware modules

This local, device-specific package contains the minimal module dependency
closure taken from this phone's OS3.0.308.0 `vendor_dlkm`. It supplies the
stock ADSP loader and its protection-domain/audio-core dependencies, the
Synaptics S3910 touch stack (`xiaomi_touch`/`synaptics_tcm2`), proprietary
Wi-Fi 7 (`cnss2`/`qca_cld3_kiwi_v2`) and Qualcomm IPA/rmnet data-path modules
that are not present in the stock `vendor_boot` ramdisk.

Its loader only inserts exact-version kernel modules; it does not write to any
remoteproc `state` file and does not modify modem NV or Android partitions.
The ADSP module command only registers Xiaomi's `adsp-loader`; the separate
platform service later writes the same `/sys/kernel/boot_adsp/boot` trigger
used by recovery after firmware mounts are ready.
The touch path uses the original DTBO and creates a standard evdev input node;
it does not emulate touches through uinput. The package also carries the exact
OS3.0.308.0 Synaptics firmware, panel configuration and limits extracted from
the stock ODM image, preventing the kernel firmware fallback timeout at boot.
Android changes `enable_touch_raw` to `1` only when its proprietary THP HAL is
running. nwrt has no such HAL and consumes the standard input node directly,
so the loader explicitly leaves this control at `0`; otherwise the panel IRQ
keeps firing while `/dev/input/event*` produces no coordinates.
Because nwrt's DRM panel is already enabled when the late vendor touch module
registers, it also performs one initial `resume` through Synaptics' own sysfs
control after the asynchronous firmware check. This mirrors Android's first
panel-unblank callback without blanking or redrawing the physical screen.
Wi-Fi can be debugged incrementally with `wifi-base`,
`wifi-bus`, then `wifi-driver`. The Xiaomi 14 Pro device tree matches
`qcom,cnss-kiwi`, so the main path deliberately excludes `icnss2`.
`rfkill` is supplied with the matching Xiaomi module set and loaded before
`cfg80211`.
The stock Kiwi v2 `WCNSS_qcom_cfg.ini` is installed under
`/usr/lib/firmware/wlan/qca_cld/kiwi_v2`; the loader adds that directory to
the kernel firmware search path before inserting Wi-Fi modules.

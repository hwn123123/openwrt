# Xiaomi 14 Pro USB compatibility sidecar

This package runs the unmodified HyperOS `android.hardware.usb-service.qti`
binary and its matching AIDL library in the device-local Android/Bionic
compatibility root.

Ownership is intentionally split:

- the stock `IUsb` service observes and coordinates Type-C, moisture and USB
  power-role state;
- `xm14pro-usb-framework` supplies the framework-side `IUsbCallback` that
  HyperOS normally registers from `UsbPortManager`, allowing the unmodified
  stock service to start its uevent and bus-reset worker. After callback
  registration and a successful port-status query, it writes a readiness
  marker containing the current kernel boot ID;
- `xm14pro-usb-gadget` remains the sole ConfigFS/UDC owner and reproduces the
  stock `init.qcom.usb.rc` `ncm.gs6` composition (`05c6:a4a1`). When this
  sidecar is enabled, the gadget verifies that exact boot-ID marker before
  binding the UDC;
- OpenWrt remains the sole owner of IP addressing, DHCP, firewall, tethering
  and proxy policy;
- `android.hardware.usb.gadget-service.qti` is intentionally not shipped or
  started: the stock binary does not implement NCM. HyperOS creates NCM
  directly in `init.qcom.usb.rc`, so starting the gadget HAL would neither
  reproduce nor repair the original NCM path.

This is a userspace compatibility layer for the original Android hardware
components. It is unrelated to the device A/B slots and does not modify an
Android system slot.

The package never creates fake battery, power-supply or Type-C nodes. Those
must be provided by the stock kernel charger/ADSP path before the HAL can
report them.

Do not add an automatic UDC rebind or role-cycle watchdog. The previous
recovery daemon masked the missing framework callback and has been removed.

Stock OS3.0.308.0 payload hashes:

- `android.hardware.usb-service.qti`:
  `cb114f33ef3be91cc1efb1ba3c14c4d1115fdcad63544b3abe22bd2d0a2e8385`
- `android.hardware.usb-V1-ndk.so`:
  `d551592018484b800878a4ad0db22e34a9f3fa171eaef9f5ef4f16e433aed2e7`

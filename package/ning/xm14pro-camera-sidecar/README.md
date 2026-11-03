# Xiaomi 14 Pro camera sidecar

This package starts the stock Android camera HAL inside an isolated root on
NWRT slot B. The stock `system`, `vendor`, `odm`, and `system_ext` images remain
on the device at `/mnt/nwrt-data/pixelos-original` and are mounted read-only.
They are never copied into this package or Git. The A-slot partitions and
Android userdata are not mounted or changed.

The LuCI page uses `/usr/libexec/xm14pro-camera/control` to take a single
640×480 JPEG frame. Camera IDs 1, 2, 3, and 4 correspond to the front, main,
ultrawide, and telephoto cameras. Photos saved from LuCI live in
`/mnt/nwrt-data/camera/photos`; previews live in `/tmp/xm14pro-camera`.
The UI does not claim continuous preview or video recording; those functions
are still unimplemented.

## Runtime

`stack start` creates private Binder and HWBinder devices and starts the stock
service managers, sensors, display allocator, media software codec, camera
provider, and cameraserver. The provider uses the same private ordinary Binder
context as cameraserver for its AIDL registration. The software codec APEX is
extracted from the device-local stock CAPEX and mounted read-only. The display
allocator runs as root in this private root because this kernel exposes
`/dev/dma_heap/qcom,system` as mode 0600 root; UID 1000 was verified to fail
buffer allocation with `Permission denied`. No host DMA-BUF permissions are
changed.

Four small compatibility services and two preloads provide the Android calls
observed from the private cameraserver. Their sources are in `src/`; the Binder
services reject unrecognized transaction codes. They are only launched on the
isolated Binder network. The stock telephony and audio property bridges come
from the installed NWRT telephony sidecar. `stack stop` stops only processes
started by this camera sidecar.

The stack starts on demand when the user selects “启动并试拍” or requests a frame.
The camera provider can take around a minute to register and enumerate after
startup. LuCI starts the stack in the background and polls `service_ready`
before sending the photo request, so its RPC call does not wait for provider
enumeration. `control status` marks `capture_ready=true` only after a successful
JPEG capture from the current provider and cameraserver process IDs.

## Build and verification

`src/build-compat.sh` rebuilds the Bionic compatibility binaries using
`ANDROID_NDK` and `ANDROID_SYSTEM_LIB64` (a matching stock system lib64
containing `libbinder_ndk.so`). `src/build-snapshot.sh` rebuilds the snapshot
client. The OpenWrt package build compiles the small musl chroot launcher from
`src/camera-launch.c` and packages all scripts and binaries.

On 2026-11-04, the packaged sidecar was installed on a running B-slot NWRT.
After stopping all manually launched private camera processes and unmounting
the private root, `stack start` enumerated 11 camera IDs. A fresh call through
`control snapshot 2 preview` returned success and wrote a real JPEG. Before
that teardown, IDs 1–4 each produced real 640×480 JPEG images.

After a full NWRT reboot, `control load` restored the camera and CVP drivers;
LuCI exposes this as “加载摄像头驱动”. The asynchronous `control start` path became
ready in 48 seconds, and `control snapshot 2 preview` returned a 640×480 JPEG
with visible scene content in 11 seconds. Cellular remained up. The camera
services start on demand; the driver does not automatically load at boot.

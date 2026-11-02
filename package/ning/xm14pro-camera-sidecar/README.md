# Xiaomi 14 Pro camera client

`src/camera-snapshot.c` captures a YUV frame with Android's NDK camera API
and converts it to JPEG in software. Camera IDs 1, 2, 3 and 4 were verified
on a live Xiaomi 14 Pro: front, main, ultrawide and telephoto respectively.
Logical rear camera ID 0 does not currently return frames in the isolated
runtime, so the UI does not offer it.

The package installs only the snapshot client. The isolated camera provider,
camera server, Android Binder managers and compatibility services are still
device-local runtime dependencies. `/usr/libexec/xm14pro-camera/control`
reports `capture_ready=false` unless that runtime is present. The Android
images under `/mnt/nwrt-data/pixelos-original` stay on the device and are
mounted read-only; they are not copied into this package or Git.

## Cold-start work still required

The current live sidecar was assembled interactively; reboot recovery is not
yet implemented by this package. Before shipping an automatic service, its
startup must: (1) verify the B slot and checksums of the device-local stock
images; (2) mount `system`, `vendor`, `odm`, and `system_ext` read-only beneath
an isolated overlay root; (3) refresh camera `/dev/video*`, `/dev/media*`,
`/dev/v4l-subdev*`, ashmem, and private Binder nodes; (4) start the private
Binder managers, sensor and display allocator helpers, provider, and camera
server in that order; and (5) wait for a successful camera enumeration probe.
It must stop those processes when camera use ends so ordinary router operation
does not keep the camera stack awake.

The live prototype also needs four Binder compatibility services:
`media.camera.proxy`, `sensor_privacy`, `processinfo`, and
`SurfaceFlingerAIDL`, plus narrow ActivityManager and permission bridges.
Their current device-local test stubs accept broad transaction sets and are
**not** included in this package. Each service needs a documented, minimal
transaction implementation and error handling before automatic startup can
be considered stable. `capture_ready=true` now requires a successful snapshot
from the current camera service processes; it does not imply reboot persistence.

After a cold reboot, the stock private `hwservicemanager` aborts because the
isolated root has no Android property service. A temporary shim that accepts
only the `hwservicemanager.disabled=false` write, together with the existing
property-wait backoff, let that manager remain idle without a CPU spin.
Starting the provider and camera server then still returned zero camera IDs;
the remaining helper services and Binder bridges need proper implementations.
The temporary shim and permissive test stubs are not part of this package.

The private Binder service manager must also run as Android UID 1000 after
`chroot`: binderfs retains the first context manager UID, and launching a
replacement as root returns `EPERM`. The camera server needs its stock
`u:r:cameraserver:s0` domain; without it, `media.camera` is not registered.
With those identities, the provider, sensor and allocator helpers, and the
temporary Binder bridges registered in the private manager, but camera
enumeration still returned zero devices and a snapshot timed out. These are
startup findings, not a working cold-boot path. All probe processes were
stopped after the check.

To rebuild the client, set `ANDROID_NDK` and `ANDROID_SYSTEM_LIB64` to the
Android NDK and a matching extracted stock system library directory, then run
`src/build-snapshot.sh`. The checked-in arm64 binary's SHA-256 is
`4e110478f6c3aa4c9a61040f277fa3bc854e6dd313c41590fd4a929fd056ab37`.

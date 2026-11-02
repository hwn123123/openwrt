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

To rebuild the client, set `ANDROID_NDK` and `ANDROID_SYSTEM_LIB64` to the
Android NDK and a matching extracted stock system library directory, then run
`src/build-snapshot.sh`. The checked-in arm64 binary's SHA-256 is
`4e110478f6c3aa4c9a61040f277fa3bc854e6dd313c41590fd4a929fd056ab37`.

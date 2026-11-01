#!/system/bin/sh
set -eu

# Clear only struct bootloader_message (the first 2048 bytes of misc).  The
# A/B metadata begins after it and must remain untouched.
misc=/dev/block/by-name/misc
expected_size=4194304
expected_zero_sha256=e5a00aa9991ac8a5ee3109844d84a55583bd20572ad3ffcd42792f3c36b183ad

if [ ! -b "$misc" ]; then
    echo "BCB clear: misc block device is missing" >&2
    exit 80
fi

actual_size=$(/system/bin/blockdev --getsize64 "$misc" 2>/dev/null || true)
if [ "$actual_size" != "$expected_size" ]; then
    echo "BCB clear: unexpected misc size: $actual_size" >&2
    exit 81
fi

/system/bin/dd if=/dev/zero of="$misc" bs=2048 count=1 2>/dev/null
/system/bin/sync

actual_zero_sha256=$(
    /system/bin/dd if="$misc" bs=2048 count=1 2>/dev/null |
        /system/bin/sha256sum |
        /system/bin/cut -d ' ' -f 1
)
if [ "$actual_zero_sha256" != "$expected_zero_sha256" ]; then
    echo "BCB clear: read-back verification failed" >&2
    exit 82
fi

echo "BCB clear: recovery request removed"

#!/system/bin/sh
set -u

[ "$#" -gt 0 ] || {
    echo "usage: nwrt-root 'COMMAND'" >&2
    exit 2
}
command="$*"
[ "${#command}" -le 80 ] || {
    echo "root command is too long (maximum 80 bytes)" >&2
    exit 2
}

sequence="$(cat /proc/sys/kernel/random/uuid 2>/dev/null)" || sequence="$(date +%s)-$$"
setprop debug.nwrt.command "$command" || exit 125
setprop debug.nwrt.sequence "$sequence" || exit 125

attempt=0
while [ "$(cat /nwrt-root.done 2>/dev/null)" != "$sequence" ]; do
    attempt=$((attempt + 1))
    if [ "$attempt" -ge 120 ]; then
        echo "root command timed out" >&2
        exit 124
    fi
    sleep 1
done

cat /nwrt-root.out
rc=$(cat /nwrt-root.rc 2>/dev/null || echo 125)
exit "$rc"

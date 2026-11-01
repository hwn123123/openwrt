#!/system/bin/sh
set -u

log=/tmp/nwrt-rootd.log
echo "$(date): recovery root bridge started" >>"$log"
last_sequence=

while true; do
    sequence=$(getprop debug.nwrt.sequence)
    if [ -n "$sequence" ] && [ "$sequence" != "$last_sequence" ]; then
        last_sequence=$sequence
        command=$(getprop debug.nwrt.command)
        echo "$(date): execute request $sequence" >>"$log"
        if [ -z "$command" ]; then
            echo "empty root command" >/nwrt-root.out.tmp
            rc=126
        else
            /system/bin/sh -c "$command" >/nwrt-root.out.tmp 2>&1
            rc=$?
        fi
        chmod 0644 /nwrt-root.out.tmp
        mv -f /nwrt-root.out.tmp /nwrt-root.out
        echo "$rc" >/nwrt-root.rc.tmp
        chmod 0644 /nwrt-root.rc.tmp
        mv -f /nwrt-root.rc.tmp /nwrt-root.rc
        echo "$sequence" >/nwrt-root.done.tmp
        chmod 0644 /nwrt-root.done.tmp
        mv -f /nwrt-root.done.tmp /nwrt-root.done
    fi
    sleep 1
done

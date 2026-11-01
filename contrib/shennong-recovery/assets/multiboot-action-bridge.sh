#!/system/bin/sh
set -u

ui_log=/multiboot-ui.log
bridge_log=/multiboot-power.log
state_dir=/tmp/multiboot-action-bridge
mkdir -p "$state_dir"

request_power_action() {
    action=$1
    marker=$2
    [ ! -e "$state_dir/$marker" ] || return 0
    case "$marker" in
        poweroff|reboot-bootloader)
            if ! /nwrt/bin/xm14-clear-recovery-request >>"$bridge_log" 2>&1; then
                echo "$(date): refusing $action because BCB clear failed" >>"$bridge_log"
                return 1
            fi
            ;;
    esac
    touch "$state_dir/$marker"
    echo "$(date): request $action from $marker" >>"$bridge_log"
    sync
    if ! setprop sys.powerctl "$action"; then
        echo "$(date): setprop failed for $action" >>"$bridge_log"
        rm -f "$state_dir/$marker"
        return 1
    fi
}

request_system_reboot() {
    marker=reboot-system
    [ ! -e "$state_dir/$marker" ] || return 0
    echo "$(date): clear BCB and request reboot from confirmed system action" >>"$bridge_log"
    if ! /nwrt/bin/xm14-clear-recovery-request >>"$bridge_log" 2>&1; then
        echo "$(date): refusing system reboot because BCB clear failed" >>"$bridge_log"
        return 1
    fi
    touch "$state_dir/$marker"
    sync
    if ! setprop sys.powerctl reboot; then
        echo "$(date): setprop failed for reboot" >>"$bridge_log"
        rm -f "$state_dir/$marker"
        return 1
    fi
}

while true; do
    if [ -r "$ui_log" ]; then
        # The selector logs "selected ..." whenever the highlight moves.  Those
        # lines are navigation state, not confirmation, and must never trigger
        # an action.  The four messages below are emitted only from the power
        # key confirmation path immediately before the selector's reboot call.
        grep -Fq "power off device" "$ui_log" && request_power_action shutdown,userrequested poweroff
        grep -Fq "reboot to current default system" "$ui_log" && request_system_reboot
        grep -Fq "reboot to recovery" "$ui_log" && request_power_action reboot,recovery reboot-recovery
        grep -Fq "reboot to bootloader" "$ui_log" && request_power_action reboot,bootloader reboot-bootloader
    fi
    sleep 1
done

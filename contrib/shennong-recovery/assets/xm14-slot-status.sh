#!/system/bin/sh

# The stock boot-control library reports the boot LUN here, which can disagree
# with the slot that actually loaded this recovery image. Report the boot slot.
case "$(getprop ro.boot.slot_suffix)" in
    _a) exit 0 ;;
    _b) exit 1 ;;
    *) exit 2 ;;
esac

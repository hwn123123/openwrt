#!/bin/sh

real=/usr/sbin/runc.real
bundle=
command=
previous=

for argument in "$@"; do
	case "$previous" in
		--bundle|-b) bundle=$argument ;;
	esac
	case "$argument" in
		--bundle=*) bundle=${argument#--bundle=} ;;
		create|run) command=$argument ;;
	esac
	previous=$argument
done

case "$command" in
	create|run)
		[ -n "$bundle" ] || bundle=$PWD
		if [ -f "$bundle/config.json" ]; then
			/usr/bin/ucode /usr/libexec/xm14pro-runc-compat.uc \
				"$bundle/config.json" || exit $?
		fi
		;;
esac

exec "$real" "$@"

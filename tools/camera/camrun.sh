#!/bin/sh
# Start the stock camera HAL stack inside the camroot (see camroot.sh).
# Logs: /var/log/lagos-cam/{logcat.txt,<service>.out}. `camrun.sh stop` kills it.
set -u
M=/var/lib/lagos/camroot/merged
LOG=/var/log/lagos-cam
HERE=$(dirname "$(readlink -f "$0")")
CHROOT=$(command -v chroot)

if [ "${1:-}" = stop ]; then
	for pid in $(cat $LOG/*.pid 2>/dev/null); do kill "$pid" 2>/dev/null; done
	rm -f $LOG/*.pid
	exit 0
fi

mkdir -p $LOG

start() {
	name=$1
	shift
	case $1 in /vendor/*) shim=/vendor/lib64/libpropshim.so ;; *) shim=/system/lib64/libpropshim.so ;; esac
	env -i PATH=/system/bin:/vendor/bin LD_PRELOAD=$shim \
		ANDROID_ROOT=/system ANDROID_DATA=/data \
		ANDROID_I18N_ROOT=/apex/com.android.i18n ANDROID_TZDATA_ROOT=/apex/com.android.tzdata \
		${WRAP:-} $CHROOT $M "$@" > $LOG/$name.out 2>&1 &
	echo $! > $LOG/$name.pid
	sleep "${DELAY:-1}"
	kill -0 "$(cat $LOG/$name.pid)" 2>/dev/null && echo "$name up" || { echo "$name DIED:"; tail -5 $LOG/$name.out; }
}

python3 $HERE/fake-logd.py $M $LOG/logcat.txt &
echo $! > $LOG/logd.pid
sleep 1
start servicemanager /system/bin/servicemanager
start hwservicemanager /system/bin/hwservicemanager
start vndservicemanager /vendor/bin/vndservicemanager /dev/vndbinder
start allocator /vendor/bin/hw/android.hardware.graphics.allocator-V2-service-mediatek
DELAY=5 WRAP="${HALWRAP:-}" start camerahalserver /vendor/bin/hw/camerahalserver

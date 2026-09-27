#!/bin/sh
# The stock GKI kernel has no CONFIG_PID_NS, so killing the LXC init does not
# take Android's userspace with it. Kill every process living in a mount
# namespace that holds an Android init/ueventd/zygote (any Waydroid instance).
host=$(readlink /proc/1/ns/mnt)
android_ns=""
for d in /proc/[0-9]*; do
	n=$(readlink "$d/ns/mnt" 2>/dev/null) || continue
	[ "$n" = "$host" ] && continue
	case "$(head -c 40 "$d/cmdline" 2>/dev/null | tr '\0' ' ')" in
		/system/bin/init*|/system/bin/ueventd*|zygote*) android_ns="$android_ns $n" ;;
	esac
done
[ -z "$android_ns" ] && exit 0
for sig in TERM KILL; do
	for d in /proc/[0-9]*; do
		n=$(readlink "$d/ns/mnt" 2>/dev/null) || continue
		case " $android_ns " in *" $n "*) kill -$sig "${d#/proc/}" 2>/dev/null ;; esac
	done
	[ $sig = TERM ] && sleep 2
done
exit 0

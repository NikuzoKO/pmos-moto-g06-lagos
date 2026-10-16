#!/bin/sh
# lagos: shadows /usr/sbin/sxmo_brightness.sh (~/.config/sxmo/hooks is first in
# PATH). brightnessctl without -d picks the first LED, the vibrator; the panel
# is lcd-backlight (LED class, no /sys/class/backlight on this kernel).
# SPDX-License-Identifier: AGPL-3.0-only
# Copyright 2022 Sxmo Contributors

# include common definitions
# shellcheck source=scripts/core/sxmo_common.sh
. sxmo_common.sh
# shellcheck source=configs/default_hooks/sxmo_hook_icons.sh
. sxmo_hook_icons.sh

notify() {
	if [ -z "$SXMO_WOB_DISABLE" ]; then
		# the second word tells lagos-volume-osd this is brightness, not volume
		echo "$(getvalue) brightness" > "$XDG_RUNTIME_DIR"/sxmo.obsock
	else
		getvalue | xargs notify-send -r 888 "$icon_brightness Brightness"
	fi
}

setvalue() {
	brightnessctl -d lcd-backlight -q set "$1"%
}

up() {
	if [ -n "$1" ]; then
		brightnessctl -d lcd-backlight -q set "$1"%+
	else
		brightnessctl -d lcd-backlight -q set 5%+
	fi
}

down() {
	# bugged https://github.com/Hummer12007/brightnessctl/issues/82
	# brightnessctl -d lcd-backlight --min-value "${SXMO_MIN_BRIGHTNESS:-5}" set 5%-

	value="$(getvalue)"

	if [ "$value" -le "${SXMO_MIN_BRIGHTNESS:-5}" ]; then
		return
	fi


	if [ -n "$1" ]; then
		if [ "$((value-$1))" -ge "${SXMO_MIN_BRIGHTNESS:-5}" ]; then
			brightnessctl -d lcd-backlight -q set "$1"%-
			return
		fi
	elif [ "$((value-5))" -ge "${SXMO_MIN_BRIGHTNESS:-5}" ]; then
		brightnessctl -d lcd-backlight -q set 5%-
		return
	fi

	brightnessctl -d lcd-backlight -q set "${SXMO_MIN_BRIGHTNESS:-5}"%
}

getvalue() {
	# need brightnessctl -d lcd-backlight release after 0.5.1 to have --percentage
	brightnessctl -d lcd-backlight info | awk -F'[(%]' 'NR==2{print $2}'
}

smooth_brightness_adjustment() {
	# $1 is target
	current_brightness=$(getvalue)
	if [ "$current_brightness" -eq "$1" ]; then
		return
	fi

	count=0

	# Set variables depending on the difference between current and wanted brightness
	if [ "$current_brightness" -gt "$1" ]; then
		difference=$((current_brightness - $1))
		direction="down"
	elif [ "$current_brightness" -lt "$1" ]; then
		difference=$(($1 - current_brightness))
		direction="up"
	fi

	while [ "$count" -lt "$difference" ]; do
		# Call up or down with value of 1
		"$direction" 1
		count=$((count + 1))
		sleep 0.4
	done
}

case "$1" in
	silent)
		SILENT=1
		shift
		;;
	-h|--help)
		#editorconfig-checker-disable
		cat <<- EOF
		Usage: sxmo_brightness.sh [options] [value]

		Options:
		  silent		Prevents notification with (w/x)ob. Use this as first arg to below options.
		  notify		Notify with (w/x)ob current brightness.
		  setvalue VALUE	Set current brightness to VALUE.
		  up VALUE		Up the brightness by 5%% or if VALUE supplied, by VALUE.
		  down VALUE		Lower the brightness by 5%% or if VALUE supplied, by VALUE. Caps out at SXMO_MIN_BRIGHTNESS.
		  getvalue		Get current value.
		  smooth_brightness_adjustment VALUE	Adjust the brightness by steps of 1 to VALUE
		EOF
		#editorconfig-checker-enable
		exit 0
		;;
esac

"$@"
if [ -z "$SILENT" ]; then
	notify
fi

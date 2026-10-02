#!/bin/sh
# configversion: 5697e0579ea20a4a6b8dc05f5814d0c6
# SPDX-License-Identifier: AGPL-3.0-only
# Copyright 2022 Sxmo Contributors

# include common definitions
# shellcheck source=scripts/core/sxmo_common.sh
. sxmo_common.sh

# This hook is called when the system becomes unlocked again

sxmo_led.sh blink red green &

# lagos: reconnect Bluetooth audio (~/.config/lagos/bt-autoconnect) when the phone is woken
setsid -f "$HOME"/.local/bin/lagos-bt-autoconnect > /dev/null 2>&1

sxmo_wm.sh display on
sxmo_wm.sh inputevent touchscreen on

sxmo_jobs.sh stop periodic_blink
sxmo_jobs.sh stop periodic_wakelock_check

# Go to the next idle state after 120 seconds of inactivity
if [ -e "$XDG_CACHE_HOME/sxmo/sxmo.noidle" ]; then
	sxmo_jobs.sh stop idle_locker
else
	case "$SXMO_WM" in
		river)
			sxmo_jobs.sh start idle_locker sxmo_idle.sh -w \
				timeout "${SXMO_UNLOCK_IDLE_TIME:-120}" 'sh -c "
					exec sxmo_state.sh idle
				"'
			;;
		sway)
			sxmo_jobs.sh start idle_locker sxmo_idle.sh -w \
				timeout "${SXMO_UNLOCK_IDLE_TIME:-120}" 'sh -c "
					swaymsg mode default;
					exec sxmo_state.sh idle
				"'
			;;
		i3)
			# Idle fails to trigger if formatted like sway
			sxmo_jobs.sh start idle_locker sxmo_idle.sh -w \
				timeout "${SXMO_UNLOCK_IDLE_TIME:-120}" \
					'sh -c "i3-msg mode default; exec sxmo_state.sh idle"'
			;;
		dwm)
			sxmo_jobs.sh start idle_locker sxmo_idle.sh -w \
				timeout "${SXMO_UNLOCK_IDLE_TIME:-120}" "sxmo_state.sh idle"
			;;
	esac
fi

wait

#!/bin/sh
# lagos: shadows /usr/bin/sxmo_keyboard.sh (~/.config/sxmo/hooks is first in
# sxmo's PATH). wvkbd stays running hidden with --auto: it binds sway's
# input-method-v2 and shows itself when a text field takes focus
# (text-input-v3: GTK, Qt, ATL, foot) and hides when it loses it. So:
#   open    show the running keyboard (start it if needed); with KEYBOARD_ARGS
#           naming a layout (sxmo_dtmf's "-l dialer") run that one visibly
#   close   hide it (it shows again on the next text focus; wvkbd re-reads the
#           output size on every show, so rotation needs no restart); a custom
#           layout is stopped and the hidden --auto one comes back
#   toggle  show/hide the running one

. sxmo_common.sh

real=/usr/bin/sxmo_keyboard.sh
custom="$XDG_RUNTIME_DIR/lagos-keyboard.custom"

running() {
	sxmo_jobs.sh running sxmo_keyboard -q
}

start_auto() {
	rm -f "$custom"
	KEYBOARD_ARGS="$LAGOS_KEYBOARD_ARGS --hidden --auto" "$real" open
}

signal() {
	pkill "-$1" -x "${KEYBOARD%% *}"
}

# the profile's KEYBOARD_ARGS are the base; a caller may override them
LAGOS_KEYBOARD_ARGS="${LAGOS_KEYBOARD_ARGS:-$KEYBOARD_ARGS}"

case "$1" in
	close)
		if [ -e "$custom" ] || ! running; then
			"$real" close
			[ -n "$SXMO_NO_VIRTUAL_KEYBOARD" ] || start_auto
		else
			signal USR1
		fi
		;;
	isopen)
		running
		;;
	toggle)
		if running; then signal RTMIN; else start_auto; signal USR2; fi
		;;
	*)
		[ -n "$SXMO_NO_VIRTUAL_KEYBOARD" ] && exit 0
		case " $KEYBOARD_ARGS " in
			*" -l "*)
				"$real" close
				touch "$custom"
				"$real" open
				;;
			*)
				if ! running || [ -e "$custom" ]; then
					"$real" close
					start_auto
					sleep 0.3
				fi
				signal USR2
				;;
		esac
		;;
esac

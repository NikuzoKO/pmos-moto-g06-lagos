#!/bin/sh
# configversion: 113bf453dd3310afff192d841089e5ac
# SPDX-License-Identifier: AGPL-3.0-only
# Copyright 2022 Sxmo Contributors

# This script handles input actions, it is called by lisgd for gestures
# and by dwm for button presses. Gestures are tied to actions in
# sxmo_hook_lisgdstart.sh .

ACTION="$1"

# include common definitions
# shellcheck source=scripts/core/sxmo_common.sh
. sxmo_common.sh

stop_proximity_lock() {
	sxmo_jobs.sh stop proximity_lock
}

# bonsaid reports a held volume key once, 400ms in, and never its release:
# keep stepping while the key is still down (evtest --query exits 10)
volume_hold() {
	direction="$1"
	if [ "$direction" = up ]; then
		device=/dev/input/by-path/platform-mtk-pmic-keys-event
		key=KEY_VOLUMEUP
	else
		device=/dev/input/by-path/platform-10010000.kp-event
		key=KEY_VOLUMEDOWN
	fi
	steps=0
	while [ "$steps" -lt 40 ]; do
		sxmo_audio.sh vol "$direction"
		evtest --query "$device" EV_KEY "$key"
		[ $? -eq 10 ] || break
		steps=$((steps + 1))
		sleep 0.1
	done
}

exec 0<<EOF
$(sxmo_wm.sh focusedwindow -r)
EOF
read -r WMCLASS
read -r WMNAME

sxmo_debug "STATE: $(sxmo_state.sh get) ACTION: $ACTION WMCLASS: $WMCLASS WMNAME: $WMNAME"

if sxmo_state.sh is_locked; then
	case "$WMNAME" in # Handle programs
		*"epy"*|*"epr"*)
			case "$ACTION" in
				"voldown_one")
					sxmo_type.sh l
					exit 0
					;;
				"volup_one")
					sxmo_type.sh h
					exit 0
					;;
			esac
			;;
	esac
	case "$ACTION" in
		"powerbutton_one")
			sxmo_state.sh click
			;;
		"powerbutton_two")
			sxmo_state.sh click 2
			;;
		"powerbutton_three")
			sxmo_state.sh click 2
			;;
		"voldown_one")
			sxmo_audio.sh vol down 5
			exit
			;;
		"voldown_two")
			sxmo_audio.sh vol down 10
			exit
			;;
		"voldown_three")
			volume_hold down
			exit
			;;
		"volup_one")
			sxmo_audio.sh vol up 5
			exit
			;;
		"volup_two")
			sxmo_audio.sh vol up 10
			exit
			;;
		"volup_three")
			volume_hold up
			exit
			;;
	esac
	#we're locked, don't process the rest of the script
	exit 0
fi

#special context-sensitive handling
case "$WMCLASS" in
	*"mpv"*)
		case "$ACTION" in
			"oneright")
				sxmo_type.sh -k Left
				exit 0
				;;
			"oneleft")
				sxmo_type.sh -k Right
				exit 0
				;;
			"oneup")
				sxmo_type.sh m
				exit 0
				;;
			"onedown")
				sxmo_type.sh p
				exit 0
				;;
		esac
		;;
	*"acme"*)
		case "$ACTION" in
			"volup_one")
				xdotool click 3
				exit 0
				;;
			"voldown_one")
				xdotool click 2
				exit 0
				;;
		esac
		;;
	*"links"*)
		case "$ACTION" in
			# you're probably dragging the scrollbar
			*"uprightedge") exit 0 ;;
			*"downrightedge") exit 0 ;;
			*"onedown")
				sxmo_type.sh -M Ctrl -M Shift -k b
				exit 0
				;;
			*"oneup")
				sxmo_type.sh -M Ctrl -M Shift -k f
				exit 0
				;;
		esac
		;;
	*"netsurf"*)
		case "$ACTION" in
			# you're probably dragging the scrollbar
			*"uprightedge") exit 0 ;;
			*"downrightedge") exit 0 ;;
			*"onedown")
				sxmo_type.sh -k Page_Up
				exit 0
				;;
			*"oneup")
				sxmo_type.sh -k Page_Down
				exit 0
				;;
		esac
		;;
	*"firefox"*|*"librewolf"*)
		case "$ACTION" in
			# you're probably dragging the scrollbar
			*"uprightedge") exit 0 ;;
			*"downrightedge") exit 0 ;;
		esac
		;;
	*"foot"*|*"st"*|*"vte"*|"terminal") # Terminals
		case "$WMCLASS" in # Handle programs without touch support
			*"st"*)
				case "$WMNAME" in
					*"weechat"*|*'gomuks'*)
						case "$ACTION" in
							*"onedown")
								sxmo_type.sh -k Page_Up
								exit 0
								;;
							*"oneup")
								sxmo_type.sh -k Page_Down
								exit 0
								;;
						esac
						;;
					*"less"*|*"amfora"*)
						case "$ACTION" in
							*"onedown")
								sxmo_type.sh u
								exit 0
								;;
							*"oneup")
								sxmo_type.sh d
								exit 0
								;;
						esac
						;;
					*'irssi'*)
						case "$ACTION" in
							"onedown")
								sxmo_type.sh -M Alt p
								exit 0
								;;
							"oneup")
								sxmo_type.sh -M Alt n
								exit 0
								;;
						esac
						;;
					*'epy'*|*'epr'*)
						case "$ACTION" in
							"onedown")
								sxmo_type.sh h
								exit 0
								;;
							"oneup")
								sxmo_type.sh l
								exit 0
								;;
						esac
						;;
					*'nnn'*|'lf')
						case "$ACTION" in
							"onedown")
								sxmo_type.sh -k Down
								exit 0
								;;
							"oneup")
								sxmo_type.sh -k Up
								exit 0
								;;
						esac
						;;
				esac
				;;
		esac

		case "$WMNAME" in # Handle programs
			*"weechat"*)
				case "$ACTION" in
					*"oneleft")
						sxmo_type.sh -M Alt -k a
						exit 0
						;;
					*"oneright")
						sxmo_type.sh -M Alt -k less
						exit 0
						;;
				esac
				;;
			*" sms")
				case "$ACTION" in
					*"upbottomedge")
						number="$(printf %s "$WMNAME" | sed -e 's|^\"||' -e 's|\"$||' | cut -f1 -d' ')"
						sxmo_terminal.sh sxmo_modemtext.sh conversationloop "$number"
						exit 0
						;;
				esac
				;;
			*"tuir"*)
				if [ "$ACTION" = "rightbottomedge" ]; then
					sxmo_type.sh o
					exit 0
				elif [ "$ACTION" = "leftbottomedge" ]; then
					sxmo_type.sh s
					exit 0
				fi
				;;
			*"less"*)
				case "$ACTION" in
					"leftbottomedge")
						sxmo_type.sh q
						exit 0
						;;
					"leftrightedge_short")
						sxmo_type.sh q
						exit 0
						;;
					*"oneleft")
						sxmo_type.sh ":n" -k Return
						exit 0
						;;
					*"oneright")
						sxmo_type.sh ":p" -k Return
						exit 0
						;;
				esac
				;;
			*"amfora"*)
				case "$ACTION" in
					"downright")
						sxmo_type.sh -k Tab
						exit 0
						;;
					"upleft")
						sxmo_type.sh -M Shift -k Tab
						exit 0
						;;
					*"oneright")
						sxmo_type.sh -k Return
						exit 0
						;;
					"upright")
						sxmo_type.sh -M Ctrl t
						exit 0
						;;
					*"oneleft")
						sxmo_type.sh b
						exit 0
						;;
					"downleft")
						sxmo_type.sh -M Ctrl w
						exit 0
						;;
				esac
				;;
			*"epy"*|*"epr"*)
				case "$ACTION" in
					*"left"|"voldown_one")
						sxmo_type.sh l
						exit 0
						;;
					*"right"|"volup_one")
						sxmo_type.sh h
						exit 0
						;;
					"voldown_three"|"twodownbottomedge")
						sxmo_type.sh q
						exit
						;;
				esac
				;;
			*'nnn'*|'lf')
				case "$ACTION" in
					*"left")
						sxmo_type.sh -k Right
						exit 0
						;;
					*"right")
						sxmo_type.sh -k Left
						exit 0
						;;
				esac
				;;
		esac

		case "$WMCLASS" in # Handle general scrolling without touch support
			*"st"*)
				case "$ACTION" in
					*"onedown")
						sxmo_type.sh -M Ctrl -M Shift -k b
						exit 0
						;;
					*"oneup")
						sxmo_type.sh -M Ctrl -M Shift -k f
						exit 0
						;;
				esac
				;;
			"org.gnome.vte.application"|"terminal")
				case "$ACTION" in
					# For VTE, fallback to doing nothing,
					# you're probably dragging the scrollbar
					*"uprightedge") exit 0 ;;
					*"downrightedge") exit 0 ;;
				esac
				;;
		esac
		;;
esac

#standard handling
case "$ACTION" in
	"powerbutton_one")
		if echo "$WMCLASS" | grep -i "megapixels"; then
			sxmo_type.sh -k space
		else
			sxmo_state.sh click
		fi
		exit 0
		;;
	"powerbutton_two")
		sxmo_state.sh click 2
		exit 0
		;;
	"powerbutton_three")
		sxmo_terminal.sh
		exit 0
		;;
	"voldown_one")
		# The keyboard opens on text focus; volume down just lowers the volume
		sxmo_audio.sh vol down
		exit
		;;
	"voldown_two")
		# Menus are on the top-edge swipe; every volume press changes the volume
		sxmo_audio.sh vol down
		exit
		;;
	"voldown_three")
		# Menus are on the top-edge swipe; holding keeps changing the volume
		volume_hold down
		exit
		;;
	"volup_one")
		# Menu moved to the top-edge swipe down; volume up just raises the volume
		sxmo_audio.sh vol up
		exit
		;;
	"volup_two")
		# Menus are on the top-edge swipe; every volume press changes the volume
		sxmo_audio.sh vol up
		exit
		;;
	"volup_three")
		# Menus are on the top-edge swipe; holding keeps changing the volume
		volume_hold up
		exit
		;;
	"rightleftedge")
		sxmo_wm.sh previousworkspace
		exit 0
		;;
	"leftrightedge")
		sxmo_wm.sh nextworkspace
		exit 0
		;;
	"twoleft")
		sxmo_wm.sh movepreviousworkspace
		exit 0
		;;
	"tworight")
		sxmo_wm.sh movenextworkspace
		exit 0
		;;
	"righttopedge")
		sxmo_brightness.sh up
		exit 0
		;;
	"lefttopedge")
		sxmo_brightness.sh down
		exit 0
		;;
	"upleftedge")
		sxmo_audio.sh vol up
		exit 0
		;;
	"downleftedge")
		sxmo_audio.sh vol down
		exit 0
		;;
	"upbottomedge")
		# No keyboard gesture: wvkbd opens itself when a text field gets focus
		# (--auto, see hooks/sxmo_keyboard.sh); the nav bar sits on this edge
		exit 0
		;;
	"downbottomedge")
		# The keyboard hides from its button on the nav bar (bottom right)
		exit 0
		;;
	"downtopedge")
		# Notification shade with quick toggles (lagos-statusbar); the sxmo
		# app menu is on Home pressed on an empty workspace
		sxmo_dmenu.sh isopen || "$HOME"/.local/bin/lagos-shade toggle
		exit 0
		;;
	"twodowntopedge")
		sxmo_hook_quickactions.sh
		exit 0
		;;
	"uptopedge")
		"$HOME"/.local/bin/lagos-shade close
		sxmo_dmenu.sh close
		dunstctl close-all
		exit 0
		;;
	"twodownbottomedge")
		sxmo_killwindow.sh
		exit 0
		;;
	"uprightedge")
		sxmo_type.sh -k Up
		exit 0
		;;
	"downrightedge")
		sxmo_type.sh -k Down
		exit 0
		;;
	"leftrightedge_short")
		sxmo_type.sh -k Left
		exit 0
		;;
	"rightrightedge_short")
		sxmo_type.sh -k Right
		exit 0
		;;
	"rightbottomedge")
		sxmo_type.sh -k Return
		exit 0
		;;
	"leftbottomedge")
		sxmo_type.sh -k BackSpace
		exit 0
		;;
	"topleftcorner")
		sxmo_appmenu.sh sys
		exit 0
		;;
	"toprightcorner")
		sxmo_appmenu.sh scripts
		exit 0
		;;
	"bottomleftcorner")
		sxmo_dmenu.sh close
		sxmo_keyboard.sh close
		if [ -n "$WMCLASS" ]; then
			sxmo_state.sh set lock
		else
			sxmo_state.sh set screenoff
		fi
		exit 0
		;;
	"bottomrightcorner")
		sxmo_rotate.sh
		exit 0
		;;
esac

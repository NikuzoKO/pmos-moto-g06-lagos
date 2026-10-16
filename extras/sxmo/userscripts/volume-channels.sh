#!/bin/sh
# title="$icon_spk Volume channels"
# Per-channel volumes (see ~/.config/pipewire/pipewire.conf.d/60-lagos-channels.conf):
# Media = default sink (volume keys), Notifications = ring/SMS/app sounds.

# include common definitions
# shellcheck source=scripts/core/sxmo_common.sh
. sxmo_common.sh

SOUND="$HOME/.local/bin/lagos-sound"

# "<volume>%", "vibrate" or "muted" for a channel
show() {
	"$SOUND" status | awk -v c="$1" '$1 == c { print ($3 == "vibrate" ? "vibrate" : $3 == "mute" ? "muted" : $2 "%") }'
}

setvol() {
	pick="$(printf '%s\n' "$icon_cls Back" 0 10 20 30 40 50 60 70 80 90 100 |
		sxmo_dmenu.sh -p "$2 volume")" || return
	case "$pick" in
		*Back) return ;;
	esac
	"$SOUND" set "$1" sound
	"$SOUND" volume "$1" "$pick"
	[ "$1" = notify ] && "$SOUND" play notify "${SXMO_TEXTSOUND:-/usr/share/sxmo/notify.ogg}" &
}

while : ; do
	pick="$(printf '%s\n' \
		"$icon_cls Close Menu" \
		"$icon_mus Music: $(show media)" \
		"$icon_bel Notifications: $(show notify)" \
		"$icon_bel Alarms: $(show alarm)" \
		"$icon_cfg System: $(show system)" |
		sxmo_dmenu.sh -p "Volume")" || exit 0
	case "$pick" in
		*Music:*) setvol media Music ;;
		*Notifications:*) setvol notify Notifications ;;
		*Alarms:*) setvol alarm Alarms ;;
		*System:*) setvol system System ;;
		*) exit 0 ;;
	esac
done

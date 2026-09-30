#!/bin/sh
# title="$icon_spk Volume channels"
# Per-channel volumes (see ~/.config/pipewire/pipewire.conf.d/60-lagos-channels.conf):
# Media = default sink (volume keys), Notifications = ring/SMS/app sounds.

# include common definitions
# shellcheck source=scripts/core/sxmo_common.sh
. sxmo_common.sh

vol() { wpctl get-volume "$1" 2>/dev/null | awk '{ v = int($2 * 100 + 0.5); print ($3 ? v "% (muted)" : v "%") }'; }

setvol() {
	pick="$(printf '%s\n' "$icon_cls Back" 0 10 20 30 40 50 60 70 80 90 100 |
		sxmo_dmenu.sh -p "$2 volume")" || return
	case "$pick" in
		*Back) return ;;
	esac
	wpctl set-mute "$1" 0
	wpctl set-volume "$1" "$pick%"
	[ "$2" = Notifications ] && mpv --no-resume-playback --quiet --no-video \
		--audio-device=pipewire/lagos-notify "${SXMO_TEXTSOUND:-/usr/share/sxmo/notify.ogg}" >/dev/null 2>&1 &
}

nodeid() { pw-cli ls Node 2>/dev/null | awk -v n="\"$1\"" '/^\tid /{ i = $2 } $3 == n { sub(",", "", i); print i; exit }'; }

while : ; do
	pick="$(printf '%s\n' \
		"$icon_cls Close Menu" \
		"$icon_mus Media: $(vol "$(nodeid lagos-media)")" \
		"$icon_bel Notifications: $(vol "$(nodeid lagos-notify)")" |
		sxmo_dmenu.sh -p "Volume")" || exit 0
	case "$pick" in
		*Media:*) setvol "$(nodeid lagos-media)" Media ;;
		*Notifications:*) setvol "$(nodeid lagos-notify)" Notifications ;;
		*) exit 0 ;;
	esac
done

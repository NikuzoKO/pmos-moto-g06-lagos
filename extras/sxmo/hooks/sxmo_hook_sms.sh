#!/bin/sh
# configversion: 26b9fe022a43d084906232c0de6739dd
# SPDX-License-Identifier: AGPL-3.0-only
# Copyright 2022 Sxmo Contributors

# This script is executed after you received a text, mms, or vvm
# You can use it to play a notification sound or forward the sms elsewhere

# The following parameters are provided:
# $1 = Contact Name or Number (if number not in contacts)
# $2 = Text (or 'VVM' if a vvm)
# mms and vvm will include these parameters:
# $3 = MMS or VVM payload ID
# Finally, mms may include this parameter:
# $4 = Group Contact Name or Number (if number not in contacts)

# shellcheck source=scripts/core/sxmo_common.sh
. sxmo_common.sh

# do nothing if active call
if sxmo_modemcall.sh list_active_calls | grep -q active; then
	exit
fi

# do nothing if silent mode
if [ -f "$XDG_CONFIG_HOME"/sxmo/.busy ]; then
	end="$(cat "$XDG_CONFIG_HOME"/sxmo/.busy)"
	if [ -z "$end" ] || [ "$(date +%s)" -lt "$end" ]; then
		exit
	fi
fi

# Channel state from the volume panel (and vibrate-only at night)
mode="$("$HOME/.local/bin/lagos-sound" mode notify 2>/dev/null || echo sound)"

if [ "$mode" = sound ] && [ ! -f "$XDG_CONFIG_HOME"/sxmo/.noring ]; then
	mpv --no-resume-playback --quiet --no-video --audio-device=pipewire/lagos-notify "$SXMO_TEXTSOUND" >> /dev/null 2>&1 &
fi

if [ "$mode" != mute ] && [ ! -f "$XDG_CONFIG_HOME"/sxmo/.novibrate ]; then
	sxmo_vibrate 500 "${SXMO_VIBRATE_STRENGTH:-1}" &
fi

wait

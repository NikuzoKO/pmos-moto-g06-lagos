#!/bin/sh
# configversion: f1282bb39c782471bb788ccf8df0bb76
# Stock app list plus Android apps run through Android Translation Layer,
# slotted in alphabetically.
. sxmo_hook_icons.sh

/usr/share/sxmo/default_hooks/sxmo_hook_apps.sh | awk -v entry="$icon_wap WhatsApp ^ 0 ^ $HOME/.local/bin/atl-whatsapp" '
	/ Foot \^ / { sub(/Foot \^ 0 \^ .*/, "Kitty ^ 0 ^ kitty") }  # lagos: kitty replaces foot
	!done && tolower(substr($0, index($0, " ") + 1)) > "whatsapp" { print entry; done = 1 }
	{ print }
	END { if (!done) print entry }'

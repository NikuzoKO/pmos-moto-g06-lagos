#!/bin/sh
# default statusbar hook minus the red "!: N" unread-notification counter
# (set_notifications returns early when SXMO_DISABLE_LEDS is empty)
unset SXMO_DISABLE_LEDS
exec /usr/share/sxmo/default_hooks/sxmo_hook_statusbar.sh "$@"

#!/bin/sh
# lagos: shadows /usr/sbin/sxmo_terminal.sh (~/.config/sxmo/hooks is first in
# PATH) to add kitty, the terminal here (SXMO_TERMINAL=kitty in
# ~/.config/sxmo/profile). A session started before that change still says
# foot, so foot maps to kitty too.
# SPDX-License-Identifier: AGPL-3.0-only
# Copyright 2022 Sxmo Contributors
# shellcheck disable=SC2086

if [ -z "$*" ]; then
	set -- $SHELL
fi

if [ -z "$TERMNAME" ]; then
	TERMNAME="$*"
fi

case "$SXMO_TERMINAL" in ""|foot*) SXMO_TERMINAL=kitty ;; esac

case "$SXMO_TERMINAL" in
	"kitty"*)
		set -- $SXMO_TERMINAL -T "$TERMNAME" "$@"
		;;
	"st"*)
		set -- $SXMO_TERMINAL -T "$TERMNAME" -e "$@"
		;;
	"tilix"*)
		set -- $SXMO_TERMINAL -t "$TERMNAME" -e "$@"
		;;
	"foot"*)
		set -- $SXMO_TERMINAL -T "$TERMNAME" "$@"
		;;
	"vte-2.91"*)
		set -- ${SXMO_TERMINAL% --} --title "$TERMNAME" -- "$@"
		;;
	"alacritty"*)
		# Test if alacritty was called with shell or a program
		# Even with dynamic_title = true in config title will be static with -T switch
		if [ "$*" = "$SHELL" ]; then
			set -- $SXMO_TERMINAL
		else
			set -- $SXMO_TERMINAL -T "$TERMNAME" -e "$@"
		fi
		;;
	*)
		printf "%s: '%s'\n" "Not implemented for SXMO_TERMINAL" "$SXMO_TERMINAL" >&2
		set -- $SXMO_TERMINAL "$@"
esac

# Optional file descriptor the command can use for stdin/stdout/stderr
exec 4<&0
exec 5>&1
exec 6>&2

exec "$@"

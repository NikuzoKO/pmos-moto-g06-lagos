# Rosé Pine, matching the desktop

The phone's sxmo session themed like the author's Hyprland desktop (Rosé
Pine; Rosé Pine Moon in the terminal). User configuration, not packaged.

| File | Goes to |
|---|---|
| `foot.ini` | `~/.config/foot/foot.ini` (Roboto Mono, Rosé Pine Moon) |
| `sxmo-sway` | `~/.config/sxmo/sway` (bar colours, no borders, no gaps) |
| `sxmo-conky.conf` | `~/.config/sxmo/conky.conf` (clock) |
| `dunst-rosepine.conf` | `~/.config/dunst/dunstrc.d/50-rosepine.conf` |
| `gtk-settings.ini` | `~/.config/gtk-3.0/settings.ini` and `~/.config/gtk-4.0/settings.ini` |
| `50-sxmo.conf`, `60-roboto-mono.conf` | `~/.config/fontconfig/conf.d/`: Roboto Mono for sxmo's "Sxmo" alias and every generic family, the Nerd Font behind it for icons |

Add to `~/.config/sxmo/profile`:

```sh
export BEMENU_OPTS="--fn 'Roboto Mono 11' --tb '#191724' --tf '#c4a7e7' --fb '#191724' --ff '#e0def4' --nb '#191724' --nf '#908caa' --ab '#191724' --af '#908caa' --hb '#26233a' --hf '#c4a7e7' --sb '#403d52' --sf '#e0def4' --cb '#191724' --cf '#e0def4' --scb '#191724' --scf '#6e6a86'"
export KEYBOARD_ARGS="--bg 191724 --fg 26233a --fg-sp 1f1d2e --press 524f67 --press-sp 403d52 --text e0def4 --text-sp c4a7e7 --fn 'Roboto Mono 14'"
export SXMO_BG_IMG="$HOME/.local/share/sxmo/background.jpg"
```

Every font is Roboto Mono. Packages: `font-roboto-mono adw-gtk3
papirus-icon-theme`. The `ryoku-folders` icons (Papirus-Dark based) come from the desktop into
`~/.local/share/icons`. The wallpaper goes to
`~/.local/share/sxmo/background.jpg`; sxmo's swaybg shows it in fill mode,
which for a landscape image on this portrait panel means full height,
centred. The lock screen colours are in `pmaports/peanutbutter` (0102).

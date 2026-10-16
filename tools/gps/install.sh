#!/bin/sh
# Build gpsfeed against the camroot's bionic, install it, the service and the geoclue hookup.
# Run as root (sudo -n sh install.sh) with the camroot mounted (camroot.sh).
set -eu
HERE=$(dirname "$(readlink -f "$0")")
M=/var/lib/lagos/camroot/merged
B=$M/apex/com.android.runtime/lib64/bionic
gcc -O2 -nostartfiles -fno-stack-protector -Wl,--allow-shlib-undefined \
	-Wl,-dynamic-linker,/system/bin/linker64 -o /tmp/gpsfeed "$HERE/gpsfeed.c" $B/libc.so $B/libdl.so
install -m755 /tmp/gpsfeed $M/vendor/bin/gpsfeed
install -Dm644 "$HERE/lagos-gps.service" /etc/systemd/system/lagos-gps.service
# geoclue: read the relay's socket
if ! grep -q '^nmea-socket=' /etc/geoclue/geoclue.conf; then
	cp /etc/geoclue/geoclue.conf /etc/geoclue/geoclue.conf.bak
	sed -i 's|^\[network-nmea\]$|[network-nmea]\nnmea-socket=/run/lagos-gps/nmea.sock|' /etc/geoclue/geoclue.conf
fi
systemctl daemon-reload

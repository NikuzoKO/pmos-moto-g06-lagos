#!/bin/sh
# Build the minimal Android root that the stock MediaTek daemons run in
# (ccci_mdinit/ccci_rpcd for the modem, fuelgauged/fuelgauged_nvram for the
# battery gauge), from your own phone's extracted vendor and system partitions.
#
# usage: build.sh <vendor dir> <system dir> <output.tar.gz>
#   <vendor dir>  extracted vendor_a (has bin/ccci_mdinit, lib64/)
#   <system dir>  extracted system_a root (has system/lib64,
#                 system/apex/com.android.runtime.apex)
# needs: llvm-readelf, clang + ld.lld, unzip, 7z
#
# Install on the phone: tar -C /var/lib/lagos -xzf <output.tar.gz>

set -eu

VENDOR=$(realpath "$1")
SYSTEM=$(realpath "$2")
OUTPUT=$(realpath "$3")
HERE=$(dirname "$(realpath "$0")")
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

# Bionic and the linker live in the runtime APEX (an ext4 image in a zip)
unzip -q -d "$WORK/apex" "$SYSTEM/system/apex/com.android.runtime.apex" apex_payload.img
7z x -y -o"$WORK/apex/payload" "$WORK/apex/apex_payload.img" >/dev/null
BIONIC="$WORK/apex/payload/lib64/bionic"

ROOT="$WORK/android"
RUNTIME="$ROOT/apex/com.android.runtime"
mkdir -p "$ROOT/system/bin" "$ROOT/system/lib64" "$ROOT/vendor/bin" \
	"$ROOT/vendor/lib64" "$ROOT/vendor/etc" "$RUNTIME/bin" "$RUNTIME/lib64/bionic" \
	"$ROOT/dev" "$ROOT/proc" "$ROOT/sys" "$ROOT/tmp" "$ROOT/data/vendor"
cp "$WORK/apex/payload/bin/linker64" "$RUNTIME/bin/"
ln -s /apex/com.android.runtime/bin/linker64 "$ROOT/system/bin/linker64"
BINARIES="ccci_mdinit ccci_rpcd fuelgauged fuelgauged_nvram"
# Loaded with dlopen() (by fuelgauged and libnvram), so they aren't in any
# NEEDED list
DLOPENED="libfgauge_gm30.so libcustom_nvram.so"
for binary in $BINARIES; do
	cp "$VENDOR/bin/$binary" "$ROOT/vendor/bin/"
done

# Copy each needed library from bionic, vendor or system, in that order.
# The system libbinder goes in place of the vendor one: the system
# libbinder_ndk won't link against the VNDK libbinder without namespaces.
place_library() {
	name=$1
	if [ -e "$BIONIC/$name" ]; then
		cp "$BIONIC/$name" "$RUNTIME/lib64/bionic/"
		ln -sf "/apex/com.android.runtime/lib64/bionic/$name" "$ROOT/system/lib64/$name"
	elif [ "$name" != libbinder.so ] && [ -e "$VENDOR/lib64/$name" ]; then
		cp "$VENDOR/lib64/$name" "$ROOT/vendor/lib64/"
	elif [ -e "$SYSTEM/system/lib64/$name" ]; then
		cp "$SYSTEM/system/lib64/$name" "$ROOT/system/lib64/"
	else
		echo "missing library: $name" >&2
		exit 1
	fi
}

needed_libraries() {
	llvm-readelf -d "$1" | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p'
}

library_path() {
	for directory in "$ROOT/vendor/lib64" "$RUNTIME/lib64/bionic" "$ROOT/system/lib64"; do
		[ -f "$directory/$1" ] && { echo "$directory/$1"; return; }
	done
}

queue=""
for binary in $BINARIES; do
	queue="$queue $ROOT/vendor/bin/$binary"
done
seen=" "
for name in $DLOPENED; do
	seen="$seen$name "
	place_library "$name"
	queue="$queue $(library_path "$name")"
done
while [ -n "$queue" ]; do
	set -- $queue
	current=$1
	shift
	queue="$*"
	for name in $(needed_libraries "$current"); do
		case "$seen" in *" $name "*) continue ;; esac
		seen="$seen$name "
		place_library "$name"
		queue="$queue $(library_path "$name")"
	done
done

# Stand-in for Android's property service, built against the phone's bionic
clang --target=aarch64-linux-android30 -O2 -fPIC -shared -nostdlib -fuse-ld=lld \
	-Wl,-soname,libpropshim.so -o "$ROOT/system/lib64/libpropshim.so" \
	"$HERE/propshim.c" -L"$BIONIC" -lc
cp "$HERE/lagos.prop" "$ROOT/vendor/etc/"

tar -C "$WORK" -czf "$OUTPUT" android
echo "wrote $OUTPUT"

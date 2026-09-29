#!/bin/sh
# Build a throwaway Android root for the stock MTK camera HAL.
# Stock system_a/vendor_a from super are the read-only lower layers; writes go
# to /var/lib/lagos/camroot/{upper,vupper}. Run as root. `camroot.sh down` undoes it.
set -eu

SUPER=/dev/disk/by-partlabel/super
BASE=/var/lib/lagos/camroot
M=$BASE/merged
SYS=/mnt/stock-system
VEN=/mnt/stock-vendor

# name start-sector sectors fstype (from `lpdump super`, slot a)
map_part() {
	mountpoint -q "$2" && return 0
	loop=$(losetup -f)
	losetup -r -o $(($3 * 512)) --sizelimit $(($4 * 512)) "$loop" "$SUPER"
	mkdir -p "$2"
	mount -t "$5" -o ro${6:+,$6} "$loop" "$2"
}

down() {
	for mnt in $(awk -v m="$M" 'index($2, m) == 1 {print $2}' /proc/mounts | sort -r); do
		umount -l "$mnt" || true
	done
}

[ "${1:-}" = down ] && { down; exit 0; }

map_part system_a $SYS 2728 3603432 ext4 noload
map_part vendor_a $VEN 9211904 1014272 erofs

mkdir -p $BASE/upper $BASE/work $BASE/vupper $BASE/vwork $M
mountpoint -q $M || mount -t overlay overlay \
	-o lowerdir=$SYS,upperdir=$BASE/upper,workdir=$BASE/work $M
mountpoint -q $M/vendor || mount -t overlay overlay \
	-o lowerdir=$VEN,upperdir=$BASE/vupper,workdir=$BASE/vwork $M/vendor

mount -t proc proc $M/proc
mount --rbind /sys $M/sys
mount --make-rslave $M/sys

# /dev: a private tmpfs with copies of the host nodes plus our own binderfs
mount -t tmpfs -o mode=755 dev $M/dev
(cd /dev && find . -xdev \( -type c -o -type b -o -type d -o -type l \) \
	! -path './pts*' ! -path './shm*' | cpio -pdm $M/dev 2>/dev/null)
mkdir -p $M/dev/binderfs $M/dev/socket $M/dev/pts
mount -t binder binder $M/dev/binderfs
for dev in binder hwbinder vndbinder; do ln -sf binderfs/$dev $M/dev/$dev; done
chmod 666 $M/dev/binderfs/*binder

# apexes: payload.img sits stored at offset 4096 in each .apex zip
mount -t tmpfs -o mode=755 apex $M/apex
info=$M/apex/apex-info-list.xml
echo '<?xml version="1.0" encoding="utf-8"?><apex-info-list>' > $info
for apex in com.android.runtime com.android.i18n com.android.tzdata com.android.os.statsd; do
	img=$SYS/system/apex/$apex.apex
	size=$(python3 -c "import zipfile,sys; print(zipfile.ZipFile(sys.argv[1]).getinfo('apex_payload.img').file_size)" $img)
	loop=$(losetup -f)
	losetup -r -o 4096 --sizelimit $size $loop $img
	mkdir -p $M/apex/$apex
	mount -o ro $loop $M/apex/$apex
	echo "<apex-info moduleName=\"$apex\" modulePath=\"/system/apex/$apex.apex\" preinstalledModulePath=\"/system/apex/$apex.apex\" versionCode=\"1\" isFactory=\"true\" isActive=\"true\" provideSharedApexLibs=\"false\" partition=\"SYSTEM\"/>" >> $info
done
echo '</apex-info-list>' >> $info

# dirs stock vendor init creates that the camera HAL writes to
for dir in linkerconfig tmp data/misc data/local/tmp mnt/vendor/cct \
	data/vendor/camera_dump data/vendor/flash data/vendor/shading data/vendor/shading_otp \
	data/vendor/dipdebug data/vendor/flicker data/vendor/aee_exp data/vendor/tombstones; do
	mkdir -p $M/$dir
done
# property shim (tools/android-root/propshim.c) against bionic, into both partitions
HERE=$(dirname "$(readlink -f "$0")")
gcc -O2 -fPIC -shared -nostdlib -nostartfiles -fno-stack-protector \
	-Wl,--hash-style=both -Wl,-z,noexecstack -Wl,-soname,libpropshim.so \
	-o $BASE/libpropshim.so $HERE/../android-root/propshim.c \
	$M/apex/com.android.runtime/lib64/bionic/libc.so
install -m644 $BASE/libpropshim.so $M/system/lib64/libpropshim.so
install -m644 $BASE/libpropshim.so $M/vendor/lib64/libpropshim.so
# props are per process: anything another daemon would set must be here
{
	echo servicemanager.ready=true
	# no HIDL on this device: libhidl clients check this instead of asking hwservicemanager
	echo hwservicemanager.disabled=true
	echo ro.hardware=mt6768
	echo ro.boot.hardware=mt6768
	echo ro.boot.hardware.platform=mt6768
	echo ro.debuggable=0
	cat $HERE/../android-root/lagos.prop
	cat $M/system/build.prop $M/vendor/odm/etc/build.prop $M/vendor/build.prop
} | grep -E '^[a-z].*=' > $M/vendor/etc/lagos.prop

mount -t tmpfs -o mode=755 linkerconfig $M/linkerconfig
mount -t tmpfs -o mode=1777 tmp $M/tmp
# properties matter: without them linkerconfig emits the flat legacy config
env -i LD_PRELOAD=/system/lib64/libpropshim.so chroot $M /apex/com.android.runtime/bin/linkerconfig --target /linkerconfig
echo "camroot ready at $M"

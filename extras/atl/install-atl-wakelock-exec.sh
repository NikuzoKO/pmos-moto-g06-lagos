#!/bin/sh
# Build and install atl-wakelock-exec (run as root). No libcap-utils needed:
# the file capability is written as a raw security.capability xattr.
set -e
dir=$(dirname "$0")
cc -O2 -static -o /usr/local/bin/atl-wakelock-exec "$dir/atl-wakelock-exec.c"
chown root:wheel /usr/local/bin/atl-wakelock-exec
chmod 0750 /usr/local/bin/atl-wakelock-exec
# vfs_cap_data v2: magic 0x02000000, no effective flag; permitted CAP_BLOCK_SUSPEND (36 = bit 4 of the 2nd word)
python3 -c 'import os, struct; os.setxattr("/usr/local/bin/atl-wakelock-exec", "security.capability", struct.pack("<5I", 0x02000000, 0, 0, 1 << 4, 0))'
# the wakelock files are root:root 0644 by default
cat > /etc/tmpfiles.d/lagos-wakelock.conf <<'EOT'
# Android apps under ATL take kernel wakelocks (PowerManager.WakeLock) through atl-wakelock-exec
z /sys/power/wake_lock 0660 root wheel -
z /sys/power/wake_unlock 0660 root wheel -
EOT
systemd-tmpfiles --create /etc/tmpfiles.d/lagos-wakelock.conf

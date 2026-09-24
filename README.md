# postmarketOS on the Motorola Moto G06 (`lagos`)

Work-in-progress postmarketOS port for the Motorola Moto G06 / G06 Power
(`lagos`, XT2535-x), MediaTek Helio G81 Ultra (**MT6768**, same die as the
Galaxy A31's "Helio P65").

## Status (2026-09-24 evening)

| Area | State |
|---|---|
| Bootloader unlock | Works via the community **Penumbra/antumbra + kaeru** exploit (no official Motorola unlock for lagos) |
| Kernel | **Stock Android GKI kernel** `6.6.66-android15-8-g9b6ad5b4b813-ab13433063-4k` (from stock `boot_a`) |
| Vendor drivers | Stock first-stage vendor modules (156, from the stock `vendor_boot` ramdisk), loaded in stock `modules.load` order |
| eMMC | ✅ `mmcblk0` + all partitions |
| USB networking | ✅ CDC NCM gadget, phone at `172.16.42.1` |
| initramfs → rootfs | ✅ `switch_root` into the pmOS rootfs, systemd reaches a (degraded) running state |
| SSH | ✅ `ssh user@172.16.42.1`. **~29s from reboot to SSH, no manual steps** (with plymouth masked, see below) |
| Display | ⚠️ Vendor MTK DRM loads (`/dev/dri/card0`, a connector reports `connected`), but nothing draws yet. The screen stays on LK's "hello moto" frame |
| Touch, audio, modem, WiFi/BT, battery | ❌ Not attempted yet |
| Watchdog | ✅ Fed by stock `mtk_wdt.ko` (no more resets) |
| Known failed units | `getty@tty1` (no VT), `nftables`, `postmarketos-zram-swap` (modules not loaded). All harmless |

Tested on slot A only. **Slot B is unusable on the test unit**: it black-screen
bootloops even with stock firmware, and its `system_b` is only ~11.6 MB.

## Why the stock GKI kernel instead of the Motorola kernel source?

The package in `pmaports/linux-motorola-lagos` builds Motorola's
[`kernel-mtk`](https://github.com/MotorolaMobilityLLC/kernel-mtk) (6.6.56 at the
pinned commit). That kernel boots and runs pmOS's initramfs, but everything
important on this SoC (eMMC host, clocks, pinctrl, PMIC wrapper, regulators,
USB PHY/musb, display, watchdog) comes from **vendor modules** built against
Google's exact GKI build `6.6.66-android15-8-…`. They don't load on a
self-built kernel (KMI/modversions mismatch), and the eMMC node
(`mediatek,mt6768-mmc`) isn't matched by upstream `mtk-sd`. So the rootfs
never appears.

So the working path is **stock GKI kernel + stock vendor modules**, with
initramfs patches to cope with the GKI config:

- **No `CONFIG_DEVTMPFS`**: `/dev` is a tmpfs populated with busybox `mdev -s`.
- **Load order matters**: the modules are loaded in stock `modules.load` order
  *before* udev starts. Otherwise udev's modalias autoload races them (e.g.
  `clkbuf` oopses in `pmif_parse_dts_v1` when it probes before the PMIC
  wrapper).
- **kmod `modprobe` in stage 2** needs `modules.dep.bin` & co., so run
  `depmod -a <gki-version>` in the rootfs chroot.

The own-kernel route stays open for later: rebuild Google's exact
android15-6.6 source at the matching tag with `DEVTMPFS` enabled, keeping the
KMI intact.

## Repo layout

```
pmaports/
  device-motorola-lagos/   deviceinfo (load addresses, header v4, cmdline), stock DTB table
  linux-motorola-lagos/    kernel-mtk build (currently NOT what boots, see above)
initramfs-gki/
  init.sh.patch            load_gki_modules in stage 1 (only used with a separate initramfs-extra)
  init_2nd.sh.patch        load_gki_modules before setup_udev (the fix that made it boot)
  init_functions.sh.patch  tmpfs+mdev /dev fallback, mdev after losetup, load_gki_modules, lagos_debug_crash on fail_halt_boot
  gki.load                 stock modules.load order (156 modules)
  90-lagos-gki.files       mkinitfs extra-files list (modules + index files)
tools/
  extract_pstore.py        pull the kernel console / pmsg out of an expdb dump
  pmos-telnet.py           run commands in the initramfs debug shell over USB (telnet :23)
```

The initramfs patches are against `postmarketos-initramfs` **3.12.3-r1** and
were applied by hand in the rootfs chroot. Upgrading that package reverts them.
They are not packaged yet.

## Key findings (the non-obvious ones)

**Boot image / LK**
- The MTK LK needs the stock load addresses: base `0x40000000`, kernel
  `+0x80000`, ramdisk `+0x07c80000`, tags/dtb `+0x0bc80000`. With the
  mkbootimg defaults (`0x10000000`, not DRAM here) LK fails `mblock_reserve`
  and asserts in `mt_boot.c`.
- boot-deploy's header v3/v4 `mkbootimg` call **ignores
  `deviceinfo_flash_offset_*`**, so the addresses have to go in
  `deviceinfo_bootimg_custom_args`.
- LK always decompresses the kernel (`panic: decompress kernel image fail!!!`
  on a raw `Image`), so it has to be a **gzip** kernel.
- The cmdline needs `bootopt=64S3,32N2,64N2`.
- LK expects the stock MTK **dt_table** blob (magic `d7b7ab1e`, not a bare FDT)
  in vendor_boot's dtb section. That's `lagos-stock.dtb`, extracted from stock
  `vendor_boot_a`.
- **`init_boot_a` must be emptied.** Otherwise its ramdisk (Android
  first-stage init) is concatenated on top and runs instead of pmOS's `/init`.

**Plymouth must be masked**
- `plymouth-read-write.service` (`plymouth update-root-fs --read-write`)
  hangs forever on the vendor MTK DRM device. It's a oneshot ordered before
  `sysinit.target`, so dbus, logind and sshd start minutes late or not at all.
  SSH then authenticates but the session never opens, because `pam_systemd`
  waits for logind. Mask every `plymouth*.service` in `/etc/systemd/system`
  (`ln -sf /dev/null …`). After that, sshd is up ~10s after `switch_root`.

**Debugging without UART**
- On an abnormal reset, LK's `kedump` copies the whole pstore/ramoops region
  into the **`expdb`** partition. Dump `expdb` (antumbra in download mode, or
  `dd` from `/dev/mmcblk0p3` once pmOS is running) and run
  `tools/extract_pstore.py` on it to get the kernel console and pmsg logs.
  This is how every boot problem after the bootloader stage was found.
- A clean hang (no crash) leaves nothing in expdb. The patched initramfs
  crashes on purpose (`echo c > /proc/sysrq-trigger`) from `fail_halt_boot` so
  that kedump fires. (An earlier 150s "still in initramfs" crash timer was
  dropped: it also fired while paused in `pmos.debug-shell`, and even after
  `switch_root`.)
- Once the rootfs is up but unreachable, a debug-only systemd oneshot that
  dumps `ps`, `/proc/1/{wchan,stack}` and `journalctl _PID=1` to
  `/var/log`, read back later from the initramfs debug shell, is what found
  the plymouth hang.
- `pmos.debug-shell` on the cmdline gives a telnet shell on
  `172.16.42.1:23` before root is mounted (`tools/pmos-telnet.py`).
- kaeru's `fastboot oem mem read` (CONFIG_FASTBOOT_MEM_COMMAND) can't read
  pstore: LK maps it `map:0` and the read faults.

**kaeru on lagos**
- Upstream kaeru commit c43e231 ("lagos: Update offsets") targets **newer**
  firmware than this unit. Building with the older offsets was needed.
- The new kaeru honours the `misc` BCB. After failed boots `misc` holds
  `boot-recovery`, and Power+Vol-Down lands in recovery. Reliable route to
  fastboot: write `boot-bootloader` into `misc` with antumbra from download
  mode, then power on.

## Reproducing (outline)

1. Unlock with Penumbra/antumbra + kaeru (see XDA; firmware-version dependent,
   the Feb 2026 preloader reportedly blocks the DA crash step).
2. Copy `pmaports/*` into your pmaports checkout (`device/downstream/`),
   `pmbootstrap init` for `motorola-lagos`, `pmbootstrap install`.
3. In the rootfs chroot:
   - replace `/boot/vmlinuz` with the gzip kernel from your stock `boot_a.img`
     (an upgrade of `linux-motorola-lagos` overwrites this);
   - copy the stock vendor_boot ramdisk modules (`lib/modules/*.ko` +
     `modules.load`, lz4 ramdisk) to
     `/usr/lib/modules/6.6.66-android15-8-g9b6ad5b4b813-ab13433063-4k/`, make
     `modules.dep` paths relative, run
     `depmod -a 6.6.66-android15-8-g9b6ad5b4b813-ab13433063-4k`;
   - install `initramfs-gki/gki.load` as `/usr/lib/modules/gki.load` and
     `90-lagos-gki.files` into `/usr/share/mkinitfs/files/`;
   - apply the three `initramfs-gki/*.patch` in `/usr/share/initramfs/`;
   - mask plymouth: `for u in /usr/lib/systemd/system/plymouth*.service; do ln -sf /dev/null /etc/systemd/system/$(basename $u); done`;
   - `mkinitfs`.
4. Flash `boot.img` → `boot_a`, `vendor_boot.img` → `vendor_boot_a`, an empty
   image → `init_boot_a`, and the pmOS disk image → `userdata` (pmOS finds
   `pmOS_boot`/`pmOS_root` as subpartitions via loop).
5. Host: `ip link set <usb-if> up; ip addr add 172.16.42.2/24 dev <usb-if>`.
   The interface re-enumerates once at `switch_root`, so do it again then.

Stock firmware blobs and the vendor `.ko` files are not redistributed here.
Extract them from your own device.

## Next steps

1. Display: the MTK DRM driver is already loaded. Try a bare KMS client
   (`kmscube`, `modetest`) to see whether it can scan out, then a compositor
   (phosh/sxmo). Plymouth's hang on this device suggests DRM calls can block,
   so start with `modetest -c` and watch for hangs.
2. Touch, audio, WiFi/BT, modem: these need the second-stage vendor modules
   (vendor_dlkm) and firmware.
3. Package the initramfs hack and the plymouth masks properly (a
   device-specific hook/package instead of patching `postmarketos-initramfs`).
4. zram/nftables: load or ship the matching GKI modules.

## Prior art

- Galaxy A31 (MT6768) postmarketOS port: same SoC family.
- `device-xiaomi-lancelot` (Redmi 9, MT6769T): mainline port on
  `linux-postmarketos-mediatek-mt6768` (pmaports MR !7292).

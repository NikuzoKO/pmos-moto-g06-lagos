# Mali-G52 (panfrost) on the stock GKI 6.6 kernel

Out-of-tree gpu-sched.ko + panfrost.ko, built on the phone from
android15-6.6 @ 9b6ad5b4b813 (`~/build/gpu/lagos-gpu`, base commit ed33d91 there).

- `panfrost-mt6768.patch`: MTK power hook (mtk_gpufreq_wrapper_legacy), devfreq over
  the MTK OPP table, MMU IRQ unmask at init, runtime-PM ref fix on remove.
- `build.sh`, `Kbuild`, `mtk.symvers`: build glue (KDIR = ACK common, clang,
  KBUILD_EXTRA_SYMBOLS=mtk.symvers; .config needs the BTF symbols or struct module shifts).
- `lagos-gpu-load` + `lagos-gpu.service`: insmod at boot with a crash guard
  (/var/lib/lagos/gpu-disabled), writes /run/lagos-gpu.env (WLR_RENDERER=gles2).
- `tinydm-lagos-gpu.conf` → /etc/systemd/system/tinydm.service.d/lagos-gpu.conf.
- `lagos-gles-guard [switch]`: moves the session to GLES2 and falls back to pixman
  if sway doesn't survive 30 s within 4 min.
- `kmsgl.c`: GBM/EGL on card0 (mediatek) → mesa kmsro → panfrost, then AddFB2.
  `gcc -o kmsgl kmsgl.c $(pkg-config --cflags --libs gbm egl glesv2 libdrm)`

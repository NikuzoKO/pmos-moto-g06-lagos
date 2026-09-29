#!/bin/sh
# Out-of-tree gpu-sched + panfrost for the stock lagos GKI kernel
KDIR=${KDIR:-$HOME/build/gpu/common}
exec make -C "$KDIR" ARCH=arm64 LLVM=1 M="$(dirname "$(readlink -f "$0")")" \
	CONFIG_DRM_SCHED=m CONFIG_DRM_PANFROST=m \
	KBUILD_EXTRA_SYMBOLS="$(dirname "$(readlink -f "$0")")/mtk.symvers" "$@"

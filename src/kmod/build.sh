#!/usr/bin/env bash
# Build psset.ko and pd2055_cpufreq.ko against the PD2055 4.19.152-perf+ kernel.
#
# Needs the tree prepared by the PD2055 repository:
#   work/kernel-devsrc             4.19.125 tree with device headers overlaid
#   work/kernel-4.19.125-build     prepared arm64 objtree
#   work/kheaders/tree             device headers from /sys/kernel/kheaders.tar.xz
#   work/recon/boot/vmlinux.elf    device vmlinux, only when Module.symvers
#                                  is missing or REGEN_SYMVERS=1
set -euo pipefail

CHARGE=$(cd "$(dirname "$0")/../.." && pwd)
REPO=$(cd "$CHARGE/../.." && pwd)
MODS="psset pd2055_cpufreq"
MODDIR=$CHARGE/build/kmod-build
KSRC=${KSRC:-$REPO/work/kernel-devsrc}
KOBJ=${KOBJ:-$REPO/work/kernel-4.19.125-build}
KHDR=${KHDR:-$REPO/work/kheaders/tree}
VMLINUX=$REPO/work/recon/boot/vmlinux.elf

die() { echo "build kmod: $*" >&2; exit 1; }

[ -d "$KSRC" ] || die "missing $KSRC"
[ -f "$KOBJ/include/generated/autoconf.h" ] || die "missing prepared objtree $KOBJ"
[ -f "$KHDR/include/generated/autoconf.h" ] || die "missing device headers in $KHDR"

export PATH="$REPO/tools/cross-local/usr/bin:$REPO/tools/local/usr/bin:$PATH"
export LD_LIBRARY_PATH="$REPO/tools/cross-local/usr/lib/x86_64-linux-gnu:$REPO/tools/local/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export BISON_PKGDATADIR="$REPO/tools/local/usr/share/bison"
export M4="$REPO/tools/local/usr/bin/m4"

mkdir -p "$MODDIR" "$CHARGE/build"
OBJS=
for mod in $MODS; do
	cp "$CHARGE/src/kmod/$mod.c" "$MODDIR/$mod.c"
	OBJS="$OBJS $mod.o"
done
printf 'obj-m :=%s\n' "$OBJS" > "$MODDIR/Makefile"

if [ "${REGEN_SYMVERS:-0}" = 1 ] || [ ! -s "$KOBJ/Module.symvers" ]; then
	[ -f "$VMLINUX" ] || die "missing $VMLINUX for symbol CRCs"
	python3 -I "$REPO/work/port/gen_symvers.py" "$VMLINUX" "$KOBJ/Module.symvers"
fi

# Kbuild reads include/generated/autoconf.h from the objtree. The objtree
# was generated from the vanilla 4.19.125 Kconfig, so use the device copy
# for the build and restore it after.
BACKUP=$(mktemp)
cp "$KOBJ/include/generated/autoconf.h" "$BACKUP"
restore() {
	cp "$BACKUP" "$KOBJ/include/generated/autoconf.h"
	rm -f "$BACKUP"
}
trap restore EXIT
cp "$KHDR/include/generated/autoconf.h" "$KOBJ/include/generated/autoconf.h"

make -C "$KSRC" O="$KOBJ" ARCH=arm64 \
	CROSS_COMPILE=aarch64-linux-gnu- CC=aarch64-linux-gnu-gcc HOSTCC=gcc \
	KERNELRELEASE=4.19.152-perf+ M="$MODDIR" \
	KCFLAGS="-ffile-prefix-map=$REPO=/pd2055" \
	KBUILD_MODPOST_WARN=1 modules

for mod in $MODS; do
	aarch64-linux-gnu-strip --strip-debug "$MODDIR/$mod.ko"

	readelf -p .modinfo "$MODDIR/$mod.ko" | \
		grep -q 'vermagic=4.19.152-perf+ SMP preempt mod_unload modversions aarch64' || \
		die "bad vermagic: $mod"

	cp "$MODDIR/$mod.ko" "$CHARGE/build/$mod.ko"
	md5sum "$CHARGE/build/$mod.ko"
done

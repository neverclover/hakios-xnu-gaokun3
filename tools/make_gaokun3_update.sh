#!/usr/bin/env bash
# make_gaokun3_update.sh -- an "ipsw" for HakiOS on the MateBook E Go: only the parts that change.
#
# The stick keeps everything it has (system.apfs, the partition layout, the logdump volume); this
# package carries:
#   EFI/hakios/payload.bin   kernel + root.apfs + boot args   -- changes with every kernel build
#   EFI/BOOT/BOOTAA64.EFI    the loader                      -- only when the loader changed
# Both are ordinary files inside the HAKIOS_ESP volume of the stick; overwriting them IS the update.
# A full image (make_gaokun3_full_image.sh) is needed once, or when the layout/system changes.
#
# The package is a zip laid out exactly as the stick's ESP: unpack into the ESP volume and the
# files are where they belong. manifest.txt says what changed, with hashes and the kernel stamp,
# and apply-hakios-update.ps1 on the Windows side does the finding and the copying.
set -eu
cd "$(dirname "$0")/.."

test -f hakios-kernel; test -f xnuloader/loader.efi
test -f userland/root.apfs || { echo "run: REST=1 userland/build.sh first"; exit 1; }

ARGS="$(python3 tools/hakios-config.py args)"
if [ "${WANT_VERBOSE:-0}" = 1 ]; then ARGS="-v $ARGS"; fi

STAMP="$(git rev-parse --short=8 HEAD)"
NAME="hakios-gaokun3-update-g${STAMP}"

echo "== payload (kernel + root.apfs)"
python3 xnuloader/mkpayload.py -o build/update-payload.bin --platform gaokun3 \
	kernel=hakios-kernel ramdisk=userland/root.apfs --cmdline "$ARGS"

# NEXT_KERNEL=path: also ship that kernel as the "next" payload (the loader boots it when present)
if [ -n "${NEXT_KERNEL:-}" ]; then
	echo "== payload-next (the newer kernel being tried)"
	python3 xnuloader/mkpayload.py -o build/update-payload-next.bin --platform gaokun3 \
		"kernel=$NEXT_KERNEL" ramdisk=userland/root.apfs --cmdline "${NEXT_ARGS:-$ARGS}"
fi

echo "== package"
rm -rf build/update-pkg "build/$NAME.zip"
mkdir -p build/update-pkg/EFI/hakios build/update-pkg/EFI/BOOT
cp build/update-payload.bin build/update-pkg/EFI/hakios/payload.bin
cp xnuloader/loader.efi build/update-pkg/EFI/BOOT/BOOTAA64.EFI
EXTRA_NOTE=""
if [ -n "${NEXT_KERNEL:-}" ]; then
	cp build/update-payload-next.bin build/update-pkg/EFI/hakios/payload-next.bin
	EXTRA_NOTE="this package also carries EFI/hakios/payload-next.bin: the loader will boot THAT first (the newer experiment). To go back: delete payload-next.bin on the ESP, or flash the previous package."
fi

LOADER_SHA=$(sha256sum xnuloader/loader.efi | cut -d' ' -f1)
LOADER_CHANGED=keep
if [ -f build/update-loader-sha256.txt ]; then
	[ "$(cat build/update-loader-sha256.txt)" = "$LOADER_SHA" ] || LOADER_CHANGED=yes
else
	LOADER_CHANGED=yes   # first package: no record to compare against
fi
echo "$LOADER_SHA" > build/update-loader-sha256.txt

{
	echo "HakiOS gaokun3 update -- g$STAMP -- $(date -Iseconds)"
	echo "kernel stamp: 2.1.13-hakios-darwin22.5-sc8280xp-g${STAMP}  (check the boot banner or logdump for this line)"
	[ -n "$EXTRA_NOTE" ] && echo "" && echo "$EXTRA_NOTE"
	echo ""
	echo "LOADER: $LOADER_CHANGED   (keep = the loader on the stick is unchanged; do not touch BOOTAA64.EFI)"
	echo ""
	echo "sizes:"
	du -h build/update-pkg/EFI/hakios/payload.bin | awk '{print "  payload.bin  " $1}'
	if [ -n "${NEXT_KERNEL:-}" ]; then
		du -h build/update-pkg/EFI/hakios/payload-next.bin | awk '{print "  payload-next.bin  " $1}'
	fi
	echo "  BOOTAA64.EFI $(du -h xnuloader/loader.efi | cut -f1)"
	echo ""
	echo "sha256:"
	sha256sum build/update-pkg/EFI/hakios/payload.bin xnuloader/loader.efi | \
		sed -e 's#build/update-pkg/##' -e 's#^#  #'
	if [ -n "${NEXT_KERNEL:-}" ]; then
		sha256sum build/update-pkg/EFI/hakios/payload-next.bin | sed -e 's#build/update-pkg/##' -e 's#^#  #'
	fi
	echo ""
	echo "apply on the laptop (one command, from the folder holding this zip):"
	echo "  powershell -ExecutionPolicy Bypass -File apply-hakios-update.ps1"
	echo "or by hand: open the HAKIOS_ESP volume on the stick, replace EFI\\hakios\\payload.bin"
	echo "(and only if LOADER: yes, EFI\\BOOT\\BOOTAA64.EFI)."
} > build/update-pkg/manifest.txt

(cd build/update-pkg && zip -q -r -9 "../$NAME.zip" EFI manifest.txt)
OUT="${OUT_DIR:-build}"
mv "build/$NAME.zip" "$OUT/"
cp build/update-pkg/manifest.txt "$OUT/manifest-$NAME.txt"
echo "== done: $OUT/$NAME.zip"
du -h "$OUT/$NAME.zip"

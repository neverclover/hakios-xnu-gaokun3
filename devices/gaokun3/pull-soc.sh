#!/bin/bash
# devices/gaokun3/pull-soc.sh: read this device's own device tree and turn it into soc.args (what the kernel/loader needs to know
# about the SoC: the modem's blocks, the IPA, the mailboxes), compared with the SC8280XP reference.
#
# usage: devices/gaokun3/pull-soc.sh        (device on adb; pulls /sys/firmware/fdt)
#        devices/gaokun3/pull-soc.sh FILE   (use an already-pulled device tree)
set -e
cd "$(dirname "$0")/.."
FDT=${1:-device-tree.dtb}
if [ "$FDT" = device-tree.dtb ] && [ ! -f device-tree.dtb ]; then
  echo "pull-soc: pulling /sys/firmware/fdt from the device..."
  adb pull /sys/firmware/fdt device-tree.dtb || { echo "pull-soc: adb pull failed; put the DTB at devices/gaokun3/device-tree.dtb and re-run"; exit 1; }
fi
python3 "$(dirname "$0")/../../tools/soc-args.py" "$FDT" > soc.args.new || { echo "pull-soc: soc-args.py failed"; exit 1; }
# keep only the values that differ from the SC8280XP defaults; everything else stays in soc/configs/sc8280xp.config
mv soc.args.new soc.args
echo "pull-soc: soc.args written (review it: only values that differ from sc8280xp.config should be present)"

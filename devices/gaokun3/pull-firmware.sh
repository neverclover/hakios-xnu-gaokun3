#!/bin/bash
# devices/gaokun3/pull-firmware.sh: pull this device's firmware off the device over adb into fw-*/ at the top of the repo,
# the way devices/redwood/pull-firmware.sh does. All of it is the vendor's and stays out of git.
#
# The MateBook E Go runs mainline Linux with a working UEFI; its firmware (GPU, Wi-Fi, audio DSP, modem, haptics, cameras)
# lives in /lib/firmware (and /vendor/firmware_mnt on the Android side). Pull what a driver needs when that driver is ported.
#
# usage: devices/gaokun3/pull-firmware.sh
set -e
cd "$(dirname "$0")/.."
echo "pull-firmware: this device's firmware is pulled per-driver when that driver is ported (see devices/redwood/pull-firmware.sh for the template)."
echo "pull-firmware: no firmware is needed for the minimal bring-up (CONFIG_FIRMWARE='device' with no HAKIOS_* drivers beyond the SoC family's)."

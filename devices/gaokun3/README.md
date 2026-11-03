# hakios-sm7325-style README for gaokun3: everything HakiOS knows about the Huawei MateBook E Go 2023 (gaokun3).

# hakios-gaokun3: the Huawei MateBook E Go 2023 (Snapdragon 8cx Gen 3 / SC8280XP)

One HakiOS device repository: the device (`device.conf`, `configs/`, its UEFI platform, firmware scripts) and its SoC family's
layer (`soc/`: the SC8280XP family, SM8475). `tools/hakios-device.py sync` in the common repository clones it into `devices/gaokun3/`.

## The device

Everything in HakiOS that belongs to this laptop is here. The kernel, loader and user space above
know nothing about the device: addresses, interrupts, power rails and firmware come from its device
tree and its Android. A port to another Qualcomm device is another directory like this one.

| What | Where |
|---|---|
| SoC | Qualcomm Snapdragon 8cx Gen 3 (SC8280XP / SM8475): 4x Cortex-X1c + 4x Cortex-A78c, Adreno 690 |
| UEFI for the device | `uefi/`: a port of Mu-Silicium (a patch and files; `apply.sh` clones and applies, `export.sh` saves) — a laptop UEFI, not a phone's |
| U-Boot for the device | `u-boot/files/`: its device tree, `hakios_gaokun3_defconfig` and environment (docs/u-boot.md) |
| Prebuilt UEFI images | `prebuilt/`: UEFI fd, BootShim.bin, the device tree `device-tree.dtb` (the device's Android kernel's), `ramdisk`, `mkbootimg.py` |
| The device's firmware | **not in the repository**: `pull-firmware.sh` pulls it off your device over `adb` into `fw-*/` at the top |
| Tools | `tools/modem_firmware.py`, `tools/cellular_probe.py` (the modem's nodes in the device tree) |

What the kernel does for this device in particular is the drivers `drivers/*/hakios.*` (display, touch, storage, Adreno, Wi-Fi,
audio DSP, modem, haptics, cameras); the head of each file says where its numbers come from. That is the map of what to rewrite
for another device.

## Porting to another Qualcomm device: the SoC addresses

The kernel's modem and IPA drivers carry this device's register and memory addresses as defaults and take any that differ from
boot-args `soc_*` (`hakios.mpss.c`, `soc_override()`, and `hakios.ipa.c`). To bring up another device, pull its own device tree
and turn it into those arguments:

```sh
devices/<name>/pull-soc.sh      # adb pull /sys/firmware/fdt, then tools/soc-args.py -> devices/<name>/soc.args
```

`tools/soc-args.py DEVICE-TREE.dtb` reads the `qcom,ipa`, `qcom,ipcc`, the AOSS mailbox, the RPMh controller and `apps-smmu`
nodes and prints `soc_ipa_base`, `soc_gsi_base`, `soc_ipcc`, `soc_aoss`, `soc_rsc`, `soc_apps_smmu`. The run scripts append
`devices/<name>/soc.args` to the kernel command line automatically (`SOC_ARGS=` to point elsewhere). The memory addresses
(`soc_smem`, `soc_cmd_db`, `soc_mpss_mem`, `soc_imem`) come from the reserved-memory nodes; add them by hand for now.

## Running

From the top of the repository, see the main README: `./xl enter`, `./xl ramdisk`, `./xl run`,
or everything in one command: `tools/modem-cycle.sh "ARGS" SECS`, or `make phone`.

For a UEFI-native boot (nothing of the phone's own):

```sh
make gaokun3_defconfig
make kernel              # needs the toolchain (theos + xnu-deps/setup.sh)
make loader
make esp                 # build/esp: EFI/BOOT/BOOTAA64.EFI + EFI/hakios/payload.bin
```

then copy `build/esp` to a FAT32 volume (or serve it to QEMU with `QEMU_ESP=1 qemu/run.sh`).

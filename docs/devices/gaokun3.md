<!-- Made by tools/docs-gen.py from devices/gaokun3/device.conf. Do not edit: change that, then run tools/docs-gen.py. -->


# MateBook E Go 2023 (`gaokun3`)

| | |
|---|---|
| Vendor | Huawei |
| SoC | `sc8280xp` |
| Configuration | `make gaokun3_defconfig` |
| Maintainer | the HakiOS developers (README.md) |
| Repository | [`devices/gaokun3/`](https://github.com/oosexgang/hakios-xnu-kernel/tree/main/devices/gaokun3) |
| Firmware | `devices/gaokun3/pull-firmware.sh` pulls it from your own phone |

## What works

| Part | State | |
|---|---|---|
| cpu | partly | 4x Cortex-X1c (big) + 4x Cortex-A78c (little), PSCI=SMC, cpufreq=1 (CONFIG_HAKIOS_CPUFREQ): configured and built, the same boot path seen under QEMU only, not yet seen on the machine |
| gpu | no | Adreno 690 (fd690 in Linux); no HakiOS Adreno port for SC8280XP yet (gpu=1) |
| storage | no | NVMe SSD over PCIe (Linux works); no HakiOS UFS/NVMe driver for SC8280XP yet (ufs=1) |
| display | no | simple-framebuffer via UEFI GOP (0xC6200000, 1600x2560); no GOP framebuffer driver yet (display=1) |
| touch | no | Goodix/FocalTech over SPI (Linux panel-hx83121a); no HakiOS touch driver yet (touchscreen=1) |
| buttons | no | power/volume via PMIC SPMI; no HakiOS buttons driver yet (buttons=1) |
| audio | no | WCD9380 codec + audio DSP; no HakiOS ADSP/sound driver yet (adsp=1 sound=4) |
| battery | no | Huawei gaokun3-EC battery; no HakiOS battery driver yet (battery=1) |
| haptic | no | no haptics driver ported (haptic=5) |
| camera | no | no camera driver ported (cam=5) |
| torch | no | no torch driver ported (torch=N) |
| wifi | no | WCN6850/ath11k; no HakiOS Wi-Fi driver yet (wifi=1) |
| bluetooth | no | no bluetooth driver ported (bt=1) |
| cellular | no | no modem driver ported (modem=1) |
| network | no | no virtio/net driver needed on real UEFI; none yet (net=1) |
| thermal | no | SC8280XP thermal sensors read; no HakiOS thermal driver yet (thermal=1) |
| usb | no | no USB driver ported (usb=1) |
| sensors | no | no sensor driver ported (sensors=1) |

The state is what has been seen on this device, written down in `devices/gaokun3/device.conf`; a part the table does not name has not been looked at.

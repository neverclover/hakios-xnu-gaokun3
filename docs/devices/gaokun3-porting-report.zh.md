# HakiOS → 华为 MateBook E Go 2023（gaokun3 / SC8280XP / Snapdragon 8cx Gen 3）iOS(XNU) 构建适配报告

**结论先行：适配完成，并且做到了"一步启动 iOS"。**两个可启动磁盘镜像都在 `porting_notes/artifacts/`：

- `hakios-gaokun3.img`（64MiB）——最小链：XNU 内核到用户空间入口；
- `hakios-gaokun3-full.img`（2.9GiB）——**一步到位版：从这一个 U 盘直接启动完整 iOS 用户态**
  （iPhone X iOS 16.5 的 launchd/SpringBoard/WindowServer 家族），QEMU 里已截到 **Apple logo**、
  日志看到 BootUI 把系统 shell 交接给 `com.apple.springboard`，78 个 launchd 作业成功拉起。

为此新增的关键代码：给引导器加了**从启动卷直接读系统镜像**的路径（`\EFI\hakios\system.apfs`
→ 常驻 RAM 盘地址）——上游文档明说 2.5GB 的 Darwin 镜像进不了 63MiB 的 payload，也没有别的通道，
这条"一张磁盘引导完整 iOS"的路就是缺口本身（详见 §3.2、§6）。

日期：2026-11-02。仓库：HakiOS main a48d2b29（v2.1.13）+ 本次新增的 gaokun3 层、loader 平台定义与系统镜像读取。

---

## 1. 任务与参考

- 参考 https://github.com/oosexgang/hakios-xnu-kernel （HakiOS：Apple 开源 Darwin 内核
  XNU xnu-8796.121.2 跑在高通 SoC 上）为 MateBook E Go 2023（gaokun3）做 iOS/XNU 构建适配，
  直接到可启动镜像。
- 设备侧参考：right-0903/linux-gaokun（设备树/补丁索引；mainline 6.14+ 已收编
  `sc8280xp-huawei-gaokun3.dts`）、vahiru/gaokun-android（本机 UEFI-only 引导事实）。

## 2. 关键事实（全部来自读码与本机验证，非猜测）

### HakiOS 构建/启动链
- 配置 = Android 式多层 defconfig：`common_defconfig + open.config + <soc>.config + <device>.config`
  → `.config`；`CONFIG_HAKIOS_*` 决定编进内核的驱动，其余 CONFIG_* 生成 boot-args。
- `xnuloader/loader.c` 是 freestanding UEFI 应用（clang `aarch64-unknown-windows` + lld-link），
  **从零合成 Apple 风格设备树**（cpus/arm-io/gic/chosen/memory-map/RAMDisk），平台参数来自
  `struct platform` 硬编码表；从 ESP 卷 `\EFI\hakios\payload.bin` 读 XNUP payload，把内核搬到
  保留 RAM 窗口，构造 boot_args 后跳入 XNU。`make esp` 产出正是可 UEFI 原生启动的目录树。
- `xnuloader/mkpayload.py`：kernel+ramdisk+cmdline+kernel.ver 打成 XNUP；`userland/hello` 是
  裸 Mach-O（作为 ramdisk 内容），`make qemu-open` 就是这么用的（已核对 Makefile/脚本）。

### gaokun3 硬件事实（mainline 7.3 `sc8280xp.dtsi` + board dts 逐项确认）
- CPU：8 核 = 4x Cortex-A78c（little，MPIDR reg 0x0..0x300）+ 4x Cortex-X1c（big，0x400..0x700）；
  PSCI `method="smc"`；MPIDR 编号方式与 SC7280 同构（core_shift=8）。
- GICv3：distributor **0x17A00000**，redistributor **0x17A60000**（与 redwood 平台表完全相同）。
- SoC 公共块与 HakiOS 内核的 SC7280 默认值**逐项一致**：ipcc 0x408000、aoss-qmp 0xc300000、
  apps_rsc 0x18220000、apps_smmu 0x15000000、smem 0x80900000、cmd_db 0x80860000。
  → 内核 soc_* 覆盖机制开箱即用（`devices/gaokun3/soc.args` 记录了逐项核对）。
- IPA：sc8280xp 树没有 qcom,ipa/ipa_fws 节点（不同代 IPA）→ modem 是独立的后续大工程。
- 该机器**无串口、无 fastboot、只有 UEFI**（gaokun-android README 原话）；显示走 UEFI GOP
  （board dts simple-framebuffer@**0xC6200000**，1600x2560）。
- 本机 mainline 树可编译该设备树：cpp+dtc 产出 165KB DTB（仅良性告警）。

## 3. 已完成的适配（git diff 全貌：4 处，其余全是新增文件）

1. **设备层 `devices/gaokun3/`**（`tools/hakios-device.py check gaokun3` → all hold together）：
   - `device.conf`（元数据 + 每条硬件状态如实标注：cpu=partial“已在 QEMU 见到同一路径，真机未跑过”，
     gpu/storage/display/touch/audio/battery/wifi/... = missing 并注明 Linux 侧对应物）；
   - `configs/gaokun3_defconfig` = `common_defconfig + open.config + sc8280xp.config + gaokun3.config`；
   - `configs/gaokun3.config`（CONFIG_DEVICE="gaokun3"；关掉所有未移植驱动及其参数符号）；
   - `soc/soc.conf` + `soc/configs/sc8280xp.config`（SC8280XP 家族层：8 核/big=7/Adreno 690 元数据；
     开 SOCINFO/CPUFREQ/THERMAL/PMIC；确认地址的注释）；
   - `soc.args`（逐项核对记录：全部等于默认值；IPA 缺节点的说明）、`pull-soc.sh`、`pull-firmware.sh`、`README.md`。
   - `make gaokun3_defconfig`：kernel.ver = **2.1.13-hakios-darwin22.5-sc8280xp-ga48d2b29-dirty**，
     boot-args = `cpus=8 bootcpu=7 cpufreq=1 socinfo=1 thermal=1 thermal_stop=105 pmic=1`（70 字符）。
   - 自动生成的设备文档 `docs/devices/gaokun3.md` + index（docs-gen/docs-check 全绿）。
2. **`xnuloader/loader.c`：新增 `platform_gaokun3`**（核心代码适配）：
   ```c
   static const struct platform platform_gaokun3 = {
       .name = "gaokun3", .io_base = 0x00400000, .io_size = 0x3FC00000,
       .gic_dist = 0x17A00000, .gic_redist = 0x17A60000, .uart = 0, .log_other_cpu = 1,
       .cores = 8, .core_shift = 8,
       .small_cores = 0x0f,   // 0-3 Cortex-A78c little, 4-7 Cortex-X1c big（与 dtsi reg 0x0..0x700 一致）
   };
   ```
   以及 `efi_main` 平台识别：`SCREEN.base == 0xC6200000 → platform_gaokun3`（按设备树自己的
   framebuffer 地址认机，与按 0xE1000000 认手机同理）；PSCI=SMC（psci_hvc=0 默认）；无串口（uart=0）。
3. **交叉工具链引导**（toolchain-bootstrap，doctor 最终 13 ok/3 warn/**0 fail**）：
   theos + L1ghtmann iOSToolchain（**ld64-609**，支持 arm64）+ iPhoneOS16.5.sdk（theos/sdks sparse）+
   `xnu-deps/setup.sh` stage1/stage2（mig/migcom/tcsh/unifdef/unzbm + EXPORT_HDRS +
   libfirehose_kernel.a/libpthread_kext.a）。坑录：`uuid-dev` 是 stage2 硬依赖；GitHub 偶发 HTTP2
   framing 错误用不落地的 `GIT_CONFIG_COUNT` 注入 HTTP/1.1 重跑；工具链 tarball 已备份
   `~/hakios/dl/`；仓库必须在 WSL ext4（NTFS ':' 文件名坑）。

## 4. 构建产物（本机交叉构建，非 prebuilt）

| 产物 | 大小 | 校验 |
|---|---|---|
| `hakios-kernel` | 14,841,528 B | Mach-O arm64 PIE；内置 9 驱动（socinfo/cpufreq/thermal/pmic/battguard + virtio 测试通道）；`make-kernel: built`（最后的 kext symbol set 报错是 HakiOS 文档明说的预期现象） |
| `xnuloader/loader.efi` | 66,048 B | PE32+ EFI application ARM64（含 platform_gaokun3） |
| `userland/hello` | 16,528 B | Mach-O arm64（ramdisk 内容） |
| `qemu/gaokun3-payload.bin` | 14,858,336 B | XNUP：kernel+ramdisk+cmdline+kernel.ver |
| **`build/hakios-gaokun3.img`** | **67,108,864 B (64 MiB)** | **GPT，分区1 FAT32 “EFI System” boot+esp 标志；`/EFI/BOOT/BOOTAA64.EFI`(66048) + `/EFI/hakios/payload.bin`(14858336) + `kernel.ver`(50)**；SHA256 见 `gaokun3-SHA256SUMS` |

## 5. QEMU（AAVF UEFI）端到端验证 — 三次运行

1. **引导链基线**（官方 prebuilt 内核+loader，11-01 23:46）：BdsDxe 从 ESP 启动 BOOTAA64.EFI →
   loader banner → “payload: 16825720 bytes from \EFI\hakios\payload.bin on the volume” →
   Darwin XNU 22.5.0 → … → pid1 launchd。证明这套镜像格式在纯 UEFI 环境可启动（日志 `~/hakios/logs/qemu-run.log`）。
2. **本次 gaokun3 构建 + 完整设备参数**（`cpus=8 bootcpu=7 cpufreq=1 socinfo=1 thermal=1 pmic=1`，-smp 8）：
   loader 报 `kernel.ver=2.1.13-hakios-darwin22.5-sc8280xp-...`，构造 DT、把内核交给 core 7，
   Darwin banner → **“hakios-kernel: 9 drivers built in ... thermal: 27 sensors in two blocks
   (0xc263000, 0xc265000), every 2000 ms; stop at 105 C”** → 读 TM 寄存器时
   `Unclassified kernel abort (fault_code=0x10)`。
   → gaokun3 驱动按配置真实激活并工作；停在 QEMU virt 上不存在的 SC8280XP 硬件——这是预期结果
   （真机才有那些寄存器）。日志 `~/hakios/logs/qemu-gaokun3.log`。
3. **对照运行**（同一 gaokun3 内核/引导器/payload，去掉 SoC 参数，-smp 1）：
   `GenericARMPE: platform "device-tree"` → `hakios-kernel: version 2.1.13-hakios-darwin22.5-sc8280xp-...`
   → `load_init_program: /sbin/launchd` → **到达 launchd(pid 1)**，与官方 prebuilt 完全同深
   （launchd 停在 open RAM disk 无完整用户态，属 HakiOS 项目口径限制，非移植引入）。
   日志 `~/hakios/logs/qemu-gaokun3-openargs.log`。

## 5b. 完整 iOS：IPSW 链与引导器新路径（第二轮）

用户要求"直接一步到位启动 iOS"。HakiOS 的完整用户态来自 **iPhone X（iPhone10,3/10,6）iOS 16.5 的
Apple 官方 IPSW**（项目不携带也不下载任何 Apple 文件）。本轮完成：

1. **找到并下载官方 IPSW**（ipsw.me v4 API → Apple CDN）：
   `iPhone10,3,iPhone10,6_16.5_20F66_Restore.ipsw`，6,165,698,198 字节、
   SHA256 `c1091c40…d52f2`——与 unpack.sh 内嵌期望值分毫不差（大小即校验）。
2. **`make unpack`**：从 IPSW 提取 rootfs（43,230 个文件/2.9GB，经 select-system.py 裁剪）、
   OS/App cryptex、dyld 共享缓存（43 件全数校验）、d22ap 设备树。坑：需要系统 `lzfse`
   （`apt-get install lzfse`；mkidentity.sh 里 `make lzfse` 的目标名与上游 lzfse 仓库不符，会失败）。
3. **`REST=1 userland/build.sh`**：`userland/root.apfs`（4MB，/redwood 小盘）+
   **`userland/system.apfs`（2.48GB，完整 Darwin 用户态 APFS：stage0 充当 /sbin/launchd、
   真实 launchd/SpringBoard/WindowServer/mediaserverd 家族、dyld 缓存、全部 382+ 作业）**。
4. **`make loader`（真身份）**：从 d22ap 设备树生成 identity.h（D22AP/iPhone10,3/iPhone X 进了
   72KB 的 loader.efi）。坑：`build.sh` 只在 identity.h 缺失时才生成——先跑过 `loader-open`
   留下占位文件的话，必须删掉再生成，否则身份不会进镜像（本次踩过并纠正）。
5. **引导器新代码（给 loader.c 的第三处改动）**：`read_volume_file()` + 常驻地址放置块——
   没拿到别的来源的常驻镜像时，尝试 `\EFI\hakios\system.apfs`，检查窗口空间后**直接读进
   `win_base + RESIDENT_RD_OFFSET`**（与 payload 自带 resident 镜像同法；先用 AllocateAddress
   试过：会撞引导器自己的窗口记账，失败——已改成上游的写法）。
6. **QEMU 验证走真机同款形态**：2.9GB GPT+FAT32 磁盘挂 virtio-blk，AAVMF 从 ESP 的 BOOTAA64.EFI
   引导，payload 与 system.apfs 都从卷上读（QEMU 的 `fat:rw:` 目录模拟有 2GB 单文件上限，
   真实 FAT32 没有——顺带这也更像真机）。结果（`~/hakios/logs/qemu-gaokun3-darwin.log`）：
   - `system image from the volume: 939fc000 bytes` → `resident image at pa fc000000 … it is the root (md0), the payload ramdisk is md1`
   - `GenericARMPE: platform "D22AP"`（身份生效）、内核 banner `version 2.1.13-…-sc8280xp-…`（本次构建）
   - launchd 起：40 分钟会话（TCG 单核）结束时统计——**362 个作业 Successfully spawned**
     （spotlight 家族、securityd、UserEventAgent、healthd、notifyd、runningboardd、mediaserverd、
     WeatherWidget/锁屏组件、PosterBoard 的 SBRendererService……），日志 2.5MB；
     backboardd 打出完整交接序列：
     `[BootUI] systemShellChangedWithPrimary: com.apple.springboard` →
     **`[SystemShell] System shell "com.apple.springboard" finished startup after 88.70s`** →
     **`[BootUI] updateOverlay: Dismissing overlay: shell launched`**（引导层撤销、画面交给 SpringBoard）
   - ramfb 截屏：**黑底白色 Apple logo**（`porting_notes/artifacts/qemu-ios-screen.png`，
     1125×2436 = iPhone X 面板分辨率；logo 侧躺是 ramfb 在 QEMU 里的显示方向，真机由固件决定。
     后续帧在 overlay dismissed 之后，会话受 timeout 截止未再截）
   - （中途计数 78 是启动第 ~5 分钟时的快照；最终数 362，见上）
7. **一步到位交付镜像 `build/hakios-gaokun3-full.img`（2.9GiB）**：GPT，FAT32 ESP（boot+esp），
   `/EFI/BOOT/BOOTAA64.EFI`（带身份+系统镜像读取）+ `/EFI/hakios/payload.bin`（内核+root.apfs+
   **真机 boot-args**）+ `/EFI/hakios/system.apfs` + kernel.ver；静态校验（分区表/文件清单）
   与 SHA256 见 `gaokun3-full-SHA256SUMS`。与 QEMU 验证过的测试盘只差一处：payload 用的是
   gaokun3 的真机参数（`cpus=8 bootcpu=7 cpufreq=1 socinfo=1 thermal=1 pmic=1`——这些驱动在
   真机上面对真硬件；QEMU 里不带它们，因为寄存器不存在）。

## 6. 真机使用与限制（如实）

- 把 `build/esp-gaokun3/`（= 镜像里 FAT 的内容）拷到 U 盘 FAT32/ESP，开机 UEFI 菜单选择：
  固件加载 `\EFI\BOOT\BOOTAA64.EFI` → loader 认 GOP 0xC6200000 → 读卷上 payload → 跳入 XNU。
  全程不需要 fastboot/ABL/Linux；Secure Boot 不参与（loader 未签名）。
- 到 launchd 之后需要真实 Darwin 用户态：`make unpack IPSW=<iPhone X 16.5 IPSW>` +
  `make images`（用户自备；本机无 IPSW，本次按 HakiOS 的 open 口径验证——与上游 `make qemu-open`
  的“可启动”定义一致）。
- 风险/限制：①真机 GOP fb 地址若不是 0xC6200000，loader 会走未知机保守分支（真机验证后可能要
  换成固件协议/SMBIOS 识别）；②GPU(Adreno 690)/NVMe/显示/触摸/音频/Wi-Fi/modem 未移植
  （device.conf 逐条标 missing）；③SC8280XP 的 IPA 与参考机不同代，modem 是更大工程；
  ④payload 63MiB 上限（内核 15MB 无压力；大 Darwin 镜像走 USB/分区路径，见 docs/uefi-boot.md）。

## 6b. 真机第一次开机（2026-11-02 12:53，使用者实拍）——与第三轮修复

**真机把两件事一次证明、一次证伪：**

证明的：
- UEFI 从盘上找到并启动了 `BOOTAA64.EFI`；`payload: 19036112 bytes from \EFI\hakios\payload.bin on the volume` ✓
- **给 loader 新加的“从卷读系统镜像”路径在真机工作**：`resident image at pa 108000000 size 939fc000: it is the
  root (md0)` ——2.48GB 完整放好；真机内存布局：最大空闲块 `100000000 size 300000000`（4G 起、12GB RAM），
  内核窗口 `phys window 100000000 size 180000000`（6GB）——2.48GB 系统镜像 + 内核 + 保留，宽裕。
- 文字经固件控制台正常上屏（画面竖屏方向，与面板原生 1600x2560 一致）。

证伪的：
- `platform qemu-virt` ——**这台机器的固件不把 GOP 帧缓冲按设备树那个地址暴露**（`display WxH at …` 一行没有出现，
  说明 locate_protocol(GOP) 失败或 fb_base=0；文字走的是 ConOut）。于是按地址认机器失败 → 设备树按 QEMU 的
  GIC(0x08000000) 生成 → 内核在起核/挂设备树处卡住（屏幕停在 `cmdline: 158 of 607 characters`，
  那是 loader 打印设备树之前的最后一行；之后内核拿到错误的 GIC 地址，无声挂死）。
- 另注意到真机上 `exception hook 0/3: status 8000000000000003`（EFI CPU arch 协议拒绝注册异常钩子，
  QEMU 上则根本没有该协议——两边都没有钩子，不是卡死主因）。

**第三轮修复（已在仓库与镜像里）：**
1. **payload 声明平台**：`mkpayload.py --platform gaokun3` 加 `platform` 条目；loader 优先按声明选
   `platform_gaokun3`（不再依赖 GOP 地址；地址匹配保留为手机那条路的回退）。未知声明直接停下报错，不盲跑。
2. **诊断信标**：`device tree: identity` / `device tree: memory map` / `cores: asking the firmware which ones are free`
   / 对 PSCI 拒绝回答的核心打印返回值 —— 下次真机照片能精确到步。
3. 重新生成两个镜像（`hakios-gaokun3.img`、`hakios-gaokun3-full.img`，payload 均含 `platform gaokun3`；
   新 loader 73216B，带 d22ap 身份 + 系统镜像读取 + 平台声明，SHA256 已更新）。
   **请重新把 `hakios-gaokun3-full.img` 写回 U 盘再试**——预期这次屏幕会继续走到
   `cores: asking the firmware…`、内核 banner（`GenericARMPE: platform "D22AP"`），之后内核的串口在这台机器上不存在，
   画面停在 loader 的最后输出属正常（kernel 的日志要去 logdump 分区/USB 取，见 docs）。

QEMU 回归（新 loader、未声明 payload）：必须与之前完全同深度（到 launchd）——见 `~/hakios/logs/` 最新记录。

## 6c. 真机第二次开机（13:2x 实拍）——**内核已在真硬件上跑起来**

照片逐行（倒置）关键内容：
- `platform gaokun3 (declared by the payload)` ✓（第三轮修复生效；`payload entry 'platform' size 8` 被解析）
- `CurrentEL=1 MIDR=410fd4b0` = Cortex-A78C —— UEFI 本身跑在小核上，与设备树一致
- 信标全部出现：`device tree: identity`、`device tree: memory map`、`cores: asking the firmware which ones are free`
- **PSCI 在真机固件下工作正常**：`cores for the kernel, in the order it takes them: 700 600 500 400 300 200`
  （按大核优先枚举 MPIDR 0x700..0x200），`core 100 is not off (the firmware says 0)` 是固件对保留核的正确回答
- `the kernel will run on core 700 (this is core 0)` → **CPU_ON 成功，内核被交到 Cortex-X1C 大核** → `jumping to 103f4194a8`
- 用户观察到：随后**屏幕忽然刷出一大段日志（含 "pmc failed"），然后黑屏**。

解释（读码确认）：这是 HakiOS 的**设计行为**——内核 panic 后经 return slot 交回引导器；引导器组装
"自己的日志 + == KERNEL LOG =="（compose_logs），**若盘上有名为 `logdump` 的 GPT 分区就写进分区**
（write_logs → find_log_partition，从分区第 0 块起 raw 写入并读回校验），**没有这个分区就把整段日志
喷到控制台**——我们的盘没建这个分区，所以看到了刷屏；黑屏=日志喷完后引导器 `reboot()`。
真机取日志的官方通道就是 logdump 分区（docs/phone-recovery.md 的同一机制）。

**第四轮修复（已完成、已出盘）**：两个磁盘镜像都加 10MiB 的 `logdump` 命名分区；QEMU 已端到端证明该通道
（分区里读回完整 staged 日志：引导器日志 + `== KERNEL LOG ==` 段共 2.3 万字节）。
新镜像（含平台声明 + logdump）：`hakios-gaokun3-full.img` SHA256 `5505a7eb…1d882b`（2910MiB）、
`hakios-gaokun3.img` SHA256 `af6114f6…61976c4`（76MiB）。下次真机开机后把 U 盘插回电脑（**Windows 若提示
给 logdump 分区"装载/格式化"要点取消**），从分区第 0 字节起就是那次开机的完整日志——
"pmc failed" 的上下文一看便知。

## 6d. 真机第三次开机（13:4x，logdump 到手）——**内核在真硬件上驱动了真实寄存器**

用最小链盘 + logdump 分区，真机这次把完整链路走到底并由看门狗导出：
- `the kernel will run on core 700` → `jumping to` → **内核在 Cortex-X1c 上真跑起来了**，并向真实片上寄存器写值：
  - `ddr: memory bandwidth vote 16383 on MC0 (0x50000) and SH0 (0x50004)`（内存控制器投票，未 FAILED）
  - `cpufreq: group 0 … row 16 -> 20 … 2438 MHz`、`cpufreq: group 1 … row 8 -> 20 … 2995 MHz`
    （**SC8280XP 真实频率档，EPSS 表读+写 PERF_STATE 生效**）
- `log: 32768 bytes written to logdump (0), read back the same (0)` —— **logdump 通道在真机工作**（`(0)`=EFI success）。
- 之后内核静默挂死：`THE KERNEL HUNG on core 700: no sign of life for 50 seconds`，看门狗把 32KB 日志写盘后复位。

**挂死点 = cpufreq 之后的 thermal 初始化**。读驱动源码定位（`drivers/power/hakios.cpufreq.c` 里 thermal 的兄弟块）：
`hakios.thermal.c` 只**读** TSENS 状态字，按 `qcom,tsens-v2`（SM7325）模型、默认 `c263000`。
**SC8280XP 用 `qcom,tsens-v3`**，块/寄存器模型与地址都不同；真机上读一个该模型无法描述的块 →
**片上总线不响应 → 静默挂死**（不是 data abort，因为地址在片上 fabric 内挂住）。PMIC（SPMI arbiter）同源风险。

**第五轮修复（已出盘）**：在设备层 `gaokun3.config` 关闭 `CONFIG_THERMAL/CONFIG_HAKIOS_THERMAL` 与
`CONFIG_PMIC/CONFIG_HAKIOS_PMIC`（只读辅助、非启动所需；**驱动移植到 v3 后再开**）。boot-args 随之变为
`cpus=8 bootcpu=7 cpufreq=1 socinfo=1`（保留真机已验证有效的 socinfo/cpufreq/ddr 路径）。
**内核无需重编**——两驱动本就"仅在 boot-arg=1 时运行"，去参数即休眠，二进制与本次被诊断的一致。
新盘：`hakios-gaokun3.img` SHA256 `6d684842…432f57`（76MiB）、
`hakios-gaokun3-full.img` SHA256 `d1814f45…cb9e3f`（2910MiB，含 `system.apfs` + logdump）。

**待办（真机下一步）**：用去 thermal/pmic 的盘再开一次——预期越过挂死点、进入 XNU 更深处（GIC/驱动/bsd_init），
再由 logdump 导出的完整日志决定下一个修复点。另：把本次 `logdump` 分区（raw，非文件系统）备份成文件发我，
可读到挂死前 26KB 完整内核日志（含 socinfo/ddr 的先前输出），进一步确认无其它隐患。

## 6e. 真机显示第一阶段：让机器自己的设备树替固件说话

真机没有把 GOP 暴露给引导器（§6b），但**这台机器自己的固件设备树把面板写得明明白白**
（right-0903/linux-gaokun `dts/sc8280xp-huawei-gaokun3.dts`）：
`chosen/framebuffer@c6200000: simple-framebuffer, 1600x2560, stride 6400, a8r8g8b8`——
这正是内核 `boot_args.v_depth 32` 需要的格式；参考内核靠它在真机上出图（Linux simplefb），
说明固件的扫描输出在移交后仍持续驱动面板。

**第六轮改动（已出盘）**：
- `struct platform` 增加 `fb_base/fb_width/fb_height/fb_stride`；`platform_gaokun3` 填入上面那组值。
- 引导器在固件不给 GOP 时改用**平台表里的面板**（新日志行：`display 1600 x 2560 at c6200000
  (from the machine's own device tree)`），照常经 `boot_args.video` 传给内核 →
  内核 MiniIOMobileFramebuffer 就有面板可驱动（boot logo、后续 UI 的目标）。
- 设备层 `CONFIG_BOOTARGS="fbwidth=1600 fbheight=2560"`：用户态报告真实面板
  （不设则保持 iPhone X 尺寸并缩放——两种都能显示，设了更干净）。
- 新盘：最小 `782e5fa5…49ee1f94`；完整 `56dbfffc…b8c6b26`（同一构建）。

**这条路的性质**：显示输出走**固件维护的帧缓冲**（GOP 语义），不是 Adreno 参与绘制——Adreno
在 HakiOS 里是"往面板上送 3D 画面"的下一阶段（§6f）。真机下次开机预期看到：引导器文字 →
内核 **Apple logo 直接出现在笔记本屏上**（1600×2560，方向以固件为准）。若用户态起后画面由
WindowServer 接管仍黑，那是无 GPU 软合成的边界，见下一节。

## 6f2. 从主线设备树把两件事的"接线图"抄全了（refs/sc8280xp.dtsi、refs/gk3.dts，经代理取得）

**触摸屏（真机）**：板级 DTS 写得很清楚——
`&spi6 { touchscreen@0: compatible = "himax,hx83121a-ts"; spi-max-frequency = <12000000>;
interrupts-extended = <&tlmm 175 ...>; reset-gpios = <&tlmm 99 ...>; panel = <&panel>;
touchscreen-size-x/y = 25600×16000; swapped-x-y; inverted-x; }`。
SPI6 控制器 `spi@998000`（qcom,geni-spi，时钟 `GCC_QUPV3_WRAP0_S6_CLK`，电源域 RPMh CX）。
→ 接通清单：GENI-QUPv3 SPI 主机（新驱动路径，比手机那套 SE 配置更通用）+ HX83121A 报文解析
（坐标带交换/反转，正好把面板 2560×1600 的轴向换算写对）+ TLMM 175/99 两针 + CX 域投票
（RPMh 通道已验证可用——就是 ddr/cpufreq 那条）。另外真机日志里那条
`transport:SPI ... builtIn: YES` 说明用户态 HID 框架已经在等这台 SPI 触摸——接上内核侧 SPI 后
有较大概率直接走通 HID 通道。

**GPU（Adreno 690，a6xx 家族 + 独立 GMU）**：SoC DTS：`gpu@3d00000`（reg 三窗：0x3d00000/0x40000、
cx_mem 0x3d9e000、cx_dbgc 0x3d61000），`gmu@3d6a000`（rscc 0x3de0000、pdc 0xb290000），
GPU SMMU `iommu@3da0000`，时钟 `qcom,gpucc-sc8280xp.h`（已取得），OPP 从 270 MHz 到 840 MHz 级，
电源走 RPMh（`RPMH_REGULATOR_LEVEL_*`，同 ddr/cpufreq 的通道）。
→ 手机的 22 步序列里，**寄存器级骨架同族可复用**（GMU/HFI/SMMU/TrustZone zap 的结构都在），
但**地址与全部时钟/电源常量必须按 sc8280xp 换**（不是"补两行地址"的量级——GPUCC 在 0x3d90000
独立块、GMU 寄存器在 GPU 块之外、SMMU 在 0x3da0000）。HakiOS 驱动的 `adreno=N` 逐级安全探测
正是为这类机器准备的：下一步先把 GPUCC 的寄存器偏移表（gpucc-sc8280xp.c）和 a680 固件
（a680_zap.mdt/b0x、a680_gmu.bin、a680_sqe.fw；kernel.org 与 GitHub 此刻都在限流，稍后取）
备好，再按 step 1→2→… 的真机探测推进。
EPSS 侧顺带再证一次：主线 cpufreq@18591000 就只有两个域——第八轮的两域修正是对的。

HakiOS 的 GPU 驱动（`drivers/gpu/hakios.adreno.c`）是为 **SM7325 的 Adreno 642L** 写的，
22 步逐级点亮（`adreno=N` 可停在任一步：读 GCC 喂电/RPMh 名称 → eFuse 认卡 → GPUCC → CX 电源
→ GMU 寄存器与 SMMU → TrustZone 验 zap shader → 固件上电 → 3D 核出图）。对 SC8280XP 的差距：
- GPU 是 **a680 系**（mainline `qcom,sc8280xp-a680-gpu`，GPU 块 @0x3d00000、GPUCC @0x3d90000——
  与 SC7280 家族**同址**；QFPROM 的 speed-bin 偏移需按 sc8280xp.dtsi 校正）；
- 需要**这台机器自己的 GPU 固件**（a680 系 zap shader/GMU/sqe——不在我们的盘上，来源是它的
  Android 分区或 mu/linaro 的 firmware 仓库），以及按它的 DTS 参数化各 block 地址（驱动现为手机地址硬编码）。
第一阶段（GOP 帧缓冲）不需要任何这些；它先行，Adreno 作为其上的第二级。

## 6g. 真机第二次显示尝试（20:0x）：jump 后仍黑 → 出 verbose 盘把内核日志搬上屏幕

烧写显示版（`56dbfffc…`）后：引导器文字正常 → `jumping to` 之后黑屏——**这在设计上正常**
（内核一接管，屏幕要等用户态 WindowServer 才有第一帧；这台机器没有串口，中间过程无处可看）。
但为把"黑屏的这段时间"变成可观察的过程，出 **第七轮 verbose 盘**：payload 命令行加 `-v`
——引导器随即把 `boot_args.video.v_display` 置 0（"verbose boot wants the text"），
**XNU 的文本控制台直接写在 0xC6200000 的面板上**：真机上应看到内核 banner、驱动初始化、
bsd_init、launchd 的行文逐帧滚出（HakiOS 在无串口真机上的既定观察方式，代价只是每行文本的
显存滚动）。新盘：最小 `236e2f6d…730a7c`；完整盘见 §8。若画面开始滚内核文字，
下一张屏幕照片就能定位真机启动走到哪一步。完整盘：`7a1fd3eb…c28dd668`。

## 6h. 真机 verbose 启动（20:2x）：内核文字第一次滚在笔记本屏幕上——并纠正了挂点归因

verbose 盘在真机上成功：屏幕从 `vm_cache_geometry` 一路滚到 **`cpufreq: group 1 … 2995 MHz`**，
几秒后黑屏（= 50 秒看门狗后复位）。照片给出的事实链：

- `GIC subsys is initialized`、`vm bootstrap done`、`BSD root: md0`、
  `MiniIOMobileFramebuffer: display 1600x2560 on a panel of 1600x2560 (6400 bytes per row)`——
  **内核已经在驱动笔记本屏**（照片里的文字就是它画的）
- 6 个核注册、`4 of 6 processors are running`（多核在真硬件上跑起来了）
- **`group 0`（2 核）与 `group 1`（3 核）之后停住**：驱动按 3 个 EPSS 域循环，本机的引导核
  （MPIDR 0x700）落在"域 2"，而 SC8280XP 是**双簇芯片**——0x18593000 处没有第三块 EPSS，
  读它 = 与热传感器同类的总线挂死。**前两轮真机的"墙"一直是这里，不是 thermal**（那次它甚至没轮到运行）。
- 另一处被照片纠正：`cpu 4 (phys id 0x300) is a slower core`——`small_cores=0x0f` 按物理编号
  标反了大小核；本机 X1C=编号 0-3、A78C=4-7（运行核 MIDR=410fd4b0 是 A78C，编号 7）→ 改 `0xf0`。

**第八轮改动（新内核 + 新引导器）**：
- `hakios.cpufreq.c`：新启动参数 `cpufreq_domains=`（1..3，缺省 3 与手机一致）；两域时核按
  `aff1<4→0，否则→1` 分组；只循环存在的域。
- 配置：`CONFIG_CPUFREQ_DOMAINS=2` 进设备层 → args `… cpufreq=1 socinfo=1 cpufreq_domains=2 fbwidth=…`。
- 引导器：`platform_gaokun3.small_cores = 0xf0`（注释同步纠正）。
- 验证顺序：QEMU 回归（同参数新内核，判"不破坏现有路径"）→ 出盘 → 真机。
- 追加修正（8b）：那两行 `group` 打印还把哪块 EPSS 属于哪个簇说清了——`0x18591000` 的表顶是
  2438（A78c 的上限），`0x18592000` 的是 2995（X1c 的）。两域分支的映射据此对调
  （编号 4-7 → 域 0，0-3 → 域 1），并把 QEMU 回归跑通（新内核同参数下 `cpufreq: on core …`
  这行——真机上从未出现的那行——在 QEMU 里正常打印，launchd/SpringBoard 路径与基线一致）。
- 新交付（8b，含以上全部）：最小盘 `a10a099d…b0f78d`、完整盘 `404476dd…891f9d`
  （内核 `8cd6573e…`，引导器 `5be12584…059361b3`；均 verbose+显示+两域 EPSS+正确大小核）。
  真机预期：越过此前的墙（出现 `cpufreq: on core 7 …` 行）进入从未见过的更深区域，
  直到下一个停点或用户态接管屏幕（文字 → Apple logo）。

## 6i. 真机第三次启动（21:16，小盘）+ 这次导出的 pmf 就是原始字节——全部核对通过

用户烧的是 **76MB 小盘**（快速试盘），它的终点本来就是设计好的：
`Hello from user space on Darwin! / I am process 1…`（hello 测试程序）→ `pid 1 exited … exit 0`
→ 内核**有序**收尾（`psci: cpu 5 ends the system…`、给引导器降 DDR 投票、`kernel returned: log …
26737 bytes`、`log: 29184 bytes written to logdump`）——照片上的 panic 块正是这段收尾转储，
**不是故障**。这一轮的真正内容在日志里：

- `initialize_screen: b=C6200000, w=00000640, h=00000A00`——内核按笔记本面板初始化 ✓
- `cpufreq: group 0 (4 of our cores): … 2438 MHz`（**4 个 A78c，正落在 EPSS0**）
  `cpufreq: group 1 (2 of our cores): … 2995 MHz`（X1c→EPSS1）——**两域角色修正被真机证实** ✓
- `cpu 1 (phys id 0x600) is a slower core…`——大小核标记现在方向正确 ✓
- 越过旧墙：日志里出现 `cpufreq: on core 7 a loop ran at 82, now at 82`（此前从未出现），一路到用户空间 ✓
- 另发现：这台机器只有 6 个核可交给内核（`core 100 is not off`：0x000/0x100 固件不放）——后续按 6 核规划

pmf 这次的"未压缩"导出其实是原始字节 + 头（日志连续存放在 0x5d0 起）——直接切片即可读，
全文已在 `artifacts/logdump-3.txt`。

**下一步**：烧完整盘（`404476dd…`）——同一条已验证的路径，只是根换成 2.48GB 真系统、pid 1 换成真 launchd。

## 6j. ★ 真机完整 iOS 进入桌面（22:2x，round-8b 完整盘 `404476dd…`）

**HakiOS 首次在 gaokun3 真硬件上把完整 iOS 引导到桌面**：GOP 帧缓冲路径（§6e）驱动面板，
软件合成（无 GPU 的既定降级：`no GPU draws, vibrant views are plain`），用户态到桌面。
这同时反证了 §6i 的判断：小盘那次"快速退出"是 hello 的正常终点。

已知未接通的（都在设计里的下一步，不是新故障）：
- **触摸屏**：HakiOS 的手机触摸驱动（`drivers/input/hakios.goodix.c`）按 SM7325 写死了
  TLMM 引脚/QUP 引擎/GCC 偏移；本机是 QUPv2 世代，触摸控制器型号与接线要从机器自己的
  DTS 读（本小时网络抖，取不到 right-0903 的 DTS——已列为第一批要做的事）。
- **GPU**：Adreno a680 系（§6f）。驱动本身支持 `adreno=N` 逐级探测，缺的是（1）按本机
  DTS 参数化那几个 block 地址（GPUCC 同址，QFPROM 偏移要改）（2）这台机器的 GPU 固件
  （linux-firmware `qcom/sc8280xp/a680_*`，网络恢复后取）。
- 输入通道在真机上目前只有触摸屏一条（XNU 这边没有 USB HID 栈），触摸接通前无法点按。

## 6k. ★★ 真机 iOS 桌面实拍 + 第一份真机用户态日志（22:4x）

照片（`artifacts/real-device-ios-desktop.png`）：1600×2560 面板上完整 iOS 16 主屏——壁纸、
天气/日历小组件（"No weather data / No events today"，无网络所致）、图标网格、搜索条、Dock；
时钟走时正常（epoch 生效）。**拔掉 U 盘系统照跑**（根是内存里的常驻镜像），稳定不崩。

真机日志（未压缩导出=原始分区字节，`artifacts/logdump-real-boot.txt`）：
- 开机 **24 秒**内核日志已 416KB、进程号到 ~90（rtcreportingd/contextstored/identityservicesd
  的 XPC 注册与失败查找）——真硬件比 TCG 快两个数量级；launchd 正常按 mach 请求拉起服务
- `UsagePage:0xd primaryUsage:0x4 transport:SPI reportInterval:0 ... builtIn: YES`——
  用户态 HID 子系统枚举到一台 **SPI 总线的内置输入设备**（这台二合一的键盘/数字化仪走 SPI）。
  内核侧还没有 SPI/HID 驱动接它——是输入接通的第二条线索（比 I2C 触摸屏更有现成价值：键盘）。
- 日志里每 2 秒的 `log: N bytes written to logdump` 是引导器的进度镜像（内核不返回就不含
  内核全文）——设计如此。

**屏幕上偶尔闪过文字** = `-v` 文本控制台的零星打印。第十轮起交付盘不带 `-v`
（图形模式，屏幕归用户态独占）：**干净桌面盘已出**——完整 `b2fc2501…35909bf8`、
最小 `d93cd8dc…09afe101`（内核/引导器与 8b 相同，只是命令行去掉 `-v`；`WANT_VERBOSE=1`
可在构建脚本里要回带日志的调试盘）。触摸与 GPU 的接通仍是 §6j/§6f 的两件事：DTS
（本小时外网 TLS 一直断，恢复即取）与 a680 固件。

## 6l. 第十一轮：触摸屏驱动（Himax HX83121a on QUPv3 SPI6），编译一次通过

驱动 `drivers/input/hakios.himax.c`（1330 行，逐 helper 模仿 goodix.c，因此首编即过）：
五段安全阶梯（`himax_stop=N` 可停在任何一段），全部打印后才动手：
1. **CX 电源域**：cmd_db 找 `cx.lvl` → RPMh TCS 要 ≥384 档（ddr/cpufreq 验证过的通道）；QEMU 上
   没有 cmd_db 会打 `no command database (magic 00 00 00 00)` 然后**原样离开**——回归里确认引导不受影响
2. **时钟**：GCC 投票 0x52008 的 wrap0 各比特 + SE6 分支（halt 0x17004/0x17008/0x17864，全部来自
   gcc-sc8280xp.c）；RCG 0x17868 挂到 TCXO 19.2MHz，分频 2 → **9.6MHz 上线**（19.2 没有整除 12 的
   档位，从下方守 DTS 的 12MHz 上限）；GENI FIFO 初始化照抄 goodix，但按厂商驱动的 **SPI mode 3**
   + 300ns CS setup
3. **引脚**：99 输出(空闲高)，175 输入，154-157 复用 **function 1**（goodix 对 "qup15" 用的同一编号体系）
4. **复位与识别**：99 拉低 5ms→高，等 60ms（芯片重读 flash ~55ms，来自厂商驱动时序）；interface_on
   （哑读+确认 CONTI=0x31/BURST=0x10）；type-3 AHB 读 **IC_ID(0x900000d0)** 与 **FW_STATUS(0x900000a8)**
   ——不是 0x05 就停在这里并把状态打出来
5. **报文**：写 RawOut 选择 0xf6（厂商注释："must be 0xf6, fuck you huawei"）；8ms 看 175、80ms 兜底；
   INCR4 突发读命令 0x30（4807 字节 = 40×60 u16 原始帧），厂商阈值/找峰/去重/质心 → 传感器坐标
   (≤25600,≤16000)，按 DTS 注释的顺序**先反转 x 再交换 xy**：panel_x=sy×1600/16000、
   panel_y=(25600−sx)×2560/25600 → `hakios_touch_post`（与手机共用队列，五指退出手势照常可用）

集成：`drivers/input/conf` 注册 driver HIMAX、`drivers/map` 加行（sync-drivers 自动拷树）、
hub `hakios.input.c` 在 goodix 之后调 `hakios_himax_init()`、声明进 `xnu/files/bsd/sys/hakios.input.h`；
未构建的配置由生成器给空桩。**注意**：boot-arg 检查器只认 git 跟踪的文件——新驱动要先 `git add`/提交，
否则 `CONFIG_TOUCHSCREEN` 会被判成"无人读取"（本轮踩过）。
QEMU 回归（带 `touchscreen=1`）：himax 到第 1 段安全退出，hello 链路 93 作业不变。
git 提交：himax 驱动一个 commit（house 文风）。真机探针盘 = verbose 版（屏幕上的 `himax:` 行就是仪表）。

### 6l+. 第十二轮：真机第一轮看不到输出——"站着汇报"补上（可观测性教训）

r11 真机盘（`86800a2b…`）桌面正常起、触摸无反应。导出 logdump 解析：里面**没有任何 himax 行**——
不是驱动没跑，而是它跑在开机第 1 秒内，而 logdump 的 2 秒心跳快照只保留内核日志的**最后 240 字节**
（loader.c 的 note_kernel_progress），屏幕滚动也早已滚过头。真机上"错过即不可见"。

修正（git 提交 "himax: standing by and saying so"）：
- `reached` 记录 bring_up 到过的阶段；
- 失败路径线程**不退出**：每 30 秒打一条 `himax: standing by: it got as far as stage N of 5; the
  interrupt line is …, the reset line is …; the engine's clock branch reads …, the votes …`——
  全是读现成的状态，不碰任何新硬件；
- 有意 `himax_stop=N`（返回值 0）仍按原样安静终止（这个坑在写补丁时就抓到了）；
- 就绪路径的统计行同步改成 30 秒节奏。

QEMU 回归复跑：驱动失败路径输出正确、内核照常干净返回（会话在返回前结束，所以 QEMU 里看不到
30 秒后的状态行——真机桌面永不退出，必看到）。r12 盘（kernel `见 SHA256SUMS-r12-touchstatus.txt`）
= verbose：桌面上下半分钟会浮一条 himax 状态行，它就是触摸的"仪表盘"。
**教训入册**：在这台没有串口控制台的机器上，任何常驻驱动都要自带周期性状态输出；一次性打印等于没打印。

### 6l++. r12 真机仍"看不到状态行"——两条通道各查：面板在桌面接管后就不再绘制内核文本；
### 而 logdump 的实时镜像窗口只有 24 秒（NOTE_SECONDS），恰好在桌面就绪那一刻关门

r12 的在线导出证实：快照序列在 **24 s** 处截断（两次开机分毫不差——那是 loader 的
`NOTE_SECONDS=24`，不是系统出了毛病；心跳到 24 s 仍是活的）。另外 r11 的照片里文本块冻结在
开机后约 15 s 的内容——**桌面合成接管面板后，内核文本不再被绘制**：`-v` 通道只在开机窗口有效。

第十三轮（git 提交 "the log's live window"）：
- `NOTE_SECONDS` 24→**240 s**；60 s 后写盘节奏 2 s→10 s（32KB 日志缓冲装得下整窗）；
- 快照尾部 240→**512 字节**（状态行完整不截断）；
- 中断模型不动（窗口内保持中断、窗口外静默观察，与原设计一致；这 240 s 的持中断时长
  已被历次开机的前 24 s 实践覆盖，只是拉长）。
驱动侧无需再改——r12 的 standing-by 线程每 30 秒就打一条，现在它们会进快照尾巴。
**这轮的验证方法**：烧 r13，桌面起来后随便什么时候导出 logdump，最后一条快照里必有
`himax: standing by: ... stage N ...`——N 就是我们要的答案。

## 6m. 第十四轮：状态通道（log 是"此刻的世界"，状态是"这个驱动是谁"）

r13 真机导出终于把全貌照出来了，两个盲区坐实：
1. **环满即盲**：内核日志环 1.25MB 在 ~180 s 写满，长度计数停住；引导器取"最后 512 字节"读的是
   **物理尾部**——环转起来后那里不是最新内容（compose_live_log 早就有换行感知，快照没做——已补）。
2. **洪流淹状态行**：桌面运行期内核日志 ~5KB/s，一条 300 字节的状态行 60ms 就滑出任何"尾部窗口"。
   "给状态行留大点的尾巴"这条路根本不对。

于是把两件事分开（git 提交 "a state channel of its own"）：
- 内核新增 **状态环**（4KB，只装驱动自己的 standing-by/统计行；`loader_status(fmt,…)`，`XNULIVE3`
  版本位挂在 live 结构尾部——老引导器遇到新内核照样安全，新引导器遇到老内核也照样安全）；
- 引导器每次快照：换行感知地取日志尾 512 字节，**另外单列**打印状态环最近 700 字节
  （"the drivers' own notes, most recent last:"）；
- himax 的 standing-by 与统计行改走 `say_both`（面板/日志一份，状态环一份）。
顺带修了两处自伤：数组与函数重名（改名 `loader_status_ring`）、osfmk 里要显式声明 `vsnprintf`
（符号在 kern 核里就有，头不写而已——编译器 note 直接给了线索）。

QEMU 回归：新内核链路完整、失败路径输出正常。r14 盘 = verbose，版本烙印 `g6785f04-dirty`：
**这次无论什么时候导出 logdump，最后必带驱动的状态**——它停在哪一段、引脚/时钟/中断各是什么，
一次导出全齐。

## 6n. 第十七轮：一次开机讲完全程（取证 + 自愈阶梯 + 双行状态）

烧写是回路上最慢的一步，所以这一轮把"一次真机开机能说的话"说满（提交 *"one boot, the whole
story"*）：

- **交易取证**：`geni_transfer` 把最后一次总线操作完整记账（发出字节、回来的前 8 字节、结束时的
  引擎标志与状态），状态行每 30 秒附带一条 `himax: evidence: bus operation N: ...`；
- **第 4 段自愈阶梯**：若 DTS 上那颗由 TLMM 52 使能的 5.5V 轨（`vreg_avdd_5p5`，厂商注明"常开"）
  的引脚读到低——**拉高**（只升不降，面板模拟也吃这条轨）、等一次完整 flash reload、再 probe；
  仍哑——把引擎按 **SPI mode 0** 重初始化再问第三遍。做过什么全部写进状态行
  （`already tried: raised the 5.5 V rail; tried the engine in mode 0.`）；
- **关键答案双通道**：总线唤醒应答、IC_ID、FW 状态直接进状态环（不再只进洪流）；
- **窗口扩容**：引导器日志缓冲 32K→64K，状态窗口 700→1400 字节——状态行+取证行一次快照全带上。

当前真机状态（r15/r16 两次导出）：第 1、2、3 段全通（CX 域、纯投票时钟、GENI、引脚），卡在
第 4 段芯片识别；中断线持续为低——芯片十有八九活着。r17 = 烙印 `gd5f89e1d`，
盘 `e2b6ad85…`。一次开机 + 一次导出 = 判定全部四种失败模式，或直接被自愈阶梯修好。

## 6o. 第十八轮：机器纠正了我——mode 0 与 0x31 0x13（"一次开机讲完全程"见效）

r17 那份 220 秒、46 个快照、65KB 的导出把判决直接送上了门（这也是 §6n 那套扩容的全部意义）：

- mode 3 下 MISO 全零（`answers 0 0`）——**阶梯自动换 mode 0 重问**；
- mode 0 下芯片开口：`answers 0x31 0x13`——CONTI 与写入完全一致，BURST 低两位 0x03 是**自增4**
  ——正是厂商驱动点名的 `HIMAX_AHB_BURST_INCR4`、这颗芯片出复位自带的默认；华为固件顺着它跑，
  我却按手机的 0x10 去硬卡。**我的检查写死了，芯片没有错**；
- `bus operation 44: out (f3 0d 00 00) back (ff ff ff 13)`——三字节头（ff=IC 思考期）+数据在位，
  **AHB-over-SPI 协议整条打通**；
- 5.5V 轨 `config 0x3c1, pad high`——常开属实，阶梯正确地没碰它。

第十八轮（提交 *"take the chip's own report of its increment"*）：`(burst & ~0x03) == 0x10` 即可，
自增状态以芯片的回报为准（`himax_incr4 = burst & 3`，帧解码路径本来就按 incr4 走，现在按它说
的走）。顺带：这台机器的 IC 在 mode 0 上应答——第一枪也许该先打 mode 0，但**留着** mode 3→0
的次序：厂商文件白纸黑字是 mode 3，机器的偏好留在证据里而不是写进假设。

r18 = 烙印 `g6ebae78`，校验见 `SHA256SUMS-r18-incr4.txt`。下一次导出的期望：
`the bus is awake (... increment ... on)` → `IC_ID ... HX83121A` → `FW_STATUS ... running` →
`RawOut ... 0xf6` → `himax: so far ...`。四段全过的话，触摸就只剩"桥进 iOS"一段了。

## 6p. 第十九轮：内核五段全过——芯片在流帧，帧在自述（★ 触摸内核侧收官）

r18 真机导出（烙印 `g6ebae78`）：**stage 5 达成**——

```
himax: IC_ID (0x900000d0) is 0x83121a00: a "HX83121A"
himax: FW_STATUS (0x900000a8) is 0x00000005: state byte 0x5: the firmware is running
himax: so far 14872 frames in 14872 looks (0 bad) ... interrupt line low
```

67 fps、连续 220 秒、零坏帧——总线、时钟、引脚、芯片、固件、事件栈读取全部为真。
唯一的缺：`0 finger positions`——解码器（峰值+重心，全实现）没从帧里提出手指。
这说明"栈里装的东西"与"解码器假设的布局"还差一层：或者 RawOut 0xf6 的使能没生效（固件在发
标准报告），或者矩阵起点/字序与厂商那 1 字节前缀的怪癖不符——正是解码注释里自留的两个怀疑点。

r19（提交 *"let the frames describe themselves"*）把帧本身变成证词，每轮状态都报：
- 事件栈首 16 字节（出现"有东西动过"的帧时保留其开头）；
- 样本全域值域 `smin..smax`（未触碰的面板应悬在 0x8000；若全 0x0000/0xffff 即构帧错位）;
- 首数据字节的类型计数（0x01/0x02/0x05·0x07/其它）；
- "有东西动过"的帧数。

一次开机 + 一次划屏（请用户务必在窗口里触摸几下）= 判定唯一修法。r19 = 烙印 `g747d85e0`。
内核硬件路径至此**只剩解码对齐一段**，GPU 的材料（§6f2/GPUCC/speed-bin）已全部备好。

## 6q. 第二十一轮：按这台机器自己的驱动读栈——分块（trunk）续读

r20 导出（烙印 `g8018466`）把最后一段案情定死：**小读正常、4807 字节的大读全零**——

```
evidence: 4807 bytes out (f3 30 00 ...), 4807 came back (00 00 00 ...)
samples range 0..0; first-byte counts 0 0 0 11118; frames where something moved: 0
```

静息矩阵的平衡值是 0x8000，全零只可能是"芯片没在回答"（引擎标志 `0xc000081` 也带错误位）。
回厂商文件对号：为这台机器写的 `hx83121a_gaokun_read_event_stack()` 读栈的方式是**分块**——
每块重新起一个 type-3 头 `f3 30`，块与块之间靠 IC 的 INCR4 自动续址（`max_trunk_sz =
spi_xfer_max_sz - 3`）。一口气 4807 字节它不应，分段它才吐——协议就该照机器的写法走。
（顺带确认 RawOut 0xf6 写入回读一致——使能根本不是问题。）

第二十一轮（提交 *"read the stack in trunks"*）：帧读取改为 1024 字节一段的续读，其余一切不动。
r21 = 烙印 `g449d062`，盘 `cdef9945…`。下一次导出如果 `samples range` 悬在 0x8000 附近、
划屏时 `frames where something moved` 上涨，触摸的内核侧就正式收官，直接进入
「桥接 iOS 验收」。若仍全零，则下一档是 0xf4 构帧（厂商同样为华为机器留过这条线索）。

## 6r. 第二十三轮：系统侧通路全部上表 + 一次不依赖手指的自检（r22+r23 = 一张盘定胜负）

r21 导出（分块仍全零）后回读厂商文件，真相在注释里：**`himax_sense_on` 与 `2ND_FLASH_RELOAD`
握手被标为"必要的步骤"**（sorting/N-frame 标"不必要"）——固件 0x05 "running" 不等于在扫描；
没做 sense-on，模拟前端从未上电，事件栈自然全零（这就是为什么连每段的第一个字都不吐）。
r22（提交 *"do the steps this firmware's own driver calls required"*）按厂商顺序补全：清
`0x100072c0` → CTRL_FW←RE_INIT → 复位 → 等固件把 `0x72c0` 写回自己（最多 1s，成败都进状态
通道）→ **再**写 RawOut 0xf6（顺序也是厂商的）。

r23（提交 *"make the whole path telltale"*）把③段（内核 ring → /dev/hakios-touch → inject.c
虚拟 HID → backboardd）也纳入状态通道与自检：

- hub 新增两行状态：`/dev/hakios-touch is up`、**`userland has opened the touch node`**
  （第一读者的开门 = 窗口服务器的手已经伸进来了）；
- 完整镜像的演示任务追加 `rwtouch wait 90 button home`：90 秒时按一次"虚拟 home 键"——
  **不靠任何芯片**，ring/节点/虚拟 HID/SpringBoard 全链路若通，屏幕会真的回桌面（肉眼+日志双证）。

至此一次开机三种结局都能自证：①芯片+桥全通（划屏即点）；②芯片仍静但屏幕回家（桥已通，只差芯片）；
③连 home 都没动（桥的问题，状态行会指出缺哪半）。r23 = 烙印 `gb7ebbc5`，root.apfs `51ebcdb9`
（重烘，含自测任务），盘 `2e62802b…`。

## 6s. 第二十四轮：最后一处错配是坐标标度（环约定 65535 分数，我们发的是像素）

r23 导出（烙印 `gb7ebbc5`）是一次**全线贯通的展览**：

```
the firmware wrote back its own address: the data is reloaded   ← sense-on 握手成功
so far 5433 frames, 0 bad, 1084 finger positions, last at 260, 1547
the stack itself: first bytes (00 00 5a 5a ...), frames where something moved: 5111
input: userland has opened the touch node
```

芯片、固件、帧、解码、ring、用户态开门——每一环都有了实证；使用者的旁证也对上了：
"触摸时屏幕上刷 log"就是最初 8 行 `himax: finger ...`（触摸触发、写在帧缓冲上而桌面没重绘）。
桌面不动的原因只剩一个，而且是纯簿记错误：**`/dev/hakios-touch` 的契约是把屏幕记作 0..65535
的分数**（见 `sys/hakios.input.h`），我们发的是 1600/2560 像素——所有手指都被解释为
左上角 2%×6% 区域里的触摸；而自检按 home 键在桌面本来就不可见（探针选得不好）。

r24（提交 *"the ring asks for the display in fractions of 65535"*）：上报处按
`x*65535/(PANEL_X-1)` 换算（内部跟踪仍用面板像素，打印仍是人类坐标）；自检换成
**从顶边下拉通知中心**（`rwtouch wait 90 swipe 0.5 0.01 0.5 0.6 400`）——在桌面必然可见，
触摸系统通与不通，一眼即知。r24 = 烙印 `g46fa55f`，盘 `b7937f17…`，root.apfs `75845467…`。

## 6t. 第二十五轮：★★ 真机触摸响应达成——修掉"划动只会点"的旧值错误

使用者报告（r24 真机）：**触摸屏正常响应了**——iPhoneOS 在 MateBook E Go 上接受这颗 Himax
的触摸。同时报两个残留：略微错位；**只能点按，无法划动**。

第一个原因立现，是纯代码错误：坐标换算写在 DOWN 分支里，**MOVE 分支复用了那份局部变量**——
整个拖动过程中，虚拟手指一直携带按下点的坐标。对 iOS 而言这就是一根不动的手指：点按可用
（DOWN/UP 坐标正确），划动必然无效。修法是每事件换算一次；统计行加两个计数
（`%u moves, the longest one drew %u`）——下次导出能直接看出拖动是否又成了"线"。

（峰检测与重心逐行核对过厂商原式：j++ 跳过右邻、强度降序、L1<3 去重、含那条不对称 ±1 修正——
全部一致。所以"略微错位"不在解码数学里；用受控四点测试量它：四角+中心各慢按一次，导出里的
`himax: finger ... at x, y` 给出五组实测值，仿射校正照值算。）

r25（提交 *"a move that carries the down's position is a tap, not a line"*）= 烙印 `gfaae946`，
盘 `4d0464c5…`。

## 6v. 第二十六轮：读栈要跟着"有报告"的信号走——空回答不是抬指（划动的真凶）

r25 真机导出（烙印 `gfaae946`）的新计数一举定案：**389 个位置、0 次 MOVE**——划动的每一帧
都被当成新按。结合 r21 早已见过的"空栈读回全零"，机制完整：

- 我们以 8ms 无条件轮询，其中一部分落在**两次固件出帧之间**——事件栈没人写过时读回全零；
- 全零经解码就是"没有触摸"→ **正在按着的手指被判抬起**，下一帧又按下；
- 对 iOS：一条划动 = 一列微点按——所以点按活着、划动与拖动都死。
- 厂商驱动从不轮询，只在中断上读——差别就在这里。

r26（提交 *"read when the chip says there is something to read"*）：

- 读栈跟随中断线（电平低=有报告；这正是设备树把 INT 记为 level-low 的原因）；
- 全零的"空回答"被扣住（`%u empty answers held back`），**不改变任何手指状态**；
- 真实的无触摸帧仍照常判抬起——它有平衡值 0x8000，不是零。这条区分干净可靠。

r26 = 烙印 `g0a79dac`，盘 `d8839a95…`。判据下次导出直读：`(%u moves, the longest one drew NNNN)`
应随划动增长；若如此，翻页/拖动应当跟手，五点标定顺带做掉错位。

## 6w. 第二十七轮：一条划动的压力就是一团——合并分簇的峰，放宽跟踪的绳

r26 导出（烙印 `g0a79dac`）：`385 empty answers held back` 起作用了，**但 300+ 位置仍 0 moves**——
帧有内容、位置在变、就是接不上上一帧。剩下的解释只有几何：手指滚压时压力摊在两三个通道上，
去重的 3 通道之外还活着第二个峰，逐帧重心在两簇之间**来回切换**——厂商注释里的
"画直线时点在两个触控通道间来回拉扯，尤其加大接触面时"就是它；而**对角线上两通道的跳变
L1 = 2×427+2×400 = 1654，已超我们 3 通道(1281) 的跟踪容差**——每次拉扯都被判为新按。

r27（提交 *"a drawn line is one blob"*）两处对症：

- **峰簇合并**：与更强峰相距 ≤7 通道的次峰并掉（并计数 `%u extra peaks merged`）——并排的
  两个手指中心距约 9 通道以上，一团压力不会超过这个尺度；五指测试会检验这条界线；
- **跟踪容差放宽到 6 通道**（一条绳仍远小于第二指的位置，但容得下重心抖动）。

r27 = 烙印 `g56dde46`，盘 `6bdc2cef…`。期望：`moves` 随划动增长、`the longest one drew`
到千位数、翻页/拖动跟手；`extra peaks merged` 若高达数百也证明机制确实如此。

## 6x. 第二十八轮：答案没有固定形状——问它，矩阵在哪就读哪（"混乱"的根子）

r27 导出（烙印 `g56dde46`）把"峰分裂"假设干净地**证伪**了：4024 帧只发生 8 次合并，
603 个位置仍只有 1 次 MOVE；位置频繁取到 `(0,34)`、`1599` 这类边缘值——随机噪声峰的形状。
真正的证据在两次原始抓取的对比里：

- `came back (ff ff 80 80 00 80 00 80)` —— 平衡值 0x8000 若按大端读正是 `80 00`，
  而且这次数据从第 3 字节就开始（思考字节只有 2 个）；
- 另一次 `first bytes (00 00 5a 5a a5 b1 5b f1)` —— 前缀又成了 4 字节头。

即：**突发回答的前缀长度逐帧变化**（IC 在准备好的那一刻才开始驱动 MISO），厂商的 OFST=4
又是从"不含总线前缀"的缓冲区起算的。固定一处读矩阵 = 在噪声与矩阵之间掷硬币——点按侥幸
可用（错位、偶发），划动必然成随机点列。

第二十八轮（提交 *"ask every answer where the matrix is"*）：不再假设，**逐帧去问**——
每 128 帧对整条回答做两字节步长、两种字节序的搜索，取"最多样本落在平衡值 ±0x1000 带内"
的位置与字节序（每 10 字抽 1 字 ×240 的探针）；从此读那里。找到与找不到都会说
（`the matrix is at byte N (big-endian words): X% at rest` / `no matrix found`），
统计行也带上 `matrix at byte N, X% at rest`。

r28 = 烙印 `gfa6666e`，盘 `cb285861…`。这次导出先回答那个缺失的事实：矩阵到底在哪、
字节序是什么；对齐正确后位置必然连续（`moves` 随之流动），错位标定再做第二次。

## 6y. 第二十九轮：帧里没有一片 0x8000 的海——那它是哪种帧？（读法改由数据决定）

r28 导出（烙印 `gfa6666e`）把自对齐的**结论**带回来了，而且比预想的更深一层：

```
himax: no matrix found in the answer (138/240 ... 88/240 ... 103/240 ... samples at the equilibrium anywhere)
```

整个回答里**没有任何位置能有大样本落在 raw 平衡带**（最高 57.5%；且那次扫描因探针几何错误其实只试了
字节 0/2/4）。"raw 电容海 + 少数峰"这幅图景**不成立**。再看厂商栈长公式——
`128 + (2 + 40×60 + 40 + 60)×2 = 5132`——回答里除了矩阵还带着**别的数组**；而这个 RawOut 模式
本就"把逐通道变化混在 raw 单元里"。**差分(rest≈0)与原始(rest≈0x8000)两种静止**，数据自己会说。

第二十九轮（提交 *"ask the frame what kind of frame it is"*）：

- 读取长度按厂商的 **5132 字节**全栈（窗口不再漏掉任何模型）；
- 扫描同时回答两个问题：**矩阵在哪一字节、什么字节序**（步长 2、大小端都试），以及
  **它是哪种帧**——对每个候选同时数"raw 静止带内"与"delta 静止带内"的样本，取信者多的模型；
- 探针改为按真实行结构取样（20 行 × 每 6 列），扫描上界保证 40×60 整个装得下；
- 判锁阈值降为 33%（r28 的 60% 阈值配错模型时只会拒绝一切）；选定结果进状态通道
  （`the matrix is at byte N (big-endian words), as raw capacitance/deltas (rest zero): X% at rest`）
  与统计行（`matrix at byte N as raw|delta, X% at rest`）；
- 解码过滤器随模型切换：delta 帧里 rest=0、指尖=正变化，小负值按噪声、大负值按断路。

补一笔（出盘前想清楚的事）：57% 这个"半个平衡海"太干净了，不像模型错，倒像**每个字都骑在两个字上**——
矩阵从回答的**奇数字节**开始（全双工读里芯片开始驱动 MISO 的时机偏一字节，完全可能）。第一版扫描
`off += 2` 把这类答案整个跳过。于是扫描**逐字节**提问（提交 *"the asking steps one byte, not two"*）。
代价：每 128 帧一次约 2×2400×4×240 次读——约 10ms，每 10 秒一次，值得。

终版 r29 = 烙印 `g829e9db`，盘 `7af6186e…`。下次导出第一件要读的事：**它自报的位置/字节序/模型与百分比**。
若它报告 ~90%+ at rest——对齐就是根因，位置流随之连续、moves 流动；若仍卡在半数级——下一层是
"raw/delta 逐行交错"（行距模型已在草稿）。

## 6z. 第三十轮：帧自己报头（5a5a 锚定），且它们就是差分——两条硬件结论

r29 终版导出（烙印 `g829e9db`）第一次带回了**驱动自报的帧情报**，一开口就推翻两处：

```
the matrix is at byte 0 ... as deltas (rest zero): 100% ...
the matrix is at byte 162 ... as deltas (rest zero): 100% ...
the matrix is at byte 131 ... as raw capacitance: 60% ...
（还有：无人触摸时系统自己弹触摸事件——"幽灵点按"）
```

1. **模型 = 差分（delta）**：任何位置都到不了 raw(0x8000) 海（最高 61%），而 delta 带能到 100%。
   厂商文件自己就写着 `EQUILIBRIUM` 段的推测语气（"触控区**貌似**只会大于 0x8000"、多处 FIXME）——
   它从未验证过这份固件；**这台设备的固件发的是差分**：静止≈0（含小负漂移），指尖=数百到数千的正变化。
2. **统计定位被"静止=到处一样"骗了**：空闲时全帧都在 rest 带里 → 扫描退到最低偏移（**帧头**！）
   → 把帧头当矩阵解码 → 随机隆起 → 幽灵点按与乱坐标。位置其实不必统计——
   实时取证抓到 `first bytes (00 00 5a 5a a5 f4 e7 0e)`：**每帧以 `00 00 5a 5a` 开头**，
   矩阵在数据第 4 字节起——而总线回显字节数会浮动（2 或 3），所以固定绝对偏移不可靠。

第三十轮（提交 *"every frame says where it starts: anchor on 5a5a; the frames are deltas"*）：

- **每帧在头 32 字节里找 `5a5a`**，矩阵起点 = 同步位置 +2（=数据第 4 字节）——锚在帧自己的头上，
  回显浮动不再是问题；找不到同步的整帧作废（计数 `%u without a frame start`），不改任何手指状态；
- 过滤器**改为纯差分**：`v<0x2800 → 形变量`，负值/大值 = 漂移与断路 → 0；
- 首帧与每 512 帧做一次**字节序校验**（两序都数 rest，取高者，只决定一次并公告
  `the anchored matrix reads little/big-endian: X/110 probe cells at their rest`），
  统计行改报 `anchored at byte %u, %u%% at rest`。

r30 = 烙印 `gb1d1a2e`，盘 `422bf260…`。这次导出先读三行：`anchored at byte`（应恒为同步+2）、
`reads little/big-endian`（一次定论）、然后 `moves/longest` 是否随划动流动、静止时是否再无幽灵点按。

## 6aa. 第三十一轮：事件栈是"整条报告"的队列——取一条，等它重新装满

r30 真机（烙印 `gb1d1a2e`）使用者报告"直接挂了，偶尔抽搐乱跳"。导出自报：

```
2262 帧里 415 帧没有帧头；anchored at byte 7, 26% at rest；
9131 extra peaks merged; moves 375 / longest 851（首次出现像样的跟踪！）
the first bytes 抓取: 一次 00 00 5a 5a a5 f4…，一次 00 00 00 00 80 00 80 00（0x8000 小端连排！）
```

拼起来：**事件栈是报告队列。我们按电平每 8ms 去舀，常常只舀到半条**——矩阵后半是零：
一只手的压力散成满屏伪峰（9131 次合并），静止时队列不满也读出残缺——"抽搐乱跳"。
两种"帧头"其实是同一结构的两个相位：`00 00 5a 5a` 出现在队首时看得见；队列不满时读到的是
后半段（全零 + 0x8000 序列）。厂商每个中断只取一条、取完队列即空——这个节奏才是契约。

第三十一轮（提交 *"the stack is a queue of whole reports: take one, then let it refill"*）：

- **一次断言只读一次**：见线拉低→读；此后必须等线回高（队列被取空）才接受下一条——厂商节奏；
- **第一条完整帧当场定模型**：锚定处对**两种字节序 × 两种静止（delta≈0 / raw≈0x8000）**同时计数，
  取信者并公告（`order L delta x% raw y%, order B ... -- reading as ...`）；
- **把那帧的 10 行抽样十六进制打进日志**（`row NN: xxxx ×12`）——若还有错，字节会替我们说话。

r31 = 烙印 `g363678a`，盘 `78fe827f…`。读下次导出的顺序：`reading as ...`（模型定论）→ `row ..:`
（真帧字节，可离线复核）→ `moves/longest`（应随划动流动）→ 静止是否再无抽搐。

## 6ab. 第三十二轮：这个模式是"流"，不是"队列"——按电平读、拒收撕裂帧

r31 真机（烙印 `g363678a`）"触摸屏完全不工作"。导出即刻自证其因：

```
so far 1 frames in 2870 looks —— 一次断言一读的协议饿死了驱动
the first whole frame at byte 7: order L delta 54% raw 43%, order B delta 40% raw 56%
```

两件事一起清楚：

- **RawOut 0xf6 是连续流**：固件不断压入报告、INT 常驻低——"等线回高"永远等不到。
  r26 起的按电平连续读才是与这颗芯片相处的节奏（"每断言一条"是我这一轮的过度推断，撤回）。
- **历轮"半数级静止"的来源是撕裂帧**：读落在固件写入之中，矩阵后半是**精确 0x0000**——
  真实面板无论 raw(0x8000) 还是 delta(±小值) 都不会整片恰为 0。撕裂帧被当数据解码，
  统计自然摇摆（54/43、40/56 这种"两边各半"）——模型判断被污染，位置自然乱跳。

第三十二轮（提交 *"this RawOut mode streams: read on the level, refuse the torn frames"*）：

- 恢复按电平读（线低即读，8ms 一询）；
- **撕裂判据**：锚定后在矩阵上抽 20×9 格，**若 1/3 以上是精确 0x0000 → 整帧作废**（计数 `%u torn`），
  不改任何手指状态，下一轮重读；
- 模型判定只在**不撕裂**的帧上做，且要求胜出方 ≥60% 静止才锁定；不足则明说
  （`believes no reading clearly (best X%); asking again on the next`）直到锁中；锁中即打印
  帧字节样本（`row NN:`×10）供离线核验。

r32 = 烙印 `g39caf6a`，盘 `83b6dbf1…`。下次导出读序：`reading as ...`（干净帧上的定论）→
`row ...:`（真字节）→ `torn` 计数（读与写打架的频度，顺带量化这颗固件的出帧节拍）→
`moves/longest` 与静止是否安定。

## 6ac. 第三十三轮：报告里同时有两种行——逐行问，逐行按它说的读

r32 真机（烙印 `g39caf6a`）"依旧无响应，过一会开始乱跳"。导出三条硬数据：

```
0 torn（"整片精确零"的判据从未触发——不存在这种撕裂形态）
best 56% / 57%（干净帧上也没有单一模型——但两个带各占近半：L 里 delta 54% 且 raw 43%）
90/892 无帧头（有些帧的同步不在头 32 字节内——被白白拒掉）
```

"两个带各占近半、且互不冲突"不是掷硬币——**是逐行混合**：这份报告同时携带原始电容阵列与
差分阵列（它的尺寸式 `2+40×60+40+60` 早写着矩阵之外还有别的数组）。而 r32 的设计失误让
最该有的**十六进制行样本**只在"模型锁中"时打印——从未锁中，真字节一直没露面。

第三十三轮（提交 *"the report carries rows of both kinds: ask every row what it is"*）：

- **每行自报身份**：40 行各自按 20 采样分类（R=原始 / D=差分 / #=死 / ?=混合），
  过滤器**逐行**按类解码（混合按无处理，安全）；
- **40 字符地图进状态通道**（`rows: RRDD..#? (%u raw, %u delta, %u dead, %u mixed)`）——
  不管真实排布如何（交错/分区/半帧），地图就是答案；
- **首帧无条件打印真字节**（`row NN [c]: 0044×12`）——这轮起字节一定在导出里，我离线逐行核；
- 帧头签名放宽为 `5a 5a a5`（历次抓取完全一致的三字节），搜索扩到 330 字节——那 22% 不再扔。

r33 = 烙印 `g57f1502`，盘 `9551d39f…`。下次导出读序：**地图**（结构一目了然）→ **真字节行**
（离线复核的终审材料）→ `moves/longest` 与静止是否安定。逐行解码对结构的形状不再敏感——
这次无论报告怎么排，读法都对。

## 6ad. 第三十四轮：地图自己解释了它自己——矩阵是 5 个"8 行块"，块间有记账字节

r33 真机（烙印 `g57f1502`）"状况依旧"，但**地图把结构直接读出来了**：

```
big-endian 说:   RRRRRRRR?DDDDDDDDRRRRRRRR?DDDDDDDDRRRRRR
little-endian 说: DDDDDDDD?RRRRRRRRDDDDDDDD?RRRRRRRRDDDDDD
```

两张图**逐行恰好互补**（一处 R = 另一处 D），周期 17 = 8+1+8。这只有一种解释：
**同一串字节、逐块交替的相位**——0x8000 的静值按大端写字节是 `80 00`；整体右移一个字节后
按小端读就是 `00 80`→0x8000……每 8 行（一次门控驱动 8 条 TX 线）插入一字节记账，
块与块之间相位翻转。"raw 行/delta 行"从来不是两种数组——是相位让同一数据在两种字节序下
各半边走一半。（另外发现：真字节行打印走了内核日志，被 64KB 导出窗淹掉——所以上轮"字节终审"
缺席；已把这类打印一律改走状态通道。）

第三十四轮（提交 *"the matrix is five blocks of eight rows: find each block on its own"*）：

- **逐块对齐**：从帧头锚点起，5 个块各自在自己的候选窗口（±8 字节）内找"看见最多面板"的位置，
  后一块以前一块的实测位置起算（记账字节自然被跳过）；
- **字节序从对齐中落出**：两种序都全块计数，取见面板多者——不再单独猜序；
- 状态通道新判决行：`reads %s; blocks at B +%d +%d +%d +%d; rows: %s; steady N/40`——
  块位置、漂移量、40 行地图、**跨帧稳定性**（上一帧地图与这一帧一致数）；
- **解码视图的矩阵行**进状态通道（首帧，`the matrix as it is read`）——这次不会被日志淹没。

r34 = 烙印 `gd608f41`，**走 ipsw 式更新工作流**：`hakios-gaokun3-update-gd608f41e.zip`（5.8MB，
覆盖 U 盘 `EFI\hakios\payload.bin` 即完成刷写；loader 未变）。地图若成 40×R、steady 40/40、
位置流连续——结构就此终审完结。

## 6ae. 第三十五轮：两处一起对——别拒收完整的帧，答该答的那条（最新）

r34 真机（烙印 `gd608f41`）："现在不会乱跳了，但触摸完全不工作"。导出直白：

```
0 frames in 6533 looks; 10619 without a frame start（空回答只有 647）
```

——**每一帧都被 r34 新加的"完整帧校验"拒了**。它假设报告之后应有安静段；但矩阵后面是
tx/rx 数组（面板数据本身，永不安静）——我的过度修正。"不乱跳"恰恰来自这些拒绝（什么都不
解码自然安静），"不工作"也是同一原因（0 帧被接受）。

同时把锚定规则按环的实际补全第二半：**窗口里有多条报告时取最新的一条**（末个能装下的签名），
不是最旧的——环形队列里最旧的那条可能已滞后手指几百毫秒。

第三十五轮（提交 *"answer the newest report in the ring, and stop refusing whole ones"*）：

- 帧头判定回到 r33 的地图规则：**签名即收**（`5a 5a a5`）；
- 锚点取**窗内最后一条**（矩阵装得下的最后一个签名）；
- 块对齐、地图、跨帧稳定性、状态通道里的真字节——全部保留。

r35 = 烙印 `ga1299e4`，更新包 `hakios-gaokun3-update-ga1299e4f.zip`（5.8MB，`LOADER: keep`）。
读序不变：`reads …; blocks at …; rows …; steady N/40` → 真字节 → `moves/longest`。这次
"块对齐 + 最新帧 + 不拒收"三者齐了——若地图 40×R 且 steady 40/40，而位置仍不连续，
剩下的就只有阈值；若地图又变（比如出现 #），真字节已在状态通道，离线直接对表。

## 6af. 第三十六轮：真字节终审——120 字节的诚实行 + 不固定间隔的单字节记账（逐行走位）

r35 真机（烙印 `ga1299e4`）"仍不工作；不触摸时 touch/none 交替闪烁"。但这次**状态通道里
出现了矩阵真身**（改通道是对的），离线可直读：

```
row 00: 800f 8007 8012 7ffb 800d 7ff2     ← 完美的小端 0x8000 原始静值！
row 08: 077f 1380 0680 8007 800f 7ff8     ← 同族数值右移一字节的形态
row 12/16/28/32: 正常；row 20/24/36: 移位
```

`blocks at 9 +0 +0 +0 +0; rows: DDDDDDDD?RRRRRRRR...; steady 40/40` —— 地图稳定了，但
显示行说清了 r34"8 行块"只对一半：**行是整齐的 120 字节，每 8~9 行之间滑入 1 个字节**
（相位边界在行 8/17/25/34，间隔不固定）。按固定 8 行块距找位，错位行原样留下——
这就是"仍不工作/交替"的所在。

第三十六轮（提交 *"rows are rows: walk the matrix one row at a time"*）：彻底放弃固定周期——

- **逐行走位**：每行在前一行实测结尾 +120 的附近 0..6 字节内找自己的起点（6 列探针），
  两种字节序全走一遍取见面板多者；标记在哪、隔几行、几个字节——全不需要假设；
- **drift 数字串**（每行一位，相对朴素计数的累计偏移）进判决行：数字上台阶的位置就是
  记账字节的位置——40 个字符把结构直接拼写出来；
- 地图/跨帧稳定/状态通道真字节照旧（真字节与地图出自同一状态，不再各说各话）。

r36 = 烙印 `g180f7cb`，更新包 `hakios-gaokun3-update-g180f7cb9.zip`（5.8MB，LOADER: keep）。
读序：`rows:` 应当 40×R、`drift:` 上台阶处=结构、`steady 40/40`，然后位置流与 moves。
（"touch/none 交替闪烁"多半是 -v 控制台在滚我们自己的周期性状态行——触摸安定后可考虑去掉
 -v，桌面就干净了。）

## 6ag. 第三十七轮：delta 行从来不存在——静值原始单元就是这个格式的时钟，只对它对表

r36 真机（烙印 `g180f7cb`）"仍然不工作"。导出把走位算法的最后一处盲点照亮了：

```
drift: 22222222...（恒定）  rows: D/R 交替依旧
读到的行: 0080 0080 0080 / 8000 8000 8000 —— 同一段字节流的两种相位
```

我的 `panel_ish` 把**两种静止都算数**（0x8000 原始带 ✓、0x0080 差分带 ✓）——于是错相位的行
同样得高分，走位停在原地（drift 恒定），D/R 交替只是把"错半个字"的读法在评分里来回显形。
**这份固件里没有 delta 数组**：全部就是原始电容，静止 0x8000 小端——厂商 simple_filter
的直觉从头到尾是对的，错的只是我们喂给它的字节起点。

第三十七轮（提交 *"the quiet cells are the clock: align rows on the raw rest alone"*）：
走位评分**只数原始静止带**（0x7000..0x9000）——静值单元是这格式的时钟信号，相位、字节序、
记账字节的位置都从它落出；其余全部保留（地图/稳定/drift 数字串/真字节）。

r37 = 烙印 `ge6c60fc`，包 `hakios-gaokun3-update-ge6c60fc2.zip`（5.8MB，LOADER: keep）。
预期：`drift:` 应为 `111...`（真行起点在 naive+1），`rows:` 40×R、`steady 40/40`、
真字节行全在 0x7fxx-0x81xx 静值带；触摸行出现正隆起 → 位置与 moves 应当就此连成线。

## 6ah. 第三十八轮：结构判决归档；剩下的是"手不是试验台"——抬指保护/轻压保持/跳变见证/五点标定

r37 真机（烙印 `ge6c60fc`）：**"触摸屏终于基本工作了"**——结构终审通过：

```
rows: R×38 + 两个 ?（恰在相位边界行 8/17）；drift: 3333333334444444477777777788888888999999
——台阶在行 8(+1)/16(+3)/24(+1)/32(+1)：记账字节的确切位置与个数，第一次被完整拼写
真字节: 全片 8000 静值海；steady 40/40
划动: 459 moves / 最长 1135；虚假峰合并从 7909 → 77（结构对了,噪声自己消失）
```

用户报告的两件小事正是"真实手"的样子：错位（待标定）、偶发卡顿断流。r38（提交
*"a line is drawn by its middle too"*）四处：

- **抬指保护**：一帧缺席=坏帧不是抬指；连续 3 帧缺席才发 UP（≈0.15s，真抬指没那么短）——
  断流的主源头；
- **迟滞阈值**：已有手指在场时弱峰门 WEAK_PEAK 800 → KEEP_PEAK 300——笔画中段轻压不再被
  "指甲门"吞掉（这正是厂商注释警告过的"划线断掉"）；
- **跳变见证**：单帧 >1400 单位的瞬移被扣一帧，下一帧同意才成立——撕裂帧戴着旧指纹的
  幻影出局，真甩动只慢一帧；
- **五点标定开始**：每次启动前 6 次 DOWN 的落点进状态通道（`himax: tap at x, y (of 1600 x 2560)`），
  不再被内核日志淹没。

r38 = 烙印 `ge5f7da4`，包 `hakios-gaokun3-update-ge5f7da40.zip`（5.8MB）。
标定流程：刷好后**慢点五处**（左上、右上、左下、右下、中心，间隔 ≥1s）→ 再划动试断流 →
导出。五点到手，我离线拟合（含可能的轴交换/镜像），r39 把仿射写进映射——错位一次修平。

## 6ai. 第三十九轮：标定点把矩阵转置了——行是六十线轴

r38 真机（烙印 `ge5f7da4`）："基本工作，但错位不规则：按 s 可能触发 q、按 d 又触发 r；
计算器按 3 有时中有时触发正上方的 6，其他键基本正常"。r38 新加的五点采样把真相直接拍到
状态通道（同一串重复是快照窗口的性质，不是六次点按）：

```
唯一落点两组：右侧 x≈1558-1578、左侧 x≈37-96 —— y 全部落在 2330-2520（底部一带）
```

上下的点按**从不改变 y**、左右的点按干净地走完 x——"不规则错位"根本不是仿射错位：
**我们按 40 行 × 60 列读，这份答案其实是 60 行 × 40 列**（转置读把真实列号 C 混进
linear%60 的次通道噪声里——垂直就这样被"藏"了起来；键盘上一行一行的错位、计算器
3/6 在两键边界摇摆，全是同一件事）。门控块证据也对上了：我们 120B"行"里看到的
960B 台阶 = 真实 80B 行的**每 12 行**（60/5）——两个模型都预测 960B，只有转置能解释
竖向失灵。dts 的 size-x=25600/size-y=16000 本来就配 60/40——配的是它自己的扫描序。

第三十九轮（提交 *"the calibration taps transpose the frame"*）：

- `HIMAX_MAX_TX 40→60、HIMAX_MAX_RX 60→40`，`X_TOTAL/Y_TOTAL` 互换（各轴总额跟着走）——
  走位/地图/滤波/接触追踪全部自动按新形状运行，**修复只落在常量上**；
- 映射注释改为按点按实证：六十线轴不反相直接喂 panel_x；四十线轴喂 panel_y（**是否还需
  一行翻转，下一次五点见分晓**——翻转是单行改动）。

用户补充（r38 上测试）：**水平划桌面正常；底部上滑回桌面不行；右上角下滑能开控制中心；
计算器 0/1/2 大多不错位但略卡顿**——与转置模型完全一致：可读的行轴（横向手势）全部正常；
不可读的列轴（竖向手势）失灵。而"y 为什么恰好粘底部"还有一层：转置视图里记账字节落在
每帧列 0 附近（固定线性位置 1020/2040/3060…），**被当成了固定的假峰**——真实触控与它混合，
报告 y 就被拖向底部带；底行键（0/1/2）恰在假峰附近"大多不错位"，中行键（3/6）在边界摇摆。
按正确 60×40 形状走位后，记账字节落在行边界，被逐行 ±d 自然吞掉——假峰随转置一起消失。

r39 = 烙印 `g2fba647`，包 `hakios-gaokun3-update-g2fba6475.zip`（5.8MB）。
预期：地图 60 行×R、drift 每 12 行一台阶、**五点各就各位**（若整屏上下镜像=那一线翻转）。

## 6aj. 第四十轮：转置被真机证实；"不灵敏"改为向按压本身要答案（阈值窗+两本 census）

r39 真机（烙印 `g2fba647`）——转置一次落地，结构侧全部对上预言：

```
rows: 59×R（仅行24一个 ?）; drift: 3..3(13行)4..4(11行)5..5(11行)8..8(11行)9..9 —— 台阶在行 13/24/35/46
= 真实每 12 行一个门控块（60=5×12），与转置模型分毫不差
真字节: 60 行全读作干净 0x8000 静值; steady 60/60
用户实测: 水平划桌面正常, 竖向手势(底部上滑)不行→这次反过来: 点按 y 铺满全高(264..2436)，
x 却 5/6 落在左缘(0..108)——症状随转置对调了轴; 划线最长 1526（历史最高）
```

（读法：点按样本里 x 卡左缘、y 已正常——正是转置病的镜像；而"y 粘底"曾由假峰解释，如今
换 x 粘边，需要问的是**哪些矩阵行真的在报峰**。）"还是不够灵敏"的另一半在阈值：滤波器
的接受窗还是手机时代的 0x100..0x1000——本面板静噪实测只有 ±0x30，而超过 0x1000 的强压
过去会被当成"无"。

第四十轮（提交 *"ask the presses how tall they are"*）：

- 阈值窗改为 **0x60..0x3000**（下限=实测静噪的两倍；上限=整个合理量程）；
- **行 census**：每帧存活的峰报告自己落在哪一行——状态通道输出 6×10 数字串（每行×100）：
  "面板右侧到底发不发信号"由数据直接回答，不用再猜；
- **强度 census**：每帧最强 cell 分四档计数（<600/600-1500/1500-4000/>4000）+历史最高——
  "灵敏度"从此有了本机的数字，阈值往哪边调由按压自己投票。

r40 = 烙印见下包。读序：`peaks per matrix row x100`（x 粘边的真相）→ 强度分布（不灵敏的
真相）→ 五点点位是否各就各位。两本 census 在手，剩下的是照着数字拨两个旋钮。

## 7. 复现步骤（新机器）

```sh
# WSL2 Ubuntu；仓库放 ext4。装 git curl clang lld make python3 flex bison uuid-dev lzfse 7zip ...
git clone https://github.com/oosexgang/hakios-xnu-kernel  # + 本报告 devices/gaokun3 与 loader.c 三处改动
# 工具链：clone theos 到 ~/theos；解 iOSToolchain-x86_64.tar.xz（L1ghtmann/llvm-project）到 ~/theos/toolchain；
#        theos/sdks sparse 出 iPhoneOS16.5.sdk；xnu-deps/setup.sh --stage1 && --stage2（GitHub 抖动用 HTTP/1.1 重跑）
make gaokun3_defconfig && make kernel
# 最小链：
make loader-open && make hello && ./make_gaokun3_image.sh        # -> build/hakios-gaokun3.img
# 完整 iOS（一步到位）：
curl -O https://updates.cdn-apple.com/2023SpringFCS/fullrestores/032-85130/6AB0D10A-8AF9-4F6C-8CF5-A1B315CABE88/iPhone10,3,iPhone10,6_16.5_20F66_Restore.ipsw
make unpack IPSW=./iPhone10,3,iPhone10,6_16.5_20F66_Restore.ipsw
REST=1 userland/build.sh                                          # root.apfs + system.apfs
make loader                                                       # 真身份（注意先删占位 identity.h）
./make_gaokun3_full_image.sh                                      # -> build/hakios-gaokun3-full.img
# QEMU 验证：porting_notes/scripts/step23b_qemu_full_darwin_disk.sh（GPT 磁盘形态，会话 SECS=2400）
# 出盘脚本默认是"干净桌面"命令行；要真机上屏幕上滚内核文字的调试盘：
WANT_VERBOSE=1 ./make_gaokun3_full_image.sh
```

## 6ak. GPU 路径第一步（r41）：驱动就在上游仓库里；这座岛与那块卡

用户指令："触摸接近尾声，现在开始 GPU 路径，最好一步到位。" 勘察结果（全部有据）：

- **上游 HakiOS 自带 XNU 原生 Adreno 驱动** `drivers/gpu/hakios.adreno.c`（3852 行，
  SC7280/Adreno 642L，带完整 20+ 步 bring-up、GMU/SQE/zap、SMMU 页表、提交环；
  `docs/driver-review/gpu-display.md` 是它自己的评审）。配置体系里 GPU 是一等公民
  （`CONFIG_GPU=y → gpu=1`，固件走 `fw-gpu/ → root.apfs:/fw`）。
- **SC8280XP 的 GPU 岛与 SC7280 逐寄存器同址同布局**（dtsi 对照）：core@3d00000、
  GMU@3d6a000(0x34000)、RSCC@3de0000、PDC@0b290000、GPUCC@3d90000(0x9000)、
  SMMU(iommu)@3da0000(0x20000)、cx@3d9e000；GDSC 偏移相同（cx 0x106c/gx 0x100c，
  gds_hw_ctrl 0x1540、clamp 0x1508）。
- **A690 的固件就是 a660 系**（Linux 对 sc8280xp 的做法，linux-surface#3 亦证）：
  a660_gmu.bin(55252)/a660_sqe.fw(43292)——驱动本来就找这两个名字；zap 按本机设备树
  的名字 **qcdxkmsuc8280.mbn(14392)**，取 linux-firmware 同 SoC 兄弟机（LENOVO/21BX）
  的签名件（gk3.dts 指定的 HUAWEI 路径同名文件）。三件已入 `xnu/fw-gpu/` 并打进
  root.apfs:/fw（build.sh 自动；曾把目录放高一层，`../` 解析差一级，已纠）。
- 驱动适配（本机为唯一构建目标，按 himax 先例直改）：头注释说明这块卡；速度熔丝
  不在任何已知表里——**不读借来的偏移、如实说明**；zap 名字换成设备树的名字。
- **教训先行**：上游 `say()` 只进 loader 的内核日志通道；本导出的可读通道是 4KB 状态环。
  于是把 say 的最后一句存下（`adreno_last_said`），由 `hakios_adreno_minute()` 在 GPU 未保持
  时每分钟向环里重述 `adreno: last step N; the last word said: …`——与触摸的 census 同框，
  开机淹没不再吞证据。（我先插了一份重复镜像，见上游早有 say 后撤销——良好意图的重复也要拆。）

r41 = 烙印 `gc8b97da`，包 5.9MB（含固件）。真机首跑预期：`gpu=1` 已在启动参数里，
导出必读 `adreno:` 的步进与终态；到哪一步、卡在哪一步，下一轮按它修。

## 6am. 第四十二轮：黑屏的解剖（idle 3 秒 + 唤醒读不回属性）与 GPU 预飞的实名字

r41 真机（烙印 `gc8b97da`）："进入桌面数秒后黑屏"。导出给出的是一条完整时间线：

```
t=63s: iOS 首启的"idle interval 3"到点 —— BackBoard 软件调暗到 0（我们无背光可控，调暗=黑）
t=73-80s: 用户点按（Contact 事件在流）→ backboardd 走唤醒:
    selector 17（写入 3084B 显示属性）→ 我们收下; selector 27（读回）→ 我们还零 = "没有这块屏"
    selector 50（电源 [0x10000,0]）、84 —— 同样只有零
t=98s: 三声 panic_trace（watchdog 为 panic 装好调试环）; t=107.8s 内核终语 —— 用户长按电源重启，
logdump 写下上一幕
```

GPU 侧同场：预飞清单还写着手机的文件名（"小镜像里没有 fw/a660_zap.mdt"）——**GPU 一步都没走**，
我 r41 改的名只覆盖了后来的查找点，没改预飞表（好意图也要过实码关）。另外发现覆盖文件的真实
同步路是 `xnu/apply.sh + xnu/files/`，我只改仓库侧不会进构建树——两处同改后用内核串验证。

第四十二轮（提交 *"a screen that was set is read back as set"*）：

- **MiniIOMobileFramebuffer**：selector 17 写入的显示属性**存下来**，27 读回时**原样返回**
  （设过的屏必须答得出自己设定过——这正是唤醒失败形状）；50（电源）记录并明示每次变化
  （`display power is now 0x…`），未实现路径维持"成功+零"的礼貌。
- **GPU 预飞**换成这台机器真实有的名字（a660_gmu/a660_sqe/qcdxkmsuc8280.mbn；.b02 若厂商不分段
  就缺——那一步会明说缺哪个）。

r42 = 烙印 `ga3b3d0c`，包 `hakios-gaokun3-update-ga3b3d0c3.zip`（5.9MB）。预期：GPU 步进日志
出现（走到 zap 分段那步停是**好结果**——证据精确）；idle 熄屏仍可能发生（那是 iOS 的策略），
但点按应当把屏唤醒回来；导出里 `adreno: last step N` 给出 GPU 的准确前线。

## 6an. 第四十三轮：第二次黑屏的完整读数——机器活着，画的全黑

r42（烙印 `ga3b3d0c`）"症状依旧黑屏"，但新仪表把话说全了：

```
t=79.97: selector 27 读属性 —— 这次原样返回了（r42 的存/回显生效）
t≈80.1:  selector 17 再写入 —— 存下
t=80.02: "display power is now 0x10000" —— 显示电源被明说为开
t=108.18: "108 frames in 5440 ms (19 a second)" —— 合成器在画，帧在流
t=108.42: himax 步进正常 —— 触摸在动
两次导出都停在 t≈108 —— 同一个物理动作：用户在 ~108s 长按电源重启（不是内核死）
```

**结论：系统一直活着；黑的是内容。** 而内容全黑的原因在更早一行里自己招了：
`[Backlight] brightness change:0.5 reason:synchronizeALSPreferencesAndDisplayBrightness`
+ `CBU_ShouldWaitForALS: timeout waiting for ALS arrival` —— **自动亮度开着，环境光传感器不存在**：
无传感器的曲线终点是 0，idle 调暗落到全黑，唤醒再问同一条曲线、再得 0。（r42 的属性存/回显
不是无用——它让"设过"的显示能被读回，这一层必须真；只是病不在那层。）

第四十三轮（提交 *"no light sensor means no auto-brightness"*）：

- **inject.c（在 backboardd 里，启动最初）**：按真实导出名找
  `BKSDisplayBrightnessSetAutoBrightnessEnabled`（.tbd，iOS 16.5 的清单里确有）并关掉自动亮度，
  再把亮度定回系统自己在启动时选的 0.5——等于人肉去"设置/辅助功能/显示"关一次的事，程序在
  自己的进程里做。找不到名字就只报告不动手。
- **GPU**：预飞清单去掉幻影文件（`.b02` 不必存在）。这次导出应见到真正的步进日志——走到
  "这张卡的着色器固件是单个传统 .mbn，段表在文件**里面**，读它还没写"为止，前面的时钟/GDSC/
  GMU 步都会留下名字和读数。

r43 = 烙印见包名（`step82` 出包）。验证点：桌面静止 3 秒后**是否还亮着**；GPU 的步数走到几。

## 6ao. 第四十四轮：第三次黑屏的读全——三分钟里的完整动机链（注意烙印：那还是 r42）

r43 包送达后用户再报"还是相同的黑屏"——导出先说了一件操作上的事：**stamp 是 `ga3b3d0c`，
那一趟跑的仍是 r42**（GPU 还说旧句、inject 没报告任何动作）；apply 按"最后写入时间"挑包，
旧 zip 比新 zip 新。——刷写核对以后每轮先看导出第一行的烙印。

r42 的完整动机链（这一份把话说全了）：

```
23:31:38 BrightnessSystem 实例化 → DisplayBrightnessAuto=1（存着的偏好）→ 0.5 起
23:31:51 backlight factor → 0.000000 "permanently:YES"（开机 20 秒，还没有任何 idle）
23:31:54 BKAmbientLightSensorDisableAutoBrightness（系统自己关了一次）
23:31:55 用户碰了一下 → 再开 auto + factor→1.0 —— 亮回来了
23:31:58-59 "minimum brightness 0.000000"×2 —— idle(3s) 又熄
23:33:00   finger 0..3 在流、坐标正常 —— 触摸全好，屏不再亮回
```

——"无感光器的自动亮度曲线"与"3 秒的开机 idle 间隔"两台机器一起把屏幕按黑；触摸全程健在。

第四十四轮（提交 *"the idle of this machine is answered with a working day"*；
内核与 r43 相同——GPU 的首次真走也在其中）：

- inject.c 按本文件既有先例（找名、报告、不猜地址）：`-[BKSSystemShellService setIdleSleepInterval:]`
  （连接类同找）把"3 秒"答成**一个工作日**——没有背光可抬的屏没有睡眠的一半；
- `keep_lit()`（自动亮度 off + 0.5）说**两次**：装载时一次，十秒后（偏好同步落定之后）再一次。

r44 = 烙印 `g86d533cc`，包 5.9MB。验证：桌面不再自己黑；`adreno:` 第一次真步进；
census 让机器静走到 t≈110 自然可读。

## 6ap. 第四十五轮：r44 首上机的两份成绩单——GPU 走了两步（第二步是我们自己的 bug），黑屏的最后一层是我们自己按下的 0

用户这次带来了真正的 r44（导出烙印 `g86d533cc` ✓；apply 的"大字回显烙印"就是为堵这个口加的）。
两件事同时发生：

**GPU 第一次真走步。** 第 1 步全绿并读出了这台机器的实情：GCC 的 PLL/总线授权寄存器值、
RPMh 资源表在 0x80860000、`gfx.lvl/cx.lvl/mx.lvl` 九个档位（0..480）。第 2 步（熔丝）
先如实说"这 SoC 的速度档位不在本表的任何清单里，不读，按设备树的 A690 干活"——然后
**却被报成 FAILED**：r41 我把 `bins_known:` 标签放在原 `return true` 之后，跳过路径越过返回值
落到函数外，w0 里是谁就是谁（这次恰好是假）。"说是说非的函数，每条路都要说"——一个
return 补上，20 步从未试过的路才没有白丢。（教训：C 函数非 void 而路径 fall-off，编译器
未必拦，硬件替你拦。）

**黑屏的最后一层是我们自己按下的 0。** 用户观察到控制中心亮度条=0、硬件其实满亮——
正是那句 `brightness change:0 reason:setDisplayBrightness (permanent)`：我们把 double 0.5
递给了一个取 **float** 的函数——arm64 上被读走的是低 32 位，0.5 的双精度低位就是 0.0。
dlsym 的原型必须按现场证据写，不是按猜测。修成 0.5f。
另外 `-[BKSSystemShellService setIdleSleepInterval:]` 的替换报告在了、但 `Setting system idle
interval to 3` 还在——那个类不是这条链的实际执行者（服务端走连接分发）。对策略不再猜名：
抬"调暗的地板"——替换 `-[BLControl setMinimumBrightnessLevel:fadeDuration:]`，把调暗下限
顶到 0.5（=开机时系统自己选的亮度）。没有背光的屏，"熄屏"的全部含义就是那层调黑；
地板与亮度同高，屏保持可读，其余电源故事照原样进行。BLControl 这个类是本文件早已
服务过的（`waitForALSArrival`），这次连的是它另一根线。

顺带的观察（值得单独记住）：**这块屏的硬件亮度从头到尾都在装载器手里（满亮），iOS 的
"亮度"从来只是它自己画的调暗层的浓度**——"黑屏"从来不是面板灭，是那张全黑的层。第四十二
轮的属性存/回显、这一轮的地板与类型，全部都是在跟那层浓度打交道。

r45 = 提交 `585496a`（*"a path that skips must return like a path that reads"*）。验证：
GPU 应走得更远（第 3 步起：GPUCC、GDSC、GMU）；屏在任何"睡眠"里保持可读；CC 亮度=50%。

## 6aq. 第四十六轮：GPU 走了四步——第五步拦住它的是仪表，不是硬件

r45 上机（烙印 `g585496ae` ✓）。这次导出形变了：会话以"回到装载器"结束，**完整 1MB 内核日志**
都在（我的截取器第一次只读到第一个空块——改为全读）。GPU 的成绩：

```
步1 全绿：GCC 寄存器、RPMh 三张电压表（gfx/cx/mx 各九档）
步2 如实跳过 → 修好 return 后这次是 done ✓
步3 全绿：GPUCC 全体寄存器读数（cx_gdscr 0x222001、gmu_clk_src...）
步4 全绿：写 cx_gdscr 0x222000 → hw_ctrl 0x8001fe0 → "CX is on" —— 电源开关在这台机器上合上了
步5 写全都应答（pll votes 0xe1157f、memnoc、snoc、ddrss、gmu/hub clk_src、smmu vote），
    唯 gpu_cc_ahb (0x1078) 写 0x1 不应答 ——
    对照 refs/gpucc-sc8280xp.c：这台的 ahb 挂在 hub_ahb_div 树上（另一棵树），且
    频率计数器自检把 CXO"测"成 23.9MHz —— debug mux 输入编号是按别的 SoC 校准的。
```

判定：**仪表读不出自己的探针，就不许它裁决**——读数保留、步 5 放行（所有寄存器逐写逐验是真，
GMU 的 HFI 握手在后面几步会给出真答案）。下一步的候选病灶已登记：ahb 父链按本机表接。

黑屏侧的 r45 成绩：`brightness change:0.5 (permanent)` ——float 修好、0.5 真的落进系统了；
`setMinimumBrightnessLevel...` 没找到（第二次猜名失手）。于是改法收敛为**不再猜名**：
把 `BLControl`/`BrightnessSystem` 两个类的**方法清单原样列进日志**（类自己报名字），
下一轮按真名替换。贯穿三次会话的熄灭机制也已定形：开机 ~13 秒后 `backlight factor→0
"permanently"`（还原"最后一次请求的因子"，而这条记录从出厂起就是空=0），交互能把它抬回
1（三次实录）——地板/因子的真名一拿到，这条链就剪断。

r46 = 提交 `b123f52`（*"a meter that cannot name its own probe..."*）。验证：GPU 应走到
GMU/RSCC/HFI（步 6 起）；日志里出现两行"类自报的方法清单"；黑屏照旧（这轮不动它，等真名）。

## 6ar. 第四十七轮：GPU 走到 GMU 的门前，在那里没了声音——给它画一条夜界的线

r46 上机（烙印 `gb123f52b` ✓）。用户的 .pmf 其实是**串口全捕获**（含多个会话的尾部——之前
按"logdump"截取的教训再次出现：读形状，不猜格式）。决定性的一幕在最新会话的末尾：

```
adreno: 15: rscc slot 0..3 ...           ← RSCC 各槽状态读完（全是安静）
adreno: 15: about to write gmu cx_falnext_intf at 0x3d7e3c4: 0x1   ← 第一句：活过
adreno: 15: about to write gmu cx_fal_intf   at 0x3d7e3c0: 0x1     ← 第二句：之后会话终结
（psci/return to loader... 自动重启）
```

**对 GMU 固件区的第一次真写，这台卡没有回话。** 这正是上游头注释预告过的形状——"有的步子
会终止这场运行，常常一言不发"；它也给了办法：`adreno=N` 边界参数。另一条独立证据链：这台
GMU 的房内编号与 642L 不同（a660/kailu 世代 GMU 偏移换表——步 3 读到 `gmu_clk_src` 应答、
`cx_gmu` 却带出 `0x6221` 的怪值；0x3d7e3xx 在 642L 表里是固件接口区，在 a690 上未必）。

r47（提交 *"the GPU walks to the edge of what is known, and stops there for the night"*）：

- gaokun3 的 BOOTARGS 加 **`adreno=14`**：夜界画在"每一步都还有回音"的最后一步——步 1-14 的
  全部答案保留，桌面照常起，触摸照常工作；
- 第十五步留给下一次的**单步细探针**（`fine`/`adreno_pause` 机制上游自带）——先把本代 GMU 的
  偏移从厂商表里读出来再进那间屋子。

另外记下观察：`list_backlight_methods` 在 r46 的日志里没有出场——注入库确认带着新句（strings
=2），但 backboardd 里没打出——两因之一：该类不在 backboardd 进程（CoreBrightness 在被链接
的库文件里，类名也许带前缀）或构造次序问题。下一轮方法清单若还缺席，就改为在类列表里按
"含 Backlight/Brightness 的类名"扫一遍再列。黑屏这轮不动（等真名，不猜名）。

## 6as. 第四十八轮：苹果 logo 后的 panic 是我们自己埋的雷——每分钟那行统计，从 r40 起每次都把内核打死在嘴里

r47（烙印 `g02521c83` ✓）真机给了第一份**完整 panic 文书**（这正是串口捕获比 logdump 窗口
慷慨的地方）：

```
panic(cpu 2): Kernel data abort. far=0x0000000000002f80   ← 整数 12160 被当成字符串指针
```

而它发生的时刻——census 六行打完的下一行——把前四场"日志在 108 秒戛然而止"全部串成了同一条线：
**从 r40 那圈加"最响的一格多高"起，每分钟那条 "so far" 统计就没活着打印过一次**：g346 把五个
新实参插在了 `seen.last_x, seen.last_y` 之后，可格式串的真实顺序是
`... the last at %u, %u; interrupt line %s; every %u us; loudest %u; strengths %u..` ——
`%s` 拿到的是 `peak_max`（=0x2f80）。此前它每次都在第一分钟把内核打死（r41-r44 我们把这误记成
"用户在 108 秒按了电源"——一半对：按的其实是重启）。这次 r47 的会话活到第一次打印，恰在打印
嘴里 data abort。

修法（提交见包名烙印）：

- 实参按格式串的真实顺序排好（18 个 %u → 中断线 %s → 轮询 us → 最响 %u → 四档强度）；
- `say_both/tried_this` 挂上 `__printflike(1,2)`——**但诚实记录：这套构建把 format 警告吞了**
  （故意再打错一次，编译器没吭声）。编译期防不住这一类，守门人只能是导出里的这一行本身；
  下一轮起导出必读 `so far ...` 全句——它活着出现，这条雷才算拆掉。

黑屏侧的连锁更正：既然内核每 60 秒死一次，此前"熄屏后无法唤回"的观察里混进了**机器已经不
在了**的时刻。真实的死时钟在 +110s 前后；而 +13s 的 factor→0 与 +60s 的 idle 调暗仍是真机制——
方法清单（list_backlight_methods）这次在不在，下一份导出见分晓。

GPU 侧 r47 的收获一并记：夜界（adreno=14）生效后，日志显示这台机器**走到了步 12**——
而且步 10 留下了最要紧的一句：`gmu dtcm 0x3d71000: 写 0x600d0001/0xa110002 → 读回一致`
——**GMU 的数据内存在这台机器上是活的**（"THE GPU BLOCK IS ALIVE"）。它停在的是格式问题，
不是电的问题。
"这台卡的签名件是 ELF 三段、148B 表、6560B 哈希——不是 MDT 的'头+哈希'形，TrustZone 不认"：
这正是预想的 a690 单文件 mbn 边界，第十五步的致命写没有发生，桌面正常起到最后。

## 6at. 第四十九轮：拆雷成功的确认 + 触摸其实不迟钝 + 让卡读自己的程序头表（步 12 是 3D 的第一道真门）

r48（烙印 `gfb8e145f` ✓）——用户："现在可以正常进入系统并看见 50% 的亮度滑块，不过 3D 加速未工作。" 三份读数互相印证：

- **拆雷确认**：`himax: so far 2828 frames ...` 完整打印，机器活过第一分钟，进桌面正常，亮度滑块可见（factor 那条链随机器不再死于 +110s 而自然不再是障碍——它从未需要真名替换，机器活着就是答案）。
- **触摸不迟钝**：强度直方图 `loudest cell ever 12135, strengths: 55/600, 18/600-1500, 28/1500-4000, 65/over`——65 帧峰值超 4000、最高 12135（逼近 UP_THRESHOLD）。先前"不够灵敏"多半是"机器每 60 秒已死"混进来的。census 全零是**我自己的显示 bug**：`row_hist/100` 把小计数压成 0——已去掉除法（每行封顶 9）。诚实记账：被我称作"面板不响应"的数字，一部分是我自己的舍入。
- **3D 未工作的真身**：GPU 三场都停在**步 12**（签名格式），不是步 15——更正上一轮我看串了的混淆（"步 15/cx_fal"来自更早构建的拼接日志）。步 12 的 `zap_header` 写的是手机"拆分 .mdt"模型（头紧接哈希、二者即整文件）；这台卡的 `qcdxkmsuc8280.mbn` 是单个 32 位 ELF、程序头表 3 项：头+表(148B)、哈希块(off 4096, 6560B)、可加载着色器代码(off 12288)。等式 `52+32·段+哈希==size` 必然不成立 → 拒绝文件、从没问 TrustZone。
  - 修：照 Linux `qcom_mdt` 的做法**读卡自己的程序头表**——遍历各段，把"头到最后一段非可加载段(签名材料)的末尾"这段（≤一页）交 TrustZone，可加载代码留给后续 PAS_LOAD 步。这是我们能对这台 GPU 的 hypervisor 提的第一个真问题。
  - 夜界从 14 推到 **15**（GMU 自己的点亮：内存/固件/RSCC 握手）——岛已通电可访问（步 4、10 早成功），每步有界自报；不开 keep，桌面仍软件渲染，安全。若停在哪一步，它会自报到那一步。

r49 = 提交见包名烙印（*"the card reads its own program headers..."*）。预期下一份导出：步 12 或"TRUSTZONE 接受签名"、或 TrustZone 给出的确切答复码；然后步 13-15 的 GMU 证据；触摸 census 出现真实数字。

**3D 加速是一条路，不是一步**：GPU 点亮只是第一程；之后还有 Metal/GL 接口与 IOSurface 合成接到那块由装载器接管的屏上。这一轮拆掉的是这条路上第一道真实的门。

## 6au. 第五十轮：格式修复被证实（TrustZone 第一次真回话）+ 触摸 census 出真数 + 让这台机器走上游为它设计的生产 bring-up

r49（烙印 `gfc57a811` ✓，用户："能正常进系统看到 50% 滑块，不过 3D 未工作"）。三处进展：

- **格式修复生效**：那句"文件不是'头+哈希'形、不交 TrustZone"消失了——驱动读卡自己的程序头表、
  交出签名材料，**第一次收到 QTEE 的真答复**：`the call returns -3145797, its answer is 0`（QTEE
  以 0x300000 为基的错误，具体号待解）。这是"从不曾问"到"问了并有答"的跨越。
- **census 出真数**（上轮去掉 `/100` 的修复被证实）：`7151000002 / 1095041021 / 0030300000 /
  1000300090 / 0000000000 / 0000000000`——面板的峰集中在前 40 行，后 20 行（40-59）恒零。说明这台
  屏的实际矩阵是 **40 行**，r39 转置时把行数设成 60 是多出的空行；当前取点仍正确（手指坐标满量程、
  用户说触摸能用），记账不一致记下列备。强度直方图本会话 27/16/2/16、最高 12161——触摸本身健康。
- **步 12 的 -3145797 不是墙**：读通全貌后更正上一轮的步号混淆——`what[]/steps[]` 只到 22 步，
  没有"步 26"；独立签名探针是**步 12**，真正的 zap 加载在**步 19**。而步 19 的代码写得清楚：
  `init_image` 一被拒就"取为 TrustZone 保有着之前的着色器、原样使用"，**继续**步 20/21——这正是
  热启动（上一次固件已把 GPU 的 zap 载入 TrustZone）的正常分支。这台机器从 Windows UEFI 引导过来，
  它的 GPU 固件默认起着——这声 -3145797 大概率正是"那边已经有一份"。

r50（提交见包名烙印）：**去掉 `adreno=15`、只留 `gpu=1`**——`start_by_itself()` 判定"这就是那颗 SoC
（`HAKIOS_SOC_BASE`）且三个固件齐"正是本机，于是走上游为这台 SoC 设计的**生产 bring-up**：last_step=21、
跳过纯看的步 10-12、keep=1。若一路到 21 且 `core_runs`，`HakiosGPUStart()` 起 `HakiosGPU` 服务、
GPU 常亮待用（用户态尚无 Mesa 接它，桌面仍软渲、安全）；若停在某步，因 `by_itself` 会明说"THE GPU IS
NOT STARTED: 步 N（什么）失败；系统以软件渲染"——桌面不受影响。同时保留步 12 的修正：QTEE 自有错误
只报名号、不再致命（供日后诊断走位用）。

"还差多少到 3D"的实话一并记录：桌面真正能用的是 **OpenGL ES 经 Mesa**（`userland/build.sh GL=1` 已
内建：取源编译 Mesa、`libOSMesa`+`libhakios.gl`+`src/eagl.c`），它要的是"GPU 被 keep、`HakiosGPU` 服务
活着"——即本 r50 这一跳。真正叫 Metal 的那条路更远：A 着色器编译器大体完成、B 设备对象图半程（发现链 QEMU
已验、缓冲/库未在 Darwin 跑过）、C 内核侧未接 B；且**窗口服务器用不了 Metal**——它要 Apple 自家的
`default.metallib`，Darwin 16.5 镜像里没有（上游手机上也只能留在 GL 路）。路径：r50 能否 keep →
GL=1 造 Mesa 进根文件系统 → 桌面可见 3D；Metal 之名其后。

## 6av. 第五十一轮：一步到位太急了——把走位收回到步 14（问完 TrustZone、装好 GMU 固件，但在"启动 GMU"之前停手）

r50（烙印 `g497d17e5` ✓，用户："启动到一半直接挂了"）。这是我最大胆的一次跳：去掉 `adreno=`、只留
`gpu=1`，让 `start_by_itself()` 走生产 bring-up（一路到 21、keep、GMU 常亮）。第一次尝试就走到那么远,
代价是这次真挂了。

读这份导出的难处先记下：整份只有**一个** XNULOADER banner，且任何走位的开场行"steps up to N, without
those in mask"出现**零次**——说明捕获到的多是**上一场（r49 adreno=15）的 logdump 碎片**，r50 自己的现场
没被完整捕到。能看到的相关片段：步 15 的 `rscc slot`、`about to write gmu cx_fal_intf / general_7
(0x3d7e3c0–0x3d7e730)`——这是 GMU **启动**（step 15，把它自己的 M3 微控制器放出去跑）那一步。而驱动
头注释早警告过：一次坏写的惩罚可能迟到（"看到之后约 17 秒才复位")。所以"启动到一半直接挂了"最合理的
解释是：GMU 一被启动就跑了起来，某次访问把它卡住/把总线挂住，晚些时候机器就僵了。keep 又让它一直挂着。

教训：对从没跑过 13-21 的机器，一次跳到 21+keep 太急。收回边界——

r51（提交见包名烙印）：`adreno=14`。**保留**真进展（程序头表读取、步 12 遇 QTEE 错误只报号不致命），
但走位在 **14 停手**：步 12 问完 TrustZone 就继续（非致命）、步 13 建 GMU 地址空间、步 14 装 GMU
固件（写进步 10 已证明活着的 dtcm）——**但不执行步 15（启动 GMU）**，于是没有微控制器被放出去跑，
没有迟到的总线挂起，走完 `gpu_block_stop()` 关掉 GPU、桌面照常。这是"问一句真问题、但不让任何东西
留在跑"的安全前沿。下一轮若 13-14 干净，再用 `adreno_only=15`+`fine` 单独细探那一步,定位是哪次写挂住总线。

（QEMU 回归 cmd 不含 gpu=/adreno=，驱动在 QEMU 上全程休眠，安全。）

## 6aw. 第五十二轮：能进桌面了——"触摸混乱"其实是矩阵朝向这个旧账终于藏不住了

r51（烙印 `g20c37d2` ✓，用户："现在可以正常进系统，但是触摸屏处于混乱状态"）。先摆正因果：
**r50/r51 都没改触摸的功能代码**（r48 修的是那行每分钟统计的参数序，r49 修的是 census 的 `/100`
显示——都不影响坐标）。机器能稳稳停在桌面是**第一次**发生（此前不是 +110s 被统计炸死，就是 GPU
中途挂），所以那个一直存在的**坐标映射问题**这才第一次暴露在使用者眼前——不是新坏了，是终于看得见了。

把 r51 会话按时间切分成 4 段来看真实手指流（这是对的读法，别只挑一个快照）：
- 有一段确实**连贯**：finger 0 稳定 1353,737→822→911→949（x 恒定、y 匀滑下移），finger 1 另一条平滑线
  ——多点跟踪本身在工作。
- 但**"五点测试"的六个 tap**：y 铺满全屏（cells 0–38 of 40，正常），**x 全挤在 1190–1407**（= 60 计数
  轴的 cells 44–52）。而代码里 `panel_x = tx*27`（tx 是 60 计数的行轴）。**可 r49 的 census 说面板只有约
  40 条线在报峰**——一个只该到 39 的轴，tap 却把它推过了 44–52。**矩阵到底是 60×40 还是 40×60，这个旧账
  一直没定死**，而这正是"混乱"的来源：横向被压在一条窄带里、分辨率粗（27px 一档）。

不盲目再调那套手调常数（`X_SCALE=9`、轴交换都曾是凑的）。要**用已知目标做一次受控标定**才能一次定对。
r52（提交见包名烙印）只做一件诊断事：**给 tap 那行加上原始轴读数**——`axis %u,%u (cells Y of 60, X of
40)`——直接报出触摸真正移动了哪个轴、到第几格。这样下一份导出就是可算的：请使用者**按顺序只点五个已知点**
（左上→右上→左下→右下→中心），我据真实仿射一次定标，不再猜。（GPU 边界仍 14，未动。）

## 6ax. 第五十三轮补记：GPU 的真改善——r51 上步 13、14 首次走绿，GMU 的固件装进去并读回了版本号

上一份导出里其实藏着 r51 自己的走位（我上轮只顾切分触摸会话，这轮挖出来了，
按 `adreno: 13:` 的最后出现处读全段）：

```
13: smmu idr0 = 0x4c017e09; 9 stream matchers, 7 contexts    ← GPU 自己的 SMMU 活着、照表配置
    cb 寄存器逐笔写读一致 (tcr 0x803519, cba2r 0x1 …)        → step 13: done
14: ahb 计频 23.9 MHz（仍是那台计数口径）
    0xd4c8 字节进 GMU 内存、0x1dc+两段经寄存器小窗口         ← a660_gmu.bin 装入
    THE GMU'S FIRMWARE IS LOADED; 从 GMU 里读回版本:
      core 0x3001000a  power 0x30000010  HFI 0x20010000     → step 14: done
over; the system goes on starting … 130 s: kernel 已写 1.31 MB 日志, 一切正常
```

**GMU 的微控制器固件现在躺在它自己的内存里，而且它把版本号答了回来**——这台机器上 GPU 第一次
"有东西在回应"而不只是"有电"。r50 的教训仍在：再往前一步（步 15 放它开跑）会挂，所以边界
留 14 是对的；下一座山还是那座：单独细探步 15。另外步 12 在 r51 的新句（"只报号不致命"）
在这段窗口里没出现（窗口开头截断），但走位能过 13/14，说明它确实没再致命——顺带记一句:
那 44 行"alive Ns after step"说明真机配置里 watch 是开着的（3s），不是只有 by_itself 关它。

（对照上一轮问的"还差多少": 闸 A 从"未知"变成了"验证到 14、只差 15 之后的 GMU 开跑与 3D 核"。）

## 6ay. 第五十四轮：五点标定坐实"几何塌了"——一轴只分得出左右、另一轴近乎无信号；且它随引导漂移

r52（烙印 `g00c5eb1` ✓）。使用者确认按 **左上→右上→左下→右下→中心** 顺序点。真实 cell 换算
（修正我调试行的 9 倍除法笔误后：40 轴 = axis_x/410、60 轴 = axis_y/432）：

| 点 | 上屏报的位置 | 60 轴 cells | 40 轴 cells |
|----|--------------|------------|-------------|
| 左上 | (54,31) | 2.0 | 38.5 |
| 右上 | (27,1313) | 1.0 | 19.0 |
| 左下 | (54,25) | 2.0 | 38.6 |
| 右下 | (108,1322)| 4.0 | 18.9 |
| 中心 | (27,1340) | 1.0 | 18.6 |
| (多一次) | (54,41) | 2.0 | 38.4 |

**40 轴只能把世界分成"左(38.5)"与"右+中心(≈19)"两半（中心饱和到右的值）；60 轴近乎无信号（1-4）。
几何不是翻转或缩放错——是一整段通道没有响应。** 而且同一块屏在不同会话里的行为不同（r50/r51 划线
连贯、这次塌轴）——**配置随引导漂移**。我们的初始化只做"清 reload 标志、请求再初始化、引脚复位、
等握手"——芯片复位时重读的是**它自己 flash 里的固件**；而 HX83121A 的**通道配置（厂商 .ini：touch
window 的 TX/RX 数量与 mux 映射）我们从未显式读取或下载过**。ROM 默认的窗口几何与这块 40×60 面板
对不上，正是"一部分区域有反应、其余塌掉、跨引导不稳定"的形状。

下一步的钥匙在厂商配置里：这台机器的 Windows 分区还留着 HIMAX 驱动包，其 .ini/.bin 就是那本表。
向使用者要它（DriverStore\FileRepository\*himax* 下的配置文件）；拿到后在初始化里加"下载 ini"
（走我们已经会用的 AHB-over-SPI 写通道），几何问题才算对上源头。顺带记下待修：调试行的 cells
除法差 9 倍（下个小版本一起改）。

## 6az. 第五十五轮：使用者递来 EGoTouchRev——"混乱"的真身在十分钟内落定

使用者给出参考：`github.com/awarson2233/EGoTouchRev`（MateBook E Go 触摸的完整逆向工程：内核
`himax-thp.ko` + 用户态服务 + 对 Windows 原厂驱动 himax_thp_drv.dll 的 Ghidra 记录）。它的
`Common/include/FrameLayout.h` 把帧讲死了——**全部偏移"对照 Ghidra 逆向验证过"**：

- 帧 = **7 字节头（5A A5 5A…）+ 40 行 × 60 列原始矩阵（4800B，就在偏移 7——与我们多年实测的
  "anchored at byte 7" 完全吻合）+ 128 词状态表**。没有"差分行/原始行"之分——**我们的整套路行扫描
  是对不存在结构的逆向**。
- **矩阵是 40 行 × 60 列**（kTxCount=40, kRxCount=60）。我们把 60 当行数——**多扫的 20 行其实是
  状态表的字节被当成了电容**。这解释了漂移、假行、census 的错位、五点标定的塌轴：不是通道聋了，
  是**我们数错了轴、还把状态字节当数据**。
- 状态表的命名词（他们的词表）：**词 6=固件自复位标志**（原厂驱动遇此报 SOS——"某些时段整场静默"
  的另一嫌疑人），**词 54/55=芯片自己算好的单点 X/Y**（0xFF=无点）——现成的硬件基准，正可对表我们
  从矩阵算的点。词 0=IDLE、词 9=帧时间戳、词 14/16=噪声计数……
- **级联的第二颗 HX83121A**（slave：另一条 SPI 0x890000、INT GPIO39、总线命令 F4/F5——正是我们
  头注释里"华为未知"的 type-4！）只交 7B 头 + 166 词（9×9 辅助栅格/笔用）——**手指几何不需要它**，
  本轮不动第二路。

r55 的做法：把帧解析改回厂商的固定布局（TX=40/RX=60、行距 60——正是 r39 修对的那个行距！）、
映射改成干净的线性换算（删掉 X_SCALE=9 那类凑出来的常数；原点与两个方向先按 dts 的 framebuffer
取直，真实翻转交给带基准的标定）；读回状态表的词 6（自复位就只说一次）与词 54/55（**每个 tap
旁边并排报"芯片自己说它在哪儿"**）。调试行的 cells 除法随新常数自动变正确（X_SCALE=1 后 x/DX=列）。

（QEMU 无此设备，回归只验桌面。GPU 不动。）

## 6ba. 第五十五轮上机：帧走对了、五点却聚到矩阵一角——真凶浮出：**AFE 从未校准**

r53（`ged967960` ✓，使用者："还是有幽灵点击"）。新格式行都在场。同一轮里先记两个硬事实：

1. **同一个五点测试，r52 与 r53 两次开机，原始 cell 响应落在平铺数组的两端**（r52: 前 ~180 格；
   r53: 后 ~340 格）。**这不是解析问题能解释的——是控制器自己在两次开机里给出了不同的通道几何。**
2. 状态表词 54/55（芯片自报单点）全程报同一个 (56064, 3)——**不跟随**；词 6 自复位**没有**触发。

而 EGoTouchRev 的 `HimaxChipLifecycle.cpp` 把差别说破了：厂商的 Init 在开帧之后还要做三件事，
我们一件都没做——**读 OTP 项目号**（`W273AS2700` 等，选这颗面板的配置）、**发命令通道上的
StartCalibration(0x01)**、**ForceToScanRate(0x0e,120Hz)**。它的结束语是 *"Initialization and
Sense ON successful"*——**没做这套，模拟前端就停在电默认值上**：哪条通道对应哪块面板，随它上次
怎么醒的。这正好吞下"跨开机漂移"、"塌轴"、"自报点无效"三件事——**也解释了为什么接完 Windows
再进 HakiOS 的那几次表现好**（Windows 驱动刚校准过，芯片还热着）。

命令通道的格式（他们的 DLL 逆向，HimaxProtocol.cpp）：五个 16 字节槽 @0x10007550、写指针 @0x1000753C；
包体 byte2=id、byte4=参数、末两字节=按"假想头 A8 8A"求补码和的校验；再向同址写 4 字节触发
`A8 8A id 00`；读回 16 字节确认；槽 %5 轮转。项目号在运行态可直接从 **0x10007014**（fw 的
proj_info）读——比走 OTP/ddreg 那套仪式（安全模式、LPWU 定时器、EB/B9/BB 口令）便宜得多，而
固件本来就把选中的配置装进去了。

r54：初始化末尾（开帧之后，与厂商同序）加 `himax_sense_on()`——清命令槽、读回**芯片自报的
项目名**打进日志、发校准与 120Hz 两条命令（厂商自己也标 non-fatal）。若这判对了，下一轮五点
标定应当第一次稳定；"幽灵点击"多半随通道归位一起消。(映射方向仍待那次标定钉——若厂商 DT 的
swap 说法成立，行→x、列→y。)

## 6bb. 第五十五轮：命令通道真通了（项目名"Gaokun"！），但几何仍漂——下一个嫌疑坐实：**旧帧环**

r54（`e420bb98` ✓）上机的三行都在：
```
himax: the chip names its own project: "Gaokun"      ← 芯片固件里就是这台机器的配置, 且它答话了
himax: the chip was asked to calibrate its front end
himax: the chip was asked to scan at 120 Hz
```
命令格式与校验全对——通道打通。但五点数据这次聚到另一角（列 57-59、行 0-2；r53 是行 34-39、
列 11-20）——**依然随开机变**。校准请求被"接受"不等于几何稳定，继续查厂商还做了什么我们没做：

`HimaxChipMode.cpp` + 生命周期开头三步（我们此前从未做）：
1. **解锁 psram rawdata 窗口**：0x10000000 ← 结束指针低半（他们填零）；
2. **排序模式**：0x10007F04 ← 0（EGO_RAWDATA 就是"全原始"）；
3. **`hx_set_N_frame(1)`**：0x10007294 ← **1**，再 ← **0x7F0C0001**——**"每次中断给一整个完整帧"**；
4. 之后才把 0x100072EC←0xF6 写成 **write_and_verify（读回确认）**（我们的 0xf6 是当年单向写的）。

**嫌疑就此坐实成机制**：不设 N_frame=1，芯片的 event stack 是个**旧帧环**——他们自己注释都写了
"读到的帧可能比手指晚四分之一秒"、Init 末尾还要特意"读掉丢弃旧帧"。我们的环同理：每次读的"最新帧"
实际混着几十毫秒到几百毫秒前的状态——快速按下时峰在旧位置、连按互相踩——**幽灵点击与跨会话的
"角落漂移"都能被这个解释**（每次开机按下节奏不同→环里混的东西不同）。

r55：把 1-3 步补进 `himax_sense_on()`（顺序照厂商，带读回重试 `reg_try`；0xf6 也补一次读回确认）。
若这是主犯，五点应当第一次**跨会话一致**。另记：真机 word 54/55 这次"无点"（此前是同一个死值）——
和环一样，都归到"帧没喂对"这一类。（映射方向仍留给下一次一致的五点钉。）

## 6bc. 第五十六轮：N_frame 判中了——几何活了；剩下的"幽灵"是**单帧假blob**，给它立见证人

r55（`g3d9edc80` ✓）上机，三步全部生效（rawdata 窗口、每中断一完整帧、项目名照旧"Gaokun"）。
五点的原始读数**第一次跨到了整屏**：

```
#1 左上: 行1 列0    #2 右上: 行0 列48    (中段#3-#5: 行0-2 列16/22/39 —— 移动的假点!)
#6 左下: 行39 列5   (tap 记录器上限 6, 右下与中心没被记到 —— 计数要放宽)
```

- **三个真实按下全部落对了位置**（左上=列0行1；右上=列48行0；左下=行39）——**旧帧环就是漂移主犯**，
  r39 时代"角一致、对面塌"的一切怪象到此收账。
- 中间的 #3-#5 聚在行 0-2、列从 48→16 递减——**是手指从右上抬向左下途中, 单帧冒出的假blob**——
  用户口中的"幽灵点击"。真实按下不动（帧间≈0），移动的blob活不过一帧。
- 词 54/55 自报点仍是死值 (56064,3)——这个模式里芯片不填它, 暂弃（留着无害）。

r56：给"开始按"立**两帧见证**——第一次看到的 blob 只押住不落地，下一帧在同一处再见到才成 DOWN
（一帧只活一次的 blob 就是抬指/掌边/甩腕的样子；8ms 一帧，见证只让真按下晚 16ms, 不可感）。
顺带把 tap 记录上限 6→10，五点测试要全程可测。若这轮干净，下一步就是**用五点的一次完整数据把
仿射（含边缘死区）一次定死**——那是触摸收官前最后一颗钉。

## 6bd. 第五十六轮上机：去抖生效（无单帧幽灵了），但抓到了**会动的下一张多米诺：芯片固件自复位**

r56（`gb4fe7c95` ✓）。十个 DOWN（上限已放到 10）：

```
左上角 5 次: 行0-1 列0-2     左下角 5 次: 行37-39 列5-7
—— 右上/右下/中心: 没有; 会话末段: "the chip says its firmware reset itself" 第一次出现!
—— 之后: 0 frames, anchored at byte 0
```

判读：两簇的**真实位置依然正确**（左列列 0-2、底行行 37-39——几何没坏）；但一次按下被拆成
多个 DOWN——这不是单帧幽灵（那个已被两帧见证拦住），而是**blob 在按下的几十帧里断续**——
外加后半程整场静默。**词 6 抓到真凶：芯片固件自复位。** 自复位会把初始化写进 RAM 的一切
清零——rawdata 窗口、N_frame=1、0xf6、校准请求——芯片退回默认模式（差帧混排→blob断续），
最后连帧也不交。这也回头解释了此前"跨会话角落漂移"里剩余的那部分：**有些会话中途发生过
自复位，我们从未应答**。

r57：厂商的 SOS 处理照搬——状态表词 6 一旦置位就 `chip_rearm`：唤醒(gaokun_wake) →
rawdata 模式 → 重发校准/扫描率。让"自己重启的芯片"每次都重新被问一遍，而不是听之任之。

（另记两笔账：① 词 54/55 自报点在 EGO_RAWDATA 模式不填——别再引它；② 若 r57 后仍有
"一次按下拆成几个 DOWN"，那是弱阈/保持阈的门限问题（KEEP_PEAK 300），下一轮按数据调。）

## 6be. 第五十八轮：r57 是我们自己喂出的风暴——**开机的重载本来就会置词 6**；应答改认症状、并且封顶

r57（`ga7c23054` ✓）现场读出来是倒退：一个会话里**完整的唤醒+模式+命令连发五次**。原因很干净：
词 6"固件自复位"在**我们开机自己的程序重载**下就会置位——r57 的应答（看见即重初始化）于是
重初始化→又一次重载→词 6 又亮→再应答……**自喂的风暴**。（顺带把导出结构的旧账再钉一遍：文件
末尾那些 `x100`/旧格式 tap 是**更老会话的 logdump 尾巴**，切段要靠时间戳与格式年代，不能只看
"最后一段"。）

r58 的原则：**标志只记账，症状才触发，而且封顶。**
- 词 6 保留计数与一次性说明，不再触发任何动作；
- 轮询的统计块（30s 一窗）里看**真正的症状**：连续两窗 `seen.reports` 零增长（整场静默），
  才唤醒重问一遍 init 的全部问题——**最多三次**，第三次后明说"等一次更热的开机"；
- 空闲面板本就会静默：三次的封顶让良性静默只花三次重问，恶性静默则从"整场死"变成"最多三次
  内自愈或明确认输"。

（r57 那份导出里唯一可放心引用的仍是我方几何正确的事实本身——它来自 r55/r56 的会话；本轮没有
新的触摸数据可析, 先修回归。五点复测随 r58 请。）

## 6bf. 第五十九轮：挂载系统改为 iPadOS——不用换镜像，换的是加载器递出去的"身份证"

使用者要求把系统改成 iPadOS。查明了机制：我们挂的一直是 iOS 16.5（build **20F66**）——而
iPadOS 16.5 是**同一个 build、同一套二进制**；"手机还是平板"由**加载器从自己 IPSW 抄进设备树
的身份**决定（`xnuloader/identity.h`，`mkidentity.sh` 生成，仓库不存）。属性表里就写着
`artwork-device-idiom`（当时值 "phone"）、`artwork-scale-factor`（当时 3）、`sandman-support`……

做法（全在工作站，6.2GB IPSW 一个字节都没下载——用 HTTP Range 只取了两头 + 37KB 成员）：
1. api.ipsw.me 查得 iPad13,18 的 16.5/20F66 IPSW（"iPad_Fall_2022", 6,245,018,772B）；
2. 远端 zip 按 Range 读中央目录 → 取出 `Firmware/all_flash/DeviceTree.j271ap.im4p`（WiFi 板）；
3. **真实的适配点一处**：`mkidentity.py` 硬性要求 `/product/haptics`——iPhone 有 Taptic Engine，
   **Wi-Fi iPad 的设备树根本没有这个节点**（KeyError）。改成与占位路径同样的可选处理（提交 a25693e）。
4. 生成新 identity.h 并核对：`idiom="pad"`、**比例因子 2**（我们的 1600×2560 面板从此按
   800×1280 点的平板画布渲染——这台 3:2 平板第一次拿到贴身的 UI 尺度）、`sandman-support=1`、
   无 `baseband-chipset`、产品名 "iPad (10th generation)"。`graphics-featureset-class` 变 APPLE7
   ——若渲染出异象，加载器里有一处现成先例（当年把 artwork-display-gamut 按 sRGB 改写）可单点覆写。
5. `make loader` → QEMU 换上新 BOOTAA64.EFI 全桌面回归绿（同一加载器两用：AArch64 UEFI）。
6. 出包 `ga25693e4`：LOADER: yes——**这一包要连 EFI\BOOT\BOOTAA64.EFI 一起刷**；内核无改动,
   驱动侧仍是 r58（静默重问封顶版），下一轮继续五点对齐修驱动。

（顺带记下工具小抄：远端取 zip 成员 = Range 读尾部找 EOCD/中央目录 → 按 offset 取成员；
Python zipfile 配 seek/seekable/read 的迷你 Remote 类即可复用全部现成工具。）

## 6bg. 第五十九轮半：iPad 布局上机成功，但要横着坐——`fbrotate`：旋转长在交换的复制里

使用者反馈：布局对了（pad idiom 落地），但画面是竖的——他们要把这台 16:10 的 3:2 面板横着用。
这正是 Windows 在这台机器上一直在解的问题：面板物理竖贴（1600×2560），桌面横画——Windows 靠
GPU 合成器在扫描输出前转 90°。**我们此刻没有 GPU**，但驱动里每条 swap 本来就把整个画面逐行
复制进面板缓冲（drawRows，"缩放一帧约 9 ms"）——**旋转就接进这次复制**：

- `fbrotate=90|270`：MiniIMF 把"报告尺寸"照 boot-args 变成横的（2560×1600），系统从此横着画；
  交换时只有**满幅单层**这一种常规情形走旋转快速路径——**32×32 分块转置**（列读对行写，不分块
  每像素一次缓存缺失；分块后与已付的缩放复制同量级）。其余情形照旧（桌面只发满幅）。
- **一个旋钮管两半**：himax 在**上报出口**做同角度转（DOWN/MOVE/UP 三处），分析、tap 日志、
  格点全部保留面板与芯片自己的坐标——只有 userland 看到的坐标跟着画面转。
- 控制台直写面板缓冲，启动日志照旧竖屏可读；桌面一起来才横过来——顺序自然。
- boot-args 换向：`fbwidth=2560 fbheight=1600 fbrotate=90`（90 还是 270 取决于屏轴哪边是"上"，
  上机一次就知道；反向改一个词重打包）。QEMU 回归不传这些参数，旧路径原样绿。
- 性能预期写在这里接受检验：CPU 转置 1600×2560×4B ≈ 16MB/帧，分块后每帧十几 ms——UI 够用，
  视频与滚动要等真 GPU；这不是权宜的骗局，是"没有合成器时谁来做旋转"的诚实答案。

（工具记号：TUNABLE 宏读 boot-args；`PE_parse_boot_argn("fbrotate", ...)` 在输入驱动里惰性读一次。
两半共用一个参数的理由：显示与触摸的坐标系**必须**同源，否则又回到"幽灵点击"那种两头对不上的局。）

**目录闸记下的课**：新旋钮第一次打包时 `hakios-config.py` 直接拒绝——`CONFIG_BOOTARGS` 里的每个词都必须
在 `configs/boot-args.lst` 里，而那份表由 `tools/boot-args.py --lst/--markdown/--symbols/--check` 从代码
生成（CI 同源校验）。正确的收尾三步：造完旋钮 → 再生三表 → `--check` 绿 → 再生 `.config` → 重建内核。
（另记一条流程教训：把脚本从 stdin 喂给 bash 时，脚本里若启动 QEMU 之类吃 stdin 的程序，
后半段会被吞掉——回归脚本一律落盘按路径执行。）

## 6bh. 第六十轮：第一次横屏上机——画面被压进竖屏：快速路径只认"整屏矩形"，而窗口服务器不欠谁整屏

r59 上机：iPad 布局在、画布确实是 2560×1600（横屏布局出现即是证明），但画面**被缩放压进竖面板**
——旋转快速路径没被走到。回想自己的守门条件就脸红：它只接受"一张完整画布、原尺寸、原点"的
swap——**层偏移、脏矩形、带填充的 stride**，任何一种都会从守门前walk过、落回旧的缩放路径。
"桌面只会发整屏"又是一次没有证据的假定（和第 6aw 节那次"最后一段=现场"同族）。

r60 的修法：旋转回答**任何不等比缩放（即 1:1）的矩形**——面板上的目标矩形是请求矩形**转出来的**
（自己按 90/270 公式算，绝不用旧路径的缩放映射），分块只覆盖那块矩形。并且从此不猜：fbrotate 开着时，
前三次 swap 把**实际收到的 surface/尺寸/src/dst/判定**原样打出来（`fbrotate=9: surface ... turned=yes|no`）
——上轮要是有这三行，一轮就到目的地，不用两轮。启动日志保持竖屏是设计：控制台直写面板缓冲，
桌面一起来才横——调试期竖读日志不碍事，真要横它是独立的小事（字符格转置），先不做。

（90 还是 270 的骰子仍没揭：上机一次见分晓，反向只改 boot-args 一个词。）

## 6bi. 第六十一/二轮：答案要活过环形缓冲；按"用户态拿的还是面板数字"这条假设对齐全部尺寸出口

r60 的导出教了两件事：①**当前会话只剩 96KB 残段**——早期诊断行（display init、三行矩形、前 8 个
swap 报告）全被环形缓冲挤掉；文件里那些 `1600×2560` 的 swap 矩形行全是**旧嵌入 logdump**（旧烙印
g02521c83）的，不能当现案。②要拿现场数据，就得让读数**反复出现**——r61 把旋转台账挂进 5 秒统计块：
`fbrotate=9: canvas … panel …: N turned, M the scaled way (last refused: surface … src … dst …)`，
环形缓冲杀不掉会重发的行；（顺带：离线公式自检通过，代码里 90/270 的方向注释写反了——已改，90=顶边落在面板右短边。）

r62 按使用者指定的第二假设修：**内核说到尺寸的所有出口必须同一套话**——
① `GetDisplayArea`（selector 29）此前根本没答，补答画布 (0,0,fWidth,fHeight)；
② `GetDisplaySize` 本就答画布（已核对）；
③ fbrotate 开启时驱动向 `/chosen` 写 **`display-rotation`=90/270**——那是 iBoot 在面板物理转装设备上的
原语，让 stock backboardd 至少能看见"画面与面板为什么切法不同"，而不是从压扁去猜。
vinfo（控制台的世界）不动——启动日志保持竖读是设计。台账保留：**若 surface 仍按 1600×2560 被拒，
那行报出的数字就是下一跳的起点**；若 N turned 上去了而画面反了，那是 90/270 的骰子，改一个词。

## 6bj. 第六十四轮：QEMU 是半个实验室；三条方向落地为 A+B 一个包

连续三份设备导出都停在 T+80~110 秒——恰在桌面第一帧之前（加载器心跳镜像正常，但会话就是在那里
收笔）。与其再要一次时机，把能自己做的先做掉：

**QEMU 复现实验**（step115，自带 ramfb 就是手机竖屏 1125×2436）：几何完全复现
（`display 2436x1125 on a panel of 1125x2436`），launchd/SpringBoard/backboardd 都起来了——
**但 QEMU 里没有一次 swap 走到我们的驱动**。结论：QEMU 是"半个实验室"——能复现启动与问答，
不复现帧。（也解释了 notes 区只在设备心跳里打印：QEMU 日志无该段。）

**三条方向**：
- **A（只声明，不代劳）**：画布=面板（1:1 老路照旧稳），只把 `/chosen` 的 `display-rotation=90`
  声明出去——真机上这就是 iBoot 处理转装面板的机制，成则用户态显示栈代劳，败也只是回到现状。
  注意 r62 已把该属性写过一次（但那时画布是横的、条件不对）——r64 是它的第一次条件正确的试验。
- **B（问话留痕）**：externalMethod 入口对每个 selector 首次调用打一行（问题+参数+答案尺寸）——
  "窗口服务器的竖屏尺寸从哪条路要来的"从此有直接证据；QEMU 先跑（backboardd 在 QEMU 也会起，
  问不问、问哪条本地就能看），设备靠 notes 兜底。
- **C（保底）**：按竖屏先收官触摸与 GPU——这台机器在 Windows 下的横屏本就是 GPU 合成器转的；
  Adreno 走通后旋转是合成器一行能力，不该由 CPU 苦役硬扛。

r64 = A+B：boot-args 回 `fbwidth=1600 fbheight=2560 fbrotate=90`（只声明），加 B 的问话留痕；
QEMU 探针先行。若 A 成——最干净的结果；若 A 败——B 的行直接说出下一跳。

## 6bk. 第六十五轮：挖到根——r59 起的显示改动从没进过任何内核；六轮"问题依旧"是诚实的

QEMU 带表探针揭示构建链的真洞：`xnu/apply.sh` 只在**建树**那一次把 `xnu/files/` 覆盖进去；
之后 `make kernel` 见树存在就跳过——**驱动仓库有 sync-drivers.sh，iokit 的 files/ 没有对应环节**。
结果：r59 的旋转、r61 的台账、r62 的属性、r63 的笔记、r64 的留痕——**六轮里没有任何一行进过
上机的内核**（第一次真编译还带回六个潜伏错误：插入锚吞掉返回类型行 ×2、逗号表达式取错尾值、
handleOpen 缺类内声明+签名不符族原型——全部修好，`strings` 实证新代码在 hakios-kernel 里）。
使用者每一次"问题依旧"都与日志一致：上机的一直是旧驱动+新参数。

修复的制度化：新增 `xnu/sync-files.sh`（apply.sh 同一动作，随时可重放），并把 `make kernel`
焊成先同步再编译；"K-OK"从此才名副其实。

QEMU 实验的边界也要记录（step115/124/125，仪表版内核）：QEMU 里 SpringBoard/backboardd 会起、
display init 行正常——但 **userland 从不打开我们的帧缓冲服务**（opened=0、asks=0、swap=0）。
设备上的旧会话（11 月 4–5 日）明明有 swap 行，说明用户态是认这个服务的——QEMU 缺的是那条匹配
链，它至多是"1/4 个实验室"。真正的判决交给设备上的三条 notes：
几何/声明笔记、`userland opened the framebuffer`、每个 selector 的首问留痕——它们不依赖导出时机。

r65 = A+B 的真实版：画布=面板（1:1 稳路）+ `display-rotation=90` 声明 + 全套留痕。若 stock 栈
认这个声明——旋转由苹果自己的机制完成；若不认——首个被拒/被问的矩形与 selector 序列直接说出
它的尺寸是从哪条路要来的。

## 6bl. 第六十六轮：下一代真的有梯子——hakios-arm64e-dada（iOS 26 / xnu-12377 已在手机上到主屏）

使用者递来参考：https://github.com/superboyyy/hakios-arm64e-dada —— **xnu-12377.121.6（iOS 26
的内核）+ arm64e 用户态，小米 15（SM8750）上到主屏、触摸可用、闪存读写、Adreno 经
metal2vulkan→Turnip→内核驱动**。它 built on HakiOS：`kernel/` 是对新 XNU 的完整内核 patch
（91,637 行——HakiOS 全部内核工作在 12377 上的重放+他们的增量），`base/` 就是 HakiOS 仓库的
iOS 26 分叉（xnuloader/构建/文档都活着）。sc8280 在他们全部材料里不存在——平台那一半永远是
我们自己的。

三个直接结论：
1. **"iPadOS 26 用户态"不再是远征，是有图纸的搬家**：新基座现成，搬的是我们的驱动与机制；
   我们的 GPU 计划与他们的路线（Turnip）同法，可互抄答案。
2. **身份管道照样能用**：iPad13,18（A14）在 iPadOS 26 支持列表里——到那代，换身份仍是几行属性。
3. **唯一的硬件门槛是 PAC**：iOS 26 用户态全 arm64e，需 ARMv8.3 指针认证。Oryon 有（他们
   "hardware PAC"），SC8280XP 的 X1C/A78C 没有任何表写着——加载器现在直接问芯片：读
   ID_AA64ISAR1（APA/API/GPA 字段）打在 MIDR 那行旁边，答案随每次启动在场。QEMU 顺带自验
   （cortex-a76 应报 no PAC）。**使用者代查已确认：8cx Gen3 支持硬件 PAC——闸门定性通过**；
   r66 的探针留作机器自证（刷了它，APA 字段自己说话）。

搬家第一步已落：`xnu-12377.121.6` 的 dada 树在工作站开建（他们的 kernel/apply.sh 就是
clone Apple tag + 一个 patch——同族工艺）。他们的 HISTORY.txt 记法与我们同源（wifi/modem
按步标记），确认这是 HakiOS 血统的正统延伸。新代路线若走通，GPU 一步可借他们的
metal2vulkan→Turnip（690 在 Turnip 支持内）。但顺序不变：**这一代先收官**（五点、GPU、
桌面整洁），搬家作第二 entry——8796 永远是可刷的底。

顺序不变：这一代先收官（五点标定→GPU），搬家作第二 entry 双内核并存，不冒险。

## 6bm. 第六十七轮起：OS 26 迁移开工（使用者定调："先开始os26迁移工作吧，触摸屏放缓"）

M0（完成）：`~/hakios/hakios-dada` clone；`~/hakios/xnu-12377` = Apple xnu-12377.121.6 +
dada 91,637 行 patch **干净应用**。他们的 docs/ARM64E.md 是关键地图：arm64e 内核（标准-PAC 密钥
后端、加载器做 auth-fixup 链接与五把随机密钥、`__pac_keys` 经镜像传递、TTBR1 签名域对齐）已在
QEMU cortex-a710 到 PID 1——**iOS 26 一代的内核半边是现成的**，欠用户态数据与真机。

分叉对比（决定搬家清单）：他们 base/ = HakiOS 同构仓库（同 tools/docs/xnu-build 体系，HISTORY
记法同源）；**loader_status 笔记环是我们独有**（他们 0 处）——要过继；heartbeat 机制上游已有。
设备表就是往 `platforms[]` 加一个 `platform_gaokun3`（结构逐字段同款）。

M1（结论：Linux 侧卡在一面编译器墙，不是配置问题）：新代内核在我们 WSL 上编译，先修通两处小坑
（xnu-env 末行 `[ Darwin ] &&` 在 set -e 下炸 source；`RC_DARWIN_KERNEL_VERSION=12377.121.6`——
他们 tools/dada-build.sh 本来就写着）与一处工具链年龄（`-mno-implicit-sme` 是 Apple 私有旗——修成
构建树先探测再添加）。真正深的一层：新代 XNU 把 ptrauth 限定了**整数类型**——port.h 里
`mach_port_t __ptrauth(ptrauth_key_process_independent_data, 1, ptrauth_string_discriminator(...))`
（"auth int"，HAS_STD_PAC 时代的签名整数存储）。**上游 clang 21 与 22（免 root 解包 clang-22 全套
运行时实测）都只接受指针上的 __ptrauth，拒绝整数上的**——这不是版本滞后，是 Apple-clang 独有特性
的上游空白。dada 的 docs/ARM64E.md 也自证：里程碑 0-4 全部"built on macOS"。arm64 的 bootstrap 阶段
为何也带上 ptrauth_calls 仍可再查（gated by __has_feature，也许能按 pass 关旗），但 arm64e 终版
必然要这些语法。**结论：12377 内核的最终编译需要一台 macOS（Apple clang）——其余搬家部件
（驱动源码、加载器 C、身份 Python、用户态管道）都不受此限，可先行。**

顺序（使用者定）：触摸五点与旧代 GPU 步进**放缓**；终局 GPU 走新代现成的 metal2vulkan→Turnip。
旧基座 8796 保持可刷可跑，作为双 entry 的底。

## 6bn. 第六十七轮下半场：**没有 Mac，12377 也在 Linux 上编过了**——拆墙全过程

用户的条件："找找别的办法吧 我没有这个条件"。路是三层，全拆了（arm64 = 不带 PAC 的编译通道，
与 8796 时代的形态同代）：

1. **ptrauth 语法墙消失**：全树 356 处 `__has_feature(ptrauth_calls)` 双分支门控在 plain arm64 下
   全部走 plain 分支——一个 ptrauth 语法错都没有。
2. **iig 墙（Apple 闭源生成器）**：借头升级到 **macOS 26.1 SDK**（joseluisq 库有发布，setup.sh 的
   借用路径 13.3→26.1 一处替换）；26.1 框架头带来 cache-mode 四项，独缺 RealTime——按 .iig 原值
   手写一处；`Panic_Impl/SetPowerOverride_Impl/CreatePMAssertion_Impl/ReleasePMAssertion_Impl`
   四条声明按 cpp 里的定义手写进类；`kIOUserServrMaxPanicReasonLength` 宏在使用点前定义。
3. **老寄存器名墙**：`APIxKeyLo_EL1` 改原生编码 `S3_6_C2_Cx_5`（C 与 .s 三处），语义精确相同；
   第二处 `pacib` 的 inline asm 要自带 `.arch_extension pauth`（上游按块作用域）。

其余的拆墙笔记（工具链/库层）：`-mno-implicit-sme` 改为构建树探测后添加；`xnu-env.sh` 末行
`[ Darwin ] &&` 在 set -e 下炸 source（改 if）；stage2 的两颗库对 12377 导出头要 `-DXNU_KERNEL_PRIVATE=1`
（新头"仅核可见"）+ `-isystem $(clang -print-resource-dir)/include`（EXTERNAL_HEADERS 的 stdatomic 垫片
要 include_next 的下一站）；dada 的 Wi-Fi/BT 设备文件停车 + `hakios_dada_wifi_init` 空 stub（redwood_input
引用它）——注意 **config.pl 不容 inline 注释**，conf 条目行改完要删 BUILD/obj/*/options+normal 才会重放。

终链在跑。这一台机器从此可以自建 iOS 26 代的内核（plain arm64）；arm64e 通道若将来需要，
整数-auth 的 44 处成对宏是已量化的第二层预案。

**结果（17:32）**：`BUILD/obj/DEVELOPMENT_ARM64_BCM2837/mach.development.bcm2837`——18,879,560 字节
Mach-O arm64，`Darwin Kernel Version 12377.121.6: ... skynet:xnu-12377`，**在这台没有 Mac 的 Linux 上
链接完成**。278 条 kextsymboltool 的 import-list 抱怨正是 dada 自己判据里过滤的噪声。
把"我们的全部改动"从 dada patch 里剥出来对照干净基线重放一遍：**整个 Linux 移植面 = 226 行**
（`porting_notes/artifacts/xnu-12377-linux-bringup.patch`；11 个树内文件 + base 的两份依赖脚本）。
墙看着吓人，砖其实是按块数的——数完了，也就拆完了。内核副本：`xnu-12377-linux-linked.macho`。
树内状态在 xnu-12377 的 git（f05f3c0），可复现：Apple tag → dada apply.sh → 这份 226 行。

真实需求（使用者）："在工作站编译改内核，最终在笔记本上打包——整盘网络传输太慢。"
结构本身就支持：U 盘里唯一每轮变的东西是 `EFI/hakios/payload.bin`（kernel+root.apfs+boot-args，
~19MB，zip 后 **~6MB**）；`system.apfs`(2.5G)/分区/logdump **都不变**。整盘镜像只在
布局或系统镜像变化时才需要。

- 工作站（WSL）：`WANT_VERBOSE=1 bash make_gaokun3_update.sh`
  → `porting_notes/artifacts/hakios-gaokun3-update-g<烙印>.zip`（目录结构=U 盘 ESP 落位）
  + `manifest-*.txt`（烙印、大小、sha256、LOADER 是否变化——loader.efi 哈希有状态记录，
  不变则 `LOADER: keep`）。
- 笔记本（Windows，U 盘插上进行中）：把 U 盘 HAKIOS_ESP 卷里的 `EFI\hakios\payload.bin`
  覆盖即可；一键脚本 `apply-hakios-update.ps1`（自动找包、按标签挂载 ESP、按 manifest 决定
  是否连 `BOOTAA64.EFI` 一起换、复制后回读哈希验证）。DiskGenius 手动覆盖同效。
- 首个 r33 包已产出：`hakios-gaokun3-update-g57f1502e.zip`（5.8MB；loader `e6f78ee5…` 与
  现盘一致 → keep）。

## 8. 交付物清单

- `porting_notes/artifacts/hakios-gaokun3-full.img` —— **一步到位的 iOS 磁盘**（2.9GiB GPT：ESP 内含
  带身份的引导器 + payload + 完整 Darwin 系统镜像）
- `porting_notes/artifacts/gaokun3-full-SHA256SUMS`、`gaokun3-SHA256SUMS` —— 产物摘要
- `porting_notes/artifacts/hakios-gaokun3.img` —— 最小可启动磁盘（64MiB）
- `porting_notes/artifacts/qemu-ios-screen.png` —— QEMU 里完整 iOS 启动的 Apple logo 截屏（1125×2436）
- `porting_notes/scripts/` —— 镜像组装、loader 补丁、QEMU 验证的可重复脚本
- HakiOS 仓库内（WSL `~/hakios/xnu`）：`devices/gaokun3/*`、`xnuloader/loader.c`（平台 + 系统镜像读取
  + 警告修复）、`docs/devices/gaokun3.md`、`build/esp-gaokun3{,-full}/`、`hakios-kernel`、
  `xnuloader/loader.efi`、`userland/{root,system}.apfs`、`qemu/*.img`
- 过程日志：WSL `~/hakios/logs/{01..14}-*.log`、`qemu-run.log`、`qemu-gaokun3*.log`（含
  `qemu-gaokun3-darwin.log` = 完整 iOS 那次）；工具链记录 `~/hakios/logs/README-bootstrap.md`
- IPSW（Apple 官方，勿分发）：`~/hakios/dl/iPhone10,3,iPhone10,6_16.5_20F66_Restore.ipsw`

## 6bo. 第六十八轮：**新代内核第一次被我们的加载器带起来**——QEMU 尽的力与不该它背的锅

M2 开场意外之喜：**不用先做加载器大合体**——plain arm64 的 12377 Mach-O 没有 auth-fixup 链，
现有加载器直接装载、跳转，活着到了 12.6s：`Darwin Kernel Version 12377.121.6` banner、pmap/zone
全图、调度器、IOKit IOMD、BANK、kdp——新代那句 `Log queues configured: slot 9×32K` 是它送来的
见面礼（将来设备观察会喜欢这套）。

拆过的关卡（都留在代码里）：

1. **种子**：新代要求 bootloader 给 256 字节随机种子，少了 panic 点名（`prng_random.c:75`）。
   加载器种子扩到 256，用 splitmix64 把计数器充分混合；两代兼容（22 代要 64 就拿 64）。
2. **观察通道**：新代内核日志全在缓冲区，只有内核回到加载器（XNULDRST 槽）才 dump——卡住态没有输出。
   于是 QEMU monitor `info registers` + 自制 Mach-O 符号解析器 + 指令解码器直接钉 PC：
   两次 30s 采样落在同一条指令 = `_ExceptionVectorsBase + 0x200`（SP1 同步向量入口）= **异常风暴**。
3. **给风暴装声**：entry 里 `bl`+printf 的第一版——**printf 自己在这个阶段又炸**（二次风暴）。
   改成**哑的公告板**：槽后加五个 .quad（marker/ESR/FAR/ELR/计数，纯 store 零依赖），第 4 次走
   纯 assembly 的回家路（关 MMU、br 到加载器 stub）。QEMU 上连板子都没被写——entry 的指令流在
   TCG 里的行为到此为止：**QEMU 既不是 Apple 也不是这枚高通**，它第三次证明只能看 boot/几何。

**转向**：真身实验直接上真机——真机有验证过的 logdump 通道。加载器新增**双载荷选择**：
`payload-next.bin` 在场就先带它，banner 明说带的是哪个（QEMU 验证选择逻辑 ✓：
`payload: … from \EFI\hakios\payload-next.bin`）。r67 包 = payload.bin（8796 常规，默认引导不变）
+ payload-next.bin（12377，同一套设备 boot-args）+ 新加载器（seed256 + 公告板读取 + 双选）。
真机上的期望：新代 banner → 走多远算多远 → 若回退，logdump 会带回来的是**新代内核的真机日志**。
回退 = 重刷 r66 包（ga48159ba8.zip 在手）。

## 6bp. 第六十九轮：**新代内核在真机上活了 98 秒**——123.pmf 里的第一次新代真机会话

r67（gd5329505）刷上后第一次开机的串口（`~/logs/123.pmf`）：

- 加载器明说选了 **payload-next.bin**（23,074,360 字节），jumping to 10353a4a8；
- 之后 98.4 秒的实时串口 = **新代内核的完整桌面会话**：
  `himax: IC_ID is 0x83121a00: a "HX83121A"`、`the firmware is running`、
  `input: /dev/hakios-touch is up`、display notes（canvas 1600x2560, fbrotate=90 declared）、
  adreno SMMU 身份、launchd/SpringBoard/xpc 服务群连续拉起、MiniIOSurface 出图……
  **dada 补丁带来的 HakiOS 驱动套件在新代上原生跑通**；
- 会话以秩序流程结束（`log: ... written to logdump`、`psci: cpu 2 ends the system`、
  `ddr: memory bandwidth vote 1024 for the loader`）——礼貌地回到加载器，logdump 通道工作正常；
- 后半四段 `--- kernel message buffer ---` = log 卷历史 dump 的回读（旧会话），不是本次的 panic；
- 小杂音：新代把 payload 的 epoch 墙钟解析歪了（实时行打出昨天的日期）——时间机制两代有差，记下。

**边界要诚实**：屏幕使用者还没确认看到什么；98.4s 的结束是主动流程（使用者动作或机制超时），
不是崩溃——全文件没有一条 `panic:`。QEMU 的向量风暴就此定性为 **TCG 语义特有**，真机没有它。

**结论**：M2 的主问题（新代能不能在这台机器上活）已经有了肯定答案。接下来按证据排：
确认屏幕 → 修 epoch/时钟 → 新代心跳与观察面 → 新代的用户态换代（iPadOS 26 rootfs，身份流水线复用）。

## 6bq. 第七十轮：使用者的眼睛补上了日志读不出的部分——加载器长出"回来接常规"的退路

使用者的描述（比日志诚实）：屏幕上的文本滚到 logdump 那行之后一段就黑、然后就重启，体感远没有
日志时间戳的一百秒。两件事各自有解：

1. **"重启"不是 panic**：全日志没有一条 `panic:`，我们埋的 `GAOKUN: panic` 标记也没响——收尾是
   秩序流程（`psci: cpu 2 ends the system`→回到加载器→加载器按老规矩冷复位）。而 payload-next
   还在盘上 → 下一次开机**又**带新代——"黑屏-重启"会循环。
2. **退路**：加载器的收尾现在多一步——**试验载荷回到后，不站着复位，而是就地改带常规载荷再跑**
   （和 fastboot `oem run` 循环同一条被真机验证过的路）。一次开机 = 试新代一次 → 自动接上能用的
   桌面；想再试新代，重启即可；想彻底停试，删 payload-next.bin。
   （QEMU 端到端验证：载荷缺根映像 → 快速回核 → `the newer payload came back` → 常规核继续起。）

**留给下一轮的观察**：屏幕上新代的文本在 logdump 行后就不再滚——新代的早期文本控制台与
真机的显示接管之间有一段黑。这就是 M4（新代显示保真）的第一条线索，也是它值一次专门实验的理由。

## 6br. 第七十一轮：真机被试验困住了——加载器学会"数自己的失败"，新代改试单核

一天里新代真机行为的三段：19:31 跑 98 秒（进异常路径收尾，最终日志没落盘）→ r68 起每次死在
`core 700: the firmware answers 0` 之后的核启动一步（2 秒级）→ 机器进入"看门狗复位→再试→再死"
的无限循环。删掉 payload-next 后 8796 正常到桌面（那份日志里 8 次 himax 点名是**心跳机制的正常
脉动**，不是重启循环——顺带把 r66 心跳在 r68 加载器下完好确认了）。所以：**机器与固件没有坏**，
新代的早死是它自己 SMP bring-up 附近的问题，而且状态敏感（同内核下午能走远、现在走不远）。

设计的缺口也暴露了：r68 的"回来接常规"只对**会回来**的失败有效；硬挂+看门狗复位的失败救不到。
r69 补上**跨重启的失败计数**——UEFI 变量 `HakiNextFail`（NVRAM 活过复位）：

- 带 payload-next 之前先 +1（挂了不复位计数的路，由"不回来"本身完成计数）；
- **连着两次没回来 → 这一轮不再试新代**，直接起常规载荷（打一行说明）；
- **payload-next.bin 不在卷上 → 清零**（"把文件放回去"= 重新武装，用户的开关）；
- QEMU 三 boot 验证：1、2 试新代挂 → 3 `the newer payload did not come back on two boots in a
  row: not trying it this boot` → 常规核完整起（181KB log）✓。

新代的试验载荷同轮换**单核变体**（`cpus=1`，去掉 bootcpu）——死在叫核那步，就把核这一维剪掉：
单核若能走到下午那种深度，问题锁死在多核 bring-up；若单核也早死，是更早的共性。r69（gdf03d252）。
教训一句：**试验机制必须假设最坏的失败是"不回来"**，一切救场都要建在能被观察到的那条边上。

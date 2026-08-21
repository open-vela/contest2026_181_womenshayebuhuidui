# 24. 遗留问题清单

> 日期：2026-08-21
> 状态：PAN 上网主线已完成并回归通过（见 21 号文）。这里记录**明确知道、但本轮有意
> 不做**的事，含各自的判断依据和动手时的入口。
> 维护约定：解决一项就把它移到对应的复盘文档并在这里标注去向，不要直接删。

## P0 — 会吃掉整块 `/data`，优先级高于任何新功能

### 24.1 XIP 下 NOR 写路径未全部 RAM 驻留

**现象**　非正常掉电后，下次开机偶发在
`nx_mount("/dev/config0", "/data", "littlefs")` 里 HardFault，任务 `AppBringUp`，
板子起不来：

```
INFO: NOR MTD registered at /dev/config0 (offset=2464 blocks=1024)
dump_assert_info: Assertion failed panic: at arm_hardfault.c:186 task: AppBringUp
sched_dumpstack: [ 3] [<0x1203ca06>] sf32lb_flash_unlock+0x1/0x20
```

**同一份镜像有时能过有时不能过**，pandbg 与正式镜像表现也不同——不是确定性的代码路径
问题。而且一旦中招，之后每次开机都中（损坏的元数据要写才能修，一写又炸）。

**机制**　`sf32lb_flash_hw_init()`（`vendor/sifli/chips/sf32lb52/sf32lb_flash.c:120`）
检测到自己在 XIP 执行就跳过 `HAL_FLASH_Init`：

```c
pc = (uintptr_t)&sf32lb_flash_hw_init;
if (pc >= FLASH2_BASE_ADDR && pc < FLASH2_BASE_ADDR + SF32LB_NOR_TOTAL_SIZE) {
    g_flash_hw_initialized = false;
    syslog(LOG_WARNING, "WARN: skip HAL_FLASH_Init during XIP bringup, ...");
    return OK;
}
```

于是首次 NOR 写/擦由 `sf32lb_nor_prepare_io()` 懒加载
`sf32lb_flash_preinit_runtime()`（同文件 :181）。该函数**自己**标了
`SF32LB_FLASH_RAMFUNC`，但它调用的这几个 HAL 函数仍在 XIP flash 里：

```c
status = HAL_FLASH_PreInit(hflash);                      /* XIP */
HAL_FLASH_ISSUE_CMD(hflash, SPI_FLASH_CMD_RST_EN, 0);    /* 给自己取指的 flash 发 RESET */
HAL_FLASH_ISSUE_CMD(hflash, SPI_FLASH_CMD_RST, 0);
HAL_FLASH_SET_QUAL_SPI(hflash, true);                    /* 切 QSPI 模式 */
HAL_FLASH_CLR_PROTECT(hflash);
```

在给自己正在取指的那块 flash 发 RESET、切模式，能不能活下来取决于后续指令是否已在
I-cache 里——所以是间歇性的。非正常掉电正好让 littlefs 在 mount 时做恢复写入，命中它。

**恢复手段**　`docs_ble/tools/erase_data.py`：擦 NOR `0x129A0000:0x400000`（`/data` 的
littlefs 分区），擦完重烧，`sifli_ap.c` 的 `forceformat` 分支会重建文件系统。镜像本体在
`0x12010000..0x125FA000`，与该区间不重叠。代价是 bond 丢失，但板子发起连接时 SSP 走
user_confirm 自动接受，会自己重新配对（实测 7 秒内重新拿到 IP），**不需要在手机上
「忘记设备」**。

**修复方向**　二选一：

1. 把 `HAL_FLASH_PreInit` / `HAL_FLASH_ISSUE_CMD` / `HAL_FLASH_SET_QUAL_SPI` /
   `HAL_FLASH_CLR_PROTECT` 这条调用链整体 `__ramfunc` 化；
2. 把 NOR 写使能提前到一个确定的、不在文件系统 mount 中间的时机，并保证那段代码
   RAM 驻留。

**为什么本轮不做**　要动 vendor HAL 的驻留属性，影响面覆盖所有 flash 操作，且与 PAN
目标无关。值得单独一轮带完整回归（含反复掉电测试）来做。

---

## P1 — 影响产品完整度

### 24.2 本机蓝牙地址是硬编码假值

手表对外显示 `cd:ab:78:56:34:12`，设备名 `Agent-Watch-cd:ab:78:56:34:12`，bt-pan 网卡
MAC 也是它。多台设备同时在场会撞地址；而 `pan_get_device_id()` 用
`SHA256(BD_ADDR)[0:16]` 生成 Device-Id/chip_id，假地址意味着**所有设备的 Device-Id
相同**，一旦接入需要设备标识的云侧服务就会出问题。

入口：LCPU 的 NVDS 区（`sf32lb52_bt_adapter.c` 里
`SF32LB52_BT_NVDS_BUF_START 0x2040FE00`、`SF32LB52_BT_NVDS_PATTERN`）。产品化做法是
每台设备烧片时写入唯一 BD_ADDR，或从芯片 UID 派生。

不阻塞当前功能，配对与 PAN 都正常。

### 24.3 吞吐没有实测数字（本轮放弃）

镜像里没有 iperf，也没有 wget / webclient，`ping` 只能测延迟不能测带宽。要出数字需往
defconfig 加 `NETUTILS_IPERF` 或一个 HTTP 下载客户端；当前 SRAM 占用 90.48%、余量约
37 KB，加进去要重新核 SRAM 预算（见 19 号文）。

**已知的间接指标**：协商 L2CAP MTU 1691，接口 MTU 1500，1500 字节包能连续跑，60 分钟
长稳里大包丢包约 4.4%、小包 0.33%，RTT 均值 159 ms（120 次统计）。

注意 22 号文规则 3 的约束：**靠加大 ACL 包长提吞吐这条路已经没有余量**（487 已是邮箱
ring 上限），要提只能做分片流水与链路参数（sniff / QoS）。

---

## P2 — 工程卫生

### 24.4 `vendor/sifli` 里有被跟踪的构建产物

```
boards/sf32lb52/drivers/cmake_install.cmake
boards/sf32lb52/drivers/input/cmake_install.cmake
boards/sf32lb52/drivers/lcd/cmake_install.cmake
```

这三个文件被 `ninja` 改写，内容是**上次构建目录的绝对路径**（`CMAKE_INSTALL_PREFIX`
指向 `out/xxx/staging`），因此每次换构建目录它们就变「脏」。本来不该入库。

处理方式：向 `open-vela/vendor_sifli` 提一个 `git rm --cached` + `.gitignore` 的清理 PR。
**给上游仓提 PR 前记得别把这三个文件的本地改动带上。**

### 24.5 团队仓 `README.md` 仍是组委会模板

团队仓 README 第六节要求提交前替换成自己的作品说明（作品名、赛道、运行方式、简介）。
作品尚未完成，暂不写。

另外 `TASK_STATUS.md` 关于 PAN 的记录已过期（还写着「PAN vs SLIP 待定」「官方未实现
PAN」），改 README 时一并更新。

### 24.6 大赛要求的其余交付物尚未准备

按《大赛总览》，除代码与 AI 日志外还需要：介绍文档（.docx/.pdf/.pptx）、5 分钟以内的
演示视频；若使用 AI Coding 则**至少提供一个可复用的 Skill**。这些都还没做。

三个公共仓的 PR 已提交（zblue#231、sifli#29、bluetooth#591，CI 全绿、MERGEABLE，等组委会
review），索引见 23 号文。

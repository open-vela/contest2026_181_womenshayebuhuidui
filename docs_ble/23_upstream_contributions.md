# 23. 公共仓贡献索引（评审可见入口）

> 日期：2026-08-21
> 用途：本作品的一部分改动**按大赛规则不能放在团队仓**，而是以 PR 提交到 openvela
> 公共仓。这篇是给评委和后来人的索引：改了哪个仓、哪几行、修的是什么、对应 PR 在哪。

## 为什么团队仓里看不到这些代码

《参赛代码提交指南》规定：

> 「若需改动 nuttx 等公共仓库，则不在专属仓内直接 push，而是 fork 对应公共仓、以 PR
> 形式提交到 `dev-ai-contest-2026` 分支，由组委会 review 后合入。」

大赛的官方文档**没有**提供把公共仓改动同步回团队仓的机制（没有 patch 目录约定、没有
manifest revision 覆盖、也没有要求团队仓记录上游 PR）。团队仓能承载的只有文档，所以
就有了这一篇——**公共仓的代码在 PR 里，团队仓里的这份索引负责让它可被找到**。

同时也说明：团队仓里**不放**这些文件的副本。曾经放过（`fw_patches/`），是误判，已撤销，
原因见 `fw_patches/README.md`。

## 本作品在公共仓的改动

这三项都属于平台底层，是 PAN 上网能跑起来的前提条件。技术背景与完整证据链见
`21_pan_breakthrough_authoritative.md`，可复用规则见 `22_pan_engineering_guide.md`。

### 1. `open-vela/external_zblue` — net_buf 池注册

| | |
|---|---|
| 文件 | `port/sections/defines.c`（+28 行） |
| 本地 commit | `d9fb8207cc1` |
| 分支 | `pan/netbuf-pool-registration` |
| PR | _待创建_ |

zblue 的 NuttX port 用手写数组 `_net_buf_pool_list[]` 代替 Zephyr 的 linker section 收集
net_buf 池。`pool_id()` 遍历该数组反查指针，**找不到时静默返回 0**（`__ASSERT` 在
release 下被编掉）。任何未注册的池，其 buffer 会拿到 `_net_buf_pool_list[0]` 的
`max_alloc_size` **和它的 `data_pool` 基址**——尺寸不对，且写进别的池的存储区。

本次注册了 `pan_tx_pool`、`rfcomm_tx_pool`、`bt_avrcp_tx_pool`（追加在数组末尾，zblue
内部池下标不变）。其中 SPP 的 `rfcomm_tx_pool` 是既有缺陷，与本作品无关也一并修了。

> 这是三项里最值得单独 review 的：28 行，收益明确，且对**任何**在该 port 上自定义
> net_buf 池的项目都成立。

### 2. `open-vela/vendor_sifli` — 邮箱 ring 写入、ACL 长度上限、堆布局

| | |
|---|---|
| 文件 | `chips/sf32lb52/sf32lb52_bt_adapter.c/.h`、`sf32lb52_bth4.c`、`sifli_allocateheap.c`（4 文件，+116/−105） |
| 本地 commit | `db73380` |
| 分支 | `bletest` |
| PR | _待创建_ |

三件事必须同时生效才自洽，所以在一个 commit 里：

- **邮箱 ring 必须整帧一次写入。** `struct circular_buf` 的 `read_idx_mirror`（LCPU 写）
  与 `write_idx_mirror`（HCPU 写）在同一条 D-cache line，`ring_write()` 结尾的
  `up_clean_dcache()` 会把 HCPU 缓存里那份陈旧 read_idx 一起写回，**回退 LCPU 的读指针**
  → H4 解析失同步 → `Hardware error, hardware code: 0` → 停回 NoCP → ACL 信用耗尽 →
  TX 死锁。旧代码先单独写 1 字节 H4 type 再触发中断，精确打开了这个窗口。
- **合成的 `HCI_Read_Buffer_Size` 把 ACL 长度压到 487** = ring 可用 492 − H4 type 1 −
  HCI ACL 头 4，保证任何 ACL 包都能一次写完。更长的 L2CAP PDU 由 zblue 正常分片。
- **堆上界钉在 0x2007FB00。** 0x2007FB00 以上是 HCPU custom config + 两个
  HCPU2LCPU 邮箱 buffer；`up_allocate_heap()` 原来一直开到 0x20080000，而本工程
  `.bss` ~455 KB、堆只剩 ~32 KB，**确实会分配到邮箱上去**，LCPU 写 HCI 字节等于改写
  malloc 出来的对象。

> 后两项对所有基于 SF32LB52 的板子都成立，不只本作品。第三项还解释了本项目历史上
> 一系列「换个无关配置崩点就转移」的怪现象。

### 3. `open-vela/frameworks_bluetooth` — PAN TX 池与开机自动连接

| | |
|---|---|
| 文件 | `service/stacks/zephyr/sal_pan_interface.c`、`service/profiles/pan/panu_service.c`（+121/−34） |
| 本地 commit | `ec9ad5c5` |
| 分支 | `bletest`（该分支相对上游共 34 个 commit，是 PAN/BNEP 整条线） |
| PR | _待创建_ |

- BNEP TX 池按 1691 MTU 正确取到尺寸后，去掉调试期的诊断脚手架，改为单次分配 +
  100 ms 等待，并加 `tx_nobuf` 计数以区分「池耗尽」和「池坏了」。
- 池从 4 个 buffer 降到 2 个：每个约 1.7 KB `.bss`，而堆必须给邮箱让出空间（见第 2 项）。
  TAP 读循环一次只发一帧、buffer 在 ACL 完成时释放，深度只影响流水。
- 开机自动连接补两处：`last_nap` 缺失时从 bond list 回退取最近配对的 BR/EDR 设备；
  连接成功后回写 `last_nap`。此前旧固件配过对的表冷启动永远不会自动上网。

> 注意 PR 粒度：`bletest` 相对上游 34 个 commit，包含整条 PAN/BNEP 从零实现的调试历史
> （含若干后来被替代的中间方案）。发 PR 时可选择整条线一起提（体现完整工作量），
> 或另开分支只提 `ec9ad5c5` 这类最终结论（便于 review）。

## 提交状态与操作步骤

三个公共仓截至本文档写作时**都还没有 Sen70s fork**，改动已在本地各仓提交完毕。
fork / push / PR 的具体命令、patch 备份、以及注意别带上的构建产物，见工作区根目录
`upstream_patches/README.md`（该目录故意不在团队仓内，避免又变成代码副本）。

PR 创建后请回填上面三张表的「PR」行，让这篇索引保持可用。

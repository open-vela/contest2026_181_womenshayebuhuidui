# 19. SRAM 预算（Gate G）

> 日期：2026-08-20
> 依据：`out/nuttx_contest_board_ai_agent`（产品）与 `out/nuttx_contest_board_pandbg`（调试）
> 工具：`arm-none-eabi-size -A`、`arm-none-eabi-nm --size-sort -S`、`nuttx.map`、板上 `free`

## 结论

产品镜像静态占用 512 KB SRAM 的 **463,424 B / 88.39%**，剩下 60,864 B 里 IDLE 栈拿
16,096 B，堆只拿到约 44.5 KB；但 PSRAM 8 MB 与这段 SRAM 尾巴合成同一个 umm 堆
（`CONFIG_MM_REGIONS=2`），运行期分配绝大部分落在 PSRAM，板上 `free` 显示
8,433,248 B 总量、峰值只用 1,072,664 B。PAN 的运行期开销（每连接 1,514 B 收包缓冲、
1,514 B tun 读缓冲、4 KB DHCP 线程栈）走的是这个堆，不吃静态 SRAM，所以 PAN 打通
不会把 88.39% 顶上去。

## SRAM 分布

| 区间 | 大小 | 说明 |
|------|------|------|
| `sram` 区 | 0x20000000 + 0x80000 = 524,288 B | `nuttx.map` Memory Configuration |
| `.data` | 128,232 B | 其中 `g_allsyms` 一个人占 112,976 B |
| `.ramfunc` | 18,496 B | 必须驻留 RAM 的函数（flash/PSRAM 操作期间不能 XIP） |
| `.bss` | 316,672 B | 见下表 |
| 静态合计 | `_ebss - 0x20000000` = **463,424 B（88.39%）** | |
| IDLE 栈 | 16,096 B | `CONFIG_IDLETHREAD_STACKSIZE` |
| 堆（SRAM 部分） | ≈ 44,480 B | `up_allocate_heap()`：`g_idle_topstack` → `SRAM_END` |
| 堆（PSRAM 部分） | 8,388,608 B | `kumm_addregion(0x60000000, 0x800000)` |

板上 `free` 实测（开机 5 分钟、bluetoothd + ai_agent 已跑）：

```
      total       used       free    maxused    maxfree  nused  nfree name
    8433248    1070264    7362984    1072664    7361968    559      9 Umem
```

`8,433,248 − 8,388,608 = 44,640`，与算出来的 SRAM 尾巴对得上：两段是**同一个堆**，
`ps` 里能看到 bluetoothd 的线程栈一部分在 `0x2007xxxx`（SRAM 尾），一部分在
`0x600xxxxx`（PSRAM）。

## 前 15 大可写符号（产品镜像）

| 大小 (B) | 符号 | 归属 |
|---------:|------|------|
| 112,976 | `g_allsyms` | `CONFIG_ALLSYMS` 生成的符号表（.data，非 const） |
| 99,075 | `g_iob_buffer` | 网络 IOB：`IOB_NBUFFERS=64 × IOB_BUFSIZE=1534` |
| 32,768 | `sys_work_q_stack` | zblue sysworkq 静态栈 |
| 17,000 | `net_buf_data_hci_rx_pool` | zblue HCI RX 池 |
| 16,384 | `g_hp_work_stack` | NuttX hpwork |
| 16,384 | `g_lp_work_stack` | NuttX lpwork |
| 16,384 | `s_tls_raw_buf` | ai_agent TLS |
| 11,264 | `s_jobs` | ai_agent |
| 10,268 | `s_history` | ai_agent |
| 10,040 | `net_buf_data_rfcomm_tx_pool` | zblue RFCOMM/SPP TX 池 |
| 8,192 | `bt_lw_stack_area` | zblue "BT LW WQ" 静态栈 |
| 7,620 | `g_discovery_results` | 蓝牙扫描结果缓存 |
| 6,800 | `net_buf_data_pan_tx_pool` | **PAN BNEP TX 池（MTU 1691）** |
| 4,096 | `g_uart_buffer` | |
| 2,140 | `g_sf32lb52_bt_priv` | bth4 驱动私有区（含 IPC 环形缓冲） |

分类合计：`net_buf_data_*` 各池 39,378 B；所有 `*stack*` 静态栈 73,728 B。

## PAN 的账

- 静态：`net_buf_data_pan_tx_pool` 6,800 B（已含在 88.39% 里）。
- 运行期（堆，实际落 PSRAM）：每条连接的 `pan_conn_t` 内嵌 `rx_eth[1514]`；
  `pan_read_buf` 1,514 B（全局一份）；DHCP 线程栈 `PAN_DHCP_STACK_SIZE` 4,096 B；
  外加 `bt-pan` netdev 与 L2CAP/BNEP 通道结构。总量 10 KB 量级，对 7.3 MB 空闲堆
  可忽略。
- L2CAP MTU 固定 1691（HyperOS NAP 对 <1691 会回 UNACCEPTABLE_PARAMS），
  这条决定了 TX 池的尺寸，不能为省 SRAM 调小。

## 与调试镜像的差

pandbg = 463,552 B / 88.42%，只比产品多 128 B：`seq_rx`/`seq_tx` 两个 4 B 计数器
（ACL 全长 trace 用），以及 `g_allsyms` 因多出两个符号增长 136 B。
换句话说诊断开关（`DEBUG_HARDFAULT_ALERT`/`BUSFAULT`/`USAGEFAULT`/`ARCH_STACKDUMP`/
`BOARD_RESET_ON_ASSERT`）几乎不占 SRAM，代价在 flash 与串口带宽上。

## 顺带证明了一件事

`sys_work_q_stack`（32,768 B）+ `bt_lw_stack_area`（8,192 B）= 40,960 B。18 号记录里
那个「z_sys_init 被跑两遍」的问题，如果按「给第二组队列各分一块栈」去修，就要再掏
41 KB——而 SRAM 尾部一共只剩 44.5 KB，还得留给堆。所以幂等化是唯一现实的修法。

## 如果以后需要要回 SRAM（按性价比排序，本轮都没做）

1. `g_allsyms` 112,976 B。生成的 `allsyms_final.c` 里 `struct symtab_s g_allsyms[]`
   不带 `const`，所以落在 .data；表在运行期只读，改成 const 后整块留在 XIP flash，
   一把省 110 KB（占满打满的 SRAM 的 21%）。但生成器在 `nuttx/`，超出本轮授权范围，
   只记录不动。
2. `net_buf_data_rfcomm_tx_pool` 10,040 B：本项目不用 SPP，关掉 RFCOMM 即可回收，
   属 defconfig 范围。
3. `g_discovery_results` 7,620 B：扫描结果条数可配。
4. `s_tls_raw_buf` / `s_jobs` / `s_history` 合计 37,916 B 在 `packages/ai_agent`，
   未获授权修改。
5. `g_iob_buffer` 99,075 B 是 PAN 吞吐的直接依赖，不动。

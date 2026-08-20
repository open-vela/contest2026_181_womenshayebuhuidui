# 18. bluetoothd 开机即 HardFault 的根因：zblue SYS_INIT 表被跑了两遍

> 日期：2026-08-20
> 影响：产品镜像同样中招（不是调试镜像的副作用）

## 现象

rcS 改成自动 `bluetoothd &` 之后，每次开机约 2 s（`lcd_async_init` 还在 usleep、
`nsh_main` 还卡在 rcS 的 `sleep 2`）板子就断言：

```
Assertion failed panic: at file: /arch/arm/src/arm_m/arm_hardfault.c:186
    task: BT LW WQ process: bluetoothd
```

断言 dump 里的 PC/LR 都是 `0x1201b1eb`（`_assert` 自己），因为
`DEBUG_HARDFAULT_ALERT` 没开，`hfalert()` 打的 CFSR/HFSR/BFAR 和真正的故障 PC
全被编译掉了；此外 assert 之后没有复位，控制台彻底静默，只能物理拔插 USB 才
能再烧录。

## 证据：dump_tasks 里出现了两组同栈线程

`dump_tasks` 是这次定位的关键（原始日志 `logs/boot_after_flash.log`）：

| PID | GROUP | COMMAND   | STACKBASE  | STACKSIZE |
|-----|-------|-----------|------------|-----------|
| 5   | 0     | sysworkq  | 0x20053308 | 32688     |
| 6   | 0     | BT LW WQ  | 0x200497c0 | 8112      |
| 9   | 8     | sysworkq  | 0x20053308 | 32688     |
| 10  | 8     | BT LW WQ  | 0x200497c0 | 8112      |

group 0 是开机初始化任务，group 8 是 `bluetoothd`。两组线程的 STACKBASE **完全
相同** —— 同一段 8112 B 的栈上跑着两个线程，崩的就是后建的那个（PID 10）。

## 机制

1. zblue 的 `k_sys_work_q_init` / `long_wq_init` 是 `SYS_INIT` 表项，栈来自
   `K_THREAD_STACK_DEFINE()`，即每个队列**一块静态数组**。
2. zblue 的 NuttX 移植层 `port/kernel/thread.c:k_thread_create()` 用
   `pthread_attr_setstack(&pattr, stack, stack_size)` 把这块静态数组直接交给
   `pthread_create`。所以同一表项跑第二遍 = 第二个线程用同一块栈。
3. `k_work_queue_start()` 还会重新初始化队列的信号量，而第一个线程正阻塞在上面
   （`work_queue_main` → `z_sched_wait` → `k_sem_take`），把带等待者的信号量
   memset 掉，第二个线程一碰队列就 HardFault。
4. 谁跑了两遍：
   - `vendor/sifli/chips/sf32lb52/sf32lb52_bth4.c:sf32lb52_bt_initialize()`
     在注册 `/dev/ttyHCI0` 之后调 `z_sys_init()`（开机，group 0）；
   - `frameworks/.../stacks/zephyr/sal_adapter_interface.c:bt_sal_init()`
     再调一次（bluetoothd，group 8）。

## 为什么这套线程必须属于 bluetoothd

NuttX 的文件描述符表在 task group 上，pthread 跟着父任务共享。HCI 的 fd 是
`h4_open()` 里 `open("/dev/ttyHCI0")` 拿的，发生在 `bt_enable()` 之后，也就是在
bluetoothd 的 group 里。如果工作队列线程留在 group 0，队列里任何要写 HCI 的
work（`bt_hci_cmd_send_sync` 之类）就会在错误的 fd 表上 `write()`。所以保留下来
的那一次 `z_sys_init()` 必须是 bluetoothd 的那次。

顺带一提：`h4_init()`（设备表项的 init_fn）只打一行日志，fd 和 RX 线程都在
`h4_open()` 里创建，所以驱动侧完全不需要提前跑这张表。

## 修复

| 文件 | 变更 |
|------|------|
| `vendor/sifli/chips/sf32lb52/sf32lb52_bth4.c` | 删掉 `sf32lb52_bt_zblue_init_once()` 及调用，只保留 `uart_bth4_register()`（提交 `05abc09`） |
| `external/zblue/zblue/port/kernel/init.c` | `z_sys_init()` 记住调用者 pid，owner 仍存活就直接返回 |

第二处刻意**不用**简单的 `static bool`：这些线程是调用者的 pthread，会随进程
一起死，一次性标志会让重启后的 bluetoothd 完全没有工作队列。改成
「owner 进程还活着就跳过，死了就重跑」（`sched_getparam(owner)` 探活，无副作用）。
残留风险是 pid 复用导致误判存活，代价是 BT 要等一次重启，不会崩。

## 连带发现（暂不动）

`sf32lb52_bth4.c` 里两处历史注释提到把 SSP 全部搬进 bth4 是为了绕开
「zblue sysworkq HardFault when bt_hci_cmd_send_sync is called from ...」。
两个 sysworkq 共用一块 32 KB 栈，正好能解释那类崩溃。按「最小修复先行」的路线，
这些 workaround 本轮保持原样，等 PAN 全链路稳定后再评估回收。

## 调试镜像同时打开的诊断开关（提交 `6487ab6`）

`CONFIG_DEBUG_HARDFAULT_ALERT` / `DEBUG_BUSFAULT` / `DEBUG_USAGEFAULT` /
`ARCH_STACKDUMP` / `BOARDCTL_RESET` / `BOARD_RESET_ON_ASSERT=2`。
后两个让 assert 自动重启（`board_reset()` → `up_systemreset()`，本板早就实现，
只是没编进去），好处有两个：崩溃日志可重复，且每次重启都重新打开 SFBL 下载窗口，
崩掉的板子不必再物理拔插 USB 才能烧录。`DEBUG_MEMFAULT` 依赖 `ARCH_USE_MPU`，
本工程是 flat build 无 MPU，开 MPU 会改变调试镜像要复现的内存行为，故不开。

## 验证方法

烧录后 `ps`：`sysworkq` 与 `BT LW WQ` 各只应有一份，且 GROUP 等于 `bluetoothd`
的 group；`bluetoothd` 与 `ai_agent` 随 rcS 自启。

## 验证结果（2026-08-20，pandbg 镜像，日志 `logs/boot_faultdbg.log`）

```
  PID GROUP PRI POLICY   TYPE    NPX STATE    EVENT   STACK COMMAND
    6     6 100 RR       Task      - Waiting  Sem     0008088 bluetoothd
    7     6 110 FIFO     pthread   - Waiting  Sem     0032688 sysworkq
    8     6  10 FIFO     pthread   - Waiting  Sem     0008112 BT LW WQ
    9     6 103 RR       pthread   - Waiting  Sem     0008112 bt_service_6
   12    12 100 RR       Task      - Waiting  Signal  0032664 ai_agent -d
```

- 各一份，都在 group 6（bluetoothd）；开机不再有任何 Assertion / HardFault。
- `bluetoothd` 与 `ai_agent -d` 均由 rcS 自启。
- `bttool` → `enable` 全程正常：adapter state 1→2→3→4，
  `[pan] BNEP server register (psm 0x000f) ret=0`、
  `[pan] PANU SDP record register ret=0`，
  `Adapter Name: Agent-Watch-cd:ab:78:56:34:12, Class: 0x002A0704, Mode:1`
  （bth4 的 R92 补丁把下发给 LCPU 的 CoD 改写成 0x020510/PANU）。
- `set scanmode 2` 成功（Write_Scan_Enable 0x03，inquiry + page scan 都开），
  手机可发现。
- `/data/misc/bt/bt_storage.db` 存在（12288 B），bond 数据库跨重启保留。
- `pan` 子命令：`pan connect <addr> <dstrole> <srcrole>`、`pan disconnect <addr>`、
  `pan dump`。
- 注：`enable` 之后 group 6 里会多出一个名为 `sysworkq`、栈 8112 B 的线程
  （pid 42，栈基址与 pid 7 那块 32688 B 静态栈不同），是 SAL 侧另建的队列线程，
  与本文修的重复初始化无关。

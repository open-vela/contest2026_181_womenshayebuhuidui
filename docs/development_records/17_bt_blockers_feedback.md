# 17. 蓝牙联网方案阻碍点汇总（反馈组委会）

日期：2026-08-13
队伍：Sen70s (#181) | 硬件：SF32LB52-DevKit-LCD（双模蓝牙 5.3，无 WiFi）
用途：向组委会反馈 5 条联网路径的无法执行点，请求技术支持

## 一、30 秒读懂（总体一览）

**我们要做什么**：让 SF32LB52（无 WiFi）通过蓝牙真机联网，跑通 ai_agent LLM 对话。

**现状**：5 条路径逐条验证后，**每条都在不同环节被阻断**——不是缺代码，而是框架缺失（PAN）、固件行为异常（BREDR 模拟、ext adv 假成功）、内存破坏（BLE 连接）和硬件约束（USB）层层叠加。

| # | 方案 | 卡点环节 | 一句话结论 | 能否自解 |
|---|---|---|---|---|
| 1 | 手机蓝牙共享（PAN） | 框架层 | PAN SAL 全工程缺失，设备无法加入手机网络 | 需官方实现 |
| 2 | 经典蓝牙 SPP | 驱动层 | 30+ 条 HCI 命令被 vendor 驱动模拟，从未到控制器 | 需厂商澄清 |
| 3 | BLE GATT NUS（官方 App 路线） | 内存层 | 广播已修复✅；连接建立被预存内存破坏阻断❌ | 需官方协助定位 |
| 4 | USB RNDIS（非蓝牙备选） | 硬件层 | USB 控制器被串口独占，composite 端点不足（4<6） | 硬件不可行 |
| 5 | 蓝牙 SPP 代理（官方 AI_AGENT_BLE_NET） | 驱动层 | 依赖 BREDR SPP，同方案 2 阻断 | 同 2 |

## 二、统一监测点定义（下文所有方案共用）

每个方案的验证都经过以下 5 个标准监测点，任一监测点失败即链路不通：

| 监测点 | 监测内容 | 监测手段 | 通过标准 |
|---|---|---|---|
| **M1 命令链路** | HCI 命令是否真实下发、控制器是否正常响应 | 驱动层 trace（bth4 tx/recv 打印完整字节） | 命令出现在 tx 日志，且 CMD_COMPLETE status=0x00 |
| **M2 空口可见性** | 广播/信号是否真实到达空中 | 手机 nRF Connect + 系统蓝牙双通道扫描 | 扫描列表可见设备名与 UUID |
| **M3 连接建立** | 对端连接请求是否被接受并上报 | 设备端 HCI 事件 trace（0x3E LE Meta）+ GATT 连接日志 | 出现连接完成事件 + on_connected 日志 |
| **M4 数据流** | 连接后的数据帧是否正常收发 | 双方收发日志 + 帧协议校验 | 帧往返完整、无错位 |
| **M5 稳定性** | 长时间运行无崩溃 | 连接保持 5-10 分钟观察 | 无 hardfault / dumpstack / 挂死 |

## 三、方案 1：手机蓝牙网络共享（PAN/BNEP）

**做什么**：手机开"蓝牙网络共享"，设备以 PANU 身份经 BNEP over L2CAP 接入手机网络（手机系统自带能力，无需 App）。

**监测点结果**：

| 监测点 | 结果 | 说明 |
|---|---|---|
| M1 | ⚠️ 未执行 | PAN 初始化代码不存在，无命令可下发 |
| M2-M5 | ❌ 不可达 | 框架层缺失，后续环节全部无法进入 |

**阻碍点**：openvela 框架有 PAN API 头（`bt_pan.h`）和 profile 骨架（`panu_service.c`），但 **SAL 适配层（`sal_pan_interface.h` / `bt_sal_pan_*`）全工程无实现**，`panu_service` 未编译。官方 dev 分支核实同样缺失。自实现 BNEP 需 600-1000 行 + 框架改动。

**冲突范围**：PAN 属 BREDR 链路，与 BLE（方案 3）无资源冲突；其缺失影响所有"系统级蓝牙共享"场景，不影响方案 3。

**诉求**：官方提供 PAN SAL 实现或 BNEP over L2CAP 参考移植。

## 四、方案 2：经典蓝牙 SPP（含方案 5 的 SPP 代理）

**做什么**：BREDR SPP 通道承载 IP 包（官方 `AI_AGENT_BLE_NET` 的"手机 App 蓝牙 SPP 代理上网"路线，方案 5 与之共用此链路）。

**监测点结果**：

| 监测点 | 结果 | 说明 |
|---|---|---|
| M1 | ❌ 失败 | 30+ 条 BREDR 命令（Write_Scan_Enable、Write_Local_Name、Write_Class_of_Device、Set_Event_Mask 等）被 `sf32lb52_bt_emulate_cmd()` 本地模拟 status=0，**从未下发 LCPU**（tx trace 无这些命令） |
| M2-M5 | ❌ 不可达 | 控制器未收到扫描使能等命令，BREDR 可发现性/连接参数从未真实设置 |

**阻碍点**：vendor 驱动 `sf32lb52_bth4.c` 的模拟层使 BREDR 链路的命令路径长期"假成功"，BREDR 端到端能力无法验证。

**冲突范围**：影响所有 BREDR 路线（SPP 直连、SPP 代理、PAN 的底层 ACL）；不影响 BLE 链路（方案 3）。

**诉求**：思澈厂商对 LCPU 固件 BREDR 支持情况与 bth4 模拟层的官方说明（哪些命令应真实下发、为什么模拟）。

## 五、方案 3：BLE GATT NUS 数据通道（官方 App com.agent.coapp 路线）

**做什么**：官方配套 App 通过 BLE GATT NUS（UUID 6e400001）与设备通信，设备端 TUN + GATT 帧隧道，App 转发上网。

**监测点结果**：

| 监测点 | 结果 | 说明 |
|---|---|---|
| M1 | ✅ 通过 | 9 条初始化命令全部真实下发且 CMD_COMPLETE status=0x00（0x2001/0x2006/0x2008/0x2009/0x200A 均在 trace 中） |
| M2 | ✅ 通过（本队修复） | 原 ext adv（0x2039）被 LCPU 假成功（status=0 但空口无信号）；改为 legacy 广播后 nRF Connect 实测：**Advertising type: Legacy + Agent-Watch + NUS UUID + RSSI -43dBm** |
| M3 | ❌ **卡点** | 手机 connectGatt 后 45 秒设备端零连接事件（无 0x3E） |
| M4 | ❌ 不可达 | 连接未建立 |
| M5 | ❌ 不可达 | 同上 |

**M3 失败的证据链（字节级）**：

```
① zblue 计算 LE_SET_EVENT_MASK 掩码 = 0x020f（bit0=LE 连接完成事件，正确）
② sys_put_le64 写入 net_buf 后立即读回 = 0f 02 00...（正确）
③ 驱动层发送时 = 08 0f 02...（被破坏：0x020f → 0x0f08）
④ 错误掩码下发 LCPU → LCPU 屏蔽连接完成事件（0x3E/0x01）
⑤ 手机连接永远无法建立
```

**结论**：zblue 命令 net_buf 在写入正确值后、发送前被**预存内存越界写**破坏。同源证据：inode 链表被破坏（BFAR=0x00100121 hardfault）、关闭 GATT_CLIENT 触发布局敏感崩溃（恢复即消失）、崩溃点随机——指向持续性越界写。

**冲突范围（重要）**：该内存破坏源影响 zblue 的**所有** net_buf 数据通道（HCI 命令池、ACL 收发池）——即使当前打通连接，M4 数据流仍可能被同一破坏源污染（历史先例：连接后 bluetoothd uv__io_poll hardfault）。因此这不只是"连接问题"，是**整个 BLE 数据面**的阻断点。

**诉求（最高优先级）**：官方协助定位 zblue 在 NuttX 上的内存破坏源（net_buf 池管理 / 引用计数 / 与 LCPU IPC 交互），或提供 SF32LB52 BLE peripheral 连接建立的可运行参考配置。

## 六、方案 4：USB RNDIS（非蓝牙备选）

**做什么**：设备 USB 虚拟网卡直连电脑，有线高速联网（12Mbps）。

**监测点结果**：

| 监测点 | 结果 | 说明 |
|---|---|---|
| M1（USB 枚举） | ❌ 失败 | 电脑侧枚举超时（error -110/-71）；设备端 RNDIS netdev 注册成功（eth1 出现）但 `usbdev_register` 返回 -EBUSY |
| 网络连通 | ❌ 不可达 | 同上 |

**阻碍点与冲突范围（方案间冲突典型）**：
1. 芯片**只有一个 USB 控制器**（on-package），当前被 **CDC ACM 串口独占**（烧录/调试依赖 ttyACM0）——RNDIS 注册与串口互斥
2. 两个 Type-C 口**并联同一控制器**（实测插口对调 ttyACM0 不消失），不是独立通道
3. **Composite（串口+网卡共存）端点不足**：控制器仅 4 端点（EP0+EP1/2/3），RNDIS(3)+CDCACM(3) 需 6 端点，硬件不可行
4. 禁用串口换 RNDIS 不可接受（烧录走串口，无串口无法再烧录）

**诉求**：无（硬件约束，仅记录）。

## 七、方案间冲突关系总图

```
LCPU 蓝牙控制器
 ├─ BLE 链路（方案 3）── 广播✅ 连接❌（net_buf 破坏，影响所有 zblue 数据面）
 └─ BREDR 链路（方案 1/2/5）── 命令被驱动模拟（方案 2）＋ PAN SAL 缺失（方案 1）

芯片 USB 控制器（唯一，on-package）
 ├─ CDC ACM 串口（烧录/调试，必须保留）
 └─ RNDIS 网卡（方案 4）── 与串口互斥；composite 端点不足 4<6，硬件不可行
```

## 八、诉求汇总（按优先级）

| # | 诉求 | 对应方案 | 优先级 |
|---|---|---|---|
| 1 | zblue/NuttX net_buf 内存破坏源定位支持 | 方案 3 | **最高**（阻断所有 BLE 数据通道） |
| 2 | PAN SAL 实现 / BNEP 参考移植 | 方案 1 | 高 |
| 3 | 思澈 LCPU 固件 BREDR 命令支持说明 + bth4 模拟层澄清 | 方案 2/5 | 高 |
| 4 | SF32LB52 BLE peripheral 连接建立参考实现/配置 | 方案 3 | 高 |

## 附：本队已沉淀的可复用成果

- legacy 广播修复（zblue `20ebb784582`、vendor `70777ae`/`b994695`、packages/ai_agent `d0e2d26`）
- App 端 BLE 隧道完整实现（BleTunnelManager/TunnelService/TcpProxy，前台服务合规修复）
- 诊断文档 11-16 号（HCI trace 方法论、掩码证据链、net_buf 破坏实锤、监测点定义）

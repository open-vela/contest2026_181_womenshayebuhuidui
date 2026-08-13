# 16. BLE 连接建立排查：事件掩码与 net_buf 破坏实锤

日期：2026-08-13
状态：连接建立根因链已完整定位，待执行 workaround + E1 探针

> 承接 14/15 号记录。广播已空口验证（legacy ADV_IND），本记录覆盖「手机连接建立」阶段的完整排查。

## 1. 多智能体诊断结论（App 端 + 设备端）

### 1.1 App 端（子代理审查，4 项）

| # | 问题 | 根因 | 状态 |
|---|---|---|---|
| 1 | 启动隧道约 30 秒闪退 | `TunnelService.startForeground()` 只在 BLE CONNECTED 分支调用，连接失败时 30 秒内未调用 → Android 12+ 抛 ForegroundServiceDidNotStartInTimeException | 已修复（提前到 startTunnel） |
| 2 | 修复 1 后"立即闪退" | startForeground 提前执行后暴露 Manifest 未声明 `foregroundServiceType` → MissingForegroundServiceTypeException | 已修复（声明 connectedDevice + 带类型重载 + notify 更新通知） |
| 3 | UI 永远"正在连接" | onConnectionStateChange 不检查 status（133 超时无 ERROR 分支）；ViewModel 轮询循环 service==null 即 break | 已修复 status 检查；轮询待改 |
| 4 | 服务卡死 | DISCONNECTED 分支 running 守卫导致失败路径不清理 | 待改（非阻塞） |

### 1.2 设备端（子代理审查，3 项）

| # | 假设 | 验证结果 |
|---|---|---|
| 1 | LE_SET_EVENT_MASK(0x2001) 被驱动模拟吞掉 → LCPU 事件掩码未设置 | 部分成立：已移出模拟列表真实下发，**LCPU 正常响应 status=00**（标准控制器） |
| 2 | adv.c 团队补丁 -ENOMEM 豁免为死代码（ONE_TIME 条件恒真） | 确认（影响连接池紧张场景） |
| 3 | LCPU 上报 enhanced(0x0B) vs legacy(0x01) 子事件与 host 掩码不匹配 | 指向最终根因（见第 3 节） |

## 2. 已执行修复

### 设备端
1. `sf32lb52_bth4.c`：`LE_SET_EVENT_MASK` 移出模拟列表真实下发（git `70777ae`）
2. `sf32lb52_bth4.c`：RX trace 增强——打印完整 8 字节 + 0x3E LE Meta 标记（git `b994695`）
3. `hci_core.c`：LE event mask 无条件同时使能 bit0（legacy CONN_COMPLETE）+ bit9（ENH_CONN_COMPLETE）（git `20ebb784582`）
4. `defconfig`：`CONFIG_BT_MAX_CONN=2`（git `5501c11`）

### App 端（无 git，com.agent.coapp-main 目录未纳入版本管理）
1. `TunnelService.kt`：startForeground 提前 + 带类型重载 + notify 更新
2. `BleTunnelManager.kt`：连接失败 status 检查 → ERROR 状态机
3. `AndroidManifest.xml`：TunnelService 声明 `foregroundServiceType="connectedDevice"`

## 3. 决定性 HCI 证据链

### 3.1 LCPU 是标准控制器（初始化全链路 status=00）

```text
TX 0x0C03 RESET / 0x1003 / 0x1001 / 0x2001 / 0x1009 / 0x2006 / 0x2008 / 0x2009 / 0x200A
RX 全部 CMD_COMPLETE status=00（含 0x2001，证明 LCPU 支持事件掩码命令）
```

### 3.2 内存破坏实锤（本次最大发现）

```text
[hci_core] LE event mask=0x20f bit0=1 bit9=200        ← zblue 计算正确
[hci_core] events after put: 0f 02 00 00 00 00 00 00  ← sys_put_le64 写入后读回正确
sf32lb52 bth4 tx: 01 01 20 08 0f ...                  ← 发送时变成 08 0f（破坏！）
```

**结论**：zblue 的 net_buf 命令缓冲在 `sys_put_le64` 写入正确值后、`bt_send` 发送前被越界写破坏（0x020f → 0x0f08）。这是项目预存内存破坏 bug 的又一实锤（与此前 inode 链表被破坏 BFAR=0x00100121、布局敏感崩溃同源）。掩码错误下发 → LCPU 屏蔽 legacy 连接完成事件（bit0）→ 手机连接永远无法建立（设备日志无任何 0x3E 事件）。

## 4. 多智能体校对结论（workaround 方案）

### 4.1 实现可行性（子代理确认）

- 驱动层拦截 0x2001 重写 `data[3..10]=0x0f 0x02 0...`（掩码 0x020f 小端）位置正确、偏移正确
- 掩码 0x020f = bit0/1/2/3/9，与 zblue 计算值精确一致，无副作用（全工程唯一发送者）
- 需补 `len >= 11` 守卫；插入位置在 emulate 检查之后、IPC memcpy 之前

### 4.2 风险警告（子代理 2）

- workaround 治标不治本：连接建立后 ACL/GATT 数据流走同一 net_buf 池，破坏源仍在则数据流大概率被污染 → "连上后间歇性 hardfault"（历史先例：BLE 连接触发 bluetoothd uv__io_poll 崩溃）
- 破坏模式评估：越界写最可能（65%）、ref 计数错误次之（25%）、FIFO 损坏低（10%）
- IPC ring buffer 为 CPU memcpy 非 DMA，已排除 ring DMA 嫌疑

### 4.3 定位破坏源实验（E1-E4）

1. **E1（最优先）**：le_set_event_mask 与 hci_core_send_cmd 打印 buf 地址 + data 指针 + 内容——一次烧录锁定破坏环节（越界写/指针错/FIFO 错）
2. E2：net_buf 池加 redzone 定位越界方向和 pool
3. E3：IPC ring_write 边界断言、RX 溢出分支断言
4. E4：缩小调度窗口观测破坏相关性

## 5. 下一步方案（待用户确认）

**一次烧录做两件事**：
1. workaround：驱动层强制 0x2001 掩码（标注 TEMP，打通连接链路）
2. E1 探针：打印 buf/data 指针二分破坏时刻（为根治收集数据）

预期：连接建立成功（App 隧道打通），同时拿到破坏源定位数据；连接后若数据流被破坏则立即转向根治。

## 6. git 检查点清单（本轮）

| 仓库 | 检查点 | 内容 |
|---|---|---|
| vendor/sifli | `70777ae` | LE_SET_EVENT_MASK 移出模拟列表 |
| vendor/sifli | `b994695` | RX trace 增强（8 字节 + 0x3E 标记） |
| zblue | `20ebb784582` | 事件掩码双位使能 |
| zblue | `a396deb0999` | 团队 zblue 修改快照 |
| contest2026_181 | `5501c11` | BT_MAX_CONN=2 |
| contest2026_181 | `4fe2840` | 文档/任务快照 |
| packages/ai_agent | `eb7c2ac` | BLE GATT 网络桥 + UI 快照 |

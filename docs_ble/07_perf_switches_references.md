# 07 BLE 吞吐性能开关与参考文献（Round 3，2026-08-15）

> 内容：zblue 栈内发现的性能开关 + 公开文献要点。真机试验项，勿在未测前盲目开启。

## zblue 栈中未启用的性能开关（当前 defconfig 均为 not set）

| 开关 | 作用 | Zephyr 默认 | 建议 |
|------|------|------------|------|
| CONFIG_BT_GAP_AUTO_UPDATE_CONN_PARAMS | 外设连接 **5 秒后自动发起连接参数更新请求**（GAP 推荐做法） | y（本构建显式关了） | 真机试验项 1 |
| CONFIG_BT_GAP_PERIPHERAL_PREF_PARAMS | 在 GAP 服务加 PPCP 特征，声明外设偏好连接参数 | y | 配合上一项；默认 MIN_INT=24(30ms)/MAX_INT=40(50ms) |
| CONFIG_BT_AUTO_DATA_LEN_UPDATE | 连接建立时自动发起 **DLE（数据长度扩展 27→251 字节）** | 视 BT_USER_DATA_LEN_UPDATE | 真机试验项 2（需控制器支持） |

含义：BLE 吞吐 ≈ 连接间隔 × 每间隔包数 × 载荷。Android 作为 central 通常用默认
连接参数（30ms 级别），外设不主动请求参数更新就永远跑在默认值；DLE 决定单包
空口长度。两者对吞吐影响最大，而 MTU(247) 只决定 ATT 层分片大小。

⚠️ LCPU 固件闭源且有怪癖（ext-adv 空发、BREDR 不支持等），上述开关可能在真机
上引发异常（如连接参数更新不被响应、DLE 协商失败）。**列为真机试验矩阵，不预开启。**

## 公开文献要点（2026-08-15 检索）

### NUS 吞吐量
- [Nordic DevZone: NUS (Nordic UART Service) maximum rate](https://devzone.nordicsemi.com/f/nordic-q-a/29110/nus-nordic-uart-service-maximum-rate/115568)
  — NUS 是通用串口服务，速率瓶颈在连接参数/MTU/DLE，而非服务本身。
- [Nordic DevZone: How to extract maximum throughput from the peripheral_uart example](https://devzone.nordicsemi.com/f/nordic-q-a/84941/how-to-extract-maximum-throughput-from-the-peripheral_uart-example/354089)
  — 提升要点：连接间隔、每间隔通知数、DLE、MTU 协商。
- [Nordic DevZone: Maximising BLE throughput on iOS vs Android](https://devzone.nordicsemi.com/f/nordic-q-a/113875/maximising-ble-throughput-on-ios-vs-android?ReplySortBy=CreatedDate&ReplySortOrder=Descending)
  — 双平台行为差异：Android 每连接间隔通知数限制更严。

### 连接间隔是最大坑
- [my BLE GATT throughput capped at 4KB/s and the bug was connection interval](https://www.hotmolts.com/post/my-ble-gatt-throughput-capped-at-4kbs-and-the-bug--a491593b-b058-4f89-901d-ba8f1d19f950)
  — 实测案例：吞吐被连接间隔锁死在 4KB/s，调参数后大幅提升。与我们场景直接相关。

### Android 侧
- [Android 分批发送蓝牙数据 MTU请求和服务发现的顺序问题](https://blog.csdn.net/xyzroundo/article/details/152858520)
  — Android requestMtu 与服务发现时序细节（我们的 App 在发现后请求 MTU，属常见做法）。
- [BLE延迟优化实战：从协议栈原理到Android低功耗蓝牙性能调优](https://devpress.csdn.net/avi/6994ac8254b52172bc5c3202.html)
  — Android 侧优化全景。

### PAN 佐证（Round1 结论补充）
- [蓝牙PAN协议——不用WiFi，蓝牙也能上网？](https://yunthinker.com/648.html) /
  [经典蓝牙协议PAN详解](https://m.elecfans.com/article/4058777.html)
  — PAN(BNEP) 属**经典蓝牙 BREDR**，与 BLE 无关 —— 佐证本板 PAN 不可行结论。
- [Nordic DevZone: How to TCP/IP over BNEP/PAN](https://devzone.nordicsemi.com/f/nordic-q-a/25447/how-to-tcp-ip-over-bnep-pan-personal-area-network/100302)
  — BREDR PAN 的 TCP/IP 承载示例。

## 真机试验矩阵（挂在 docs_ble/04 验证计划后）

| 试验 | 改动 | 期望 | 风险 |
|------|------|------|------|
| 基线 | 当前固件（MTU 247 分片） | 链路通，吞吐 ~4-20KB/s | - |
| +连接参数 | CONFIG_BT_GAP_AUTO_UPDATE_CONN_PARAMS=y（默认 30-50ms） | 吞吐提升 | LCPU 不响应参数更新 |
| +PPCP 激进 | MIN_INT=12(15ms)/MAX_INT=20(25ms) | 进一步提升 | 连接不稳/断链 |
| +DLE | CONFIG_BT_AUTO_DATA_LEN_UPDATE=y | 空口单包 251B | 控制器不支持时协商失败（无害） |

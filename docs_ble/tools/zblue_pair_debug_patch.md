# zblue 配对诊断补丁（gitignored，不入库）

日期：2026-08-15 晚
文件：apps/external/zblue/zblue/subsys/bluetooth/host/classic/{ssp.c, br.c}
（external/zblue 为同一仓库，两份内容已同步）

## 改动

### ssp.c bt_hci_link_key_req()（Link_Key_Request 处理）
- syslog(6, "[zblue] link_key_req %s", addr) 入口
- conn 查找失败：syslog(3, "[zblue] link_key_req: NO CONN - neg reply")
- 无 key：syslog(4, "[zblue] link_key_req: no key - neg reply")

### br.c bt_hci_conn_req()（BR 连接请求处理）
- syslog(6, "[zblue] conn_req from %s type 0x%02x", ...) 入口
- add_br 失败：syslog(3, ...) / accept 失败：syslog(3, ...) / 成功：syslog(6, "accepted")

注意：syslog 级别用数字（3=ERR 4=WARN 6=INFO）——zblue port log.h 会 undef LOG_ERR
且 LOG_* 是函数式宏，syslog(LOG_ERR,...) 不展开会编译失败。

## 配套配置

defconfig：CONFIG_BT_DEBUG_LOG=y + CONFIG_BT_DEBUG_LOG_LEVEL=6（zblue 栈日志到 syslog）。

## 目的

定位 BR 配对中 Link_Key_Request 事件处理链（事件到 zblue 但 Neg_Reply 未发出的问题）。

---

## R53 追加：加密事件丢失三级诊断（2026-08-15 深夜）

### nuttx/drivers/serial/uart_bth4.c（已提交 688aa576924）
- uart_bth4_receive 的 circbuf 空间不足分支加 syslog：
  `uart_bth4 rx dropped: circbuf full (type=%u len=%zu space=%u)`
  —— circbuf 溢出时原本静默丢帧（-ENOMEM），这是 EncryptChange 帧丢失的候选根因。

### apps(frameworks)/connectivity/bluetooth/service/stacks/zephyr/hci_h4.c（gitignored）
- 增加 `#include <syslog.h>`
- bt_sal_hci_transport_recv 每帧拆出后打印：
  `[h4] frame type=0x%02x evt=0x%02x len=%d`
- get_rx 返回 NULL 时打印：
  `[h4] DROP frame type=0x%02x evt=0x%02x (no buf)`
  —— BT_LOGD/BT_LOGE 均为 no-op（CONFIG_BLUETOOTH_SERVICE_LOG_LEVEL 未启用），原实现静默丢帧。

### apps/external/zblue/.../host/hci_core.c（gitignored）
- rx_work_handler 入口加：`[zblue] rx_work enter`

---

## R54 追加：SSP/加密路径修正（2026-08-16 凌晨）

### 重大认知修正
- **`04 13` 是 Number-of-Completed-Packets（合法事件：num handle count），不是 EncryptChange！**
  R52 的"EncryptChange 归一化"把 NCP 的 num 从 1 改成 0，破坏合法事件。已删除。
- 真正的 Encryption Change 事件码是 **0x08**——LCPU 流程中从未出现（认证失败路径不产生加密）。
- 认证流程（R53 日志实证，全部标准格式）：
  Connection Complete (0x03) -> Auth Requested (0x0411, CS status=0) ->
  Link Key Request (0x17, len=6 标准) -> zblue neg reply (0x040c) ->
  **Authentication Complete (0x06) status=0x05 失败** -> Disconnection Complete (0x05 reason=0x16)。

### vendor/sifli/chips/sf32lb52/sf32lb52_bth4.c（gitignored）
- 删除 `04 13` 归一化块
- 增加跟踪（不修改）：`EVT 0x08 encrypt-change`（真加密事件）和
  `ReadLocalFeatures rsp status=.. feat: ..`（0x0403 响应，BT_FEAT_BREDR 判定依据）

### external/zblue/.../host/classic/br.c（gitignored）
- bt_br_init 入口：`[zblue] br_init enter`
- WRITE_SSP_MODE 后：`[zblue] br_init: WRITE_SSP_MODE ret=%d`
  （确认 SSP 初始化是否执行/成功——若 SSP 未启用，LCPU 只能走 legacy PIN，
  而 HyperOS 3.0.1.0 无 legacy PIN 弹窗 -> 认证必然失败）

---

## R55/R56 追加：SSP 事件追踪 + PAN 跳过加密（2026-08-16）

### 关键事实（R54 日志实证）
- **Write_SSP_Mode (0x0c56) 成功**（CC status=0x00），但 LCPU 认证时仍发
  Link_Key_Request (0x17) -> zblue neg reply -> Auth Complete (0x06) status=0x05 失败 -> 断开。
  **LCPU 的 SSP 流程未真正工作**（无 IO_CAPA_REQ 0x22/0x31、无 User Confirm）。
- 连接建立后 LCPU 发过 `04 23 0d`（13 参数，语义不明）——zblue 的事件号体系
  （IO_CAPA_REQ=0x31, RESP=0x32, USER_CONFIRM=0x33）与标准（0x22/0x23/0x24）不同，
  且 LCPU 发的 0x23 到达 hci_h4 后**未进入 hci_event**（无 Unhandled 打印）——
  中间某处静默丢失（bt_recv 日志 R56 验证）。
- **R55 策略**：PAN worker 跳过 set_security（BNEP 无需加密链路），
  sal_pan_interface.c 加 `#define PAN_SAL_SKIP_SECURITY 1`（可改 0 恢复）。

### external/zblue/.../host/classic/ssp.c（gitignored）
- io_capa_resp/io_capa_req/user_confirm_req 入口加 syslog（字段名注意：
  resp 有 capability/oob_data/authentication 无 status；req 只有 bdaddr）

### external/zblue/.../host/hci_core.c（gitignored）
- bt_recv_unsafe 加逐事件日志：`[zblue] bt_recv type=%u evt=0x%02x len=%u`

### hci_h4.c（gitignored）
- buf_tailroom 不足分支改 syslog（BT_LOGE 是 no-op）：`[h4] DROP tailroom`

---

## R57：SSP 事件桥 + rx_work 排空（2026-08-16）

### 根因（R56 日志实证）
1. **0x23 事件进入 bt_recv 但滞留 rx_queue**：rx_work_handler 单事件+自提交模式
   在本移植上丢事件（handler 运行中 k_work_submit 不保证重调度）。
   R57 改为 while 循环排空整个队列。
2. **事件号体系不匹配**：LCPU 用标准号（0x23 IO_CAPA_REQ / 0x24 RESP / 0x25
   USER_CONFIRM / 0x26 PASSKEY_REQ / 0x28 SSP_COMPLETE / 0x2b NOTIFY），
   zblue 本分支用偏移号（0x31/0x32/0x33/0x34/0x36/0x3b）。
3. **LCPU 事件按 handle 寻址**（status+handle+payload），zblue 按 bdaddr 寻址。

### vendor bth4 桥接（gitignored）
- handle->addr 映射：Connection Complete (0x03) 注册 / Disconnection (0x05) 注销
- 6 个 SSP 事件转换为 zblue 格式（bdaddr 寻址），cap/oob/auth 钳制合法值
- 日志：`sf32lb52 bth4: SSP bridge 0x23 -> 0x31 (handle 0x0081)`
- 注意：LCPU 事件参数格式为假设（status+handle+payload），若手机弹窗流程
  不推进需重新核对 LCPU 实际字段布局。

### zblue rx_work_handler（gitignored）
- while 循环排空 rx_queue（保留尾部重提交兜底）

---

## R59：可发现性修复（inquiry scan activity + 本地名字）（2026-08-16）

### 根因（R58 日志实证）
- zblue br_init 的 Write_Inquiry_Mode (0x0c45) 在 enable 早期失败 -> br_init 提前 return，
  后续 Write_Local_Name (0x0c13) 未执行；scan activity 写入因 BT_DEV_READY 未置位
  返回 -EAGAIN（write_scan_activity 检查 READY）被 (void) 吞掉。
- LCPU 默认 inquiry/page scan activity = 0 -> inquiry 无窗口 -> 手机搜不到板子。
- 手机能连上（page scan 由 enable 流程设置 activity）但搜索（inquiry）失败。

### vendor bth4 兜底（gitignored）
- 拦截 Write_Scan_Enable (0x0c1a)：inq 位置位时补发 Write_Inquiry_Scan_Activity
  (0x0c1e, 0x0400/0x0024)；page 位置位时补发 Write_Page_Scan_Activity (0x0c1c)。
- 同时补发 Write_Local_Name (0x0c13) "Agent-Watch"（与 BLE adv 名一致）。
- 注意：补发命令在 0x0c1a 之后直接发出，其 CC 会被 zblue send_sync 忽略（opcode 不匹配）。

## R60 追加：强制 inquiry scan（2026-08-16）
- zblue enable 只发 Write_Scan_Enable 0x02（page only）——手机搜索需要 inquiry。
- bth4 在 0x0c1a 发送前强制置 inquiry 位（data[3] |= 0x01）。
- 发送后仍补发 inquiry/page scan activity + 本地名字。

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

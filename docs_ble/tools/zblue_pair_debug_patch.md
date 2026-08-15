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

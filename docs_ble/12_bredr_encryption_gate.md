# 12. BREDR 加密关口：link key 不持久化（根因）与配对→PAN 验证方案

日期：2026-08-15 深夜 ｜ 分支：bletest ｜ 板子：SF32LB52 DevKit（/dev/ttyACM0, 1M baud）

## 现象

`pan connect` 全流程已跑通至：create_br → ACL up → set_security ret=0 →
worker 超时 L2CAP anyway → L2CAP connect ret=0，但 `[pan] security level=2`
从未出现，手机随后断 ACL（reason 0x08 timeout）。

## HCI 解码（/tmp/pan_raw.log，bth4 trace 40 字节）

```
[pan] worker: request encryption
01 11 04 02 80 00                 = HCI Authentication_Requested (0x0411) handle 0x0080
04 0f 04 00 ...                   = Command Complete status=0
04 16 06 a4 d1 fe b3 cc a4        = HCI Link_Key_Request (0x16)  控制器向 host 要密钥
01 0c 04 06 a4 d1 fe b3 cc a4      = HCI Link_Key_Neg_Reply (0x040C)  zblue RAM 无密钥→拒绝
[bttool] Pair Display [A4:CC:B3:FE:D1:A4][BREDR][PIN] please reply:
                                    → 控制器降级 legacy PIN 配对，等在控制台输 PIN
[pan] worker: timeout, L2CAP anyway → 未加密通道上发 L2CAP CONNECT_REQ → 手机拒绝
```

## 根因

1. **控制器（LCPU 闭源固件）不持久化 link key**：每次认证都发 Link_Key_Request 向 host 要。
2. **zblue（external/zblue port）BR link key 只存 RAM**：`br_key_pool`（CONFIG_BT_MAX_PAIRED=1）。
   持久化仅在 CONFIG_BT_SETTINGS 下生效（keys_br.c 的 store/set 处理全在
   `#if defined(CONFIG_BT_SETTINGS)` 内），而本移植**没有 settings 子系统**
   （port/subsys 只有 bluetooth/flash/fs/power/shell）。
3. 因此每次重启后密钥即失 → 每次连接都要重新 legacy PIN 配对；
   控制台无人输 PIN → 认证挂起 → 2 秒超时后裸发 L2CAP → 被拒。

## zblue 关键代码位置（已验证）

- `host/classic/ssp.c` `bt_hci_link_key_req()`：Link_Key_Request 处理——
  `conn->br.link_key` 或 `bt_keys_find_link_key()` 命中 → `link_key_reply()`
  （16 字节 key）；未命中 → `link_key_neg_reply()`。
- `host/classic/keys_br.c` `bt_keys_find_link_key/get/store`：RAM 池管理；
  store 在 CONFIG_BT_SETTINGS 下写 settings（本移植未启用）。
- 配对完成存储点：`ssp.c:531`（auth complete 后 store）、`smp.c:890`。
- `bt_conn_set_security`（conn.c:2775）→ `bt_ssp_start_security`（ssp.c:368）→
  `conn_auth`（ssp.c:348）→ HCI Authentication_Requested。

## 验证方案 A（无需改代码）——同一启动内先配对再 pan connect

1. 板子重启后（密钥已失），手机删除旧配对记录 → 发起配对；
2. 手机弹 PIN 输入框 → 输入 0000；
3. 板子控制台 `pair pin A4:CC:B3:FE:D1:A4 1 0000` 应答（bttool 命令）；
4. 配对完成（Link_Key_Notification → zblue RAM 存 key）；
5. **不重启**，立即 `pan connect A4:CC:B3:FE:D1:A4 1 2`（dst=NAP, src=PANU）；
6. 预期：Link_Key_Request → zblue 用 RAM key 应答 → Encryption Change →
   `[pan] security level=2` → L2CAP（MTU 1691）→ BNEP Setup 0x0000 SUCCESS →
   TAP "bt-pan" 创建 → PROFILE_STATE_CONNECTED。

关键差异点：密钥在 RAM 中，Link_Key_Request 得到 16 字节 key 应答而非拒绝。

## 验证方案 B（持久化，突破后实现）

文件式 BR key store：port 层新增 br_key_file 挂钩——
- 保存：`bt_keys_link_key_store()`（keys_br.c）无条件调用 port 钩子写文件
  （如 /data/misc/bt/br_key.bin，与框架 bt_storage 同目录）；
- 加载：`bt_hci_link_key_req()` 未命中 RAM 时，从文件恢复进 br_key_pool 再应答。

## 上网验证（突破后）

当前镜像无 ping/dhcpc。两个选项：
1. defconfig 加 CONFIG_NETUTILS_PING（或 DHCPC），重编译烧录；
2. 静态 IP：`ifconfig bt-pan 192.168.44.2 netmask 255.255.255.0` +
   默认路由 192.168.44.1（Android 蓝牙网络共享常用 192.168.44.x 网段），
   用 ifconfig 包计数 + 手机端连接状态验证。

# 蓝牙 PAN 打通设计：BNEP 数据面从零重写

- 日期：2026-08-20
- 状态：待用户评审
- 验收标准：**产品级**（可长稳运行，非 demo 一次通过）
- 目标平台：SiFli SF32LB52（openvela / NuttX），协议栈 external/zblue
- 测试设备：2 台 HyperOS 手机 + 1 台 iPhone 6（无备用方案）

> 文档位置说明：superpowers 的默认路径是 `docs/superpowers/specs/`，但本仓库的
> `docs/` 是 openvela 上游文档仓库（不应混入项目设计稿），而项目顶层
> `/home/aila/projects/vela_contest` 本身不是 git 仓库。`docs_ble/` 是指向
> `contest2026_181_womenshayebuhuidui/docs_ble` 的符号链接，既是本项目既有
> 蓝牙文档的所在，也在一个真实的 git 仓库内，因此本设计稿落在
> `docs_ble/superpowers/specs/`。

## 0. 一句话目标

让 `bt-pan` 接口经手机蓝牙网络共享（NAP）取得 DHCP 地址，并让 `ai_agent`
通过它完成 AI API 的 HTTPS 调用，在三台目标手机上可重复、可长稳。

## 1. 范围与边界

**允许改动**（用户已授权）：

- `frameworks/connectivity/bluetooth/`（蓝牙框架：service / profiles / SAL）
- `external/zblue/`
- `vendor/sifli/`（bth4、defconfig、rcS）

**不改**：`packages/ai_agent/` 源码。对 ai_agent 的影响只通过 defconfig 表达。

**已决的配置项**：

| 配置 | 现值 | 目标值 | 理由 |
|---|---|---|---|
| `CONFIG_AI_AGENT_BLE_GATT` | y | **n** | 见 §1.1 |
| `CONFIG_TUN_NINTERFACES` | 1 | **1（不变）** | 关掉 GATT 后 TUN 槽位不再争用 |
| `CONFIG_CONFIG_BLUETOOTH_DEFAULT_COD` | 0x00280704 | **0x002A0704** | 补 Networking bit(17)，见 §7.1 |
| `CONFIG_BT_BUF_ACL_RX_SIZE` | 1025 | **1695** | 见 §5.1 |
| `CONFIG_BT_BUF_ACL_RX_COUNT_EXTRA` | 5 | **2** | 抵消 §5.1 的 SRAM 增量 |
| `CONFIG_KVDB` | 未设 | **y（仅调试 defconfig）** | 打开 btsnoop（`log_server.c:243`）；产品 defconfig 不启用，见 §5.3 |

### 1.1 关闭 BLE GATT 是安全的

结论：**可以整体关闭，不影响 ai_agent 走网络调 AI API。**

依据（逐条已在代码中核对）：

1. AI API 调用走 `bt-pan` 接口 + NuttX 标准 socket 栈，与 BLE GATT 无任何调用关系。
2. `CONFIG_AI_AGENT_BLE_GATT` 现存的唯一实际职责是**备份网络通道**
   （`network_manager.c:633-634` 把 `ble_gatt_net_is_connected()` 计入
   `backup_ready`）。用户已明确不需要备用方案。
3. legacy 命令通道在生产路径**本来就已禁用**——`agent_main.c:646-663` 的注释
   写明 "the legacy command channel is disabled to avoid taking the instance"。
4. `ble_cmd_handler.c:24-26` 只处理三条命令：`wifi_config`、`ping`、`status`。
   本板无 Wi-Fi（`.config` 中 `CONFIG_DRIVERS_IEEE80211 is not set`），
   `wifi_config` 是死路径；`ping`/`status` 是诊断。**不承担配网或取 token。**
5. 所有引用点（`agent_main.c:80-82,646`、`network_manager.c:269-270,633-634,929-943`、
   `nsh_commands.c:171-172,510-550,1059-1061`）全部包在 `#ifdef CONFIG_AI_AGENT_BLE_GATT`
   内，关闭后编译干净，无需改 ai_agent 源码。

附带收益：省下 ble_gatt.c(772) + ble_gatt_net.c(526) + ble_cmd_handler.c(184) 行代码
的 flash 与其运行时缓冲，对 SRAM 已达 90.9% 的现状是正向的。

## 2. 现状诊断：为什么 `bt-pan` UP 了却拿不到 IP

R99（2026-08-19）已证明 BNEP 之下每一层都工作：配对 BONDED → 加密 `enc=1`
且 security level=2 → L2CAP PSM 0x000F CONF_RSP SUCCESS → 手机主动回
Setup Response `status=0x00` → `pan_netif_state_cb ifname:bt-pan, state:1`。

也就是说链路没问题，**问题在 BNEP 的字节编码**。这是好消息：字节对不对可以
离线证明，不必靠上板猜（§6）。

现存缺陷共 10 条。D1–D6 都在
`frameworks/connectivity/bluetooth/service/stacks/zephyr/`；D7 是配置层问题；
D8–D10 是第二轮静态复核新增的，见 §11（**D9 优先级高于 D1**：它让 DHCP
DISCOVER 连发都发不出去，D1 描述的"发出去但被手机丢弃"其实还轮不到发生）：

**D1（DHCP 失败的直接原因）** `sal_pan_interface.c:966-978`
——发送数据帧时 type 写 `0x00`（General Ethernet，规范要求随后跟 dst6+src6+proto2
共 14 字节），却只写了 3 字节（type + proto2），并把 MAC 显式丢弃
（`(void)dst_addr; (void)src_addr;`）。接收方按 General Ethernet 解析时会把
IP 载荷的前 12 字节当成 MAC 地址，整帧被判为无效而丢弃。DHCP DISCOVER 和 ARP
（dst=ff:ff:ff:ff:ff:ff）因此从未真正到达手机的 NAP 桥。

**D2** `sal_pan_interface.c:209-266`——Setup Connection Request 用 2 字节写
UUID Size 字段（`setup[2]=0x00; setup[3]=0x10`），规范该字段只有 **1 字节**。
正确的 PANU→NAP 请求是 7 字节：`01 01 02 11 16 11 15`。
（历史误判：R94 把手机回的 `0x0003` 读成"需要 128-bit UUID"，实际
`0x0003` 就是 *Invalid Service UUID Size*，是我们自己发错了长度字段。）

**D3** `sal_pan_interface.c:341-352`——Setup Connection Response 造了一个规范里
不存在的 length 字段，发 6 字节。正确是 4 字节：`01 02 00 00`。

**D4** `sal_pan_interface.c:369-390`——解析端把标准 4 字节 Response 当成
"HyperOS 非标准"，并且**任何状态码都判成 SUCCESS**（`else { ... treating as
success anyway ...; conn->state = PAN_CONN_CONNECTED; }`）。这会把失败的握手
当成成功，掩盖真实错误。

**D5** `sal_pan_interface.c:299`——switch 标签数值冲突：
`BNEP_SETUP_CONN_RESP`(0x02) 与 `BNEP_COMPRESSED_ETHERNET`(0x02) 同值，
控制帧与压缩数据帧无法区分。

**D6** `include/sal_pan_interface.h:24-58`——响应码从 `0x0003` 起全部错位
（`0x0003` 实为 *Invalid Service UUID Size*，`0x0004` 为 *Connection Not
Allowed*，`0x0005`–`0x0007` 在规范中不存在）；且缺少
`BNEP_COMPRESSED_ETHERNET`(0x02)、`BNEP_COMPRESSED_ETHERNET_SRC_ONLY`(0x03)、
`BNEP_COMPRESSED_ETHERNET_DEST_ONLY`(0x04) 三个数据帧类型——**接收路径因此
无法解析手机发来的压缩帧**，而 Android 的 NAP 常态使用压缩帧。

**D7（产品级阻塞，非 demo 阻塞）** MTU 承诺与接收能力不一致：
`sal_pan_interface.c:489,907` 请求 `rx.mtu = 1691`（BNEP 规范最小值），
而 `BT_L2CAP_RX_MTU = CONFIG_BT_BUF_ACL_RX_SIZE - 4 = 1021`
（`include/zephyr/bluetooth/l2cap.h:39`）。我们向手机承诺 1691 却只能重组
1021，手机发来的整尺寸帧（TLS 记录、TCP 全段）会在重组阶段被丢弃。
小包（DHCP、ARP）不受影响，所以这条不会阻塞首次拿 IP，但会让 HTTPS 调用
在 PMTU 黑洞里卡死。详见 §5。

## 3. BNEP 协议层：从零重写

设计原则：**BNEP 是无状态的封装层**——没有自己的流控、重传、分片，全部交给
L2CAP。所以这一层只有两件事：正确编码、正确解码。任何"状态机"的复杂度都应
落在连接建立（Setup）阶段，数据阶段必须是纯函数式的。

### 3.1 头文件重写（唯一真值源）

`stacks/include/sal_pan_interface.h` 中的枚举按规范重建：

```c
/* BNEP 帧类型（头字节低 7 位；bit7 = 扩展头标志） */
#define BNEP_TYPE_MASK                      0x7f
#define BNEP_EXT_FLAG                       0x80
#define BNEP_GENERAL_ETHERNET               0x00  /* +dst6 +src6 +proto2 */
#define BNEP_CONTROL                        0x01
#define BNEP_COMPRESSED_ETHERNET            0x02  /* +proto2            */
#define BNEP_COMPRESSED_ETHERNET_SRC_ONLY   0x03  /* +src6 +proto2      */
#define BNEP_COMPRESSED_ETHERNET_DEST_ONLY  0x04  /* +dst6 +proto2      */

/* BNEP 控制消息类型 */
#define BNEP_CTRL_CMD_NOT_UNDERSTOOD        0x00
#define BNEP_CTRL_SETUP_CONN_REQ            0x01
#define BNEP_CTRL_SETUP_CONN_RSP            0x02
#define BNEP_CTRL_FILTER_NET_TYPE_SET       0x03
#define BNEP_CTRL_FILTER_NET_TYPE_RSP       0x04
#define BNEP_CTRL_FILTER_MULTI_ADDR_SET     0x05
#define BNEP_CTRL_FILTER_MULTI_ADDR_RSP     0x06

/* Setup Connection Response 码（2 字节，大端） */
#define BNEP_RSP_SUCCESS                    0x0000
#define BNEP_RSP_INVALID_DST_UUID           0x0001
#define BNEP_RSP_INVALID_SRC_UUID           0x0002
#define BNEP_RSP_INVALID_UUID_SIZE          0x0003
#define BNEP_RSP_CONN_NOT_ALLOWED           0x0004

/* Filter Response 码（2 字节，大端） */
#define BNEP_FILTER_RSP_ACCEPTED            0x0000
#define BNEP_FILTER_RSP_UNSUPPORTED         0x0001
#define BNEP_FILTER_RSP_INVALID_RANGE       0x0002
#define BNEP_FILTER_RSP_TOO_MANY            0x0003

/* PAN 服务 UUID（16-bit） */
#define BNEP_UUID16_PANU                    0x1115
#define BNEP_UUID16_NAP                     0x1116
#define BNEP_UUID16_GN                      0x1117
```

注意帧类型 `0x02` 与控制消息类型 `0x02` 的同值不再是问题：它们处于**不同的
命名空间**（帧头字节 vs 控制帧的第 2 字节），只要解析分两级、switch 分两个
就不会冲突。D5 的根因是把两层塞进了同一个 switch。

### 3.2 帧编解码：两个纯函数

新建 `stacks/zephyr/bnep_codec.c` / `.h`，**不依赖 zblue、不依赖 NuttX、不碰
连接状态**，只做字节搬运。这样它可以在开发机上用单元测试直接跑（§6.1）。

```c
/* 编码：把一个完整以太帧（含 14 字节头）写成 BNEP 帧。
 * 返回写入 out 的字节数，或负错误码。
 * 当 compress 为 true 且 MAC 与链路两端一致时使用压缩类型。 */
int bnep_encode_eth(uint8_t *out, size_t out_cap,
                    const uint8_t *eth_frame, size_t eth_len,
                    const uint8_t local_mac[6], const uint8_t peer_mac[6],
                    bool compress);

/* 解码：把 BNEP 帧还原成完整以太帧（含 14 字节头）。
 * 跳过所有扩展头。控制帧返回 BNEP_DECODE_IS_CONTROL 并给出控制载荷位置。 */
int bnep_decode_eth(uint8_t *out, size_t out_cap,
                    const uint8_t *bnep, size_t bnep_len,
                    const uint8_t local_mac[6], const uint8_t peer_mac[6],
                    const uint8_t **ctrl_out, size_t *ctrl_len);
```

**第一版 TX 一律用 `BNEP_GENERAL_ETHERNET`（14 字节全写）**——规范合法、无条件
正确、最容易在 Wireshark 里判读。压缩只在 §7 的吞吐门不达标时才作为优化打开。
这是"最小修复先行"路线的具体体现。

**RX 必须支持全部四种数据帧类型 + 扩展头跳过**，因为 Android 的 NAP 常态发送
压缩帧，我们没有选择权。压缩帧的还原规则：

| 收到的类型 | dst 填 | src 填 |
|---|---|---|
| `0x00` General | 帧内自带 | 帧内自带 |
| `0x02` Compressed | `local_mac` | `peer_mac` |
| `0x03` SrcOnly | `local_mac` | 帧内自带 |
| `0x04` DestOnly | 帧内自带 | `peer_mac` |

扩展头格式：头字节 bit7=1 表示后面跟扩展头，每个扩展头为
`[bit7=还有更多][7bit 类型][1 字节长度][载荷]`。解码时**必须循环跳到最后一个
扩展头**再取网络载荷，否则会把扩展头当 IP 数据交给网络栈。

### 3.3 MAC 地址与 BD_ADDR 的字节序（最易犯错处）

BNEP 直接把 48 位 BD_ADDR 当作 48 位 MAC 使用，但两者字节序相反：

- HCI / zblue 的 `bt_addr_t.val[6]` 是**小端**（LAP 在前）
- 以太网 MAC 是**网络序（大端）**

因此 `bt_addr_t` → MAC 必须**逐字节反转**。这一步做错的表现是：握手能过、
单播能收到但 ARP 永远解析不出对端，非常容易被误判为"手机不响应"。

统一在 `bnep_codec.h` 提供一个**不依赖 zblue 类型**的转换器（保持 §3.2 的
"编解码器零依赖"性质，这样它才能在开发机上独立编译测试）：

```c
/* le48 为小端 48 位地址（zblue 的 bt_addr_t.val 即此布局），
 * 输出网络序 MAC。调用方传 addr->val，codec 不认识 bt_addr_t。 */
static inline void bnep_mac_from_le48(uint8_t mac[6], const uint8_t le48[6])
{
    for (int i = 0; i < 6; i++) { mac[i] = le48[5 - i]; }
}
```

SAL 层这样用：`bnep_mac_from_le48(local_mac, bt_addr->val);`

### 3.4 Setup 握手状态机

只有三个状态，PANU 侧（我们）永远是发起方：

```
IDLE ──L2CAP CONNECTED──▶ SETUP_SENT ──收到 RSP=0x0000──▶ CONNECTED
                              │                              │
                              └──RSP≠0x0000 或 超时(5s)──▶ FAILED（断 L2CAP）
```

- 发出：`01 01 02 11 16 11 15`（dst=NAP 0x1116, src=PANU 0x1115，UUID Size=2）
- 期待：`01 02 00 00`
- **任何非 `0x0000` 的响应码都必须判失败**并记录具体码值（修正 D4）。
  产品级要求：不允许"treating as success anyway"这类掩盖失败的分支。
- 收到对端的 `FILTER_*_SET`：回 `FILTER_RSP_UNSUPPORTED`(0x0001)。规范允许
  不支持过滤器，但**必须回**，不回会让对端等待。
- 收到无法识别的控制消息类型：回 `CMD_NOT_UNDERSTOOD`(0x00) 并附上那个未识别
  的类型字节。这是规范强制要求。
- 若对端（少见）先发 `SETUP_CONN_REQ` 给我们：校验 UUID Size ∈ {2,4,16} 与
  UUID 值，然后回 4 字节 `01 02 00 00`（修正 D3）。

## 4. 数据面：TAP ↔ BNEP

### 4.1 关键接口变更：不要在上层剥以太头

现状 `profiles/pan/panu_service.c:580-605` 在 TAP 侧就把 14 字节以太头剥掉，
然后把 `dst`、`src`、`proto` 作为独立参数传给
`bt_sal_pan_write(&addr, proto, dst, src, payload, len)`，而 SAL 层又把 MAC
丢弃（D1）。这个"拆开再重组"的接口形状**就是 D1 得以存在的结构性原因**。

xiaozhi 的做法恰好相反且正确（`bts2_app_pan.c:378-423`）：把 dst MAC 完整交给
BNEP 层，由 BNEP 层自己决定用哪种帧类型。

新接口——**整帧进，整帧出**：

```c
/* 传入完整以太帧（含 14 字节头），SAL 层负责 BNEP 封装 */
bt_status_t bt_sal_pan_write_eth(const bt_address_t *addr,
                                 const uint8_t *eth_frame, uint16_t eth_len);
```

`panu_service.c` 的 TAP 读循环因此简化为「read() → 原样交给 SAL」，
不再触碰以太头。RX 方向同理：SAL 层还原出完整以太帧后整帧 `write()` 回 TAP。
这样以太头的解析责任只存在于 `bnep_codec.c` 一处。

### 4.2 TAP 接口的 MAC 必须设成本机 BD_ADDR

现状 TAP 以 `IFF_TAP` + 名字 `bt-pan` 打开（`panu_service.c:539`），但没有设置
硬件地址，MAC 是全零或随机值。后果：

- 我们发出的帧 src MAC 与 BNEP 链路的 BD_ADDR 不一致，具备 MAC 校验的 NAP
  会丢弃；
- 压缩帧永远无法命中（`local_mac`/`peer_mac` 比对失败），只能退化为 General；
- DHCP 的 `chaddr` 与链路地址不一致，某些 DHCP 服务端会拒绝续租。

做法：在 `netif` **ifup 之前**用 `SIOCSIFHWADDR` 把 MAC 设为本机 BD_ADDR
反转后的 6 字节。NuttX 的 `netdev_ioctl.c:1117` 明确注释
"will not take effect until ifup"，所以顺序是硬约束：

```
open(/dev/tun) → TUNSETIFF(IFF_TAP,"bt-pan") → SIOCSIFHWADDR(local_mac)
  → BNEP Setup 成功 → SIOCSIFFLAGS(IFF_UP) → dhcpc
```

### 4.3 DHCP 触发时序

照抄 xiaozhi 的时序（`bt_lwip.c:52-89`）：**link up 与 DHCP 启动都发生在 BNEP
Setup Response 成功之后**，不是 L2CAP 连上之后。当前实现把 `bt-pan` 置 UP 的
时机偏早（R99 日志里 `pan_netif_state_cb state:1` 出现得比握手完成早），
会让 dhcpc 在数据通路还没就绪时就开始重试并耗尽 3 次重试预算
（`CONFIG_NETUTILS_DHCPC_RECV_TIMEOUT_MS=3000`、`RETRIES=3` → 9 秒内耗尽，
而手机侧约 30 秒才超时断链）。

产品级要求：DHCP 失败不得使整条链路进入不可恢复状态。dhcpc 耗尽重试后应
退避重试（首次 2s，指数退避至 30s 上限），而不是放弃并等待手机断链。

### 4.4 广播与多播

DHCP DISCOVER（dst=`ff:ff:ff:ff:ff:ff`）和 ARP 请求都是广播帧。BNEP 对广播
没有特殊帧类型——广播就是 dst 全 F 的 General Ethernet 帧。这正是 D1 致命的
原因：广播帧的 dst 必须真实写入，压缩类型（`0x02`/`0x03`）**不可用于广播**，
因为接收方会把 dst 还原成自己的单播 MAC。

编码器规则（必须写进单元测试）：**dst 非本链路对端单播地址时，一律使用
General Ethernet。** 这条规则让广播、多播、以及经 NAP 转发给第三方的帧
自动走正确路径。

IPv6：xiaozhi 直接丢弃 `0x86dd`（`bts2_app_pan.c:566-592`）。本项目同样只需
IPv4，编码器对 `0x86dd` 不做特殊处理（照常封装），由网络栈决定；但 defconfig
不启用 IPv6，避免无谓的 RA/NS 流量占用蓝牙带宽。

## 5. 缓冲、MTU 与栈预算

### 5.1 修正 MTU 承诺与接收能力的矛盾（D7）

`BT_L2CAP_RX_MTU` 是从 `CONFIG_BT_BUF_ACL_RX_SIZE` 推导的常量
（`include/zephyr/bluetooth/l2cap.h:39`：`CONFIG_BT_BUF_ACL_RX_SIZE - 4`）。
要让 `rx.mtu = 1691` 真正成立，必须
`CONFIG_BT_BUF_ACL_RX_SIZE ≥ 1691 + 4 = 1695`。

RX 缓冲个数由 `include/zephyr/bluetooth/buf.h:102-104` 推导：

```
BT_BUF_ACL_RX_COUNT = MAX(CONFIG_BT_BUF_ACL_RX_COUNT, BT_MAX_CONN + 1)
                      + CONFIG_BT_BUF_ACL_RX_COUNT_EXTRA
```

| 参数 | 现状 | 目标 |
|---|---|---|
| `BT_BUF_ACL_RX_SIZE` | 1025 | 1695 |
| `BT_BUF_ACL_RX_COUNT_EXTRA` | 5 | 2 |
| 推导个数（`MAX_CONN=2`） | `3 + 5 = 8` | `3 + 2 = 5` |
| 粗估池占用 | ≈ 8 × 1.04 KB ≈ 8.3 KB | ≈ 5 × 1.71 KB ≈ 8.6 KB |

即：**把 EXTRA 从 5 降到 2，可以在几乎不增加 SRAM 的前提下把接收 MTU 从 1021
提到 1691。** 这是本设计里性价比最高的一项改动。风险是并发 RX 缓冲更少，
需在 §7 的吞吐门里观察是否出现 `bt_buf` 耗尽日志；若出现则改为
`EXTRA=3`（≈10.3 KB，仍在可接受范围）并从别处腾。

注：`CONFIG_BT_L2CAP_TX_MTU=253` **不需要改**。PAN 的发送走
`sal_pan_interface.c:70` 里专用的 `pan_tx_pool`（已按 `PAN_TX_MTU=1691` 定尺），
`CONFIG_BT_L2CAP_TX_MTU` 只影响控制帧使用的全局池。
（同样地，早前有过 `CONFIG_BT_BUF_ACL_TX_SIZE=27` 会限制 BNEP 吞吐的判断，
已核对 `external/zblue/zblue/subsys/bluetooth/host/conn.c:168-176,629-636`
证伪：现代 zblue 用零拷贝 view 分片，BR 分片长度取自 HCI Read_Buffer_Size
返回的 `hdev->br.mtu`，与 `BT_BUF_ACL_TX_SIZE` 无关。）

### 5.2 栈预算：编解码器不得使用大栈缓冲

`CONFIG_BT_RX_STACK_SIZE=1200`——整个 zblue RX 线程连同我们的 BNEP 回调链只有
1200 字节栈。一个 1691 字节的临时以太帧缓冲会直接冲垮它，症状是随机崩溃或
栈溢出断言，且只在收到大包时出现（小包测试全绿）。这是最容易在 demo 阶段
逃过、在产品阶段暴雷的一类缺陷。

约束（写进代码审查清单）：

- `bnep_decode_eth` 的 `out` 缓冲**必须由调用方提供**，且来自静态/堆内存，
  不得是 RX 回调里的栈数组；
- 每条 PAN 连接分配一个静态 1600 字节 RX 重组缓冲（`BT_MAX_CONN=2`，
  但 PAN 只允许 1 条，见 §8）→ 1 × 1600 字节，可接受；
- TX 方向直接写入 `net_buf_tail(buf)`，零额外缓冲；
- 若 1200 字节栈在实测中仍紧张，提高到 2048（成本 848 字节，一次性）。

### 5.3 SRAM 总预算

现状记录为 **90.9%**（R66）。本设计的净增量估算：

| 项 | 增量 |
|---|---|
| ACL RX 池（SIZE↑ + EXTRA↓） | ≈ +0.3 KB |
| PAN RX 重组缓冲（静态 1600 B） | +1.6 KB |
| 关闭 `AI_AGENT_BLE_GATT` | 待测（预期 **−** 数 KB） |
| `CONFIG_KVDB=y` + btsnoop | 待测（+，仅调试构建启用） |

净值预期接近零或为负，但 **90.9% 的基线必须先用上板 `free` 实测确认**，
不能沿用文档记录值。`CONFIG_KVDB`/btsnoop 只在调试 defconfig 启用，
产品 defconfig 关闭——避免调试设施进入 SRAM 预算。

## 6. 离线验证：让 Wireshark 当独立裁判

产品级要求的核心是**不靠"上板试试看"来判定协议正确性**。本节给出两道
不依赖手机、不依赖蓝牙硬件的验证关卡。

> 背景约束：开发机**没有蓝牙适配器**（已实测：`/sys/class/bluetooth` 不存在、
> `lsusb` 无蓝牙设备、bluetooth 服务 inactive），因此"用 BlueZ 起一个 NAP
> 做对照"的方案不可行。下面两道关卡都不需要适配器。

### 6.1 Gate A：编解码器单元测试（开发机，秒级）

因为 `bnep_codec.c` 不依赖 zblue 与 NuttX（§3.2 的设计目的），可以在开发机上
直接编译成一个 host 可执行文件跑测试。用例必须覆盖：

- PANU→NAP Setup Request 逐字节等于 `01 01 02 11 16 11 15`
- Setup Response 逐字节等于 `01 02 00 00`，长度 4
- 每个响应码的判定（只有 `0x0000` 判成功）
- 四种数据帧类型的编码与解码往返（round-trip）一致
- 广播帧（dst=`ff:ff:ff:ff:ff:ff`）**必须**编成 General Ethernet，不得压缩
- 带 1 个、2 个、3 个扩展头的接收帧能正确跳过并取到正确载荷
- `bt_addr_t` ↔ MAC 的字节序反转
- 截断/畸形输入（长度不足、UUID Size 非法值、out 缓冲不够）返回错误而不越界
- 1500 字节载荷的编码不超过 1691，1691 字节输入的解码不溢出

这一关把 §2 的 D1–D6 全部变成**可自动检出**的回归项。

### 6.2 Gate B：HCI trace → pcap → Wireshark BNEP dissector

`vendor/sifli/chips/sf32lb52/sf32lb52_bth4.c` 已有 HCI 十六进制打点
（RX `:930-945`、TX `:1272-1287`），受 `SF32LB52_BT_TRACE` 控制。把它变成
一条完整的抓包链路：

1. **修 trace 的两个问题**（都在 `vendor/sifli`，边界内）：
   - 上限 40 字节改为完整长度（BNEP 数据帧要看到全部 14 字节以太头 +
     EtherType，40 字节勉强够判读头部但不足以生成有效 pcap）；
   - RX 分支目前从 `priv->rxbuf[0]` 开始打印，而 `n` 取自本次新到的 `len`
     ——缓冲区里已有 pending 数据时打印的是错位内容。应打印本次到达的
     `data[0..len)`，或改为在整包组装完成后打印一次。
2. **新增 `docs_ble/tools/bnep_pcap.py`**：读串口日志，抽取
   `sf32lb52 bth4 recv/send` 行的 hex，按方向打上 HCI H4 方向字节，写成
   **pcap（linktype 201 = `LINKTYPE_BLUETOOTH_HCI_H4_WITH_PHDR`）**。
3. 用 Wireshark 打开：它自带 BNEP dissector，会独立地把我们的帧解析成
   BNEP 类型、UUID、Setup Response 码、还原的以太头。

判定标准（**这就是"100% 搞定"的可证明含义**）：

- Wireshark 对每一个 BNEP 帧都给出正确类型，**没有一条 malformed**；
- Setup Request/Response 的字段与 §3.4 逐字节一致；
- DHCP DISCOVER 能被 Wireshark 一路解析到 `Bootstrap Protocol` 层，
  且 `chaddr` 等于我们用 `SIOCSIFHWADDR` 设的 MAC（§4.2）；
- 手机回的 DHCP OFFER 同样可解析。

Wireshark 的 BNEP dissector 与我们的实现来自完全独立的源头，它说对才算对
——这消除了"自己写的解析器验证自己写的编码器"这种自证循环。项目历史上
R94（把 `0x0003` 误读为需要 128-bit UUID）和 R96（把标准 4 字节 Response
误判为 HyperOS 非标准）两次误判，都是缺少独立裁判造成的。

#### 6.2.1 工具链可用性：已实测打通（无需 root）

开发机没有安装 tshark，且 `sudo` 需要密码、github.com 不可达。已实测出一条
**完全用户态、无需 root** 的路径，并跑通了全链路冒烟测试：

```bash
# 1) 只下载 deb，不安装（apt-get download 不需要 root）
mkdir -p /tmp/tshark_local/debs && cd /tmp/tshark_local/debs
apt-get download tshark wireshark-common libwireshark15 libwireshark-data \
  libwiretap12 libwsutil13 libsmi2ldbl liblua5.2-0 libspandsp2 \
  libssh-gcrypt-4 libc-ares2 libsnappy1v5 libnl-route-3-200 \
  libmaxminddb0 libbrotli1 libgcrypt20 libgnutls30

# 2) 解到私有前缀
cd /tmp/tshark_local && for d in debs/*.deb; do dpkg -x "$d" root/; done

# 3) 运行
export LD_LIBRARY_PATH=/tmp/tshark_local/root/usr/lib/x86_64-linux-gnu
/tmp/tshark_local/root/usr/bin/tshark -r capture.pcap
```

得到 TShark 3.6.2，`tshark -G protocols | grep bnep` 确认
`Bluetooth BNEP Protocol / BT BNEP / btbnep` 存在。

**冒烟测试结果（已实际执行）**——手工构造一个含 L2CAP 建链 + 本设计规定的
BNEP 字节序列的 pcap，tshark 独立解析输出：

```
1  localhost → remote   L2CAP  Sent Connection Request (BNEP, SCID: 0x0040)
2  remote → localhost   L2CAP  Rcvd Connection Response - Success
3  localhost → remote   BNEP   Sent Control - Setup Connection Request
                                 - dst: <PAN NAP>, src: <PAN PANU>
4  remote → localhost   BNEP   Rcvd Control - Setup Connection Response
                                 - Operation Successful
5  11:22:33:44:55:66 → ff:ff:ff:ff:ff:ff  ARP  Who has 192.168.44.1? ...
```

这条输出**独立证实了本设计的三个关键论断**：

1. `01 01 02 11 16 11 15` 就是正确的 PANU→NAP Setup Request（dst=NAP、
   src=PANU、UUID Size=2），即 D2 的修正方案正确；
2. `01 02 00 00` 就是标准的 4 字节 Setup Response 且 `0x0000` = *Operation
   Successful*，即 D3/D4 的"HyperOS 非标准"判断是错的，D6 的响应码表按规范
   重建是正确的；
3. type=`0x00` 后跟完整 14 字节以太头的 General Ethernet 帧可被一路解析到
   ARP，且广播 dst 保持为 `ff:ff:ff:ff:ff:ff`，即 D1 的修正方案正确。

也就是说：**§3 的编码规范在写任何固件代码之前就已经被一个独立实现验证过了。**
Gate B 剩下的工作只是把真实设备的 HCI trace 喂进同一条链路。

pcap 格式要点（冒烟测试中已验证）：linktype **201**
（`LINKTYPE_BLUETOOTH_HCI_H4_WITH_PHDR`），每包前置 **4 字节大端方向字**
（0=Sent，1=Rcvd），随后是 H4 类型字节（ACL=`0x02`）+ ACL 头 + L2CAP 头 + 载荷。
必须把 L2CAP 的 Connect Request/Response（PSM `0x000F`）也一并写入 pcap，
tshark 才能把该 CID 绑定到 BNEP dissector。

## 7. 连接、角色与可发现性

### 7.1 Class of Device 必须带 Networking 位

生效值是 `0x00280704`，缺少 Service Class 的 **Networking 位（bit 17）**。
手机在决定是否向本设备提供网络共享时会看这一位，缺失会导致手机侧的 NAP
根本不把我们当网络客户端。修正：`0x00280704 | 0x00020000 = 0x002A0704`。

但"改 defconfig 即可"是错的，这里有一个符号名陷阱，必须按实际链路改：

1. Kconfig 里声明的符号名本身带了一次 `CONFIG_` 前缀
   （`frameworks/connectivity/bluetooth/Kconfig:278` 是
   `config CONFIG_BLUETOOTH_DEFAULT_COD`，`default 0x00280704`），
   所以生成的宏是 **`CONFIG_CONFIG_BLUETOOTH_DEFAULT_COD`**
   （`out/nuttx_contest_board_ai_agent/.config:3205` 实测确认）。
2. 消费侧用的却是单前缀名：`service/common/bluetooth_define.h:29-33`
   `#ifdef CONFIG_BLUETOOTH_DEFAULT_COD` → 该宏**不存在**，
   于是走 `#else` 分支的硬编码 `0x00280704`。
3. defconfig:323 现有的 `CONFIG_BLUETOOTH_DEFAULT_COD=0x00020510`
   **是一行死配置**：它不对应任何合法 Kconfig 符号，被 olddefconfig 丢弃，
   所以那个"看起来已经带 Networking 位"的值从未生效。

因此要同时改三处才可靠：`bluetooth_define.h` 的 `#ifdef` 改成双前缀名、
`#else` 兜底值改成 `0x002A0704`、defconfig 用双前缀名写
`CONFIG_CONFIG_BLUETOOTH_DEFAULT_COD=0x002A0704` 并删掉死行。

还有一个持久化覆盖问题：`CONFIG_BLUETOOTH_STORAGE_UNQLITE_SUPPORT=y`，
`DEFAULT_DEVICE_OF_CLASS` 只是 `adapter_properties_default()`
（`service/common/storage.c:99`）在**没有已存记录时**的初值；
`adapter_service.c:379` 之后是从 storage 读的。老板子上很可能已存了旧 COD，
改默认值不会自动生效。所以 Gate 必须**运行时回读** COD，
而不是只看编译期常量（见 §8 Gate F）。

`vendor/sifli` 侧已有转发 `HCI_Write_Class_Of_Device` 的通路
（`sf32lb52_bth4.c:822,1148,1168`），无需改 bth4 代码。

### 7.2 SDP：记录本身是对的，`.uuid` 字段是另一回事

**更正前一轮的判断。** `profiles/pan/panu_service.c:1108` 的
`.uuid = {BT_UUID128_TYPE, {0}}`（全零）**不是 SDP 问题**。真正对外发布的
PANU 服务记录是 `stacks/zephyr/sal_pan_interface.c:423-458` 的
`g_panu_sdp_attrs`，它已经正确声明了 `BT_SDP_PANU_SVCLASS`、
L2CAP Protocol Descriptor 指向 PSM `0x000F`（`:440` 用的就是 `BT_BNEP_PSM`）、
以及 BNEP ProtocolDescriptor 和 ProfileDescriptor。这一节原来的结论作废。

`profile_service_t.uuid` 的唯一消费者是
`service/profiles/service_manager.c:163-183` 的 `service_manager_get_uuid()`，
它会**跳过全零 UUID**（`:173` 与 `empty_uuid` 比较后 `continue`），
而该函数只被 `adapter_service.c:2287` 的 `adapter_get_uuids()` 调用，
再往上只有 IPC 的"查询本机 UUID 列表"接口。全仓库没有任何路径把它写进
EIR 或 SDP（已 grep 确认 `sal_adapter_interface.c` 无此写入）。

结论：把它改成 `BT_UUID_DECLARE_16(BT_UUID_PANU)` 只影响本机 API 上报的
自查一致性，**对手机侧行为零影响**，属于顺手修正而非阻塞项。
（注：`bt_uuid.h` 目前也没有 `BT_UUID_PANU` 宏，需要一起补。）

### 7.3 连接触发时机与 SDP 冲突

xiaozhi 的经验（`main.c:537-547`）：SDP 查询与 PAN 连接同时进行会互相干扰，
它插入 **3 秒**延迟规避。它把 PAN 的发起挂在 HID 连接事件上
（`main.c:594-606`），重连也走 HID（`main.c:970`）——这是它自己的产品形态
决定的，本项目没有 HID，不应照搬。

本项目的触发链：

```
ACL 建链 → 配对 BONDED → 加密完成(enc=1, sec level ≥ 2)
  → 延迟 3s（规避 SDP 冲突）
  → L2CAP connect PSM 0x000F
  → BNEP Setup 握手成功
  → SIOCSIFHWADDR + ifup + dhcpc（§4.2/§4.3）
```

加密是硬前提：BNEP 通道的 `sec_level` 设为 `BT_SECURITY_L2`，由
zblue 的 `l2cap_br_conn_security()` 在 CONN_REQ 阶段把关。R99 的日志已证明
这一段是通的，本设计不改动它，仅确保重写后仍保持同样的时序。

### 7.4 重连策略（产品级要求）

R99 的现象是"手机约 30s 超时断链"。产品级必须能自愈：

- ACL 断开 → 清理 BNEP 状态、`ifdown bt-pan`、释放 TAP 的 IP；
- 有上限的退避重连；
- 重连成功后重新走完整的 §7.3 链条，**不复用旧的 BNEP 会话状态**；
- 每次状态迁移打一条 syslog，字段固定（便于 §6.2 的日志解析工具复用）。

**更正（第三轮复核）：退避机制本来就有，不要重写。** 初稿这里写的是
"指数退避 2s/4s/8s/16s/30s（上限），无限重试"，实际读代码后作废：
`panu_service.c:728-769` 已经实现了从 xiaozhi 移植的三分类重连
（首连 3 次 × 3s；连过再断 30 次 × 10s；外加 `:196-210` 的 5s LCPU 冷却窗），
DHCP 侧 `dhcpc_request()`（`RETRIES=3` × `RECV_TIMEOUT_MS=3000`）外面还套了
10 次 × `sleep(2)`，合计约 110 秒耐心。再叠一层指数退避只会让两套策略打架，
且"无限重试"与产品级的可预期行为相冲突。

这一区真正的缺陷是 **DHCP 工作线程的生命周期**，三条：

1. `g_dhcp_running` 在线程的四条退出路径上都没清（`:281/:287/:317/:326`），
   而 `pan_start_dhcp():331` 拿它当重入锁——**链路不断而 DHCP 失败时，
   之后永远不再重试**。
2. 该线程是 joinable 且从不 join，每次连接漏一个 TCB + 4 KB 栈（Gate H 会量到）。
3. DHCP 彻底耗尽后是死胡同：BNEP 链路还在，三分类断连逻辑不触发，
   于是既没有 IP 也没有重连。应主动断链，把决策交还给已有的断连处理。

见计划 Task 9。

## 8. 上板验证与产品级验收门

按顺序执行，前一门不过不进下一门。Gate A/B 见 §6，不需要硬件。

| 门 | 内容 | 通过标准 |
|---|---|---|
| A | 编解码器单元测试（开发机） | 全绿，覆盖 §6.1 全部用例 |
| B | pcap → Wireshark BNEP dissector | 零 malformed；DHCP 可解析到 Bootstrap 层 |
| C | DHCP 取址 | **入门判据**：运行时回读 COD，bit 17(Networking) 已置（§7.1 的持久化覆盖问题）。**主判据**：3 台设备各 10 次连接，成功率 ≥ 9/10 |
| D | ICMP | ping 网关 100 包，丢包 < 1%，无 30s 断链 |
| E | DNS + HTTPS | 成功完成一次 AI API 调用（含 TLS 握手） |
| F | 吞吐 | 记录实测 kbps；不设硬门槛，但需 ≥ AI API 可用的下限。**追加判据**：满 MSS 上行（1514 字节帧）连续跑通，且 `tx_oversize` 计数恒为 0（验 D8 + §11.3） |
| G | SRAM | 上板 `free` 实测，剩余量记录并 ≥ Gate C 之前的基线 |
| H | 长稳 | 连续 2 小时，含 ≥3 次人为断链恢复，无内存泄漏、无崩溃 |

**Gate E 是真正的业务验收点**：它同时验证了 IP 层、MTU（§5.1，TLS 记录会用到
大包）、DNS 和 ai_agent 的实际路径。Gate C 通过但 Gate E 失败几乎必然指向
§5.1 的 MTU 问题。

设备说明：

- **2 台 HyperOS 手机**：主验证平台。注意 HyperOS 可能要求用户在设置里
  显式为本设备授权"蓝牙网络共享"，首次连接需人工确认（列为已知前置操作，
  不算失败）。
- **iPhone 6**：iOS 通过"个人热点"提供 NAP。前置条件是**必须有可用蜂窝数据
  且个人热点已开启**——否则 iPhone 不会广播 NAP 服务，这属于测试前置条件
  不满足，不是实现缺陷。iPhone 用于验证我们的 BNEP 实现不依赖 Android 特有
  行为（这正是 D4 那种"HyperOS 非标准"猜测最容易翻车的地方）。

## 9. 风险与未知

| 风险 | 影响 | 处置 |
|---|---|---|
| LCPU（蓝牙控制器）持续数据吞吐行为未知——历史所有测试都只跑过控制面 | 可能在 Gate D/F 暴露固件层限制 | Gate D 一旦出现规律性丢包，先用 §6.2 的 pcap 区分是主机侧丢还是 LCPU 侧丢 |
| SRAM 基线 90.9% 是文档记录值，非实测 | §5.3 的预算可能不成立 | Gate G 之前先测一次裸基线；若不足则优先砍 `BT_BUF_ACL_RX_COUNT_EXTRA` 与调试设施 |
| `BT_RX_STACK_SIZE=1200` 是否够 | 大包时随机崩溃 | §5.2 的静态缓冲约束 + Gate D 用满 MTU 的包压测 |
| HyperOS 需要人工授权网络共享 | Gate C 无法自动化 | 接受人工确认，在测试脚本里明确提示 |
| iPhone 6 无蜂窝数据/未开热点 | Gate 的 iOS 分支无法执行 | 前置条件核对清单，不满足则该分支标记 blocked 而非 failed |

## 10. 明确不做（YAGNI）

- **不做 NAP / GN 角色**，只做 PANU。本设备是网络消费方。
- **不实现 BNEP 过滤器**（Network Protocol Type / Multicast Address）。
  收到 SET 请求一律回 `UNSUPPORTED`(0x0001)，规范允许。
- **不做 IPv6**。
- **第一版不做 TX 帧压缩**（§3.2）。只在 Gate F 吞吐不达标时才作为优化引入。
- **不重构 openvela 蓝牙框架的四层架构**。小米移植说明的三条约束
  （SAL API 作为协议栈接入桥梁、Framework 提供完整服务栈、协议栈需适配
  NuttX POSIX API）本项目**已全部满足**，重做架构不解决 BNEP 字节编码问题。
- **不从 xiaozhi 移植 BNEP**。xiaozhi 的 BNEP/L2CAP/SDP/GAP/HCI 全部在
  `sdk/middleware/bluetooth/lib/lib_bt_gcc.a`（12,174,618 字节闭源库）内，
  没有源码可移植。开源的 `bts2_app_pan.c` 只是调用方，其价值是**行为参考**
  （尤其 §4.1 的整帧传递、§4.3 的 DHCP 时序），已吸收进本设计。

  补充（2026-08-20）：尝试拉取 xiaozhi 的 `sdk` submodule **已失败**
  ——github.com 不可达（`Failed to connect to github.com port 443`，两次重试
  均超时）。因此 xiaozhi 侧的参考仅限于仓库内已有的开源文件
  （`bts2_app_pan.c`、`bt_lwip.c`、`app/src/main.c`、`proj.conf`），
  这些已全部读取并吸收。**本设计不对 SDK 有任何依赖**，该失败不阻塞任何 Gate。
  （注：pip 经镜像可用、apt 经清华镜像可用，仅 github 不可达——见 §6.2.1。）

## 11. 第二轮静态复核：新增缺陷 D8–D10 与 MTU 派生设计

写实施计划时逐行复核了数据路径的两端（`panu_service.c` 的 TAP 侧与
`sal_pan_interface.c` 的 L2CAP 侧），又找出两个此前完全没被记录的阻塞项。
两个都不影响小包，所以在只测 DHCP/ARP 的历史轮次里永远不会暴露，
但两个都会在真实业务流量（HTTPS 到 AI API）上必然触发。

### 11.1 D9：本地 TX 长度上限把 DHCP DISCOVER 直接拒掉（最高优先级）

`sal_pan_interface.c:73`

```c
#define PAN_TX_PAYLOAD_MAX (CONFIG_BT_L2CAP_TX_MTU - 3)
```

`CONFIG_BT_L2CAP_TX_MTU=253`（defconfig:322 实测），所以上限是 **250 字节**。
`bt_sal_pan_write()` 在 `:960-963` 用它做硬拒：

```c
if (length > PAN_TX_PAYLOAD_MAX) {
    BT_LOGW("%s frame too long %u", __func__, length);
    return BT_STATUS_NOMEM;
}
```

这里的 `length` 是**剥掉 14 字节以太头之后**的载荷长度。而一个 DHCP DISCOVER
的载荷是 IP(20) + UDP(8) + BOOTP：NuttX 的 `struct dhcp_msg`
（`apps/netutils/dhcpc/dhcpc.c:129` 附近，固定头 236 字节 + `options[312]`）
实际发送长度按 `len = pend - &packet` 计算，DISCOVER 大约 260–270 字节，
合计约 **290 字节 > 250**。

**也就是说 DHCP DISCOVER 从来没被发出去过，它在进 BNEP 编码之前就被本地拒了。**
这解释了为什么此前所有轮次都是"bt-pan UP 但拿不到 IP"，且抓不到任何 BNEP 数据帧。
ARP 请求载荷只有 28 字节，能过这道闸，所以只有 ARP 走到了 D1 那一步。

这个上限本身是没有依据的。zblue 的 BR/EDR 发送路径**不看**
`CONFIG_BT_L2CAP_TX_MTU`：`l2cap_br.c:1906` 只检查
`buf->len > br_chan->tx.mtu`，而 `tx.mtu` 来自对端 CONFIG_REQ 里的 MTU 选项
（`l2cap_br.c:1289`），没有 MTU 选项时兜底 `L2CAP_BR_DEFAULT_MTU`=672
（`:1448`）。Android/HyperOS NAP 会给 1691。再往下
`bt_l2cap_br_send_cb` → `bt_conn_send_cb`，由 conn.c 按控制器
`Read_Buffer_Size` 报的 ACL 长度做零拷贝分片，与 `CONFIG_BT_L2CAP_TX_MTU` 无关。
而 `pan_tx_pool` 本来就是按 `BT_L2CAP_BUF_SIZE(PAN_TX_MTU=1691)` 开的
（`:67-71`），缓冲区容量从一开始就够。

修正：TX 上限改为从**协商结果**派生，`conn->chan.tx.mtu` 减去本帧 BNEP 头长度。

### 11.2 D8：TAP 读缓冲偏小会让上行**永久卡死**（不是丢包）

`panu_service.c:654-663` 用 `pan_get_tun_packet_size()` 的返回值开读缓冲：

```c
ret = pan_get_tun_packet_size(PAN_DEV_NAME);
g_pan.tun_packet_size = ret;
pan_read_buf = malloc(g_pan.tun_packet_size);
```

而 `pan_get_tun_packet_size()`（`:608-631`）返回的是
`ifr_mtu - sizeof(eth_hdr_t)`，也就是 **MTU 再减 14**。
NuttX 侧的实际数值链是：`netdev_register.c:318-323` 对 `NET_LL_ETHERNET`
设 `llhdrlen=14`、`pktsize=CONFIG_NET_ETH_PKTSIZE`；defconfig:194
`CONFIG_NET_ETH_PKTSIZE=1514`；`netdev_ioctl.c:1020-1022` 的 `SIOCGIFMTU`
返回 `pktsize - llhdrlen` = **1500**。于是 `tun_packet_size = 1486`，
`pan_read_buf` 只有 1486 字节，而 `:585` 就用这个长度去 `read()`。

关键在于 NuttX TUN 驱动对"缓冲太小"的处理不是截断，是拒绝且**不出队**
（`nuttx/drivers/net/tun.c:1127-1133`）：

```c
if (priv->read_d_len > 0)
  {
    if (buflen < priv->read_d_len)
      {
        ret = -EINVAL;
        break;      /* 注意：read_d_len 没有被清零，帧还在队列里 */
      }
```

`read_d_len` 是含 14 字节以太头的整帧长度，最大 1514。所以只要 IP 栈产生一个
1487–1514 字节的帧（一个满 MSS 的 TCP 段正好是 1514 字节，
`TCP_MSS = 1514-14-20-20 = 1460`），`read()` 就永远返回 `-EINVAL`，
该帧永远留在队列里，poll 永远可读——**上行方向从此彻底停摆**，
不是掉一个包而是整条接口死掉。DHCP 阶段包都小，所以历史测试测不出来。

修正：读缓冲按 `CONFIG_NET_ETH_PKTSIZE`（1514）开，不要用 `MTU-14`；
`read()` 也传同一个长度。同时 `pan_on_data_received()` 里
`malloc(tun_packet_size + sizeof(ethhdr))` 和 `:896` 的长度校验也要跟着改成
按整帧上限，否则下行会在同一个数字上出现对称的错误。

### 11.3 MTU 派生：一处协商，两端对齐

D8 和 D9 的共同根因是：**三个 MTU 各自独立瞎猜**——TAP 的 MTU 由 NuttX
按以太网默认给 1500，BNEP 的 TX 上限由一个和 BR 路径无关的 Kconfig 给 250，
L2CAP 的 `tx.mtu` 由对端协商给 1691 或 672，三者之间没有任何约束关系。
逐个打补丁只会再埋新的不一致，所以第一版就把它做成单一派生链：

```
L2CAP CONFIG 协商完成 → br_chan->tx.mtu (权威值)
        ↓ 减去最坏情况 BNEP 头 14 字节（General Ethernet）
BNEP 可承载的以太载荷上限 = tx.mtu - 14
        ↓ 通过 SIOCSIFMTU 写回
TAP 接口 MTU = min(1500, tx.mtu - 14)
        ↓ IP 栈据此算 TCP MSS，从此不会产生超限帧
```

具体规则：

1. BNEP setup 成功后、`ifup` 之前，读 `conn->chan.tx.mtu`，
   用 `SIOCSIFMTU` 把 TAP 的 MTU 设成 `min(1500, tx.mtu - 14)`。
   `netdev_ioctl.c:1023-1025` 的 `SIOCSIFMTU` 会把
   `d_pktsize` 设成 `ifr_mtu + 14`，IP 栈的 `TCP_MSS`/`UDP_MSS`
   都是从 `d_pktsize` 推导的，所以这一步之后上层永远不会造出超限帧。
2. TX 侧仍然保留一道运行时检查，但阈值取 `conn->chan.tx.mtu`
   而不是编译期常量；触发即说明 MTU 派生链坏了，按 `BT_STATUS_NOMEM`
   返回并计数告警（正常流量下计数必须恒为 0，这是 Gate F 的追加判据之一）。
3. 读缓冲固定 `CONFIG_NET_ETH_PKTSIZE`=1514，与 MTU 协商结果解耦——
   缓冲开大是免费的（一次 malloc），开小是永久卡死，不对称，所以取上界。
4. `tx.mtu < 1691`（例如兜底 672）不再是错误路径，而是自动降级为
   MTU 658 的可用链路。这同时去掉了对"对端必须给 1691"的隐含依赖。

### 11.4 D10：`profile_service_t.uuid` 的定性更正

见 §7.2。原判断"全零 UUID 导致 SDP 记录不对"是错的，已作废：真正的 SDP 记录
在 `sal_pan_interface.c:423-458` 且本来就正确。`.uuid` 只影响本机
`bt_adapter_get_uuids()` 的自查上报，对手机侧行为零影响，降级为顺手修正。

### 11.5 对 §2/§8 的影响

- 缺陷总数 7 → 10。修复优先级重排为 **D9 → D8 → D1 → D2/D3/D4/D5/D6 → D7 → D10**：
  D9 不修则一个字节都发不出去，D1 的编码正确性无法被观测。
- Gate B（离线 pcap 判定）现在必须**先看到 DHCP DISCOVER 出现在 trace 里**，
  再看它的 BNEP 头是否合规——前者验 D9，后者验 D1。
- **Gate F 追加判据**：满 MSS 上行（1514 字节帧）连续跑通，
  且 TX 超限计数器 `tx_oversize` 恒为 0（验 D8 + §11.3）。
- **Gate C 入门判据**：运行时回读 COD，确认 bit 17 已置（验 §7.1 的持久化覆盖
  问题）。放在 Gate C 之前是因为 Networking 位是手机愿不愿意向我们提供网络共享
  的前提——这一条不过，DHCP 失败的原因就无法归因到 BNEP。
  两条判据都已写进 §8 的门表，不新增字母门。




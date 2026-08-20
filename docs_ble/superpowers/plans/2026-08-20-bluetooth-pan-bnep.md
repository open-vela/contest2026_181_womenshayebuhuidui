# 蓝牙 PAN / BNEP 数据面重写 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让 `bt-pan` 接口经手机 NAP 取得 DHCP 地址并承载 ai_agent 的 HTTPS AI API 调用，在 2 台 HyperOS + 1 台 iPhone 6 上可重复、可长稳。

**Architecture:** 把 BNEP 编解码抽成一个零依赖的纯函数层 `bnep_codec.[ch]`（不引用 zblue、不引用 NuttX），使它能在开发机上用 gcc 直接单元测试；SAL 层 `sal_pan_interface.c` 只负责 zblue 通道管理与状态机，profile 层 `panu_service.c` 只负责 TAP 与网络接口。所有以太头的解析责任集中在 codec 一处，消除现有实现"上层拆开、下层丢弃"的结构性缺陷。

**Tech Stack:** C（NuttX / openvela，C11）、zblue（Zephyr BT host 移植）、NuttX TUN/TAP、CMake（`frameworks/connectivity/bluetooth/CMakeLists.txt`）、host gcc 11.4 + 自写断言宏做单元测试、Python 3 生成 pcap、用户态 tshark 3.6.2 做独立裁判。

**Spec:** `docs_ble/superpowers/specs/2026-08-20-bluetooth-pan-design.md`

## Global Constraints

- 允许改动的目录：`frameworks/connectivity/bluetooth/`、`external/zblue/`、`vendor/sifli/`、`contest2026_181_womenshayebuhuidui/board/contest_board/configs/`、`docs_ble/`。**`packages/ai_agent/` 源码不得修改**——对它的影响只通过 defconfig 表达。
- 目标 defconfig：`contest2026_181_womenshayebuhuidui/board/contest_board/configs/ai_agent/defconfig`。
- 构建产物目录：`out/nuttx_contest_board_ai_agent/`。
- BNEP L2CAP PSM = `0x000F`。PAN 角色只做 PANU（UUID16 `0x1115`），对端为 NAP（`0x1116`）。
- BNEP 的目标 L2CAP MTU 是 **1691**（Android/HyperOS NAP 会协商到这个值），但**任何长度上限都必须从 `br_chan->tx.mtu` 派生，不得使用编译期常量**。`CONFIG_BT_L2CAP_TX_MTU` 与 BR/EDR 发送路径无关（`l2cap_br.c:1906` 只比 `tx.mtu`），用它做闸门就是 spec §11.1 的 D9。对端只给兜底 672 时链路降级为 MTU 658 可用，不算错误。
- TAP 读缓冲固定 `CONFIG_NET_ETH_PKTSIZE`（1514），**不得**用 `MTU-14`：NuttX `tun_read()` 在 `buflen < 整帧长` 时返回 `-EINVAL` 且不出队（`nuttx/drivers/net/tun.c:1127-1133`），会让上行永久卡死而不是丢包（spec §11.2 的 D8）。
- `CONFIG_BT_RX_STACK_SIZE=1200`——**任何 RX 回调路径上不得出现 > 256 字节的栈数组**。大缓冲必须是静态或堆内存。
- 严格响应码判定：Setup Connection Response 只有 `0x0000` 判成功，**禁止任何 "treat as success anyway" 分支**。
- 每个 Task 结束时必须能独立编译通过；codec 相关 Task 还必须单元测试全绿。
- 提交在 git 仓库 `contest2026_181_womenshayebuhuidui`（分支 `feat/ai-agent-contest`）。`frameworks/` 是独立 git 仓库，其改动单独提交。**不 push。**

### 已由独立工具验证的字节序列（不得改动）

| 内容 | 字节 | 验证者 |
|---|---|---|
| PANU→NAP Setup Connection Request | `01 01 02 11 16 11 15` | tshark 3.6.2 btbnep dissector |
| Setup Connection Response 成功 | `01 02 00 00` | 同上（解析为 *Operation Successful*） |
| General Ethernet + 广播 ARP | `00` + `ff*6` + src6 + `08 06` + ARP | 同上（一路解析到 ARP） |

### 计划内代码的预先验证记录

T1–T3 的 codec 源码与测试台已在开发机上**整体抽出编译运行过一遍**
（gcc 11.4，`-Wall -Wextra -Werror -O1 -fsanitize=address,undefined`，
无告警、sanitizer 全程静默），并按结果修正了计划本身的两处错误：

1. `bnep_encode_eth` 的 `src_omittable` 漏了广播/组播守卫，导致广播帧被编成
   `0x04 DEST_ONLY`（实测 type=0x04、n=37，应为 0x00、n=43）。已在 T2 Step 3 修正。
2. `test_decode_roundtrip_all_types` 的 General 用例误传 `compress=true`，
   使编码器合法地省掉 src，roundtrip 必然不等。已在 T3 Step 1 改为 `false` 并加注说明。

修正后 15 个 `test_*()` 函数、69 条断言全部通过。T4 的 `bnep_pcap.py` 也已用
假日志跑通并经 tshark 判定（4 个 ACL 包全部正确解析为 L2CAP Connect Req/Rsp +
BNEP Setup Req/Rsp）。

**第二次独立复验（改完计划之后又跑了一遍）**：同样把三段代码从本文档里抽出来、
在 `/tmp` 下用同一组编译选项构建，`gcc` 退出码 0 且零诊断输出，`./t` 输出
`all checks passed`，ASan/UBSan（含 `detect_leaks=1`、
`detect_stack_use_after_return=1`、`halt_on_error=1`）全程静默。
这一轮又抓到计划本身的两处错误，都已修掉：

3. 测试台 Makefile 的 `CODEC_DIR` 深度不对。`docs_ble` 是符号链接，
   `../../../frameworks` 会被编译器按物理路径解析到
   `contest2026_181_womenshayebuhuidui/frameworks`（不存在）。更麻烦的是它的报错
   与 Task 1 Step 3 那个**故意的**预期失败一字不差，会把人骗住。已改为从
   Makefile 自身位置推导四级并用 `$(info)` 打印出来自查，且已在 `/tmp` 下用
   同构的符号链接布局验证过：`CODEC_DIR` 正确指向 `vela_contest/frameworks/...`，
   `make test` 编译并运行成功。
4. Task 2/Task 3 的"追加到 `test_bnep_codec.c`"若字面执行，会把新 `test_*()`
   放到 `main()` 后面，`-Werror=implicit-function-declaration` 直接编译失败
   （复验时专门构建了这个字面版本确认过）。两处都已改成明写"插在 `main()` 之前"。

顺带修掉一处不一致：T5/T7 曾引用 `make ... run`，而 Makefile 只有
`all`/`test`/`clean` 三个目标，已统一为 `test`。

**因此 T1–T4 的代码块可以直接照抄，不需要再"设计"。**

---

## File Structure

**新建**

| 文件 | 职责 |
|---|---|
| `frameworks/connectivity/bluetooth/service/stacks/zephyr/bnep_codec.h` | BNEP 常量表 + 编解码 API + MAC 字节序转换。零依赖（只 `<stdint.h> <stddef.h> <stdbool.h>`）。 |
| `frameworks/connectivity/bluetooth/service/stacks/zephyr/bnep_codec.c` | 编解码实现。无 syslog、无 malloc、无全局状态。 |
| `docs_ble/tools/bnep_codec_test/test_bnep_codec.c` | host 单元测试（自写断言宏，无外部框架）。 |
| `docs_ble/tools/bnep_codec_test/Makefile` | host gcc 编译 + 运行。 |
| `docs_ble/tools/bnep_pcap.py` | 串口日志 → pcap（linktype 201）。 |
| `docs_ble/tools/setup_tshark.sh` | 用户态安装 tshark（无 root）。 |
| `docs_ble/tools/gate_b.sh` | Gate B 一键：日志 → pcap → tshark → 判定。 |
| `docs_ble/tools/mk_pandbg_config.sh` | 从产品 defconfig 生成调试 board config（Gate B 用）。 |
| `docs_ble/tools/gate_c.py` | Gate C：30 次连接自动打分。 |
| `docs_ble/tools/gate_h.py` | Gate H：2 小时长稳 + 断链自愈。 |
| `docs_ble/superpowers/plans/2026-08-20-gate-results.md` | Gate 实测结果记录（唯一验收产物）。 |

**修改**

| 文件 | 改什么 |
|---|---|
| `.../stacks/include/sal_pan_interface.h` | 删掉错误的枚举，改为 `#include "bnep_codec.h"` + 保留 SAL 层自己的类型 |
| `.../stacks/zephyr/sal_pan_interface.c` | 控制路径与数据路径全部改走 codec；新增 `bt_sal_pan_write_eth`；每连接一个静态 RX 重组缓冲；TX 上限改从 `tx.mtu` 派生（修 D9）；导出 `bt_sal_pan_get_tx_mtu()` 给 profile 层设 TAP MTU |
| `.../profiles/pan/panu_service.c` | TAP 整帧收发、读缓冲改 1514（修 D8）、按协商 MTU 设 `SIOCSIFMTU`、DHCP 线程生命周期修正、状态日志字段固定化、`.uuid` 顺手改 UUID16 |
| `.../service/common/bluetooth_define.h:29-33` | COD 宏名从单前缀改双前缀，`#else` 兜底值改 `0x002A0704` |
| `.../framework/include/bt_uuid.h` | 补 `BT_UUID_PANU 0x1115` / `BT_UUID_NAP 0x1116` 宏 |
| `frameworks/connectivity/bluetooth/CMakeLists.txt:314` | 追加 `bnep_codec.c` |
| `board/contest_board/configs/ai_agent/defconfig` | COD（双前缀符号）/ ACL RX / GATT off；删掉 `:323` 的死配置行 |
| `vendor/sifli/chips/sf32lb52/sf32lb52_bth4.c` | HCI trace 打全长、修 RX 打印偏移 |
| `vendor/sifli/chips/sf32lb52/Kconfig` | 新增 `SF32LB52_BT_TRACE_ACL_FULL`（默认 n，仅调试构建开） |

**注意 `netlib_setmacaddr` 不需要动。** 前一轮曾计划补 `SIOCSIFHWADDR` 的顺序，
实际读代码后作废：`panu_service.c:561-562` 里 `netlib_setmacaddr` 已经在
`netlib_ifup` 之前，顺序本来就是对的（NuttX 要求 MAC 在 ifup 前设，
见 `netdev_ioctl.c:1117` 的注释）。T7 只是把 `ifup` 挪到 BNEP 握手之后，
`setmacaddr` 留在原位。

**依赖顺序**：T1 → T2 → T3 → T4（可与 T5 并行）→ T5 → T6 → T7 → T8 → T9 → T10。

T6 与 T7 之间**不能停**：T6 删掉 `bt_sal_pan_write` 而 T7 才改调用方，
中间构建是断的。两者共用一次提交（T7 Step 8）。

---

## Task 1: BNEP codec 骨架 + 控制帧 + host 测试台

**Files:**
- Create: `frameworks/connectivity/bluetooth/service/stacks/zephyr/bnep_codec.h`
- Create: `frameworks/connectivity/bluetooth/service/stacks/zephyr/bnep_codec.c`
- Create: `docs_ble/tools/bnep_codec_test/test_bnep_codec.c`
- Create: `docs_ble/tools/bnep_codec_test/Makefile`

**Interfaces:**
- Consumes: 无（本 Task 是根）
- Produces: 后续 Task 依赖以下**确定签名**：
  - `void bnep_mac_from_le48(uint8_t mac[6], const uint8_t le48[6])`
  - `int bnep_encode_setup_req(uint8_t *out, size_t cap, uint16_t dst_uuid16, uint16_t src_uuid16)`
  - `int bnep_encode_setup_rsp(uint8_t *out, size_t cap, uint16_t rsp_code)`
  - `int bnep_encode_filter_rsp(uint8_t *out, size_t cap, uint8_t rsp_msg_type, uint16_t rsp_code)`
  - `int bnep_encode_cmd_not_understood(uint8_t *out, size_t cap, uint8_t unknown_type)`
  - `int bnep_parse_control(const uint8_t *ctrl, size_t len, struct bnep_control *info)`
  - `enum { BNEP_OK = 0, BNEP_ERR_TRUNCATED = -1, BNEP_ERR_NOSPACE = -2, BNEP_ERR_BADTYPE = -3, BNEP_ERR_BADUUID = -4 }`

- [ ] **Step 1: 建测试台 Makefile**

创建 `docs_ble/tools/bnep_codec_test/Makefile`：

```make
# host 单元测试：直接编译 codec 源码，不链接 zblue/NuttX
#
# docs_ble 是个符号链接（-> contest2026_181_womenshayebuhuidui/docs_ble），而
# frameworks/ 是那个仓库的兄弟目录。写 ../../../frameworks 会被编译器按物理路径
# 解析、落到 contest2026_181_womenshayebuhuidui/frameworks（不存在）——而报错
# 恰好也是 "bnep_codec.h: No such file"，跟"头文件还没写"这个预期失败长得一样，
# 排查起来会很费时间。所以从 Makefile 自身位置推导，并打印出来自查。
MKFILE_DIR := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
CODEC_DIR := $(abspath $(MKFILE_DIR)/../../../../frameworks/connectivity/bluetooth/service/stacks/zephyr)
$(info CODEC_DIR = $(CODEC_DIR))

CFLAGS := -std=c11 -Wall -Wextra -Werror -g -O1 -fsanitize=address,undefined \
          -I$(CODEC_DIR)
LDFLAGS := -fsanitize=address,undefined

.PHONY: all test clean
all: test

bnep_codec_test: test_bnep_codec.c $(CODEC_DIR)/bnep_codec.c $(CODEC_DIR)/bnep_codec.h
	$(CC) $(CFLAGS) -o $@ test_bnep_codec.c $(CODEC_DIR)/bnep_codec.c $(LDFLAGS)

test: bnep_codec_test
	./bnep_codec_test

clean:
	rm -f bnep_codec_test
```

`-fsanitize=address,undefined` 是刻意的：codec 全是指针与长度算术，ASan/UBSan
能在开发机上抓住越界，而这些越界在板子上只表现为随机崩溃。

- [ ] **Step 2: 写第一批失败测试（控制帧）**

创建 `docs_ble/tools/bnep_codec_test/test_bnep_codec.c`：

```c
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "bnep_codec.h"

static int g_fail;
#define CHECK(cond) do { \
    if (!(cond)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); g_fail++; } \
} while (0)
#define CHECK_MEM(got, want, n) do { \
    if (memcmp((got), (want), (n)) != 0) { \
        printf("FAIL %s:%d  bytes differ\n  got : ", __FILE__, __LINE__); \
        for (size_t _i = 0; _i < (size_t)(n); _i++) printf("%02x ", ((const uint8_t*)(got))[_i]); \
        printf("\n  want: "); \
        for (size_t _i = 0; _i < (size_t)(n); _i++) printf("%02x ", ((const uint8_t*)(want))[_i]); \
        printf("\n"); g_fail++; \
    } \
} while (0)
```

接着在同一文件里追加控制帧测试：

```c
/* tshark 已独立确认：01 01 02 11 16 11 15 */
static void test_setup_req(void)
{
    uint8_t out[16];
    const uint8_t want[] = { 0x01, 0x01, 0x02, 0x11, 0x16, 0x11, 0x15 };
    int n = bnep_encode_setup_req(out, sizeof(out),
                                  BNEP_UUID16_NAP, BNEP_UUID16_PANU);
    CHECK(n == 7);
    if (n == 7) { CHECK_MEM(out, want, 7); }

    /* 容量不足必须报错而不越界 */
    CHECK(bnep_encode_setup_req(out, 6, BNEP_UUID16_NAP, BNEP_UUID16_PANU)
          == BNEP_ERR_NOSPACE);
}

/* tshark 已独立确认：01 02 00 00 = Operation Successful */
static void test_setup_rsp(void)
{
    uint8_t out[16];
    const uint8_t want[] = { 0x01, 0x02, 0x00, 0x00 };
    int n = bnep_encode_setup_rsp(out, sizeof(out), BNEP_RSP_SUCCESS);
    CHECK(n == 4);
    if (n == 4) { CHECK_MEM(out, want, 4); }

    const uint8_t want_fail[] = { 0x01, 0x02, 0x00, 0x03 };
    n = bnep_encode_setup_rsp(out, sizeof(out), BNEP_RSP_INVALID_UUID_SIZE);
    CHECK(n == 4);
    if (n == 4) { CHECK_MEM(out, want_fail, 4); }
}

static void test_parse_control_setup_rsp(void)
{
    struct bnep_control info;
    /* 标准 4 字节响应，成功 */
    const uint8_t ok[] = { 0x02, 0x00, 0x00 };  /* 不含帧头字节 0x01 */
    CHECK(bnep_parse_control(ok, sizeof(ok), &info) == BNEP_OK);
    CHECK(info.msg_type == BNEP_CTRL_SETUP_CONN_RSP);
    CHECK(info.rsp_code == BNEP_RSP_SUCCESS);
    CHECK(info.is_success == true);

    /* 0x0003 = Invalid Service UUID Size —— 必须判失败 */
    const uint8_t bad[] = { 0x02, 0x00, 0x03 };
    CHECK(bnep_parse_control(bad, sizeof(bad), &info) == BNEP_OK);
    CHECK(info.rsp_code == BNEP_RSP_INVALID_UUID_SIZE);
    CHECK(info.is_success == false);

    /* 截断输入 */
    const uint8_t trunc[] = { 0x02, 0x00 };
    CHECK(bnep_parse_control(trunc, sizeof(trunc), &info) == BNEP_ERR_TRUNCATED);
}
```

再追加 Setup Request 解析、过滤器响应、未知控制消息、MAC 字节序，以及 `main`：

```c
static void test_parse_control_setup_req(void)
{
    struct bnep_control info;
    /* 对端要求我们当 NAP（不支持）：dst=NAP src=PANU，UUID Size=2 */
    const uint8_t req[] = { 0x01, 0x02, 0x11, 0x16, 0x11, 0x15 };
    CHECK(bnep_parse_control(req, sizeof(req), &info) == BNEP_OK);
    CHECK(info.msg_type == BNEP_CTRL_SETUP_CONN_REQ);
    CHECK(info.uuid_size == 2);
    CHECK(info.dst_uuid16 == BNEP_UUID16_NAP);
    CHECK(info.src_uuid16 == BNEP_UUID16_PANU);

    /* UUID Size = 16 也必须能解析（取低 16 位） */
    uint8_t req16[2 + 32] = { 0x01, 0x10 };
    req16[2 + 2] = 0x11; req16[2 + 3] = 0x15;          /* dst 128-bit 的 UUID16 段 */
    req16[2 + 16 + 2] = 0x11; req16[2 + 16 + 3] = 0x16;/* src */
    CHECK(bnep_parse_control(req16, sizeof(req16), &info) == BNEP_OK);
    CHECK(info.uuid_size == 16);
    CHECK(info.dst_uuid16 == 0x1115);
    CHECK(info.src_uuid16 == 0x1116);

    /* 非法 UUID Size */
    const uint8_t bad[] = { 0x01, 0x08, 0, 0, 0, 0, 0, 0, 0, 0 };
    CHECK(bnep_parse_control(bad, sizeof(bad), &info) == BNEP_ERR_BADUUID);
}

static void test_filter_and_unknown(void)
{
    uint8_t out[8];
    const uint8_t want_f[] = { 0x01, 0x04, 0x00, 0x01 };  /* NetType RSP, Unsupported */
    int n = bnep_encode_filter_rsp(out, sizeof(out),
                                   BNEP_CTRL_FILTER_NET_TYPE_RSP,
                                   BNEP_FILTER_RSP_UNSUPPORTED);
    CHECK(n == 4);
    if (n == 4) { CHECK_MEM(out, want_f, 4); }

    const uint8_t want_u[] = { 0x01, 0x00, 0x7f };
    n = bnep_encode_cmd_not_understood(out, sizeof(out), 0x7f);
    CHECK(n == 3);
    if (n == 3) { CHECK_MEM(out, want_u, 3); }
}

static void test_mac_byte_order(void)
{
    /* zblue bt_addr_t.val 是小端；MAC 是网络序，必须整体反转 */
    const uint8_t le48[6] = { 0x66, 0x55, 0x44, 0x33, 0x22, 0x11 };
    const uint8_t want[6] = { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66 };
    uint8_t mac[6];
    bnep_mac_from_le48(mac, le48);
    CHECK_MEM(mac, want, 6);
}
```

```c
int main(void)
{
    test_setup_req();
    test_setup_rsp();
    test_parse_control_setup_rsp();
    test_parse_control_setup_req();
    test_filter_and_unknown();
    test_mac_byte_order();

    if (g_fail) { printf("\n%d check(s) FAILED\n", g_fail); return 1; }
    printf("all checks passed\n");
    return 0;
}
```

> Task 2 / Task 3 会往 `main()` 里追加更多 `test_*()` 调用。追加时保持
> 「先声明函数、再在 main 里按顺序调用」的写法，不要重排已有调用。

- [ ] **Step 3: 运行测试，确认失败**

```bash
cd /home/aila/projects/vela_contest/docs_ble/tools/bnep_codec_test
make test
```

Expected: 先看到一行 `CODEC_DIR = /home/aila/projects/vela_contest/frameworks/...`
（**路径必须是这个**，不是 `contest2026_181_womenshayebuhuidui/frameworks/...`），
然后编译失败：`fatal error: bnep_codec.h: No such file or directory`。

`CODEC_DIR` 印错了就是 Makefile 的路径深度不对，不是"头文件还没写"——
两者的报错文字一模一样，靠这行 `$(info)` 区分。

- [ ] **Step 4: 写 `bnep_codec.h`**

创建 `frameworks/connectivity/bluetooth/service/stacks/zephyr/bnep_codec.h`。
先写文件头与常量（与 spec §3.1 逐字一致）：

```c
/****************************************************************************
 * BNEP (Bluetooth Network Encapsulation Protocol) codec.
 *
 * Zero-dependency by design: no zblue, no NuttX, no libc beyond memcpy.
 * This is what makes it unit-testable on the host (docs_ble/tools/
 * bnep_codec_test). Keep it that way.
 ****************************************************************************/

#ifndef BNEP_CODEC_H_
#define BNEP_CODEC_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Frame types: low 7 bits of the BNEP header byte. Bit 7 = extension flag. */
#define BNEP_TYPE_MASK                      0x7f
#define BNEP_EXT_FLAG                       0x80
#define BNEP_GENERAL_ETHERNET               0x00  /* + dst6 + src6 + proto2 */
#define BNEP_CONTROL                        0x01
#define BNEP_COMPRESSED_ETHERNET            0x02  /* + proto2              */
#define BNEP_COMPRESSED_ETHERNET_SRC_ONLY   0x03  /* + src6 + proto2       */
#define BNEP_COMPRESSED_ETHERNET_DEST_ONLY  0x04  /* + dst6 + proto2       */
```

```c
/* Control message types (second byte of a BNEP_CONTROL frame). Note these
 * live in a DIFFERENT namespace than the frame types above -- 0x02 means
 * "Setup Connection Response" here and "Compressed Ethernet" there. Never
 * switch on both in one statement. */
#define BNEP_CTRL_CMD_NOT_UNDERSTOOD        0x00
#define BNEP_CTRL_SETUP_CONN_REQ            0x01
#define BNEP_CTRL_SETUP_CONN_RSP            0x02
#define BNEP_CTRL_FILTER_NET_TYPE_SET       0x03
#define BNEP_CTRL_FILTER_NET_TYPE_RSP       0x04
#define BNEP_CTRL_FILTER_MULTI_ADDR_SET     0x05
#define BNEP_CTRL_FILTER_MULTI_ADDR_RSP     0x06

/* Setup Connection Response codes (2 bytes, big endian). */
#define BNEP_RSP_SUCCESS                    0x0000
#define BNEP_RSP_INVALID_DST_UUID           0x0001
#define BNEP_RSP_INVALID_SRC_UUID           0x0002
#define BNEP_RSP_INVALID_UUID_SIZE          0x0003
#define BNEP_RSP_CONN_NOT_ALLOWED           0x0004

/* Filter response codes (2 bytes, big endian). */
#define BNEP_FILTER_RSP_ACCEPTED            0x0000
#define BNEP_FILTER_RSP_UNSUPPORTED         0x0001
#define BNEP_FILTER_RSP_INVALID_RANGE       0x0002
#define BNEP_FILTER_RSP_TOO_MANY            0x0003

/* PAN service UUIDs (16-bit). */
#define BNEP_UUID16_PANU                    0x1115
#define BNEP_UUID16_NAP                     0x1116
#define BNEP_UUID16_GN                      0x1117

/* Sizing. BNEP requires an L2CAP MTU of at least 1691. */
#define BNEP_MIN_L2CAP_MTU                  1691
#define BNEP_ETH_HDR_LEN                    14
#define BNEP_MAX_ETH_FRAME                  1514  /* 14 + 1500 */

/* Return codes. Non-negative return values are byte counts. */
enum {
    BNEP_OK             =  0,
    BNEP_ERR_TRUNCATED  = -1,
    BNEP_ERR_NOSPACE    = -2,
    BNEP_ERR_BADTYPE    = -3,
    BNEP_ERR_BADUUID    = -4,
};

/* Parsed BNEP control message. */
struct bnep_control {
    uint8_t  msg_type;    /* BNEP_CTRL_*                                   */
    uint8_t  uuid_size;   /* SETUP_CONN_REQ only: 2, 4 or 16               */
    uint16_t dst_uuid16;  /* SETUP_CONN_REQ only: low 16 bits of dst UUID   */
    uint16_t src_uuid16;  /* SETUP_CONN_REQ only: low 16 bits of src UUID   */
    uint16_t rsp_code;    /* SETUP_CONN_RSP / FILTER_*_RSP                  */
    bool     is_success;  /* SETUP_CONN_RSP: rsp_code == BNEP_RSP_SUCCESS   */
    uint8_t  unknown_type;/* CMD_NOT_UNDERSTOOD payload                     */
};
```

最后是 API 与内联转换器（`bnep_encode_eth` / `bnep_decode_eth` 在本 Task 只声明，
Task 2 / Task 3 才实现——未被引用的声明不会导致链接错误）：

```c
/* zblue's bt_addr_t.val is little-endian; Ethernet MACs are network order.
 * Getting this wrong lets the handshake and unicast work while ARP never
 * resolves, which reads like "the phone is not answering". */
static inline void bnep_mac_from_le48(uint8_t mac[6], const uint8_t le48[6])
{
    for (int i = 0; i < 6; i++) { mac[i] = le48[5 - i]; }
}

static inline bool bnep_mac_is_broadcast(const uint8_t mac[6])
{
    return (mac[0] & mac[1] & mac[2] & mac[3] & mac[4] & mac[5]) == 0xff;
}

static inline bool bnep_mac_is_multicast(const uint8_t mac[6])
{
    return (mac[0] & 0x01) != 0;
}

/* --- Control frames. Each writes a complete BNEP frame including the
 *     0x01 header byte, and returns the byte count. --- */
int bnep_encode_setup_req(uint8_t *out, size_t cap,
                          uint16_t dst_uuid16, uint16_t src_uuid16);
int bnep_encode_setup_rsp(uint8_t *out, size_t cap, uint16_t rsp_code);
int bnep_encode_filter_rsp(uint8_t *out, size_t cap,
                           uint8_t rsp_msg_type, uint16_t rsp_code);
int bnep_encode_cmd_not_understood(uint8_t *out, size_t cap,
                                   uint8_t unknown_type);

/* Parses a control payload -- ctrl points at the MESSAGE TYPE byte, i.e.
 * one byte past the 0x01 frame header. Returns BNEP_OK or an error. */
int bnep_parse_control(const uint8_t *ctrl, size_t len,
                       struct bnep_control *info);

/* --- Data frames (implemented in Task 2 / Task 3). --- */

/* Encodes a complete Ethernet frame (14-byte header included) as BNEP.
 * Broadcast and multicast destinations are always sent as General
 * Ethernet regardless of `compress`, because the receiver would otherwise
 * reconstruct the destination as its own unicast MAC. */
int bnep_encode_eth(uint8_t *out, size_t cap,
                    const uint8_t *eth_frame, size_t eth_len,
                    const uint8_t local_mac[6], const uint8_t peer_mac[6],
                    bool compress);

/* Decodes a BNEP frame into a complete Ethernet frame (14-byte header
 * reconstructed). Skips every extension header. For a control frame,
 * returns BNEP_DECODE_IS_CONTROL and sets *ctrl_out / *ctrl_len. */
#define BNEP_DECODE_IS_CONTROL (-100)
int bnep_decode_eth(uint8_t *out, size_t cap,
                    const uint8_t *bnep, size_t bnep_len,
                    const uint8_t local_mac[6], const uint8_t peer_mac[6],
                    const uint8_t **ctrl_out, size_t *ctrl_len);

#endif /* BNEP_CODEC_H_ */
```

- [ ] **Step 5: 写 `bnep_codec.c` 的控制帧部分**

创建 `frameworks/connectivity/bluetooth/service/stacks/zephyr/bnep_codec.c`：

```c
#include "bnep_codec.h"

#include <string.h>

static inline void put_be16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xff);
}

static inline uint16_t get_be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

int bnep_encode_setup_req(uint8_t *out, size_t cap,
                          uint16_t dst_uuid16, uint16_t src_uuid16)
{
    if (cap < 7) { return BNEP_ERR_NOSPACE; }

    out[0] = BNEP_CONTROL;
    out[1] = BNEP_CTRL_SETUP_CONN_REQ;
    out[2] = 2;                       /* UUID Size: ONE byte, not two */
    put_be16(&out[3], dst_uuid16);
    put_be16(&out[5], src_uuid16);
    return 7;
}

int bnep_encode_setup_rsp(uint8_t *out, size_t cap, uint16_t rsp_code)
{
    if (cap < 4) { return BNEP_ERR_NOSPACE; }

    out[0] = BNEP_CONTROL;
    out[1] = BNEP_CTRL_SETUP_CONN_RSP;
    put_be16(&out[2], rsp_code);      /* no length field exists here */
    return 4;
}

int bnep_encode_filter_rsp(uint8_t *out, size_t cap,
                           uint8_t rsp_msg_type, uint16_t rsp_code)
{
    if (rsp_msg_type != BNEP_CTRL_FILTER_NET_TYPE_RSP &&
        rsp_msg_type != BNEP_CTRL_FILTER_MULTI_ADDR_RSP) {
        return BNEP_ERR_BADTYPE;
    }
    if (cap < 4) { return BNEP_ERR_NOSPACE; }

    out[0] = BNEP_CONTROL;
    out[1] = rsp_msg_type;
    put_be16(&out[2], rsp_code);
    return 4;
}
```

继续追加到同一文件：

```c
int bnep_encode_cmd_not_understood(uint8_t *out, size_t cap,
                                   uint8_t unknown_type)
{
    if (cap < 3) { return BNEP_ERR_NOSPACE; }

    out[0] = BNEP_CONTROL;
    out[1] = BNEP_CTRL_CMD_NOT_UNDERSTOOD;
    out[2] = unknown_type;
    return 3;
}

int bnep_parse_control(const uint8_t *ctrl, size_t len,
                       struct bnep_control *info)
{
    if (!ctrl || !info || len < 1) { return BNEP_ERR_TRUNCATED; }

    memset(info, 0, sizeof(*info));
    info->msg_type = ctrl[0];

    switch (ctrl[0]) {
    case BNEP_CTRL_CMD_NOT_UNDERSTOOD:
        if (len < 2) { return BNEP_ERR_TRUNCATED; }
        info->unknown_type = ctrl[1];
        return BNEP_OK;

    case BNEP_CTRL_SETUP_CONN_REQ: {
        if (len < 2) { return BNEP_ERR_TRUNCATED; }
        uint8_t usz = ctrl[1];
        if (usz != 2 && usz != 4 && usz != 16) { return BNEP_ERR_BADUUID; }
        if (len < (size_t)(2 + 2 * usz)) { return BNEP_ERR_TRUNCATED; }
        info->uuid_size = usz;
        /* The 16-bit value lives in the last two bytes of a 2- or 4-byte
         * UUID, and at offset 2..3 of a 128-bit UUID (Bluetooth Base UUID
         * layout: 0000xxxx-0000-1000-8000-00805F9B34FB). */
        const uint8_t *d = &ctrl[2];
        const uint8_t *s = &ctrl[2 + usz];
        info->dst_uuid16 = (usz == 16) ? get_be16(&d[2]) : get_be16(&d[usz - 2]);
        info->src_uuid16 = (usz == 16) ? get_be16(&s[2]) : get_be16(&s[usz - 2]);
        return BNEP_OK;
    }

    case BNEP_CTRL_SETUP_CONN_RSP:
        if (len < 3) { return BNEP_ERR_TRUNCATED; }
        info->rsp_code = get_be16(&ctrl[1]);
        info->is_success = (info->rsp_code == BNEP_RSP_SUCCESS);
        return BNEP_OK;

    case BNEP_CTRL_FILTER_NET_TYPE_RSP:
    case BNEP_CTRL_FILTER_MULTI_ADDR_RSP:
        if (len < 3) { return BNEP_ERR_TRUNCATED; }
        info->rsp_code = get_be16(&ctrl[1]);
        return BNEP_OK;

    case BNEP_CTRL_FILTER_NET_TYPE_SET:
    case BNEP_CTRL_FILTER_MULTI_ADDR_SET:
        /* We never negotiate filters; the caller answers UNSUPPORTED. */
        return BNEP_OK;

    default:
        return BNEP_ERR_BADTYPE;
    }
}
```

- [ ] **Step 6: 运行测试，确认通过**

```bash
cd /home/aila/projects/vela_contest/docs_ble/tools/bnep_codec_test
make test
```

Expected: `all checks passed`，退出码 0，ASan/UBSan 无告警。

- [ ] **Step 7: 提交**

`frameworks/` 与 `contest2026_181_womenshayebuhuidui/` 是两个独立仓库，分别提交：

```bash
cd /home/aila/projects/vela_contest/frameworks
git add connectivity/bluetooth/service/stacks/zephyr/bnep_codec.h \
        connectivity/bluetooth/service/stacks/zephyr/bnep_codec.c
git commit -m "feat(bnep): add zero-dependency BNEP codec with control frames"

cd /home/aila/projects/vela_contest/contest2026_181_womenshayebuhuidui
git add docs_ble/tools/bnep_codec_test
git commit -m "test(bnep): host unit test harness for the BNEP codec"
```

---

## Task 2: 数据帧编码（含广播规则）

**Files:**
- Modify: `frameworks/connectivity/bluetooth/service/stacks/zephyr/bnep_codec.c`（追加 `bnep_encode_eth`）
- Modify: `docs_ble/tools/bnep_codec_test/test_bnep_codec.c`（追加测试 + `main` 调用）

**Interfaces:**
- Consumes: Task 1 的 `bnep_mac_is_broadcast`、`bnep_mac_is_multicast`、
  `BNEP_GENERAL_ETHERNET`、`BNEP_COMPRESSED_ETHERNET`、
  `BNEP_ERR_NOSPACE`、`BNEP_ERR_TRUNCATED`、`BNEP_ETH_HDR_LEN`
- Produces: `bnep_encode_eth`（签名见 Task 1 的头文件，此处实现）

- [ ] **Step 1: 写失败测试**

在 `test_bnep_codec.c` 中追加，**插在 `main()` 之前**（C 要求先声明后使用；
直接追加到文件末尾会让 `main()` 里的调用变成隐式声明，`-Werror` 下即编译失败）：

```c
/* 本机 MAC 与对端 MAC，供压缩判定使用 */
static const uint8_t LOCAL_MAC[6] = { 0xaa,0xbb,0xcc,0xdd,0xee,0x01 };
static const uint8_t PEER_MAC[6]  = { 0xaa,0xbb,0xcc,0xdd,0xee,0x02 };

static size_t build_eth(uint8_t *buf, const uint8_t dst[6],
                        const uint8_t src[6], uint16_t proto,
                        size_t payload_len)
{
    memcpy(&buf[0], dst, 6);
    memcpy(&buf[6], src, 6);
    buf[12] = (uint8_t)(proto >> 8);
    buf[13] = (uint8_t)(proto & 0xff);
    for (size_t i = 0; i < payload_len; i++) { buf[14 + i] = (uint8_t)i; }
    return 14 + payload_len;
}
```

```c
static void test_encode_general(void)
{
    uint8_t eth[64], out[128];
    size_t eth_len = build_eth(eth, PEER_MAC, LOCAL_MAC, 0x0800, 20);

    /* compress=false -> 一律 General Ethernet：1 + 14 + 20 = 35 */
    int n = bnep_encode_eth(out, sizeof(out), eth, eth_len,
                            LOCAL_MAC, PEER_MAC, false);
    CHECK(n == 35);
    if (n == 35) {
        CHECK(out[0] == BNEP_GENERAL_ETHERNET);
        CHECK_MEM(&out[1], eth, eth_len);   /* 以太头与载荷原样跟随 */
    }
}

/* 这条是 DHCP / ARP 能否工作的核心：广播绝不能压缩 */
static void test_encode_broadcast_never_compressed(void)
{
    uint8_t eth[64], out[128];
    const uint8_t bcast[6] = { 0xff,0xff,0xff,0xff,0xff,0xff };
    size_t eth_len = build_eth(eth, bcast, LOCAL_MAC, 0x0806, 28);

    /* 即使显式要求压缩，广播也必须落到 General Ethernet */
    int n = bnep_encode_eth(out, sizeof(out), eth, eth_len,
                            LOCAL_MAC, PEER_MAC, true);
    CHECK(n == (int)(1 + eth_len));
    if (n > 0) {
        CHECK(out[0] == BNEP_GENERAL_ETHERNET);
        CHECK_MEM(&out[1], bcast, 6);       /* dst 必须真实保留为全 F */
    }
}

static void test_encode_multicast_never_compressed(void)
{
    uint8_t eth[64], out[128];
    const uint8_t mcast[6] = { 0x01,0x00,0x5e,0x00,0x00,0xfb };
    size_t eth_len = build_eth(eth, mcast, LOCAL_MAC, 0x0800, 10);
    int n = bnep_encode_eth(out, sizeof(out), eth, eth_len,
                            LOCAL_MAC, PEER_MAC, true);
    CHECK(n > 0);
    if (n > 0) { CHECK(out[0] == BNEP_GENERAL_ETHERNET); }
}

static void test_encode_compressed(void)
{
    uint8_t eth[64], out[128];
    /* dst == peer, src == local -> 两端都可省 -> Compressed: 1 + 2 + 20 = 23 */
    size_t eth_len = build_eth(eth, PEER_MAC, LOCAL_MAC, 0x0800, 20);
    int n = bnep_encode_eth(out, sizeof(out), eth, eth_len,
                            LOCAL_MAC, PEER_MAC, true);
    CHECK(n == 23);
    if (n == 23) {
        CHECK(out[0] == BNEP_COMPRESSED_ETHERNET);
        CHECK(out[1] == 0x08 && out[2] == 0x00);
    }
}
```

```c
static void test_encode_bounds(void)
{
    uint8_t eth[BNEP_MAX_ETH_FRAME], out[BNEP_MIN_L2CAP_MTU];

    /* 满尺寸 1514 字节以太帧，General 编码 = 1515，必须 <= 1691 */
    size_t eth_len = build_eth(eth, PEER_MAC, LOCAL_MAC, 0x0800, 1500);
    CHECK(eth_len == 1514);
    int n = bnep_encode_eth(out, sizeof(out), eth, eth_len,
                            LOCAL_MAC, PEER_MAC, false);
    CHECK(n == 1515);

    /* 输出容量不足 -> NOSPACE，不得越界（ASan 会抓） */
    CHECK(bnep_encode_eth(out, 10, eth, eth_len,
                          LOCAL_MAC, PEER_MAC, false) == BNEP_ERR_NOSPACE);

    /* 输入短于 14 字节以太头 -> TRUNCATED */
    CHECK(bnep_encode_eth(out, sizeof(out), eth, 13,
                          LOCAL_MAC, PEER_MAC, false) == BNEP_ERR_TRUNCATED);
}
```

并在 `main()` 中追加调用（放在 `test_mac_byte_order();` 之后）：

```c
    test_encode_general();
    test_encode_broadcast_never_compressed();
    test_encode_multicast_never_compressed();
    test_encode_compressed();
    test_encode_bounds();
```

- [ ] **Step 2: 运行测试，确认失败**

```bash
cd /home/aila/projects/vela_contest/docs_ble/tools/bnep_codec_test
make test
```

Expected: 链接失败，`undefined reference to 'bnep_encode_eth'`。

- [ ] **Step 3: 实现 `bnep_encode_eth`**

追加到 `bnep_codec.c`：

```c
int bnep_encode_eth(uint8_t *out, size_t cap,
                    const uint8_t *eth_frame, size_t eth_len,
                    const uint8_t local_mac[6], const uint8_t peer_mac[6],
                    bool compress)
{
    if (!out || !eth_frame || !local_mac || !peer_mac) {
        return BNEP_ERR_TRUNCATED;
    }
    if (eth_len < BNEP_ETH_HDR_LEN) { return BNEP_ERR_TRUNCATED; }

    const uint8_t *dst = &eth_frame[0];
    const uint8_t *src = &eth_frame[6];
    const uint8_t *proto = &eth_frame[12];
    const uint8_t *payload = &eth_frame[BNEP_ETH_HDR_LEN];
    size_t payload_len = eth_len - BNEP_ETH_HDR_LEN;
```

```c
    /* Broadcast and multicast must stay General: a compressed frame makes
     * the receiver rebuild the destination as its own unicast MAC, which
     * silently eats DHCP DISCOVER and every ARP request.
     *
     * The guard has to sit on BOTH omittable flags, not just dst_omittable.
     * A broadcast frame we originate has src == local_mac, so with the guard
     * only on dst the ladder falls through to DEST_ONLY (0x04) and still
     * emits a compressed frame. Confirmed by running the two tests above
     * against a src_omittable that lacked the guard: type came out 0x04
     * with n=37 instead of 0x00 with n=43. */
    bool bcast_or_mcast = bnep_mac_is_broadcast(dst)
                          || bnep_mac_is_multicast(dst);
    bool dst_omittable = compress && !bcast_or_mcast
                         && memcmp(dst, peer_mac, 6) == 0;
    bool src_omittable = compress && !bcast_or_mcast
                         && memcmp(src, local_mac, 6) == 0;

    uint8_t type;
    if (dst_omittable && src_omittable) {
        type = BNEP_COMPRESSED_ETHERNET;
    } else if (dst_omittable) {
        type = BNEP_COMPRESSED_ETHERNET_SRC_ONLY;
    } else if (src_omittable) {
        type = BNEP_COMPRESSED_ETHERNET_DEST_ONLY;
    } else {
        type = BNEP_GENERAL_ETHERNET;
    }

    size_t need = 1;
    if (type == BNEP_GENERAL_ETHERNET) { need += 12; }
    else if (type == BNEP_COMPRESSED_ETHERNET_SRC_ONLY) { need += 6; }
    else if (type == BNEP_COMPRESSED_ETHERNET_DEST_ONLY) { need += 6; }
    need += 2 + payload_len;

    if (cap < need) { return BNEP_ERR_NOSPACE; }

    size_t o = 0;
    out[o++] = type;
    if (type == BNEP_GENERAL_ETHERNET) {
        memcpy(&out[o], dst, 6); o += 6;
        memcpy(&out[o], src, 6); o += 6;
    } else if (type == BNEP_COMPRESSED_ETHERNET_SRC_ONLY) {
        memcpy(&out[o], src, 6); o += 6;
    } else if (type == BNEP_COMPRESSED_ETHERNET_DEST_ONLY) {
        memcpy(&out[o], dst, 6); o += 6;
    }
    out[o++] = proto[0];
    out[o++] = proto[1];
    memcpy(&out[o], payload, payload_len);
    o += payload_len;

    return (int)o;
}
```

- [ ] **Step 4: 运行测试，确认通过**

```bash
cd /home/aila/projects/vela_contest/docs_ble/tools/bnep_codec_test
make test
```

Expected: `all checks passed`。

- [ ] **Step 5: 提交**

```bash
cd /home/aila/projects/vela_contest/frameworks
git add connectivity/bluetooth/service/stacks/zephyr/bnep_codec.c
git commit -m "feat(bnep): encode Ethernet frames, never compressing broadcast"

cd /home/aila/projects/vela_contest/contest2026_181_womenshayebuhuidui
git add docs_ble/tools/bnep_codec_test/test_bnep_codec.c
git commit -m "test(bnep): cover encode paths incl. broadcast and bounds"
```

---

## Task 3: 数据帧解码（4 种类型 + 扩展头）

**Files:**
- Modify: `frameworks/connectivity/bluetooth/service/stacks/zephyr/bnep_codec.c`（追加 `bnep_decode_eth`）
- Modify: `docs_ble/tools/bnep_codec_test/test_bnep_codec.c`

**Interfaces:**
- Consumes: Task 1 的常量与 `BNEP_DECODE_IS_CONTROL`；Task 2 的 `bnep_encode_eth`（往返测试要用）
- Produces: `bnep_decode_eth`（签名见 Task 1 头文件）

- [ ] **Step 1: 写失败测试**

追加到 `test_bnep_codec.c`，同样**插在 `main()` 之前**：

```c
static void test_decode_roundtrip_all_types(void)
{
    uint8_t eth[128], bnep[256], back[256];
    const uint8_t *ctrl; size_t ctrl_len;

    /* General：dst 是第三方地址，src 虽然等于 local_mac 但这里传 compress=false，
     * 强制走不压缩路径。注意不能传 true——那样 src 是合法可省的，编码器会
     * 正确地发 DEST_ONLY(0x04)，而接收端重建 src 时只能填 peer_mac，
     * 于是 roundtrip 比较必然不等（实测 back[11] 得到 0x02 而非 0x01）。
     * 这不是缺陷：线路上省掉的 src 语义就是「发送方自己」，只是本测试同时
     * 固定了 local/peer 两个身份，无法表达它。 */
    const uint8_t other[6] = { 0x02,0x03,0x04,0x05,0x06,0x07 };
    size_t eth_len = build_eth(eth, other, LOCAL_MAC, 0x0800, 32);
    int n = bnep_encode_eth(bnep, sizeof(bnep), eth, eth_len,
                            LOCAL_MAC, PEER_MAC, false);
    CHECK(bnep[0] == BNEP_GENERAL_ETHERNET);
    int m = bnep_decode_eth(back, sizeof(back), bnep, (size_t)n,
                            LOCAL_MAC, PEER_MAC, &ctrl, &ctrl_len);
    CHECK(m == (int)eth_len);
    if (m == (int)eth_len) { CHECK_MEM(back, eth, eth_len); }

    /* Compressed：解码方向是「手机发给我们」，所以 dst=local, src=peer */
    eth_len = build_eth(eth, LOCAL_MAC, PEER_MAC, 0x0800, 32);
    bnep[0] = BNEP_COMPRESSED_ETHERNET;
    bnep[1] = 0x08; bnep[2] = 0x00;
    memcpy(&bnep[3], &eth[14], 32);
    m = bnep_decode_eth(back, sizeof(back), bnep, 3 + 32,
                        LOCAL_MAC, PEER_MAC, &ctrl, &ctrl_len);
    CHECK(m == (int)eth_len);
    if (m == (int)eth_len) { CHECK_MEM(back, eth, eth_len); }

    /* SrcOnly：帧内带 src，dst 补 local */
    bnep[0] = BNEP_COMPRESSED_ETHERNET_SRC_ONLY;
    memcpy(&bnep[1], PEER_MAC, 6);
    bnep[7] = 0x08; bnep[8] = 0x00;
    memcpy(&bnep[9], &eth[14], 32);
    m = bnep_decode_eth(back, sizeof(back), bnep, 9 + 32,
                        LOCAL_MAC, PEER_MAC, &ctrl, &ctrl_len);
    CHECK(m == (int)eth_len);
    if (m == (int)eth_len) { CHECK_MEM(back, eth, eth_len); }
}
```

```c
static void test_decode_dest_only_and_broadcast(void)
{
    uint8_t bnep[128], back[256];
    const uint8_t *ctrl; size_t ctrl_len;
    const uint8_t bcast[6] = { 0xff,0xff,0xff,0xff,0xff,0xff };

    /* DestOnly 携带广播 dst，src 补 peer */
    bnep[0] = BNEP_COMPRESSED_ETHERNET_DEST_ONLY;
    memcpy(&bnep[1], bcast, 6);
    bnep[7] = 0x08; bnep[8] = 0x06;
    for (int i = 0; i < 28; i++) { bnep[9 + i] = (uint8_t)i; }

    int m = bnep_decode_eth(back, sizeof(back), bnep, 9 + 28,
                            LOCAL_MAC, PEER_MAC, &ctrl, &ctrl_len);
    CHECK(m == 14 + 28);
    if (m == 14 + 28) {
        CHECK_MEM(&back[0], bcast, 6);
        CHECK_MEM(&back[6], PEER_MAC, 6);
        CHECK(back[12] == 0x08 && back[13] == 0x06);
    }
}

static void test_decode_extension_headers(void)
{
    uint8_t bnep[128], back[256];
    const uint8_t *ctrl; size_t ctrl_len;
    size_t o = 0;

    /* Compressed + 3 个扩展头，最后一个的 more 位清零 */
    bnep[o++] = BNEP_COMPRESSED_ETHERNET | BNEP_EXT_FLAG;
    bnep[o++] = 0x08; bnep[o++] = 0x00;
    bnep[o++] = 0x80; bnep[o++] = 2; bnep[o++] = 0xa1; bnep[o++] = 0xa2;
    bnep[o++] = 0x80; bnep[o++] = 1; bnep[o++] = 0xb1;
    bnep[o++] = 0x00; bnep[o++] = 3; bnep[o++] = 0xc1; bnep[o++] = 0xc2;
    bnep[o++] = 0xc3;
    size_t hdr_end = o;
    for (int i = 0; i < 16; i++) { bnep[o++] = (uint8_t)(0x40 + i); }

    int m = bnep_decode_eth(back, sizeof(back), bnep, o,
                            LOCAL_MAC, PEER_MAC, &ctrl, &ctrl_len);
    CHECK(m == 14 + 16);
    if (m == 14 + 16) {
        CHECK_MEM(&back[0], LOCAL_MAC, 6);
        CHECK_MEM(&back[6], PEER_MAC, 6);
        CHECK_MEM(&back[14], &bnep[hdr_end], 16);
    }

    /* 扩展头长度撒谎，越过帧尾 -> TRUNCATED */
    bnep[4] = 200;
    CHECK(bnep_decode_eth(back, sizeof(back), bnep, o,
                          LOCAL_MAC, PEER_MAC, &ctrl, &ctrl_len)
          == BNEP_ERR_TRUNCATED);
}
```

```c
static void test_decode_control_and_bounds(void)
{
    uint8_t back[256];
    const uint8_t *ctrl = NULL; size_t ctrl_len = 0;

    /* 控制帧：返回 IS_CONTROL 并指向 message type 字节 */
    const uint8_t rsp[] = { 0x01, 0x02, 0x00, 0x00 };
    CHECK(bnep_decode_eth(back, sizeof(back), rsp, sizeof(rsp),
                          LOCAL_MAC, PEER_MAC, &ctrl, &ctrl_len)
          == BNEP_DECODE_IS_CONTROL);
    CHECK(ctrl == &rsp[1]);
    CHECK(ctrl_len == 3);

    /* 带扩展头的控制帧同样要能识别 */
    const uint8_t rsp_ext[] = { 0x01 | BNEP_EXT_FLAG, 0x02, 0x00, 0x00 };
    CHECK(bnep_decode_eth(back, sizeof(back), rsp_ext, sizeof(rsp_ext),
                          LOCAL_MAC, PEER_MAC, &ctrl, &ctrl_len)
          == BNEP_DECODE_IS_CONTROL);

    /* 未知帧类型 */
    const uint8_t bad[] = { 0x7f, 0x00 };
    CHECK(bnep_decode_eth(back, sizeof(back), bad, sizeof(bad),
                          LOCAL_MAC, PEER_MAC, &ctrl, &ctrl_len)
          == BNEP_ERR_BADTYPE);

    /* 空帧 */
    CHECK(bnep_decode_eth(back, sizeof(back), bad, 0,
                          LOCAL_MAC, PEER_MAC, &ctrl, &ctrl_len)
          == BNEP_ERR_TRUNCATED);

    /* General 帧声明了 14 字节头却只给了 8 字节 */
    const uint8_t shortg[] = { 0x00, 1,2,3,4,5,6,7 };
    CHECK(bnep_decode_eth(back, sizeof(back), shortg, sizeof(shortg),
                          LOCAL_MAC, PEER_MAC, &ctrl, &ctrl_len)
          == BNEP_ERR_TRUNCATED);

    /* 输出缓冲不足 */
    uint8_t small[10];
    const uint8_t comp[] = { 0x02, 0x08, 0x00, 1,2,3,4,5,6,7,8 };
    CHECK(bnep_decode_eth(small, sizeof(small), comp, sizeof(comp),
                          LOCAL_MAC, PEER_MAC, &ctrl, &ctrl_len)
          == BNEP_ERR_NOSPACE);
}
```

在 `main()` 中追加：

```c
    test_decode_roundtrip_all_types();
    test_decode_dest_only_and_broadcast();
    test_decode_extension_headers();
    test_decode_control_and_bounds();
```

- [ ] **Step 2: 运行测试，确认失败**

```bash
cd /home/aila/projects/vela_contest/docs_ble/tools/bnep_codec_test && make test
```

Expected: 链接失败，`undefined reference to 'bnep_decode_eth'`。

- [ ] **Step 3: 实现 `bnep_decode_eth`**

追加到 `bnep_codec.c`：

```c
int bnep_decode_eth(uint8_t *out, size_t cap,
                    const uint8_t *bnep, size_t bnep_len,
                    const uint8_t local_mac[6], const uint8_t peer_mac[6],
                    const uint8_t **ctrl_out, size_t *ctrl_len)
{
    if (!out || !bnep || !local_mac || !peer_mac || !ctrl_out || !ctrl_len) {
        return BNEP_ERR_TRUNCATED;
    }
    if (bnep_len < 1) { return BNEP_ERR_TRUNCATED; }

    uint8_t type = bnep[0] & BNEP_TYPE_MASK;
    bool has_ext = (bnep[0] & BNEP_EXT_FLAG) != 0;
    size_t i = 1;

    const uint8_t *dst = NULL;
    const uint8_t *src = NULL;

    switch (type) {
    case BNEP_CONTROL:
        /* Control payload starts right after the header byte and runs to
         * the end of the frame; extension headers, if any, follow the
         * control message and are not our concern here. */
        *ctrl_out = &bnep[1];
        *ctrl_len = bnep_len - 1;
        return BNEP_DECODE_IS_CONTROL;

    case BNEP_GENERAL_ETHERNET:
        if (bnep_len < i + 12) { return BNEP_ERR_TRUNCATED; }
        dst = &bnep[i]; i += 6;
        src = &bnep[i]; i += 6;
        break;

    case BNEP_COMPRESSED_ETHERNET:
        dst = local_mac;
        src = peer_mac;
        break;

    case BNEP_COMPRESSED_ETHERNET_SRC_ONLY:
        if (bnep_len < i + 6) { return BNEP_ERR_TRUNCATED; }
        dst = local_mac;
        src = &bnep[i]; i += 6;
        break;

    case BNEP_COMPRESSED_ETHERNET_DEST_ONLY:
        if (bnep_len < i + 6) { return BNEP_ERR_TRUNCATED; }
        dst = &bnep[i]; i += 6;
        src = peer_mac;
        break;

    default:
        return BNEP_ERR_BADTYPE;
    }
```

```c
    /* Protocol type. */
    if (bnep_len < i + 2) { return BNEP_ERR_TRUNCATED; }
    uint8_t proto_hi = bnep[i];
    uint8_t proto_lo = bnep[i + 1];
    i += 2;

    /* Skip every extension header. Each is [more|type][len][payload].
     * Forgetting this loop hands extension bytes to the IP stack as if
     * they were payload. */
    while (has_ext) {
        if (bnep_len < i + 2) { return BNEP_ERR_TRUNCATED; }
        bool more = (bnep[i] & BNEP_EXT_FLAG) != 0;
        uint8_t ext_len = bnep[i + 1];
        i += 2;
        if (bnep_len < i + ext_len) { return BNEP_ERR_TRUNCATED; }
        i += ext_len;
        has_ext = more;
    }

    size_t payload_len = bnep_len - i;
    if (cap < BNEP_ETH_HDR_LEN + payload_len) { return BNEP_ERR_NOSPACE; }

    memcpy(&out[0], dst, 6);
    memcpy(&out[6], src, 6);
    out[12] = proto_hi;
    out[13] = proto_lo;
    if (payload_len) { memcpy(&out[BNEP_ETH_HDR_LEN], &bnep[i], payload_len); }

    return (int)(BNEP_ETH_HDR_LEN + payload_len);
}
```

> 注意 `memcpy(&out[0], dst, 6)` 在 General Ethernet 情形下 `dst` 指向
> `bnep` 内部——`out` 与 `bnep` **必须是两块不重叠的缓冲**。SAL 层不得做
> 原地解码。Task 6 的静态重组缓冲满足这一约束。

- [ ] **Step 4: 运行测试，确认通过**

```bash
cd /home/aila/projects/vela_contest/docs_ble/tools/bnep_codec_test && make test
```

Expected: `all checks passed`，ASan/UBSan 无告警。

- [ ] **Step 5: 提交**

```bash
cd /home/aila/projects/vela_contest/frameworks
git add connectivity/bluetooth/service/stacks/zephyr/bnep_codec.c
git commit -m "feat(bnep): decode all four data frame types and skip extensions"

cd /home/aila/projects/vela_contest/contest2026_181_womenshayebuhuidui
git add docs_ble/tools/bnep_codec_test/test_bnep_codec.c
git commit -m "test(bnep): cover decode paths, extension headers, malformed input"
```

---

## Task 4: Gate B 工具链（HCI trace 全长化 + pcap 生成 + tshark 判定）

**Files:**
- Modify: `vendor/sifli/chips/sf32lb52/sf32lb52_bth4.c`（新增 ACL 全长 trace）
- Create: `docs_ble/tools/setup_tshark.sh`
- Create: `docs_ble/tools/bnep_pcap.py`
- Create: `docs_ble/tools/gate_b.sh`

**Interfaces:**
- Consumes: 无代码依赖（可与 Task 1–3 并行）
- Produces: 日志行格式契约，`bnep_pcap.py` 与 bth4 双方都依赖它：
  ```
  sf32lb52 bth4 acl: dir=<tx|rx> seq=<uint> off=<uint> total=<uint> <hex bytes>
  ```
  一个 ACL 包按 32 字节切成多行，`seq` 在同方向内单调递增，`off` 是本行第一个
  字节在包内的偏移，`total` 是整包长度（含 H4 类型字节）。

**为什么只全长 trace ACL：** Gate B 需要的 BNEP 帧和 L2CAP 建链信令都走 ACL
（`0x02`）。HCI 命令/事件保持现有 40 字节上限即可。这把日志量压到可用范围，
也避开了在 `CONFIG_BT_RX_STACK_SIZE=1200` 的 RX 路径上开大栈缓冲。

- [ ] **Step 1: 装用户态 tshark（无 root）**

创建 `docs_ble/tools/setup_tshark.sh`：

```bash
#!/usr/bin/env bash
# Install tshark into a private prefix without root. The dev box has no
# tshark package installed, sudo needs a password, and github is
# unreachable -- but apt-get download and the Ubuntu mirror both work.
set -Eeuo pipefail

PREFIX="${TSHARK_PREFIX:-$HOME/.local/tshark}"
PKGS=(tshark wireshark-common libwireshark15 libwireshark-data
      libwiretap12 libwsutil13 libsmi2ldbl liblua5.2-0 libspandsp2
      libssh-gcrypt-4 libc-ares2 libsnappy1v5 libnl-route-3-200
      libmaxminddb0 libbrotli1 libgcrypt20 libgnutls30)

mkdir -p "$PREFIX/debs" "$PREFIX/root"
cd "$PREFIX/debs"
apt-get download "${PKGS[@]}"
cd "$PREFIX"
for d in debs/*.deb; do dpkg -x "$d" root/; done

cat > "$PREFIX/tshark" <<EOF
#!/usr/bin/env bash
export LD_LIBRARY_PATH="$PREFIX/root/usr/lib/x86_64-linux-gnu"
exec "$PREFIX/root/usr/bin/tshark" "\$@"
EOF
chmod +x "$PREFIX/tshark"

"$PREFIX/tshark" --version | head -1
"$PREFIX/tshark" -G protocols | grep -q btbnep \
  && echo "btbnep dissector: OK" || { echo "btbnep MISSING"; exit 1; }
```

Run: `bash docs_ble/tools/setup_tshark.sh`
Expected: `TShark (Wireshark) 3.6.2 ...` 后跟 `btbnep dissector: OK`。

- [ ] **Step 2: 在 bth4 里加 ACL 全长 trace（分块，栈安全）**

在 `vendor/sifli/chips/sf32lb52/sf32lb52_bth4.c` 顶部的宏区加一个开关，紧跟在
现有 `SF32LB52_BT_TRACE` 定义之后：

```c
/* Full-length ACL tracing for Gate B (docs_ble/tools/gate_b.sh). Emitted in
 * 32-byte chunks so the hex buffer stays off the 1200-byte BT RX stack.
 * Debug-only: the product defconfig leaves this off. */
#ifdef CONFIG_SF32LB52_BT_TRACE_ACL_FULL
#  define SF32LB52_BT_TRACE_ACL_FULL 1
#else
#  define SF32LB52_BT_TRACE_ACL_FULL 0
#endif
```

并在 `vendor/sifli/chips/sf32lb52/Kconfig` 里声明这个开关。插入位置是
`config BSP_USING_LCPU_PATCH`（`:1355-1359`）之后、`menuconfig BSP_USING_PDM`
（`:1361`）之前，缩进跟随该文件现有的 4 空格风格：

```
    config SF32LB52_BT_TRACE_ACL_FULL
        bool "Trace full-length HCI ACL packets"
        default n
        help
            Dump every HCI ACL packet in full, 32 bytes per syslog line, so
            that docs_ble/tools/bnep_pcap.py can rebuild a pcap for
            Wireshark's BNEP dissector. Very verbose - debug builds only.
```

（该文件全篇是纯 `#define` 风格、没有既存的 BT 相关 Kconfig 项，
所以这是第一个；`SF32LB52_BT_TRACE`（`:39`）保持硬编码 1 不动。）

在同文件加一个静态辅助函数（放在第一个使用它的函数之前）：

```c
#if SF32LB52_BT_TRACE_ACL_FULL
static void sf32lb52_bt_trace_acl(const char *dir, const uint8_t *pkt,
                                  size_t total)
{
  static unsigned int seq_tx;
  static unsigned int seq_rx;
  unsigned int seq;
  char hex[32 * 3 + 1];

  if (total < 1 || pkt[0] != H4_ACL)
    {
      return;
    }

  seq = (dir[0] == 't') ? seq_tx++ : seq_rx++;

  for (size_t off = 0; off < total; off += 32)
    {
      size_t n = (total - off > 32) ? 32 : (total - off);
      int pos = 0;

      for (size_t i = 0; i < n; i++)
        {
          pos += snprintf(&hex[pos], sizeof(hex) - pos, "%02x ",
                          pkt[off + i]);
        }

      syslog(LOG_INFO,
             "sf32lb52 bth4 acl: dir=%s seq=%u off=%u total=%u %s\n",
             dir, seq, (unsigned int)off, (unsigned int)total, hex);
    }
}
#endif
```

若该文件中没有 `H4_ACL` 宏，用 `0x02` 字面量，并在同处补一行
`#define H4_ACL 0x02`（文件已有 `H4_EVT`，见 `sf32lb52_bth4.c:1004`，
按同样风格补齐）。

- [ ] **Step 3: 挂上两个调用点**

**RX**——在 `sf32lb52_bth4.c` 的接收循环里，`packet_len` 已确定且整包已到齐
之后（即 `:975` 的 `if ((size_t)packet_len > priv->rxlen) { break; }` 之后、
任何 `memmove` 消费之前）插入：

```c
#if SF32LB52_BT_TRACE_ACL_FULL
      sf32lb52_bt_trace_acl("rx", priv->rxbuf, (size_t)packet_len);
#endif
```

这是唯一正确的位置：此处 `priv->rxbuf[0..packet_len)` 恰好是一个完整 H4 包。
现有 `:930-945` 的 trace 打的是「本次串口 chunk 的长度」配「缓冲区开头的
内容」，缓冲里有 pending 数据时两者错位——那段保留不动（它对调试 HCI
事件仍有用），Gate B 只用新的 `acl:` 行。

**TX**——在 `sf32lb52_bt_send` 里现有 tx trace 块（`:1274-1287`）之后、
`sf32lb52_host_send_packet` 调用之前插入：

```c
#if SF32LB52_BT_TRACE_ACL_FULL
  sf32lb52_bt_trace_acl("tx", hdr, len + drv->head_reserve);
#endif
```

启用方式：只在 Task 8 生成的调试 board config（`ai_agent_pandbg`）里
`CONFIG_SF32LB52_BT_TRACE_ACL_FULL=y`。**产品 defconfig 必须不含这一行**
——Task 8 Step 5 的检查项之一。

- [ ] **Step 4: 写 pcap 生成器**

创建 `docs_ble/tools/bnep_pcap.py`：

```python
#!/usr/bin/env python3
"""Turn sf32lb52 bth4 ACL traces into a pcap that Wireshark can dissect.

Reads a serial log on stdin (or a file argument), reassembles the chunked
'sf32lb52 bth4 acl:' lines into whole H4 packets, and writes a pcap with
linktype 201 (LINKTYPE_BLUETOOTH_HCI_H4_WITH_PHDR): each record is a
4-byte big-endian direction word (0 = sent, 1 = received) followed by the
raw H4 packet.

Wireshark needs the L2CAP Connection Request/Response on CID 0x0001 to
bind the BNEP dissector to the data channel, so capture from before the
PAN connection starts.
"""
import re
import struct
import sys

LINE = re.compile(
    r"sf32lb52 bth4 acl: dir=(tx|rx) seq=(\d+) off=(\d+) total=(\d+) ([0-9a-fA-F ]+)"
)
```

```python
def reassemble(lines):
    """Yield (direction, packet_bytes) in log order."""
    pending = {}   # (dir, seq) -> bytearray
    order = []     # (dir, seq) first-seen order
    totals = {}

    for line in lines:
        m = LINE.search(line)
        if not m:
            continue
        d, seq, off, total, hexs = m.groups()
        key = (d, int(seq))
        off, total = int(off), int(total)
        data = bytes.fromhex(hexs.replace(" ", ""))

        if key not in pending:
            pending[key] = bytearray(total)
            totals[key] = total
            order.append(key)
        buf = pending[key]
        if off + len(data) <= len(buf):
            buf[off:off + len(data)] = data

    for key in order:
        d, _ = key
        yield (0 if d == "tx" else 1), bytes(pending[key])


def write_pcap(path, packets):
    with open(path, "wb") as f:
        # magic, ver 2.4, tz 0, sigfigs 0, snaplen, linktype 201
        f.write(struct.pack("<IHHiIII",
                            0xa1b2c3d4, 2, 4, 0, 0, 262144, 201))
        for i, (direction, pkt) in enumerate(packets):
            payload = struct.pack(">I", direction) + pkt
            f.write(struct.pack("<IIII", i, 0, len(payload), len(payload)))
            f.write(payload)


def main():
    src = open(sys.argv[1]) if len(sys.argv) > 1 else sys.stdin
    out = sys.argv[2] if len(sys.argv) > 2 else "bnep.pcap"
    pkts = list(reassemble(src))
    write_pcap(out, pkts)
    print(f"wrote {out}: {len(pkts)} ACL packets")


if __name__ == "__main__":
    main()
```

- [ ] **Step 5: 用自造样本验证工具链（不需要板子）**

创建 `docs_ble/tools/gate_b.sh`：

```bash
#!/usr/bin/env bash
# Gate B: serial log -> pcap -> tshark BNEP dissection -> verdict.
set -Eeuo pipefail

LOG="${1:?usage: gate_b.sh <serial.log> [out.pcap]}"
PCAP="${2:-${LOG%.log}.pcap}"
TSHARK="${TSHARK:-$HOME/.local/tshark/tshark}"

[[ -x "$TSHARK" ]] || { echo "tshark not found; run setup_tshark.sh"; exit 1; }

python3 "$(dirname "$0")/bnep_pcap.py" "$LOG" "$PCAP"

echo "--- dissection ---"
"$TSHARK" -r "$PCAP"

echo "--- verdict ---"
malformed=$("$TSHARK" -r "$PCAP" -Y '_ws.malformed' 2>/dev/null | wc -l)
bnep=$("$TSHARK" -r "$PCAP" -Y 'btbnep' 2>/dev/null | wc -l)
setup_ok=$("$TSHARK" -r "$PCAP" -Y 'btbnep.control_type == 0x02' 2>/dev/null | wc -l)
dhcp=$("$TSHARK" -r "$PCAP" -Y 'dhcp || bootp' 2>/dev/null | wc -l)

echo "BNEP frames      : $bnep"
echo "Setup responses  : $setup_ok"
echo "DHCP frames      : $dhcp"
echo "Malformed frames : $malformed"

if [[ "$malformed" -ne 0 ]]; then echo "GATE B: FAIL (malformed)"; exit 1; fi
if [[ "$bnep"      -eq 0 ]]; then echo "GATE B: FAIL (no BNEP)";   exit 1; fi
echo "GATE B: PASS"
```

先用一个**人造日志**验证脚本本身（这一步已在设计阶段实测跑通，见 spec §6.2.1）：

```bash
cd /home/aila/projects/vela_contest/docs_ble/tools
python3 - > /tmp/fake_bth4.log <<'PY'
def emit(d, seq, pkt):
    for off in range(0, len(pkt), 32):
        chunk = pkt[off:off + 32]
        hexs = " ".join(f"{b:02x}" for b in chunk)
        print(f"sf32lb52 bth4 acl: dir={d} seq={seq} off={off} "
              f"total={len(pkt)} {hexs} ")

def acl(cid, payload):
    l2 = (len(payload).to_bytes(2, 'little')
          + cid.to_bytes(2, 'little') + payload)
    return (b'\x02' + (1).to_bytes(2, 'little')
            + len(l2).to_bytes(2, 'little') + l2)

# L2CAP Connect Request PSM 0x000F, SCID 0x0040
emit("tx", 0, acl(0x0001, bytes.fromhex('02010400') + bytes.fromhex('0f004000')))
# L2CAP Connect Response: DCID 0x0041, SCID 0x0040, result/status success
emit("rx", 0, acl(0x0001, bytes.fromhex('03010800')
                  + bytes.fromhex('41004000') + bytes.fromhex('00000000')))
# BNEP Setup Connection Request / Response
emit("tx", 1, acl(0x0041, bytes.fromhex('01010211161115')))
emit("rx", 1, acl(0x0040, bytes.fromhex('01020000')))
PY
bash gate_b.sh /tmp/fake_bth4.log /tmp/fake_bth4.pcap
```

Expected: tshark 输出中出现
`Sent Connection Request (BNEP, SCID: 0x0040)`、
`Setup Connection Request - dst: <PAN NAP>, src: <PAN PANU>`、
`Setup Connection Response - Operation Successful`，最后 `GATE B: PASS`。

- [ ] **Step 6: 提交**

```bash
cd /home/aila/projects/vela_contest/vendor
git add sifli/chips/sf32lb52/sf32lb52_bth4.c
git commit -m "feat(bth4): full-length chunked ACL tracing behind a debug switch"

cd /home/aila/projects/vela_contest/contest2026_181_womenshayebuhuidui
git add docs_ble/tools/setup_tshark.sh docs_ble/tools/bnep_pcap.py docs_ble/tools/gate_b.sh
git commit -m "test(pan): Gate B toolchain -- HCI trace to pcap to tshark verdict"
```

> 若 `vendor/` 不是独立 git 仓库，则该文件随 `frameworks/` 之外的顶层改动一起，
> 按仓库实际归属提交；执行前先跑 `git -C vendor rev-parse --show-toplevel` 确认。

## Task 5: SAL 控制路径改走 codec（修 D2/D3/D4/D5/D6）

**Files:**
- Modify: `frameworks/connectivity/bluetooth/service/stacks/include/sal_pan_interface.h`（整文件 25-51 行的常量区）
- Modify: `frameworks/connectivity/bluetooth/service/stacks/zephyr/sal_pan_interface.c:209-266`（Setup Request 发送）
- Modify: `frameworks/connectivity/bluetooth/service/stacks/zephyr/sal_pan_interface.c:281-402`（`pan_chan_recv` 整个函数）
- Modify: `frameworks/connectivity/bluetooth/CMakeLists.txt:314`
- Test: `docs_ble/tools/bnep_codec_test/`（已有，本 Task 只需保持全绿）+ 固件编译

**Interfaces:**
- Consumes（T1 产出）：`bnep_encode_setup_req(uint8_t*, size_t, uint16_t, uint16_t) -> int`、`bnep_encode_setup_rsp(uint8_t*, size_t, uint16_t) -> int`、`bnep_encode_cmd_not_understood(uint8_t*, size_t, uint8_t) -> int`、`bnep_parse_control(const uint8_t*, size_t, struct bnep_control*) -> int`、`struct bnep_control`、`BNEP_UUID16_PANU/NAP`、`BNEP_RSP_*`
- Produces（T6/T7 依赖）：`pan_conn_t` 增加字段 `uint64_t setup_sent_ms;`；新增静态函数 `static void pan_handle_control(pan_conn_t*, const uint8_t*, size_t);`

- [ ] **Step 1: 删掉头文件里错误的常量，改为包含 codec**

`sal_pan_interface.h` 现在 25-51 行那一整段常量全部有问题：响应码从 `0x0003`
起就是错的（`BNEP_CONN_RESP_FAIL_CONN_NOT_ALLOWED 0x0003` 实际是
*Invalid Service UUID Size*），而且缺 `0x02/0x03/0x04` 三个数据帧类型，
还有 `BNEP_SETUP_CONN_RESP`(0x02) 与压缩帧类型值冲突（D5）。
把 25-51 行整段替换为：

```c
/* BNEP PSM (Bluetooth Core Spec Vol 3 Part E) */
#define BT_BNEP_PSM 0x000F

/* All BNEP frame/control/response constants live in bnep_codec.h, which is
 * zero-dependency so the same header compiles in the host unit test.
 * Do not re-declare them here: the previous local copies had the response
 * codes wrong from 0x0003 up and gave 0x02 two conflicting meanings. */
#include "bnep_codec.h"
```

- [ ] **Step 2: 编译，确认所有旧常量的使用点都暴露出来**

Run: `./build.sh contest_board:ai_agent -j8 2>&1 | grep -E 'error|BNEP_'`
Expected: 报 `BNEP_FRAME_ETH`/`BNEP_FRAME_CONTROL`/`BNEP_SETUP_CONN_REQ`/
`BNEP_SETUP_CONN_RESP`/`BNEP_UUID_NAP`/`BNEP_CONN_RESP_SUCCESS` 未声明。
这一步的目的就是让编译器把所有需要改的地方列出来，逐个替换成 codec 的名字
（`BNEP_GENERAL_ETHERNET`/`BNEP_CONTROL`/`BNEP_CTRL_SETUP_CONN_REQ`/
`BNEP_CTRL_SETUP_CONN_RSP`/`BNEP_UUID16_NAP`/`BNEP_RSP_SUCCESS`）。

- [ ] **Step 3: 用 codec 重写 Setup Request 发送**

把 `sal_pan_interface.c:209-266` 的 `pan_try_send_setup()` 整个函数体替换为：

```c
static void pan_try_send_setup(pan_conn_t* conn)
{
    uint8_t req[16];
    struct net_buf* buf;
    int n, ret;

    if (!conn || conn->state != PAN_CONN_L2CAP_PENDING) {
        syslog(LOG_WARNING, "[pan] send_setup: unexpected state=%d\n",
            conn ? (int)conn->state : -1);
        return;
    }

    /* R85: both directions must have finished L2CAP config. Our outbound
     * channel reaches BT_L2CAP_CONNECTED when the phone's CONFIG_REQ
     * arrives; sending BNEP earlier makes the phone drop the link. */
    if (conn->chan.state != BT_L2CAP_CONNECTED) {
        syslog(LOG_INFO, "[pan] send_setup: deferred (L2CAP state=%u)\n",
            conn->chan.state);
        return;
    }

    /* 7 bytes: 01 01 02 11 16 11 15. The UUID Size field is ONE byte
     * (BT Core Vol 3 Part B 3.2.2.1). The old code wrote it as two bytes
     * and the phone answered 0x0003 = Invalid Service UUID Size, which
     * earlier rounds misread as "needs a 128-bit UUID". */
    n = bnep_encode_setup_req(req, sizeof(req),
        BNEP_UUID16_NAP, BNEP_UUID16_PANU);
    if (n <= 0) {
        syslog(LOG_ERR, "[pan] send_setup: encode failed %d\n", n);
        return;
    }

    buf = bt_l2cap_create_pdu_timeout(NULL, 0, K_NO_WAIT);
    if (!buf) {
        syslog(LOG_ERR, "[pan] send_setup: tx pool exhausted\n");
        return;
    }
    net_buf_add_mem(buf, req, (size_t)n);
    ret = bt_l2cap_chan_send(&conn->chan.chan, buf);
    if (ret < 0) {
        syslog(LOG_ERR, "[pan] send_setup: send failed %d\n", ret);
        net_buf_unref(buf);
        return;
    }
    conn->state = PAN_CONN_BNEP_PENDING;
    conn->setup_sent_ms = pan_now_ms();
    syslog(LOG_INFO, "[pan] setup req sent (%d bytes, dst=NAP src=PANU)\n", n);
}
```

并在 `pan_conn_t`（`:89-95`）里加一个字段：

```c
    uint64_t setup_sent_ms;        /* for the 5s handshake timeout */
```

- [ ] **Step 4: 用 codec 重写控制帧处理**

在 `pan_chan_recv` 之前插入这个新函数。它取代旧代码里 `:297-395` 那个
`switch (data[0])`——旧 switch 把帧类型和控制类型混在一个 switch 里，
所以 `case BNEP_SETUP_CONN_RESP:`(0x02) 和压缩帧 0x02 撞了（D5）。

```c
static void pan_send_ctrl(pan_conn_t* conn, const uint8_t* frame, size_t len)
{
    struct net_buf* buf = bt_l2cap_create_pdu_timeout(NULL, 0, K_NO_WAIT);

    if (!buf) {
        syslog(LOG_ERR, "[pan] ctrl tx pool exhausted\n");
        return;
    }
    net_buf_add_mem(buf, frame, len);
    if (bt_l2cap_chan_send(&conn->chan.chan, buf) < 0) {
        net_buf_unref(buf);
    }
}

static void pan_handle_control(pan_conn_t* conn, const uint8_t* ctrl,
    size_t ctrl_len)
{
    struct bnep_control info;
    uint8_t out[16];
    int n;

    if (bnep_parse_control(ctrl, ctrl_len, &info) != BNEP_OK) {
        syslog(LOG_WARNING, "[pan] malformed control frame (%u bytes)\n",
            (unsigned)ctrl_len);
        return;
    }

    switch (info.msg_type) {
    case BNEP_CTRL_SETUP_CONN_RSP:
        /* Strict: only 0x0000 is success. The old code treated every
         * non-zero status as success "anyway", which turned a rejected
         * handshake into a half-open link that silently ate all data. */
        if (!info.is_success) {
            syslog(LOG_ERR, "[pan] setup rejected, rsp=0x%04x\n",
                info.rsp_code);
            pan_conn_report(conn, PROFILE_STATE_DISCONNECTED);
            bt_l2cap_chan_disconnect(&conn->chan.chan);
            return;
        }
        if (conn->state != PAN_CONN_BNEP_PENDING) {
            syslog(LOG_WARNING, "[pan] setup rsp in state %d, ignored\n",
                (int)conn->state);
            return;
        }
        conn->state = PAN_CONN_CONNECTED;
        syslog(LOG_INFO, "[pan] BNEP setup OK, tx_mtu=%u\n",
            conn->chan.tx.mtu);
        pan_conn_report(conn, PROFILE_STATE_CONNECTED);
        return;

    case BNEP_CTRL_SETUP_CONN_REQ:
        /* Phone-initiated setup (it dialed our PSM). Answer with the
         * 4-byte standard response; anything else and Android tears the
         * channel down. */
        syslog(LOG_INFO, "[pan] setup req from peer dst=0x%04x src=0x%04x\n",
            info.dst_uuid16, info.src_uuid16);
        if (info.dst_uuid16 != BNEP_UUID16_PANU) {
            n = bnep_encode_setup_rsp(out, sizeof(out),
                BNEP_RSP_INVALID_DST_UUID);
            if (n > 0) { pan_send_ctrl(conn, out, (size_t)n); }
            return;
        }
        n = bnep_encode_setup_rsp(out, sizeof(out), BNEP_RSP_SUCCESS);
        if (n > 0) { pan_send_ctrl(conn, out, (size_t)n); }
        if (conn->state != PAN_CONN_CONNECTED) {
            conn->state = PAN_CONN_CONNECTED;
            syslog(LOG_INFO, "[pan] BNEP setup OK (peer initiated)\n");
            pan_conn_report(conn, PROFILE_STATE_CONNECTED);
        }
        return;

    case BNEP_CTRL_FILTER_NET_TYPE_SET:
        /* We support no filters (spec 10: YAGNI). Answering
         * "Unsupported" is the correct, spec-legal reply. */
        n = bnep_encode_filter_rsp(out, sizeof(out),
            BNEP_CTRL_FILTER_NET_TYPE_RSP, BNEP_FILTER_RSP_UNSUPPORTED);
        if (n > 0) { pan_send_ctrl(conn, out, (size_t)n); }
        return;

    case BNEP_CTRL_FILTER_MULTI_ADDR_SET:
        n = bnep_encode_filter_rsp(out, sizeof(out),
            BNEP_CTRL_FILTER_MULTI_ADDR_RSP, BNEP_FILTER_RSP_UNSUPPORTED);
        if (n > 0) { pan_send_ctrl(conn, out, (size_t)n); }
        return;

    case BNEP_CTRL_FILTER_NET_TYPE_RSP:
    case BNEP_CTRL_FILTER_MULTI_ADDR_RSP:
    case BNEP_CTRL_CMD_NOT_UNDERSTOOD:
        syslog(LOG_INFO, "[pan] ctrl 0x%02x rsp=0x%04x\n",
            info.msg_type, info.rsp_code);
        return;

    default:
        n = bnep_encode_cmd_not_understood(out, sizeof(out),
            info.unknown_type);
        if (n > 0) { pan_send_ctrl(conn, out, (size_t)n); }
        syslog(LOG_WARNING, "[pan] unknown ctrl 0x%02x, replied "
            "Command Not Understood\n", info.unknown_type);
        return;
    }
}
```

- [ ] **Step 5: 把 `bnep_codec.c` 加入构建**

`frameworks/connectivity/bluetooth/CMakeLists.txt:314` 附近是 zephyr SAL 源码
列表，紧跟在 `sal_pan_interface.c` 之后追加一行：

```cmake
    list(APPEND CSRCS ${BLUETOOTH_DIR}/service/stacks/zephyr/bnep_codec.c)
```

- [ ] **Step 6: 编译 + host 测试双绿**

Run:
```bash
make -C docs_ble/tools/bnep_codec_test test
./build.sh contest_board:ai_agent -j8 2>&1 | tail -20
```
Expected: 单元测试 `all checks passed`；固件编译 0 error。
注意此时 `pan_chan_recv` 还在用旧数据路径（T6 才改），只要它引用的常量已换成
codec 的名字就能编过。

- [ ] **Step 7: 提交**

```bash
cd frameworks && git add connectivity/bluetooth/service/stacks/zephyr/bnep_codec.c \
  connectivity/bluetooth/service/stacks/zephyr/bnep_codec.h \
  connectivity/bluetooth/service/stacks/include/sal_pan_interface.h \
  connectivity/bluetooth/service/stacks/zephyr/sal_pan_interface.c \
  connectivity/bluetooth/CMakeLists.txt
git commit -m "fix(bnep): rewrite control path on a tested codec

Setup Request now carries a 1-byte UUID Size field (7 bytes total), the
Setup Response is the standard 4 bytes, and only 0x0000 counts as success.
Removes the local constant table whose response codes were wrong from
0x0003 up and whose 0x02 collided between control and compressed frames."
cd .. && git add docs_ble && git commit -m "test(bnep): host unit tests for the BNEP codec"
```

---

## Task 6: SAL 数据路径重写（修 D1 + D9）

**Files:**
- Modify: `frameworks/connectivity/bluetooth/service/stacks/zephyr/sal_pan_interface.c:52-73`（TX 上限宏）
- Modify: `frameworks/connectivity/bluetooth/service/stacks/zephyr/sal_pan_interface.c:281-402`（`pan_chan_recv` 数据分支）
- Modify: `frameworks/connectivity/bluetooth/service/stacks/zephyr/sal_pan_interface.c:942-987`（`bt_sal_pan_write` → `bt_sal_pan_write_eth`）
- Modify: `frameworks/connectivity/bluetooth/service/stacks/include/sal_pan_interface.h`（API 声明）

**Interfaces:**
- Consumes（T2/T3 产出）：`bnep_encode_eth(uint8_t* out, size_t cap, const uint8_t* eth, size_t eth_len, const uint8_t local[6], const uint8_t peer[6], bool compress) -> int`、`bnep_decode_eth(uint8_t* out, size_t cap, const uint8_t* bnep, size_t bnep_len, const uint8_t local[6], const uint8_t peer[6], const uint8_t** ctrl_out, size_t* ctrl_len) -> int`、`BNEP_DECODE_IS_CONTROL`、`bnep_mac_from_le48`
- Produces（T7 依赖，精确签名）：
  - `bt_status_t bt_sal_pan_write_eth(const bt_address_t* addr, const uint8_t* eth_frame, uint16_t eth_len);`
  - `uint16_t bt_sal_pan_get_tx_mtu(const bt_address_t* addr);`（未连接返回 0）
  - `void pan_on_eth_received(bt_address_t* addr, const uint8_t* eth_frame, uint16_t eth_len);`（由 T7 在 `panu_service.c` 实现，本 Task 只声明并调用）

- [ ] **Step 1: 删掉错误的 TX 上限，改成从协商值派生**

`sal_pan_interface.c:72-73` 现在是：

```c
/* Max BNEP Ethernet payload per global ACL PDU (253 - 3 BNEP hdr) */
#define PAN_TX_PAYLOAD_MAX (CONFIG_BT_L2CAP_TX_MTU - 3)
```

`CONFIG_BT_L2CAP_TX_MTU=253` → 上限 250 字节 → 约 290 字节的 DHCP DISCOVER
被本地拒掉，一个字节都没上过空口（spec §11.1 / D9）。整段替换为：

```c
/* BNEP frame size limits.
 *
 * There is no compile-time TX limit: the BR/EDR L2CAP send path checks
 * buf->len against the *negotiated* br_chan->tx.mtu (zblue
 * classic/l2cap_br.c:1906) and never looks at CONFIG_BT_L2CAP_TX_MTU.
 * pan_tx_pool is already sized for PAN_TX_MTU (1691), so the only real
 * ceiling is what the peer agreed to in its CONFIG_REQ.
 *
 * PAN_ETH_FRAME_MAX is the largest Ethernet frame we ever handle; it must
 * match CONFIG_NET_ETH_PKTSIZE on the TAP side so neither direction can
 * be the short end. */
#define PAN_ETH_FRAME_MAX 1514
/* Worst-case BNEP header: General Ethernet = type(1)+dst(6)+src(6)+proto(2) */
#define PAN_BNEP_HDR_MAX  15
```

- [ ] **Step 2: 用 codec 重写 `pan_chan_recv`**

把 `:281-402` 整个 `pan_chan_recv` 替换为下面这版。重点：
先解码，控制帧交给 T5 的 `pan_handle_control`，数据帧还原成整个以太帧后
一次性交给 profile 层。重组缓冲是**每连接一个静态数组**——
`CONFIG_BT_RX_STACK_SIZE=1200`，1514 字节的栈数组会直接踩爆 RX 线程栈，
而且只在大包时才崩，小包测试全过（Global Constraints 那条约束就是为此）。

```c
/* One reassembly buffer per connection, in BSS not on the stack:
 * CONFIG_BT_RX_STACK_SIZE is 1200 bytes. Only the zblue RX thread runs
 * this callback, and it handles one channel's PDU at a time, so a single
 * buffer per pan_conn_t is enough. */
static int pan_chan_recv(struct bt_l2cap_chan* chan, struct net_buf* buf)
{
    pan_conn_t* conn = CONTAINER_OF(BT_L2CAP_BR_CHAN(chan), pan_conn_t, chan);
    const uint8_t* ctrl = NULL;
    size_t ctrl_len = 0;
    uint8_t local_mac[6];
    uint8_t peer_mac[6];
    int n;

    if (!conn || buf->len < 1) {
        return -EINVAL;
    }

    bnep_mac_from_le48(peer_mac, conn->addr.addr);
    memcpy(local_mac, g_pan.local_mac, 6);

    n = bnep_decode_eth(conn->rx_eth, sizeof(conn->rx_eth),
        buf->data, buf->len, local_mac, peer_mac, &ctrl, &ctrl_len);

    if (n == BNEP_DECODE_IS_CONTROL) {
        pan_handle_control(conn, ctrl, ctrl_len);
        return 0;
    }
    if (n < 0) {
        syslog(LOG_WARNING, "[pan] rx decode failed %d (type=0x%02x len=%u)\n",
            n, buf->data[0], buf->len);
        return 0;   /* a bad frame is not a channel error */
    }
    if (conn->state != PAN_CONN_CONNECTED) {
        syslog(LOG_WARNING, "[pan] rx data before setup done, dropped\n");
        return 0;
    }

    pan_on_eth_received(&conn->addr, conn->rx_eth, (uint16_t)n);
    return 0;
}
```

`pan_conn_t`（`:89-95`）加缓冲字段：

```c
    uint8_t rx_eth[PAN_ETH_FRAME_MAX];
```

并在 `g_pan`（`:122-137`）里加本机 MAC 缓存：

```c
    uint8_t local_mac[6];      /* BD_ADDR byte-reversed; filled lazily */
    bool local_mac_valid;
```

本机 MAC **不要**在 `bt_sal_pan_init()` 里取。`bt_sal_get_address()`
（`sal_adapter_interface.c:852-866`）内部有
`SAL_ASSERT(got.type == BT_ADDR_LE_PUBLIC)`，在控制器还没上报地址时会直接断言。
改成惰性获取，插在 `pan_chan_recv` 上面：

```c
/* bt_sal_get_address asserts if the controller has not reported an
 * address yet, so resolve it lazily on the first frame instead of during
 * bt_sal_pan_init(). By the time any BNEP frame moves, the ACL is up and
 * the address is definitely valid. */
static const uint8_t* pan_local_mac(void)
{
    if (!g_pan.local_mac_valid) {
        bt_address_t local;
        if (bt_sal_get_address(PRIMARY_ADAPTER, &local) != BT_STATUS_SUCCESS) {
            syslog(LOG_ERR, "[pan] local address unavailable\n");
            return NULL;
        }
        bnep_mac_from_le48(g_pan.local_mac, local.addr);
        g_pan.local_mac_valid = true;
    }
    return g_pan.local_mac;
}
```

`pan_chan_recv` 里对应改成：

```c
    const uint8_t* local_mac = pan_local_mac();

    if (!local_mac) {
        return 0;
    }
    bnep_mac_from_le48(peer_mac, conn->addr.addr);
```

（即删掉 `uint8_t local_mac[6];` 与那句 `memcpy`。）

- [ ] **Step 3: 用整帧接口替换 `bt_sal_pan_write`**

旧 `bt_sal_pan_write`（`:942-987`）的签名本身就是缺陷的载体：它收下
`dst_addr`/`src_addr` 然后 `(void)` 掉（`:950-951`），于是 General Ethernet
的 12 字节 MAC 永远写不进去（D1）。删掉整个函数，换成整帧接口：

```c
bt_status_t bt_sal_pan_write_eth(const bt_address_t* addr,
    const uint8_t* eth_frame, uint16_t eth_len)
{
    pan_conn_t* conn;
    struct net_buf* buf;
    const uint8_t* local_mac;
    uint8_t peer_mac[6];
    uint16_t limit;
    int n, ret;

    if (!addr || !eth_frame || eth_len < BNEP_ETH_HDR_LEN
        || eth_len > PAN_ETH_FRAME_MAX) {
        return BT_STATUS_PARM_INVALID;
    }
    conn = pan_find_conn(addr);
    if (!conn || conn->state != PAN_CONN_CONNECTED) {
        return BT_STATUS_NOT_READY;
    }
    local_mac = pan_local_mac();
    if (!local_mac) {
        return BT_STATUS_FAIL;
    }

    /* The only real ceiling is the negotiated L2CAP MTU. If we ever exceed
     * it the MTU derivation chain is broken (the TAP MTU should have been
     * clamped to tx.mtu - 14 at ifup, see Task 7), so count it loudly
     * instead of silently dropping - g_pan.tx_oversize must stay 0. */
    limit = conn->chan.tx.mtu;
    if ((uint32_t)eth_len + PAN_BNEP_HDR_MAX > (uint32_t)limit) {
        g_pan.tx_oversize++;
        syslog(LOG_ERR, "[pan] tx %u > tx_mtu %u (oversize=%lu)\n",
            eth_len, limit, (unsigned long)g_pan.tx_oversize);
        return BT_STATUS_NOMEM;
    }

    buf = bt_l2cap_create_pdu_timeout(&pan_tx_pool, 0, K_NO_WAIT);
    if (!buf) {
        return BT_STATUS_NOMEM;
    }

    bnep_mac_from_le48(peer_mac, conn->addr.addr);
    /* compress=false in v1: always General Ethernet. Compression only saves
     * 12 bytes and every wrong-compression bug is a silent blackhole. */
    n = bnep_encode_eth(net_buf_tail(buf), net_buf_tailroom(buf),
        eth_frame, eth_len, local_mac, peer_mac, false);
    if (n <= 0) {
        syslog(LOG_ERR, "[pan] tx encode failed %d\n", n);
        net_buf_unref(buf);
        return BT_STATUS_FAIL;
    }
    net_buf_add(buf, (size_t)n);

    ret = bt_l2cap_chan_send(&conn->chan.chan, buf);
    if (ret < 0) {
        syslog(LOG_ERR, "[pan] tx send failed %d (len=%d tx_mtu=%u)\n",
            ret, n, conn->chan.tx.mtu);
        net_buf_unref(buf);
        return BT_STATUS_FAIL;
    }
    return BT_STATUS_SUCCESS;
}

uint16_t bt_sal_pan_get_tx_mtu(const bt_address_t* addr)
{
    pan_conn_t* conn;

    if (!addr) {
        return 0;
    }
    conn = pan_find_conn(addr);
    if (!conn || conn->state != PAN_CONN_CONNECTED) {
        return 0;
    }
    return conn->chan.tx.mtu;
}
```

`g_pan` 再加一个计数器：

```c
    uint32_t tx_oversize;      /* must stay 0 in normal operation */
```

- [ ] **Step 4: 顺手修两处指针类型错用**

`:938` 与旧 `:980` 把 `struct bt_l2cap_br_chan*` 传给收
`struct bt_l2cap_chan*` 的函数。因为 `chan` 是 `bt_l2cap_br_chan` 的第一个成员，
地址相同所以能跑，但类型是错的、编译器只给告警。统一成 `&conn->chan.chan`：

```c
    return bt_l2cap_chan_disconnect(&conn->chan.chan) == 0 ?
        BT_STATUS_SUCCESS : BT_STATUS_FAIL;
```

- [ ] **Step 5: 更新头文件声明**

`sal_pan_interface.h` 把旧的 `bt_sal_pan_write` 声明（`:57-59`）替换为：

```c
bt_status_t bt_sal_pan_write_eth(const bt_address_t* addr,
    const uint8_t* eth_frame, uint16_t eth_len);
uint16_t bt_sal_pan_get_tx_mtu(const bt_address_t* addr);
```

`pan_service.h` 里旧的 `pan_on_data_received` 声明保留不动（T7 会把它的实现
改成薄封装），并新增：

```c
void pan_on_eth_received(bt_address_t* addr, const uint8_t* eth_frame,
    uint16_t eth_len);
```

- [ ] **Step 6: 编译（此时会因 `panu_service.c` 还在调旧接口而失败）**

Run: `./build.sh contest_board:ai_agent -j8 2>&1 | grep -E 'error' | head`
Expected: 只剩 `panu_service.c` 里 `bt_sal_pan_write` 未声明的错误——
这正是 T7 的入口。**T6 不单独提交**，与 T7 一起提交，避免留一个编不过的中间提交。

---

## Task 7: TAP 整帧收发 + MTU 派生（修 D8 + 落地 §11.3）

**Files:**
- Modify: `frameworks/connectivity/bluetooth/service/profiles/pan/panu_service.c:580-606`（TAP 读）
- Modify: `frameworks/connectivity/bluetooth/service/profiles/pan/panu_service.c:608-631`（缓冲尺寸）
- Modify: `frameworks/connectivity/bluetooth/service/profiles/pan/panu_service.c:640-670`（enable 路径）
- Modify: `frameworks/connectivity/bluetooth/service/profiles/pan/panu_service.c:860-909`（`pan_on_data_received`）
- Modify: `frameworks/connectivity/bluetooth/service/profiles/pan/panu_service.c:1108`（`.uuid`）
- Modify: `frameworks/connectivity/bluetooth/framework/include/bt_uuid.h`

**Interfaces:**
- Consumes（T6 产出）：`bt_sal_pan_write_eth(const bt_address_t*, const uint8_t*, uint16_t) -> bt_status_t`、`bt_sal_pan_get_tx_mtu(const bt_address_t*) -> uint16_t`
- Produces（T9 依赖）：`static int pan_set_tap_mtu(const char* devname, uint16_t mtu);`、`static void pan_ifup_and_dhcp(void);`

- [ ] **Step 1: 读缓冲改成整帧上限**

`panu_service.c:608-631` 的 `pan_get_tun_packet_size()` 返回 `MTU - 14`，
而 `:659` 用它开读缓冲、`:585` 用它当 `read()` 长度。实际数值链是
`CONFIG_NET_ETH_PKTSIZE=1514` → `SIOCGIFMTU` 给 1500 → 缓冲只有 **1486**。
NuttX `tun_read()` 在 `buflen < 整帧长` 时返回 `-EINVAL` 并且**不出队**
（`nuttx/drivers/net/tun.c:1127-1133`），所以第一个满 MSS 的 1514 字节帧
一到，上行就永久卡死（spec §11.2 / D8）。

在文件顶部（`:92` 的 `PAN_DEV_NAME` 旁）加：

```c
/* Largest Ethernet frame the TAP device can hand us. Must equal
 * CONFIG_NET_ETH_PKTSIZE: netdev_register.c:318-323 sets d_pktsize to it
 * for NET_LL_ETHERNET, and tun_read() rejects (without dequeuing!) any
 * read whose buflen is smaller than the queued frame. Deriving this from
 * SIOCGIFMTU is what broke it - MTU is pktsize minus the 14-byte header,
 * so an MTU-sized buffer is always 14 bytes short. */
#define PAN_ETH_FRAME_MAX CONFIG_NET_ETH_PKTSIZE
```

把 `:654-659` 的三行替换为：

```c
        g_pan.tun_packet_size = PAN_ETH_FRAME_MAX;
        pan_read_buf = malloc(PAN_ETH_FRAME_MAX);
```

`pan_get_tun_packet_size()` 整个函数删掉（它唯一的调用点就是这里）。

- [ ] **Step 2: TAP 读改成整帧直送**

`:580-606` 的 `pan_tap_poll_data` 现在先 `memcpy` 出以太头、再把头和载荷拆开
交给 `bt_sal_pan_write`。改成整帧一次交出去：

```c
static void pan_tap_poll_data(service_poll_t* poll, int revent, void* userdata)
{
    if (revent & POLL_READABLE) {
        int ret = read(g_pan.tun_fd, pan_read_buf, PAN_ETH_FRAME_MAX);

        if (ret < (int)sizeof(eth_hdr_t)) {
            if (ret < 0 && errno != EAGAIN && errno != EINTR) {
                BT_LOGE("%s tap read failed %d", __func__, errno);
            }
            return;
        }
        bt_pm_busy(PROFILE_PANU, &g_pan.peer_addr);
        bt_sal_pan_write_eth(&g_pan.peer_addr, pan_read_buf, (uint16_t)ret);
        bt_pm_idle(PROFILE_PANU, &g_pan.peer_addr);
        return;
    }

    if (revent & POLL_WRITABLE) {
        return;
    }

    BT_LOGE("%s poll disconnected", __func__);
    pan_close_all_conn();
}
```

- [ ] **Step 3: 下行改成整帧写 TAP**

`:860-909` 的 `pan_on_data_received` 是"由 SAL 传来 dst/src/proto/payload，
在这里拼以太头"的形状——拼头的责任现在归 codec 了。把它整个替换成
新的 `pan_on_eth_received`（`pan_service.h:61` 的旧声明一并换掉，
全仓库只有它自己在用，已 grep 确认无其他调用者）：

```c
void pan_on_eth_received(bt_address_t* addr, const uint8_t* eth_frame,
    uint16_t eth_len)
{
    pan_msg_t* pan_msg;
    uint8_t* packet;

    if (!eth_frame || eth_len < sizeof(eth_hdr_t)
        || eth_len > PAN_ETH_FRAME_MAX) {
        BT_LOGE("%s bad frame len %u", __func__, eth_len);
        return;
    }

    pan_msg = (pan_msg_t*)malloc(sizeof(pan_msg_t));
    if (pan_msg == NULL) {
        BT_LOGE("%s msg malloc failed", __func__);
        return;
    }
    packet = malloc(eth_len);
    if (packet == NULL) {
        free(pan_msg);
        BT_LOGE("%s packet malloc failed", __func__);
        return;
    }
    memcpy(packet, eth_frame, eth_len);

    pan_msg->evt_id = DATA_IND_EVT;
    memcpy(&pan_msg->addr, addr, sizeof(bt_address_t));
    pan_msg->data_evt.protocol = (uint16_t)((eth_frame[12] << 8)
                                            | eth_frame[13]);
    pan_msg->data_evt.length = eth_len;
    pan_msg->data_evt.packet = packet;

    do_in_service_loop(pan_service_event_process, pan_msg);
}
```

`on_pan_data_incoming`（`:794-808`）不用改：它已经是"整帧写 `tun_fd`"，
之前只是被上面那层拼出来的帧喂着。

- [ ] **Step 4: MTU 派生 —— 按协商结果设 TAP 的 MTU**

`pan_tap_bridge_open()`（`:531-568`）现在开完设备就直接 `netlib_ifup()`，
此时 BNEP 还没握手，`tx.mtu` 还不知道。把 `ifup` 从 open 里挪出来，
改成握手成功之后再做，中间插入 MTU 设置。

先加这个工具函数（放在 `pan_tap_bridge_open` 上面）：

```c
/* NuttX SIOCSIFMTU writes d_pktsize = ifr_mtu + d_llhdrlen
 * (netdev_ioctl.c:1023-1025), and TCP_MSS/UDP_MSS are derived from
 * d_pktsize (netconfig.h:325,494). Clamping the MTU here is therefore
 * what stops the IP stack from ever generating a frame that BNEP cannot
 * carry - it is the load-bearing half of the MTU derivation chain. */
static int pan_set_tap_mtu(const char* devname, uint16_t mtu)
{
    struct ifreq ifr = { 0 };
    int sockfd, ret;

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        BT_LOGE("%s socket failed %d", __func__, errno);
        return -errno;
    }
    strlcpy(ifr.ifr_name, devname, IFNAMSIZ);
    ifr.ifr_mtu = mtu;
    ret = ioctl(sockfd, SIOCSIFMTU, (unsigned long)&ifr);
    if (ret < 0) {
        ret = -errno;
        BT_LOGE("%s SIOCSIFMTU %u failed %d", __func__, mtu, errno);
    } else {
        BT_LOGI("%s MTU set to %u", devname, mtu);
    }
    close(sockfd);
    return ret;
}
```

把 `pan_tap_bridge_open()` 里的 `:562` 那行 `netlib_ifup(...)` **删掉**。
MAC 设置（`:561` 的 `netlib_setmacaddr`）保持在原位——它必须在 ifup 之前，
NuttX 的 `SIOCSIFHWADDR` 处理里写了 "will not take effect until ifup"
（`netdev_ioctl.c:1117`），现在的顺序正好是对的，不要动。

- [ ] **Step 5: 握手成功后的启动序列**

`:770-784` 那段 `PROFILE_STATE_CONNECTED` 分支现在是直接 `pan_start_dhcp()`。
换成完整序列：先按协商 MTU 设接口，再 ifup，最后才 DHCP。

```c
static void pan_ifup_and_dhcp(void)
{
    uint16_t tx_mtu = bt_sal_pan_get_tx_mtu(&g_pan.peer_addr);
    uint16_t if_mtu;

    /* tx.mtu is whatever the peer agreed to in its L2CAP CONFIG_REQ:
     * 1691 from Android/HyperOS NAP, or the 672 fallback if it sent no MTU
     * option (zblue classic/l2cap_br.c:1289,1448). Either is usable - we
     * just have to tell the IP stack which one it is. 14 bytes is the
     * worst-case BNEP header (General Ethernet). */
    if (tx_mtu <= BNEP_ETH_HDR_LEN) {
        BT_LOGE("no negotiated MTU (%u), aborting ifup", tx_mtu);
        return;
    }
    if_mtu = (uint16_t)(tx_mtu - BNEP_ETH_HDR_LEN);
    if (if_mtu > 1500) {
        if_mtu = 1500;
    }
    pan_set_tap_mtu(PAN_DEV_NAME, if_mtu);

    if (netlib_ifup(PAN_DEV_NAME) < 0) {
        BT_LOGE("ifup %s failed", PAN_DEV_NAME);
        return;
    }
    BT_LOGI("%s up, tx_mtu=%u if_mtu=%u, starting DHCP",
        PAN_DEV_NAME, tx_mtu, if_mtu);
    pan_start_dhcp(PAN_DEV_NAME);
}
```

`:781-782` 两行换成 `pan_ifup_and_dhcp();`。

对应地，断开分支要 `netlib_ifdown(PAN_DEV_NAME)`，否则下次握手时接口还是
上一次的 MTU。`pan_tap_bridge_close()`（`:570-578`）已经有 ifdown，
但它只在 PAN **disable** 时调用；连接断开而 PAN 仍 enable 的情况要单独处理。
在 `PROFILE_STATE_DISCONNECTED` 分支里 `g_dhcp_running = false;`（`:726`）之后加：

```c
            netlib_ifdown(PAN_DEV_NAME);
```

- [ ] **Step 6: `.uuid` 顺手修正（非阻塞项）**

先在 `framework/include/bt_uuid.h:33` 的 `BT_UUID_HFP_AG` 之后补两个宏：

```c
#define BT_UUID_PANU 0x1115
#define BT_UUID_NAP 0x1116
```

再把 `panu_service.c:1108` 改掉：

```c
    .uuid = BT_UUID_DECLARE_16(BT_UUID_PANU),
```

**注意这不是 SDP 修复。** 对外发布的 PANU SDP 记录是
`sal_pan_interface.c:423-458` 的 `g_panu_sdp_attrs`，本来就是对的。
`profile_service_t.uuid` 的唯一消费者是 `service_manager.c:163-183`
（它会跳过全零 UUID），再往上只有 IPC 的"查询本机 UUID 列表"。
所以这一步**对手机侧行为零影响**，只是让本机自查一致（spec §7.2 / D10）。

- [ ] **Step 7: 编译，跑通 T6+T7 的联合改动**

Run: `./build.sh contest_board:ai_agent -j8 2>&1 | tail -20`
Expected: 0 error。同时确认 `make -C docs_ble/tools/bnep_codec_test test` 仍全绿。

- [ ] **Step 8: 提交（T6 + T7 一起）**

```bash
cd frameworks
git add connectivity/bluetooth/service/stacks/zephyr/sal_pan_interface.c \
        connectivity/bluetooth/service/stacks/include/sal_pan_interface.h \
        connectivity/bluetooth/service/profiles/pan/panu_service.c \
        connectivity/bluetooth/service/profiles/include/pan_service.h \
        connectivity/bluetooth/framework/include/bt_uuid.h
git commit -m "fix(bnep): carry whole Ethernet frames and derive every limit from tx.mtu

The data path used to hand the SAL layer a dst/src/proto triple and then
discard the MACs, so General Ethernet frames went out with a 3-byte header
instead of 15 and the phone dropped every broadcast. Frames now cross the
SAL boundary intact.

Two length bugs went with it. The TX ceiling was CONFIG_BT_L2CAP_TX_MTU-3
= 250 bytes, which rejected a ~290-byte DHCP DISCOVER before it reached the
encoder; the BR/EDR send path never consults that Kconfig, only the
negotiated tx.mtu. And the TAP read buffer was MTU-14 = 1486, while
tun_read() returns -EINVAL without dequeuing when the buffer is shorter
than the queued frame, so the first full-MSS frame would wedge the uplink
permanently. The TAP MTU is now clamped to tx.mtu-14 at ifup, which keeps
the IP stack from generating anything BNEP cannot carry."
```

---

## Task 8: defconfig 与 COD（修 D7 + §7.1 的符号名陷阱）

**Files:**
- Modify: `contest2026_181_womenshayebuhuidui/board/contest_board/configs/ai_agent/defconfig`
- Modify: `frameworks/connectivity/bluetooth/service/common/bluetooth_define.h:29-33`
- Create: `contest2026_181_womenshayebuhuidui/board/contest_board/configs/ai_agent_pandbg/defconfig`

**Interfaces:**
- Consumes: 无（纯配置）
- Produces（T10 依赖）：`ai_agent_pandbg` 这个调试用 board config，比 `ai_agent`
  多开 `SF32LB52_BT_TRACE_ACL_FULL`（T4 引入）用于 Gate B 抓 trace

- [ ] **Step 1: 修 COD 的符号名链路**

这里有个三重错位，只改 defconfig 是不生效的：

1. `frameworks/connectivity/bluetooth/Kconfig:278` 声明的符号名自带一次前缀
   （`config CONFIG_BLUETOOTH_DEFAULT_COD`），生成的宏是
   **`CONFIG_CONFIG_BLUETOOTH_DEFAULT_COD`**。
2. `bluetooth_define.h:29` 却在 `#ifdef CONFIG_BLUETOOTH_DEFAULT_COD`（单前缀），
   永远为假 → 走 `#else` 的硬编码。
3. defconfig:323 现有的 `CONFIG_BLUETOOTH_DEFAULT_COD=0x00020510` 不对应任何
   合法符号，被 olddefconfig 丢弃——这行是死的。

验证现状：`grep CONFIG_CONFIG_BLUETOOTH_DEFAULT_COD out/nuttx_contest_board_ai_agent/.config`
应得 `0x00280704`（Kconfig 的 default），而单前缀名在 `.config` 里根本不存在。

`bluetooth_define.h:29-33` 替换为：

```c
/* The Kconfig symbol is literally named CONFIG_BLUETOOTH_DEFAULT_COD, so
 * Kconfig emits CONFIG_CONFIG_BLUETOOTH_DEFAULT_COD. Testing the
 * single-prefix name here silently fell through to the #else for years.
 * 0x002A0704 = Wearable/Wristwatch major+minor, service class bits
 * Networking(17) | Capturing(19) | Audio(21). The Networking bit is what
 * makes a phone offer Bluetooth tethering to us. */
#ifdef CONFIG_CONFIG_BLUETOOTH_DEFAULT_COD
#define DEFAULT_DEVICE_OF_CLASS CONFIG_CONFIG_BLUETOOTH_DEFAULT_COD
#else
#define DEFAULT_DEVICE_OF_CLASS 0x002A0704
#endif
```

- [ ] **Step 2: 改 defconfig**

删掉 `:323` 的死行，按实际符号名写。同时落实 spec §5 的缓冲调整和 GATT 关闭：

```
CONFIG_CONFIG_BLUETOOTH_DEFAULT_COD=0x002A0704
CONFIG_BT_BUF_ACL_RX_SIZE=1695
CONFIG_BT_BUF_ACL_RX_COUNT_EXTRA=2
# CONFIG_AI_AGENT_BLE_GATT is not set
```

三个数字的依据：

- `1695` —— BNEP 要 L2CAP MTU 1691，而 `BT_L2CAP_RX_MTU` 定义为
  `CONFIG_BT_BUF_ACL_RX_SIZE - BT_L2CAP_HDR_SIZE`（`l2cap.h:39`），
  现值 1025 只给到 1021。我们 CONFIG_REQ 里承诺 1691 却只能重组 1021，
  对端一发满长帧就丢（D7）。1695 - 4 = 1691，正好对齐承诺。
- `EXTRA 5 → 2` —— RX 池大小 ≈ `size × count`，单包从 1025 涨到 1695
  是 +65%，把 count 从 (base+5) 降到 (base+2) 把总量按住在 8.3→8.6 KB
  量级，基本是免费的。
- GATT 关掉 —— spec §1.1 已逐条核过：AI API 走 `bt-pan` + NuttX socket，
  GATT 现存的唯一活口是 `network_manager.c:633-634` 的备用通道，
  legacy 命令通道早已停用（`agent_main.c:646-663` 的注释写明
  "the legacy command channel is disabled to avoid taking the instance"），
  `ble_cmd_handler.c:24-26` 只处理 `wifi_config`/`ping`/`status`
  而本板无 WiFi（`# CONFIG_DRIVERS_IEEE80211 is not set`）。
  所有引用都在 `#ifdef CONFIG_AI_AGENT_BLE_GATT` 内，
  **因此不需要改 `packages/ai_agent` 的任何一行源码**。

`CONFIG_BT_L2CAP_TX_MTU=253` **不动**：PAN 用的是自己的 `pan_tx_pool`
（按 1691 开的），而 BR/EDR 发送路径不看这个值（见 Global Constraints）。

- [ ] **Step 3: 调试 board config 用生成的方式，不手抄**

`ai_agent/defconfig` 有 10 KB、300 多行。手抄一份调试变体，两边一定会漂。
改成从产品 defconfig 派生，差异只写在脚本里。

创建 `docs_ble/tools/mk_pandbg_config.sh`：

```bash
#!/usr/bin/env bash
# Generate the Gate B debug board config from the product one so the two can
# never drift: the only differences are the lines appended below.
#
# Usage: bash docs_ble/tools/mk_pandbg_config.sh
# Then:  ./build.sh contest_board:ai_agent_pandbg -j8
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CFGDIR="$ROOT/contest2026_181_womenshayebuhuidui/board/contest_board/configs"
SRC="$CFGDIR/ai_agent/defconfig"
DST="$CFGDIR/ai_agent_pandbg/defconfig"

[ -f "$SRC" ] || { echo "missing $SRC" >&2; exit 1; }
mkdir -p "$(dirname "$DST")"

{
  cat "$SRC"
  echo ''
  echo '# --- generated by docs_ble/tools/mk_pandbg_config.sh, do not edit ---'
  # Full-length HCI ACL trace: the input to bnep_pcap.py (Gate B).
  echo 'CONFIG_SF32LB52_BT_TRACE_ACL_FULL=y'
  # btsnoop needs KVDB to read its enable key (log_server.c:243).
  echo 'CONFIG_KVDB=y'
} > "$DST"

echo "wrote $DST"
```

Run: `bash docs_ble/tools/mk_pandbg_config.sh`
Expected: `wrote .../ai_agent_pandbg/defconfig`。

board config 不需要在任何地方注册——`configs/<name>/defconfig` 是被
`tools/configure.sh` 自动发现的，`configs/` 下现有 `ai_agent`、`nsh`、
`spp_verify` 三个就是这个形状。

- [ ] **Step 4: 两个配置都构建**

```bash
cd /home/aila/projects/vela_contest
./build.sh contest_board:ai_agent -j8 2>&1 | tail -20
bash docs_ble/tools/mk_pandbg_config.sh
./build.sh contest_board:ai_agent_pandbg -j8 2>&1 | tail -20
```

Expected: 两个都 0 error。

- [ ] **Step 5: 验证生成的 `.config` 真的带上了新值**

这一步是必须的，因为 Step 1 那个双前缀陷阱正是"defconfig 写了但没生效"
的典型——只看构建成功会漏掉。

```bash
cd /home/aila/projects/vela_contest
for k in CONFIG_CONFIG_BLUETOOTH_DEFAULT_COD CONFIG_BT_BUF_ACL_RX_SIZE \
         CONFIG_BT_BUF_ACL_RX_COUNT_EXTRA CONFIG_AI_AGENT_BLE_GATT \
         CONFIG_SF32LB52_BT_TRACE_ACL_FULL CONFIG_KVDB; do
  printf '%-42s prod=%-12s dbg=%s\n' "$k" \
    "$(grep -E "^$k=" out/nuttx_contest_board_ai_agent/.config | cut -d= -f2 || echo unset)" \
    "$(grep -E "^$k=" out/nuttx_contest_board_ai_agent_pandbg/.config | cut -d= -f2 || echo unset)"
done
```

Expected:

| 键 | prod | dbg |
|---|---|---|
| `CONFIG_CONFIG_BLUETOOTH_DEFAULT_COD` | `0x002A0704` | `0x002A0704` |
| `CONFIG_BT_BUF_ACL_RX_SIZE` | `1695` | `1695` |
| `CONFIG_BT_BUF_ACL_RX_COUNT_EXTRA` | `2` | `2` |
| `CONFIG_AI_AGENT_BLE_GATT` | unset | unset |
| `CONFIG_SF32LB52_BT_TRACE_ACL_FULL` | **unset** | `y` |
| `CONFIG_KVDB` | **unset** | `y` |

前两列出现 `unset` 而表里写的是具体值 → 说明符号名还是错的，回到 Step 1。
`CONFIG_AI_AGENT_BLE_GATT` 若仍为 `y`，检查是否被别处 `select`。

- [ ] **Step 6: 提交**

```bash
cd /home/aila/projects/vela_contest/frameworks
git add connectivity/bluetooth/service/common/bluetooth_define.h
git commit -m "fix(bt): honour the CONFIG_BLUETOOTH_DEFAULT_COD Kconfig symbol

The Kconfig entry is itself named CONFIG_BLUETOOTH_DEFAULT_COD, so Kconfig
emits CONFIG_CONFIG_BLUETOOTH_DEFAULT_COD. bluetooth_define.h tested the
single-prefix name, which never existed, so the class of device was always
the hardcoded fallback and any defconfig override was silently ignored.
The fallback now also carries the Networking service-class bit, which is
what makes a phone offer Bluetooth tethering."

cd /home/aila/projects/vela_contest/contest2026_181_womenshayebuhuidui
git add board/contest_board/configs/ai_agent/defconfig \
        ../docs_ble/tools/mk_pandbg_config.sh
git commit -m "config: advertise Networking in COD, size ACL RX for BNEP, drop BLE GATT

CONFIG_BT_BUF_ACL_RX_SIZE=1025 gave an L2CAP RX MTU of 1021 while we
advertise 1691 to the peer, so a full-length BNEP frame from the phone was
dropped during reassembly; 1695 makes the two agree. RX_COUNT_EXTRA drops
from 5 to 2 to keep the pool roughly the same size.

BLE GATT only served as a backup network channel, which this product does
not use; disabling it needs no ai_agent source change because every
reference is already inside #ifdef CONFIG_AI_AGENT_BLE_GATT."
```

`vendor/sifli` 的 Kconfig 改动跟 Task 4 的 bth4 改动一起提交（同一仓库、
同一主题：Gate B 的 trace 设施）。

---

## Task 9: DHCP 线程生命周期 + 状态日志（落地 spec §7.4）

**Files:**
- Modify: `frameworks/connectivity/bluetooth/service/profiles/pan/panu_service.c`

**Interfaces:**
- Consumes（T7）：`pan_ifup_and_dhcp()`
- Produces（T10 依赖）：固定字段的状态迁移日志，Gate C 的自动判定靠它

**先纠正一件事：退避逻辑已经存在，不要重写它。** 读完代码后的实际情况是：

- 重连退避已经实现且是三分类的（`:728-769`，从 xiaozhi 移植）：首连失败
  3 次 × 3s（`PAN_FIRST_CONNECT_MAX_RETRIES`）；已连过再断则 30 次 × 10s
  （`PAN_ABNORMAL_MAX_RECONNECT` / `PAN_ABNORMAL_RECONNECT_INTERVAL_MS`）；
  外加 `:196-210` 的 5s LCPU 冷却窗。
- DHCP 的耐心也够：`dhcpc_request()` 内部是
  `CONFIG_NETUTILS_DHCPC_RETRIES=3` × `RECV_TIMEOUT_MS=3000` ≈ 9s，
  外层 `:291` 又套了 `retries < 10` 配 `sleep(2)`，合计约 110 秒。

所以 **spec §7.4 初稿说的"2/4/8/16/30s 指数退避"在这份代码里是多余的**——
再叠一层只会让两套退避互相打架。真正的缺陷是 DHCP 工作线程的生命周期，
下面三条都会在 Gate H（2 小时长稳、要求无泄漏）暴露。

- [ ] **Step 1: `g_dhcp_running` 必须在线程每条退出路径上清掉**

`pan_dhcp_thread` 有四条退出路径，**没有一条**清 `g_dhcp_running`：
`:281`（`netlib_getmacaddr` 失败）、`:287`（`dhcpc_open` 失败）、
`:317`（成功）、`:326`（10 次耗尽）。它只在 `PROFILE_STATE_DISCONNECTED`
（`:726`）被清。

而 `pan_start_dhcp()` 的第一句是 `if (g_dhcp_running) { return; }`（`:331`）。
两者相加的后果：**链路保持连接、但 DHCP 拿不到地址时，这个标志永久为真，
之后任何一次 `pan_start_dhcp()` 都直接返回**——不断链就再也不会重试。

这个标志同时承担两个语义（`:291` 的循环条件把它当取消信号用，
`:331` 把它当重入锁用），两个语义都要求"线程活着就为真、退出就为假"。
所以正确的修法是让线程负责清它，而不是新增第三个标志。

把 `pan_dhcp_thread` 改成单一出口：

```c
static void* pan_dhcp_thread(void* arg)
{
    const char* devname = (const char*)arg;
    struct dhcpc_state ds;
    void* handle = NULL;
    uint8_t mac[6];
    int retries = 0;
    bool got_lease = false;

    BT_LOGI("[pan] state=dhcp_start dev=%s", devname);

    if (netlib_getmacaddr(devname, mac) != 0) {
        BT_LOGE("[pan] state=dhcp_failed reason=no_mac dev=%s", devname);
        goto out;
    }

    handle = dhcpc_open(devname, mac, 6);
    if (!handle) {
        BT_LOGE("[pan] state=dhcp_failed reason=open dev=%s", devname);
        goto out;
    }

    /* g_dhcp_running doubles as the cancellation flag: the DISCONNECTED
     * handler clears it to break us out of this loop. */
    while (g_dhcp_running && retries < PAN_DHCP_MAX_ATTEMPT) {
        if (dhcpc_request(handle, &ds) == OK) {
            got_lease = true;
            break;
        }
        retries++;
        BT_LOGW("[pan] state=dhcp_retry attempt=%d/%d delay=2s",
            retries, PAN_DHCP_MAX_ATTEMPT);
        sleep(2);
    }

    if (got_lease) {
        pan_dhcp_apply_lease(devname, &ds);
        g_auto_state = PAN_AUTO_CONNECTED;
    } else {
        BT_LOGE("[pan] state=dhcp_failed attempt=%d", retries);
    }

out:
    if (handle) {
        dhcpc_close(handle);
    }
    /* Cleared last, and by the thread itself: pan_start_dhcp() uses this as
     * its reentry guard, so leaving it set on any exit path wedges DHCP for
     * the rest of the link's life. */
    g_dhcp_running = false;

    if (!got_lease && g_pan_enabled_for_dhcp_recovery()) {
        pan_dhcp_give_up_and_reconnect();
    }
    return NULL;
}
```

`PAN_DHCP_MAX_ATTEMPT` 就是原来的字面量 10，提到宏区（放在
`PAN_ABNORMAL_MAX_RECONNECT` 之后）：

```c
#define PAN_DHCP_MAX_ATTEMPT 10 /* x (9s dhcpc_request + 2s sleep) ~ 110s */
```

`pan_dhcp_apply_lease()` 是把原 `:293-313`（设 IP/掩码/网关 + `dns_add_nameserver`）
原样搬出来的静态函数，**内容一行不改**，只是为了让线程主体只有一个出口。
它的日志改成 spec §7.4 的格式：

```c
    BT_LOGI("[pan] state=dhcp_ok ip=%s gw=%s dns=%s", ip_str, gw_str, dns_str);
```

- [ ] **Step 2: DHCP 线程必须 detached，否则每次连接漏一个 TCB**

`:336-341` 用默认属性建线程，NuttX 下默认是 joinable，而代码从不
`pthread_join`。每次 PAN 连接漏一个线程控制块加 4 KB 栈。Gate H 要跑
2 小时、至少 3 次人为断链，这是会被量出来的。

```c
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 4096);
    /* Nobody joins this thread, so it has to reap itself. */
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&g_dhcp_thread, &attr, pan_dhcp_thread,
            (void*)devname) == 0) {
        pthread_setname_np(g_dhcp_thread, "pan_dhcp");
    } else {
        BT_LOGE("[pan] state=dhcp_failed reason=thread");
        g_dhcp_running = false;
    }
    pthread_attr_destroy(&attr);
```

`pthread_attr_destroy` 也是新加的——原代码 init 了不 destroy。

- [ ] **Step 3: DHCP 耗尽后要交还给重连状态机**

现在 DHCP 失败是个死胡同：BNEP 链路还在（所以三分类断连逻辑不触发），
但永远没有 IP。修法是让 DHCP 彻底失败时主动断开 PAN，由已有的
`PROFILE_STATE_DISCONNECTED` 分支按"异常断连"走 30 次 × 10s 重连。

```c
/* DHCP exhausted while the BNEP link is still up: the disconnect handler
 * owns all reconnect policy, so hand the failure to it rather than growing
 * a second retry ladder here. Most likely cause is the user not having
 * authorised network sharing on the phone yet. */
static void pan_dhcp_give_up_and_reconnect(void)
{
    BT_LOGW("[pan] dhcp exhausted, dropping link to retry; "
            "check Bluetooth tethering is enabled on the phone");
    bt_sal_pan_disconnect(&g_last_nap_addr);
}
```

条件判断用现有状态，不要新造标志——`g_pan_enabled_for_dhcp_recovery()`
在 Step 1 的代码里是占位名，实现时替换成：

```c
    if (!got_lease && g_has_last_nap
        && g_auto_state == PAN_AUTO_CONNECTED) {
        pan_dhcp_give_up_and_reconnect();
    }
```

注意 `bt_sal_pan_disconnect` 是从 DHCP 线程调的，不在 service loop 里。
先确认它是否要求在 service loop 上下文执行：

```bash
grep -n "bt_sal_pan_disconnect" -A 20 \
  frameworks/connectivity/bluetooth/service/stacks/zephyr/sal_pan_interface.c \
  | head -30
```

若它直接操作 zblue 对象（而 zblue 有自己的线程模型），就改成
`do_in_service_loop()` 投递一个事件，由 service loop 调用——
文件里 `:794-808` 的 `on_pan_data_incoming` 就是这个模式，照它写。

- [ ] **Step 4: 日志字段固定化**

spec §7.4 要求每次状态迁移打一条字段固定的日志，Gate C 的成功率统计直接
grep 它。统一成这个形状（前缀一律 `[pan] state=`）：

```
[pan] state=connecting peer=xx:xx:xx:xx:xx:xx attempt=1/3
[pan] state=bnep_setup_sent
[pan] state=bnep_ready tx_mtu=1691 if_mtu=1500
[pan] state=dhcp_start dev=bt-pan
[pan] state=dhcp_ok ip=192.168.x.x gw=192.168.x.x dns=x.x.x.x
[pan] state=dhcp_retry attempt=2/10 delay=2s
[pan] state=dhcp_failed attempt=10
[pan] state=disconnected class=never|abnormal reconnect=3/30
```

把 T5/T6/T7 里已有的 `BT_LOGI` 文案也对齐到这个格式。这不是装饰——Gate C
要跑 30 次连接，人工读日志判定不现实。

- [ ] **Step 5: 编译 + 提交**

```bash
cd /home/aila/projects/vela_contest
./build.sh contest_board:ai_agent -j8 2>&1 | tail -20
```

```bash
cd /home/aila/projects/vela_contest/frameworks
git add connectivity/bluetooth/service/profiles/pan/panu_service.c
git commit -m "fix(pan): let the DHCP worker clear its own running flag

g_dhcp_running is both the cancellation flag for the retry loop and the
reentry guard for pan_start_dhcp(), but no exit path in the worker cleared
it - only the disconnect handler did. So whenever DHCP failed while the
BNEP link stayed up, the flag stayed set and every later attempt returned
immediately: no address, and no way back without a disconnect. The worker
now has a single exit that clears it, and hands a terminal failure to the
disconnect handler, which already owns all the reconnect policy.

The worker was also joinable and never joined, leaking a TCB and 4KB of
stack per connection, which the two-hour soak would have caught.

The retry ladders that were already here are left alone: dhcpc_request()
plus the outer loop already waits ~110s, and the three-class reconnect
logic already backs off."
```

## Task 10: 上板执行 Gate C–H

**Files:**
- Create: `docs_ble/tools/gate_c.py`（30 次连接打分）
- Create: `docs_ble/tools/gate_h.py`（2 小时长稳 + 3 次人为断链）
- Create: `docs_ble/superpowers/plans/2026-08-20-gate-results.md`（结果记录）

**Interfaces:**
- Consumes：T7 的 `state=` 日志、T8 的两个 board config、T4 的 `gate_b.sh`
- Produces：Gate 结果表。这是唯一的验收产物。

**前置：SRAM 裸基线镜像已经存好了。** spec §9 要求"Gate G 之前先测一次裸基线"，
而 90.9% 是文档记录值不是实测值。改动前的镜像已快照到：

```
out/pan_baseline/nuttx-prechange.bin      md5 0dd6bd3654960220bd60e47b04d96ff9
out/pan_baseline/dotconfig-prechange
```

（快照时 `frameworks/` 的 `git status` 是干净的，所以这是真正的改动前镜像。
`out/` 不入 git，这两个文件只在本机。）

**烧录方式**（已验证流程，来自 `docs_ble/11_pan_l2cap_breakthrough.md:104-111`）：

```bash
cd /home/aila/projects/vela_contest
for i in 1 2 3; do
  python3 logs/flash_rts.py out/nuttx_contest_board_ai_agent/nuttx.bin /dev/ttyACM0 \
    && break
  echo "retry $i"
done
```

注意 `flash_rts.py` 的默认镜像路径是旧的
（`out/openvela_contest2026_181_board_ai_agent/nuttx.bin`），**必须显式传参**，
否则会烧一个 8 月 20 日 00:57 的旧固件然后困惑半天。
串口 `/dev/ttyACM0` @ 1000000。烧录到 99% 报
`timeout waiting for RAM command response` 时固件往往已写入，发 `reboot` 确认。

- [ ] **Step 1: 先测裸基线（Gate G 的分母）**

```bash
cd /home/aila/projects/vela_contest
python3 logs/flash_rts.py out/pan_baseline/nuttx-prechange.bin /dev/ttyACM0
```

烧好后串口发 `free`，记录 `Umem` 行的 free/total。这就是基线。写进
`docs_ble/superpowers/plans/2026-08-20-gate-results.md` 的第一行。

**不要跳过这一步。** 跳过它 Gate G 就只能拿 90.9% 这个来源不明的数字比，
而本计划净增了一个 1514 B 的 `rx_eth` 静态缓冲加 ACL RX 池调整，
是否真的"接近零"必须有分母。

- [ ] **Step 2: Gate B —— 用真机 trace 跑离线判定**

烧调试固件（T8 的 `ai_agent_pandbg`），抓一次完整的 PAN 连接过程：

```bash
cd /home/aila/projects/vela_contest
bash docs_ble/tools/mk_pandbg_config.sh
./build.sh contest_board:ai_agent_pandbg -j8
python3 logs/flash_rts.py out/nuttx_contest_board_ai_agent_pandbg/nuttx.bin /dev/ttyACM0
python3 docs_ble/tools/pan_e2e.py 2>&1 | tee logs/gate_b_$(date +%H%M%S).log
bash docs_ble/tools/gate_b.sh logs/gate_b_*.log
```

**判定顺序（spec §11.5，不能颠倒）**：

1. trace 里**必须先出现 DHCP DISCOVER**（tshark 过滤 `bootp`）。
   看不到 → D9 没修干净，回 Task 6，别往下看。
2. 该帧的 BNEP 头必须被 `btbnep` dissector 解析为
   `General Ethernet`，dst = `ff:ff:ff:ff:ff:ff`，且零 malformed。
   解析不出来 → D1 没修干净，回 Task 6。
3. Setup Connection Request 必须是 7 字节 `01 01 02 11 16 11 15`，
   Response 必须是 4 字节且 dissector 显示 *Operation Successful*。
   → 验 D2/D3/D4。

Gate B 不过不进 Gate C。这一门不需要 DHCP 真的成功，只需要字节对。

- [ ] **Step 3: Gate C —— DHCP 取址，3 台设备各 10 次**

先验入门判据（spec §8 的 Gate C 入门判据，验 §7.1 的 COD 持久化覆盖）：

串口发 `bttool` → `dump`（或现有的 `pan dump`），确认 COD 回读值 bit 17
（`0x00020000`）已置。**期望 `0x002A0704`。**

若回读到 `0x00280704`（缺 bit 17）→ `bluetooth_define.h` 的
`DEFAULT_DEVICE_OF_CLASS` 没生效，回 Task 8 Step 1。
若回读到别的值（例如 `0x00020510`）→ 是 unqlite 里存着旧记录
（`CONFIG_BLUETOOTH_STORAGE_UNQLITE_SUPPORT=y`，`DEFAULT_DEVICE_OF_CLASS`
只是"无记录时的初值"）。清存储或用 `set cod` 之类的现有命令覆盖，
**并把这一现象写进结果文档**——它意味着改 defconfig 对已出货设备无效。

再写 `docs_ble/tools/gate_c.py`：循环 10 次「连接 → 等 `state=dhcp_ok` →
`ifconfig` 记 IP → 断开 → 等 5s 冷却」，解析 T9 Step 4 的固定字段日志打分。
三台设备各跑一轮（手机 MAC 用命令行参数传）。

通过标准：每台 ≥ 9/10。

**HyperOS 首次连接需要人工在设置里授权"蓝牙网络共享"**（spec §8）。
脚本第一轮必须先停下来提示用户确认，授权完成后再开始计数——
把人工步骤算进失败率是不诚实的。iPhone 6 需要**蜂窝数据可用 + 个人热点已开**，
不满足就把这一支标 `blocked` 而不是 `failed`。

- [ ] **Step 4: Gate D/E —— ICMP 与 DNS+HTTPS**

```
ping -c 100 <网关>        # 丢包 < 1%，且 100 包期间不出现 30s 断链
nslookup baidu.com
```

Gate E 是真正的业务验收点：让 ai_agent 实际完成一次 AI API 调用（含 TLS 握手）。
`packages/ai_agent` 没改过，走它自己的正常启动路径即可。

Gate C 过而 Gate E 挂，几乎必然是 MTU：TLS 记录会用到大包。
此时先看 `state=bnep_ready` 那行的 `tx_mtu` / `if_mtu` 实测值——
若 `tx_mtu=672`（对端没给 MTU 选项，走了 zblue 的兜底），
`if_mtu` 应为 658，链路可用但每个 TLS 记录会分片，慢但不该失败。
若 `if_mtu` 比 `tx_mtu-14` 大 → `pan_set_tap_mtu()` 没生效，回 Task 7 Step 4。

- [ ] **Step 5: Gate F —— 吞吐 + 满 MSS 上行**

记录实测 kbps（不设硬门槛）。**追加判据**（spec §11.5，验 D8 + §11.3）：

- 连续跑满 MSS 上行（1514 字节以太帧）。构造方式：`ping -s 1472 <网关>`
  正好是 1472 + 8(ICMP) + 20(IP) + 14(Eth) = 1514。跑 200 包。
- 跑完读 `tx_oversize` 计数器（T6 加的 `g_pan.tx_oversize`），
  **必须恒为 0**。非 0 说明 MTU 派生链坏了：IP 栈造出了 BNEP 送不出去的帧。
- 上行不得卡死。卡死 = D8 没修干净（`tun_read()` 的 `-EINVAL` 不出队），
  回 Task 7 Step 1/2 检查读缓冲是不是真的 1514。

`tx_oversize` 需要一条读取途径。若 bttool 没有现成命令，用最省事的办法：
在 T6 的 `bt_sal_pan_write_eth` 里让计数器非 0 时每次都打一条
`[pan] tx_oversize=%u`，Gate F 直接 grep 日志——**不要**为了这个计数器
新加 IPC 命令。

- [ ] **Step 6: Gate G —— SRAM**

烧产品固件（`ai_agent`），串口 `free`，跟 Step 1 的基线比。
通过标准：剩余量 ≥ 基线。

若低于基线，按 spec §9 的处置顺序砍：先 `CONFIG_BT_BUF_ACL_RX_COUNT_EXTRA`
（从 2 再往下），再砍调试设施。**不要**砍 `rx_eth` 的 1514 —— 那会把 D8
放回来。

- [ ] **Step 7: Gate H —— 2 小时长稳**

`docs_ble/tools/gate_h.py`：连接后每 60s 打一次 `free` + `ifconfig`，
期间人为断链 ≥ 3 次（手机关蓝牙 / 走出距离），每次都必须自愈到
`state=dhcp_ok`。

通过标准：2 小时无崩溃；`free` 的空闲量不单调下降（验 T9 Step 2 的
线程泄漏修复）；3 次断链全部恢复。

线程泄漏的量化观察点：每次断链重连会起一个新的 `pan_dhcp` 线程。
串口发 `ps`，`pan_dhcp` **不应**累积多个条目——累积就是
`PTHREAD_CREATE_DETACHED` 没生效。

- [ ] **Step 8: 写结果文档并提交**

`docs_ble/superpowers/plans/2026-08-20-gate-results.md` 记录每一门的实测值，
包括 blocked 的分支和原因。**不要写"应该通过"**，只写实测到的数字。

```bash
cd /home/aila/projects/vela_contest/contest2026_181_womenshayebuhuidui
git add docs_ble/superpowers/plans/2026-08-20-gate-results.md \
        docs_ble/tools/gate_c.py docs_ble/tools/gate_h.py
git commit -m "test(pan): record measured Gate C-H results"
```

---

## 自审记录

**Spec 覆盖**（spec 的 10 条缺陷 + 新增判据逐条落到任务）：

| spec 条目 | 任务 |
|---|---|
| D1 BNEP 数据帧只写 3 字节头 | T2（编码）+ T6（接进 SAL） |
| D2 Setup Request UUID Size 用了 2 字节 | T1（编码）+ T5 |
| D3 Response 造了不存在的 length 字段 | T1 + T5 |
| D4 任何状态码都判 SUCCESS | T5（`info.is_success` 严格判定） |
| D5 frame type 与 control type 混在一个 switch | T3（解码）+ T5/T6 |
| D6 缺 0x02/0x03/0x04 三种压缩帧类型 | T3 + T6 |
| D7 `BT_BUF_ACL_RX_SIZE=1025` < 承诺的 1691 | T8 Step 2 |
| D8 TAP 读缓冲 `MTU-14`，`tun_read()` 永久卡死 | T7 Step 1/2，Gate F 验 |
| D9 250 字节 TX 上限挡掉 DHCP DISCOVER | T6，Gate B 判定 1 验 |
| D10 `profile_service_t.uuid` 全零（顺手修） | T7 Step 6 |
| §7.1 COD 双前缀符号名陷阱 | T8 Step 1，Gate C 入门判据验 |
| §11.3 MTU 派生链 | T6（TX 取 `tx.mtu`）+ T7 Step 4/5（`SIOCSIFMTU`） |
| §7.4 更正后的 DHCP 线程生命周期三条 | T9 Step 1/2/3 |
| Gate C 入门判据（COD 回读 bit 17） | T10 Step 3 |
| Gate F 追加判据（满 MSS + `tx_oversize==0`） | T10 Step 5 |

spec §10「明确不做」的四条在计划里都没有被偷偷做：无 NAP/GN 角色、
BNEP 过滤器一律回 `UNSUPPORTED`（T5）、无 IPv6、TX 不压缩
（T6 的 `bnep_encode_eth(..., false)`）。

**占位符扫描**：本文件不得含 `PLACE[H]OLDER` / `TOD[O]` / `TB[D]` / `FIXM[E]`
这四类待填标记（此处特意写成字符类，好让检查命令不会匹配到自己），
也不得留 HTML 注释形式的待填标记。检查命令见下面的执行前检查清单。

**类型一致性**（跨任务的函数签名，实现时不得漂）：

| 符号 | 签名 | 定义 | 调用 |
|---|---|---|---|
| `bnep_encode_setup_req` | `int (uint8_t*, size_t, uint16_t, uint16_t)` | T1 | T5 |
| `bnep_encode_eth` | `int (uint8_t*, size_t, const uint8_t*, size_t, const uint8_t[6], const uint8_t[6], bool)` | T2 | T6 |
| `bnep_decode_eth` | `int (uint8_t*, size_t, const uint8_t*, size_t, const uint8_t[6], const uint8_t[6], const uint8_t**, size_t*)` | T3 | T6 |
| `bt_sal_pan_write_eth` | `bt_status_t (const bt_address_t*, const uint8_t*, uint16_t)` | T6 | T7 Step 2 |
| `bt_sal_pan_get_tx_mtu` | `uint16_t (const bt_address_t*)` | T6 | T7 Step 5 |
| `pan_on_eth_received` | `void (const bt_address_t*, const uint8_t*, uint16_t)` | T7 Step 3 | T6 |
| `pan_set_tap_mtu` | `int (const char*, uint16_t)` | T7 Step 4 | T7 Step 5 |
| `PAN_ETH_FRAME_MAX` | `1514` | SAL 侧字面量 / profile 侧 `CONFIG_NET_ETH_PKTSIZE` | T6/T7 |

`PAN_ETH_FRAME_MAX` 在两个文件里各定义一次是有意的：SAL 层不该依赖
NuttX 的网络配置宏（它要能在 host 上编译测试）。两边都是 1514，
**若 `CONFIG_NET_ETH_PKTSIZE` 被改动，T6 的字面量必须同步**——
defconfig `:194` 现值 1514，T8 不动它。

**已知会在实现期被推翻的假设**（写下来，好过实现时假装没看见）：

- T9 Step 3 假设 `bt_sal_pan_disconnect()` 可以从 DHCP 线程直接调。
  Step 3 里已给出核对命令和退路（改走 `do_in_service_loop`）。
- T4 的 bth4 挂钩点行号（`:975`、`:1274-1287`）来自静态阅读，
  实现时以实际代码结构为准，判据是"`priv->rxbuf[0..packet_len)`
  恰好是一个完整 H4 包"。
- `defconfig` 目前有一批**未提交的**改动（`BT_MAX_CONN=2`、
  `CONFIG_HCI_AUTO_REPLY_IN_JUST_WORK=y`、以及那行死的
  `CONFIG_BLUETOOTH_DEFAULT_COD=0x00020510`）。前两条是配对能通的关键，
  **保留**；第三条 T8 删掉。T8 的提交会把这些一起带上，写 commit message
  时注意别把它们说成是本次新加的。

**执行前检查清单**（在开工前跑一遍，各条都应为真）：

```bash
cd /home/aila/projects/vela_contest

# 1. 计划里没有待填标记（模式写成字符类，避免匹配到这条命令自己）
grep -nE 'PLACE[H]OLDER|TOD[O]|TB[D]|FIXM[E]' \
  docs_ble/superpowers/plans/2026-08-20-bluetooth-pan-bnep.md

# 2. frameworks 工作区干净（否则先弄清楚多出来的改动是什么）
git -C frameworks status --porcelain

# 3. 基线镜像在
md5sum out/pan_baseline/nuttx-prechange.bin   # 0dd6bd36...

# 4. 板子在
ls -l /dev/ttyACM0

# 5. host 编译器和 tshark 可用
gcc --version | head -1
bash docs_ble/tools/setup_tshark.sh 2>/dev/null | tail -2 || \
  echo "tshark 还没装，T4 Step 1 会装"
```

第 1 条应无输出，第 2 条应无输出。

---

## Execution Handoff

**推荐：subagent-driven（`superpowers:subagent-driven-development`）。**

理由是这份计划的任务性质分成三段，而中间那段最怕上下文被前面的细节挤掉：

- **T1–T4 适合并行派给子代理**。它们只碰新建文件（`bnep_codec.[ch]`、
  host 测试台、Python/Shell 工具），互不冲突，且每个都有"编译 + 跑测试全绿"
  这种客观完成判据。T1–T3 的代码块已经在开发机上真编译真跑过
  （见「计划内代码的预先验证记录」），子代理照抄即可，不需要再设计。
- **T5–T7 必须串行、且最好在同一个上下文里做完**。T6 删掉
  `bt_sal_pan_write` 而 T7 才改调用方，中间构建是断的；两者还共享
  `bt_sal_pan_write_eth` / `bt_sal_pan_get_tx_mtu` / `pan_on_eth_received`
  三个新签名。派给不同代理就会出现签名漂移，这正是自审表存在的原因。
- **T8–T10 需要人和硬件**。T10 全程要插板子、要在手机上点授权、
  要跑 2 小时。这段不该由代理自动推进，它需要一个人看着串口。

所以建议的执行形态：

| 阶段 | 方式 |
|---|---|
| T1、T2、T3 | 三个子代理并行（或一个子代理顺序做完，它们本来就是一个文件的三段） |
| T4 | 与上面并行，独立子代理（只碰 vendor/sifli + tools/） |
| T5 → T6 → T7 | 单个上下文串行，中途不换代理 |
| T8 | 单个子代理，重点是 Step 5 的 `.config` 回读验证 |
| T9 | 单个子代理，Step 3 开头先跑那条 grep 再决定实现形态 |
| T10 | **人工执行**，代理只负责写 `gate_c.py` / `gate_h.py` 和记录结果 |

**Gate A（T1–T3 全绿）是唯一的硬前置**：编解码器的单元测试不全绿，
后面所有上板动作都是在猜。

另一种选择是 `superpowers:executing-plans` 全程 inline 单上下文推进——
好处是签名绝不会漂、每一步的上下文都在；代价是 T1–T4 的并行度浪费掉，
而且 2892 行的计划配上十个任务的实现细节，单上下文大概率要压缩几轮。
如果倾向于稳过快，选这个也是合理的。


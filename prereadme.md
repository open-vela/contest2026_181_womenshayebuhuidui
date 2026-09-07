# PRE-README（草稿）

> **这不是最终提交版。** 组委会给的 `README.md` 暂时保留原样（里面是拉取工程、目录约定、
> 提交流程的官方说明，开发期还要照着用）。作品完成后按 `README.md` 第六节的模板把它
> 替换掉，届时本文件的内容并入正式 README 后可以删除。
>
> 维护者：Team 181（Sen70s）　最后更新：2026-08-21
> 当前状态：蓝牙联网主线已打通并回归通过；AI 对话与 UI 在迭代中

---

## 一、作品简介（草稿）

**在没有 WiFi 模组的穿戴级硬件上，做一个能联网、能对话的 AI 智能体手表。**

SF32LB52 这块板子只有蓝牙、没有 WiFi。常规做法是让手机 App 做一层私有协议代理，设备
端只能访问被代理商定好的接口。我们走的是另一条路：**让手表通过手机的「蓝牙网络共享」
拿到一个真正的 IP 地址**（BNEP/PAN over BR/EDR + DHCP），于是设备端拿到的是标准
socket 能力——ping、DNS、HTTP、任何云端 API 都能直接用，不需要手机侧配合做协议转发。

这条路在本项目开始时是不通的：openvela 的蓝牙框架里 PANU profile 从未被编入构建，
zblue 也没有 BNEP 实现。我们补齐了 BNEP 编解码与 PAN SAL，并在过程中修掉了三个平台
移植层的缺陷（详见第五节）。

选题方向：**AI 硬件产品创新**。

## 二、当前完成度

写清「做到哪了」，避免评委和自己都被过时的文档误导。

### 已完成并在真机回归通过

| 能力 | 状态 | 证据 |
|------|------|------|
| 蓝牙 PAN 上网（BNEP/PANU + DHCP） | ✅ 完成 | 冷启动无人干预即联网；网关 / 公网 IP / DNS 域名 / 1472 字节大包 ping 全部 0% 丢包 |
| 开机自动重连 | ✅ 完成 | bond 与 last_nap 持久化，上电自动连回手机并拿到 IP |
| 断链自动恢复 | ✅ 完成 | 手机侧断链后按退避自动重连并重新 DHCP |
| 长时间稳定性 | ✅ 完成 | 60.6 分钟 / 60 轮：0 assert、0 断链，堆用量首尾持平（无泄漏） |
| BR/EDR 配对（SSP） | ✅ 完成 | HyperOS 手机 JUST_WORKS 自动接受，link key 落盘 |
| LVGL 显示与 UI 页面 | ✅ 可用 | `CONFIG_AI_AGENT_LVGL_UI=y`，launcher / about 等页面；桌面显示 bt-pan 的 IP；桌宠页有历史对话窗口 |
| 触摸输入 | ✅ 完成 | FT6146；修掉了「武装边沿中断前没排空锁存 INT」导致触摸全程无响应的缺陷 |
| 端侧语言模型（velaAI TFLM） | ✅ 完成 | 与蓝牙合并进同一份固件；云端不可用时自动兑底，`ask <问题>` 即可 |
| 网络对时 | ✅ 完成 | 自实现 SNTP（`src/infra/time_sync.c`），联网后约 3 s 内对上；`timesync` 可手动重试 |
| 端侧语音命令识别 | 🔄 未并入 | 代码在 `app/ai_agent/speech/`，尚未并进当前全局固件 |

固件规模：flash 7,623,176 B / 9,792 KB（76.03%，镜像区上界由 /data 分区决定），
SRAM 426,912 B / 512 KB（81.43%）。

### 进行中 / 未完成

- 云端 LLM 尚未接入（板子上没配 API key）。这不阻塞对话：云端不可用时会自动兑底到
  端侧模型，实测能正确回答域内问题
- 端侧推理耗时 17～47 s。曾以为是 tensor arena 放在 PSRAM 导致，腾出 SRAM 搬回去之后
  实测没有变化——瓶颈在每 token 从 XIP flash 读 3 MB 权重、以及两段式生成约 96 次前向，
  待单独一轮优化
- 端侧语音命令识别（mic + VAD + 命令词）尚未并进全局固件
- 触摸屏还没法直接向 agent 提问，入口只有 NSH `ask`
- 吞吐量实测（镜像里没有 iperf / wget，`ping` 只能测延迟；见 `docs_ble/24` P1）
- 本机蓝牙地址是硬编码假值，多台设备会撞地址（见 `docs_ble/24` P1）
- 大赛要求的介绍文档、演示视频、可复用 Skill 尚未准备
- `time()` 返回的是本地时间而非 UTC（`CONFIG_LIBC_LOCALTIME` 未开，`TZ` 无效，改成 RTC 直接存本地时间）。接需要 UTC 时间戳签名的云 API 时要在调用点把 8 小时减回去

> XIP 下 NOR 复位窗口那个 P0（非正常掉电可能损坏 `/data`）已在 Round 15 修复，
> Round 16 用「写入过程中硬复位」跑了 5 轮验证通过，见 `docs_ble/24` 24.1。

## 三、目录结构

```
app/ai_agent/            AI 智能体应用：对话逻辑、LVGL UI、端侧语音命令、本地 LM 实验
  ├── speech/            命令词识别（TFLite）
  ├── model/             模型与 tokenizer 数据
  ├── ui/                LVGL 页面
  ├── audio/             音频编解码与采集
  └── tools/             构建期补丁与主机侧测试脚本
app/hello_app/           组委会示例应用（保留）
quickapp/hello_quickapp/ 组委会示例快应用（保留）
board/contest_board/     板级适配：defconfig（ai_agent 正式 / ai_agent_pandbg 调试）
docs/                    交付手册、开发记录（25 篇）、环境搭建
docs_ble/                蓝牙联网这条线的完整技术记录（见下）
logs/Sen70s/             AI Coding 日志（10 个会话）
```

`docs_ble/` 是本作品技术含量最集中的部分，建议评委从这里看：

| 文档 | 内容 |
|------|------|
| `00_README.md` | 索引 |
| `21_pan_breakthrough_authoritative.md` | **PAN 打通的权威复盘**：三个根因的完整证据链 + 验证结果 |
| `22_pan_engineering_guide.md` | **工程指南**：四条平台硬规则、症状→先查什么、验证口径 |
| `23_upstream_contributions.md` | **公共仓贡献索引**：按大赛规则走 PR 的三项平台修复 |
| `24_open_issues.md` | 遗留问题清单（含 P0） |
| `LOG.md` | 逐轮调试日志（Round 1–14） |
| `tools/` | 真机验证脚本（长稳、BNEP 抓包判定、NSH 执行器等） |

## 四、运行方式

### 1. 拉取工程

```bash
repo init -u https://github.com/open-vela/contest2026_181_womenshayebuhuidui \
  -b dev-ai-contest-2026 -m contest2026_181_womenshayebuhuidui.xml
repo sync -c -j8
```

> ⚠️ **本作品依赖三个公共仓的改动，目前以 PR 形式等待组委会合入**（见第五节）。
> 在 PR 合入前，按上面命令拉到的是未修复版本，PAN 上网跑不起来。复现完整功能需要
> 在三个公共仓分别 checkout 对应分支，各仓与分支见 `docs_ble/23_upstream_contributions.md`。

### 2. 编译

```bash
export PATH="$PWD/prebuilts/gcc/linux-x86_64/arm-none-eabi/bin:\
$PWD/prebuilts/build-tools/linux-x86_64/bin:\
$PWD/prebuilts/kconfig-frontends/bin:$PATH"

prebuilts/tools/cmake/bin/cmake -B out/nuttx_contest_board_ai_agent -GNinja \
    -DBOARD_CONFIG=../contest2026_181_womenshayebuhuidui/board/contest_board/configs/ai_agent \
    -DEXTRA_FLAGS='-Wno-cpp -Wno-deprecated-declarations -Wno-error -Wno-strict-prototypes -Wno-undef' \
    nuttx
cd out/nuttx_contest_board_ai_agent && ninja resetconfig && ninja -j8
```

产物：`out/nuttx_contest_board_ai_agent/nuttx.bin`（预期 SRAM 90.48%）。

> **不要跑 `savedefconfig`**：它会 copy_if_different 回源 defconfig 并抹掉全部注释。
> 改配置直接编辑 defconfig，然后 `ninja resetconfig`。

### 3. 烧录

板子的 RTS 接在电源控制上，需要在上电瞬间抢占 ROM bootloader 窗口，用脚本：

```bash
python3 contest2026_181_womenshayebuhuidui/docs_ble/tools/flash_rts.py \
    out/nuttx_contest_board_ai_agent/nuttx.bin /dev/ttyACM0
```

### 4. 验证蓝牙上网

手机侧打开「蓝牙网络共享 / 蓝牙网络分享」，然后给板子上电，**不需要任何控制台操作**：

```
[pan] state=adapter-on-auto-connect
[pan] BNEP setup OK, tx_mtu=1691
[pan] state=dhcp_ok dev=bt-pan ip=192.168.44.140
[netmgr] Active channel: bt-pan (primary)
```

之后在 NSH 里：

```bash
nsh> ifconfig bt-pan
nsh> ping -c 4 223.5.5.5
nsh> ping -c 3 www.baidu.com
```

> 用串口跑脚本时注意：**RTS 接板子电源，用 pyserial 默认参数 open 串口等于给板子断一次
> 电**。要连续观察请用 `docs_ble/tools/nsh2.py`，或全程只保持一个串口句柄。

首次配对：板子发起连接时 SSP 走 user_confirm 自动接受，手机侧确认即可，不需要输 PIN。

### 5. 和端侧模型对话

```bash
nsh> ask 打开客厅的灯
Sent to agent: 打开客厅的灯
[Agent]: 好的，已为您打开客厅灯
```

`ask` 是 NSH builtin，把问题投到 agent 的 inbound 队列。云端 LLM 不可用（没配 API key
就是这种情况）时自动兑底到端侧 velaAI TFLM 模型，当前推理耗时 16～49 s。

> agent 自身的 `vela>` CLI 里也有 `ask`，但那个线程优先级 30、NSH 是 100，两者抢同一个
> console，NSH 每次都赢——所以要用 builtin 这一个。

## 五、公共仓改动（重要）

按大赛规则，公共仓改动不放在本仓，而是 fork + PR 到 `dev-ai-contest-2026`。本作品有
三项平台底层修复走这条路，**PR 已提交、CI 全绿、GitHub 判定 MERGEABLE**：

| PR | 修的是什么 |
|----|-----------|
| [external_zblue#231](https://github.com/open-vela/external_zblue/pull/231) | 自定义 net_buf 池没注册进 `_net_buf_pool_list[]`，`pool_id()` 静默返回 0，buffer 拿到别的池的尺寸和存储区 |
| [vendor_sifli#29](https://github.com/open-vela/vendor_sifli/pull/29) | HCPU→LCPU 邮箱 ring 分块写入回退 LCPU 读指针导致 Hardware_Error；堆上界盖住邮箱 buffer |
| [frameworks_bluetooth#591](https://github.com/open-vela/frameworks_bluetooth/pull/591) | PAN/BNEP over BR/EDR 实现、TX 池尺寸、开机自动连接 |

细节与索引见 `docs_ble/23_upstream_contributions.md`，根因证据链见 `docs_ble/21`。

## 六、AI Coding 使用说明（草稿）

完整对话日志见 `logs/Sen70s/`（10 个会话）。这条线上 AI 协作最有价值的部分不是写代码，
而是**在一个没有调试器、只有串口日志的双核闭源平台上做根因定位**：

- **从症状反推机制**：`tailroom=249` 这个数字与任何配置都不匹配，是靠读 zblue NuttX
  port 的 `pool_id()` 实现，发现 `__ASSERT` 在 release 下被编掉、静默返回 0，才定位到
  「池没注册」这个根因——而不是继续调 MTU 配置。
- **区分因果**：`Unable to allocate buffer within timeout` 连刷看起来像 buffer 池不够，
  实际是 `Hardware_Error` 导致 LCPU 停回 NoCP、ACL 信用耗尽的**后果**。在 buffer 池上
  找原因会白跑很多轮。
- **把一次性调试沉淀成规则**：`docs_ble/22` 那四条硬规则和「症状→先查什么」对照表，
  是从 14 轮调试里提炼的，下一个人不用重走。
- **过程中的返工也记下来**：例如一度误判 `frameworks/connectivity/bluetooth` 不受 git
  跟踪、把代码抄成快照塞进团队仓，后来发现它本身就是独立 repo project（`openvela.xml:152`），
  已撤销并在文档里写清缘由。

> 正式 README 里这一节需要补充：Skill 的完整性与可复用性（大赛要求至少提供一个可复用
> Skill，目前尚未准备）。

## 七、写正式 README 前的检查清单

- [ ] 用本文件内容替换 `README.md`（组委会模板），按其第六节的结构
- [ ] 更新 `TASK_STATUS.md`——里面关于 PAN 的记录已过期（还写着「PAN vs SLIP 待定」
      「官方未实现 PAN」）
- [ ] 补介绍文档（.docx/.pdf/.pptx）与 5 分钟以内演示视频
- [ ] 准备至少一个可复用 Skill
- [ ] 确认三个公共仓 PR 的合入状态，据此更新第四节「运行方式」的前置说明
- [ ] 导出并提交最新的 AI Coding 日志到 `logs/`

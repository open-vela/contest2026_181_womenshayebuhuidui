# QEMU 环境 AI Agent 全链路验证记录

> 本文记录在 QEMU（aarch64 virt）上对 `ai_agent` 框架的完整验证：网络连通、LLM 对话、cron 定时任务、REST API 与 skill 在线安装。验证日期 2026-08-11，日志归档在 [`../../logs/qemu/`](../../logs/qemu/)。

## 1. 验证环境

| 项 | 值 |
|---|---|
| 目标机 | QEMU `virt` (aarch64, cortex-a53, gic-version=3) |
| 内核 | NuttX (openvela)，配置 `qemu-aiagent` |
| 网卡 | `virtio-net-pci`（QEMU slirp user 网络，网段 10.0.2.0/24） |
| LLM | 阶跃星辰 `https://api.stepfun.com/v1`，模型 `step-3.7-flash`（OpenAI 兼容） |
| 数据目录 | `/data/ai_agent`（QEMU 上自动挂载 tmpfs） |

关键 QEMU 启动参数（脚本见 [`../../logs/qemu_net_test.sh`](../../logs/qemu_net_test.sh)）：

```bash
-netdev user,id=net0,hostfwd=tcp::28789-:28789 \
-device virtio-net-pci,netdev=net0
```

## 2. 构建与启动要点

- 配置源：`vendor/openvela/boards/vela/configs/qemu-aiagent/defconfig`
- 为使 virtio-net 工作，defconfig 新增：
  - `CONFIG_DRIVERS_VIRTIO_PCI=y`
  - `CONFIG_DRIVERS_VIRTIO_PCI_POLLING_PERIOD=1000`（QEMU virt 无 MSI 中断控制器，必须用轮询模式；否则 `virtio_pci_probe: Failed to allocate MSI, ret=-138`）
- 修改 defconfig 后必须删除 `.config/.config.prev/.../include/nuttx/config.h` 再重新 configure，否则 `config.h` 不重新生成（mkconfig 的 `.config.prev` 比较机制）。

## 3. 网络连通验证 [已验证]

日志：`qemu_aiagent_session.log`

```text
eth0: HWaddr 00:e0:de:ad:be:ef  inet addr:10.0.2.15
PING 10.0.2.2 56 bytes of data, 2 packets transmitted, 2 received, 0% packet loss
[netmgr] Network connected: 10.0.2.15
[netmgr] State: DISCONNECTED -> CONNECTED
net_status: IP: 10.0.2.15, State: CONNECTED
DNS: nameserver 223.5.5.5 / 8.8.8.8（可 set_dns 覆盖）
```

## 4. LLM 对话 ask 验证 [已验证]

日志：`qemu_aiagent_session.log`

```text
vela> set_llm https://api.stepfun.com/v1 step-3.7-flash 1k8F****
LLM backend: api.stepfun.com:443/v1/chat/completions (model: step-3.7-flash) [router slot 0]

vela> ask 你好，请介绍一下你自己
[agent] LLM resp: text=458, tool_use=0, calls=0
[Agent]: 你好！我是 AI Agent，运行在 Vela 嵌入式设备（NuttX 系统）上的个人助手。
```

- TLS 链路独立验证：`net_diag http api.stepfun.com` → `Status: 404, Latency: 240ms`（握手成功，404 为根路径无内容）
- 首次回复含“让我查一下...”的流式中间态，随后输出最终回答。

## 5. cron 主动任务定时触发验证 [已验证]

日志：`qemu_cron_test.log`

通过 `ask` 让 LLM 调用 `cron_add` 工具创建任务（验证 LLM 工具调用 + cron 调度 + 消息推送全链路）：

```text
Tool call: cron_add args={"name": "qem-cron-test", "schedule_type": "every",
                          "interval_s": 20, "message": "cron-ok",
                          "channel": "system", "chat_id": "cron"}
[tool_cron] cron_add: OK: Added recurring job 'qem-cron-test' (id=4ffc65e5),
            runs every 20 seconds.
[Agent]: 已创建定时任务：消息 "cron-ok" 将每20秒发送到 system 频道
```

每 20 秒精确触发 4 次，消息推送成功：

```text
[85.43s] [cron] Cron job firing: qem-cron-test (4ffc65e5)  -> [Agent]: cron-ok
[105.43s] [cron] Cron job firing: qem-cron-test (4ffc65e5) -> [Agent]: cron-ok
[125.43s] [cron] Cron job firing: qem-cron-test (4ffc65e5) -> [Agent]: cron-ok
[145.43s] [cron] Cron job firing: qem-cron-test (4ffc65e5) -> [Agent]: cron-ok
```

`cron_list` 工具验证：`[tool_cron] cron_list: 1 jobs`，LLM 回复“当前定时任务列表（共 1 个）”。

补充事实：cron 任务文件为 `/data/ai_agent/cron.json`（JSON `{"jobs":[...]}`，支持 `every`/`at` 两种 kind），检查间隔 10 秒。

## 6. REST API（coapp 接口）验证 [已验证]

日志：`qemu_rest_test.log`。REST 服务内嵌于 WebSocket server，监听 28789；经 `-hostfwd=tcp::28789-:28789` 从宿主访问。

| 接口 | 结果 |
|---|---|
| `GET /api/config` | [已验证] 返回 api_key/model/llm_host/llm_path/proxy 等全量配置 JSON |
| `POST /api/skills` | [已验证] 上传 skill 返回 `{"ok":true}`，写盘 + 热加载 |
| `GET /api/skills` | [已验证] 返回 skills 数组（含名称/描述/内容/大小/mtime） |
| `DELETE /api/skills/{name}` | [已验证] 删除成功返回 `{"ok":true}` |
| `GET /api/logs` | [已验证] 返回日志 JSON 数组 |

```text
GET  /api/config   -> {"api_key":"","model":"",...}
POST /api/skills   -> {"ok":true}
GET  /api/skills   -> 含 "rest-test-skill"（description: installed via REST API）
DELETE /api/skills/rest-test-skill -> {"ok":true}
```

skill 内容使用 markdown front-matter 格式（`---` + `name`/`description`），上传后 `skill_loader_refresh()` 热加载生效。

## 7. skill 在线安装验证 [部分验证，发现 CLI 缺陷]

- `net_diag http raw.githubusercontent.com` → [已验证] TLSv1.2 握手成功（`TLS-ECDHE-RSA-WITH-CHACHA20-POLY1305-SHA256`），HTTP 301 正常重定向。
- `install_skill` CLI 命令 → [未通过] 实测报 `Download failed: HTTP -1`（`VELA_TLS_ERR_CONNECT`，DNS 解析失败）。

根因 [已验证/代码级]：`install_skill <name> <url>` 的 help 声称传 URL，但实现直接把 `url` 当作 host 传给 `vela_https_get()`（[nsh_commands.c](../../../../packages/ai_agent/src/channels/nsh_commands.c) 仅校验 `https://` 前缀后传入）。`vela_https_get()` 不做 URL 解析（[vela_tls.c](../../../../packages/ai_agent/src/infra/vela_tls.c) `tls_ctx_connect(host, port)` 直接 getaddrinfo），导致 `https://raw...` 整体被当作 host 解析失败。

建议（[排障备选]）：`cmd_install_skill` 应先用 `strstr(url, "://")` 拆出 host 与 path 再调用 `vela_https_get(host, "443", path, ...)`。REST `POST /api/skills` 已是可用的在线安装通道，可先用于规避。

## 8. 遗留问题与备注

1. NSH 串口输入行长度受限：一次性发送 >80 字符的命令会被截断（如 `echo '...'` 写文件），`>>` 重定向也不支持。长输入请分块发送或使用 `vela>` CLI。
2. QEMU 重启后 `/data`（tmpfs）内容丢失，LLM key 需每次 `set_llm` 重新配置；真机上为持久化存储无此问题。
3. 参考脚本：`logs/qemu_net_test.sh`（qem03）、`logs/qemu_cron_test.sh`（qem04）、`logs/qemu_rest_test.sh`（qem05）、`logs/qemu_diag.sh`（诊断）。

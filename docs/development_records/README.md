# 开发过程完整记录

## 1. 文档目的

本目录整理 `contest2026_181_womenshayebuhuidui` 从 AI Coding 日志、设备连接、固件构建、烧录、NuttX 启动、NSH 交互，到 AI Agent/LVGL 显示验证的完整过程。它是过程记录和故障索引，不替代 [`../setup_guide.md`](../setup_guide.md) 的基础环境指南。

原始 AI 对话仍保存在 [`../../logs/Sen70s/`](../../logs/Sen70s/)。本目录只提炼可复现事实，不复制 JSONL。

## 2. 状态标记

- **[已验证]**：有命令输出、构建产物或用户现场反馈支持。
- **[部分验证]**：流程的一部分成功，完整链路尚未完成。
- **[待验证]**：需要再次连接开发板或现场观察。
- **[推断]**：根据现象作出的可能解释，不代表唯一根因。
- **[排障备选]**：建议的下一步检查，尚未证明必要。

## 3. 当前状态

| 阶段 | 状态 | 结论 |
|---|---|---|
| AI 对话日志导出 | [已验证] | 已导出到 `logs/Sen70s`，manifest 中会话健康状态为 `ok` |
| USB/串口连接 | [部分验证] | 重新枚举后成功使用 `/dev/ttyACM0` |
| 固件构建 | [已验证] | 当前配置启用 `CONFIG_EXAMPLES_AI_AGENT=y` |
| 固件烧录 | [已验证] | `SF32LB52` 镜像写入 `0x12010000` 成功 |
| NuttX 启动 | [已验证] | 启动信息和 NSH 已出现 |
| NSH 输入 | [已验证] | picocom 必须使用 `--omap crlf` 才能正常回车和执行命令 |
| `ai_agent`/`lvgldemo` 编入固件 | [已验证] | `builtin_list.h` 和 `nuttx.map` 均有证据 |
| 屏幕实际点亮 | [待验证] | 尚未取得现场画面或 `lvgldemo`/`ai_agent` 的显示成功证据 |
| QEMU 全链路验证（网络/LLM/cron/REST） | [已验证] | 见 [`07_qemu_aiagent_llm_validation.md`](07_qemu_aiagent_llm_validation.md)，QEMU 上 LLM 对话、cron 定时触发、REST API 均通过 |
| 真机 BLE SPP+TUN 设备端链路 | [已验证] | 见 [`08_ble_spp_tun_real_device.md`](08_ble_spp_tun_real_device.md)，TUN/adapter/SPP server 就绪；**但 2026-08-12 发现 LCPU 固件不支持 BREDR，SPP 通道作废，转向 BLE GATT** |

## 4. 快速运行

如果只需要从设备检测一直执行到应用运行，直接阅读 [`00_quick_start_device_to_app.md`](00_quick_start_device_to_app.md)。

## 5. 从连接到显示的推荐顺序

1. 连接并重新确认 USB 设备节点：见 [`02_device_usb_serial.md`](02_device_usb_serial.md)。
2. 编译、烧录并确认 NuttX/NSH：见 [`03_build_flash_nuttx.md`](03_build_flash_nuttx.md)。
3. 在 NSH 中先运行 `lvgldemo`，再运行 `ai_agent -h`、`ai_agent -q hello`：见 [`04_ai_agent_lvgl_display.md`](04_ai_agent_lvgl_display.md)。
4. 按故障表处理异常：见 [`05_troubleshooting.md`](05_troubleshooting.md)。
5. 只有现场确认屏幕非全黑、内容正确且重启可复现后，才能将显示状态改为“已验证”。

## 6. 记录目录

- [`00_quick_start_device_to_app.md`](00_quick_start_device_to_app.md)：从设备连接、构建烧录到运行项目的简明流程。
- [`01_project_and_ai_logs.md`](01_project_and_ai_logs.md)：项目架构、Git 注意事项和 AI 日志采集。
- [`02_device_usb_serial.md`](02_device_usb_serial.md)：USB、串口、RTS 和 NSH 终端。
- [`03_build_flash_nuttx.md`](03_build_flash_nuttx.md)：配置、构建、烧录和启动。
- [`04_ai_agent_lvgl_display.md`](04_ai_agent_lvgl_display.md)：AI Agent、LVGL 和屏幕验收。
- [`05_troubleshooting.md`](05_troubleshooting.md)：错误原因、处理和证据。
- [`06_pet_ui_design.md`](06_pet_ui_design.md)：宠物 UI 设计。
- [`07_qemu_aiagent_llm_validation.md`](07_qemu_aiagent_llm_validation.md)：QEMU 环境 AI Agent 全链路验证（网络/LLM 对话/cron/REST API/skill）。
- [`08_ble_spp_tun_real_device.md`](08_ble_spp_tun_real_device.md)：SF32LB52 真机蓝牙 SPP+TUN 代理链路验证（架构、配置、六层根因排障）。
- [`09_app_spp_tun_integration.md`](09_app_spp_tun_integration.md)：手机 App 蓝牙代理联网规划——**SPP 方案否决（LCPU 固件无 BREDR）→ BLE GATT NUS 透传转向**（决策记录、新架构、里程碑）。
- [`10_pan_route_reanalysis.md`](10_pan_route_reanalysis.md)：PAN 路线重新分析——**BREDR 误判纠正**（芯片支持双模、官方栈为 ZBLUE）、PAN 官方未实现证据（dev 分支核实）、**冗余代码清单**（~2900/3400 行）、未记录事件补录。

# 故障原因、处理与验证

每条记录区分“已确认事实”和“可能原因”。没有证据支持时，不把推断写成唯一根因。

## 1. AI 对话日志看似缺失

- **状态**：[已验证]
- **现象**：启动 AI 工具后，团队仓库中看不到对应日志。
- **原因**：采集器自动导出目标原先是工作区根目录，而不是团队仓库的 `logs/Sen70s`。
- **处理**：设置 `CONTEST_REPO_DIR` 指向团队仓库；保留 `TEAM_ID` 和 `GITHUB_LOGIN`。
- **验证**：`logs/Sen70s/manifest.json` 已记录 4 个会话，健康状态均为 `ok`。
- **来源**：[`01_project_and_ai_logs.md`](01_project_and_ai_logs.md)。

## 2. 单个 transcript unreadable

- **状态**：[部分验证]
- **现象**：collector staging 的错误目录出现 `transcript_unreadable`。
- **结论**：这是单个会话/转录文件异常，不能推断整个采集器停止工作。
- **处理**：检查 manifest 中其他会话；其他记录正常导出后继续使用。
- **后续**：若同类错误持续出现，保存错误文件和时间戳，再检查采集器版本及输入转录格式。

## 3. `dmesg` 无法读取

- **状态**：[已验证]
- **现象**：

  ```text
  dmesg: 读取内核缓冲区失败: 不允许的操作
  ```

- **原因**：`kernel.dmesg_restrict=1` 限制普通用户读取 ring buffer。
- **处理**：使用：

  ```bash
  journalctl -k -n 100 --no-pager
  ```

- **结果**：成功通过内核日志继续排查 USB。

## 4. USB `error -71` 和枚举失败

- **状态**：[部分验证]
- **现象**：USB 设备反复无法稳定枚举，内核日志出现 `error -71`。
- **已确认事实**：设备后来重新连接/重启后出现 `1a86:55d3`，并创建 CDC ACM 串口。
- **可能原因**：[推断] USB 协议枚举异常，可能涉及线材、供电、信号质量、主机控制器或 RTS/板端复位时序；现有证据不能证明唯一根因。
- **处理**：重新连接和重启开发板，等待重新枚举，然后重新执行 USB、内核日志和串口节点检测。
- **验证结果**：最终获得 `/dev/ttyACM0`，并用其成功烧录。
- **来源**：[`02_device_usb_serial.md`](02_device_usb_serial.md)。

## 5. `/dev/serial/by-id` 路径不存在

- **状态**：[已验证]
- **现象**：原先使用的 by-id 路径在设备断开/重新枚举后不存在。
- **原因**：USB 设备节点和别名由当前枚举结果生成，旧路径不能永久复用。
- **处理**：重新检查 `/dev/ttyACM*`，确认当前设备后使用 `/dev/ttyACM0`。
- **结果**：`/dev/ttyACM0` 成功用于烧录和串口连接。
- **注意**：下次连接仍需动态确认，不能假设一定是 ttyACM0。

## 6. 烧录脚本不接受 `--connect-attempts`

- **状态**：[已验证]
- **现象**：向 `build_and_flash.sh` 传入 `--connect-attempts 20` 后，脚本提示未知参数。
- **原因**：这是底层 `sftool` 支持的参数，但当前包装脚本没有暴露该 CLI 选项。
- **处理**：移除脚本不支持的参数，使用脚本自身支持的选项；若直接调用 `sftool`，再依据 `sftool --help` 使用底层参数。
- **教训**：先查看包装脚本的参数解析，不能把底层工具参数直接假定为脚本参数。
- **来源**：[`03_build_flash_nuttx.md`](03_build_flash_nuttx.md)。

## 7. sftool 连接或 stub 超时

- **状态**：[排障备选]
- **常见现象**：`Failed to connect to the chip`、`Failed to download stub: Timeout` 或烧录挂起。
- **优先检查**：
  1. picocom/Python 是否仍占用串口。
  2. 设备节点是否因 USB 重新枚举已变化。
  3. RTS 是否按板卡要求断电、等待、上电。
  4. bootloader 是否有足够启动时间。
  5. 必要时拔插 USB 后重新检测。
- **验证标准**：出现 `Connected success!`、`Download stub success!`、写入完成并自动重启。

## 8. NSH 回车和命令无效

- **状态**：[已验证]
- **现象**：picocom 能连接并看到启动信息，但按 Enter 或输入命令没有正常执行。
- **原因**：终端发送的换行格式不符合当前 NSH 交互预期，缺少 CR/LF 映射。
- **处理**：使用：

  ```bash
  picocom -b 1000000 --noreset --lower-rts --lower-dtr --omap crlf /dev/ttyACM0
  ```

- **验证结果**：用户确认“可以了”，回车和命令恢复正常。
- **注意**：`--omap crlf` 应保留在本项目的工作命令中。

## 9. LSM6DS3 初始化错误和 XIP 警告

- **状态**：[已验证为非阻塞启动警告]
- **现象**：出现 LSM6DS3 `-5`、设备未找到，以及 XIP bringup 阶段跳过 NOR 写擦初始化的警告。
- **影响**：当前记录中 NuttX 仍成功启动并出现 NSH，因此这些信息没有阻止本次启动。
- **处理原则**：若项目不依赖该传感器，可先记录并分离；若后续需要传感器功能，再单独检查 I2C 地址、硬件连接和驱动配置。不要将其直接当作黑屏根因。

## 10. 黑屏或屏幕没有亮

- **状态**：[待验证]
- **现象**：NuttX 已启动，但用户观察到屏幕仍黑；尚未完成 `lvgldemo`、`ai_agent` 和设备节点的现场闭环。
- **第一步**：在 NSH 执行：

  ```text
  ls /dev
  ai_agent -h
  lvgldemo
  ai_agent -q hello
  ```

- **分层判断**：
  - 命令不存在：确认开发板是否烧录了包含 `CONFIG_EXAMPLES_AI_AGENT=y` 的最新镜像。
  - `/dev/lcd0` 或 `/dev/input0` 缺失：检查板级 LCD/input 初始化。
  - `lvgldemo` 和 `ai_agent` 都失败：优先检查基础显示链路。
  - `lvgldemo` 成功但 `ai_agent` 失败：检查 AI Agent 的 LVGL 初始化和设备路径。
  - 串口初始化成功但屏幕全黑：检查背光、CO5300 时序和实际 framebuffer 路径。
- **当前不能确认**：不能仅凭配置、builtin 列表或烧录成功断言屏幕已点亮。
- **验收标准**：见 [`04_ai_agent_lvgl_display.md`](04_ai_agent_lvgl_display.md) 第 6 节。

## 11. README 与源码能力不一致

- **状态**：[已验证存在差异]
- **现象**：`app/ai_agent/README.md` 描述 Bailian SDK、云端 ASR/LLM/TTS，以及 `-s`、`-i` 等用法；当前 `ai_agent_main.c` 主要是本地静态关键词匹配，getopt 为 `"hq:n"`。
- **处理**：运行验证和正式能力说明以当前源码、构建配置和 NSH 实际输出为准；把云端能力标为待实现/待验证，不要在演示材料中夸大。
- **后续**：若要实现 README 中的云端能力，应另立功能开发任务，补充网络、凭据安全、音频驱动和失败重连测试。

## 13. NOR 写入失败：littlefs 持久化 verify mismatch

- **状态**：已修复并验证
- **现象**：将 /data 从 tmpfs 改为挂载 littlefs（NOR /dev/config0）后，启动日志出现 `ERROR: verify mismatch at 0x129a0000: flash=b4b70420a4b70420 expect=01000000f00ffff7`、`ERROR: NOR write verify failed`，格式化失败回退 tmpfs。
- **阶段**：启动（挂载）/ 驱动层
- **原始错误**：
  ```
  INFO: formatting /dev/config0 as littlefs...
  ERROR: verify mismatch at 0x129a0000: flash=b4b70420a4b70420 expect=01000000f00ffff7
  ERROR: NOR write verify failed: addr=0x129a0000 size=256
  ```
- **已确认事实**：
  - 擦除成功（verify_erased 通过，`dd` 读 /dev/config0 全 0xFF）
  - 写页函数返回成功但 flash 内容不变（仍 0xFF）
  - 写后 verify 读回的数据（b4b7...）与真实 flash 内容（0xFF）不一致 → verify 读路径被写操作污染
  - `hflash->Mode == HAL_FLASH_QMODE` 时页写走 QPP(0x32) 四线命令，需芯片 QE 位；vendor 的 flash_table.c NOR_TYPE0 无 WRSR2，从未设置 QE
  - 擦除命令（SE 0x20/BE64 0xD8）是单线命令，与 QE 无关，所以擦除正常
  - preinit_runtime 手动配置的 DMA 页写路径数据未真正发出（写成功但 flash 保持 0xFF）
- **根因**：双重缺陷：① 页写默认 quad 命令但芯片 QE 位未设置；② DMA 页写路径失效且污染控制器状态。
- **处理步骤**（vendor/sifli/chips/sf32lb52/sf32lb_flash.c）：
  1. 新增 `sf32lb_flash_write_page_single()`：写页前临时 `hflash->Mode = HAL_FLASH_NOR_MODE` 强制单线 PP(0x02)，写后恢复 QMODE
  2. preinit_runtime 中禁用 DMA（hflash->dma 保持 NULL），HAL_QSPIEX_WRITE_PAGE 走 FIFO 手动路径
  3. 写页加 `up_irq_save()` 中断保护（与擦除路径一致）
- **验证结果**：
  ```
  INFO: NOR MTD registered at /dev/config0 (offset=2464 blocks=1024)
  INFO: formatting /dev/config0 as littlefs...   （首次）
  INFO: littlefs mounted on /data (persistent)    （重启后直接挂载）
  ```
  - `mount` 显示 `/data type littlefs`，`df -h` 显示 4M 容量
  - 写文件 → wdog 硬复位重启 → 文件保留；框架 MEMORY.md 跨重启保留
- **尚未确认**：DMA 页写失效的更深层原因（DMA 源地址/通道配置与 HAL 预期不符）
- **相关文档**：`sf32lb_flash.c`、`flash_table.c`（NOR_TYPE0 注释 NO CMD_WRSR2）、`bf0_hal_mpi_ex.c`（HAL_QSPIEX_WRITE_PAGE 命令选择逻辑）

## 12. 故障记录模板

新增故障时按以下格式：

```markdown
### 故障名称

- 状态：
- 现象：
- 阶段：连接 / 构建 / 烧录 / 启动 / NSH / 显示
- 原始错误：
- 已确认事实：
- 根因或可能原因：
- 处理步骤：
- 验证结果：
- 尚未确认：
- 相关文档和日志：
```

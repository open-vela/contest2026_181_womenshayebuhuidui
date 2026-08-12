# 14. BLE legacy 广播修复编译验证报告

日期：2026-08-13
状态：✅ 编译验证通过 + 真机验证通过（legacy 广播空口确认）

> 验证对象：ble_gatt.c 蓝牙修复 4 处修改 + defconfig 蓝牙配置修改（详见 13 号记录/方案文档），构建方式为 lunch + m 命令（Makefile 流程）。

## 1. 清理步骤

```bash
# out 目录改为备份（可回退），而非直接删除
mv out/openvela_contest2026_181_board_ai_agent \
   out/openvela_contest2026_181_board_ai_agent.bak_20260813
```

> 依据：CMake 仅在 `.config` 不存在或 defconfig 路径变化时重新配置，不比较 defconfig 内容——不清理则配置修改不生效。

## 2. 构建命令

```bash
cd /home/aila/projects/vela_contest
export PATH="$(pwd)/prebuilts/gcc/linux-x86_64/arm-none-eabi/bin:$(pwd)/prebuilts/kconfig-frontends/bin:$(pwd)/prebuilts/build-tools/linux-x86_64/bin:$PATH"
source build/envsetup.sh
export VELA_EXTRA_FLAGS="-Wno-cpp -Wno-deprecated-declarations -Wno-error -Wno-strict-prototypes -Wno-undef"
lunch vendor/openvela/boards/contest2026_181_board/configs/ai_agent >/dev/null 2>&1 && m -j$(nproc)
```

> lunch 与 m 必须合并执行（lunch 环境变量只对当前 shell 生效），lunch 输出重定向。

## 3. 有效 .config 关键配置项验证（新 vs 旧）

| 配置项 | 修改前（备份 .config） | 修改后（新 .config） | 期望 | 结果 |
|---|---|---|---|---|
| `CONFIG_BT_GATT_CLIENT` | `=y` | `# ... is not set` | not set | ✅ |
| `CONFIG_BLUETOOTH_GATT_CLIENT` | （未查） | `# ... is not set` | not set | ✅ |
| `CONFIG_BT_EXT_ADV` | `=y` | `=y` | =y | ✅ |
| `CONFIG_BT_EXT_ADV_LEGACY_SUPPORT` | `# ... is not set` | `=y` | =y | ✅ |
| `CONFIG_BT_EXT_ADV_MAX_ADV_SET` | `=1` | `=2` | =2 | ✅ |
| `CONFIG_BT_GATT_AUTO_UPDATE_MTU` | `# ... is not set` | **不存在** | 不存在（死配置） | ✅ |

关键结论：
1. **GATT_CLIENT 关闭真正落地**：`CONFIG_BT_GATT_CLIENT=y` 不再出现在有效 .config——此前 defconfig L252 的关闭行带括号注释后缀被 Kconfig 当纯注释忽略，是 GATT_CLIENT 实际开启的根因，现已修复。
2. **LEGACY_SUPPORT 生效**：`CONFIG_BT_EXT_ADV_LEGACY_SUPPORT=y` 已进入有效 .config——zblue 将读控制器特性位（vendor 模拟层全 0）走 legacy 广播命令。
3. **死配置验证通过**：`CONFIG_BT_GATT_AUTO_UPDATE_MTU` 因 `depends on BT_GATT_CLIENT`（已关闭）被 Kconfig 自动清除，从 .config 中消失，与审查结论一致。

## 4. 编译结果

| 项 | 值 |
|---|---|
| 结果 | ✅ 通过（EXIT_CODE=0，`#### build completed successfully ####`） |
| 耗时 | 60 秒（01:00，ccache 加速） |
| 产物 | `out/openvela_contest2026_181_board_ai_agent/nuttx.bin` |
| ble_gatt.c 编译 | ✅ 无错误无警告（`ble_gatt.c.o` 正常生成） |

编译警告清单（均为预存/无害）：
- `adv.c:1118: warning: label 'set_adv_state' defined but not used`——zblue 预存标签，开启 LEGACY_SUPPORT 后 ext 分支不再引用，无害
- `first_link/second_link/final_nuttx has a LOAD segment with RWX permissions`——链接器既有警告
- `cc1plus: '-Wno-strict-prototypes' is valid for C/ObjC but not for C++`——既有
- `Kconfig: DEFAULT_TASK_STACKSIZE set more ...`——既有 Kconfig 提示
- `.note.gnu.build-id section discarded`——既有

## 5. 固件尺寸与内存

| 区域 | 占用 | 总量 | 占比 |
|---|---|---|---|
| flash | 4,749,504 B | 16 MB | 28.31% |
| sram | 439,988 B | 512 KB | 83.92% |
| psram | 0 B | 8 MB | 0.00% |

ELF 尺寸：text 4,624,544 B / data 124,960 B / bss 296,532 B

> 注：备份旧固件（4,048,172 B）与本固件功能集不同（旧固件早于 8-12 的 UI/字体等改动），size 直接对比无意义；GATT_CLIENT 关闭的释放已体现在配置层（150KB 静态池不再编译），sram 83.92% 留有约 72KB 安全余量。

## 6. 回退保障（本地 git 检查点）

| 仓库 | 检查点 commit | 内容 |
|---|---|---|
| `packages/ai_agent` | `d0e2d26` | ble_gatt.c 修复（adv_type legacy、悬垂 handle、on_connected 清理、MTU 修复） |
| `contest2026_181_womenshayebuhuidui` | `7a94021` | defconfig 修复（GATT_CLIENT 关闭 + LEGACY_SUPPORT 等） |
| `out/` 目录 | `out/openvela_contest2026_181_board_ai_agent.bak_20260813/` | 修改前完整构建产物 |

回退方法：
- 源码：`git reset --hard <检查点>` 或 `git checkout <检查点> -- <文件>`
- 构建产物：`rm -rf out/openvela_contest2026_181_board_ai_agent && mv out/openvela_contest2026_181_board_ai_agent.bak_20260813 out/openvela_contest2026_181_board_ai_agent`

## 7. 下一步（真机验证）

1. 烧录：`./flash_only.sh --port /dev/ttyACM0 -i out/openvela_contest2026_181_board_ai_agent/nuttx.bin`（或 `./build_and_flash.sh flash ... --before no_reset`）
2. HCI trace 验收：出现 `LE_SET_ADV_PARAMS`(0x2006) / `LE_SET_ADV_DATA`(0x2008) / `LE_SET_SCAN_RSP_DATA`(0x2009) / `LE_SET_ADV_ENABLE`(0x200A)，**不再出现** 0x2036/0x2037/0x2039
3. 手机双通道（BLE + 经典蓝牙）扫描：发现 "Agent-Watch" = 修复成功的最终判据
4. 连接测试：官方 App com.agent.coapp 连接 → NUS 服务发现 → 写 6e400002 / 收 6e400003 通知
5. 稳定性：连接保持 5-10 分钟无 hardfault

## 8. 真机验证结果（2026-08-13）

### 8.1 验证过程

1. 首次烧录后板子串口无输出 → 断电重插两次后正常启动（下载模式残留）。
2. 首版修复固件（GATT_CLIENT 关闭 + LVGL 开）→ ai_agent 启动至 voice/LVGL 阶段出现 `sched_dumpstack`（线程 15/28 pthread_start+0x7f）→ 系统挂死。
3. 隔离构建 1（关闭 AI_AGENT_LVGL_UI）→ 仍崩溃（voice_asr 线程创建后 dumpstack）。
4. **二分实验（恢复 CONFIG_BT_GATT_CLIENT=y + 新增 CONFIG_BLUETOOTH_TOOLS=y）→ 系统完全稳定**，无 dumpstack、无挂死。
5. `ble_gatt_test init` → `OK (0)`，`[ble_gatt] Advertising started`。

### 8.2 二分实验结论

| 变量 | 结果 |
|---|---|
| GATT_CLIENT=y（恢复） | 系统稳定 → **GATT_CLIENT 关闭是崩溃触发器**（150KB 布局变化暴露预存越界写，布局敏感堆损坏） |
| 蓝牙 legacy 修复（保留） | 生效（0x200A 命令真实下发） |
| 无 LVGL | 临时保持关闭 |

取舍：GATT_CLIENT 保持开启（放弃 150KB RAM 优化，稳定性优先）；预存越界写源待后续定位。

### 8.3 HCI trace 实机证据（legacy 命令真实下发）

```text
sf32lb52 bth4 tx: type=0 len=36 h4=01 08 20 20 10   ← 0x2008 LE_SET_ADV_DATA
sf32lb52 bth4 tx: type=0 len=36 h4=01 09 20 20 16   ← 0x2009 LE_SET_SCAN_RSP_DATA
sf32lb52 bth4 tx: type=0 len=5  h4=01 0a 20 01 01   ← 0x200A LE_SET_ADV_ENABLE
（无 0x2036/0x2037/0x2039 ext adv 命令）
```

### 8.4 手机 nRF Connect 扫描结果（最终判据）✅

```text
Agent-Watch          CD:AB:78:56:34:12    NOT BONDED    -43 dBm
Device type: CLASSIC and LE
Advertising type: Legacy                       ← 决定性证据
Flags: LE General Discoverable, BR/EDR Not Supported
Complete Local Name: Agent-Watch
Complete list of 128-bit Service UUIDs: 6e400001-b5a3-f393-e0a9-e50e24dcca9e
Appearance: [193] Watch: Sports Watch
```

手机系统蓝牙设置同样可见。**修复完全成功**：此前 ext adv 时代（命令成功但空口无信号）→ 现在 legacy 广播空口真实发射，NUS 服务 UUID、广播名、appearance 全部正确。

### 8.5 待办

1. 官方 App com.agent.coapp 连接测试（BLE 连接 → NUS 发现 → 写 6e400002/收 6e400003）
2. 连接后 5-10 分钟稳定性观察（预存堆损坏残余风险）
3. LVGL_UI 恢复验证（GATT_CLIENT=y 布局下是否稳定）
4. 预存堆越界写源定位（redzone/二分）

# velaAI 端侧模型接入 ai_agent (TFLite Micro) — 开发记录

> 日期: 2026-08-16 | 状态: 代码完成, 固件编译通过 (含 agent 模式增强), 真机待烧录
> 关联: `app/ai_agent/LOCAL_LM.md` (技术细节), `TASK_STATUS.md`

## 一、目标

按 openvela 大赛「TFLite Micro 端侧 AI」要求, 将
[`velaAI`](../../../../../velaAI) 项目训练的 LSTM 语言模型
(V3, vocab 5000, LSTM 256, 16x8 量化, val acc 0.8705) 部署进
`app/ai_agent`, 并让桌宠 (pet page) 直接使用该模型生成回复 —
不依赖云端, 完全在 SF32LB52 MCU 上运行。

## 二、改动清单

### 新增 (contest 仓 app/ai_agent/)

| 文件 | 说明 |
|------|------|
| `model/velaai_lm_v3_16x8.tflite` | V3 模型二进制 (3.0 MB) |
| `model/model_data.h` | 模型 C 数组 (RODATA, 16 字节对齐, XIP) |
| `model/tokenizer_v3_data.h` | SP 词表表 (5000 pieces, FNV-1a 哈希) |
| `ai_lm.cxx` / `ai_lm.h` | TFLM 推理: 初始化 + 自回归生成 + `<bot>` 回复提取 |
| `tokenizer.c` / `tokenizer.h` | 简化 SentencePiece 分词 (最长匹配, 与 SP 逐位一致) |
| `tools/gen_local_lm_tables.py` | 表生成脚本 (tflite→model_data.h, sp model→词表) |
| `tools/build_host_test.sh` | 主机侧 x86 验证 (设备同款代码) |
| `tools/0004~0006-*.patch` | TFLM/NuttX 工作树补丁 (见 LOCAL_LM.md) |
| `ui/assets/` | 桌宠 UI 资源 (字体/云朵位图/壁纸, 从 packages/ai_agent 同步) |
| `LOCAL_LM.md` | 技术文档 |

### 修改

| 文件 | 说明 |
|------|------|
| `ai_agent_main.c` | `ai_agent -q "..."` 优先走本地模型, 关键词表兜底 |
| `ui/pet_page.c` | 点击 ☁️ 桌宠 → 后台线程跑模型 → 气泡显示 (lv_async_call) |
| `CMakeLists.txt` / `Makefile` | 加入 ai_lm/tokenizer/assets; CONFIG_TFLITEMICRO 时链接 tflite_micro |
| `Kconfig` | 栈默认值 49152; 文档说明 TFLM 行为 |

### 构建配置 (defconfig: vendor/sifli/.../nsh)

```text
CONFIG_HAVE_CXX=y
CONFIG_LIBCXXTOOLCHAIN=y
CONFIG_MATH_GEMMLOWP=y
CONFIG_MATH_KISSFFT=y
CONFIG_SYSTEM_FLATBUFFERS=y
CONFIG_TFLITEMICRO=y
CONFIG_EXAMPLES_AI_AGENT_STACKSIZE=49152
```

### 工作树补丁 (生产仓, 已记录在 tools/, 不可进 contest 仓)

| 位置 | 内容 |
|------|------|
| `apps/mlearning/tflite-micro/.../compatibility.h` | GCC13 placement new delete 可访问 |
| `apps/mlearning/tflite-micro/.../kernels/gather.cc` | GATHER 支持 int16 (Embedding 16x8) |
| `apps/mlearning/tflite-micro/.../kernels/unpack.cc` | UNPACK 支持 int16 (LSTM gate) |
| `apps/mlearning/tflite-micro/Kconfig` | TFLITEMICRO 去掉 MATH_RUY 依赖 (ruy 需 std::thread, TFLM 不用) |
| `nuttx/include/stdlib.h` | div_t/ldiv_t/lldiv_t/putenv/div C++ 模式跳过 (libstdc++ 冲突) |

## 三、验证

1. **主机侧 (x86 TFLM)**: 设备同款 tokenizer.c + ai_lm.cxx 编译运行,
   单步 logits 与 Python TFLite 解释器逐位一致; 全部 demo 话术输出一致;
   Tensor Arena 实测 37.8 KB (配置 64 KB)。
2. **固件编译**: `./build_and_flash.sh build` 通过,
   nuttx.bin 4.40 MB (含 3 MB 模型), flash 占用 23.6%, SRAM 72 KB/512 KB。
3. **真机**: 待烧录 (需物理拔插 USB 抓 bootloader 窗口)。

## 四、已知边界

- 量化模型部分问句回复质量有限 (如 "现在几点了" 只生成工具占位),
  `ai_lm_reply` 提取 `<bot>` 后文本, 失败时回退关键词表。
- 模型为小参数量 LSTM, 复杂语义 (多轮/推理) 不在能力范围。

---

# 增补 (2026-08-16 晚): 分词器修正 + Agent 模式

## 五、发现的问题与修复

### 5.1 分词器与 SentencePiece 不一致 (已修复)

原 `tokenizer.c` 用"贪心最长匹配", 与 SP 的 BPE 合并序不等价:

| 输入 | 原实现 (贪心) | SP 正确切分 |
|------|--------------|------------|
| " 今天天气怎么样" | `▁今天` `天气` `怎么样` | `▁` `今天天气怎么样` |
| "周杰伦" | `周` `<unk>` `<unk>` | `周` `<unk>` (连续未知合并) |

分布外 prompt 直接导致回复质量下降。重写为完整 BPE 算法
(空格→▁ / UDS 原子 / 按 id 序合并相邻对 / 连续 UNK 合并), 在 velaAI
全部数据逐位验证: **corpus_v3 20009 + train_v3 18008 条全部一致**。

### 5.2 量化质量评估 (客观基线)

用 val_v3 抽样 100 条做两段式评估 (`velaAI/eval_agent_loop.py`):

| 后端 | s1 工具 | s1 参数 | s2 整句 | s2 房间 | s2 数值 |
|------|--------|--------|--------|--------|--------|
| h5 全精度 | 100% | 99% | 82% | 81% | 100% |
| 16x8 tflite (现用) | 100% | 81% | 18% | 38% | 67% |
| 16x8 重校准 (512 序列) | 100% | 81% | 18% | 38% | 67% |

结论: 量化损伤来自 int8 权重 (槽位复制), 加大校准集无效 (输出逐条一致);
但**工具调用生成完全不受影响** → 应用层补偿空间明确。

### 5.3 Agent 模式 (`ai_lm_agent_reply`)

按训练数据格式实现两段式: `<usr> q` → 模型生成 `<call>` → 端侧执行器
(设备状态表/RTC/演示传感器) 产出 `<obs>` → 模型生成 `<bot>` 回复 →
槽位校验 (回复须含调用的房间与数值) → 不过则训练模板兜底。
另加规则意图层修正量化模型的偶发工具错选 (如裸"今天天气怎么样"误调
set_ac), 直接设备指令 (灯/窗帘/空调开关) 走动作一致性检查。

**设备同款 C 代码** (`tools/host_agent_eval.cxx`) 在同 100 条留出集上:

| 配置 | 房间正确 | 数值正确 | 结构正确 |
|------|---------|---------|---------|
| 16x8 直答 (旧) | 38% | 67% | — |
| **16x8 + agent 模式** | **100%** | **100%** | **100%** |

## 六、接入点

- `ai_agent -q "..."` → agent 模式 (关键词表兜底)
- 桌宠点击 ☁️ → 语音提问 (ASR) → agent 模式; 麦克风失败时闲聊演示
- `ai_agent -v` 语音直通同样走 agent 模式

## 七、固件

`./build_and_flash.sh build` 通过: nuttx.bin 7.82 MB (含 3 MB 模型),
flash 46.6% (16MB), SRAM 64.9% (512KB)。

## 八、真机验证 (2026-08-16 深夜, 烧录成功后)

烧录: 1M baud + no_reset_no_sync 连续 stub 超时 (已知间歇问题),
改用 `./build_and_flash.sh flash -p /dev/ttyACM0` (default_reset) 一次成功。

`ai_agent -q` 串口实测 (回复同时显示在桌宠气泡):

| 查询 | 真机回复 | 结果 |
|------|---------|------|
| 打开客厅的灯 | 好的，已为您打开客厅灯 | ✓ |
| 你好 | 你好，有什么可以帮您？ | ✓ |
| 你是谁 | 我是您家的智能小助手 | ✓ |
| 把卧室空调调到26度 | 好的，卧室空调已设为26度 | ✓ |
| 把主卧空调调到24度 | 好的，主卧空调已设为24度 | ✓ (9s 全流程) |
| 现在几点了 | 现在是 6 点 31 分 | ✓ (真实 RTC) |
| 今天天气怎么样 | 今天天气晴 | ✓ |
| 把厨房的加湿器打开 | 好的，已为您打开厨房加湿器 | ✓ |
| 厨房加湿器什么状态 | 厨房加湿器正在运行 | ✓ |
| 帮我发射火箭 | 好的，已为您查询 - 但找不到对应工具 | ✓ (拒绝) |

单次 agent 查询端到端 ~9 秒 (含进程启动 + LVGL 桌面 + 两段式推理 ~150 步)。

## 九、语音模式真机调试 (2026-08-16 深夜续, 6 个 bug 串)

M8 语音模块首次真机运行暴露一串问题, 逐一定位并修复
(每轮: 二分打印 → 烧录 → 真机触发 → 读崩溃转储/诊断计数):

| # | 症状 | 根因 | 修复 |
|---|------|------|------|
| 1 | `-v` 系统挂死 (hard fault, 串口无响应) | NuttX audio 核心 GETBUFFERINFO 直接调 `lower->ops->ioctl` (audio.c:1271, 无 NULL 保护), 自定义驱动 ops 表缺 `.ioctl` 成员 → 空指针调用 | 补 `sf32lb52_audio_ioctl` (GET/SETBUFFERINFO + ENOTTY) |
| 2 | mic 启动"成功"但 irq=5312/5s (~68 倍速率), 数据幅值 ~17000 恒定 | `AUDCODE_ADC_CLK_CONFIG_TYPE` 有 8 字段 (首字段 samplerate), 驱动初始化只写 7 值 → 全体左移: clk_src_sel=10 非法/clk_div=1 | 两处时钟表补 `{16000, ...}` 前导字段; 修后 irq≈13.5/s = 16kHz 正确速率 |
| 3 | 二次 `-v` 报 START EIO | SDK `HAL_AUDCODEC_DMAStop` 不清 State (源码注释掉), Receive_DMA 恒 HAL_BUSY | stop_capture 补 `State[CH0]=READY` |
| 4 | 监听 103 秒全程超时 (mq 零投递) | 设备/mq 均以 O_RDONLY 打开, 内核 `file_mq_send` 向只读队列发送被静默拒绝 (对照 nxrecorder 用 O_RDWR) | 设备与 mq 均 O_RDWR; 修后 4 秒完成监听 |
| 5 | 每读 640B 丢弃 4096B apb 的 84% 音频 | 部分消费后整块归还 | 维护 s_cur/s_cur_off 跨调用消费 |
| 6 | VAD 假 onset (e≈1.07e8 恒定) → ASR 恒判"再见" | 首个 apb 尾部带 kmm 堆垃圾 (max=32715), 假能量立即触发 VAD | apb 分配后清零 + VAD 前 3 帧预热; 修后 e 降至 5.2e6 |
| 7 | 修 #6 后首个半缓冲仍有满幅瞬态 | ADC 使能瞬态: 模拟通路建立期输出满幅毛刺 | worker 丢弃启动后前 4 个半缓冲事件 (~0.25s); 修后基线 read#0..2 全零 |

验证里程碑 (真机串口输出):
- `irq=1394 cplt=1394 wake=1394` — DMA/回调/worker 全通, 速率正确
- `[voice-dbg] read#1 n=640 max=319: 013f...` — 用户侧收到干净环境音
- `[voice] speech onset (e=1.1e8) → ASR 分类 → agent 回复 → 桌宠气泡` —
  真人语音全链路执行成功
- 基线 (无人说话): `read#0..2 max=0` + `no speech detected` — 垃圾/瞬态
  彻底消除, 安静环境正确静默

遗留: ASR 对真实麦克风语音分类准确率不足 (合成数据训练的域差 +
1s 窗口截断长命令), 属 M8 模型迭代项 (方向: 窗口加长/真实录音增广/
增益标定), 管线本身已完整可用。

经验: WCH USB 桥 (1a86:55d3) 楔死后软件无法恢复 (RTS/DTR 无效),
须物理重插; 烧录用 `--before default_reset` 成功率最高 (no_reset_no_sync
依赖上电时序)。

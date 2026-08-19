# 18: velaAI 端侧大模型 + 桌宠 + 语音全链路 — 经验总结

> 日期: 2026-08-16 (深夜连续会话) | 作者: AI 辅助调试记录
> 范围: velaAI TFLM 模型部署、桌宠接入、语音链路真机调试、烧录工艺
> 关联: `13_velaai_local_lm_integration.md` (技术细节), `app/ai_agent/LOCAL_LM.md`

---

## 一、烧录经验 (SF32LB52-DevKit-LCD + WCH USB 桥)

### 1.1 硬件链路

```
主机 USB ── WCH CH343 桥 (1a86:55d3, /dev/ttyACM0) ── 板载 ROM bootloader
```

### 1.2 核心结论

| 现象 | 结论 |
|------|------|
| `--before no_reset_no_sync` 连续 "timeout while waiting for RAM command response" | 依赖"刚上电在 bootloader"时序, 上电久了必失败 |
| `--before default_reset` 报 "Failed to download stub: TimedOut" | sftool 经 DTR/RTS 复位后 stub 下载偶发失败, **重试可恢复** |
| 串口完全无响应 (console 也死) | 板子已楔死, RTS/DTR 软件复位**无效**, 须物理重插 |
| 板子正常但烧录反复失败 | WCH 桥楔死 (console 仍通!), 软件无法复位桥, 须物理重插 |
| `--before default_reset` 成功率最高 | 本次会话实测: 12+ 次烧录全部由 default_reset 模式成功 |

### 1.3 推荐烧录流程

```bash
# 首选: build_and_flash.sh (default_reset 模式)
./build_and_flash.sh flash -p /dev/ttyACM0

# 失败则机械重试 (勿换参数, default_reset 是成功率之王):
for i in 1 2 3 4 5 6; do
  sftool -c SF32LB52 -p /dev/ttyACM0 -b 1000000 \
    --before default_reset --after soft_reset \
    write_flash out/sifli_sf32lb52_devkit_lcd_nsh/nuttx.bin@0x12010000 \
    && break
  sleep 2
done

# 连续 6+ 次失败 → 不要再试软件手段, 直接物理拔插 USB
```

### 1.4 何时必须物理重插

1. 板子挂死 (连 console 都无响应) — 音频 hard fault 曾致此
2. console 正常但烧录连续失败 — WCH 桥状态坏
3. 大镜像 (7.8MB) 传输中断链 (Broken pipe) — 历史记录 20%~99% 处断

物理重插后立即用 default_reset 烧录 (本次实测 1~5 次重试内成功)。

### 1.5 固件产物

```
out/sifli_sf32lb52_devkit_lcd_nsh/nuttx.bin   # 7.83MB, flash 46.6%, SRAM 64.9%
烧录地址: 0x12010000
```

---

## 二、velaAI 模型部署经验 (TFLite Micro)

### 2.1 架构 (详见 LOCAL_LM.md)

- 模型: velaAI V3 (LSTM 256 / vocab 5000 / 16x8 量化 / 2.3M 参数)
- Tensor Arena 64KB (实测需 37.8KB), 静态 BSS
- 推理: 多 IO 显式状态单步 unroll (GATHER/FULLY_CONNECTED/SOFTMAX/SPLIT/
  UNPACK/TANH/LOGISTIC/MUL/ADD/QUANTIZE/DEQUANTIZE), 无需 LSTM 专用内核
- TFLM 工作树需 3 个补丁 (placement-new / GATHER·UNPACK int16 / stdlib C++)

### 2.2 量化质量结论 (重要)

| 后端 | s1 工具选择 | s2 房间槽位 | s2 数值槽位 |
|------|-----------|-----------|-----------|
| h5 全精度 | 100% | 81% | 100% |
| 16x8 量化 | 100% | **38%** | **67%** |
| 16x8 + 512 序列重校准 | 100% | 38% (输出逐条不变) | 67% |

**结论: 损伤来自 int8 权重, 加大校准集完全无效; 但工具调用生成不受影响。**
对策 = agent 模式: 模型产 `<call>` → 端侧执行器产 `<obs>` 真值 → 模型产
`<bot>` 回复 → **槽位校验 (房间/数值须与调用参数一致) → 不过则训练模板兜底**。
设备同款 C 代码在 100 条留出集: 房间/数值/结构正确率 **100%**。

### 2.3 分词器是隐形质量杀手

原 C 分词器用"贪心最长匹配", 与 SentencePiece BPE 合并序**不等价**:
- `'▁今天天气怎么样'`: SP 切 `▁`+`今天天气怎么样`, 贪心切 `▁今天`+`天气`+`怎么样`
- 连续未知字符: SP 合并为单个 `<unk>`, 贪心逐字符出 UNK

分布外 prompt 让回复质量莫名变差。重写为完整 BPE 算法后
(corpus 20009 + train 18008 条逐位一致), 同一模型同样提示的回复显著改善。
**教训: 自实现分词器必须用训练侧全量数据做逐 token 比对验证, 少数 demo
一致不代表一致。**

BPE 逐位复现要点 (SP BPE):
1. 归一化: 去首尾空白/连续空白压缩, 空格转义 `▁`
2. `<usr>` 等用户定义符号为原子
3. 段内反复合并"拼接结果 **id 最小**"的相邻对 (词表 score = -(id-9),
   id 序即合并优先级 — 不是 score 最大!)
4. 连续未知符号合并为一个 `<unk>`

### 2.4 真机性能

- 单次 agent 查询 (两段式 ~150 步前向) 端到端 **~9 秒** (含进程启动+LVGL)
- 10 项功能查询真机全部正确 (灯/空调/时间 RTC/天气/设备状态/定时/拒绝)

---

## 三、语音链路真机调试经验 (6+1 个 bug)

首次真机运行暴露完整 bug 链, 每个都靠"二分打印 + 真机触发 + 读输出"定位:

| # | 症状 | 根因 | 教训 |
|---|------|------|------|
| 1 | hard fault 系统挂死 | NuttX audio 核心 GETBUFFERINFO **无 NULL 保护**调用 `ops->ioctl`; 驱动 ops 表漏 `.ioctl` 成员 | 自定义 lower-half 驱动的 ops 表要对照核心**所有**调用点, 不能只实现用到的 |
| 2 | DMA 速率快 68 倍, 数据恒定 ~17000 | 时钟配置结构体 **8 字段只初始化 7 个值**, 缺首字段 samplerate → 全体左移 | 结构体初始化逐字段写, 不用位置初始化; HAL 结构体定义要核对字段序 |
| 3 | 二次启动 START EIO | SDK `HAL_AUDCODEC_DMAStop` 不清 State (源码注释掉了), 重启恒 HAL_BUSY | 闭源/半成品 HAL 的 stop 路径要人工补状态清理 |
| 4 | 监听 103 秒全程超时 (mq 零投递) | 设备/mq 均 O_RDONLY 打开, 内核 `file_mq_send` 向只读队列发送**静默拒绝** | **对照同框架已知可用的应用源码** (nxrecorder 用 O_RDWR) 比读文档快 |
| 5 | 每读 640B 丢 4096B 缓冲的 84% | 部分消费的 apb 整块归还 | 流式消费要维护跨调用偏移 |
| 6 | VAD 假 onset (e 恒 1.07e8) | 首个 apb 带 kmm 堆垃圾 (max=32715) | 内核分配的缓冲**必须清零**再用 |
| 7 | 修 #6 后仍有瞬态假能量 | ADC 使能瞬态: 通路建立期输出满幅毛刺被首半缓冲捕获 | 启动后丢弃前 N 个半缓冲 (~0.25s) |

### 3.1 调试方法论 (可复用)

1. **二分打印**: 在嫌疑区间每步 printf, 烧录触发, 看最后打印位置
2. **诊断计数器**: ISR/回调/worker 各加 volatile 计数, stop 时统一打印 —
   `irq=1394 cplt=1394 wake=1394` 一行就能分辨"中断没来/回调没到/线程没醒"
3. **读崩溃转储**: NuttX panic 会打寄存器+任务表+回溯, PC 用 System.map
   解析 (注意 PC 可能落在 _assert 本身, 要看 R2 等现场寄存器)
4. **能量值会说话**: 假能量的特征是**跨运行恒定** (±0.5%) — 真噪声不会
5. **写用例前先看参照物**: nxrecorder 源码 10 分钟 = 猜 2 小时

### 3.2 最终状态

```
[voice] listening → speech onset (真人语音) → ASR 分类 → agent 回复 → 桌宠气泡
```
- 麦克风 DMA 采集: 16kHz 正确速率, 干净数据 (环境噪声 max≈300, 静默 ≈0)
- VAD: 真人语音正确检测, 安静环境正确静默
- ASR: 管线工作, 但真实麦克风语音分类准确率不足 (合成数据训练 vs 真实
  麦克风域差 + 1 秒窗口截断长命令) — 遗留 M8 模型迭代项
  (方向: 增大识别窗口/真实录音增广重训/麦克风增益标定)

---

## 四、sftool 烧录协议速查 (逆向已知, 见 doc 16)

- 帧: `0x7E 0x79`; Enter=`ATSF32\x05!`; 应答 `0xD1`
- stub 下载到 0x2005A000, BKP0R=0xA640, PA21 置位运行
- stub 跑 msh 类 shell: burn_write/burn_erase + OK/RX_WAIT 应答

---

## 五、UI 常驻与触摸冻结修复 (2026-08-16 深夜)

### 症状: "界面卡住无法点击"

两层根因 (第二个更隐蔽):

1. **进程退出 = UI 宿主消失**: ai_agent 进程就是 LVGL 桌面的宿主,
   `-q`/`-v` 展示 5 秒后 `return` 退出 → 帧缓冲停在最后一帧,
   无人轮询 `/dev/input0` → 触摸无效。ps 可确认 ai_agent 任务已不存在。
2. **fgets 阻塞冻结 UI (交互模式同样中招)**: 原交互循环
   `printf → lv_timer_handler() → fgets(阻塞等输入)` — 等输入期间
   LVGL 完全停摆, 触摸事件堆积不处理, 动画冻结。

### 修复 (`ai_agent_main.c`)

```c
/* poll 短超时循环: 每轮驱动 LVGL, 有输入才处理命令 */
while (1) {
    int ret = poll(&pfd, 1, 30);   /* 30ms */
    lv_timer_handler();             /* 触摸/动画/定时器持续处理 */
    if (ret > 0 && fgets(...)) { 处理命令; }
}
```

- `-q`/`-v` 展示完 → 回桌面 → **进入同一常驻循环** (不再退出)
- 单线程驱动 LVGL (线程安全), 控制台 `quit` 干净退出回 nsh
- 真机验证: `-q` 后进程存活 ("UI running" 横幅), quit 正常退出

### 经验

- **GUI 应用的宿主进程生命周期 = UI 生命周期**: 任何"展示后退出"的
  便捷模式都会留下冻结的最后一帧
- **阻塞 IO 与 GUI 主循环不兼容**: fgets/read 等阻塞调用必须换成
  poll/select 超时 + 每轮驱动 GUI (LVGL 单线程模型下这是标准解法)
- 判断技巧: `ps` 里看不到 GUI 宿主任务 = 进程退出型冻结; 任务在但
  UI 不动 = 主循环阻塞型冻结

### 追加: 宠物页卡死 (超级卡, 进页即冻)

UI 常驻修复后又暴露两个崩溃级问题:

1. **16KB 栈线程 + 32KB 栈数组 = 必然溢出**: 点击云朵的 worker 线程
   (栈 16KB) 调用 voice_question_ask, 其中 `int16_t window[16000]`
   (32KB) 是局部变量 → 栈溢出踩内存 → UI 卡死。
   修: 大数组一律 static / worker 栈提到 48KB。
2. **malloc 失败的降级缺失**: 眨眼中间帧需 malloc 360KB×2, 而
   SRAM 512KB - 静态 340KB → 堆仅 ~170KB → 必然失败; 失败后代码
   仍引用全零初始化的 lv_image_dsc_t (w=0/data=NULL) → 进页 ~3.2s
   后空闲眨眼触发 → LVGL 渲染坏描述符 → 卡死。
   修: 分配失败时回退为 睁眼/闭眼 两帧直切。
3. **推理线程与 UI 同优先级**: 模型推理 ~9s 期间 UI 饿死。
   修: worker 优先级降到 150 (UI=100), 推理随时可被 UI 抢占。

**教训: SRAM 预算要算总账** (静态 BSS + 每线程栈 + 堆峰值), 大缓冲
(>4KB) 不上栈、大数据结构 (>100KB) 先问堆剩多少再 malloc, 失败路径
必须有降级行为。

### 追加 2: 对话后返回主页卡死 (跨线程 LVGL) + 动画删除 + 推理可打断

场景: 对话一次后再次点击 → 返回主页 → 卡死。根因与修复:

1. **worker 跨线程调 lv_async_call**: 推理 worker (~9s) 完成时页面可能
   已被删除, lv_async_call 从非 UI 线程操作 LVGL 定时器链表 → 竞争
   → 卡死。修: worker 只写静态结果槽 + volatile ready 标志;
   UI 线程用 lv_timer (100ms) 轮询消费。页面删除时 timer 一并删除,
   迟到的结果自然作废 — 无悬挂引用。
2. **删除宠物动画** (用户要求, 亦是性能正确选择): 眨眼 (含 360KB×2
   中间帧 malloc + 每 70ms 300×300 重绘) 与点击弹跳全部移除;
   未使用位图被链接器 gc, 固件减小 370KB。
3. **推理可中途打断** (`ai_lm_cancel`): 生成循环逐 token 检查 volatile
   取消标志, 命中立即返回 — UI 点击/离开页面即刻释放 CPU。
   语音监听循环同样接入取消 + 开场 1s 无数据快速失败
   (已知: 停止后再次启动 DMA 不搬运, irq=0, 遗留 M8)。
4. worker 线程改 PTHREAD_CREATE_DETACHED (避免不可 join 的线程
   资源泄漏)。

**教训: LVGL (以及一切单线程 GUI 框架) 的铁律 — 非 UI 线程绝不调用
任何 lv_* API, "lv_async_call 线程安全" 是常见错觉; 用 结果槽+轮询
或消息队列把控制权交回 UI 线程。**

## 六、开机自启 + 收尾问题 (2026-08-16 凌晨)

### 取消标志中毒 ("你戳到小云" 秒回, 对话无法复现)

打断把 s_cancel 置 1 后无人清除, 下一次点云朵: 监听循环一进来判
"已取消" → 秒退 → 闲聊降级又被同标志挡住 → 兜底文案。
修: worker 入口 `ai_lm_cancel_clear()`; 被取消的会话不写结果槽
(避免覆盖"已打断"提示)。**教训: 一次性标志必须有明确的生命周期
Owner (谁置位、谁清除、何时清除)。**

### 开机自启 (rcS 机制)

- 板卡 `src/etc/init.d/rcS` 经 RCSRCS 打进 ROMFS (/etc/init.d/rcS),
  NSH 启动自动执行 (CONFIG_ETC_ROMFS=y)
- 内容 (生产树补丁见 tools/0007-rcs-ai-agent-autostart.patch):
  ```
  audio_setup      ← 注册麦克风
  sleep 2          ← 等 LCD 异步初始化
  ai_agent -d &    ← 守护模式后台
  ```
- ai_agent 新增 `-d`: 纯 UI 循环 (lv_timer_handler + usleep 20ms),
  不碰 stdin, 与 nsh 控制台完全共存

### 自启黑屏坑 (两连)

1. **rcS 注释坑**: rcS 先过 C 预处理器 (gcc -E) 再进 ROMFS, '#' 注释
   直接报错, '/* */' 剥不掉 (ROMFS 用原始文件) → nsh 报
   "command not found" 中止脚本。**rcS 只能写纯命令。**
2. **LCD 异步初始化竞态**: rcS 执行时 lcd_async_init 尚未注册
   /dev/lcd0 → display_init 失败 → 无头守护 → 黑屏。修: rcS sleep 2 +
   display_init 设备等待循环 (access /dev/lcd0 最多 10s)。
   手动启动不复现是因为那时 LCD 早已就绪 — **自启代码必须考虑
   "最早的启动时刻" 下所有依赖未就绪的情形。**

### 最终验收 (真机)

上电 → 麦克风自动注册 → 桌面自动出现 (无需串口) → 触摸可用 →
点云朵说话 (DMA 全速 irq=570) → 监听中返回立即打断 → 重进页正常 →
再点全新会话。

## 七、语音会话稳定性追凶 (2026-08-17 凌晨, 5 连修)

现象: 打断一次后所有点击都显示"已打断" (busy 卡死) / "你戳到小云"。
逐层剥洋葱, 每层都是真机日志抓的现行:

| # | 根因 | 证据 | 修复 |
|---|------|------|------|
| 1 | **`mq_receive` 阻塞语义误用**: flush 的取空队列循环在空队列上永久挂起 | worker 卡在 capture started 与 mic ok 之间 | 改 `mq_timedreceive` 零超时 (非阻塞尝试) |
| 2 | **自愈重建造成多 worker 并存**: 旧 worker 复活后与新 worker 抢同一 pendq/aux, 互相偷缓冲 → 数据黑洞 | 同会话 wake>cplt 与 wake<cplt 并存 | 重建前 `nxtask_delete` 旧线程, 保证唯一 |
| 3 | **信号量陈旧计数**: 死线程未消费的 post 残留, 下会话被陈旧唤醒轰炸, 白耗 skip_events/drain 判定 | wake=88 vs cplt=53 | stop 时 `nxsem_trywait` 清残留 |
| 4 | **看门狗 15s 太紧**: 正常会话全程 ~15s (反应+说话+尾判定+ASR+9s 推理), 与自然完成赛跑造成误杀 | 日志: watchdog 触发后 1s 会话自然完成 | 15s → 25s |
| 5 | **VAD 适应期被说话污染**: 用户点完立刻开口, 底噪均值抬到 4.5e7, 阈值 7.1e8 比真实语音 (1-3e8) 还高, onset 永不触发 | `vad threshold=713405184 (noise=44587824)` | 阈值上限封顶 5e7 |

另: 无数据保护扩展到全程 (onset 前连续 2s 无数据=放弃降级;
onset 后=视为语音结束, 都不会挂满 200x500ms)。
worker enter/exit 打印 + UI 看门狗 (25s 强制恢复可点击) 兜底。

**方法论: 卡死类 bug 的三件套 — (a) 线程进出打印定位死区;
(b) 计数器对比 (wake vs cplt 揭示线程生死与信号量卫生);
(c) UI 层看门狗保证最坏情况 25s 自愈, 不让用户面对永久卡死。**

---

## 六、本次会话交付清单

| 交付 | 验证 |
|------|------|
| tokenizer.c BPE 重写 | 38017 序列与 SP 逐位一致 |
| ai_lm agent 模式 (执行器/校验/意图层/模板) | 100 条留出集 100/100/100 |
| CLI/桌宠/语音全部走 agent 模式 | 真机 10 项查询全对, ~9s/次 |
| 音频驱动 3 修复 (ioctl/时钟/State) + mic 4 修复 (O_RDWR/偏移/清零/预热) | 真机语音全链路跑通 |
| UI 常驻修复 (poll 循环 + -q/-v 不退出) | 进程存活 + quit 干净退出 |
| 文档: 本文件 + 13 号记录 + LOCAL_LM.md + TASK_STATUS | — |

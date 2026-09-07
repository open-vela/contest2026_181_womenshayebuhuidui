# 2026 openvela 大赛交付执行计划（Team 181）

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在不改动任何公共仓（frameworks/packages/vendor/nuttx）的前提下，补齐赛道合规硬要求（主动×3 + Skill）、完成端云推送闭环、产出全部交付材料，并使 本地=个人fork=官方专属仓 三层一致。

**Architecture:** 全部新功能收在团队仓 `app/ai_agent/` 内：新增 `pet_care` 模块（关怀调度器，挂接已验证的 daemon 主循环 tick）、`health/hr_monitor`（模拟心率+阈值）、`net/log_pusher`（HTTP 推送）。推送链路选 **HTTP POST + 极简 Python 服务器**（不用 MQTT，避免 15KB SRAM 线程开销——SRAM 仅余 ~37KB）。UI 输出复用现有 `lvgl_ui_channel_send()` / `pet_display_set_emotion()`；文本生成复用 `ai_lm_agent_reply()`（L2）+ 模板库（L1 保底）。

**Tech Stack:** NuttX(openvela) C 应用 / LVGL 9.1 / TFLite Micro / Python3 stdlib（服务器）/ CMake nuttx_add_application 注册模式。

**Spec:** 需求来源＝官方 `docs/zh-cn/contest_2026/{contest_overview, ai_hardware_track_guide, code_submission_guide, ai_coding_log_guide}.md` + 本仓 `docs/DELIVERY_MANUAL.md` 5.4 清单 + 用户已定决策（SIM=赛后演进；App/服务器=最小推送闭环；心率=纯模拟辅助信号）。

## Global Constraints

- 不得修改 `frameworks/`、`packages/`、`vendor/`、`nuttx/`、`external/` 下任何文件（全部改动限 `contest2026_181_womenshayebuhuidui/` 内）。
- SRAM 余量 ~37KB（`docs_ble/24_open_issues.md`）：新增逻辑不新增常驻线程；L2 生成用一次性 detached pthread（8KB，每次关怀至多 1 个）。
- 任何 commit 前执行密钥复扫：`grep -rE "sk-[A-Za-z0-9_-]{20,}|1k8F[A-Za-z0-9]{6,}" --include="*.c" --include="*.h" --include="*.md"`，期望 0 命中。
- 每个任务结束必须 commit（小步提交）；commit 身份已配置（Sen70s / fhersen@outlook.com，已核实）。
- 编译验证入口：`cd /home/aila/projects/vela_contest/out/openvela_contest2026_181_board_ai_agent && ninja -j8`（已核实，与 reb.sh 第 1 步一致）。**烧录/真机验证一律标记 [HUMAN]**。
- 代码风格与现仓一致：NuttX 缩进（tab）、`s_` 前缀 static、中文注释解释"为什么"。
- 触摸/按键活动 = 空闲判定的唯一复位源（已核实 `key_input.c:196 key_monitor_thread` 与 `pet_page.c:312-331` 点击回调两个插桩点）。

## 执行模式标记

`[AGENT]`＝批准后我可自动执行；`[HUMAN]`＝需要你亲手（真机/账号/按钮）；`[AGENT准备+HUMAN执行]`＝我备好一切，你按清单操作。

---

## Phase 0：三层对齐（止血）

### Task 0.1: 远程状态核实 [AGENT]

**Files:** 无修改，只读。

- [ ] Step 1: `cd /home/aila/projects/vela_contest/contest2026_181_womenshayebuhuidui && git fetch fork && git fetch openvela`
  Expected: fetch 成功（若网络失败→报告并停止，等待用户检查 SSH/代理）
- [ ] Step 2: `git status -sb` + `git rev-list --count fork/feat/ai-agent-contest..HEAD`（期望与 9/6 缓存一致：0 落后）
- [ ] **自查:** fetch 输出无冲突分支；三个分支差异数字与 9/6 报告吻合（feat=0/0、dev 领先 openvela 53）。若有变化→重新评估 Task 0.2 基线再继续。

### Task 0.2: 提交工作区 16 天改动 [AGENT]

**Files:**
- Modify(提交): 11 个已跟踪文件（`ai_lm.cxx`、`launcher_page.c`、`pet_page.c`、`lvgl_ui_channel.c`、`about_page.c`、`img_background_watch.c`、两个 defconfig、`LOG.md`、`24_open_issues.md`、`prereadme.md`）
- Create(提交): 6 个未跟踪文件（`key_input.c/.h`、`pet_display.c/.h`、`lvgl_ui_channel.h`、`gen_bg_rgb565.py`）

- [ ] Step 1: 密钥复扫（Global Constraint 第 3 条命令），期望 0 命中
- [ ] Step 2: 分三个逻辑提交：
  - `git add app/ai_agent/ui/key_input.c app/ai_agent/ui/key_input.h app/ai_agent/ui/pet_display.c app/ai_agent/ui/pet_display.h app/ai_agent/ui/lvgl_ui_channel.h` → `feat(ui): add pet_display emotion engine + key_input module`
  - `git add app/ai_agent/ui/ app/ai_agent/ai_lm.cxx app/ai_agent/tools/gen_bg_rgb565.py board/contest_board/configs/` → `feat: launcher/pet page enhancements + defconfig slim (flash room for model)`
  - `git add docs_ble/ prereadme.md` → `docs: LOG rounds + open issues update + prereadme draft sync`
- [ ] Step 3: `git push fork feat/ai-agent-contest`
- [ ] **自查:** `git status --short` 期望完全干净；`git rev-list --count fork/feat/ai-agent-contest..HEAD` 期望 0。编译验证：`ninja -j8` 通过（确保提交的代码可构建）。

### Task 0.3: 向官方仓发 PR [AGENT准备+HUMAN合并]

- [ ] Step 1: `gh pr create --repo open-vela/contest2026_181_womenshayebuhuidui --base dev-ai-contest-2026 --head Sen70s:feat/ai-agent-contest --title "feat: ai_agent local LLM + BLE PAN + proactive care" --body-file <(生成说明文)`（若 gh 未认证→输出手工 PR 链接 `https://github.com/open-vela/contest2026_181_womenshayebuhuidui/compare/dev-ai-contest-2026...Sen70s:feat/ai-agent-contest`）
- [ ] Step 2: **[HUMAN]** 打开 PR → Self-review → Merge（官方流程允许自行合入）
- [ ] **自查:** `git ls-remote openvela dev-ai-contest-2026` 的 SHA 与本地 HEAD 一致 → 三层对齐达成。

### Task 0.4: Android App 版本控制 [AGENT准备+HUMAN建仓]

**Files:** `com.agent.coapp-main/`（29 Kotlin 文件，已核实非 git 仓库）

- [ ] Step 1: 为该目录写 `.gitignore`（`build/`, `*.apk`, `.gradle/`, `local.properties`, `.idea/`）
- [ ] Step 2: `git init && git add -A && git commit -m "feat: companion app baseline (Compose+MVVM, BLE tunnel, TCP proxy)"`
- [ ] Step 3: **[HUMAN]** 在 GitHub 个人账号建空仓（建议名 `coapp-team181`）→ 提供地址 → 我 push。（或在审核本计划时指定放弃此项）
- [ ] **自查:** push 后 `git ls-remote` 可见 HEAD；`.gitignore` 生效（`git status` 无 build 残留）。

---

## Phase 1：赛道合规（主动×3）

### Task 1.0: 构建基线确认 [AGENT]

- [ ] Step 1: `cd /home/aila/projects/vela_contest/out/openvela_contest2026_181_board_ai_agent && ninja -j8`，记录通过/告警基线
- [ ] **自查:** 基线必须通过；若基线失败，先修构建再进入 Phase 1（失败即上报，不擅自改公共仓）。

### Task 1.1: pet_care 关怀调度模块（含空闲关怀场景②） [AGENT]

**Files:**
- Create: `app/ai_agent/pet_care.h`、`app/ai_agent/pet_care.c`
- Modify: `app/ai_agent/ai_agent_main.c`（daemon 循环 277-285 行内插 `pet_care_tick();`）、`app/ai_agent/ui/key_input.c`（monitor 线程读到按键处插 `pet_care_note_activity();`）、`app/ai_agent/ui/pet_page.c`（点击回调 312-331 行处插同上）、`app/ai_agent/CMakeLists.txt`（主 app SRCS 增两文件）、`app/ai_agent/Kconfig`（新增 `AI_AGENT_CARE_IDLE_MIN`（默认5）、`AI_AGENT_CARE_COOLDOWN_MIN`（默认10）、`AI_AGENT_CARE_QUIET_START/HOUR`（22/7）四个 int 项）

**Interfaces (已核实的现有 API，直接调用):**
- 消费: `lvgl_ui_channel_send(const char*)`（气泡上屏）、`pet_display_set_emotion(pet_emotion_t)`、`chat_log_add(int is_answer, const char*)`、`ai_lm_agent_reply(const char*, char*, int)`
- 产出（后续任务依赖）:
  - `int pet_care_init(void);` / `void pet_care_tick(void);`（20ms 周期调用，内部 1Hz 分频做事）
  - `void pet_care_note_activity(void);`（活动复位）
  - `int pet_care_alarm_set(int minutes);` / `void pet_care_alarm_cancel(void);`
  - `void pet_care_hr_report(int bpm);`

- [ ] Step 1: 写 `pet_care.h`（上述 6 个接口 + 内部状态注释：`s_last_activity_s`、`s_last_care_s`、`s_alarm_at`、`s_alarm_stage`）
- [ ] Step 2: 写 `pet_care.c`：
  - 1Hz 分频（tick 计数 50 次执行一次逻辑）
  - L1 模板库：按时段（早/中/晚）×场景（idle/告警/叫醒）共 ≥12 条中文模板（常量数组）
  - 空闲关怀：`now - max(s_last_activity_s, s_last_care_s) >= idle_min` 且非静默时段（22:00-07:00，用 `localtime`）→ `lvgl_ui_channel_send(模板)` + `pet_display_set_emotion(PET_EMOTION_CONFUSED)` + `chat_log_add(1, text)` + `s_last_care_s = now`
  - L2 增强：关怀触发后起 detached pthread（`pthread_attr_setstacksize 8192`）调 `ai_lm_agent_reply("主动关怀", buf, 256)`，成功则再 `lvgl_ui_channel_send(buf)`（第二条个性化追问；失败静默，L1 已保底）
  - 冷却：`s_last_care_s` 起 cooldown 内不再触发
- [ ] Step 3: 插桩三处调用点（见 Files）
- [ ] Step 4: `ninja -j8` 编译通过
- [ ] Step 5: host 冒烟（无 LVGL 依赖的纯逻辑抽测）：临时将模板选择函数编译进 host 小程序验证时段分支——可选，若耦合 LVGL 则跳过并以真机验收兜底
- [ ] Step 6: commit `feat(care): proactive companion scheduler (idle care, quiet hours, cooldown)`
- [ ] **自查:** grep 确认 `pet_care_tick` 只被 daemon 循环调用；确认无新增常驻线程（`pthread_create` 仅关怀触发瞬间）；`git diff --stat` 确认未触碰 `frameworks|packages|vendor|nuttx`；真机验证项登记到 [HUMAN] 清单 H-2。

### Task 1.2: 闹钟叫醒场景①（set_timer 真实化） [AGENT]

**Files:**
- Modify: `app/ai_agent/ai_lm.cxx`（`ag_execute()` 的 set_timer 分支 ~797-801 行：空桩改为调用 `pet_care_alarm_set(atoi(arg))`，cancel_timer 改 `pet_care_alarm_cancel()`；`ag_verify` 对 set_timer 的回复保持不变）

**Interfaces:** 消费 Task 1.1 的 `pet_care_alarm_set/cancel`。参数约定：模型输出 `<call> set_timer N </tool>`，N=分钟数（`ag_parse_call` 已解析，不改动解析器）。

- [ ] Step 1: `pet_care.c` 实现叫醒状态机：`s_alarm_stage`: 0=无闹钟；T-60s→Stage1（`lvgl_ui_channel_send("还有一分钟哦～")`+`SPEAKING`）；T0→Stage2（晨间简报模板：问候+固定天气占位语，因无网络天气）+`HAPPY`；T0+120s 无活动→Stage3 升级模板（更急切）；任意 Stage 收到 `pet_care_note_activity()`→记录唤醒事件 `chat_log_add(0,"[唤醒确认] HH:MM")` 并清闹钟
- [ ] Step 2: **闹钟穿透静默期**（白名单）：静默检查仅在 idle/告警路径，闹钟路径跳过 quiet-hours 判断
- [ ] Step 3: `ninja -j8` 通过；grep 确认 `ag_execute` set_timer 分支已无 "直接 return success" 空桩
- [ ] Step 4: commit `feat(alarm): wire set_timer tool to real alarm state machine`
- [ ] **自查:** 真机验收 [HUMAN] H-3（语音/按键"定时"→到点叫醒）；确认取消路径（`alarm_cancel`）也有调用（意图层 cancel_timer）；确认闹钟与空闲关怀互不轰炸（闹钟 pending 时 idle 关怀跳过）。

### Task 1.3: 心率模拟 + 阈值告警场景③ [AGENT]

**Files:**
- Create: `app/ai_agent/health/hr_monitor.h/.c`（模拟器+阈值逻辑）、`app/ai_agent/health/hr_cmd.c`（NSH `hr_set` 命令）
- Modify: `app/ai_agent/CMakeLists.txt`（主 app SRCS 加 hr_monitor.c；仿 `audio_setup` 模式注册独立应用 `hr_set`，STACKSIZE 2048，Kconfig 项 `EXAMPLES_AI_AGENT_HR_CMD`）、`app/ai_agent/Kconfig`（`AI_AGENT_HR_HIGH` 默认 120）、`pet_care.c`（tick 1Hz 调 `hr_monitor_tick()`）

**Interfaces:** `void hr_monitor_tick(void);` `int hr_monitor_set(int base_bpm);`（hr_set 调用，注入事件=临时抬到 130-150 随机游走回落）；阈值触发回调直接调 Task 1.1 的 `pet_care_hr_report(bpm)`（> `AI_AGENT_HR_HIGH` 时 pet_care 发告警模板 + `PET_EMOTION_CONFUSED` + `chat_log_add(0,"[心率告警] xx bpm")`，冷却 10 分钟）。

- [ ] Step 1: 写 hr_monitor（随机游走 65-95 基线，注入后 60s 内游走到高值再指数回落）
- [ ] Step 2: 写 hr_cmd.c（`hr_set <bpm>` / `hr_set event`），CMake/Kconfig 注册（照抄 audio_setup 的 `nuttx_add_application` 结构，已核实 CMakeLists.txt:99-110 模式）
- [ ] Step 3: `ninja -j8` 通过
- [ ] Step 4: commit `feat(health): simulated heart-rate monitor + threshold alert`
- [ ] **自查:** 真机验收 [HUMAN] H-4（`hr_set event` → 桌宠担心表情+告警气泡）；材料措辞审查：所有文案无"诊断/监测健康状态"医学表述，仅"提醒/记录"。

---

## Phase 2：端云推送闭环（最小版，HTTP）

### Task 2.1: 服务器（Python stdlib 单文件） [AGENT]

**Files:** Create: `tools/server/push_server.py`

- [ ] Step 1: 实现 `http.server`：`POST /push`（JSON 数组 `{ts,role,text,events}` 落盘 `records/YYYYMMDD.jsonl`）、`GET /records`（返回 JSON）、`GET /`（极简 HTML 时间线页——演示时手机浏览器即可替代 App）
- [ ] Step 2: 本机自测：起服务→`curl -X POST --data '{"records":[{...}]}' localhost:8765/push`→`curl localhost:8765/records` 验证回读一致
- [ ] Step 3: commit `feat(server): minimal push server (stdlib, records timeline)`
- [ ] **自查:** 两个 curl 用例输出贴进 commit message 或任务日志；服务不写任何密钥；监听 0.0.0.0 便于手机访问，README 注明仅演示用。

### Task 2.2: 设备端推送 log_pusher [AGENT]

**Files:**
- Create: `app/ai_agent/net/log_pusher.h/.c`、`app/ai_agent/net/push_cmd.c`（NSH `push_now`）
- Modify: `CMakeLists.txt`（主 app SRCS + 独立应用 `push_now` 注册）、`Kconfig`（string 项 `AI_AGENT_PUSH_URL` 默认 `http://192.168.43.1:8765/push`，menuconfig 可改）

**Interfaces:** 消费 `chat_log_count()/chat_log_get(idx, buf, size)`（已核实签名）+ `chat_log.h` 若无时间戳则用 `time()` 现取。产出：`int log_push_now(void);`——遍历新记录→组 JSON→BSD socket HTTP POST（defconfig 已核实 NET_TCP/ sockets 启用）。

- [ ] Step 1: 实现 log_pusher（一次连接 POST 全部未推记录；`s_last_pushed_idx` 静态游标；失败打印 errno 退出，不重试常驻）
- [ ] Step 2: `ninja -j8` 通过
- [ ] Step 3: commit `feat(net): chat-log HTTP pusher + push_now command`
- [ ] **自查:** 端到端 [HUMAN] H-5（板子 PAN 联网后 `push_now`→服务器/浏览器见记录）；确认 SRAM：无新增常驻线程（push_now 在命令上下文同步执行，退出即释放）。

### Task 2.3: 记忆机制预置 [AGENT]

- [ ] Step 1: 在 `docs/STARTUP_GUIDE.md` 增补"记忆演示"节：框架启动时 `/data/agent/memory/MEMORY.md`（`agent_config.h:92` 已核实）会自动进入 system prompt；写入种子记忆两行（孩子昵称、喜好）供云端 LLM 首问即见效果
- [ ] Step 2: commit `docs(memory): MEMORY.md demo seeding steps`
- [ ] **自查:** 路径引用与 `agent_config.h:91-93` 一致；依赖云端 LLM（[HUMAN] H-1 后生效）。

---

## Phase 3：交付材料

### Task 3.1: README.md 正式替换 [AGENT]

- [ ] Step 1: 基于 `prereadme.md` 重写 `README.md`：作品名+赛道+一句话定位 → 用户故事（谁/场景/问题，官方基础要求原文）→ 功能清单与完成度表 → 运行方式（构建/烧录/`set_llm`/`ask`）→ 目录导航 → AI 使用声明（工具、日志位置、沉淀 Skill）→ 上游 PR 索引
- [ ] Step 2: `git add README.md && git commit -m "docs: replace contest template README with project description"`
- [ ] **自查:** 对照官方 README 三要素（作品名称、所属赛道、运行方式）逐项勾选；密钥复扫。

### Task 3.2: 介绍文档 [AGENT生成+HUMAN审]

- [ ] Step 1: 用 `document-skills:pptx` 生成 10-12 页介绍 PPT（素材：`docs/images/` 12 张 SVG + 五幕演示结构 + 得分点映射）；同时导出 PDF 版
- [ ] Step 2: **[HUMAN]** 审阅内容口径（尤其隐私叙事："本地优先、云端可选"）
- [ ] **自查:** 页面无占位文本；文件名规范（`team181_introduction.pptx/.pdf`）。

### Task 3.3: 演示视频 [AGENT脚本+HUMAN拍摄]

- [ ] Step 1: 产出 `docs/demo_script.md`：五幕分镜（叫醒/陪伴/主动关怀+心率/App 联动/愿景），每幕含操作步骤、预期画面、台词字幕、时长（总 ≤4 分 30 秒）
- [ ] Step 2: **[HUMAN]** 按分镜拍摄 + 录屏；剪辑可后补
- [ ] **自查:** 分镜每步与真机实际行为一致（与 Task 1.x 验收结果核对）；总时长合规。

### Task 3.4: 开发 Skill 沉淀（Type B） [AGENT]

**Files:** Create: `/home/aila/projects/vela_contest/.claude/skills/bt-pan-watch-debug/SKILL.md`

- [ ] Step 1: 按 `.claude/skills/executor/SKILL.md` 的 YAML front matter 格式（已核实格式）写"SF32LB52 蓝牙 PAN 调试技能"：四条硬规则（net_buf 池注册/邮箱 ring 一次写入/SRAM 上界 0x2007FB00/RTS 电源）、症状→排查表（源自 `docs_ble/22_pan_engineering_guide.md`）、工具入口（`pan_bringup.py`/`pan_soak.py`）
- [ ] Step 2: 团队仓内同步归档一份 `docs/dev_skill_bt_pan.md` 并 commit
- [ ] **自查:** frontmatter 有 name+description；在会话中实际触发一次验证可被发现。

### Task 3.5: AI Coding 日志补全 [AGENT+HUMAN]

- [ ] Step 1: 检查 `~/.claude/contest-collector-staging/` 是否有 8/20 后会话 → 有则按 `ai_coding_log_guide.md` 导出追加到 `logs/Sen70s/`
- [ ] Step 2: 扩展 `logs/Sen70s/redact.json`：增加路径规则（`/home/aila`→`/home/dev`）；对已提交 14 个文件批量替换后 commit；邮箱逐个确认（5 个文件，[HUMAN] 确认哪些可留）
- [ ] **自查:** `grep -rl "aila" logs/` 期望 0；manifest.json 与新增会话一致。

### Task 3.6: 商业一页纸 [AGENT]

- [ ] Step 1: `docs/business_onepager.md`：痛点（留守儿童）→ 双端产品 → 端云分工 → G 端模式 → 数据飞轮（与青少年心理健康 App 互 feed）→ 演进路线（SIM+传感器，官方 400 元打样券政策引用）→ 合规承诺（监护人授权/本地优先/脱敏）
- [ ] Step 2: commit
- [ ] **自查:** 无医疗诊断表述；G 端叙事不承诺已完成功能。

---

## Phase 4：提交执行

### Task 4.1: 仓库清理终检 [AGENT]

- [ ] Step 1: `git rm "新建 Microsoft Word 文档.docx"`（大赛信息个人整理稿，占位文件名不入评审）
- [ ] Step 2: `git rm -r app/ai_agent/speech/data/raw/`（已核实为 edge-tts 合成数据、非真人录音，但属可再生的中间资产，瘦身仓库；训练脚本可复现）
- [ ] Step 3: 终扫：密钥（Global Constraint 第 3 条）+ `grep -rl "aila" .` + 大文件复核
- [ ] Step 4: commit `chore: cleanup placeholder docs and regenerable raw data`
- [ ] **自查:** `git ls-files | wc -l` 前后对比记录；工作树干净。

### Task 4.2: 最终 PR + CLA [HUMAN]

- [ ] Step 1: [AGENT] push 全部收尾提交、更新 PR 描述（功能清单+验收证据链接）
- [ ] Step 2: **[HUMAN]** 合并 PR、签 CLA、官网/表单提交作品链接+介绍文档+视频
- [ ] **自查（提交后）:** `git ls-remote openvela dev-ai-contest-2026` == 本地 HEAD；官方仓 README 已是新内容。

### Task 4.3: 交付验收核对 [AGENT]

- [ ] Step 1: 逐项过 `docs/DELIVERY_MANUAL.md` 5.4 清单 + 本计划"官方要求映射表"，输出最终验收报告
- [ ] **自查:** 任何"未完成"项必须有明确原因与风险说明，不得静默跳过。

---

## [HUMAN] 动作清单（汇总，精确操作）

| # | 动作 | 前置 |
|---|------|------|
| H-1 | 板子 PAN 联网后串口执行 `set_llm mimo <大赛key>` → `router_status` → `ask 你好` | Task 1.1-1.3 烧录后 |
| H-2 | 真机验收空闲关怀：静置 5 分钟观察主动开口 | Task 1.1 |
| H-3 | 真机验收闹钟：`ask`/按键设定时→到点叫醒序列 | Task 1.2 |
| H-4 | 真机验收心率：`hr_set event`→告警 | Task 1.3 |
| H-5 | 真机验收推送：`push_now`→浏览器打开 `http://<server>:8765/` | Task 2.2 |
| H-6 | PR 合并按钮、CLA 签署、官网提交、视频拍摄剪辑、博文发布、GitHub 建仓（Task 0.4） | 对应任务 |

## 自查审核记录（计划完成后执行，结果如实记录）

1. **Spec 覆盖**：官方 8 项提交要求 → 代码=T0.2/T0.3；README=T3.1；介绍文档=T3.2；视频=T3.3；日志=T3.5；Skill=Type A 见 Task 1.4 说明（**注**：Type A Skill 依赖云端 LLM 演示，按此前调查其内容为框架内置 10 Skill 之外的自定义 Markdown，执行时并入 Task 1.1 验收后追加 `docs`+文件，不单列任务）；开发 Skill=T3.4；CLA/合并=T4.2。主动×3=T1.1/1.2/1.3。**无缺口。**
2. **占位符扫描**：全文无 TBD/TODO/"适当处理"；所有代码步骤给出接口签名或引用已核实签名；唯一"待定"是 Task 0.4 的远程仓库名（属用户决策项，已在任务内标注）。
3. **类型一致性**：`pet_care_alarm_set(int minutes)` 在 T1.1 定义、T1.2 消费一致；`pet_care_hr_report(int bpm)` T1.1 定义、T1.3 消费一致；`chat_log_add(int, const char*)`/`chat_log_get(int,char*,int)` 与 `chat_log.h:22-28` 核实一致；情绪枚举仅用已核实的 7 值（告警用 `PET_EMOTION_CONFUSED`，无虚构 WORRIED）。
4. **事实来源**：所有 file:line 引用均出自 9/6 当日只读核查（daemon 循环 `ai_agent_main.c:273-285`、set_timer 空桩 `ai_lm.cxx:797-801`、MQTT Kconfig `packages/ai_agent/Kconfig:140`、SRAM 余量 `docs_ble/24_open_issues.md`、CMake 注册模式 `CMakeLists.txt:85-110`、build 入口 `reb.sh:11-16`）。
5. **方案变更说明**：MQTT 通道降为 T5 可选——启用需 15KB 常驻（SRAM 余 37KB）且 HTTP 方案零常驻成本即可闭环，风险收益不成立。此为本次自查发现的修正。

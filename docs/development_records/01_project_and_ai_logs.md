# 项目、架构与 AI 日志记录

## 1. 项目定位

项目目录：`/home/aila/projects/vela_contest/contest2026_181_womenshayebuhuidui`。

这是 openvela AI Coding Contest 2026 的团队仓库。完整工作区位于其上级 `vela_contest`，通过 repo manifest 和 linkfile 把团队代码映射到 openvela 工程：

| 团队路径 | openvela 工作区映射 |
|---|---|
| `app/hello_app` | `packages/demos/contest2026_181_hello_app` |
| `app/ai_agent` | `packages/demos/contest2026_181_ai_agent` |
| `quickapp/hello_quickapp` | `packages/apps/contest2026_181_hello_quickapp` |
| `board/contest_board` | `vendor/openvela/boards/contest2026_181_board` |

映射来源：[`../../contest2026_181_womenshayebuhuidui.xml`](../../contest2026_181_womenshayebuhuidui.xml)。`board/contest_board` 是占位骨架；本轮实际目标板为 `SF32LB52-DevKit-LCD`。

## 2. Git 与提交注意事项

- 源码、文档和 `logs/Sen70s` 是应纳入团队仓库的开发材料。
- `out/`、编译中间文件、目标文件和链接产物不应提交。
- 不要把 API key、认证 token 或本机私密配置复制到项目文档。
- 新增的本目录 Markdown 应与现有 `docs/setup_guide.md` 互补；基础工具链和构建说明优先引用原文。

## 3. AI 对话日志为什么曾经看不到

### 现象

启动 AI 工具后，用户在团队仓库中没有看到对应日志，曾怀疑采集器没有工作。

### 原因

采集器原先默认把自动导出放到了工作区根目录的 `logs/Sen70s`，而不是团队仓库的 `contest2026_181_womenshayebuhuidui/logs/Sen70s`。

### 处理

在 `~/.claude/contest-collector.env` 中设置：

```text
TEAM_ID=contest2026_181_womenshayebuhuidui
GITHUB_LOGIN=Sen70s
CONTEST_REPO_DIR=/home/aila/projects/vela_contest/contest2026_181_womenshayebuhuidui
```

共享采集脚本 `/home/aila/.claude/contest-shared/contest-snapshot.sh` 会加载该配置并调用 `snapshot_core.py`。导出逻辑已支持 `CONTEST_REPO_DIR`，并要求目标目录位于工作区内且已经存在。

### 结果

[已验证] 当前 [`../../logs/Sen70s/manifest.json`](../../logs/Sen70s/manifest.json) 显示 4 个 Claude Code 会话，健康状态均为 `ok`；最新会话的 `last_event_at` 为 2026-08-09。原始 JSONL 仍按日期目录保存。

一次 transcript unreadable 错误曾单独出现在 collector staging 的错误目录；不能据此判断整个采集器失效。

## 4. 如何使用原始日志

日志目录规范见 [`../../logs/README.md`](../../logs/README.md)。本目录只引用 manifest 和 JSONL 作为证据来源：

- 需要确认事件顺序时，查阅 `logs/Sen70s/manifest.json` 中的 `file_path`。
- 需要确认具体命令和错误时，再读取对应 JSONL。
- 不要把整份 JSONL 转储到 Markdown，避免重复和泄露敏感内容。

## 5. 当前应用架构边界

`app/ai_agent/ai_agent_main.c` 是 NuttX builtin 应用入口，使用 LVGL 创建界面，并在本地静态关键词表中选择响应。当前源码实际表现为嵌入式本地演示，不应把 README 中描述的云端 LLM、ASR、TTS、WebSocket 能力写成已经完成的功能。

相关实现文件：

- [`../../app/ai_agent/ai_agent_main.c`](../../app/ai_agent/ai_agent_main.c)
- [`../../app/ai_agent/Kconfig`](../../app/ai_agent/Kconfig)
- [`../../app/ai_agent/CMakeLists.txt`](../../app/ai_agent/CMakeLists.txt)
- [`../../app/ai_agent/Make.defs`](../../app/ai_agent/Make.defs)

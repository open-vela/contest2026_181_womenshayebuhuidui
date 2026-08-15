# 15. 2026-08-15 深夜 ~ 08-16 凌晨：加密事件定位 + SSP 桥接 + 可发现性突破（R52→R62）

## 一句话进展

**今日打通三条关键链路**：
1. 定位并修复了 zblue rx 事件滞留（rx_work 单事件+自提交丢事件 → 循环排空）
2. 实现 LCPU 标准事件号 → zblue 偏移事件号的 SSP 桥接（0x23→0x31 等），SSP 配对流程第一次真正推进（zblue 回复 IO_CAPABILITY_REPLY 成功）
3. **可发现性突破**：手机第一次在蓝牙列表里看到板子（显示 cd:ab:78:56:34:12 = 板子 BR 地址）——inquiry scan 强制开启 + 100% 占空比 + interlaced 扫描类型

## 关键认知修正

- **04 13 是 Number-of-Completed-Packets（合法事件），不是 EncryptChange**！R52 的"归一化"在破坏它（已删除）。真正的加密事件是 0x08，本板认证失败路径从不产生。
- zblue 本分支的 SSP 事件号是**偏移号**（IO_CAPA_REQ=0x31, RESP=0x32, USER_CONFIRM=0x33, PASSKEY_REQ=0x34, SSP_COMPLETE=0x36, PASSKEY_NOTIFY=0x3b），LCPU 用**标准号**（0x23/0x24/0x25/0x26/0x28/0x2b）——必须桥接。
- LCPU 的 SSP 事件按 **handle 寻址**（且 handle 是**大端** 00 81），zblue 按 bdaddr 寻址。
- LCPU 认证流程：Auth Requested → Link Key Request → zblue neg reply（无 key）→ Auth Complete 0x05 失败 → 断开。**SSP 是唯一出路**（HyperOS 3.0.1.0 无 legacy PIN 弹窗）。
- zblue br_init 的 scan-activity 写入因 BT_DEV_READY 未置位返回 -EAGAIN 全部失败；Write_Inquiry_Mode 失败导致 br_init 提前 return（本地名字/EIR 未设置）。
- **bluetoothd 崩溃（bt_list_add_tail malloc NULL）**：/data/misc/bt storage 被写坏 → 每次启动前必须 rm -rf /data/misc/bt；崩溃后系统 assert 停机，需 USB 拔插。

## 各轮改动

- R53：uart_bth4 circbuf 溢出日志 / hci_h4 每帧 trace / zblue rx_work 入口日志
- R54：删除 NCP 误归一化；加 0x08 真加密事件与 ReadLocalFeatures 跟踪；br_init/Write_SSP_Mode 日志
- R55：PAN worker 跳过 set_security（PAN_SAL_SKIP_SECURITY=1，BNEP 无需加密链路）
- R56：bt_recv 逐事件日志；SSP 事件日志（io_capa_req/resp/user_confirm）
- R57：SSP 事件桥接（handle→addr 映射 + 标准号→zblue 号转换）+ rx_work 循环排空
- R58：修复桥接 handle 大端解析 → **io_capa_req 到达 zblue，0x042b 回复成功**（SSP 首次推进，但 LCPU 在 IO 交换后卡住）
- R59：发现 inquiry scan 从未开启（zblue enable 只开 page scan）
- R60：强制 inquiry scan on（0x0c1a 前置修改 data[3] |= 0x01）+ 补发 scan activity + 本地名字
- R61：inquiry 探针（板子主动扫描）→ **LCPU 能扫到手机**（Inquiry Result a4:d1:fe:b3:cc:a4）——射频 OK
- R62：inquiry scan 100% 占空比（0x0800/0x0800）+ interlaced 扫描类型 → **手机第一次看到板子（cd:ab:78:56:34:12）**

## 当前状态

- 板子跑 R62（含 R53-R62 全部补丁）
- 手机能搜索到板子（显示 cd:ab:78:56:34:12——LCPU 名字字段被存成地址；Write_Local_Name 未生效，待排查）
- 未完成：手机点击配对 → SSP 确认弹窗 → 配对 → link key → pan connect 加密 → BNEP → 上网

## 明天继续的入口

1. **干净会话流程**（重要！）：nsh 复位后 rm -rf /data/misc/bt → bluetoothd & → bttool → enable → **保持串口打开**（脚本不退出）→ 用户手机搜索点击配对 → 观察 SSP bridge 日志
2. enable 完整性问题：R62 末次会话 Adapter Name Cap:0 Mode:0（enable 不完整）——需确认 forcing inquiry 日志出现（inquiry scan 确认开启）
3. 手机点板子配对 → 期望：LCPU 发 0x23(IO_CAPA_REQ) → 桥接 → zblue 回 0x042b → User Confirm (0x25→0x33) → 手机弹窗确认 → Link Key → 加密 → PAN
4. 若手机配对弹窗出现但确认失败 → 检查 zblue capability（当前 0x03 DisplayOnly）与手机组合的 SSP 方法
5. 名字问题（次要）：Write_Local_Name 未生效——LCPU 名字字段 6 字节私有格式

## 文件改动记录（今日）

- nuttx/drivers/serial/uart_bth4.c：circbuf 溢出日志（已提交 688aa576924）
- vendor/sifli/chips/sf32lb52/sf32lb52_bth4.c（gitignored）：NCP 归一化删除/0x08跟踪/SSP桥接/强制inquiry/inquiry探针/scan activity兜底/本地名字
- external/zblue（apps/external 符号链接，gitignored）：rx_work 循环排空/bt_recv逐事件/br_init日志/ssp.c事件日志
- frameworks/connectivity/bluetooth（gitignored）：hci_h4 帧trace/ssp 跳过加密（sal_pan_interface.c）
- docs_ble/tools/zblue_pair_debug_patch.md：全部补丁记录

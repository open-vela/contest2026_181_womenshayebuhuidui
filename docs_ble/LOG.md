# docs_ble 验证日志

## Round 1（2026-08-14，代码级验证）

- 新建 bletest 分支（contest 仓 + packages/ai_agent 仓），旧内容完整保留（dev-branch 快照提交）。
- 结论 1：PAN/BNEP 不可用（zblue 无 BNEP SAL、无 CONFIG_BLUETOOTH_PAN、LCPU 固件不支持 BREDR）；
  BLE IPSP/6LoWPAN 暂不可用（zblue 无适配层、Android 不开放 IPSP）→ 自研 GATT NUS+TUN 代理是唯一路径。
- 突破 2：定位并修复设备端分片缺陷（ble_gatt_send 对 >MTU-3 报文直接 -EMSGSIZE，所有 IP 包被丢）→
  8×1600B 队列 + on_notify_complete 逐片发送。
- 突破 3：App 端补齐 MTU 协商（requestMtu(247) + 按 MTU-3 分片，回退 20B）。
- 突破 4：定位并修复 TUN 无默认路由缺陷（0.0.0.0/0 → 192.168.55.1）。
- 固件两轮编译通过（SRAM 463016/524288 = 88.31%）。
- 阻塞：① App 编译需可写 ~/.gradle 的环境；② 真机验证需连接开发板（当前无串口设备）。

## Round 2（2026-08-15，App 协议层修复 + 编译验证）

- 突破 5：解决 App 编译阻塞 —— GRADLE_USER_HOME 迁至工作区可写目录（2.7G 缓存拷贝），
  assembleDebug --offline 构建成功，MTU 协商修改编译通过。
- 突破 6：定位并修复 App TcpProxy 5 处协议缺陷（见 05_app_proxy_fixes.md）：
  A) 纯 ACK ack=0 → 设备数据永不确认、无限重传；B) ACK seq 用错流 → NuttX 整段丢弃；
  C) SYN-ACK 后 serverSeq 未 +1 → 首段数据错位截断；D) DNS 应答 src/dst 写反；
  E) 失败 RST ack=0 → 设备 SYN 重传超时；F) 新增 ICMP echo 应答（链路自检）。
- 突破 7：核对设备侧 NuttX 行为与代理假设一致 —— tcp_input.c 段接受逻辑
  （seq==rcv_nxt 才处理）、TUN 软校验和、UDP 校验和 0 豁免 —— 两端协议握手成立。
- 突破 8：定位设备端启动竞态 —— ble_gatt_net_init 只调用一次，bluetoothd 未就绪时
  GATT 通道永远起不来（手机无法连接）；改为 5 次 × 3s 重试（network_manager.c）。
- 固件：重试修复后 ninja 编译通过；App 修复后再次构建通过。
- 核对：GATT notify 目标按 UUID 匹配 value 属性（zblue foreach_attr），
  attr_handle+srv_id 元素索引一致，通知路径无隐患。
- 阻塞：真机验证仍需连接开发板；App 安装包 app-debug.apk 已生成
  （com.agent.coapp-main/app/build/outputs/apk/debug/）。

## Round 3（2026-08-15，主机端全链路仿真验证）

- 突破 9：编写 docs_ble/tools/tunnel_sim.py —— 无真机条件下的端到端协议仿真，
  精确镜像两端已修复逻辑（帧协议/分片/NuttX 严格 seq 接受/代理 ACK 语义/DNS 方向/ICMP）。
- 突破 10：仿真 5 项测试全部 PASS、0 丢包 —— HTTP GET、64KB 大文件（跨多帧+多分片+
  seq/ack 推进）、ICMP ping（MTU 247 与 MTU 23 回退路径）、DNS 转发（方向修复验证）。
- 意义：协议设计层已闭环；剩余验证项只剩真机 BLE 射频/协议栈行为（GATT 连接、
  MTU 协商、notify 完成回调时序、LCPU 固件兼容性）——需物理板卡。
- 突破 11：回归测试（legacy_bugs 模式复现 Round2 前缺陷）→ 旧代码 0B 数据 + 3 丢包，
  证明仿真有判别力、修复必要且有效。
- 突破 12：web 检索恢复可用，定位 zblue 栈 3 个未启用性能开关
  （AUTO_UPDATE_CONN_PARAMS / PPCP / AUTO_DATA_LEN_UPDATE）+ 收集 NUS 吞吐文献
  （连接间隔是 4KB/s 瓶颈的实锤案例等），整理为 07 文档 + 真机试验矩阵。
- 突破 13（真机，8-15）：**BREDR 闸门 0 通过** —— LCPU 固件支持 BREDR！
  真机实测：HCI RESET/Features/Version 全响应、适配器 ON（state 4）、
  get addr 返回 CD:AB:78:56:34:12、**inquiry 发现 BREDR 设备**（Inquiry Result
  事件 04 02 ... a4:d1:fe:b3:xx 重复上报）、SPP 栈初始化成功。
  推翻 docs/11 的"BREDR 不支持"结论（此前失败是 CRLF+僵尸会话工具链问题）。
- 踩坑记录（工具链，真机调试必读）：
  ① bttool 是交互式工具，命令不带前缀；② 发送命令必须只带 \n（\r 残留导致
  UnKnow command）；③ create instance error = bluetoothd 没启动（rcS 为空，
  需手动 bluetoothd &）；④ 僵尸 bttool 会话命令表为空连 quit 都无效，
  只能物理拔插 USB 复位（RTS 复位不可靠）。

## Round 4（2026-08-15，PAN 实现）

- 突破 14：确认手机 a4:cc:b3:fe:d1:a4 被 inquiry 发现（HCI 事件 LSB-first 字节
  a4 d1 fe b3 cc a4 反转即手机地址）—— BREDR 双向链路确认。
- 突破 15：**自研 BNEP SAL 层完成并编译通过**（docs_ble/10）：
  sal_pan_interface.c/h（~500 行）：L2CAP BR PSM 0x000F + Setup 握手 +
  FRAME_ETH 数据路径 + 状态机；CONFIG_BLUETOOTH_PAN=y 启用；
  panu_service/bt_pan/bt_socket_pan/tools-panu 全部激活。
  SRAM 90.35%（+10KB）编译通过。
- 待办：真机验证 pan connect 手机 NAP → bt-pan → 上网（固件已含 PAN，需烧录）。

## Round 4-2（2026-08-15 深夜，PAN 真机调试）

- 突破 16：**L2CAP BR PSM 0x000F 连接被手机接受**（docs_ble/11）：
  完整链路 ACL up → 加密 level 2 → L2CAP connect ret=0 → 手机 CONN_RSP
  result=0x0000 SUCCESS（dcid=0x0072）。阻塞推进到 CONFIG 阶段。
- 修复 1（EEXIST 根因）：pan_conn_t.chan 类型错误——zblue BR_CHAN() 宏按
  bt_l2cap_br_chan 布局越界访问 psm（calloc 垃圾值非零）→ 改 bt_l2cap_br_chan。
- 修复 2：worker ACL up 等待 3s→10s 轮询（远端 page 可超 3s）。
- 修复 3（最新）：br_chan rx.mtu 未初始化=0 → l2cap_br_conf 发 MTU=0 配置 →
  手机回 CONF_RSP UNACCEPTABLE_PARAMS → 断链。已初始化 rx.mtu=672 + sec L2。
- 修复 4：LCPU bth4 RX/TX 打印 8→40 字节（可见完整 L2CAP 信令）。
- 待办（明天）：抓我方 CONF_REQ 完整字节分析被拒原因（嫌疑：MTU option、
  zblue conf 交互、或需显式 MTU=672 option）；目标 CONFIG 通过 → BNEP Setup →
  state:2 CONNECTED → 数据面。
- 环境要点：手机「蓝牙网络共享」必须开启（否则手机直接断 ACL reason 0x13）；
  烧录用 logs/flash_rts.py + for 重试 2-3 次（RTS 时序不稳）。

## Round 4-3（2026-08-15 深夜，加密关口根因定位）

- 突破 17：**加密不完成的根因 = link key 不持久化**。
  完整 HCI 解码（/tmp/pan_raw.log 17:45 trace）：
  [pan] worker: request encryption
  01 11 04 02 80 00        = Authentication_Requested (0x0411) h=0x0080
  04 0f 04 00 ...          = CC status=0
  04 16 06 a4 d1 fe b3 cc a4  = Link_Key_Request (0x16) —— 控制器无此设备密钥
  01 0c 04 06 a4 d1 fe b3 cc a4  = Link_Key_Neg_Reply (0x040C) —— zblue RAM 也无密钥
  [bttool] Pair Display [A4:CC:B3:FE:D1:A4][BREDR][PIN] please reply:
  —— 降级 legacy PIN 配对，等控制台输 PIN（无人应答）
  [pan] worker: timeout, L2CAP anyway → 未加密通道发 CONNECT_REQ → 手机拒绝
- 根因链：zblue 的 BR link key 只存 RAM（br_key_pool，CONFIG_BT_MAX_PAIRED=1）；
  持久化只在 CONFIG_BT_SETTINGS 下生效，而本移植（external/zblue port）没有
  settings 子系统（port/subsys 无 settings）→ 每次重启密钥即失 → 每次连接都要
  重新 legacy PIN 配对。控制器（LCPU 闭源固件）也不存 key，回 Link_Key_Request
  向 host 要。
- 对策（无需改代码即可验证）：**同一启动内先配对再 pan connect** —— 配对完成
  后 zblue RAM 里有 key，Link_Key_Request 会被 16 字节 key 应答 → 加密完成 →
  security_changed(level=2) → L2CAP → BNEP。zblue 路径已验证：
  ssp.c bt_hci_link_key_req → conn->br.link_key || bt_keys_find_link_key() →
  link_key_reply()；配对 store 点在 ssp.c:531 / smp.c:890。
- 配对应答链路：bttool pair pin <addr> 1 0000 → bt_device_set_pin_code_async →
  adapter_set_pin_code（要求 BOND_STATE_BONDING）→ bt_sal_pin_reply →
  zblue zblue_pin_reply → bt_conn_auth_pincode_entry(conn,"0000")。
- 板载 TAP 桥已完整：panu_service.c pan_tap_bridge_open("/dev/tun", IFF_TAP)
  → "bt-pan" 网卡（MAC=本机 BT 地址）；BNEP FRAME_ETH ⇄ TAP 读写闭环
  （pan_tap_poll_data → bt_sal_pan_write）。
- 待办：① 配对成功后同启动内 pan connect，验证 encryption→BNEP→bt-pan；
  ② 持久化方案（文件式 BR key store，port 层挂钩 bt_keys_link_key_store /
  bt_hci_link_key_req 加载）——突破后实现；③ 上网验证（镜像无 ping/dhcpc，
  需加 CONFIG_NETUTILS_PING 或静态 IP + 包计数）。

## Round 4-4（2026-08-15 晚，可发现性根因与卡死真相）

- 突破 18：**BR「可发现」失效根因 = Write_Scan_Enable 被 H4 驱动 emulate 吞掉**。
  sf32lb52_bth4.c sf32lb52_bt_emulate_cmd() 把 WRITE_SCAN_ENABLE / WRITE_PAGE_SCAN_ACTIVITY /
  WRITE_INQUIRY_SCAN_ACTIVITY / WRITE_INQUIRY_SCAN_TYPE / WRITE_PAGE_SCAN_TYPE /
  WRITE_EIR / WRITE_LOCAL_NAME / WRITE_SSP_MODE / WRITE_CLASS_OF_DEVICE 等整套
  BR 配置命令合成成功响应（不转发 LCPU）→ zblue 以为已设置，控制器实际没开
  inquiry/page scan → 手机列表永远看不到板子（与设置 scanmode 无关，
  adapter_set_scan_mode 缓存命中/EALREADY 静默，且从未有 HCI 命令发出）。
  证据：set scanmode 2 时 bth4 trace 无任何 HCI；而 inquiry 命令（不 emulate）正常。
- 突破 19：**Sifli 官方 SDK 证实 LCPU 支持 BR scan/PAN**：
  docs.sifli.com SDK 的 BT PAN Example（sf32lb52x/example/bt/pan）：
  「The example will enable Bluetooth Inquiry scan and page scan at startup, allowing
  phones and other devices to discover and connect to this device」——手机网络共享开启后
  PAN 自动连接，finsh 命令 pan_cmd conn_pan / weather / pan_cmd ota_pan（BT PAN 下载 OTA）。
  官方示例同样需要手机开「蓝牙网络共享」，默认名 sifli_pan。
- 突破 20：**BLE 广播失败根因 = bttool 的 adv type 语义**：`-t adv_ind` 是 ext 语义
  （convert 强制 BT_LE_ADV_OPT_EXT_ADV），而控制器 LE 特性被 emulate 返回全 0
  （LE_READ_LOCAL_FEATURES 合成全零）→ ext 不支持 → STACK_ERR(status:3)。
  需 `-m legacy`（adv_mode=1 → adv_type += BT_LE_LEGACY_ADV_IND → legacy 路径）。
- 突破 21：**系统卡死真相 = ai_agent 启动崩溃破坏 mm 锁**：
  本会话「!ai_agent &」启动 ai_agent → 立即 assert（ai_agent_main → mallinfo →
  mm_foreach assert，内存不足）→ 后续 bluetoothd 全部 IPC 无响应、串口静默
  （bttool 卡在 system()/IPC）。与 adv 无关。教训：不要在 bttool 会话里启动 ai_agent。
- 其它发现：bluetoothd 的 BT_LOGE 默认编译为空（CONFIG_BLUETOOTH_SERVICE_LOG_LEVEL
  未定义）→ 需在 defconfig 开启才能看 SAL 诊断日志；ncmd_sem 机制（CC 响应 ncmd=0
  不 give → 后续命令停摆）在 emulate 合成响应中 ncmd=1 正常。
- 下一步（用户在场）：① 物理拔插 USB 重启（RTS 复位无效）；② 不要启动 ai_agent；
  ③ `adv start -t adv_ind -m legacy -n Agent-Watch` 开 BLE 广播 → 手机配对(0000) →
  ④ pan connect → 加密(link key 在 RAM) → BNEP → bt-pan → 上网；
  ⑤ 备选：改 vendor bth4 移除 WRITE_SCAN_ENABLE emulate（BR 可发现，官方 SDK 证实
  LCPU 支持），需重编译烧录。

## Round 4-5（2026-08-15 晚，双通道可发现突破）

- 突破 22：**WRITE_SCAN_ENABLE 转发生效**：新固件（bth4 移除 emulate）烧录后，
  `set scanmode 1/2` 真正下发 HCI：`04 0e 04 06 1a 0c 00` =
  Command Status status=0 opcode 0x0C1A（Write_Scan_Enable）——LCPU 接受！
  控制器 inquiry+page scan 开启 → 板子 BR 可发现（Round 4-4 根因修复验证成功）。
- 突破 23：**BLE legacy 广播成功**：`adv start -t adv_ind -m legacy -n Agent-Watch`
  → `04 0e 04 06 0a 20 00`（LE_Set_Advertising_Parameters 0x200A status=0）→
  `[adv] zblue_start_adv: zblue start OK`（新加 syslog）→
  `on_advertising_start_cb status:0` SUCCESS！之前失败因 `-t adv_ind` 是 ext 语义。
- 修复过程记录：flash_rts.py 烧录后板子进 DFU（msh/RT-Thread，dfu_pan）——
  sftool soft_reset 后 boot 链进 dfu_pan；恢复方法：写 openvela_ftab.bin@0x12000000
  （ftab[3]/[7]→0x12010000）+ nuttx@0x12010000（flash_openvela_ftab.py，Round 4-2 流程）。
- 当前状态：BR 可发现 + BLE 广播双通道就绪，等待手机配对（PIN 0000）。

## Round 6-7（2026-08-15 深夜 ~ 08-16 凌晨，加密定位 + SSP 桥接 + 可发现突破）

- 突破 24：**04 13 是 Number-of-Completed-Packets 而非 EncryptChange**——R52 的"归一化"
  破坏合法事件（num 1→0），已删除；真加密事件 0x08 在认证失败路径从不出现。
- 突破 25：**rx_work 事件滞留**——zblue rx_work_handler 单事件+自提交模式在本移植
  丢事件（运行中 k_work_submit 不保证重调度）→ 改为 while 循环排空（0x23 事件曾
  进 bt_recv 但永不处理）。
- 突破 26：**SSP 事件号桥接**——LCPU 用标准号（0x23 IO_CAPA_REQ 等）且按 handle
  （大端）寻址，zblue 用偏移号（0x31/0x32/0x33/0x34/0x36/0x3b）按 bdaddr 寻址；
  bth4 建立 handle→addr 映射并重写事件 → **zblue 首次收到 io_capa_req 并成功回复
  IO_CAPABILITY_REPLY (0x042b)**——SSP 配对流程第一次真实推进（LCPU 随后卡在 IO 交换）。
- 突破 27：**可发现性最终修复**——zblue enable 只开 page scan（0x0c1a 参数 0x02）；
  bth4 强制 inquiry 位 + 100% 占空比 scan activity（0x0800/0x0800）+ interlaced 扫描
  类型 → **手机首次在蓝牙列表看到板子（cd:ab:78:56:34:12 = 板子 BR 地址）**。
- 突破 28：**inquiry 探针验证射频**——bth4 enable 后自动发 GIAC inquiry（10s）→
  LCPU 返回 Inquiry Result（手机 a4:d1:fe:b3:cc:a4）——BR 收发链路完全正常。
- 发现：bluetoothd bt_list_add_tail malloc NULL 崩溃（storage 写坏）→ 每次测试前
  rm -rf /data/misc/bt；崩溃 assert 停机需 USB 拔插恢复。
- 现状：手机可搜到板子但尚未完成配对；LCPU 名字字段存地址（Write_Local_Name 未生效）。
- 下一步：手机点板子配对（SSP 弹窗确认）→ link key → pan connect（跳过加密已实现）
  → BNEP → bt-pan → 上网。详见 docs_ble/15_r52_r62_breakthrough.md。

## Round 8（2026-08-17 下午，恢复调试固件 + inquiry 双缺陷修复）

- 背景：用户反馈"从未连接上板子，配对时显示无法通信"。探针发现板上跑的是无蓝牙的
  交付固件（bluetoothd/bttool 均不在 builtin 列表）→ 重烧 R62 调试固件恢复环境。
- 突破 29：**inquiry 崩溃根因 = zblue 写死 num_rsp=0xff + LCPU 拒答 + 断言致死**：
  ① br.c:1033 `cp->num_rsp = 0xff`，本 LCPU 固件对 num_rsp=0xff 的 Inquiry 连
  Command Status 都不回（R61 注入 num_rsp=0x00 却有 Inquiry Result）——BT 规范
  0x00 才是 unlimited；② 超时后 bt_hci_cmd_send_sync 的 BT_ASSERT 直接 Kernel
  oops 杀死 bluetoothd（sysworkq 上下文）。此前 R61 探针注入的 inquiry 与栈自身
  discovery 重叠加剧了 LCPU 沉默。
- 修复 A：vendor bth4 移除 R61 探针注入（诊断使命完成，纯致害）。
- 修复 B：zblue hci_core.c bt_hci_cmd_send_sync 两处 BT_ASSERT → 返回 -ETIMEDOUT
  （控制器超时是错误不是致命故障，bluetoothd 必须存活以便重试）。
- 修复 C（R63）：bth4 发送路径拦截 Inquiry(0x0401)，num_rsp!=0 强制改写为 0x00。
- 真机验证进展：R63 首跑 enable 阶段 0x1009(Read_BD_ADDR) 超时（LCPU 偶发沉默，
  前两把均正常应答）→ 二跑撞上已知 bt_list_add_tail 断言（kill bluetoothd 路径）
  → RTS 复位不彻底板子静默。待 USB 拔插后继续：干净会话 → enable → inquiry →
  createbond（预期 SSP 事件桥接链路首次完整走通）。
- 已知规避：不 kill bluetoothd（用断电获得干净会话）；探针脚本 ssp_trace_probe.py。

## Round 9（2026-08-17 晚，配对完全打通 🎉 —— "无法通信"总根因歼灭战）

- 突破 30：**板上固件曾被主线交付版覆盖**（无 bluetoothd/bttool）→ 重烧调试固件恢复。
- 突破 31：**inquiry 双缺陷修复**：
  ① zblue br.c num_rsp 写死 0xff，LCPU 对 0xff 拒答（连 CS 都不回）→ bth4 改写为 0x00；
  ② send_sync 超时 BT_ASSERT 直接 Kernel oops 杀死 bluetoothd → 改为返回 -ETIMEDOUT。
- 突破 32：**R59/R62 注入的 scan-activity 命令引发 LCPU Hardware Error(04 10 01 00)**
  并使其对所有 BR 操作沉默 → R64 全部移除（保留纯参数改写的 scan enable inquiry 位）。
- 突破 33：**R57 桥接表根本性错误**：0x23 是标准 Read_Remote_Extended_Features（13 参数
  完全吻合），被误当 io_capa_req 劫持 → zblue 永远等不到 features 完成 → 配对流程根本
  不启动。R65 重写为纯事件号映射（0x24→0x31, 0x25→0x32, 0x26→0x33, 0x27→0x34,
  0x29→0x36），参数原样透传（两边都是 bdaddr 寻址）。
- 突破 34：bt_list_add_tail malloc 失败断言杀 daemon → 改为降级告警（SRAM 90.9% 逼近）。
- 突破 35：**手机侧发起配对也失败（LMP 0x22）→ 排除"角色"因素，锁定事件流本身**。
- 突破 36（终极根因）：**zblue 的 Set_Event_Mask 用偏移事件号算位（IO_CAPA_REQ=BIT(48)…），
  而 LCPU 是标准控制器，SSP 事件 0x24..0x2b 对应标准位 35..42** → SSP 事件全部被
  mask 屏蔽 → LCPU 从不上报 IO_Capability_Request → host 无法应答 → 认证超时失败。
  板侧发起=Auth Complete 0x05；手机发起=LMP Response Timeout 0x22（手机显示"无法通信"）。
  与 Round 4-3 以来所有现象吻合（旧固件 emulate 掩盖了此问题——mask 从未真正下发）。
- 修复 R69：bth4 send 顶层拦截 Set_Event_Mask(0x0c01)，强制置位标准 SSP 位
  （octet4 |= 0x78, octet5 |= 0x05），并把 0x0c01 从 emulate 表移入"转发+合成CC"名单。
- **战果（2026-08-17 20:24，首次完整 SSP 配对）**：
  io_capa_resp(手机 cap=1 auth=3) → io_capa_req → user_confirm_req passkey=633589
  （bttool g_auto_accept_pair=true 自动确认，手机侧亦有弹窗）→
  **BOND_NONE → BONDED** → Link_Key_Notification(0x18) → **br_key store: 1 key(s)**
  （P2 文件持久化闭环，/data/misc/bt/br_key.bin）。
- 待办：① PAN e2e（pan connect→bt-pan→dhcp→ping）——首跑撞上串口挂死+烧录 rc=101，
  板子需 USB 拔插后继续；② 重启后验证 br_key.bin 免配对回连；③ P4 应用层状态机。
- 工具沉淀：ssp_trace_probe.py（createbond 全事件抓取）、phone_pair_watch.py（被动监听）、
  legacy_pin_pair.py、pan_e2e.py。

## Round 9（2026-08-18/19，BNEP 全链路打通 + 配对根因总修复）

### 阶段1：蓝牙栈稳定性修复
- R64：移除 R61 探针注入（与栈 inquiry 重叠导致 LCPU 静默 + Kernel oops）
- R65：重写 SSP 事件桥接——原 R57 把 0x23（Read_Remote_Ext_Features，标准事件）误当
  io_capa_req（LCPU 私有）→ 桥接劫持了 zblue 原生事件 → zblue 永远等不到 features
  完成 → createbond 不启动。改为纯事件号重映射（0x24→0x31, 0x25→0x32, 0x26→0x33,
  0x27→0x34, 0x29→0x36），0x23/0x28/0x2b 不再劫持。
- R66：bt_list OOM 断言改为降级告警（SRAM 90% 边缘 malloc 失败不再杀 daemon）
- R69：zblue Set_Event_Mask 用偏移位 BIT(48..53)，LCPU 标准控制器需 BIT(35..42) →
  SSP 事件被屏蔽。bth4 拦截 0x0c01 强制标准位（octet4|=0x78, octet5|=0x05）+ bit7
  (0x08 Encrypt Change)。
- R69 发现：Set_Event_Mask 在 emulate 表里被合成 CC 直接返回，改写代码永远执行不到 →
  移出 emulate 表到"转发+合成 CC"名单。

### 阶段2：BNEP 连接与 SDP
- R76：注册 PANU SDP Service Record（BT_SDP_PANU_SVCLASS + PSM 0x000F + BNEP 协议描述）。
- R85：L2CAP deferred 机制——板子 BNEP 通道先 LCONF_DONE → 等手机通道也 CONNECTED
  再发 BNEP setup，避免信号通道死锁。
- R89/R91：SDP record 精简——移除 SupportedNetworkAccessTypeList/SupportedFeatures 避免
  zblue SDP server continuation state 格式错误（03 f0）导致手机误判 PANU 不完整。
- R94：BNEP Setup Request 改用 128-bit NAP UUID（00001116-0000-1000-8000-00805f9b34fb）。
- R96：接受 HyperOS 4字节 BNEP Response（标准 6 字节），单字节状态码解析。
- **R96 后状态**：手机 BNEP Response status_byte=0x00 → 视为 SUCCESS → bt-pan 接口 UP。

### 阶段3：LCPU 硬件限制与 Inquiry
- inquiry scan activity 注入（Write_Inquiry_Scan_Activity）任何值均触发 Hardware Error 0x00
  → 完全移除注入。只靠 Write_Scan_Enable inquiry bit 强制开启（R60）。
- Write_SSP_Mode=1 后 LCPU 不走 legacy PIN（原 Round 4-3 有 PIN 弹窗），SSP IO 交换
  因 mask 位错长期未生效。R69 修正后 SSP 数字比较成功。
- inquiry num_rsp 0xff → 0x00（bth4 拦截）：LCPU 对 0xff 不回 CS。

### 阶段4：PAN 数据面验证（R99，2026-08-19 首次完整 e2e）
- 配对：io_capa_req(0x24→0x31 桥接) → io_capa_resp → user_confirm_req(passkey) → BONDED
- 加密：encrypt_change enc=1 + security level=2
- BNEP：conf_rsp SUCCESS → chan_connected → BNEP setup(128-bit NAP UUID) → 手机 Response
  status=0x00 → `pan_netif_state_cb ifname:bt-pan, state:1`（接口 UP）
- 待完成：bt-pan DHCP + ping（窗口期脚本问题——BNEP 在等待期建立后手机超时断链）

### 当前阻塞
- 手机侧"无法通信"已彻底解决（R69 mask + R65 桥接 + R96 接受 4 字节 Response）。
- BNEP 连接已稳定建立（R99 验证）。
- **当前卡点**：bt-pan UP 后需要立即跑 DHCP/ping，但手机约30s 后超时断链。
  需要写一个 BNEP 成功瞬间立即切 nsh 的脚本（pan_instant.py 已写，检测逻辑修正后待测）。
- 另一个方案：手机蓝牙共享设置里授权设备（HyperOS 可能需要显式授权该设备上网）。

## Round 10（2026-08-20，开机自启后的 HardFault 根因清理）

- rcS 改为自动 `bluetoothd &` + `ai_agent &` 之后，开机约 2 s 必崩：
  `BT LW WQ` 线程（process: bluetoothd）在 arm_hardfault.c:186 断言，控制台随后静默，
  只能物理拔插 USB 才能重新烧录。
- 调试镜像补上故障诊断开关（`DEBUG_HARDFAULT_ALERT`/`BUSFAULT`/`USAGEFAULT`/
  `ARCH_STACKDUMP`）与 `BOARD_RESET_ON_ASSERT=2`——断言后自动重启，既能重复取日志，
  也自动重开 SFBL 下载窗口。改动走 `tools/mk_pandbg_config.sh` 生成器，不用
  savedefconfig（它会 copy_if_different 回源 defconfig 并抹掉注释）。
- 根因：zblue 的 `z_sys_init()`（SYS_INIT 表）被跑了两遍——`sf32lb52_bt_initialize()`
  开机跑一次（group 0），`bt_sal_init()` 在 bluetoothd 里再跑一次（group 8）。
  表里的 `k_sys_work_q_init`/`long_wq_init` 用 `K_THREAD_STACK_DEFINE()` 的静态数组建
  线程（移植层 `pthread_attr_setstack`），第二遍等于两个线程共用同一块栈，且
  `k_work_queue_start()` 会重初始化已有等待者的信号量。dump_tasks 里两组 sysworkq /
  BT LW WQ 的 STACKBASE 完全相同，是直接证据。
- 修复：① bth4 不再调 `z_sys_init()`（HCI 的 fd 在 `h4_open()` 里拿，NuttX fd 表按
  task group，必须留给 bluetoothd）；② `z_sys_init()` 按 owner pid 探活幂等
  （不能用一次性标志：工作队列线程是调用者的 pthread，随进程一起死，bluetoothd
  重启后必须重跑）。
- 连带线索：bth4 里「为绕开 sysworkq HardFault 才把 SSP 搬进驱动」的历史 workaround，
  很可能是同一根因；按最小修复先行本轮不动。
- 详见 `18_zblue_sys_init_duplicate_fix.md`。

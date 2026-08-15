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

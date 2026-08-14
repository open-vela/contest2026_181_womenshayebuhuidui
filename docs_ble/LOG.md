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

# 10 PAN 实现：BNEP SAL 层（Round 4，2026-08-15）

> 前置：09 文档闸门 0 通过（BREDR 可用）。本文档记录 BNEP 协议栈实现与验证。

## 实现内容（代码级完成 + 编译通过）

| 文件 | 内容 |
|------|------|
| service/stacks/include/sal_pan_interface.h | bt_sal_pan_init/cleanup/connect/disconnect/write 接口 + BNEP 常量 |
| service/stacks/zephyr/sal_pan_interface.c | **自研最小 PANU BNEP 栈**（~500 行）： |
|  | - L2CAP BR PSM 0x000F 通道（zblue public API bt_l2cap_chan_connect/send） |
|  | - BNEP Setup Connection Request/Response 握手（PANU→NAP: dst_role=1, src_role=2） |
|  | - 数据路径 BNEP_FRAME_ETH (0x00) + EtherType + payload |
|  | - 接收重建以太网帧（src=对端 BT 地址, dst=广播）供 TAP 桥 |
|  | - 连接状态机 ACL→L2CAP→BNEP→CONNECTED |
| CMakeLists.txt | CONFIG_BLUETOOTH_PAN 编译 sal_pan_interface.c |
| board defconfig | CONFIG_BLUETOOTH_PAN=y（依赖 ALLOW_BSD_COMPONENTS ✓, NET_TUN_PKTSIZE=1518 ✓） |

框架侧（原本就有，现被激活）：panu_service.c（TAP 桥 bt-pan）、framework/api/bt_pan.c、
IPC socket、bttool pan 命令（tools/panu.c）。

## 编译

- sal_pan_interface.c + panu_service.c + tools/panu.c 全部编译通过
- SRAM 473684/524288 = 90.35%（PAN 增加约 10KB，预算内）
- 注意：defconfig 变更后需删除 out 目录 .config 重新 cmake（ninja 不会自动重配）；
  PATH 需含 prebuilts/tools/linux/x86_64（genromfs）

## 设计取舍（最小可用版）

- 数据帧不带 MAC 扩展头：发送忽略 dst/src MAC；接收 src=peer BT 地址、dst=广播。
  TAP 桥 + 单客户端 NAP 场景足够；若有问题再实现扩展头（0x7F 01/02）。
- 未注册 L2CAP server（PANU 只出站连接；入站请求直接回拒绝 0x0003）。
- 控制通道仅实现 Setup（Filter/MultiAddr 未实现，Android NAP 通常不需要）。

## 真机验证步骤（下一步）

1. 烧录：`./build_and_flash.sh flash -p /dev/ttyACM0 -i out/openvela_contest2026_181_board_ai_agent/nuttx.bin`
2. 手机开启"蓝牙网络共享"（设置→热点与网络共享→蓝牙网络共享）
3. 串口：bluetoothd & → bttool → enable → set scanmode 2
4. 配对（手机搜索 Agent-Watch 配对，或板子 createbond <手机addr> 1）
5. `pan connect <手机地址> 1 2`（dst_role=1=NAP, src_role=2=PANU）
6. 观察：ifconfig bt-pan（应 up）、panu 回调日志
7. 验证：ping 8.8.8.8 → DNS → HTTP（经手机 NAT 出网）

# 04 真机联调验证步骤（下一步）

> 前置：本分支已含分片修复固件（out/openvela_contest2026_181_board_ai_agent/nuttx.bin
> 已重新编译通过）。App 端修改已就位，需在可写 ~/.gradle 的环境编译安装。

## 0. 编译与烧录

```bash
cd /home/aila/projects/vela_contest
# 固件（已编译，如需重编）
cd out/openvela_contest2026_181_board_ai_agent && ninja -j8
cd /home/aila/projects/vela_contest
./build_and_flash.sh flash -p /dev/ttyACM0 -i out/openvela_contest2026_181_board_ai_agent/nuttx.bin

# App（需可写 gradle 环境）
cd com.agent.coapp-main
JAVA_HOME=/usr/lib/jvm/java-17-openjdk-amd64 ./gradlew assembleDebug
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

## 1. 设备端检查（串口 NSH）

```
ai_agent            # 启动 agent（自动走 BLE GATT 通道）
# 预期日志：
#   [ble_gatt] step 1..4 ... advertising started OK
#   [ble_gatt_net] BLE GATT network channel ready
ifconfig bt-gatt   # 连接前 down；手机连上后应为 up 192.168.55.2
ble_gatt_test status   # connected=0 mtu=23（连接后 mtu 应随协商变化）
```

## 2. 手机 App 连接

1. 打开 App → BLE 连接页 → 扫描到 "Agent-Watch" → 连接。
2. 预期：设备串口出现 "GATT connected: xx:xx..."；App 显示 "隧道已建立（MTU=247）"。
3. 若 MTU 未到 247：确认手机 BLE 栈支持 requestMtu（Android 8.0+ 应支持）。

## 3. 网络穿透验证（核心验收）

```
# 设备侧（NSH）
ping -c 3 192.168.55.1          # 网关 = 手机，应通（第一跳验证）
ping -c 3 8.8.8.8               # 公网 IP 穿透（App NAT 出网）
ping -c 3 www.baidu.com         # DNS 穿透（设备 DNS → 手机）
# agent 内验证（LLM 对话即终极验收）
ai_agent 里直接对话 → LLM 响应经蓝牙代理回来
```

## 4. 性能与稳定性

- 吞吐：设备侧 time curl 下载 1MB 文件（经代理），记录 BLE 速率。
- 稳定性：长连 30 分钟 + 反复断连重连 20 次，观察 TX 队列丢帧日志
  （"[ble_gatt] TX queue full, dropping frame"）与 notify 失败率。
- 弱网：手机切 4G/热点对比。

## 5. 通过标准

- [ ] ping 通网关 192.168.55.1
- [ ] ping 通公网 IP（8.8.8.8）
- [ ] DNS 解析公网域名成功
- [ ] LLM 对话（HTTPS 到云端）经蓝牙代理完整往返
- [ ] 断连重连后无需重启 agent 即恢复

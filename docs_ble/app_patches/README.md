# app_patches — App 端已修改文件快照

> 来源：com.agent.coapp-main（独立 Android 工程，不在本仓 git 管理内）
> 修改时间：2026-08-15（bletest Round 2）

将以下两个文件**原样覆盖**回 App 工程对应路径即可获得全部修复：

| 文件 | 覆盖目标 | 修复内容 |
|------|----------|----------|
| BleTunnelManager.kt | app/src/main/java/com/agent/coapp/ble/ | requestMtu(247) + 按 (MTU-3) 动态分片（原硬编码 20B） |
| TcpProxy.kt | app/src/main/java/com/agent/coapp/vpn/ | ACK ack=0 → clientAck；ACK seq → serverSeq；SYN-ACK 后 serverSeq+1；DNS 应答 src/dst 修正；失败 RST ack=clientSeq+1；新增 ICMP echo 应答 |

编译验证（需可写 GRADLE_USER_HOME）：

```bash
cd com.agent.coapp-main
GRADLE_USER_HOME=/home/aila/projects/vela_contest/.gradle_home \
  JAVA_HOME=/usr/lib/jvm/java-17-openjdk-amd64 \
  ./gradlew assembleDebug --offline
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

已生成安装包：app/build/outputs/apk/debug/app-debug.apk（15.8MB，debug）。

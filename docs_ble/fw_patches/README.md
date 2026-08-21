# fw_patches — BT 框架已修改文件快照

> 来源：`frameworks/connectivity/bluetooth`（openvela 上游 drop，**不在任何仓的 git
> 管理内**：`frameworks/connectivity/.gitignore` 第 7 行是 `/*/`，把所有一级子目录
> 都忽略了，该仓实际只跟踪 20 个 CI 模板文件）
> 修改时间：2026-08-21（Round 11，PAN 上网打通）

把下面两个文件**原样覆盖**回对应路径即可获得本轮全部 PAN 修复：

| 文件 | 覆盖目标 | 修复内容 |
|------|----------|----------|
| `sal_pan_interface.c` | `frameworks/connectivity/bluetooth/service/stacks/zephyr/` | BNEP TX 单次分配 + 100 ms 等待；去掉遗留诊断日志；新增 `tx_nobuf` 计数；`pan_tx_pool` buffer 数 4→2（腾 3,480 B `.bss` 给堆，见 21 号文第 3 节）；补上「新增 net_buf 池必须同时注册进 `_net_buf_pool_list[]`」的注释 |
| `panu_service.c` | `frameworks/connectivity/bluetooth/service/profiles/pan/` | RX 侧只记 ARP / DHCP / 写 TAP 失败（其余是 mDNS/ND 噪声）；`last_nap` 缺失时从 bond list 回退发起自动连接；`PROFILE_STATE_CONNECTED` 时回写 `last_nap` |

## 为什么用快照而不是 patch

这两个文件在本轮之前就已经有大量未入库的本地修改（Round 5–10 累积），拿不到一个
干净的 upstream 基线来生成 diff。整文件快照是唯一能保证「覆盖即等价」的形式，与
`app_patches/` 同一套做法。

## 验证

覆盖后重建正式镜像：

```bash
export PATH="$PWD/prebuilts/gcc/linux-x86_64/arm-none-eabi/bin:\
$PWD/prebuilts/build-tools/linux-x86_64/bin:\
$PWD/prebuilts/kconfig-frontends/bin:$PATH"
cd out/nuttx_contest_board_ai_agent && ninja -j8
```

期望：SRAM 474,360 B / 90.48%；烧录后冷启动自动出现
`[pan] state=dhcp_ok dev=bt-pan ip=...`。

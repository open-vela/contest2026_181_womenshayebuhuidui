# 16: 真机语音验证 — 烧录与麦克风验证计划 (08-16)

## 现状
- 软件全部就绪: 音频驱动(修复版) + 语音模块(FFT bug 已修, 主机 81.9-84.3%) + 完整固件 7.8MB
- **烧录阻塞: USB 链路硬件级不稳定** (WCH USB-Serial 桥 1a86:55d3)
  - 今日 16:43-16:48 拔插后 stub 可下载, 7.8MB 传输 20%~99% 时断链 (Broken pipe/No such device)
  - 19:2x RTS 电源复位(仅复位芯片, 不重置桥) → stub 下载必超时
  - 结论: 必须物理拔插 USB (重置 WCH 桥), 软件无法复位该桥
- sftool 0.2.5 协议已逆向确认: 0x7E 0x79 帧, Enter="ATSF32\x05!", 0xD1 应答;
  stub 下载到 0x2005A000, BKP0R=0xA640, PA21 置位后运行; stub 跑 msh 类 shell,
  烧录命令 burn_write/burn_erase + OK/RX_WAIT 应答

## 烧录顺序 (链式脚本 /tmp/chain_flash.py 自动执行, 等待拔插)
1. **audio_test 小镜像 948KB** (音频驱动+audio_setup+nxrecorder+NSH, 传输~10s, 成功率最高)
   - 构建: 裁剪 defconfig 存于 app/ai_agent/tools/audio_test_defconfig
   - 验证: audio_setup → /dev/audio/audio0; nxrecorder 录音 → hexdump 导出 → 主机重建 PCM → 跑 ASR
2. **完整镜像 7.8MB** (语音模块+TFLM+LLM+GUI, 传输~80s, 需链路稳定/再次拔插)

## 真机验证步骤 (小镜像烧录成功后)
```sh
nsh> audio_setup                              # 期望: /dev/audio/audio0 ready
nsh> ls /dev/audio                            # 期望: audio0
nsh> nxrecorder recordraw /data/mic.pcm 1 16 16000 3   # 录 3 秒
nsh> hexdump -C /data/mic.pcm | head -20      # 期望非全零 (说话时)
# 主机: hexdump 输出 → /tmp/reconstruct_pcm.py → /tmp/dump_c2 → ASR 判定
```

## 若小镜像烧录成功但完整镜像失败
- 板子运行 audio_test 固件 (NSH only, 无 GUI) — 麦克风功能仍可验证
- 完整镜像待链路好转 (换线/换口/外接供电) 后再烧

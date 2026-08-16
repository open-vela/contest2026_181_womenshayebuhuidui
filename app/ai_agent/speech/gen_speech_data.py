#!/usr/bin/env python3
"""生成端侧命令词识别训练数据 (edge-tts 中文合成 + 增强)

输出: data/ (每类一个目录, 16kHz/16bit/mono raw PCM, 1.0s 对齐)
  data/raw/    - 原始合成 (多种音色 x 语速 x 文本变体)
  data/train/  - 增强后训练样本
  data/val/    - 验证样本 (不同语速/音色, 不参与增强)

用法:
  uv run python gen_speech_data.py [--n-unknown 300]

依赖: edge-tts, imageio-ffmpeg (bundled ffmpeg), numpy
"""
import argparse
import os
import random
import subprocess
import sys

import numpy as np

SR = 16000
WIN_SEC = 1.0          # 特征窗口
N_FRAMES = 96          # 10ms hop 的帧数 (stride 2 -> 48 帧特征)
N_MEL = 40

VOICES = [
    "zh-CN-XiaoxiaoNeural",
    "zh-CN-YunxiNeural",
    "zh-CN-YunjianNeural",
    "zh-CN-XiaoyiNeural",
    "zh-CN-YunyangNeural",
    "zh-CN-XiaohanNeural",
    "zh-CN-XiaomengNeural",
    "zh-CN-liaoning-XiaobeiNeural",
]

RATES = ["-15%", "-8%", "+0%", "+8%", "+15%"]

# unknown 类: 与命令无关的随机中文短语 (拒绝样本)
UNKNOWN_PHRASES = [
    "今天中午吃什么", "给我讲个故事", "帮我订一张机票", "明天会下雨吗",
    "我想听相声", "打开手机", "给我发个微信", "导航去机场",
    "帮我拍张照片", "搜索一下", "这个多少钱", "你好漂亮",
    "我爱你", "天气不错", "周末去爬山吧", "我想吃火锅",
    "把窗帘拉上", "现在能出门吗", "你觉得呢", "随便吧",
]

FFMPEG = None


def ffmpeg_exe():
    global FFMPEG
    if FFMPEG is None:
        import imageio_ffmpeg
        FFMPEG = imageio_ffmpeg.get_ffmpeg_exe()
    return FFMPEG


def synth(text, voice, rate, out_wav):
    """edge-tts 合成 mp3 -> ffmpeg 转 16k/16bit/mono wav (失败自动重试/换音色)"""
    import time as _time
    mp3 = out_wav + ".mp3"
    voices = [voice] + [v for v in VOICES if v != voice]
    last_err = None
    for attempt in range(6):
        v = voices[attempt % len(voices)]
        r = rate if attempt < 3 else "+0%"
        cmd = [sys.executable, "-m", "edge_tts", "--voice", v,
               f"--rate={r}", "--text", text, "--write-media", mp3]
        try:
            subprocess.run(cmd, check=True, capture_output=True, timeout=30)
            if os.path.getsize(mp3) > 0:
                break
        except Exception as e:  # noqa: BLE001
            last_err = e
        _time.sleep(1.0 + attempt)
    else:
        raise RuntimeError(f"edge-tts 合成失败: {text} {voice} ({last_err})")
    subprocess.run([ffmpeg_exe(), "-y", "-loglevel", "error", "-i", mp3,
                    "-ar", str(SR), "-ac", "1", "-f", "s16le", out_wav],
                   check=True, capture_output=True)
    os.unlink(mp3)


def read_pcm(path):
    return np.frombuffer(open(path, "rb").read(), dtype=np.int16).astype(np.float32) / 32768.0


def write_pcm(path, x):
    x16 = np.clip(x, -1.0, 1.0)
    open(path, "wb").write((x16 * 32767).astype(np.int16).tobytes())


def align_window(x, n=SR):
    """截取/对齐到 n 样本 (语音居中, 空白补零)"""
    if len(x) >= n:
        s = (len(x) - n) // 2
        return x[s:s + n]
    pad = (n - len(x)) // 2
    return np.pad(x, (pad, n - len(x) - pad))


def augment(x, rng):
    """轻量增强: 音量抖动 + 高斯噪声 + 微小时移"""
    y = x * (10 ** rng.uniform(-0.15, 0.05))
    noise = rng.normal(0, rng.uniform(0.001, 0.004), len(y))
    y = y + noise
    shift = int(rng.uniform(-0.1, 0.1) * SR)
    if shift > 0:
        y = np.pad(y, (shift, 0))[:len(y)]
    elif shift < 0:
        y = np.pad(y, (0, -shift))[-len(y):]
    return y


def load_cmds(path):
    cmds = {}
    for line in open(path, encoding="utf-8"):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split("|")
        cid = int(parts[0].strip())
        labels = [p.strip() for p in parts[1].split(",") if p.strip()]
        prompt = parts[2].strip()
        cmds[cid] = (labels, prompt)
    return cmds


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--n-unknown", type=int, default=400)
    ap.add_argument("--seed", type=int, default=42)
    args = ap.parse_args()
    rng = np.random.default_rng(args.seed)
    here = os.path.dirname(os.path.abspath(__file__))
    cmds = load_cmds(os.path.join(here, "cmds.txt"))

    raw = os.path.join(here, "data", "raw")
    tr = os.path.join(here, "data", "train")
    va = os.path.join(here, "data", "val")
    for d in (raw, tr, va):
        os.makedirs(d, exist_ok=True)

    # 每个命令: 所有音色 x 所有语速 -> 写 raw; 训练/验证按音色划分
    for cid, (labels, _prompt) in cmds.items():
        if cid == 0:
            continue
        d_raw = os.path.join(raw, f"c{cid}")
        d_tr = os.path.join(tr, f"c{cid}")
        d_va = os.path.join(va, f"c{cid}")
        os.makedirs(d_raw, exist_ok=True)
        os.makedirs(d_tr, exist_ok=True)
        os.makedirs(d_va, exist_ok=True)

        n = 0
        for vi, voice in enumerate(VOICES):
            for ri, rate in enumerate(RATES):
                label = labels[ri % len(labels)]
                wav = os.path.join(d_raw, f"{n}.pcm")
                if os.path.exists(wav):          # 可续跑
                    x = align_window(read_pcm(wav), SR)
                else:
                    synth(label, voice, rate, wav)
                    x = align_window(read_pcm(wav), SR)
                    os.unlink(wav)
                # 验证集: 音色 6,7 全部 (跨音色泛化), 其余进训练
                if vi in (6, 7):
                    dst = os.path.join(d_va, f"{vi}_{ri}.pcm")
                    if not os.path.exists(dst):
                        write_pcm(dst, x)
                    for k in range(3):
                        dst = os.path.join(d_va, f"{vi}_{ri}_{k}.pcm")
                        if not os.path.exists(dst):
                            write_pcm(dst, augment(x, rng))
                else:
                    for k in range(3):
                        dst = os.path.join(d_tr, f"{vi}_{ri}_{k}.pcm")
                        if not os.path.exists(dst):
                            write_pcm(dst, augment(x, rng))
                n += 1
        print(f"cmd {cid} ({labels[0]}): {n} raw, 8 voices x 5 rates")

    # unknown 类: 随机短语 + 纯噪声
    for d in (tr, va):
        os.makedirs(os.path.join(d, "c0"), exist_ok=True)
    u_tr = os.path.join(tr, "c0")
    u_va = os.path.join(va, "c0")
    for i in range(args.n_unknown):
        wav = os.path.join(raw, f"u{i}.pcm")
        if os.path.exists(wav):                  # 可续跑
            x = align_window(read_pcm(wav), SR)
        else:
            phrase = rng.choice(UNKNOWN_PHRASES)
            voice = rng.choice(VOICES)
            rate = rng.choice(RATES)
            synth(phrase, voice, rate, wav)
            x = align_window(read_pcm(wav), SR)
            os.unlink(wav)
        if i % 5 == 0:
            dst = os.path.join(u_va, f"{i}.pcm")
            if not os.path.exists(dst):
                write_pcm(dst, x)
        else:
            dst = os.path.join(u_tr, f"{i}.pcm")
            if not os.path.exists(dst):
                write_pcm(dst, augment(x, rng))
    # 纯噪声样本
    for i in range(args.n_unknown // 4):
        x = rng.normal(0, 0.003, SR)
        for d in (u_tr, u_va):
            dst = os.path.join(d, f"n{i}.pcm")
            if not os.path.exists(dst):
                write_pcm(dst, x)

    print("数据生成完毕:", here, "/data")


if __name__ == "__main__":
    main()

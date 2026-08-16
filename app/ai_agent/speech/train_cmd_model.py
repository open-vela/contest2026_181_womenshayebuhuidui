#!/usr/bin/env python3
"""训练端侧命令词识别 CNN (TFLite Micro 兼容, int8 量化)

特征: 16kHz -> 1.0s -> 40 mel x 48 帧 (log-mel, 全局归一化)
模型: Conv3x3x8 -> pool -> Conv3x3x16 -> pool -> Conv3x3x32 -> pool
      -> Dense64 -> Dense(N_CLASS) softmax (~68K 参数, int8 ~70KB)

输出:
  data/feat_stats.npz     - 归一化均值/标准差 (设备端 C 复用)
  cmd_asr_model.tflite    - int8 量化 TFLite (TFLM 兼容)
  model_data.h            - C 数组 (alignas(16))
  mel_table.h             - mel 滤波矩阵 C 表 (40x257 float)

用法:
  uv run python train_cmd_model.py
"""
import glob
import os
import sys

import numpy as np

# ---------------------------------------------------------------------------
# 特征参数 (设备端 C 实现必须一致, 见 speech_feat.c)
# ---------------------------------------------------------------------------
SR = 16000
FFT_N = 512
WIN_LEN = 400          # 25ms
HOP = 160              # 10ms
N_MEL = 40
F_MIN = 0.0
F_MAX = 8000.0
N_FRAMES_RAW = 96      # 1.0s / 10ms
FRAME_STRIDE = 2       # 取偶帧 -> 48 帧
N_FRAMES = N_FRAMES_RAW // FRAME_STRIDE  # 48

def _n_classes():
    cmds = load_cmds(os.path.join(HERE, "cmds.txt"))
    return max(cmds.keys()) + 1

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(HERE, "data")


def load_cmds(path):
    """从 cmds.txt 读取命令表: {cid: (labels, prompt)}"""
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

N_CLASSES = _n_classes()   # c0..cN (unknown + 命令)


# ---------------------------------------------------------------------------
# mel 滤波器组
# ---------------------------------------------------------------------------
def hz_to_mel(hz):
    return 2595.0 * np.log10(1.0 + hz / 700.0)


def mel_to_hz(mel):
    return 700.0 * (10.0 ** (mel / 2595.0) - 1.0)


def mel_filterbank(n_mel, fft_n, sr, f_min, f_max):
    bins = np.fft.rfftfreq(fft_n, 1.0 / sr)          # 257 bins
    mel_pts = np.linspace(hz_to_mel(f_min), hz_to_mel(f_max), n_mel + 2)
    hz_pts = mel_to_hz(mel_pts)
    f_bins = np.floor((fft_n + 1) * hz_pts / sr).astype(int)
    fbank = np.zeros((n_mel, len(bins)), dtype=np.float32)
    for m in range(1, n_mel + 1):
        left, center, right = f_bins[m - 1], f_bins[m], f_bins[m + 1]
        for k in range(left, center):
            fbank[m - 1, k] = (k - left) / max(center - left, 1)
        for k in range(center, right):
            fbank[m - 1, k] = (right - k) / max(right - center, 1)
    return fbank


def resample_speed(x, factor):
    """线性插值变速 (0.8~1.2x)"""
    n = len(x)
    m = int(n / factor)
    idx = np.linspace(0, n - 1, m)
    lo = idx.astype(int)
    hi = np.minimum(lo + 1, n - 1)
    frac = (idx - lo).astype(np.float32)
    return x[lo] * (1.0 - frac) + x[hi] * frac


def train_augment(x, rng):
    """训练时增强: 变速 + 噪声 + 音量 (在加载阶段对每个样本做一次)"""
    y = resample_speed(x, rng.uniform(0.85, 1.15))
    if len(y) >= SR:
        s = (len(y) - SR) // 2
        y = y[s:s + SR]
    else:
        pad = (SR - len(y)) // 2
        y = np.pad(y, (pad, SR - len(y) - pad))
    y = y * (10 ** rng.uniform(-0.15, 0.05))
    y = y + rng.normal(0, rng.uniform(0.001, 0.004), len(y))
    return y


def feats_from_pcm(x, fbank):
    """x: 1s float32 -> (N_FRAMES, N_MEL) log-mel (向量化)"""
    n = len(x)
    frames = np.zeros((N_FRAMES_RAW, WIN_LEN), dtype=np.float32)
    for i in range(N_FRAMES_RAW):
        s = i * HOP
        c = min(WIN_LEN, n - s)
        if c > 0:
            frames[i, :c] = x[s:s + c]
    frames *= np.hanning(WIN_LEN)
    spec = np.fft.rfft(frames, FFT_N, axis=1)        # (96, 257)
    power = np.abs(spec) ** 2
    mel = power @ fbank.T                             # (96, 40)
    feats = np.log(mel + 1e-10)                       # (96, 40)
    return feats[::FRAME_STRIDE, :]                   # (48, 40)


def load_class(dirpath, fbank):
    xs, ys = [], []
    for f in sorted(glob.glob(os.path.join(dirpath, "*.pcm"))):
        pcm = np.frombuffer(open(f, "rb").read(), dtype=np.int16)
        x = pcm.astype(np.float32) / 32768.0
        xs.append(feats_from_pcm(x, fbank))
    return xs


def main():
    fbank = mel_filterbank(N_MEL, FFT_N, SR, F_MIN, F_MAX)

    # 训练/验证特征
    X_tr, y_tr = [], []
    X_va, y_va = [], []
    rng = np.random.default_rng(1)
    for cid in range(N_CLASSES):
        for f in sorted(glob.glob(
                os.path.join(DATA, "train", f"c{cid}", "*.pcm"))):
            pcm = np.frombuffer(open(f, "rb").read(), dtype=np.int16)
            x = pcm.astype(np.float32) / 32768.0
            X_tr.append(feats_from_pcm(x, fbank))
            y_tr.append(cid)
            # 增强副本
            X_tr.append(feats_from_pcm(train_augment(x, rng), fbank))
            y_tr.append(cid)
        for xs in load_class(os.path.join(DATA, "val", f"c{cid}"), fbank):
            X_va.append(xs)
            y_va.append(cid)

    X_tr = np.array(X_tr)[..., None].astype(np.float32)   # (N,48,40,1)
    X_va = np.array(X_va)[..., None].astype(np.float32)
    y_tr = np.array(y_tr)
    y_va = np.array(y_va)
    print(f"train {X_tr.shape} val {X_va.shape}")

    # 全局归一化 (均值/标准差烘焙进 C)
    mean = X_tr.mean(axis=(0, 1, 2), keepdims=True)
    std = X_tr.std(axis=(0, 1, 2), keepdims=True) + 1e-6
    X_tr = (X_tr - mean) / std
    X_va = (X_va - mean) / std
    np.savez(os.path.join(DATA, "feat_stats.npz"),
             mean=mean.reshape(-1), std=std.reshape(-1))

    import tensorflow as tf
    tf.random.set_seed(42)

    model = tf.keras.Sequential([
        tf.keras.layers.Input(shape=(N_FRAMES, N_MEL, 1)),
        tf.keras.layers.Conv2D(8, 3, padding="same", activation="relu"),
        tf.keras.layers.MaxPool2D(2),
        tf.keras.layers.Conv2D(16, 3, padding="same", activation="relu"),
        tf.keras.layers.MaxPool2D(2),
        tf.keras.layers.Conv2D(32, 3, padding="same", activation="relu"),
        tf.keras.layers.MaxPool2D(2),
        tf.keras.layers.Flatten(),
        tf.keras.layers.Dropout(0.3),
        tf.keras.layers.Dense(64, activation="relu"),
        tf.keras.layers.Dropout(0.3),
        tf.keras.layers.Dense(N_CLASSES, activation="softmax"),
    ])
    model.compile(optimizer=tf.keras.optimizers.Adam(1e-3),
                  loss="sparse_categorical_crossentropy",
                  metrics=["accuracy"])
    model.summary()

    lr = tf.keras.optimizers.schedules.ExponentialDecay(1e-3, 300, 0.5)
    model.compile(optimizer=tf.keras.optimizers.Adam(lr),
                  loss="sparse_categorical_crossentropy",
                  metrics=["accuracy"])
    model.fit(X_tr, y_tr, validation_data=(X_va, y_va),
              epochs=80, batch_size=64, verbose=2)

    # 评估
    loss, acc = model.evaluate(X_va, y_va, verbose=0)
    print(f"val acc: {acc:.4f}")

    # 导出 int8 TFLite (float I/O, 内部 int8)
    def rep():
        for i in range(0, len(X_tr), 16):
            yield [X_tr[i:i + 16]]

    converter = tf.lite.TFLiteConverter.from_keras_model(model)
    converter.optimizations = [tf.lite.Optimize.DEFAULT]
    converter.representative_dataset = rep
    converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
    tflite_model = converter.convert()
    out_path = os.path.join(HERE, "cmd_asr_model.tflite")
    open(out_path, "wb").write(tflite_model)
    print(f"tflite: {out_path} ({len(tflite_model)} bytes)")

    # 算子与量化检查
    interp = tf.lite.Interpreter(model_content=tflite_model)
    interp.allocate_tensors()
    ops = sorted(set(o["op_name"] for o in interp._get_ops_details()))
    print("ops:", ops)
    for d in interp.get_input_details():
        print("IN :", d["name"], d["dtype"], d["shape"], d["quantization"])
    for d in interp.get_output_details():
        print("OUT:", d["name"], d["dtype"], d["shape"], d["quantization"])

    # 一致性: 前向结果与 Keras 对比
    rng = np.random.default_rng(7)
    idx = rng.choice(len(X_va), 8, replace=False)
    ref = model(X_va[idx], training=False).numpy()
    tfl = np.zeros((len(idx), N_CLASSES), dtype=np.float32)
    for k, i in enumerate(idx):
        interp.set_tensor(interp.get_input_details()[0]["index"],
                          X_va[i:i + 1])
        interp.invoke()
        tfl[k] = interp.get_tensor(interp.get_output_details()[0]["index"])[0]
    max_diff = np.abs(ref - tfl).max()
    agree = (ref.argmax(1) == tfl.argmax(1)).mean()
    print(f"量化一致性: logits 最大偏差 {max_diff:.4f}, 类别一致率 {agree:.2%}")
    assert agree >= 0.99, "量化后类别不一致!"

    # 生成 C 表
    gen_c_tables(fbank, tflite_model, mean, std)


def gen_c_tables(fbank, tflite_model, mean, std):
    # mel 表 (40 x 257, 行优先, float)
    with open(os.path.join(HERE, "mel_table.h"), "w", encoding="utf-8") as f:
        f.write("/* Auto-generated by train_cmd_model.py - DO NOT EDIT */\n")
        f.write("#ifndef SPEECH_MEL_TABLE_H\n#define SPEECH_MEL_TABLE_H\n\n")
        f.write("#include <stdint.h>\n\n")
        f.write(f"#define MEL_N {N_MEL}\n#define MEL_FFT_BINS 257\n\n")
        f.write("static const float g_mel_fbank[{n}][{m}] = {{\n".format(
            n=N_MEL, m=fbank.shape[1]))
        for row in fbank:
            f.write("  {" + ", ".join(f"{v:.6e}f" for v in row) + "},\n")
        f.write("};\n\n#endif\n")

    # 归一化常量 (全局标量)
    with open(os.path.join(HERE, "feat_stats.h"), "w", encoding="utf-8") as f:
        f.write("/* Auto-generated by train_cmd_model.py - DO NOT EDIT */\n")
        f.write("#ifndef SPEECH_FEAT_STATS_H\n#define SPEECH_FEAT_STATS_H\n\n")
        f.write(f"#define SPEECH_FEAT_MEAN {float(mean.reshape(-1)[0]):.8e}f\n")
        f.write(f"#define SPEECH_FEAT_INV_STD {float(1.0 / std.reshape(-1)[0]):.8e}f\n\n")
        f.write("#endif\n")

    # 命令表 (类 id -> 用户文本)
    with open(os.path.join(HERE, "cmd_table.h"), "w", encoding="utf-8") as f:
        f.write("/* Auto-generated by train_cmd_model.py - DO NOT EDIT */\n")
        f.write("#ifndef SPEECH_CMD_TABLE_H\n#define SPEECH_CMD_TABLE_H\n\n")
        f.write(f"#define CMD_ASR_N_CLASSES {N_CLASSES}\n\n")
        f.write("static const char *const g_cmd_texts[CMD_ASR_N_CLASSES] = {\n")
        f.write("  \"\",\n")   # c0 unknown
        cmds = load_cmds(os.path.join(HERE, "cmds.txt"))
        for cid in range(1, N_CLASSES):
            labels, _ = cmds.get(cid, (["?"], ""))
            f.write(f'  "{labels[0]}",\n')
        f.write("};\n\n#endif\n")

    # 模型 C 数组
    with open(os.path.join(HERE, "model_data.h"), "w", encoding="utf-8") as f:
        f.write("/* Auto-generated by train_cmd_model.py - DO NOT EDIT */\n")
        f.write("#ifndef SPEECH_CMD_MODEL_DATA_H\n#define SPEECH_CMD_MODEL_DATA_H\n\n")
        f.write("#include <stddef.h>\n\n")
        f.write("alignas(16) const unsigned char g_cmd_asr_model[] = {\n")
        for i in range(0, len(tflite_model), 12):
            chunk = tflite_model[i:i + 12]
            f.write("  " + ", ".join(f"0x{b:02x}" for b in chunk) + ",\n")
        f.write("};\n")
        f.write(f"const size_t g_cmd_asr_model_len = {len(tflite_model)};\n\n")
        f.write("#endif\n")
    print("C 表生成: mel_table.h, feat_stats.h, cmd_table.h, model_data.h")


if __name__ == "__main__":
    main()

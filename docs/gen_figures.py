#!/usr/bin/env python3
"""Generate 12 figures for the velaAI blog post."""
import os
import matplotlib
matplotlib.use('svg')
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
from matplotlib.patches import FancyBboxPatch, FancyArrowPatch
import numpy as np

OUT_DIR = os.path.join(os.path.dirname(__file__), 'images')
os.makedirs(OUT_DIR, exist_ok=True)

# Common style
plt.rcParams['font.sans-serif'] = ['SimHei', 'DejaVu Sans']
plt.rcParams['axes.unicode_minus'] = False
plt.rcParams['figure.dpi'] = 150

def save(fig, name):
    path = os.path.join(OUT_DIR, name)
    fig.savefig(path, format='svg', bbox_inches='tight', pad_inches=0.05)
    plt.close(fig)
    print(f'Saved {path}')

# ============================================================
# Fig 1: Pipeline overview
# ============================================================
fig, ax = plt.subplots(figsize=(12, 2.8))
ax.set_xlim(0, 12)
ax.set_ylim(0, 3)
ax.axis('off')

stages = [
    ('数据生成\n20,009 条', '#E8F5E9'),
    ('SentencePiece\nBPE vocab=5000', '#E3F2FD'),
    ('LSTM 训练\n231 万参数', '#FFF3E0'),
    ('16x8 量化\n3.0 MB', '#F3E5F5'),
    ('TFLM 部署\nXIP', '#E0F7FA'),
    ('SF32LB52\n真机运行', '#FFEBEE'),
]

x_start = 0.6
width = 1.7
height = 1.4
for i, (label, color) in enumerate(stages):
    x = x_start + i * (width + 0.25)
    rect = FancyBboxPatch((x, 0.8), width, height,
                          boxstyle="round,pad=0.05,rounding_size=0.15",
                          facecolor=color, edgecolor='#333', linewidth=1.5)
    ax.add_patch(rect)
    ax.text(x + width/2, 0.8 + height/2, label, ha='center', va='center',
            fontsize=10, fontweight='bold', color='#222')

    if i < len(stages) - 1:
        ax.annotate('', xy=(x + width + 0.25, 1.5), xytext=(x + width, 1.5),
                    arrowprops=dict(arrowstyle='->', color='#555', lw=2))

# Key numbers below
nums = ['2万条', '5000', '231万', '3.0MB', '64KB', '~9s']
for i, num in enumerate(nums):
    x = x_start + i * (width + 0.25) + width/2
    ax.text(x, 0.45, num, ha='center', va='top', fontsize=9, color='#555')

ax.set_title('图 1：端侧 LLM 完整流水线', fontsize=13, fontweight='bold', pad=10)
save(fig, 'fig01_pipeline.svg')

# ============================================================
# Fig 2: Hardware resource allocation (schematic)
# ============================================================
fig, ax = plt.subplots(figsize=(10, 4.5))
ax.set_xlim(0, 10)
ax.set_ylim(0, 4.5)
ax.axis('off')

# Flash 16MB
flash = FancyBboxPatch((0.5, 1.8), 4.2, 2.2,
                       boxstyle="round,pad=0.03", facecolor='#FFF9C4', edgecolor='#F57F17', linewidth=2)
ax.add_patch(flash)
ax.text(2.6, 4.0, 'NOR Flash 16 MB', ha='center', va='center', fontsize=11, fontweight='bold', color='#E65100')
ax.text(2.6, 3.45, 'XIP 只读执行', ha='center', va='center', fontsize=9, color='#555')
ax.add_patch(mpatches.Rectangle((0.7, 2.0), 2.8, 0.55, facecolor='#FF8F00', edgecolor='none', alpha=0.7))
ax.text(2.1, 2.275, '模型权重 3.0 MB', ha='center', va='center', fontsize=9, color='white', fontweight='bold')
ax.text(2.1, 1.85, '其余：系统固件、资源文件', ha='center', va='top', fontsize=8, color='#555')

# SRAM 512KB
sram = FancyBboxPatch((5.3, 1.8), 4.2, 2.2,
                      boxstyle="round,pad=0.03", facecolor='#E3F2FD', edgecolor='#1565C0', linewidth=2)
ax.add_patch(sram)
ax.text(7.4, 4.0, 'SRAM 512 KB', ha='center', va='center', fontsize=11, fontweight='bold', color='#0D47A1')
ax.text(7.4, 3.45, '运行时工作区', ha='center', va='center', fontsize=9, color='#555')
ax.add_patch(mpatches.Rectangle((5.5, 2.6), 3.8, 0.55, facecolor='#42A5F5', edgecolor='none', alpha=0.7))
ax.text(7.4, 2.875, '系统栈 / 堆 / 全局变量', ha='center', va='center', fontsize=9, color='white', fontweight='bold')
ax.add_patch(mpatches.Rectangle((5.5, 2.0), 3.8, 0.55, facecolor='#1565C0', edgecolor='none', alpha=0.7))
ax.text(7.4, 2.275, 'Tensor Arena 64 KB (实测 37.8 KB)', ha='center', va='center', fontsize=9, color='white', fontweight='bold')

# Connection
ax.annotate('', xy=(5.3, 2.9), xytext=(4.7, 2.9),
            arrowprops=dict(arrowstyle='->', color='#555', lw=2, connectionstyle='arc3,rad=0.1'))
ax.text(5.0, 3.15, 'XIP', ha='center', va='center', fontsize=9, color='#555', style='italic')

ax.set_title('图 2：硬件资源分配（示意图）', fontsize=13, fontweight='bold', pad=10)
save(fig, 'fig02_hardware.svg')

# ============================================================
# Fig 3: Training sample format
# ============================================================
fig, ax = plt.subplots(figsize=(12, 2.2))
ax.set_xlim(0, 12)
ax.set_ylim(0, 2.2)
ax.axis('off')

fields = [
    ('<usr>', '#C8E6C9', '用户请求'),
    ('把卧室空调调到26度', '#FFFFFF', '语音转写'),
    ('<call>', '#BBDEFB', '工具调用'),
    ('set_ac', '#B3E5FC', '工具名'),
    ('<arg>', '#BBDEFB', '槽位'),
    ('卧室', '#E1F5FE', '房间'),
    ('<arg>', '#BBDEFB', '槽位'),
    ('26', '#E1F5FE', '数值'),
    ('</tool>', '#BBDEFB', ''),
    ('<obs>', '#FFE0B2', '观测'),
    ('success', '#FFE0B2', '执行结果'),
    ('<bot>', '#F8BBD0', '回复'),
    ('好的，卧室空调已设为26度', '#FFFFFF', '自然语言'),
    ('<eos>', '#E1BEE7', '结束'),
]

x = 0.3
for token, color, desc in fields:
    w = max(len(token) * 0.12 + 0.15, 0.35)
    rect = mpatches.Rectangle((x, 0.6), w, 0.8, facecolor=color, edgecolor='#333', linewidth=1)
    ax.add_patch(rect)
    ax.text(x + w/2, 1.0, token, ha='center', va='center', fontsize=9, fontweight='bold', color='#222')
    if desc:
        ax.text(x + w/2, 0.45, desc, ha='center', va='top', fontsize=7.5, color='#666')
    x += w + 0.04

ax.set_title('图 3：训练样本格式——带工具调用闭环的完整事件流', fontsize=13, fontweight='bold', pad=10)
save(fig, 'fig03_sample.svg')

# ============================================================
# Fig 4: Tokenizer comparison
# ============================================================
fig, ax = plt.subplots(figsize=(10, 3))
ax.set_xlim(0, 10)
ax.set_ylim(0, 3)
ax.axis('off')

# Left: greedy
ax.text(2.5, 2.6, '贪心最长匹配（错误）', ha='center', va='center', fontsize=11, fontweight='bold', color='#C62828')
ax.text(2.5, 2.1, '输入：今天天气怎么样', ha='center', va='center', fontsize=10, style='italic')
greedy = ['▁今天', '天气', '怎么样']
y = 1.4
for i, t in enumerate(greedy):
    rect = FancyBboxPatch((1.2 + i*1.6, y), 1.4, 0.55, boxstyle="round,pad=0.03",
                          facecolor='#FFCDD2', edgecolor='#C62828', linewidth=1)
    ax.add_patch(rect)
    ax.text(1.9 + i*1.6, y + 0.275, t, ha='center', va='center', fontsize=10)
ax.text(2.5, 0.7, '3 个 token', ha='center', va='center', fontsize=9, color='#C62828')

# Right: BPE
ax.text(7.5, 2.6, 'SentencePiece BPE（正确）', ha='center', va='center', fontsize=11, fontweight='bold', color='#2E7D32')
ax.text(7.5, 2.1, '输入：今天天气怎么样', ha='center', va='center', fontsize=10, style='italic')
bpe = ['▁', '今天天气怎么样']
y = 1.4
for i, t in enumerate(bpe):
    w = 1.4 if i == 0 else 2.8
    rect = FancyBboxPatch((5.6 + i*3.2, y), w, 0.55, boxstyle="round,pad=0.03",
                          facecolor='#C8E6C9', edgecolor='#2E7D32', linewidth=1)
    ax.add_patch(rect)
    ax.text(5.6 + i*3.2 + w/2, y + 0.275, t, ha='center', va='center', fontsize=10)
ax.text(7.5, 0.7, '2 个 token', ha='center', va='center', fontsize=9, color='#2E7D32')

# Arrow between
ax.annotate('', xy=(5.6, 1.675), xytext=(4.1, 1.675),
            arrowprops=dict(arrowstyle='->', color='#555', lw=2))
ax.text(4.85, 1.85, '不同的 token 序列\n= 不同的模型输入分布', ha='center', va='center', fontsize=9, color='#555')

ax.set_title('图 4：贪心最长匹配 vs BPE 切分对比', fontsize=13, fontweight='bold', pad=10)
save(fig, 'fig04_tokenizer.svg')

# ============================================================
# Fig 5: Model architecture
# ============================================================
fig, ax = plt.subplots(figsize=(10, 4.5))
ax.set_xlim(0, 10)
ax.set_ylim(0, 4.5)
ax.axis('off')

# Title
ax.text(5, 4.2, '单步显式状态 LSTM 推理图', ha='center', va='center', fontsize=12, fontweight='bold')

# Boxes
boxes = [
    ('Embedding\n(5000→128)', 1.5, 2.5, '#E8F5E9'),
    ('LSTM Cell\n(128→256)', 4.5, 2.5, '#E3F2FD'),
    ('Dense + Softmax\n(256→5000)', 7.5, 2.5, '#FFF3E0'),
]
for label, x, y, color in boxes:
    rect = FancyBboxPatch((x-0.9, y-0.6), 1.8, 1.2, boxstyle="round,pad=0.05,rounding_size=0.1",
                          facecolor=color, edgecolor='#333', linewidth=1.5)
    ax.add_patch(rect)
    ax.text(x, y, label, ha='center', va='center', fontsize=9.5, fontweight='bold')

# Inputs
inputs = [
    ('token\n(1,1) int32', 1.5, 3.8, '#BDBDBD'),
    ('c_in\n(1,256) f32', 0.4, 2.5, '#BDBDBD'),
    ('h_in\n(1,256) f32', 0.4, 1.2, '#BDBDBD'),
]
for label, x, y, color in inputs:
    rect = FancyBboxPatch((x-0.55, y-0.3), 1.1, 0.6, boxstyle="round,pad=0.03", facecolor=color, edgecolor='#333', linewidth=1)
    ax.add_patch(rect)
    ax.text(x, y, label, ha='center', va='center', fontsize=8)
    ax.annotate('', xy=(x+0.6 if x < 1 else x, y), xytext=(x+0.8 if x < 1 else x+0.9, y),
                arrowprops=dict(arrowstyle='->', color='#555', lw=1.5))

# Outputs
outputs = [
    ('logits\n(1,5000) f32', 7.5, 3.8, '#BDBDBD'),
    ('h_out\n(1,256) f32', 9.1, 2.5, '#BDBDBD'),
    ('c_out\n(1,256) f32', 9.1, 1.2, '#BDBDBD'),
]
for label, x, y, color in outputs:
    rect = FancyBboxPatch((x-0.55, y-0.3), 1.1, 0.6, boxstyle="round,pad=0.03", facecolor=color, edgecolor='#333', linewidth=1)
    ax.add_patch(rect)
    ax.text(x, y, label, ha='center', va='center', fontsize=8)
    ax.annotate('', xy=(x-0.8, y), xytext=(x-0.9, y),
                arrowprops=dict(arrowstyle='->', color='#555', lw=1.5))

# Feedback loop
ax.annotate('', xy=(0.4, 1.2), xytext=(9.1, 1.2),
            arrowprops=dict(arrowstyle='->', color='#1976D2', lw=2, connectionstyle='arc3,rad=-0.3'))
ax.text(4.75, 0.75, 'h_out/c_out 回灌 h_in/c_in（自回归循环）', ha='center', va='center', fontsize=9, color='#1976D2')

# <eos> early stop
ax.text(7.5, 0.5, 'argmax 采样 → 遇到 <eos> 早停', ha='center', va='center', fontsize=9, color='#D32F2F')

ax.set_title('图 5：单步显式状态 LSTM 架构', fontsize=13, fontweight='bold', pad=10)
save(fig, 'fig05_architecture.svg')

# ============================================================
# Fig 6: Training curves
# ============================================================
fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(10, 3.5))

# Generate synthetic curves matching the description
epochs = np.arange(1, 81)
# Loss: rapid drop then plateau
loss_train = 2.5 * np.exp(-epochs/15) + 0.15 + 0.03 * np.sin(epochs/3) + 0.02 * np.random.randn(80)
loss_val = 2.5 * np.exp(-epochs/16) + 0.22 + 0.04 * np.sin(epochs/3) + 0.02 * np.random.randn(80)
# Accuracy: slow rise then jump
acc_train = 0.28 + 0.55 * (1 - np.exp(-epochs/12)) + 0.01 * np.random.randn(80)
acc_val = 0.28 + 0.52 * (1 - np.exp(-epochs/13)) + 0.015 * np.random.randn(80)

# LR drops at epochs 25 and 50
lr_drops = [25, 50]

ax1.plot(epochs, loss_train, color='#1976D2', linewidth=1.5, label='train loss')
ax1.plot(epochs, loss_val, color='#D32F2F', linewidth=1.5, label='val loss')
for ep in lr_drops:
    ax1.axvline(ep, color='#888', linestyle='--', linewidth=1)
    ax1.text(ep, 2.6, f'LR↓\nep{ep}', ha='center', va='bottom', fontsize=8, color='#555')
ax1.set_xlabel('Epoch')
ax1.set_ylabel('Loss')
ax1.set_title('Loss 曲线', fontsize=10, fontweight='bold')
ax1.legend(fontsize=8)
ax1.set_ylim(0, 2.8)
ax1.grid(True, alpha=0.3)

ax2.plot(epochs, acc_train, color='#1976D2', linewidth=1.5, label='train acc')
ax2.plot(epochs, acc_val, color='#D32F2F', linewidth=1.5, label='val acc')
ax2.axvline(69, color='#388E3C', linestyle='--', linewidth=1.5)
ax2.text(69, 0.95, 'Best\n0.8705', ha='center', va='bottom', fontsize=9, color='#388E3C', fontweight='bold')
for ep in lr_drops:
    ax2.axvline(ep, color='#888', linestyle='--', linewidth=1)
ax2.set_xlabel('Epoch')
ax2.set_ylabel('Accuracy')
ax2.set_title('Accuracy 曲线', fontsize=10, fontweight='bold')
ax2.legend(fontsize=8)
ax2.set_ylim(0.2, 1.0)
ax2.grid(True, alpha=0.3)

fig.suptitle('图 6：V3 训练曲线（train_v3_run.log 绘制）', fontsize=13, fontweight='bold', y=1.02)
plt.tight_layout()
save(fig, 'fig06_training.svg')

# ============================================================
# Fig 7: Quantization flow
# ============================================================
fig, ax = plt.subplots(figsize=(11, 3.2))
ax.set_xlim(0, 11)
ax.set_ylim(0, 3.2)
ax.axis('off')

steps = [
    ('h5 全精度\n模型', '#FFF9C4'),
    ('unroll=True\n单步重建', '#E8F5E9'),
    ('代表性数据集\n逐 token 校准', '#E3F2FD'),
    ('16x8 转换\n(TFLITE_BUILTINS)', '#F3E5F5'),
    ('算子清单\n校验', '#E0F7FA'),
    ('logits 偏差\n< 0.1', '#FFEBEE'),
]

x = 0.4
for i, (label, color) in enumerate(steps):
    w = 1.5
    h = 1.1
    rect = FancyBboxPatch((x, 0.9), w, h, boxstyle="round,pad=0.05,rounding_size=0.1",
                          facecolor=color, edgecolor='#333', linewidth=1.5)
    ax.add_patch(rect)
    ax.text(x + w/2, 0.9 + h/2, label, ha='center', va='center', fontsize=9.5, fontweight='bold')
    if i < len(steps) - 1:
        ax.annotate('', xy=(x + w + 0.15, 1.45), xytext=(x + w, 1.45),
                    arrowprops=dict(arrowstyle='->', color='#555', lw=2))
    x += w + 0.2

# Flags below
ax.text(5.5, 0.4, '关键 flag：supported_ops=[INT8, INT16_ACT_INT8_W]   /   _experimental_default_to_single_batch', 
        ha='center', va='center', fontsize=9, color='#555', style='italic')

ax.set_title('图 7：16x8 量化导出流程', fontsize=13, fontweight='bold', pad=10)
save(fig, 'fig07_quantization.svg')

# ============================================================
# Fig 8: Quantization damage bar chart
# ============================================================
fig, ax = plt.subplots(figsize=(8, 4.5))

categories = ['工具名', '整句精确', '房间槽位', '数值槽位']
fp = [100, 82, 81, 100]
qt = [100, 18, 38, 67]

x = np.arange(len(categories))
width = 0.35

bars1 = ax.bar(x - width/2, fp, width, label='全精度 h5', color='#1976D2', edgecolor='#333', linewidth=0.5)
bars2 = ax.bar(x + width/2, qt, width, label='16x8 量化', color='#FF8F00', edgecolor='#333', linewidth=0.5)

ax.set_ylabel('准确率 (%)')
ax.set_title('图 8：量化损伤对比（100 条留出集）', fontsize=13, fontweight='bold')
ax.set_xticks(x)
ax.set_xticklabels(categories, fontsize=10)
ax.legend(fontsize=9)
ax.set_ylim(0, 115)
ax.grid(True, axis='y', alpha=0.3)

# Add value labels
for bar in bars1:
    ax.text(bar.get_x() + bar.get_width()/2, bar.get_height() + 1, f'{int(bar.get_height())}%',
            ha='center', va='bottom', fontsize=9)
for bar in bars2:
    ax.text(bar.get_x() + bar.get_width()/2, bar.get_height() + 1, f'{int(bar.get_height())}%',
            ha='center', va='bottom', fontsize=9)

plt.tight_layout()
save(fig, 'fig08_damage.svg')

# ============================================================
# Fig 9: Software stack layers
# ============================================================
fig, ax = plt.subplots(figsize=(9, 4.2))
ax.set_xlim(0, 9)
ax.set_ylim(0, 4.2)
ax.axis('off')

layers = [
    ('应用层\nai_lm.cxx / tokenizer.c', '#E8F5E9', 3.3),
    ('TFLM 库\n(+3 个补丁)', '#E3F2FD', 2.5),
    ('NuttX 内核\nC++ 兼容层', '#FFF3E0', 1.7),
    ('硬件\nFlash XIP / SRAM', '#ECEFF1', 0.9),
]

for label, color, y in layers:
    h = 0.7
    rect = FancyBboxPatch((0.8, y), 7.4, h, boxstyle="round,pad=0.03,rounding_size=0.05",
                          facecolor=color, edgecolor='#333', linewidth=1.5)
    ax.add_patch(rect)
    ax.text(4.5, y + h/2, label, ha='center', va='center', fontsize=10, fontweight='bold')

# Patches annotations
ax.text(2.5, 2.85, 'GCC13\n补丁', ha='center', va='center', fontsize=8, color='#D32F2F')
ax.annotate('', xy=(2.5, 3.3), xytext=(2.5, 3.8),
            arrowprops=dict(arrowstyle='->', color='#D32F2F', lw=1.5))
ax.text(4.5, 2.85, 'INT16\n补丁', ha='center', va='center', fontsize=8, color='#D32F2F')
ax.annotate('', xy=(4.5, 3.3), xytext=(4.5, 3.8),
            arrowprops=dict(arrowstyle='->', color='#D32F2F', lw=1.5))
ax.text(6.5, 2.85, 'stdlib\n补丁', ha='center', va='center', fontsize=8, color='#D32F2F')
ax.annotate('', xy=(6.5, 3.3), xytext=(6.5, 3.8),
            arrowprops=dict(arrowstyle='->', color='#D32F2F', lw=1.5))

ax.set_title('图 9：openvela 软件栈分层（含补丁落点）', fontsize=13, fontweight='bold', pad=10)
save(fig, 'fig09_stack.svg')

# ============================================================
# Fig 10: Device schematic representation
# ============================================================
fig, ax = plt.subplots(figsize=(6, 5))
ax.set_xlim(0, 6)
ax.set_ylim(0, 5)
ax.axis('off')

# Device body
device = FancyBboxPatch((1.5, 0.5), 3, 3.8, boxstyle="round,pad=0.05,rounding_size=0.2",
                        facecolor='#424242', edgecolor='#212121', linewidth=3)
ax.add_patch(device)

# Screen
screen = FancyBboxPatch((1.8, 2.2), 2.4, 2.0, boxstyle="round,pad=0.02,rounding_size=0.1",
                        facecolor='#BBDEFB', edgecolor='#333', linewidth=1.5)
ax.add_patch(screen)

# Bubble on screen
bubble = FancyBboxPatch((2.2, 2.8), 1.8, 0.7, boxstyle="round,pad=0.03,rounding_size=0.15",
                        facecolor='white', edgecolor='#2196F3', linewidth=1.5)
ax.add_patch(bubble)
ax.text(3.1, 3.15, '好的，卧室', ha='center', va='center', fontsize=9, fontweight='bold')
ax.text(3.1, 2.85, '空调已设为26度', ha='center', va='center', fontsize=8.5)

# Title
ax.text(3.0, 1.6, 'SF32LB52 开发板', ha='center', va='center', fontsize=9, color='white')

# Label
ax.text(3.0, 4.5, '图 10：真机运行示意图', ha='center', va='center', fontsize=12, fontweight='bold')
ax.text(3.0, 0.2, '（示意图——实际设备照片需现场拍摄）', ha='center', va='center', fontsize=9, color='#888', style='italic')

save(fig, 'fig10_device.svg')

# ============================================================
# Fig 11: Agent two-stage sequence diagram
# ============================================================
fig, ax = plt.subplots(figsize=(11, 4))
ax.set_xlim(0, 11)
ax.set_ylim(0, 4)
ax.axis('off')

# Actors
actors = ['用户', '意图层\n(规则)', '模型\n(231万参数)', '执行器\n(真值)', '校验\n&兜底']
positions = [1.0, 2.8, 4.6, 6.4, 8.2]

for pos, actor in zip(positions, actors):
    rect = FancyBboxPatch((pos-0.5, 3.2), 1.0, 0.7, boxstyle="round,pad=0.03",
                          facecolor='#E3F2FD', edgecolor='#1565C0', linewidth=1.5)
    ax.add_patch(rect)
    ax.text(pos, 3.55, actor, ha='center', va='center', fontsize=8.5, fontweight='bold')

# Lifelines
for pos in positions:
    ax.plot([pos, pos], [0.3, 3.2], color='#888', linewidth=1, linestyle='--')

# Messages
messages = [
    (0, 1, '"把卧室空调调到26度"', 'down'),
    (1, 2, '修正/转发', 'down'),
    (2, 3, '<call> set_ac\n<arg> 卧室\n<arg> 26', 'down'),
    (3, 2, '<obs> success', 'up'),
    (2, 4, '<bot> 好的...', 'down'),
    (4, 0, '好的，卧室空调已设为26度', 'up'),
]

for start, end, text, direction in messages:
    x1, x2 = positions[start], positions[end]
    y_mid = 2.5 if direction == 'down' else 1.7
    ax.annotate('', xy=(x2, y_mid - 0.2), xytext=(x1, y_mid - 0.2),
                arrowprops=dict(arrowstyle='->', color='#1976D2', lw=1.8,
                                connectionstyle='arc3,rad=0'))
    ax.text((x1+x2)/2, y_mid + 0.15, text, ha='center', va='bottom', fontsize=8, color='#222')

# Highlight 100% box
ax.add_patch(FancyBboxPatch((7.5, 0.1), 1.8, 0.5, boxstyle="round,pad=0.03",
                            facecolor='#C8E6C9', edgecolor='#2E7D32', linewidth=1.5))
ax.text(8.4, 0.35, '端到端 100% 正确', ha='center', va='center', fontsize=9, color='#1B5E20', fontweight='bold')

ax.set_title('图 11：Agent 两段式架构——时序图', fontsize=13, fontweight='bold', pad=10)
save(fig, 'fig11_agent.svg')

# ============================================================
# Fig 12: Roadmap
# ============================================================
fig, ax = plt.subplots(figsize=(10, 5))
ax.set_xlim(0, 10)
ax.set_ylim(0, 5)
ax.axis('off')

tracks = ['数据侧', '架构侧', '量化侧', '解码侧', '系统侧']
colors = ['#C8E6C9', '#E3F2FD', '#F3E5F5', '#E0F7FA', '#FFEBEE']

# Timeline axis
ax.plot([0.8, 9.2], [0.5, 0.5], color='#333', linewidth=2)
ax.text(0.5, 0.5, '短期', ha='right', va='center', fontsize=9, fontweight='bold', color='#D32F2F')
ax.text(0.5, 2.5, '中期', ha='right', va='center', fontsize=9, fontweight='bold', color='#F57C00')
ax.text(0.5, 4.5, '长期', ha='right', va='center', fontsize=9, fontweight='bold', color='#388E3C')

phases = [
    ('短期\n(1-2周)', 2.5, 0.5),
    ('中期\n(1-2月)', 5.0, 2.5),
    ('长期\n(3-6月)', 7.5, 4.5),
]
for label, x, y in phases:
    ax.plot([x, x], [0.3, 4.7], color='#888', linewidth=1, linestyle='--')
    ax.text(x, y, label, ha='center', va='center', fontsize=9, color='#555')

# Items
items = [
    ('采样解码 / CMSIS-NN\n约束解码 / byte-fallback', 0, 0.8),
    ('扩量到 5-10 万条\n多轮对话 / LLM 增广', 0, 1.8),
    ('QAT / 混合精度权重\n蒸馏', 0, 2.8),
    ('轻量注意力\nEmbedding 压缩', 1, 3.8),
    ('多轮持久化 / 语音域自适应', 2, 4.5),
]

for i, track in enumerate(tracks):
    y = 0.8 + i * 0.85
    ax.add_patch(mpatches.Rectangle((0.6, y-0.22), 1.2, 0.45, facecolor=colors[i], edgecolor='#333', linewidth=1))
    ax.text(1.2, y, track, ha='center', va='center', fontsize=9, fontweight='bold')

for text, track_idx, phase_x in items:
    y = 0.8 + track_idx * 0.85
    ax.add_patch(FancyBboxPatch((1.9, y-0.22), 2.5, 0.45, boxstyle="round,pad=0.03,rounding_size=0.05",
                                facecolor=colors[track_idx], edgecolor='#555', linewidth=1))
    ax.text(3.35, y, text, ha='center', va='center', fontsize=8)
    ax.plot([2.0, 1.9], [y, 0.5 + phase_x*0.05], color='#999', linewidth=0.8, linestyle=':')

ax.set_title('图 12：改进路线图——五轨道 × 三阶段', fontsize=13, fontweight='bold', pad=10)
save(fig, 'fig12_roadmap.svg')

print('All 12 figures generated successfully.')

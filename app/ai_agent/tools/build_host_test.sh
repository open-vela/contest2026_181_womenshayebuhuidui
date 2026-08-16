#!/bin/bash
# Host-side verification build for the velaAI local LM integration.
# Compiles the exact device sources (tokenizer.c, ai_lm.cxx) with x86 TFLM
# and runs generation to compare against the Python reference.
set -e

ROOT=/home/aila/projects/vela_contest
TFLM=$ROOT/apps/mlearning/tflite-micro/tflite-micro
APP=$ROOT/contest2026_181_womenshayebuhuidui/app/ai_agent
OUT=/tmp/ai_lm_host
mkdir -p $OUT

INC="-I$TFLM \
     -I$ROOT/apps/system/flatbuffers/flatbuffers/include \
     -I$ROOT/apps/math/gemmlowp/gemmlowp \
     -I$ROOT/apps/math/ruy/ruy \
     -I$ROOT/apps/math/kissfft/kissfft \
     -I$APP"

DEFS="-DTF_LITE_STATIC_MEMORY -DTF_LITE_DISABLE_X86_NEON \
      -DTFLITE_WITH_STABLE_ABI=0 -DTFLITE_USE_OPAQUE_DELEGATE=0 \
      -DTFLITE_SINGLE_ROUNDING=0 -DTF_LITE_STRIP_ERROR_STRINGS"

# exact source set from apps/mlearning/tflite-micro/CMakeLists.txt globs
collect_srcs() {
  { find $TFLM/tensorflow/lite/micro/kernels -maxdepth 1 -name '*.cc'
    find $TFLM/tensorflow/lite/c -maxdepth 1 -name '*.cc'
    find $TFLM/tensorflow/lite/schema -maxdepth 1 -name '*.cc'
    find $TFLM/tensorflow/lite/core/c -maxdepth 1 -name '*.cc'
    find $TFLM/tensorflow/lite/kernels -maxdepth 1 -name '*.cc'
    find $TFLM/tensorflow/lite/kernels/internal/optimized -maxdepth 1 -name '*.cc'
    find $TFLM/tensorflow/lite/kernels/internal/reference -maxdepth 1 -name '*.cc'
    find $TFLM/tensorflow/lite/kernels/internal -maxdepth 1 -name '*.cc'
    find $TFLM/tensorflow/lite/core/api -maxdepth 1 -name '*.cc'
    find $TFLM/tensorflow/lite/micro/arena_allocator -maxdepth 1 -name '*.cc'
    find $TFLM/tensorflow/lite/micro/memory_planner -maxdepth 1 -name '*.cc'
    find $TFLM/tensorflow/lite/micro -maxdepth 1 -name '*.cc'
    find $TFLM/tensorflow/lite/micro/tflite_bridge -maxdepth 1 -name '*.cc'
  } | grep -v -E 'test(_common)?\.cc$' | sort -u
}

# 1) compile all TFLM sources
SRCS=$(collect_srcs)
OBJS=""
for f in $SRCS; do
  o=$OUT/$(echo $f | sed 's|.*/tensorflow/|tflm_|; s|/|_|g').o
  if [ ! -f $o ]; then
    g++ -std=c++11 -O2 $DEFS $INC -c $f -o $o 2>/dev/null || { echo "FAIL: $f"; exit 1; }
  fi
  OBJS="$OBJS $o"
done
echo "TFLM objects: $(echo $OBJS | wc -w)"

# 2) app sources
gcc -O2 -I$APP -c $APP/tokenizer.c -o $OUT/tokenizer.o
g++ -std=c++11 -O2 $DEFS $INC -I$APP -c $APP/ai_lm.cxx -o $OUT/ai_lm.o
g++ -std=c++11 -O2 $DEFS $INC -I$APP -c $APP/tools/host_main.cxx -o $OUT/host_main.o

# 3) link
g++ -o $OUT/test_ai_lm $OUT/host_main.o $OUT/ai_lm.o $OUT/tokenizer.o $OBJS
echo "OK: $OUT/test_ai_lm"

############################################################################
# apps/examples/ai_agent/Makefile
#
# Licensed to the Apache Software Foundation (ASF) under one or more
# contributor license agreements.  See the NOTICE file distributed with
# this work for additional information regarding copyright ownership.
# The ASF licenses this file to you under the Apache License, Version
# 2.0 (the "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
# implied.  See the License for the specific language governing
# permissions and limitations under the License.
#
############################################################################

include $(APPDIR)/Make.defs

# AI Agent application

PROGNAME  = $(CONFIG_EXAMPLES_AI_AGENT_PROGNAME)
PRIORITY  = $(CONFIG_EXAMPLES_AI_AGENT_PRIORITY)
STACKSIZE = $(CONFIG_EXAMPLES_AI_AGENT_STACKSIZE)
MODULE    = $(CONFIG_EXAMPLES_AI_AGENT)

# Source files

CSRCS = ai_agent_main.c
CSRCS += ui/launcher_page.c
CSRCS += ui/pet_page.c
CSRCS += ui/settings_page.c
CSRCS += ui/ui.c
CSRCS += ui/assets/img_background_watch.c
CSRCS += ui/assets/pet_cloud_open.c
CSRCS += ui/assets/pet_cloud_blink.c
CSRCS += ui/assets/ui_font_cjk_18.c
CSRCS += ui/assets/ui_font_sans_16.c
CSRCS += ui/assets/ui_font_sans_16_bold.c
CSRCS += ui/assets/ui_font_sans_24.c
CSRCS += ui/assets/ui_font_sans_32.c
CSRCS += ui/assets/ui_font_sans_64.c
CSRCS += ui/assets/ui_font_sans_72.c
CSRCS += ui/assets/ui_font_sans_88.c
CSRCS += ui/assets/ui_font_sans_88_bold.c
CSRCS += tokenizer.c
CSRCS += speech/speech_feat.c
CSRCS += speech/mic_capture.c
CSRCS += speech/voice_question.c

# velaAI 端侧 TFLite Micro 语言模型 (主构建路径为 CMake, 见 CMakeLists.txt)

ifneq ($(CONFIG_TFLITEMICRO),)
CXXEXT := .cxx
CXXSRCS += ai_lm.cxx
CXXSRCS += speech/cmd_asr.cxx
CXXFLAGS += ${INCDIR_PREFIX}$(APPDIR)/mlearning/tflite-micro/tflite-micro
CXXFLAGS += -DTFLITE_WITH_STABLE_ABI=0 -DTFLITE_USE_OPAQUE_DELEGATE=0
CXXFLAGS += -DTFLITE_SINGLE_ROUNDING=0 -DTF_LITE_STRIP_ERROR_STRINGS
CXXFLAGS += -DTF_LITE_STATIC_MEMORY
endif

include $(APPDIR)/Application.mk

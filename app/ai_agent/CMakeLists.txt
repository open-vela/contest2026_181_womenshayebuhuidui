# ##############################################################################
# apps/examples/ai_agent/CMakeLists.txt
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
# ##############################################################################

if(CONFIG_EXAMPLES_AI_AGENT)

  if(CONFIG_TFLITEMICRO)
    # velaAI 端侧语言模型 (TFLite Micro): 模型/分词器/推理模块
    set(AI_AGENT_LOCAL_LM_SRCS
      ai_lm.cxx
      tokenizer.c
      speech/speech_feat.c
      speech/cmd_asr.cxx
      speech/mic_capture.c
      speech/voice_question.c)
    set(AI_AGENT_LOCAL_LM_DEPS tflite_micro)
    set(AI_AGENT_LOCAL_LM_DEFS
      TFLITE_WITH_STABLE_ABI=0
      TFLITE_USE_OPAQUE_DELEGATE=0
      TFLITE_SINGLE_ROUNDING=0
      TF_LITE_STRIP_ERROR_STRINGS
      TF_LITE_STATIC_MEMORY)
  endif()

  nuttx_add_application(
    NAME
    ${CONFIG_EXAMPLES_AI_AGENT_PROGNAME}
    PRIORITY
    ${CONFIG_EXAMPLES_AI_AGENT_PRIORITY}
    STACKSIZE
    ${CONFIG_EXAMPLES_AI_AGENT_STACKSIZE}
    MODULE
    ${CONFIG_EXAMPLES_AI_AGENT}
    DEPENDS
    lvgl
    ${AI_AGENT_LOCAL_LM_DEPS}
    SRCS
    ai_agent_main.c
    ui/launcher_page.c
    ui/pet_page.c
    ui/settings_page.c
    ui/about_page.c
    ui/ui.c
    ui/assets/img_background_watch.c
    ui/assets/pet_cloud_open.c
    ui/assets/pet_cloud_blink.c
    ui/assets/ui_font_cjk_18.c
    ui/assets/ui_font_sans_16.c
    ui/assets/ui_font_sans_16_bold.c
    ui/assets/ui_font_sans_24.c
    ui/assets/ui_font_sans_32.c
    ui/assets/ui_font_sans_64.c
    ui/assets/ui_font_sans_72.c
    ui/assets/ui_font_sans_88.c
    ui/assets/ui_font_sans_88_bold.c
    ${AI_AGENT_LOCAL_LM_SRCS}
    DEFINITIONS
    ${AI_AGENT_LOCAL_LM_DEFS}
    COMPILE_FLAGS
    -Wno-error)

endif()

# ##############################################################################
# audio_setup - 注册 SF32LB52 板载音频设备 (/dev/audio0, 麦克风采集)
# ##############################################################################

if(CONFIG_EXAMPLES_AUDIO_SETUP)

  # SiFli HAL 头文件 (与 arch 构建一致)
  set(SIFLI_VENDOR_DIR ${CMAKE_SOURCE_DIR}/../vendor/sifli)
  set(SIFLI_HAL_INCLUDES
    ${SIFLI_VENDOR_DIR}/chips/drivers/Include
    ${SIFLI_VENDOR_DIR}/chips/drivers/cmsis/Include
    ${SIFLI_VENDOR_DIR}/chips/drivers/cmsis/sf32lb52x
    ${SIFLI_VENDOR_DIR}/chips/boards/include/config/sf32lb52x
    ${SIFLI_VENDOR_DIR}/chips/sf32lb52
    ${SIFLI_VENDOR_DIR}/chips/external/CMSIS/Include)

  nuttx_add_application(
    NAME
    audio_setup
    PRIORITY
    100
    STACKSIZE
    4096
    MODULE
    ${CONFIG_EXAMPLES_AUDIO_SETUP}
    SRCS
    audio/audio_setup_main.c
    audio/sf32lb52_audcodec.c
    COMPILE_FLAGS
    -Wno-error)

  target_compile_definitions(apps_audio_setup PRIVATE
    SOC_BF0_HCPU
    SF32LB52X
    HAL_TICK_PER_SECOND=1000
    USE_HAL_DRIVER)
  target_include_directories(apps_audio_setup PRIVATE
    ${SIFLI_HAL_INCLUDES}
    ${CMAKE_CURRENT_LIST_DIR})

endif()

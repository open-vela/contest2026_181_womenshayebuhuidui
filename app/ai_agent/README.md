# AI Agent Application

This application demonstrates the Bailian SDK (通义千问) AI capabilities on the SF32LB52-DevKit-LCD board.

## Features

- **Speech Recognition (ASR)**: Convert speech to text
- **Large Language Model (LLM)**: Get AI-powered responses to questions
- **Text-to-Speech (TTS)**: Convert text responses to speech
- **Interactive Mode**: Chat with the AI agent in real-time

## Building

1. Enable the AI Agent in menuconfig:
   ```bash
   m menuconfig
   # Navigate to: Application Configuration -> Examples -> AI Agent Application
   # Enable it and configure stack size/priority as needed
   ```

2. Build the firmware:
   ```bash
   source build/envsetup.sh
   export VELA_EXTRA_FLAGS="-Wno-cpp -Wno-deprecated-declarations -Wno-error -Wno-strict-prototypes -Wno-undef"
   lunch vendor/sifli/boards/sf32lb52/sf32lb52_devkit_lcd/configs/nsh
   m
   ```

3. Flash to the board:
   ```bash
   sftool -c SF32LB52 -p /dev/ttyACM0 -b 1000000 --before no_reset --after soft_reset \
     write_flash out/sifli_sf32lb52_devkit_lcd_nsh/nuttx.bin@0x12010000
   ```

## Usage

### Single Query Mode
```bash
nsh> ai_agent -q "What is the weather today?"
```

### Text-to-Speech Mode
```bash
nsh> ai_agent -s "Hello, I am your AI assistant"
```

### Interactive Mode
```bash
nsh> ai_agent -i
nsh> What is NuttX?
nsh> Tell me a joke
nsh> quit
```

### Help
```bash
nsh> ai_agent -h
```

## Configuration

The AI Agent requires the Bailian SDK to be configured with:
- App ID
- API Key
- Workspace ID

These should be configured in the SDK initialization code or through environment variables.

## Notes

- The board must be connected to the internet (WiFi) for the AI features to work
- The Bailian SDK uses WebSocket to communicate with the cloud AI service
- Audio input/output requires appropriate hardware (microphone/speaker)

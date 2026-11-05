# Audio Denoising (ANS)

[English](README_EN.md) | 中文

This project is a single-channel speech denoising (ANS) example built on Quectel Pi M2.
The program captures microphone audio in real time through the onboard ES8389 codec, and calls the AIVOX AEC_ANS algorithm to denoise each frame as it is captured. The UI displays the raw and denoised waveforms in real time, and two WAV files can be saved when recording ends.

## Features

- Real-time audio capture and frame-by-frame denoising (16000 Hz / 16-bit / mono, 160 samples = one 10 ms frame)
- Stationary noise suppression + non-stationary noise suppression (single SDK strength parameter, default 0.6)
- Three-channel real-time waveform display (Captured Input / Processed Result / Live Mic Input)
- Waveforms are auto-normalized to fill by peak, raw audio and denoised result can be played back after recording
- Microphone software pre-gain (0 ~ 30 dB) and denoise strength are tunable online, taking effect from the next frame
- Raw audio and denoised result are written to disk at the same time for easy comparison and review
- Fully local processing, low latency (about 10 ~ 30 ms), no cloud dependency

**UI Layout**

| Area | Content |
|----------|---------|
| Left — Waveforms | Captured Input (raw, playable), Processed Result (denoised, playable), Live Mic Input (Mic Test) |
| Right — Record & Process | Record switch, capture device selection, level bar, live frame count and elapsed time, output WAV path, processing result |
| Right — Function Switches | Denoise strength, microphone gain, two denoising states (Stationary / Non-stationary) |

> This demo performs noise suppression (ANS) only. It does not include acoustic echo cancellation (AEC), and no reference signal is required.

## Hardware Requirements

- Quectel Pi M2 SBC (RK3576)
- Onboard ES8389 codec (card0 / device0, microphone + speaker)
- Display device (HDMI / DSI screen)
- Keyboard and mouse

## Software Environment Setup

- OS: Debian 13 (default system on Quectel Pi M2, aarch64, with desktop environment)
- Display framework: Qt 6 Widgets (C++17)
- Audio: ALSA (libasound2)
- Algorithm: AIVOX AEC_ANS SDK (Linux aarch64 dynamic library + .mnn denoising model, not distributed with this repository)
- Dependencies:
  - Qt6 Base (Widgets)
  - libasound2 / libasound2-dev / alsa-utils
  - CMake + Ninja + g++ (C++17)
  - Runtime libraries such as libgl1-mesa-dev, qt6-qpa-plugins, libxcb-cursor0

### Implementation Steps

1. Open a terminal on the board and clone the project with git.

```bash
sudo apt update
sudo apt install -y git
git clone https://github.com/Quectel-Pi/demo-edge-algorithms.git
cd demo-edge-algorithms/ans_project
```

When this finishes, an ans_project folder should appear in the current directory.

2. Unpack the algorithm package under /home/q/ans_project/.

3. Run the following commands in sequence on the board. When the terminal prints [OK] Toolchain installed, deployment is complete.

```bash
sudo chmod 755 scripts/*.sh
sudo bash scripts/setup_toolchain.sh
```

If setup_toolchain.sh reports an error, do not continue with the following steps. First confirm that the network connection is normal, then go back to the project root and run it again.

4. Build and run (please use a regular user such as q, not sudo. Otherwise build/ and output/ will be owned by root and saving WAV files will fail with errno=13).

```bash
bash scripts/build.sh
bash scripts/run.sh
```

The startup log prints the currently effective capture / playback devices:

```
[MIXER] configuring ES8389 playback routes...
  numid=57 spk switch      on
CAP=default  PLAY=default  OUTPUT=/home/q/ans_demo/output
```

## Project Structure

```
ans_project/
├── CMakeLists.txt                    # Build configuration
├── README.md                         # Chinese project description
├── README_EN.md                      # English project description
├── AIVOX-TTS-ZH-LITE-V2.0-30k-vc/    # Algorithm package
├── src/                              # Source code directory
│   ├── main.cpp                      # Main entry point (config injection + WAV writing)
│   ├── main.h                        # appconfig / wav declarations
│   ├── audio_types.h                 # AudioFrame / ProcessorConfig / ProcessStats
│   ├── audio_capture.*               # ALSA capture thread and playback (AudioPlayback)
│   ├── ans_processor.*               # Algorithm wrapper (only module touching the SDK) + streaming worker
│   └── main_window.*                 # UI (fixed 1240×762 canvas)
└── scripts/
    ├── setup_toolchain.sh            # Toolchain setup script
    ├── build.sh                      # Build script (CMake + Ninja)
    └── run.sh                        # Run script (injects SDK path / license / mixer settings)
```

include/, lib/ and models/ are provided by the algorithm package and stay decoupled from the project. The project accesses the algorithm only through src/ans_processor.*.

## Run the Program

Launch by running run.sh from the project root.

```bash
cd ~/ans_project
bash scripts/run.sh                 # use default capture / playback devices
bash scripts/run.sh --cap hw:0,0    # specify the capture device
```

After launch:

1. Pick the capture device on the Record & Process card, then click **Start Recording** to begin recording and denoising;
2. Watch the three waveforms in real time: raw (blue), denoised (green), Mic Test (orange);
3. Click **Stop** to finish recording. Results are saved as output/recorded_input.wav and output/recorded_processed.wav;
4. Click **▶ Play Input** / **▶ Play Output** to compare them through the speaker;
5. Click **Exit** to quit.

## Troubleshooting

| Symptom | Diagnosis |
|---|---|
| No playback sound | Run amixer -c 0 cget numid=57 and confirm spk switch is on (this driver version does not support setting Speaker Switch by name); also confirm numid=51/52 volume and numid=59 Headphone Switch are off |
| "Host is down" (error -112) | default is taken over by PipeWire / PulseAudio. Use ANS_PLAYBACK_DEV=plughw:0,0, or stop PipeWire and retry |
| Playback sound is too fast / pitch shifted | The codec clock does not match the requested sample rate. First compare with aplay -D plughw:0,0 -c1 -r16000 -f S16_LE output/recorded_input.wav, then handle it according to the actual supported rates shown by aplay -D hw:0,0 --dump-hw-params |
| Saving WAV reports errno=13 | output/ or an old WAV file is owned by root: run sudo chown -R $(id -u):$(id -g) output, and run as a regular user |
| License failure | Confirm ANS_AUTH_CODE, network access to the license server, /data is writable, and /dev/ttyGS0 exists |

See ANS_DEMO_GUIDE.md for more details.

## Report an Issue

If you run into any problems while using this project, feel free to submit a technical inquiry on the [Quectel official forum](https://forumschinese.quectel.com/c/quectel-pi/58). Our support team will get back to you promptly.

Issues and pull requests are welcome to help improve this project.

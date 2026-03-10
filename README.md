# AEC3 Echo Canceller

A VST/VST3 plugin for real-time acoustic echo cancellation using Google's WebRTC AEC3 engine with WASAPI loopback capture as the far-end reference signal.

## Features
- Real-time echo cancellation powered by WebRTC AEC3
- WASAPI loopback capture for far-end reference (captures system audio output)
- Normal and Aggressive mode (tuned for speaker setups)
- Monitor Capture mode for verifying the loopback signal
- Works at any host sample rate (internal resampling to 48kHz)
- Auto-reconnect on audio device changes
- Compatible with all major DAWs and audio hosts

## Building the Plugin

### Requirements
- Windows 10/11
- [JUCE 8](https://juce.com) (Projucer)
- Visual Studio 2022 or later (C++17, x64)

### Steps
1. Clone this repository
2. Install JUCE 8 and note the install path
3. Open `AEC3 Echo Canceller.jucer` in Projucer
4. Update module paths if your JUCE install differs from `C:/JUCE8/modules`
5. Save/export the project
6. Open the generated Visual Studio solution in `Builds/VisualStudio2026/`
7. Build Release x64

The `thirdparty/AEC3/` directory contains all required headers and pre-built static libraries — no additional dependencies needed.

## Credits & Licenses
- This plugin uses [WebRTC AEC3](https://webrtc.googlesource.com/src/) © The WebRTC project authors (BSD-3-Clause).
- [Abseil](https://github.com/abseil/abseil-cpp) © The Abseil Authors (Apache-2.0).
- [JsonCpp](https://github.com/open-source-parsers/jsoncpp) © Baptiste Lepilleur (MIT).
- Built with [JUCE](https://juce.com), subject to its license.

See `THIRD_PARTY_NOTICES.md` for full license texts.

This code is released under the GPLv3 license.
See the `LICENSE` file for details.

## VST2 Support Disclaimer

Due to licensing restrictions from Steinberg, the VST2 SDK cannot be distributed with this project.
Building VST2 versions of this plugin is only possible if you already have the VST2 SDK from a previous license agreement with Steinberg.

No VST2 SDK files are included in this repository, and I cannot provide them.
VST3 build is fully supported and open source.

## Tools
This project was made with the help of Claude Code (Opus 4.6) by Anthropic.

# AEC3 Echo Canceller

**Version 6.0.0**

A VST3/Standalone plugin for real-time acoustic echo cancellation using Google's WebRTC AEC3 engine with WASAPI loopback capture as the far-end reference signal.

## Features
- Real-time echo cancellation powered by WebRTC AEC3, followed by the WebRTC noise suppressor (default 12 dB)
- WASAPI loopback capture of a reference device as the far-end signal (captures system audio output)
- Compact fixed-size window (440x236) with a simple main view and an Advanced view
- Three echo strengths (Gentle / Normal / Strong) plus a Custom state when you tune the Advanced sliders
- 14 tuning sliders for normal and double-talk modes, plus Bounded echo level, Clock drift and Hear reference switches (Advanced)
- Noise removal: Off / 6 / 12 / 18 / 21 dB
- User presets (Save/Load .ini) and Factory reset
- Reference device picker with automatic device change detection and auto-reconnect
- Input, reference and output meters, and a reference spectrum analyzer
- Delay compensation (0-200 ms) for wireless mics / ASIO routing
- "Hear reference" and "Hear removed" monitoring for checking what the plugin is working with
- Works at any host sample rate (SpeexDSP resampling to the 48 kHz processing rate)
- Compatible with all major DAWs and audio hosts (VST3, VST, Standalone)

## User Interface

### Main view
- **Header:** title, speaker-icon reference device picker, **Advanced** button, and the **On** pill (enable/bypass).
- **1 - ECHO box:** strength (Gentle / Normal / Strong), **Delay** (0-200 ms), **Room** (13-40), IN and REF meters, **Hear reference**, and a connection dot with the reference sample rate.
- **2 - NOISE box:** noise removal (Off / 6 / 12 / 18 / 21 dB), the OUT meter, and **Hear removed**.

### Advanced (same window)
- **Echo** and **Double-talk** tabs: compact sliders. The tick on each slider marks Normal, the WebRTC default. Moving any slider switches the strength to **Custom**. A help strip explains the slider under the cursor.
- **Other** tab: Bounded echo level, Clock drift, Hear reference, Save/Load preset, Factory reset, the reference spectrum, and a status line (Connected / Connecting / Disconnected / Stalled / Error / AEC3 Error).

## Processing and Latency
Signal chain: host input -> (resample to 48 kHz if needed) -> WebRTC AEC3 -> WebRTC noise suppressor -> (resample back) -> host output. The noise suppressor is the WebRTC revision `b58dcf8fec` (2026-10-02), default 12 dB. SpeexDSP resampler (quality 5) is used for hosts and reference devices that are not 48 kHz.

**Reported latency is a firm 30 ms at every host sample rate from 16 kHz up, and in every mode, including bypass** (1440 samples at 48 kHz, 1323 at 44.1 kHz, 2880 at 96 kHz). Below 16 kHz the resampler adds enough delay that the pipeline needs slightly more than 30 ms, so the plugin reports that true figure instead. The host therefore sees the same latency whether the plugin is on or bypassed, so toggling it does not shift the audio.

Why 30 ms: the natural pipeline delay at 48 kHz is 1198 samples (about 25 ms) = 480-sample frame buffering + 46 band-split + 384 AEC3 block framing + 288 noise suppressor. The remainder up to 30 ms is padding that keeps the figure fixed. A 20 ms target (the original ask) is physically impossible with this pipeline.

## What's new in 6.0.0
- Simpler window in the style of the Noise Suppression plugin: a main view with ECHO and NOISE boxes, everything else under Advanced.
- WebRTC noise suppression after echo cancellation (default 12 dB).
- AEC3 rebuilt from WebRTC revision `b58dcf8fec` (2026-10-02); Normal strength uses WebRTC's own defaults.
- JUCE 9, SpeexDSP resampling, and a fixed 30 ms latency that stays the same when bypassed.
- Factory reset, device reconnects and saved state are more robust; damaged saved values fall back to defaults.

## Upgrade Notes (from v4.1.0 / v5.0.0)
- Existing projects and v5 `.ini` presets load with their saved values (range-checked; missing values fall back to defaults).
- They gain noise removal, at the 12 dB default.
- If the loaded values do not match Gentle / Normal / Strong, the strength shows **Custom**.
- The vendor is now **RadegasTCZ**; plugin IDs are unchanged, so existing projects still find the plugin.

## Building the Plugin

### Requirements
- Windows 10/11
- [JUCE 9](https://juce.com) at `C:\JUCE9` (Projucer)
- Visual Studio 2026 (toolset v145, C++20, x64)
- Release x64 uses `AEC3.lib`; Debug x64 uses `AEC3_debug.lib` (both in `thirdparty/AEC3/lib/`)

### Steps
1. Clone this repository.
2. Re-save the **plugin** project first, then the **tests** project (the tests force-include the plugin's generated `JucePluginDefines.h`, so this order matters):
   ```
   C:\JUCE9\Projucer.exe --resave "AEC3 Echo Canceller.jucer"
   C:\JUCE9\Projucer.exe --resave Tests\AEC3Tests.jucer
   ```
   Update module paths in the `.jucer` files if your JUCE install differs from `C:/JUCE9/modules`.
3. Build the plugin:
   ```
   MSBuild "Builds/VisualStudio2026/AEC3 Echo Canceller.sln" /p:Configuration=Release /p:Platform=x64 /m
   ```
   (Use `Configuration=Debug` for a Debug build.) The VST3 and Standalone builds need nothing else. The VST (VST2) project only builds if you have the VST2 SDK (see below); without it that one project fails and the others still build. To skip it, remove VST from the plugin formats in Projucer.

`thirdparty/AEC3/` contains the required headers and the pre-built static libraries.

### Running the tests
```
MSBuild "Tests/Builds/VisualStudio2026/AEC3Tests.sln" /p:Configuration=Release /p:Platform=x64 /m
Tests\Builds\VisualStudio2026\x64\Release\ConsoleApp\AEC3Tests.exe [--category X]
```
Exit code 0 means all tests passed. Run Debug the same way with `Configuration=Debug`.

### Rebuilding the AEC3 libraries (optional)
The recipe for the pre-built libraries (CMake + Ninja, the exact WebRTC and Abseil revisions, and the small stub sources) is in [`tools/aec3-build/`](tools/aec3-build/README.md).

## Credits & Licenses
- This plugin uses [WebRTC AEC3](https://webrtc.googlesource.com/src/) © The WebRTC project authors (BSD-3-Clause).
- [Abseil](https://github.com/abseil/abseil-cpp) © The Abseil Authors (Apache-2.0).
- [SpeexDSP](https://gitlab.xiph.org/xiph/speexdsp) resampler © Xiph.Org Foundation (BSD-3-Clause).
- Built with [JUCE 9](https://juce.com), used under the AGPLv3.

See `THIRD_PARTY_NOTICES.md` for full license texts.

Copyright (C) 2026 RadegasTCZ. This program is free software, released under the GNU General Public License v3 (see `LICENSE`). JUCE, which it is built with, is under the AGPLv3; see `THIRD_PARTY_NOTICES.md`.

## VST2 Support Disclaimer

Due to licensing restrictions from Steinberg, the VST2 SDK cannot be distributed with this project.
Building VST2 versions of this plugin is only possible if you already have the VST2 SDK from a previous license agreement with Steinberg.

No VST2 SDK files are included in this repository, and I cannot provide them. Releases ship VST3 and Standalone builds only.
VST3 build is fully supported and open source.

## Tools
This project was made with the help of Claude Code by Anthropic.

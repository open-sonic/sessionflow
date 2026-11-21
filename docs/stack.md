# Stack

Arrangement-first DAW for producers. No session view, no clip launching, no live performance features.

## Base

| Topic | Decision |
|---|---|
| License | GPLv3 |
| Language | C++20 |
| Build | CMake presets, Ninja, FetchContent |
| Platforms | Windows, macOS, Linux. All three in CI from the start |
| CI | GitHub Actions |

## Libraries

| Area | Library | License |
|---|---|---|
| Audio I/O | RtAudio (ASIO, WASAPI, CoreAudio, JACK, ALSA, PulseAudio) | MIT-style, ASIO SDK GPLv3 |
| MIDI | libremidi | BSD |
| Plugin hosting | VST3 SDK, CLAP, AU on macOS | MIT, MIT, Apple |
| UI | Dear ImGui on SDL3 | MIT, zlib |
| Project model and undo | immer | Boost |
| Library index | SQLite | Public domain |
| Audio files | dr_wav, dr_flac, dr_mp3 | Public domain |
| Resampling | r8brain-free-src | MIT |
| Time stretch | Signalsmith Stretch, plus own transient mode | MIT |
| Lock-free queues | Own SPSC ring buffer, readerwriterqueue | BSD |
| Project file | JSON via nlohmann/json | MIT |
| Tests | Catch2 v3 | BSL |
| Logging | spdlog, never on the audio thread | MIT |

Not using JUCE: its open-source license is AGPLv3 and it would own the engine model.

## Architecture decisions

| Topic | Decision | Why |
|---|---|---|
| Project model | Immutable snapshots (immer). Every edit produces a new version | Undo is a pointer swap. Audio thread reads snapshots without locks |
| Plugins | Hosted out of process | A crashing plugin must not take the project down |
| Engine | Multicore audio graph with plugin delay compensation, sends, sidechain | Required from the first design, hard to retrofit |
| Waveforms | Peak cache files on disk | Large projects must scroll without decoding audio |
| Rendering | Offline bounce faster than real time, freeze and flatten | |

## Real-time checks

RealtimeSanitizer (Clang 20+), ASan and UBSan in CI.

## Out of scope for now

Session view, clip launching, Ableton Link, controller scripting.

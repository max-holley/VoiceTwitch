# VoiceTwitch

Lightweight native desktop app that reads Twitch chat aloud with local, offline
[Piper](https://github.com/OHF-Voice/piper1-gpl) text-to-speech. No cloud TTS,
no Electron, no Python at runtime. It can also fine-tune a Piper voice on your
own recordings so chat is read in your voice.

- C++20, CMake, vcpkg
- onnxruntime (CPU) + espeak-ng phonemizer → Piper ONNX voices
- Dear ImGui dashboard (GLFW; Direct3D 11 on Windows, OpenGL 3 on macOS/Linux)
- Twitch OAuth (implicit grant, `chat:read` only) + IRC over WebSocket via libcurl
- miniaudio for playback and microphone capture

## Install on Windows

The ready-built installer in this checkout is
[VoiceTwitch-0.1.0-win64.exe](build/installer/VoiceTwitch-0.1.0-win64.exe).
This link works locally when the installer is present; the EXE has not yet been
published to a [GitHub release](https://github.com/max-holley/VoiceTwitch/releases)
for public download.

Copy just the installer EXE to a Windows x64 PC and run it with administrator
permission. It includes the app, its supporting files, and the Visual C++
runtime, which setup installs only when needed. After installation, download
a voice from **Voice Models** and connect your Twitch account. Python is only
required for voice cloning.

## Features (v1)

| Screen | What it does |
| --- | --- |
| **Activity** | Live feed of chat messages and what happened to each (spoken / filtered / ignored), queue and counters, skip / clear, send a test message through the full pipeline |
| **Voice Models** | Installed voices (downloaded + cloned), set active voice, preview, delete, multi-speaker selection; browse and download any voice from Piper's catalog with its **model card / license** shown before download |
| **Voice Cloning** | Guided recording of ~200 scripted prompts (Space to record/stop, arrow keys to move), automatic cleanup, then one-click fine-tuning and export to a usable voice |
| **Twitch Connection** | Log in/out, channel, connection status, auto-connect |
| **Filters** | Editable blocklist (word / phrase / `prefix*` / `*suffix` / `*anywhere*`), import from text file, live tester |
| **Settings** | Start/stop reading, volume, speaking rate, sentence pause, username announcement, command skipping, max length, cloning options |

### Pipeline

```
Twitch IRC (wss) ──► bad-word filter ──► FIFO queue ──► Piper synthesis ──► audio out
       (IMessageSource)   (drop, never queued)  (no overlap)   (ITtsEngine, per sentence)
```

Filtering happens before queueing, so blocked messages cost no queue time or
synthesis. Synthesis streams sentence by sentence so speech starts after the
first sentence is ready. `ITtsEngine` and `IMessageSource` are the extension
points for a second TTS engine or EventSub events later.

## Measured on the dev machine (Windows 11, Release build)

| Requirement | Target | Measured |
| --- | --- | --- |
| Startup to usable window | < 2 s | ~0.2–0.7 s (voice loads async after) |
| Memory with `en_US-lessac-medium` loaded | < 200 MB | ~137 MB working set |
| Idle CPU while connected/idle | negligible | ~0.2 % of one core |
| Synthesis speed | keep up with chat | real-time factor ~0.06–0.08 (≈ 12–16× faster than real time) |

## Building

Prerequisites: CMake ≥ 3.25, Ninja, a C++20 compiler, git, and vcpkg
(`VCPKG_ROOT` set). onnxruntime is downloaded as an official prebuilt release
and espeak-ng is built from source automatically.

**Windows** (Visual Studio 2022+/Build Tools with C++; VS's bundled vcpkg works):

```bat
scripts\build-windows.cmd release
build\windows-release\voicetwitch.exe
```

**Linux**

```sh
sudo apt install ninja-build pkg-config libx11-dev libxrandr-dev libxinerama-dev \
  libxcursor-dev libxi-dev libgl1-mesa-dev libsecret-1-dev
cmake --preset linux-release && cmake --build --preset linux-release
```

**macOS (Apple Silicon)**

```sh
brew install ninja
cmake --preset macos-release && cmake --build --preset macos-release
open build/macos-release/VoiceTwitch.app
```

Tests: `ctest --preset <preset>` (set `VT_TEST_MODEL=/path/voice.onnx` to also
run an end-to-end synthesis test). Package: `cpack -G ZIP` in the build dir.

### Windows installer

Install NSIS 3.03+ on the build PC, then run from the repository root in
PowerShell:

```powershell
.\scripts\build-windows.cmd release
$cpackPath = (Select-String -Path build/windows-release/CMakeCache.txt -Pattern '^CMAKE_CPACK_COMMAND:').Line.Split('=', 2)[1]
& $cpackPath --config build/windows-release/CPackConfig.cmake -G NSIS -B build/installer
```

The resulting `build/installer/VoiceTwitch-<version>-win64.exe` includes the
Microsoft Visual C++ x64 Redistributable supplied with the build PC's MSVC
installation. Configuration validates its Microsoft signature and reads its
version. To supply it separately, configure with
`-DVT_VC_REDIST=C:/path/to/vc_redist.x64.exe`.

Setup checks both registry views for an installed x64 runtime and skips the
prerequisite when the same or a newer version is present. Otherwise it installs
the bundled runtime silently before copying the app, with no download needed
on the target PC. Setup requires administrator privileges. Runtime errors stop
setup; a requested reboot is passed to the installer without forcing a restart.
Uninstalling VoiceTwitch leaves the shared Visual C++ runtime installed.
The portable ZIP does not install this prerequisite automatically.

The Windows executable includes the app icon for Explorer and the taskbar.
macOS builds produce `VoiceTwitch.app` with its Dock icon and runtime files
inside the bundle. For headless commands on macOS, use
`VoiceTwitch.app/Contents/MacOS/VoiceTwitch`. X11 windows use the same artwork.

Headless synthesis (handy for scripting/benchmarks):

```sh
voicetwitch --say "Hello chat" --model path/to/voice.onnx [--wav out.wav] [--rate 1.2]
```

## First run

1. **Twitch**: open *Twitch Connection* and click *Log in with Twitch*. The app
   uses a built-in public Client ID (`kTwitchClientId` in `src/core/Config.h`),
   so users need no setup. Port 17563 must be free during login (it must match
   the OAuth Redirect URL `http://localhost:17563` registered for the app).
2. **Voice**: in *Voice Models*, download e.g. `en_US-lessac-medium` and click *Use*.
3. Chat is read aloud as soon as the channel is joined. Use *Stop reading* to pause.

## Data locations

`%APPDATA%\VoiceTwitch` / `~/Library/Application Support/VoiceTwitch` /
`~/.local/share/VoiceTwitch` (override with `VOICETWITCH_DATA_DIR`):

```
config.json        settings, active voice, channel
filters.json       blocklist
models/            downloaded Piper voices (+ cached voices.json catalog)
cloned-voices/     cloning projects: recordings, metadata.csv, trained model
training-env/      Python venv for cloning (created on demand)
voicetwitch.log
```

The Twitch token is kept in the OS credential store (Windows Credential
Manager, macOS Keychain, Secret Service via libsecret on Linux; if libsecret is
unavailable at build time it falls back to a `0600` file and logs a warning).

## Voice cloning

Inference is pure C++, but Piper's training tools are Python/PyTorch, so
fine-tuning runs out of process via [`training/train_voice.py`](training/README.md).
The app requires a **separately installed Python 3.9+** (nothing bundled); on
first use it creates `training-env/` and installs Piper's training package.
Training needs a compiler + CMake for Piper's native extension, several GB of
disk, and realistically an NVIDIA GPU; it is a one-off offline task.

## Licensing notes

- **espeak-ng is GPL-3.0-or-later** and is statically linked, so distributed
  VoiceTwitch binaries must be GPL-3.0 compatible. Choose the project license
  accordingly.
- onnxruntime (MIT), Dear ImGui (MIT), GLFW (zlib), miniaudio (MIT-0/Unlicense),
  nlohmann/json (MIT), libcurl (curl license).
- Piper voices are downloaded from `huggingface.co/rhasspy/piper-voices`; each
  voice has its own license/dataset terms in its `MODEL_CARD`, which the app
  shows before and after download.

## Decisions on the spec's open questions

| Question | Decision |
| --- | --- |
| Python for training | Required as a separate install (3.9+), not bundled; venv created on demand. Keeps the shipped app free of a Python runtime. |
| GUI backend | GLFW + Direct3D 11 on Windows (OpenGL drivers added ~90 MB RSS on the test machine), GLFW + OpenGL 3 on macOS/Linux. |
| Package manager | vcpkg (manifest mode). onnxruntime uses the official prebuilt release instead of the vcpkg port (which compiles abseil/protobuf/onnx from source); espeak-ng has no port and is built via ExternalProject. |
| Voice source | `rhasspy/piper-voices` `voices.json` on Hugging Face; licenses surfaced through each voice's MODEL_CARD. |

## Not in v1

Sub/bits/raid announcements, per-user voices, advanced queue rules, emote/URL
stripping, mod chat commands — see the roadmap in the build document.

# Voice cloning (training)

VoiceTwitch's inference is pure C++ (onnxruntime + espeak-ng). Producing a *new*
voice, however, means fine-tuning a Piper VITS model, and Piper's training code
is Python/PyTorch. `train_voice.py` wraps that so the app can drive it.

## Windows installer

From v0.1.1, Windows release packages include a private Python runtime, CPU
PyTorch, Piper, training dependencies and precompiled native extensions. Leave
**Python executable** blank in Settings to use it. Users need no separate Python,
compiler or pip setup. The runtime is isolated from system Python and installed
packages. Setup is performed on the release build machine, not the user's PC.

CPU training can take many hours. The base checkpoint is still downloaded on
first use; it is model data rather than an installer dependency. NVIDIA drivers
and CUDA PyTorch are not bundled. For advanced CUDA setup, select a separate
Python executable, let setup create `<data>/training-env`, then install CUDA
PyTorch into that generated environment before training.

The bundle and installer still need a successful Windows CI run before v0.1.1
can be published; the already published v0.1.0 installer does not include them.

## Source builds / explicit Python override

The following setup path is used when no bundled runtime is present, or a
Python executable is explicitly configured in Settings.

## What the app does

1. **Record** – the Voice Cloning screen walks through `assets/recording_script.txt`.
   Each take is DC-corrected, silence-trimmed, peak-normalized to -1 dBFS and
   resampled to 22.05 kHz mono 16-bit, then saved as
   `cloned-voices/<name>/wavs/utt_NNNN.wav`.
2. **Setup (first run only)** – finds 64-bit CPython 3.11-3.13 (`py -3.12` / `python3.12`, or the
   path set in Settings) and runs `train_voice.py setup`, which creates
   `<data dir>/training-env`, downloads Piper `v1.8.0` source and installs
   `piper-tts[train]` (PyTorch, Lightning, …) plus the `monotonic_align`
   extension.
3. **Train** – writes `metadata.csv`, then runs `train_voice.py train` with the
   venv interpreter. It downloads (once) the base checkpoint configured in
   Settings (default: `en_US-lessac-medium` from `rhasspy/piper-checkpoints`)
   and runs `python -m piper.train fit --ckpt_path <base> ...` for N extra
   epochs.
4. **Export** – `python -m piper.train.export_onnx`, then copies the model and
   its config to `cloned-voices/<name>/<name>.onnx(.json)`, where Voice Models
   picks it up as a "Cloned" voice.

## Requirements for building the runtime or using external Python

* Standard 64-bit CPython 3.11–3.13 (3.12 recommended) installed separately (not bundled).
* A C/C++ compiler + CMake for Piper's native extension
  (Windows: Visual Studio Build Tools; Linux: `build-essential cmake ninja-build`).
* ~6–10 GB disk for PyTorch and the ~800 MB base checkpoint.
* An NVIDIA GPU is strongly recommended. CPU training works but is very slow.
  Linux (or WSL2) is the best-trodden path for Piper training.

## Running manually

```sh
python3 train_voice.py setup --venv ~/.local/share/VoiceTwitch/training-env
~/.local/share/VoiceTwitch/training-env/bin/python train_voice.py train \
  --project-dir ~/.local/share/VoiceTwitch/cloned-voices/myvoice \
  --name myvoice --output ~/.local/share/VoiceTwitch/cloned-voices/myvoice/myvoice.onnx \
  --base-checkpoint-url "https://huggingface.co/datasets/rhasspy/piper-checkpoints/resolve/main/en/en_US/lessac/medium/epoch%3D2164-step%3D1355540.ckpt" \
  --cache-dir ~/.local/share/VoiceTwitch/training-cache --epochs 300 --batch-size 16
```

Training on a GPU box and copying the resulting `.onnx` + `.onnx.json` into a
folder under `cloned-voices/` works too.

## If external-Python setup previously failed

Install **standard 64-bit Python 3.12** from python.org. In VoiceTwitch, leave the
Python executable blank to auto-detect it, or select its `python.exe` in Settings
> Voice cloning, then retry training. The app tries 3.12 before 3.11 and 3.13 and
rejects 32-bit, free-threaded, and unsupported Python builds. An incompatible
partially installed training environment is recreated automatically; recordings
and cached checkpoints are kept.

`No matching distribution found for onnxruntime` means pip has no compatible
package for the selected interpreter/platform or configured package index. It
is not a C++ compiler error. The training output now includes the selected Python
path and retains pip's actual error. If it persists with standard x64 Python
3.12, check pip's index/proxy/network settings. Native alignment compilation
still requires the C++ build tools listed above.

## Building the distributable runtime

On a Windows x64 release builder with Python 3.13 and C++ Build Tools, run:

```powershell
python scripts/bundle-training-runtime.py --output training-runtime
```

The builder vendors dependencies alongside the official embedded CPython
3.13.17 distribution. It replaces Piper's editable install with copied package
code/native modules and preserves dependency licenses. It then tests the moved
runtime with poisoned `PYTHONHOME`/`PYTHONPATH` and without Python/compiler tools
on PATH: CPU tensor computation, phonemization, native alignment, ONNX export,
ONNX Runtime inference and training CLI loading. A readiness marker is written
only on success. CMake refuses to package a missing/incomplete bundle by default.
The workflow repeats this check on the payload installed by the actual NSIS EXE.

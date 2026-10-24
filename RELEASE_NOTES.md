VoiceTwitch v0.1.1 fixes Python detection and misleading dependency-install errors in voice cloning.

- Prefer standard 64-bit Python 3.12, with 3.11 and 3.13 as alternatives, instead of blindly selecting the newest installed Python.
- Reject unsupported versions, 32-bit interpreters and free-threaded builds before downloading training dependencies.
- Repair an incompatible environment left behind by a failed setup without removing voice recordings or cached checkpoints.
- Show the selected Python interpreter and preserve pip's actual error instead of reporting all failures as missing C++ build tools.

**Windows:** the new installer includes a private Python/Piper/PyTorch CPU training
runtime and precompiled extensions. Users no longer need to install Python,
pip, CMake or Visual Studio Build Tools. Leave the Python executable blank in
Settings > Voice cloning to use the bundled runtime. System Python is untouched.

CPU training can take many hours. NVIDIA acceleration requires a separate
external Python setup, with CUDA PyTorch installed into the generated training
environment. The base voice checkpoint
(~800 MB) is still downloaded on first training use. The larger installer now
contains the training dependencies as well as the app and Visual C++ runtime.

The release workflow must validate the relocated runtime, build/test the app,
and run the installed Windows payload's training smoke check before publishing.
Full voice training on individual PCs and GPU configurations still requires testing.

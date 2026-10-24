VoiceTwitch v0.1.1 fixes Python detection and misleading dependency-install errors in voice cloning.

- Prefer standard 64-bit Python 3.12, with 3.11 and 3.13 as alternatives, instead of blindly selecting the newest installed Python.
- Reject unsupported versions, 32-bit interpreters and free-threaded builds before downloading training dependencies.
- Repair an incompatible environment left behind by a failed setup without removing voice recordings or cached checkpoints.
- Show the selected Python interpreter and preserve pip's actual error instead of reporting all failures as missing C++ build tools.

**Windows:** download `VoiceTwitch-0.1.1-win64.exe` and install the update. Install standard **64-bit Python 3.12** from python.org separately for voice training. Leave the Python executable blank in Settings > Voice cloning, or select that installation's `python.exe`, then retry Start training. Visual Studio Build Tools with **Desktop development with C++** is still required for Piper's native training extension.

This update targets training environment setup. Full voice training on individual PCs and GPU configurations still requires testing. The release workflow checks real Windows Python 3.12 dependency installation and training imports, and builds/tests the desktop application on Windows, Linux and macOS before publishing.

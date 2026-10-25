#!/usr/bin/env python3
"""Build a relocatable Windows training runtime on the release build machine.

Compilation and pip run here, never on the user's PC. The official embedded
CPython distribution loads vendored site-packages with no system Python lookup.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import urllib.request
import zipfile
import venv

PYTHON_VERSION = "3.13.17"
TORCH_VERSION = "2.9.1+cpu"
ROOT = Path(__file__).resolve().parents[1]


def stage_packages(env: Path, runtime: Path) -> None:
    """Copy packages as ordinary imports, replacing the editable Piper install."""
    packages = runtime / "Lib" / "site-packages"
    shutil.copytree(env / "Lib" / "site-packages", packages,
                    ignore=shutil.ignore_patterns("__pycache__", "*.pyc"))
    for pattern in ("__editable__*piper*", "piper*.egg-link"):
        for path in packages.glob(pattern):
            if path.is_file():
                path.unlink()
    piper = env / "src" / "piper1-gpl-1.8.0"
    shutil.copytree(piper / "src" / "piper", packages / "piper", dirs_exist_ok=True,
                    ignore=shutil.ignore_patterns("__pycache__", "*.pyc"))
    # Preserve upstream licensing and the source version alongside the binary.
    licenses = runtime / "licenses" / "piper"
    licenses.mkdir(parents=True)
    shutil.copy2(piper / "COPYING", licenses / "COPYING")
    if (piper / "licenses").exists():
        shutil.copytree(piper / "licenses", licenses / "third-party")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if os.name != "nt" or sys.version_info[:2] != (3, 13):
        parser.error("Build with standard Windows x64 CPython 3.13")
    dest = args.output.resolve()
    if dest.exists():
        parser.error("Output must not exist; choose a fresh staging directory")
    spec = importlib.util.spec_from_file_location("training_helper", ROOT / "training/train_voice.py")
    helper = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(helper)
    if helper.cmd_check_python():
        raise SystemExit(1)
    with tempfile.TemporaryDirectory(prefix="voicetwitch-runtime-") as temp:
        temp = Path(temp)
        env = temp / "build-env"
        # Use the same setup path as a source build, but preinstall the CPU torch
        # wheel explicitly to keep CUDA's multi-GB libraries out of the installer.
        venv.EnvBuilder(with_pip=True).create(env)
        py = helper.venv_python(env)
        subprocess.run([py, "-m", "pip", "install", f"torch=={TORCH_VERSION}",
                        "--index-url", "https://download.pytorch.org/whl/cpu"], check=True)
        if helper.cmd_setup(argparse.Namespace(venv=str(env))):
            raise SystemExit("Training setup failed; runtime will not be packaged")
        archive = temp / "python-embed.zip"
        url = (f"https://www.python.org/ftp/python/{PYTHON_VERSION}/"
               f"python-{PYTHON_VERSION}-embed-amd64.zip")
        urllib.request.urlretrieve(url, archive)
        runtime = temp / "runtime"
        with zipfile.ZipFile(archive) as zf:
            zf.extractall(runtime)
        stage_packages(env, runtime)
        (runtime / "python313._pth").write_text(
            "python313.zip\n.\nLib/site-packages\nimport site\n", encoding="utf-8")
        # Record the actual resolved dependency versions and download provenance.
        freeze = subprocess.check_output([py, "-m", "pip", "freeze", "--all"], text=True)
        (runtime / "packages.txt").write_text(freeze, encoding="utf-8")
        (runtime / "manifest.json").write_text(json.dumps({
            "python": PYTHON_VERSION, "python_url": url,
            "python_sha256": hashlib.sha256(archive.read_bytes()).hexdigest(),
            "piper": helper.PIPER_VERSION, "torch": TORCH_VERSION, "accelerator": "cpu",
        }, indent=2), encoding="utf-8")
        # Move away from the build venv and run with deliberately poisoned system
        # Python settings. The embedded runtime must resolve only its own files.
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.move(str(runtime), dest)
        smoke_env = dict(os.environ, PYTHONHOME=str(temp / "missing-python"),
                         PYTHONPATH=str(temp / "missing-packages"), PYTHONDONTWRITEBYTECODE="1")
        smoke_env["PATH"] = os.pathsep.join([
            str(Path(os.environ["SystemRoot"]) / "System32"), os.environ["SystemRoot"]])
        try:
            subprocess.run([dest / "python.exe", "-I", ROOT / "training/train_voice.py",
                            "check-runtime"], cwd=temp, env=smoke_env, check=True)
        except Exception:
            shutil.rmtree(dest)
            raise
        # Written only after imports, a CPU compute operation, phonemization,
        # alignment and the training/export CLI have succeeded after relocation.
        (dest / helper.READY_MARKER).write_text(helper.PIPER_VERSION, encoding="utf-8")
    print(f"Bundled runtime ready: {dest}", flush=True)


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""VoiceTwitch voice-cloning helper.

Fine-tunes a Piper voice on recordings made in the VoiceTwitch app, using the
official Piper training code (OHF-Voice/piper1-gpl). The app runs this script
out of process; inference never depends on Python.

Subcommands:
  setup  --venv DIR
      Creates a virtual environment and installs Piper's training tooling.
      Run with standard 64-bit CPython 3.11–3.13 (3.12 recommended).

  train  --project-dir DIR --name NAME --output OUT.onnx [options]
      Run with the venv's interpreter. Downloads the base checkpoint (cached),
      fine-tunes on DIR/metadata.csv + DIR/wavs, exports OUT.onnx and
      OUT.onnx.json.

Progress is reported on stdout as lines of the form:  @@VT {"stage": ...}
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import struct
import sysconfig
import re
import shutil
import subprocess
import sys
import urllib.request
import venv
import zipfile
from pathlib import Path

PIPER_VERSION = "v1.8.0"
PIPER_ZIP_URL = f"https://github.com/OHF-Voice/piper1-gpl/archive/refs/tags/{PIPER_VERSION}.zip"
READY_MARKER = ".voicetwitch-ready"


def report(stage: str, message: str | None = None, **extra) -> None:
    payload = {"stage": stage}
    if message is not None:
        payload["message"] = message
    payload.update(extra)
    print("@@VT " + json.dumps(payload), flush=True)


def run(cmd: list[str], cwd: Path | None = None, on_line=None, env_extra: dict | None = None) -> int:
    """Runs a command, streaming output line by line ('\\r' also ends a line so
    progress bars show up)."""
    print("$ " + " ".join(str(c) for c in cmd), flush=True)
    env = dict(os.environ, PYTHONUNBUFFERED="1", **(env_extra or {}))
    proc = subprocess.Popen(
        [str(c) for c in cmd],
        cwd=str(cwd) if cwd else None,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        env=env,
    )
    assert proc.stdout is not None
    buf = b""
    while True:
        chunk = proc.stdout.read1(4096) if hasattr(proc.stdout, "read1") else proc.stdout.read(1)
        if not chunk:
            break
        buf += chunk
        parts = re.split(rb"[\r\n]", buf)
        buf = parts.pop()
        for raw in parts:
            line = raw.decode("utf-8", errors="replace").rstrip()
            if not line:
                continue
            print(line, flush=True)
            if on_line:
                on_line(line)
    if buf:
        line = buf.decode("utf-8", errors="replace").rstrip()
        print(line, flush=True)
        if on_line:
            on_line(line)
    return proc.wait()


def venv_python(venv_dir: Path) -> Path:
    if os.name == "nt":
        return venv_dir / "Scripts" / "python.exe"
    return venv_dir / "bin" / "python"


def download(url: str, dest: Path, label: str) -> None:
    dest.parent.mkdir(parents=True, exist_ok=True)
    tmp = dest.with_suffix(dest.suffix + ".part")
    req = urllib.request.Request(url, headers={"User-Agent": "VoiceTwitch"})
    with urllib.request.urlopen(req) as resp, open(tmp, "wb") as out:
        total = int(resp.headers.get("Content-Length") or 0)
        done = 0
        last_pct = -1
        while True:
            block = resp.read(1 << 20)
            if not block:
                break
            out.write(block)
            done += len(block)
            if total:
                pct = int(done * 100 / total)
                if pct != last_pct:
                    last_pct = pct
                    report("download", f"Downloading {label}: {pct}%", progress=done / total)
    tmp.replace(dest)


# --------------------------------------------------------------------------- setup


def cmd_check_python(args: argparse.Namespace | None = None) -> int:
    """Shared preflight for auto-detection, manual setup, and existing venvs."""
    supported = (
        platform.python_implementation() == "CPython"
        and (3, 11) <= sys.version_info[:2] <= (3, 13)
        and struct.calcsize("P") == 8
        and not sysconfig.get_config_var("Py_GIL_DISABLED")
    )
    if not supported:
        print(
            f"Unsupported training Python: {platform.python_implementation()} "
            f"{platform.python_version()}, {struct.calcsize('P') * 8}-bit "
            f"({sys.executable}). Install standard 64-bit Python 3.12 from python.org "
            "and select it in Settings > Voice cloning. Training supports CPython "
            "3.11-3.13; 32-bit and free-threaded builds are not supported.",
            flush=True,
        )
        return 1
    print(f"Training Python: {sys.executable} ({platform.python_version()}, 64-bit)", flush=True)
    return 0


def install_dependencies(py: Path, piper_dir: Path) -> int:
    errors = []
    rc = run([py, "-m", "pip", "install", "-e", ".[train]"], cwd=piper_dir,
             on_line=lambda line: errors.append(line) if "ERROR:" in line else None)
    if rc:
        # Preserve pip's real cause in the status line rather than blaming every
        # failure (including missing wheels, proxies and indexes) on a compiler.
        detail = errors[-1] if errors else "see the training output above"
        print(f"Piper dependency installation failed: {detail}. "
              "Check the Python version/architecture and pip index or network settings. "
              "A compiler is only needed when the output reports a native build error.", flush=True)
    return rc


def cmd_setup(args: argparse.Namespace) -> int:
    venv_dir = Path(args.venv).resolve()
    if cmd_check_python() != 0:
        return 1

    # A failed install may have left a venv using an unsupported interpreter.
    # Recreate only that generated environment; recordings/checkpoints live elsewhere.
    (venv_dir / READY_MARKER).unlink(missing_ok=True)
    existing_py = venv_python(venv_dir)
    recreate = existing_py.exists() and run(
        [existing_py, __file__, "check-python"]
    ) != 0
    report("setup", "Recreating incompatible training environment..." if recreate
           else "Creating virtual environment...")
    venv.EnvBuilder(with_pip=True, clear=recreate).create(venv_dir)
    py = venv_python(venv_dir)

    report("setup", "Upgrading pip...")
    if run([py, "-m", "pip", "install", "--upgrade", "pip", "wheel", "setuptools"]) != 0:
        return 1

    src_root = venv_dir / "src"
    piper_dir = src_root / f"piper1-gpl-{PIPER_VERSION.lstrip('v')}"
    if not piper_dir.exists():
        report("setup", f"Downloading Piper {PIPER_VERSION} source...")
        zip_path = src_root / "piper1-gpl.zip"
        download(PIPER_ZIP_URL, zip_path, "Piper source")
        with zipfile.ZipFile(zip_path) as zf:
            zf.extractall(src_root)
        zip_path.unlink()

    report("setup", "Installing PyTorch + Piper training dependencies (this takes a while)...")
    if install_dependencies(py, piper_dir) != 0:
        return 1

    # The editable install does not build espeakbridge (needs espeak-ng built from
    # source). Pull the prebuilt binary and espeak data from the official wheel.
    pkg_dir = piper_dir / "src" / "piper"
    if not list(pkg_dir.glob("espeakbridge*.pyd")) and not list(pkg_dir.glob("espeakbridge*.so")):
        report("setup", "Fetching prebuilt espeak bridge...")
        wheel_dir = src_root / "piper-wheel"
        if run([py, "-m", "pip", "download", f"piper-tts=={PIPER_VERSION.lstrip('v')}", "--no-deps",
                "--only-binary=:all:", "-d", wheel_dir]) != 0:
            return 1
        for whl in wheel_dir.glob("*.whl"):
            with zipfile.ZipFile(whl) as zf:
                for name in zf.namelist():
                    if name.startswith("piper/espeak-ng-data/") or name.startswith("piper/espeakbridge."):
                        zf.extract(name, piper_dir / "src")
        shutil.rmtree(wheel_dir, ignore_errors=True)

    report("setup", "Building monotonic alignment extension...")
    ma_dir = piper_dir / "src" / "piper" / "train" / "vits" / "monotonic_align"
    # torch's ONNX exporter imports onnxscript, which piper's extras don't pull in.
    if run([py, "-m", "pip", "install", "cython", "onnxscript"]) != 0:
        return 1
    if run([py, "-m", "Cython.Build.Cythonize", "-i", "core.pyx"], cwd=ma_dir) != 0:
        print("Native alignment extension build failed. Install Visual Studio Build Tools "
              "with Desktop development with C++ on Windows (build-essential on Linux), "
              "then retry. See the compiler output above.", flush=True)
        return 1
    target = ma_dir / "monotonic_align"
    target.mkdir(exist_ok=True)
    built = [p for p in ma_dir.iterdir() if p.name.startswith("core") and p.suffix in (".so", ".pyd")]
    if not built:
        print("monotonic_align build produced no extension module", flush=True)
        return 1
    for p in built:
        shutil.move(str(p), str(target / p.name))

    # Sanity check: the training entry point imports.
    if run([py, "-c", "import piper.train, torch; print('torch', torch.__version__, 'cuda', torch.cuda.is_available())"]) != 0:
        return 1

    (venv_dir / READY_MARKER).write_text(PIPER_VERSION)
    report("setup", "Training environment ready")
    return 0


# --------------------------------------------------------------------------- train


def checkpoint_epoch(path: Path) -> int:
    import torch  # noqa: PLC0415 (only available inside the venv)

    ckpt = torch.load(str(path), map_location="cpu", weights_only=False)
    return int(ckpt.get("epoch", 0))


def strip_checkpoint_hparams(path: Path) -> None:
    """Drops legacy hyper_parameters so LightningCLI does not try to parse them.

    The upstream base checkpoint carries old-format hparams (dataset, gpus,
    resume_from_checkpoint, ...) that the current piper.train CLI rejects with
    "Parsing of ckpt_path hyperparameters failed!". Weights are unaffected; the
    model is built from the CLI arguments instead.
    """
    import torch  # noqa: PLC0415 (only available inside the venv)

    ckpt = torch.load(str(path), map_location="cpu", weights_only=False)
    if not ckpt.get("hyper_parameters"):
        return
    ckpt["hyper_parameters"] = {}
    tmp = path.with_suffix(".tmp")
    torch.save(ckpt, str(tmp))
    tmp.replace(path)


def make_portable_checkpoint(src: Path, dst: Path) -> None:
    """Re-saves a checkpoint pickled on Linux so it loads on Windows.

    Upstream checkpoints contain pathlib.PosixPath objects, which cannot be
    instantiated on Windows; unpickle them as WindowsPath instead.
    """
    import pathlib  # noqa: PLC0415

    import torch  # noqa: PLC0415 (only available inside the venv)

    modules = [pathlib]
    if hasattr(pathlib, "_local"):  # Python 3.13+ pickles as pathlib._local.PosixPath
        modules.append(pathlib._local)
    originals = [(m, m.PosixPath) for m in modules]
    for m in modules:
        m.PosixPath = pathlib.WindowsPath
    try:
        ckpt = torch.load(str(src), map_location="cpu", weights_only=False)
    finally:
        for m, orig in originals:
            m.PosixPath = orig
    tmp = dst.with_suffix(".tmp")
    torch.save(ckpt, str(tmp))
    tmp.replace(dst)


# torch>=2.6 defaults torch.load to weights_only=True, and Lightning passes that
# explicitly, so the TORCH_FORCE_NO_WEIGHTS_ONLY_LOAD env var does not help. Piper
# checkpoints hold hparams and pathlib objects that weights-only mode rejects, so
# run Piper's modules under a launcher that loads with weights_only=False. The
# checkpoints come from Piper's own release and from our own training runs.
_LAUNCHER = """import runpy, sys, torch
_load = torch.load
def _patched(*a, **k):
    k['weights_only'] = False
    return _load(*a, **k)
torch.load = _patched
# Piper adds a ModelCheckpoint monitoring val_mos, which is never logged when the
# UTMOS predictor is off, and Lightning then aborts the run. Turn it into a no-op.
from lightning.pytorch.callbacks import ModelCheckpoint
_init = ModelCheckpoint.__init__
def _ckpt_init(self, *a, **k):
    if k.get('monitor') == 'val_mos':
        k.update(monitor=None, save_top_k=0, save_last=False)
    _init(self, *a, **k)
ModelCheckpoint.__init__ = _ckpt_init
# torch>=2.9 exports through torch.export by default, which cannot trace Piper's
# spline asserts. Use the TorchScript-based exporter Piper was written against.
_export = torch.onnx.export
def _export_legacy(*a, **k):
    k.setdefault('dynamo', False)
    return _export(*a, **k)
torch.onnx.export = _export_legacy
mod = sys.argv[1]
sys.argv = [mod] + sys.argv[2:]
runpy.run_module(mod, run_name='__main__', alter_sys=True)
"""


def piper_module_cmd(module: str, *args) -> list:
    return [sys.executable, "-c", _LAUNCHER, module, *args]


def newest_checkpoint(root: Path) -> Path | None:
    ckpts = list(root.rglob("*.ckpt"))
    if not ckpts:
        return None
    return max(ckpts, key=lambda p: p.stat().st_mtime)


def cmd_train(args: argparse.Namespace) -> int:
    project = Path(args.project_dir).resolve()
    metadata = project / "metadata.csv"
    wavs = project / "wavs"
    output = Path(args.output).resolve()
    cache_dir = Path(args.cache_dir).resolve()

    if not metadata.exists() or not wavs.exists():
        print(f"Missing {metadata} or {wavs}", flush=True)
        return 1
    lines = [l for l in metadata.read_text(encoding="utf-8").splitlines() if l.strip()]
    report("prepare", f"{len(lines)} recordings found")

    # Base checkpoint (shared across projects).
    url_hash = hashlib.sha1(args.base_checkpoint_url.encode()).hexdigest()[:12]
    base_ckpt = cache_dir / f"base-{url_hash}.ckpt"
    win_ckpt = cache_dir / f"base-{url_hash}-win.ckpt"
    if os.name == "nt" and win_ckpt.exists():
        base_ckpt = win_ckpt
    else:
        if not base_ckpt.exists():
            report("download", "Downloading base checkpoint (~800 MB, one time)...")
            download(args.base_checkpoint_url, base_ckpt, "base checkpoint")
        if os.name == "nt":
            report("prepare", "Converting base checkpoint for Windows (one time)...")
            make_portable_checkpoint(base_ckpt, win_ckpt)
            base_ckpt.unlink()
            base_ckpt = win_ckpt

    report("prepare", "Reading base checkpoint...")
    strip_checkpoint_hparams(base_ckpt)
    start_epoch = checkpoint_epoch(base_ckpt)
    target_epoch = start_epoch + 1 + args.epochs

    work = project / "training"
    work.mkdir(exist_ok=True)
    config_out = work / "config.json"

    epoch_re = re.compile(r"Epoch (\d+)")

    def on_line(line: str) -> None:
        m = epoch_re.search(line)
        if m:
            epoch = int(m.group(1))
            done = max(0, epoch - start_epoch)
            report(
                "train",
                f"Epoch {done} of {args.epochs}",
                epoch=done,
                target_epoch=args.epochs,
                progress=min(1.0, done / max(1, args.epochs)),
            )

    report("train", "Starting training...", epoch=0, target_epoch=args.epochs, progress=0.0)
    rc = run(
        [
            *piper_module_cmd("piper.train"),
            "fit",
            "--data.voice_name", args.name,
            "--data.csv_path", metadata,
            "--data.audio_dir", wavs,
            "--model.sample_rate", str(args.sample_rate),
            "--data.espeak_voice", args.espeak_voice,
            "--data.cache_dir", work / "cache",
            "--data.config_path", config_out,
            "--data.batch_size", str(args.batch_size),
            "--ckpt_path", base_ckpt,
            "--trainer.max_epochs", str(target_epoch),
            "--model.mos_metric", "none",
            "--trainer.default_root_dir", work,
        ],
        cwd=work,
        on_line=on_line,
    )
    if rc != 0:
        return rc

    ckpt = newest_checkpoint(work / "lightning_logs") or newest_checkpoint(work)
    if ckpt is None:
        print("Training produced no checkpoint", flush=True)
        return 1

    report("export", "Exporting to ONNX...")
    tmp_onnx = work / "export.onnx"
    rc = run(piper_module_cmd("piper.train.export_onnx", "--checkpoint", ckpt, "--output-file", tmp_onnx))
    if rc != 0:
        return rc
    if not config_out.exists():
        print("Training did not write a voice config", flush=True)
        return 1

    output.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(tmp_onnx, output)
    shutil.copyfile(config_out, str(output) + ".json")
    report("done", f"Wrote {output.name}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)

    p_check = sub.add_parser("check-python", help="check training interpreter compatibility")
    p_check.set_defaults(func=cmd_check_python)

    p_setup = sub.add_parser("setup", help="create the training environment")
    p_setup.add_argument("--venv", required=True)
    p_setup.set_defaults(func=cmd_setup)

    p_train = sub.add_parser("train", help="fine-tune a voice")
    p_train.add_argument("--project-dir", required=True)
    p_train.add_argument("--name", required=True)
    p_train.add_argument("--output", required=True)
    p_train.add_argument("--espeak-voice", default="en-us")
    p_train.add_argument("--sample-rate", type=int, default=22050)
    p_train.add_argument("--base-checkpoint-url", required=True)
    p_train.add_argument("--cache-dir", required=True)
    p_train.add_argument("--epochs", type=int, default=300)
    p_train.add_argument("--batch-size", type=int, default=16)
    p_train.set_defaults(func=cmd_train)

    args = parser.parse_args()
    try:
        return args.func(args)
    except KeyboardInterrupt:
        return 130
    except Exception as exc:  # surface a readable error to the app
        print(f"Error: {exc}", flush=True)
        return 1


if __name__ == "__main__":
    sys.exit(main())

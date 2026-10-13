"""Encode the approved PNG as native icons. Requires Pillow; not used by builds."""
from pathlib import Path
from PIL import Image

icons = Path(__file__).resolve().parents[1] / "assets" / "icons"
with Image.open(icons / "voicetwitch.png") as source:
    image = source.convert("RGBA")
    image.save(icons / "voicetwitch.ico", sizes=[(s, s) for s in (16, 24, 32, 48, 64, 128, 256)])
    image.save(icons / "voicetwitch.icns", sizes=[(s, s) for s in (32, 64, 128, 256, 512, 1024)])
    # Packed RGBA for GLFW on X11, embedded by CMake without an image decoder.
    (icons / "voicetwitch-64.rgba").write_bytes(
        image.resize((64, 64), Image.Resampling.LANCZOS).tobytes())

"""Check that vendored Piper imports no longer reference the build environment."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location(
    "bundle", Path(__file__).resolve().parents[1] / "scripts/bundle-training-runtime.py")
bundle = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bundle)


class BundleTests(unittest.TestCase):
    def test_stages_piper_code_native_extensions_and_licenses_without_editable_hooks(self):
        with tempfile.TemporaryDirectory() as tmp:
            env = Path(tmp) / "build-env"
            packages = env / "Lib/site-packages"
            packages.mkdir(parents=True)
            (packages / "__editable__.piper_tts-1.8.0.pth").write_text("C:/build-env/src/piper")
            (packages / "__editable___piper_tts_1_8_0_finder.py").write_text("absolute build paths")
            (packages / "piper-tts.egg-link").write_text("C:/build-env/src/piper")
            (packages / "dependency.py").write_text("VALUE = 1")
            (packages / "dependency.dist-info/licenses").mkdir(parents=True)
            (packages / "dependency.dist-info/licenses/LICENSE").write_text("dependency license")
            source = env / "src/piper1-gpl-1.8.0"
            piper = source / "src/piper"
            piper.mkdir(parents=True)
            (piper / "__init__.py").write_text("VOICE = 'bundled'")
            (piper / "espeakbridge.pyd").write_bytes(b"native phonemizer")
            (piper / "__pycache__").mkdir()
            (piper / "__pycache__/stale.pyc").touch()
            (source / "COPYING").write_text("Piper GPL license")
            (source / "licenses").mkdir()
            (source / "licenses/espeak").write_text("espeak license")
            runtime = Path(tmp) / "runtime"
            bundle.stage_packages(env, runtime)
            staged = runtime / "Lib/site-packages"
            self.assertFalse(list(staged.glob("__editable__*")))
            self.assertFalse(list(staged.glob("*.egg-link")))
            self.assertEqual((staged / "piper/__init__.py").read_text(), "VOICE = 'bundled'")
            self.assertEqual((staged / "piper/espeakbridge.pyd").read_bytes(), b"native phonemizer")
            self.assertTrue((staged / "dependency.py").exists())
            self.assertTrue((staged / "dependency.dist-info/licenses/LICENSE").exists())
            self.assertFalse((staged / "piper/__pycache__").exists())
            self.assertTrue((runtime / "licenses/piper/COPYING").exists())
            self.assertTrue((runtime / "licenses/piper/third-party/espeak").exists())


if __name__ == "__main__":
    unittest.main()

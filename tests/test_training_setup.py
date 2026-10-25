"""Regression coverage for interpreter compatibility and failed setup recovery."""
import argparse
import contextlib
import importlib.util
import io
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

spec = importlib.util.spec_from_file_location(
    "train_voice", Path(__file__).resolve().parents[1] / "training" / "train_voice.py")
helper = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helper)


class TrainingSetupTests(unittest.TestCase):
    def check_python(self, version=(3, 12), bits=8, implementation="CPython", free_threaded=0):
        with patch.object(helper.sys, "version_info", version), \
             patch.object(helper.struct, "calcsize", return_value=bits), \
             patch.object(helper.platform, "python_implementation", return_value=implementation), \
             patch.object(helper.sysconfig, "get_config_var", return_value=free_threaded), \
             contextlib.redirect_stdout(io.StringIO()):
            return helper.cmd_check_python()

    def test_supported_standard_interpreters(self):
        for minor in (11, 12, 13):
            with self.subTest(minor=minor):
                self.assertEqual(self.check_python(version=(3, minor)), 0)

    def test_rejects_unsupported_versions_architectures_and_builds(self):
        for settings in ({"version": (3, 9)}, {"version": (3, 10)},
                         {"version": (3, 14)}, {"version": (4, 0)}, {"bits": 4},
                         {"implementation": "PyPy"}, {"free_threaded": 1}):
            with self.subTest(settings=settings):
                self.assertEqual(self.check_python(**settings), 1)

    def test_unsupported_python_does_not_create_environment(self):
        with patch.object(helper, "cmd_check_python", return_value=1), \
             patch.object(helper.venv, "EnvBuilder") as builder:
            self.assertEqual(helper.cmd_setup(argparse.Namespace(venv="unused")), 1)
            builder.assert_not_called()

    def test_missing_distribution_is_preserved_in_summary(self):
        def failed_pip(*args, on_line, **kwargs):
            on_line("ERROR: No matching distribution found for onnxruntime<2,>=1")
            return 1
        output = io.StringIO()
        with patch.object(helper, "run", side_effect=failed_pip), contextlib.redirect_stdout(output):
            self.assertEqual(helper.install_dependencies(Path("python"), Path("piper")), 1)
        self.assertIn("No matching distribution found for onnxruntime", output.getvalue())
        self.assertNotIn("make sure a C/C++ compiler", output.getvalue())

    def test_retry_recreates_only_incompatible_generated_environment(self):
        for compatible in (True, False):
            with self.subTest(compatible=compatible), tempfile.TemporaryDirectory() as tmp:
                env = Path(tmp) / "training-env"
                py = helper.venv_python(env)
                py.parent.mkdir(parents=True)
                py.touch()
                marker = env / helper.READY_MARKER
                marker.touch()
                recording = Path(tmp) / "cloned-voices" / "recording.wav"
                recording.parent.mkdir()
                recording.write_bytes(b"recording")
                with patch.object(helper, "cmd_check_python", return_value=0), \
                     patch.object(helper, "run", side_effect=[0 if compatible else 1, 1]), \
                     patch.object(helper.venv, "EnvBuilder") as builder, \
                     contextlib.redirect_stdout(io.StringIO()):
                    self.assertEqual(helper.cmd_setup(argparse.Namespace(venv=str(env))), 1)
                    builder.assert_called_once_with(with_pip=True, clear=not compatible)
                    # Windows TEMP may use an 8.3 alias (RUNNER~1); setup
                    # resolves it to the full path before creating the venv.
                    builder.return_value.create.assert_called_once_with(env.resolve())
                self.assertFalse(marker.exists())
                self.assertEqual(recording.read_bytes(), b"recording")


if __name__ == "__main__":
    unittest.main()

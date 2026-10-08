import sys
import types
import unittest
from pathlib import Path
from unittest.mock import patch


def stub_tkinter():
    tk = types.ModuleType("tkinter")
    tk.Tk = object
    tk.StringVar = object
    tk.BooleanVar = object
    tk.Text = object
    tk.END = "end"
    tk.TclError = type("TclError", (Exception,), {})
    ttk = types.ModuleType("tkinter.ttk")
    for name in ("Frame", "Label", "Entry", "Button", "Checkbutton", "Combobox", "Scrollbar"):
        setattr(ttk, name, object)
    tk.ttk = ttk
    filedialog = types.ModuleType("tkinter.filedialog")
    filedialog.askdirectory = lambda title="": ""
    sys.modules["tkinter"] = tk
    sys.modules["tkinter.ttk"] = ttk
    sys.modules["tkinter.filedialog"] = filedialog


stub_tkinter()
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import launcher



class LauncherTests(unittest.TestCase):
    def test_relinker_lookup_prefers_local_build(self):
        with patch.object(launcher, "ROOT", Path("/nonexistent")):
            with patch.object(launcher.shutil, "which", return_value=None):
                self.assertIsNone(launcher.find_relinker())

    def test_config_roundtrip(self):
        with patch.object(launcher, "CONFIG", Path("/tmp/anyps5-test-config.json")):
            launcher.save_config({"dump": "/tmp/dump"})
            self.assertEqual(launcher.load_config()["dump"], "/tmp/dump")
            Path("/tmp/anyps5-test-config.json").unlink()

    def test_missing_display_exits_two(self):
        with patch.object(launcher.tk, "Tk", side_effect=launcher.tk.TclError("no display")):
            with patch.object(launcher, "Launcher", side_effect=launcher.tk.TclError("no display")):
                self.assertEqual(launcher.main(), 2)


if __name__ == "__main__":
    unittest.main()

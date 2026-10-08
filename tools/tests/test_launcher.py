import sys
import tempfile
import types
import unittest
from pathlib import Path
from unittest.mock import patch


def stub_tkinter():
    tk = types.ModuleType("tkinter")
    tk.Tk = object
    tk.Toplevel = object
    tk.StringVar = object
    tk.BooleanVar = object
    tk.Text = object
    tk.END = "end"
    tk.BOTH = "both"
    tk.LEFT = "left"
    tk.RIGHT = "right"
    tk.X = "x"
    tk.Y = "y"
    tk.W = "w"
    tk.WORD = "word"
    tk.TclError = type("TclError", (Exception,), {})
    ttk = types.ModuleType("tkinter.ttk")
    for name in ("Frame", "Label", "Entry", "Button", "Checkbutton", "Combobox", "Scrollbar", "Treeview"):
        setattr(ttk, name, object)
    tk.ttk = ttk
    filedialog = types.ModuleType("tkinter.filedialog")
    filedialog.askdirectory = lambda title="", parent=None: ""
    messagebox = types.ModuleType("tkinter.messagebox")
    messagebox.askyesno = lambda *args, **kwargs: True
    sys.modules["tkinter"] = tk
    sys.modules["tkinter.ttk"] = ttk
    sys.modules["tkinter.filedialog"] = filedialog
    sys.modules["tkinter.messagebox"] = messagebox


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

    def test_library_roundtrip(self):
        with patch.object(launcher, "LIBRARY", Path("/tmp/anyps5-test-library.json")):
            game = {"name": "Demo", "dump": "/tmp/dump", "input": "/tmp/dump/input.elf", "out": "/tmp/out",
                    "windows": False, "intel": False, "filter": "0", "status": "", "created": "now", "last_run": ""}
            launcher.save_library([game])
            self.assertEqual(launcher.load_library(), [game])
            Path("/tmp/anyps5-test-library.json").unlink()

    def test_library_missing_file_is_empty(self):
        with patch.object(launcher, "LIBRARY", Path("/tmp/anyps5-test-absent-library.json")):
            self.assertEqual(launcher.load_library(), [])

    def test_input_candidates_elf_first(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "eboot.bin").write_bytes(launcher.check_dump.SELF_MAGIC + bytes(8))
            (root / "input.elf").write_bytes(launcher.check_dump.ELF_MAGIC + bytes(8))
            (root / "notes.txt").write_text("hello")
            kinds = [(path.name, kind) for path, kind in launcher.input_candidates(root)]
            self.assertEqual(kinds[0], ("input.elf", "elf"))
            self.assertIn(("eboot.bin", "self"), kinds)

    def test_executable_path_windows_and_linux(self):
        game = {"input": "/dump/eboot.bin", "out": "/out", "windows": False}
        self.assertEqual(launcher.executable_path(game), Path("/out/eboot.elf"))
        game["windows"] = True
        self.assertEqual(launcher.executable_path(game), Path("/out/eboot.exe"))

    def test_missing_display_exits_two(self):
        with patch.object(launcher.tk, "Tk", side_effect=launcher.tk.TclError("no display")):
            with patch.object(launcher, "Launcher", side_effect=launcher.tk.TclError("no display")):
                self.assertEqual(launcher.main(), 2)

    def test_missing_tkinter_exits_two(self):
        with patch.object(launcher, "tk", None):
            self.assertEqual(launcher.main(), 2)


if __name__ == "__main__":
    unittest.main()

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import check_dump


class DumpTests(unittest.TestCase):
    def make_dump(self, root, files):
        root = Path(root)
        for name, data in files.items():
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)

    def test_self_is_blocked_and_elf_is_ready(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.make_dump(root, {
                "eboot.bin": check_dump.SELF_MAGIC + bytes(32),
                "sce_module/libc.prx": check_dump.ELF_MAGIC + bytes(32),
            })
            result = check_dump.scan_dump(root)
            kinds = {entry["path"]: entry["kind"] for entry in result["entries"]}
            self.assertIn("self", kinds.values())
            self.assertIn("elf", kinds.values())
            self.assertEqual(result["module_dirs"], ["sce_module"])
            self.assertEqual(check_dump.primary_executable(root, result["entries"])["kind"], "self")
            self.assertIn("needs a clean ELF", check_dump.render(result))
            self.assertEqual(check_dump.main([str(root)]), 1)

    def test_clean_dump_reports_ready(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.make_dump(root, {
                "input.elf": check_dump.ELF_MAGIC + bytes(32),
                "sce_module/bundled.prx": check_dump.ELF_MAGIC + bytes(32),
            })
            result = check_dump.scan_dump(root)
            self.assertTrue(all(entry["ok"] for entry in result["entries"] if entry["kind"] == "elf"))
            self.assertIn("ready for relinker", check_dump.render(result))
            self.assertEqual(check_dump.main([str(root)]), 0)

    def test_kernel_self_pkg_and_short_inputs(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.make_dump(root, {
                "kernel.bin": check_dump.SELF_KERNEL_MAGIC + bytes(32),
                "game.pkg": check_dump.PKG_MAGIC + bytes(32),
                "short.bin": b"\x7fEL",
            })
            kinds = sorted(entry["kind"] for entry in check_dump.scan_dump(root)["entries"])
            self.assertEqual(kinds, ["pkg", "self-kernel", "too-small"])
            self.assertEqual(check_dump.main([str(root)]), 1)

    def test_missing_module_dir_is_not_ready(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.make_dump(root, {"input.elf": check_dump.ELF_MAGIC + bytes(32)})
            result = check_dump.scan_dump(root)
            self.assertEqual(result["module_dirs"], [])
            self.assertIn("no clean ELF executable found", check_dump.render(result))
            self.assertEqual(check_dump.main([str(root)]), 2)


if __name__ == "__main__":
    unittest.main()

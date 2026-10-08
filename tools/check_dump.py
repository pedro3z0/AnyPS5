#!/usr/bin/env python3
import argparse
import json
import sys
from pathlib import Path


ELF_MAGIC = b"\x7fELF"
SELF_MAGIC = b"\x4f\x15\x3d\x1d"
SELF_KERNEL_MAGIC = b"\x54\x14\xf5\xee"
PKG_MAGIC = b"\x7fCNT"
MODULE_DIRS = ("sce_module", "sce_modules", "prx")


def classify(path):
    try:
        data = path.read_bytes()[:16]
    except OSError as error:
        return {"path": str(path), "ok": False, "kind": "unreadable", "detail": str(error)}
    if data[:4] == ELF_MAGIC:
        return {"path": str(path), "ok": True, "kind": "elf", "detail": "clean ELF container"}
    if data[:4] == SELF_MAGIC:
        return {"path": str(path), "ok": False, "kind": "self", "detail": "SELF container, not an ELF"}
    if data[:4] == SELF_KERNEL_MAGIC:
        return {"path": str(path), "ok": False, "kind": "self-kernel", "detail": "SELF container, not an ELF"}
    if data[:4] == PKG_MAGIC:
        return {"path": str(path), "ok": False, "kind": "pkg", "detail": "PKG archive, not an ELF"}
    if len(data) < 4:
        return {"path": str(path), "ok": False, "kind": "too-small", "detail": "file too small for ELF header"}
    return {"path": str(path), "ok": False, "kind": "unknown", "detail": "invalid ELF magic number: " + data[:4].hex(" ")}


def primary_executable(root, entries):
    by_name = {Path(entry["path"]).name: entry for entry in entries}
    for name in ("eboot.bin", "input.elf"):
        if name in by_name:
            return by_name[name]
    elfs = [entry for entry in entries if Path(entry["path"]).parent == root and entry["kind"] == "elf"]
    if elfs:
        return sorted(elfs, key=lambda entry: entry["path"])[0]
    roots = [entry for entry in entries if Path(entry["path"]).parent == root]
    if roots:
        return sorted(roots, key=lambda entry: entry["path"])[0]
    if entries:
        return sorted(entries, key=lambda entry: entry["path"])[0]
    return None


def scan_dump(root):
    root = Path(root)
    if not root.is_dir():
        raise SystemExit(f"FAIL: {root}: not a directory")
    modules = [name for name in MODULE_DIRS if (root / name).is_dir()]
    candidates = sorted([p for p in root.iterdir() if p.is_file()], key=lambda p: p.name)
    nested = sorted([p for p in root.rglob("eboot.bin")] if not (root / "eboot.bin").is_file() else [])
    entries = []
    for path in candidates:
        if path.suffix.lower() in (".bin", ".elf", ".prx", ".sprx", ".pkg", "") or path.name.startswith("eboot"):
            entries.append(classify(path))
    for directory in modules:
        for path in sorted((root / directory).iterdir()):
            if path.is_file():
                entries.append(classify(path))
    return {"root": str(root), "module_dirs": modules, "entries": entries, "nested_eboots": [str(p) for p in nested]}


def render(result):
    lines = [f"dump: {result['root']}"]
    if result["module_dirs"]:
        lines.append("module dirs: " + ", ".join(result["module_dirs"]))
    else:
        lines.append("module dirs: none (relinker needs sce_module/, sce_modules/, or prx/ beside the input)")
    if result["nested_eboots"]:
        lines.append("nested eboot candidates:")
        for path in result["nested_eboots"]:
            lines.append(f"  {path}")
    for entry in result["entries"]:
        mark = "ready" if entry["ok"] and entry["kind"] == "elf" else "blocked"
        lines.append(f"  [{mark}] {entry['path']} ({entry['kind']}: {entry['detail']})")
    primary = primary_executable(Path(result["root"]), result["entries"])
    if primary:
        lines.append(f"primary: {primary['path']} ({primary['kind']}: {primary['detail']})")
    if primary and primary["kind"] == "elf" and result["module_dirs"]:
        lines.append("status: ready for relinker (clean ELF input with bundled modules)")
    elif primary and primary["kind"] in ("self", "self-kernel"):
        lines.append("status: needs a clean ELF executable (see docs/user/USAGE.md)")
    elif primary and primary["kind"] == "pkg":
        lines.append("status: needs a clean ELF executable (see docs/user/USAGE.md)")
    else:
        lines.append("status: no clean ELF executable found (see docs/user/USAGE.md)")
    return "\n".join(lines)


def main(argv=None):
    parser = argparse.ArgumentParser(description="inspect a dumped game directory and report relinker readiness")
    parser.add_argument("dump", type=Path, help="directory holding eboot.bin and sce_module/ (or sce_modules/, prx/)")
    parser.add_argument("--json", type=Path, help="write the full result to this file")
    args = parser.parse_args(argv)
    result = scan_dump(args.dump)
    if args.json:
        args.json.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(render(result))
    primary = primary_executable(args.dump, result["entries"])
    if primary and primary["kind"] == "elf" and result["module_dirs"]:
        return 0
    if primary and primary["kind"] in ("self", "self-kernel", "pkg"):
        return 1
    return 2


if __name__ == "__main__":
    raise SystemExit(main())

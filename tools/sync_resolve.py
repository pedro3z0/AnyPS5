#!/usr/bin/env python3
"""Guarded auto-resolution for fork-sync merges."""
import argparse
import re
import subprocess
import sys

GUARDED_PREFIXES = (".github/workflows/", "3rdparty/")
GUARDED_EXACT = (".gitmodules",)
GUARDED_SUFFIXES = (
    ".po", ".json", ".json5", ".xml",
    ".png", ".jpg", ".jpeg", ".gif", ".bmp", ".ico", ".icns",
    ".ttf", ".otf", ".woff", ".woff2", ".pdf",
    ".a", ".so", ".dylib", ".dll", ".exe", ".bin", ".dat",
    ".o", ".obj", ".class", ".pyc",
)


def run(*args):
    return subprocess.run(list(args), capture_output=True, text=True)


def guarded_reason(path):
    for prefix in GUARDED_PREFIXES:
        if path.startswith(prefix):
            return "guarded path prefix " + prefix
    if path in GUARDED_EXACT:
        return "guarded file"
    lowered = path.lower()
    for suffix in GUARDED_SUFFIXES:
        if lowered.endswith(suffix):
            return "guarded file type " + suffix
    try:
        with open(path, "rb") as handle:
            if b"\0" in handle.read(1 << 20):
                return "binary content"
    except OSError:
        return "unreadable in worktree"
    return None


def stage_blob(rev, path):
    proc = run("git", "show", rev + ":" + path)
    return proc.stdout if proc.returncode == 0 else None


def split_hunks(text):
    lines = text.splitlines(keepends=True)
    hunks = []
    index = 0
    while index < len(lines):
        if not lines[index].startswith("<<<<<<< "):
            index += 1
            continue
        ours, base, theirs = [], [], []
        index += 1
        while index < len(lines) and not lines[index].startswith("|||||||") and not lines[index].startswith("======="):
            ours.append(lines[index])
            index += 1
        if index >= len(lines):
            return None
        if lines[index].startswith("======="):
            index += 1
            theirs = []
            while index < len(lines) and not lines[index].startswith(">>>>>>>"):
                theirs.append(lines[index])
                index += 1
            if index >= len(lines):
                return None
            index += 1
            hunks.append((ours, None, theirs))
            continue
        index += 1
        while index < len(lines) and not lines[index].startswith("======="):
            base.append(lines[index])
            index += 1
        if index >= len(lines):
            return None
        index += 1
        while index < len(lines) and not lines[index].startswith(">>>>>>>"):
            theirs.append(lines[index])
            index += 1
        if index >= len(lines):
            return None
        index += 1
        hunks.append((ours, base, theirs))
    return hunks


def norm(lines):
    return [line.rstrip("\r\n").rstrip() for line in lines]


def resolve_one(path):
    ours_blob = stage_blob(":2", path)
    base_blob = stage_blob(":1", path)
    theirs_blob = stage_blob(":3", path)
    try:
        with open(path, encoding="utf-8", errors="surrogateescape") as handle:
            worktree = handle.read()
    except OSError:
        return False, "unreadable in worktree", 0, 0
    if "<<<<<<< " not in worktree:
        return False, "unexpected: no conflict markers, left untouched", 0, 0
    hunks = split_hunks(worktree)
    if hunks is None:
        return False, "unparseable conflict markers", 0, 0
    if not hunks:
        if ours_blob is not None and theirs_blob is not None and base_blob is not None:
            if ours_blob == theirs_blob:
                chosen = ours_blob
            elif base_blob == ours_blob:
                chosen = theirs_blob
            elif base_blob == theirs_blob:
                chosen = ours_blob
            else:
                return False, "same-side change, needs a human", 0, 0
        else:
            return False, "same-side change, needs a human", 0, 0
        if "<<<<<<<" in chosen:
            return False, "resolved text still has markers", 0, 0
        with open(path, "w", encoding="utf-8", errors="surrogateescape", newline="") as handle:
            handle.write(chosen)
        staged = run("git", "add", "--", path)
        if staged.returncode != 0:
            return False, "could not stage resolution", 0, 0
        return True, "same-side change", 0, 0
    for ours, base, theirs in hunks:
        if ours == theirs and ours:
            continue
        if norm(ours) == norm(theirs) and norm(ours):
            continue
        if base is None:
            return False, "genuine conflict needs a human (no diff3 base)", 0, len(hunks)
        if norm(ours) == norm(base) or norm(theirs) == norm(base):
            continue
        return False, "genuine diff3 conflict needs a human", 0, len(hunks)
    merged = []
    forms = set()
    for ours, base, theirs in hunks:
        if not ours and not theirs:
            continue
        if norm(ours) == norm(theirs):
            forms.add("identical-chunks")
            merged_side = ours
        elif base is not None and norm(ours) == norm(base):
            forms.add("unchanged-in-ours")
            merged_side = theirs
        elif base is not None and norm(theirs) == norm(base):
            forms.add("unchanged-in-theirs")
            merged_side = ours
        else:
            forms.add("identical-chunks")
            merged_side = ours
        merged.append((ours, base, theirs, merged_side))
    out = []
    pos = 0
    lines = worktree.splitlines(keepends=True)
    for ours, base, theirs, merged_side in merged:
        start = None
        for i in range(pos, len(lines)):
            if lines[i].startswith("<<<<<<< "):
                start = i
                break
        end = start
        while end < len(lines) and not lines[end].startswith(">>>>>>>"):
            end += 1
        out.extend(lines[pos:start])
        out.extend(merged_side)
        pos = end + 1
    out.extend(lines[pos:])
    text = "".join(out)
    if "<<<<<<<" in text:
        return False, "output still has markers", 0, len(hunks)
    with open(path, "w", encoding="utf-8", errors="surrogateescape", newline="") as handle:
        handle.write(text)
    staged = run("git", "add", "--", path)
    if staged.returncode != 0:
        return False, "could not stage resolution", len(hunks), len(hunks)
    return True, ", ".join(sorted(forms)), len(hunks), len(hunks)


def main():
    parser = argparse.ArgumentParser(description="Guarded auto-resolution for an in-progress merge.")
    parser.add_argument("files", nargs="*", help="conflicted paths (default: from git status)")
    parser.add_argument("--blocklist-out", default=None, help="write blocked paths here")
    args = parser.parse_args()
    if args.files:
        conflicted = list(args.files)
    else:
        proc = run("git", "diff", "--name-only", "--diff-filter=U")
        conflicted = [line for line in proc.stdout.splitlines() if line.strip()]
    resolved, blocked, auto, total = [], [], 0, 0
    for path in conflicted:
        reason = guarded_reason(path)
        if reason is not None:
            blocked.append((path, reason))
            continue
        ok, note, file_auto, file_total = resolve_one(path)
        total += max(file_total, 1)
        if ok:
            resolved.append((path, note))
            auto += file_auto
        else:
            blocked.append((path, note))
    for path, note in resolved:
        print("resolved: %s (%s)" % (path, note))
    for path, note in blocked:
        print("blocked: %s (%s)" % (path, note))
    if args.blocklist_out is not None and blocked:
        with open(args.blocklist_out, "w", encoding="utf-8") as handle:
            for path, _ in blocked:
                handle.write(path + "\n")
    print("resolved %d of %d files; %d of %d hunks decided exactly" % (len(resolved), len(conflicted), auto, total))
    return 0 if (not blocked and conflicted) else 1


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
import json
import os
import queue
import shutil
import subprocess
import sys
import threading

from datetime import datetime, timezone
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import check_dump

try:
    import tkinter as tk
    from tkinter import filedialog, messagebox, ttk
except ImportError as tk_error:
    tk = None
    TK_ERROR = tk_error
else:
    TK_ERROR = None


ROOT = Path(__file__).resolve().parent.parent
CONFIG = Path.home() / ".config" / "anyps5" / "launcher.json"
LIBRARY = Path.home() / ".config" / "anyps5" / "library.json"
if sys.platform == "win32":
    LOGDIR = Path(os.environ.get("LOCALAPPDATA", str(Path.home()))) / "anyps5" / "logs"
else:
    LOGDIR = Path.home() / ".local" / "share" / "anyps5" / "logs"
DEFAULT_LIBS = ROOT / "build" / "core" / "libs" / "libs"
REFRESH = "\x00refresh"


def load_config():
    try:
        return json.loads(CONFIG.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return {}


def save_config(data):
    CONFIG.parent.mkdir(parents=True, exist_ok=True)
    CONFIG.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def load_library():
    try:
        data = json.loads(LIBRARY.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return []
    return data.get("games", []) if isinstance(data, dict) else []


def save_library(games):
    LIBRARY.parent.mkdir(parents=True, exist_ok=True)
    LIBRARY.write_text(json.dumps({"games": games}, indent=2) + "\n", encoding="utf-8")


def find_relinker():
    for path in (ROOT / "build-relinker" / "core" / "relinker" / "relinker",
                 ROOT / "build" / "core" / "relinker" / "relinker",
                 ROOT / "build-relinker" / "core" / "relinker" / "relinker.exe",
                 ROOT / "build" / "core" / "relinker" / "relinker.exe"):
        if path.is_file():
            return path
    found = shutil.which("relinker")
    return Path(found) if found else None


def timestamp():
    return datetime.now(timezone.utc).astimezone().isoformat(timespec="seconds")


def log_stamp():
    return datetime.now().strftime("%Y%m%d-%H%M%S")


def input_candidates(dump):
    root = Path(dump)
    if not root.is_dir():
        return []
    entries = []
    for path in sorted(root.iterdir()):
        if not path.is_file():
            continue
        kind = check_dump.classify(path)["kind"]
        if kind == "unreadable":
            continue
        entries.append((path, kind))
    entries.sort(key=lambda entry: (entry[1] != "elf", entry[0].name.lower()))
    return entries


ACTIONS = ("Cross", "Circle", "Triangle", "Square", "L1", "R1", "L2", "R2", "L3", "R3",
           "Options", "Up", "Right", "Down", "Left",
           "LeftStickLeft", "LeftStickRight", "LeftStickUp", "LeftStickDown",
           "RightStickLeft", "RightStickRight", "RightStickUp", "RightStickDown",
           "TouchLeft", "TouchRight", "ToggleMouse", "ToggleFullscreen")


def read_title_meta(dump):
    root = Path(dump)
    meta = {"title": root.name, "title_id": "", "version": "", "icon": ""}
    param = root / "sce_sys" / "param.json"
    try:
        data = json.loads(param.read_text(encoding="utf-8-sig"))
    except (OSError, ValueError):
        return with_icon(root, meta)
    meta["title_id"] = str(data.get("titleId", ""))
    meta["version"] = str(data.get("contentVersion", ""))
    localized = data.get("localizedParameters", {})
    if isinstance(localized, dict):
        entry = localized.get(localized.get("defaultLanguage", ""), {})
        name = str(entry.get("titleName", "") if isinstance(entry, dict) else "")
        if not name:
            entry = localized.get("en-US", {})
            name = str(entry.get("titleName", "") if isinstance(entry, dict) else "")
        if name:
            meta["title"] = name
    return with_icon(root, meta)


def with_icon(root, meta):
    icon = root / "sce_sys" / "icon0.png"
    if icon.is_file():
        meta["icon"] = str(icon)
    return meta


def parse_input_text(text):
    bindings = {action: [] for action in ACTIONS}
    for lineno, raw in enumerate(text.splitlines(), 1):
        line = raw.strip()
        if not line or line[0] in "#;":
            continue
        action, separator, source = line.partition("=")
        found = next((known for known in ACTIONS if known.lower() == action.strip().lower()), None)
        if not separator or found is None or not source.strip():
            return None, lineno
        bindings[found].append(source.strip())
    return bindings, 0


def render_input_text(bindings):
    lines = [f"{action} = {source}" for action in ACTIONS for source in bindings.get(action, []) if source]
    return "\n".join(lines) + ("\n" if lines else "")


def game_env(game):
    env = dict(os.environ)
    for key, value in (game.get("env") or {}).items():
        if value:
            env[key] = value
    return env


def default_input_path(game):
    return Path(game["out"]) / "anyps5-input.ini"


def input_path(game):
    configured = (game.get("env") or {}).get("ANYPS5_INPUT_CONFIG", "")
    return Path(configured) if configured else default_input_path(game)


def executable_path(game):
    suffix = ".exe" if game.get("windows") else ".elf"
    return Path(game["out"]) / (Path(game["input"]).stem + suffix)

if tk is not None:

    class AddGameDialog(tk.Toplevel):
        def __init__(self, parent):
            super().__init__(parent)
            self.title("Add game")
            self.transient(parent)
            self.resizable(False, False)
            self.result = None
            self.name = tk.StringVar()
            self.dump = tk.StringVar()
            self.input = tk.StringVar()
            self.out = tk.StringVar()
            self.windows = tk.BooleanVar(value=False)
            self.intel = tk.BooleanVar(value=False)
            self.filter = tk.StringVar(value="0")
            self.candidates = []
            self.meta = {}
            frame = ttk.Frame(self, padding=10)
            frame.pack(fill=tk.BOTH, expand=True)
            self.add_row(frame, "Name", ttk.Entry(frame, textvariable=self.name, width=44))
            row = ttk.Frame(frame)
            row.pack(fill=tk.X, pady=2)
            ttk.Label(row, text="Dump", width=8).pack(side=tk.LEFT)
            ttk.Entry(row, textvariable=self.dump).pack(side=tk.LEFT, fill=tk.X, expand=True, padx=4)
            ttk.Button(row, text="Browse", command=self.pick_dump).pack(side=tk.LEFT)
            row = ttk.Frame(frame)
            row.pack(fill=tk.X, pady=2)
            ttk.Label(row, text="Input", width=8).pack(side=tk.LEFT)
            self.input_box = ttk.Combobox(row, textvariable=self.input, state="readonly", width=44)
            self.input_box.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=4)
            row = ttk.Frame(frame)
            row.pack(fill=tk.X, pady=2)
            ttk.Label(row, text="Out", width=8).pack(side=tk.LEFT)
            ttk.Entry(row, textvariable=self.out).pack(side=tk.LEFT, fill=tk.X, expand=True, padx=4)
            ttk.Button(row, text="Browse", command=self.pick_out).pack(side=tk.LEFT)
            opts = ttk.Frame(frame)
            opts.pack(fill=tk.X, pady=4)
            ttk.Checkbutton(opts, text="Windows", variable=self.windows).pack(side=tk.LEFT, padx=4)
            ttk.Checkbutton(opts, text="Intel", variable=self.intel).pack(side=tk.LEFT, padx=4)
            ttk.Label(opts, text="unused-filter").pack(side=tk.LEFT, padx=(12, 2))
            ttk.Combobox(opts, textvariable=self.filter, values=("0", "1", "2"), width=3, state="readonly").pack(side=tk.LEFT)
            self.error = ttk.Label(frame, text="", foreground="#b60205")
            self.error.pack(fill=tk.X)
            buttons = ttk.Frame(frame)
            buttons.pack(fill=tk.X, pady=4)
            ttk.Button(buttons, text="Add", command=self.submit).pack(side=tk.RIGHT, padx=2)
            ttk.Button(buttons, text="Cancel", command=self.destroy).pack(side=tk.RIGHT, padx=2)

        def add_row(self, parent, label, widget):
            row = ttk.Frame(parent)
            row.pack(fill=tk.X, pady=2)
            ttk.Label(row, text=label, width=8).pack(side=tk.LEFT)
            widget.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=4)

        def pick_dump(self):
            picked = filedialog.askdirectory(title="Dumped game directory", parent=self)
            if not picked:
                return
            self.dump.set(picked)
            self.candidates = input_candidates(picked)
            labels = [f"{path.name} ({kind})" for path, kind in self.candidates]
            self.input_box.configure(values=labels)
            if labels:
                self.input_box.current(0)
                self.input.set(labels[0])
            meta = read_title_meta(picked)
            if not self.name.get():
                self.name.set(meta["title"])
            if not self.out.get():
                self.out.set(str(Path.home() / "AnyPS5" / "out"))
            self.meta = meta

        def pick_out(self):
            picked = filedialog.askdirectory(title="Output directory", parent=self)
            if picked:
                self.out.set(picked)

        def selected_input(self):
            label = self.input.get()
            for path, kind in self.candidates:
                if f"{path.name} ({kind})" == label:
                    return path
            return None

        def submit(self):
            name = self.name.get().strip()
            dump = Path(self.dump.get())
            out = Path(self.out.get())
            path = self.selected_input()
            if not name:
                self.error.configure(text="a name is required")
                return
            if not dump.is_dir():
                self.error.configure(text="dump dir is missing")
                return
            if path is None:
                self.error.configure(text="pick an input executable")
                return
            if check_dump.classify(path)["kind"] in ("self", "self-kernel", "pkg"):
                self.error.configure(text="input is a SELF or PKG container; pick the decrypted ELF")
                return
            self.result = {"name": name, "dump": str(dump), "input": str(path), "out": str(out),
                           "windows": bool(self.windows.get()), "intel": bool(self.intel.get()),
                           "filter": self.filter.get(), "status": "", "created": timestamp(), "last_run": "",
                           "title": self.meta.get("title", name), "title_id": self.meta.get("title_id", ""),
                           "version": self.meta.get("version", ""), "icon": self.meta.get("icon", ""),
                           "env": {}}
            self.destroy()

    class Launcher(tk.Tk):
        def __init__(self):
            super().__init__()
            self.title("AnyPS5")
            self.geometry("900x640")
            self.games = load_library()
            self.messages = queue.Queue()
            self.logfile = None
            self.selected = None
            self.build_ui()
            self.refresh()
            self.after(100, self.drain)

        def build_ui(self):
            main = ttk.Frame(self, padding=10)
            main.pack(fill=tk.BOTH, expand=True)
            toolbar = ttk.Frame(main)
            toolbar.pack(fill=tk.X, pady=(0, 6))
            ttk.Button(toolbar, text="Add game", command=self.add_game).pack(side=tk.LEFT, padx=2)
            ttk.Button(toolbar, text="Convert", command=lambda: self.run_step("convert")).pack(side=tk.LEFT, padx=2)
            ttk.Button(toolbar, text="Play", command=lambda: self.run_step("run")).pack(side=tk.LEFT, padx=2)
            ttk.Button(toolbar, text="Settings", command=self.open_settings).pack(side=tk.LEFT, padx=2)
            ttk.Button(toolbar, text="Input", command=self.open_input).pack(side=tk.LEFT, padx=2)
            ttk.Button(toolbar, text="Open output", command=self.open_output).pack(side=tk.LEFT, padx=2)
            ttk.Button(toolbar, text="Remove", command=self.remove_game).pack(side=tk.LEFT, padx=2)
            ttk.Button(toolbar, text="Logs", command=self.open_logs).pack(side=tk.RIGHT, padx=2)
            body = ttk.PanedWindow(main, orient=tk.HORIZONTAL)
            body.pack(fill=tk.BOTH, expand=True)
            self.grid = ttk.Frame(body, width=620)
            self.grid.pack_propagate(False)
            body.add(self.grid, weight=3)
            detail = ttk.Frame(body, width=260)
            detail.pack_propagate(False)
            body.add(detail, weight=1)
            ttk.Label(detail, text="Details", font=("TkDefaultFont", 10, "bold")).pack(anchor=tk.W, pady=(0, 4))
            self.detail = ttk.Label(detail, text="Select a game", justify=tk.LEFT, wraplength=240)
            self.detail.pack(anchor=tk.W, fill=tk.X)
            self.statusbar = ttk.Label(main, text="", relief=tk.SUNKEN, anchor=tk.W)
            self.statusbar.pack(fill=tk.X, pady=(6, 0))
            self.images = {}

        def refresh(self):
            for widget in self.grid.winfo_children():
                widget.destroy()
            self.images = {}
            columns = max(1, self.grid.winfo_width() // 170)
            for index, game in enumerate(self.games):
                row, column = divmod(index, columns)
                cell = ttk.Frame(self.grid, padding=6, relief=tk.RAISED, borderwidth=1)
                cell.grid(row=row, column=column, padx=6, pady=6, sticky=tk.N)
                image = self.cover(game)
                label = ttk.Label(cell, image=image, text=game["name"], compound=tk.TOP, wraplength=140)
                label.pack()
                label.bind("<Button-1>", lambda event, selected=index: self.select(selected))
                label.bind("<Double-1>", lambda event, selected=index: self.select(selected) or self.run_step("run"))
            game = self.current()
            if game is None:
                self.detail.configure(text="Select a game")
                self.statusbar.configure(text=f"{len(self.games)} games")
                return
            info = [game.get("title", game["name"]), game.get("title_id", ""), game.get("version", ""),
                    game.get("status") or "not converted", f"last run: {game.get('last_run') or 'never'}"]
            self.detail.configure(text="\n".join(str(part) for part in info if part))
            self.statusbar.configure(text=f"{len(self.games)} games | {game['dump']} -> {game['out']}")

        def cover(self, game):
            icon = game.get("icon", "")
            if icon and Path(icon).is_file():
                try:
                    image = tk.PhotoImage(file=icon)
                    if image.width() > 128 or image.height() > 128:
                        image = image.subsample(max(1, image.width() // 128), max(1, image.height() // 128))
                    self.images[game["name"]] = image
                    return image
                except tk.TclError:
                    pass
            placeholder = tk.PhotoImage(width=128, height=128)
            self.images[game["name"]] = placeholder
            return placeholder

        def select(self, index):
            self.selected = index
            self.refresh()
        def on_select(self, event):
            self.selected = None
            self.refresh()

        def add_game(self):
            dialog = AddGameDialog(self)
            self.wait_window(dialog)
            if dialog.result is None:
                return
            self.games.append(dialog.result)
            save_library(self.games)
            self.refresh()
            self.emit(f"added: {dialog.result['name']}")

        def current(self):
            if self.selected is None or self.selected >= len(self.games):
                return None
            return self.games[self.selected]

        def run_step(self, step):
            game = self.current()
            if game is None:
                self.emit("select a game first")
                return
            threading.Thread(target=self.execute, args=(step, game), daemon=True).start()

        def execute(self, step, game):
            self.start_log(step, game)
            try:
                if step == "convert":
                    self.convert(game)
                    self.audit(game)
                elif step == "audit":
                    self.audit(game)
                elif step == "run":
                    self.launch(game)
                self.emit(f"{step}: done")
                save_library(self.games)
                self.messages.put(REFRESH)
            except SystemExit as error:
                self.emit(f"{step}: failed ({error})")
            except Exception as error:
                self.emit(f"{step}: failed: {error}")

        def convert(self, game):
            if find_relinker() is None:
                raise SystemExit("relinker binary not found; build build-relinker or build first")
            out = Path(game["out"])
            out.mkdir(parents=True, exist_ok=True)
            args = [str(ROOT / "tools" / "convert.sh"), "--dump", game["dump"], "--out", str(out), "--input", game["input"]]
            if game.get("windows"):
                args.append("--windows")
            if game.get("intel"):
                args.append("--to-intel")
            args += ["--unused-filter", game.get("filter", "0")]
            self.stream(args)
            exe = executable_path(game)
            if not exe.is_file():
                raise SystemExit(f"conversion produced no {exe}")
            self.emit(f"executable: {exe}")

        def audit(self, game):
            registries = sorted(Path(game["out"]).glob("*.registry.json"))
            if not registries:
                raise SystemExit("no registry json in out dir; convert first")
            result = Path(game["out"]) / "audit.json"
            modules = Path(game["dump"]) / "sce_module"
            args = [sys.executable, str(ROOT / "tools" / "import_audit.py"), str(registries[-1]),
                    "--libs", str(DEFAULT_LIBS), "--json", str(result)]
            if modules.is_dir():
                args += ["--modules", str(modules)]
            self.stream(args, ok=(0, 1))
            counts = json.loads(result.read_text(encoding="utf-8"))["unique_by_class"]
            game["status"] = f"{counts['implemented']} impl, {counts['stub']} stub, {counts['absent']} absent"
            self.emit(f"audit: {game['status']}")

        def launch(self, game):
            exe = executable_path(game)
            if not exe.is_file():
                raise SystemExit(f"{exe} missing; convert first")
            env = game.setdefault("env", {})
            if not env.get("ANYPS5_SHADER_CACHE_DIR", ""):
                env["ANYPS5_SHADER_CACHE_DIR"] = str(exe.parent / "shader_cache")
            args = [str(ROOT / "tools" / "run.sh"), "--game", str(exe)]
            dump = Path(game["dump"])
            if (dump / "sce_sys").is_dir():
                args += ["--app0", str(dump)]
            self.stream(args, ok=None, env=game_env(game))
            game["last_run"] = timestamp()
            self.emit(f"last run: {game['last_run']}")

        def open_settings(self):
            game = self.current()
            if game is None:
                self.emit("select a game first")
                return
            dialog = SettingsDialog(self, game)
            self.wait_window(dialog)
            if dialog.saved:
                save_library(self.games)
                self.refresh()
                self.emit(f"settings saved: {game['name']}")

        def open_input(self):
            game = self.current()
            if game is None:
                self.emit("select a game first")
                return
            dialog = InputDialog(self, game)
            self.wait_window(dialog)

            self.emit(f"last run: {game['last_run']}")

        def remove_game(self):
            game = self.current()
            if game is None:
                self.emit("select a game first")
                return
            if not messagebox.askyesno("Remove game", f"Remove {game['name']} from the library? Files are kept."):
                return
            self.games.pop(self.selected)
            self.selected = None
            save_library(self.games)
            self.refresh()
            self.emit(f"removed: {game['name']}")

        def open_output(self):
            game = self.current()
            if game is None:
                self.emit("select a game first")
                return
            out = Path(game["out"])
            out.mkdir(parents=True, exist_ok=True)
            if sys.platform == "win32":
                os.startfile(out)
            elif sys.platform == "darwin":
                subprocess.Popen(["open", str(out)])
            else:
                subprocess.Popen(["xdg-open", str(out)])

        def open_logs(self):
            LOGDIR.mkdir(parents=True, exist_ok=True)
            if sys.platform == "win32":
                os.startfile(LOGDIR)
            elif sys.platform == "darwin":
                subprocess.Popen(["open", str(LOGDIR)])
            else:
                subprocess.Popen(["xdg-open", str(LOGDIR)])

        def start_log(self, step, game):
            LOGDIR.mkdir(parents=True, exist_ok=True)
            name = f"{log_stamp()}-{step}-{game['name'].replace(' ', '_')}.log"
            if self.logfile:
                self.logfile.close()
            self.logfile = open(LOGDIR / name, "w", encoding="utf-8")
            self.emit(f"log: {LOGDIR / name}")

        def emit(self, text):
            self.messages.put(text)

        def drain(self):
            try:
                while True:
                    line = self.messages.get_nowait()
                    if line == REFRESH:
                        self.refresh()
                        continue
                    self.log.insert(tk.END, line + "\n")
                    self.log.see(tk.END)
                    if self.logfile:
                        self.logfile.write(f"{timestamp()} {line}\n")
                        self.logfile.flush()
            except queue.Empty:
                pass
            self.after(100, self.drain)

        def stream(self, args, ok=(0,), env=None):
            self.emit("$ " + " ".join(args))
            proc = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace",
                                    env=env)
            for line in proc.stdout:
                self.emit(line.rstrip())
            code = proc.wait()
            if ok is not None and code not in ok:
                raise SystemExit(f"exit {code}: {' '.join(args)}")


def main():
    if tk is None:
        print(f"FAIL: tkinter is unavailable ({TK_ERROR}); install python3-tk (Debian, Ubuntu), python3-tkinter (Fedora), or tcl-tk (macOS brew, Windows python.org installer)", file=sys.stderr)
        return 2
    try:
        app = Launcher()
    except tk.TclError as error:
        print(f"FAIL: no display: {error}", file=sys.stderr)
        return 2
    app.mainloop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

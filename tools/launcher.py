#!/usr/bin/env python3
import json
import os
import queue
import shutil
import subprocess
import sys
import threading
try:
    import tkinter as tk
    from tkinter import filedialog, ttk
except ImportError as tk_error:
    tk = None
    TK_ERROR = tk_error
else:
    TK_ERROR = None

from datetime import datetime, timezone
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
CONFIG = Path.home() / ".config" / "anyps5" / "launcher.json"
if sys.platform == "win32":
    LOGDIR = Path(os.environ.get("LOCALAPPDATA", str(Path.home()))) / "anyps5" / "logs"
else:
    LOGDIR = Path.home() / ".local" / "share" / "anyps5" / "logs"


def load_config():
    try:
        return json.loads(CONFIG.read_text(encoding="utf-8"))
    except OSError:
        return {}


def save_config(data):
    CONFIG.parent.mkdir(parents=True, exist_ok=True)
    CONFIG.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


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

if tk is not None:
    class Launcher(tk.Tk):
        def __init__(self):
            super().__init__()
            self.title("AnyPS5")
            self.geometry("760x560")
            config = load_config()
            self.dump = tk.StringVar(value=config.get("dump", ""))
            self.out = tk.StringVar(value=config.get("out", str(Path.home() / "AnyPS5" / "out")))
            self.windows = tk.BooleanVar(value=config.get("windows", False))
            self.intel = tk.BooleanVar(value=config.get("intel", False))
            self.filter = tk.StringVar(value=config.get("filter", "0"))
            self.messages = queue.Queue()
            self.logfile = None
            self.build_ui()
            self.refresh_relinker()
            self.after(100, self.drain)

        def build_ui(self):
            main = ttk.Frame(self, padding=10)
            main.pack(fill=tk.BOTH, expand=True)
            row = ttk.Frame(main)
            row.pack(fill=tk.X, pady=2)
            ttk.Label(row, text="Dump dir", width=10).pack(side=tk.LEFT)
            ttk.Entry(row, textvariable=self.dump).pack(side=tk.LEFT, fill=tk.X, expand=True, padx=4)
            ttk.Button(row, text="Browse", command=self.pick_dump).pack(side=tk.LEFT)
            row = ttk.Frame(main)
            row.pack(fill=tk.X, pady=2)
            ttk.Label(row, text="Out dir", width=10).pack(side=tk.LEFT)
            ttk.Entry(row, textvariable=self.out).pack(side=tk.LEFT, fill=tk.X, expand=True, padx=4)
            ttk.Button(row, text="Browse", command=self.pick_out).pack(side=tk.LEFT)
            opts = ttk.Frame(main)
            opts.pack(fill=tk.X, pady=4)
            ttk.Checkbutton(opts, text="Windows PE", variable=self.windows).pack(side=tk.LEFT, padx=4)
            ttk.Checkbutton(opts, text="Intel", variable=self.intel).pack(side=tk.LEFT, padx=4)
            ttk.Label(opts, text="unused-filter").pack(side=tk.LEFT, padx=(12, 2))
            ttk.Combobox(opts, textvariable=self.filter, values=("0", "1", "2"), width=3, state="readonly").pack(side=tk.LEFT)
            self.status = ttk.Label(main, text="")
            self.status.pack(fill=tk.X, pady=2)
            buttons = ttk.Frame(main)
            buttons.pack(fill=tk.X, pady=4)
            ttk.Button(buttons, text="Check dump", command=lambda: self.run_step("check")).pack(side=tk.LEFT, padx=2)
            ttk.Button(buttons, text="Convert", command=lambda: self.run_step("convert")).pack(side=tk.LEFT, padx=2)
            ttk.Button(buttons, text="Audit", command=lambda: self.run_step("audit")).pack(side=tk.LEFT, padx=2)
            ttk.Button(buttons, text="Run game", command=lambda: self.run_step("run")).pack(side=tk.LEFT, padx=2)
            ttk.Button(buttons, text="Open log dir", command=self.open_logs).pack(side=tk.RIGHT, padx=2)
            self.log = tk.Text(main, wrap=tk.WORD, height=20)
            self.log.pack(fill=tk.BOTH, expand=True, pady=4)
            scroll = ttk.Scrollbar(self.log, command=self.log.yview)
            scroll.pack(side=tk.RIGHT, fill=tk.Y)
            self.log.configure(yscrollcommand=scroll.set)

        def refresh_relinker(self):
            found = find_relinker()
            self.status.config(text=f"relinker: {found}" if found else "relinker: not built (see docs/user/FORK.md)")
            return found

        def pick_dump(self):
            picked = filedialog.askdirectory(title="Dumped game directory")
            if picked:
                self.dump.set(picked)

        def pick_out(self):
            picked = filedialog.askdirectory(title="Output directory")
            if picked:
                self.out.set(picked)

        def open_logs(self):
            LOGDIR.mkdir(parents=True, exist_ok=True)
            if sys.platform == "win32":
                os.startfile(LOGDIR)
            elif sys.platform == "darwin":
                subprocess.Popen(["open", str(LOGDIR)])
            else:
                subprocess.Popen(["xdg-open", str(LOGDIR)])
        def emit(self, text):
            self.messages.put(text)

        def drain(self):
            try:
                while True:
                    line = self.messages.get_nowait()
                    self.log.insert(tk.END, line + "\n")
                    self.log.see(tk.END)
                    if self.logfile:
                        self.logfile.write(f"{timestamp()} {line}\n")
                        self.logfile.flush()
            except queue.Empty:
                pass
            self.after(100, self.drain)

        def start_log(self, step):
            LOGDIR.mkdir(parents=True, exist_ok=True)
            name = datetime.now().strftime("%Y%m%d-%H%M%S") + f"-{step}.log"
            if self.logfile:
                self.logfile.close()
            self.logfile = open(LOGDIR / name, "w", encoding="utf-8")
            self.emit(f"log: {LOGDIR / name}")

        def run_step(self, step):
            save_config({"dump": self.dump.get(), "out": self.out.get(), "windows": self.windows.get(),
                         "intel": self.intel.get(), "filter": self.filter.get()})
            threading.Thread(target=self.execute, args=(step,), daemon=True).start()

        def execute(self, step):
            self.start_log(step)
            try:
                if step == "check":
                    self.check()
                elif step == "convert":
                    self.convert()
                elif step == "audit":
                    self.audit()
                elif step == "run":
                    self.launch()
                self.emit(f"{step}: done")
            except SystemExit as error:
                self.emit(f"{step}: failed ({error})")
            except Exception as error:
                self.emit(f"{step}: failed: {error}")

        def check(self):
            dump = self.require_dir(self.dump.get(), "dump")
            self.stream([sys.executable, str(ROOT / "tools" / "check_dump.py"), str(dump)])

        def convert(self):
            dump = self.require_dir(self.dump.get(), "dump")
            out = Path(self.out.get())
            out.mkdir(parents=True, exist_ok=True)
            args = [str(ROOT / "tools" / "convert.sh"), "--dump", str(dump), "--out", str(out)]
            if self.windows.get():
                args.append("--windows")
            if self.intel.get():
                args.append("--to-intel")
            args += ["--unused-filter", self.filter.get()]
            self.stream(args)

        def audit(self):
            registries = sorted(Path(self.out.get()).glob("*.registry.json"))
            if not registries:
                raise SystemExit("no .registry.json in out dir; convert first")
            dump = Path(self.dump.get())
            modules = dump / "sce_module"
            args = [str(ROOT / "tools" / "audit.sh"), "--registry", str(registries[-1])]
            if modules.is_dir():
                args += ["--modules", str(modules)]
            self.stream(args)

        def launch(self):
            games = sorted(Path(self.out.get()).glob("*.elf")) + sorted(Path(self.out.get()).glob("*.exe"))
            if not games:
                raise SystemExit("no converted game in out dir; convert first")
            args = [str(ROOT / "tools" / "run.sh"), "--game", str(games[-1])]
            dump = self.dump.get()
            if dump and (Path(dump) / "sce_sys").is_dir():
                args += ["--app0", dump]
            self.stream(args)

        def require_dir(self, value, name):
            path = Path(value)
            if not value or not path.is_dir():
                raise SystemExit(f"{name} dir is missing: {value}")
            return path

        def stream(self, args):
            self.emit("$ " + " ".join(args))
            proc = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace")
            for line in proc.stdout:
                self.emit(line.rstrip())
            code = proc.wait()
            if code != 0:
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


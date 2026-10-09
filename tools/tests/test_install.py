import functools
import hashlib
import http.server
import json
import os
import shutil
import socketserver
import subprocess
import tarfile
import tempfile
import threading
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parent.parent
TAG = "v9.9.9"
STUB_VERSION = "0.0.1"


def write_stub(path, version):
    path.write_text('#!/usr/bin/env bash\nif [ "${1:-}" = "--version" ]; then echo "%s"; fi\nexit 0\n' % version)
    path.chmod(0o755)


class FakeReleases(http.server.SimpleHTTPRequestHandler):
    port = 0

    def asset_url(self, name):
        return "http://127.0.0.1:%d/releases/download/%s/%s" % (self.port, TAG, name)

    def reply(self, payload):
        body = json.dumps(payload).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path == "/repos/test/test/releases/latest":
            self.reply({"tag_name": TAG})
        elif self.path == "/repos/test/test/releases/tags/%s" % TAG:
            self.reply({"assets": [
                {"browser_download_url": self.asset_url("anyps5-9.9.9-Linux.tar.gz")},
                {"browser_download_url": self.asset_url("SHA256SUMS.txt")},
            ]})
        else:
            super().do_GET()

    def log_message(self, *args):
        pass


class InstallTest(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix="anyps5-install-test"))
        web = self.tmp / "web" / "releases" / "download" / TAG
        web.mkdir(parents=True)
        pkg = self.tmp / "pkg" / "anyps5-9.9.9-Linux"
        (pkg / "bin").mkdir(parents=True)
        (pkg / "tools").mkdir(parents=True)
        write_stub(pkg / "bin" / "launcher", "9.9.9")
        write_stub(pkg / "bin" / "relinker", "9.9.9")
        (pkg / "tools" / "convert.sh").write_text("#!/usr/bin/env bash\nexit 0\n")
        archive = web / "anyps5-9.9.9-Linux.tar.gz"
        with tarfile.open(archive, "w:gz") as tar:
            tar.add(pkg, arcname="anyps5-9.9.9-Linux")
        digest = hashlib.sha256(archive.read_bytes()).hexdigest()
        (web / "SHA256SUMS.txt").write_text("%s  %s\n" % (digest, archive.name))
        handler = functools.partial(FakeReleases, directory=str(self.tmp / "web"))
        self.server = socketserver.TCPServer(("127.0.0.1", 0), handler)
        FakeReleases.port = self.server.server_address[1]
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.env = dict(os.environ, ANYPS5_API_URL="http://127.0.0.1:%d" % FakeReleases.port, ANYPS5_REPO="test/test")

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        shutil.rmtree(self.tmp, ignore_errors=True)

    def run_tool(self, name, *args, **extra_env):
        env = dict(self.env)
        env.update(extra_env)
        return subprocess.run(["bash", str(TOOLS / name), *args], capture_output=True, text=True, env=env)

    def test_help(self):
        for name in ("install.sh", "update.sh"):
            result = self.run_tool(name, "--help")
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_check_reports_update(self):
        root = self.tmp / "installed"
        (root / "bin").mkdir(parents=True)
        (root / "tools").mkdir(parents=True)
        write_stub(root / "bin" / "launcher", STUB_VERSION)
        shutil.copy(TOOLS / "update.sh", root / "tools" / "update.sh")
        result = subprocess.run(["bash", str(root / "tools" / "update.sh"), "--check"], capture_output=True, text=True, env=self.env)
        self.assertEqual(result.returncode, 100, result.stderr)
        self.assertIn("ANYPS5_UPDATE_AVAILABLE", result.stdout)

    def test_check_up_to_date(self):
        root = self.tmp / "current"
        (root / "bin").mkdir(parents=True)
        (root / "tools").mkdir(parents=True)
        write_stub(root / "bin" / "launcher", "9.9.9")
        shutil.copy(TOOLS / "update.sh", root / "tools" / "update.sh")
        result = subprocess.run(["bash", str(root / "tools" / "update.sh"), "--check"], capture_output=True, text=True, env=self.env)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("ANYPS5_UP_TO_DATE", result.stdout)


    def test_install_from_release(self):
        prefix = self.tmp / "prefix"
        result = self.run_tool("install.sh", "--prefix", str(prefix))
        self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
        version = subprocess.run([str(prefix / "bin" / "launcher"), "--version"], capture_output=True, text=True)
        self.assertEqual(version.stdout.strip(), "9.9.9")

    def test_update_replaces_tree(self):
        root = self.tmp / "installed"
        (root / "bin").mkdir(parents=True)
        (root / "tools").mkdir(parents=True)
        write_stub(root / "bin" / "launcher", STUB_VERSION)
        write_stub(root / "bin" / "relinker", STUB_VERSION)
        shutil.copy(TOOLS / "update.sh", root / "tools" / "update.sh")
        result = subprocess.run(["bash", str(root / "tools" / "update.sh"), "--yes"], capture_output=True, text=True, env=self.env)
        self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
        version = subprocess.run([str(root / "bin" / "launcher"), "--version"], capture_output=True, text=True)
        self.assertEqual(version.stdout.strip(), "9.9.9")

    def test_corrupt_archive_rejected(self):
        archive = self.tmp / "web" / "releases" / "download" / TAG / "anyps5-9.9.9-Linux.tar.gz"
        with archive.open("r+b") as handle:
            handle.seek(-8, 2)
            handle.write(b"BADBYTES")
        result = self.run_tool("install.sh", "--prefix", str(self.tmp / "prefix-bad"))
        self.assertEqual(result.returncode, 2, result.stdout)
        self.assertIn("checksum mismatch", result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()

from pathlib import Path
import sys
import tempfile

from test_guest_intel_trampolines import PLAIN_SITE, guest_fixture
from test_guest_module_directories import needed_libraries
from test_guest_host_libc import convert


def main():
    relinker = Path(sys.argv[1]).resolve()
    imports = ("libkernel.prx", "libc.prx")
    with tempfile.TemporaryDirectory(prefix="anyps5-guest-decrypted-") as directory:
        work = Path(directory)

        needed = convert(relinker, work / "decrypted-only", imports, ["other.prx"],
                         extra=[("Media/Modules/libc.prx.esbak", guest_fixture(PLAIN_SITE))])
        assert needed == ["$ORIGIN/app0/Media/Modules/libc.prx.esbak.guest.prx",
                          "$ORIGIN/app0/sce_module/other.prx.guest.prx",
                          "libkernel.prx"], needed

        needed = convert(relinker, work / "prefers-plain", imports, ["other.prx"],
                         extra=[("Media/Modules/libc.prx", guest_fixture(PLAIN_SITE)),
                                ("Media/Modules/libc.prx.esbak", guest_fixture(PLAIN_SITE))])
        assert needed == ["$ORIGIN/app0/Media/Modules/libc.prx.guest.prx",
                          "$ORIGIN/app0/sce_module/other.prx.guest.prx",
                          "libkernel.prx"], needed

        needed = convert(relinker, work / "encrypted-only", imports, ["other.prx"],
                         extra=[("Media/Plugins/PS5Util.prx", b"not an elf at all")])
        assert needed == ["$ORIGIN/app0/sce_module/other.prx.guest.prx", *imports], needed

        plugins = work / "module-dir" / "Plugins"
        needed = convert(relinker, work / "module-dir", imports, ["other.prx"],
                         options=("--module-dir", str(plugins)),
                         extra=[("Plugins/extra.prx.esbak", guest_fixture(PLAIN_SITE))])
        assert needed == ["$ORIGIN/app0/sce_module/other.prx.guest.prx",
                          "libkernel.prx", "libc.prx"], needed
        assert (work / "module-dir" / "app0" / "Plugins" / "extra.prx.esbak.guest.prx").is_file()
    print("Guest decrypted module integration tests passed")


if __name__ == "__main__":
    main()
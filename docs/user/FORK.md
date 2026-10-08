# Fork tools

Fast Python checks, upstream sync, per-commit build artifacts, and dumped-game helpers for this fork. Upstream build, conventions, release, and progress workflows are unchanged.

## Fast checks

[`.github/workflows/fork-tools.yml`](../../.github/workflows/fork-tools.yml) runs the Python suites without submodules or a compiler: [`tools/tests/test_progress.py`](../../tools/tests/test_progress.py), [`tools/tests/test_import_audit.py`](../../tools/tests/test_import_audit.py), [`tools/hw-oracle/test_hw_oracle.py`](../../tools/hw-oracle/test_hw_oracle.py), and [`tools/tests/test_check_dump.py`](../../tools/tests/test_check_dump.py).

## Build artifacts

[`.github/workflows/build-artifacts.yml`](../../.github/workflows/build-artifacts.yml) builds the full project plus the `libs` target on every push to `main` and uploads `prx-linux`, `prx-windows`, and both relinker binaries as versioned artifacts (`v0.0.0-<sha>`), kept 14 days. Run it manually with `workflow_dispatch` for a fresh build without a commit.

## Upstream sync

[`.github/workflows/fork-sync.yml`](../../.github/workflows/fork-sync.yml) fast-forwards `main` to `boykopovar/AnyPS5` `main` every 6 hours when clean, and opens a `chore(sync)` pull request when diverged. Run manually with `workflow_dispatch` to pick another upstream, branch, or force pull-request mode.

## Dump check

[`tools/check_dump.py`](../../tools/check_dump.py) reports whether a dumped-game directory holds a clean ELF executable with `sce_module/`, `sce_modules/`, or `prx/` beside it, as required by [relinker usage](USAGE.md). Exit `0` means ready, `1` means the primary executable is still a SELF container or PKG archive, `2` means no usable executable was found.

```sh
python3 tools/check_dump.py "/path/to/dump"
```

A dumped `eboot.bin` with SELF magic (`4f 15 3d 1d`) is rejected by the relinker; the input must be a clean ELF (`7f 45 4c 46`). Keep the decrypted backup outside the dump or rename it to `input.elf`.

## Convert and audit

[`tools/convert.sh`](../../tools/convert.sh) validates the dump, picks `eboot.bin` or `input.elf`, and runs the built relinker with `--registry`. [`tools/audit.sh`](../../tools/audit.sh) classifies the registry against the built `.prx` libraries.

```sh
cmake -S . -B build-relinker -G Ninja -DCMAKE_BUILD_TYPE=Release -DANYPS5_RELINKER_ONLY=ON -DBUILD_TESTING=ON
cmake --build build-relinker --parallel
tools/convert.sh --dump "/path/to/dump" --out out
tools/audit.sh --registry out/*.registry.json --modules "/path/to/dump/sce_module"
```

Full library builds and game execution follow [build instructions](../dev/BUILD.md). The audit needs `build/core/libs/libs` from the `libs` target; without it the classification names no providing library.

## Run a converted game

Runtime layout is `app.elf`, `libs/*.prx` (built system libraries), `app0/` (game resources plus relinker `Guest module:` files), per [relinker usage](USAGE.md#runtime-layout). [`tools/run.sh`](../../tools/run.sh) fills `libs/` from the build and launches the game:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++
cmake --build build --parallel
cmake --build build --target libs --parallel
tools/run.sh --game out/app.elf
```

Your title boots when audit shows `absent 0`; `stub` entries load and throw only if called. This fork's sample converts to 411 references (`implemented 291`, `stub 41`, `module 76`, `absent 0`) and currently stops at `NGS2: rack id 0x2001 is not implemented` in `libSceNgs2.native`.


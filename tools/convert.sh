#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DUMP=""
OUT=""
WINDOWS=0
TO_INTEL=0
UNUSED_FILTER=0
EXTRA_ARGS=()
usage() {
  cat <<EOF
Usage: $(basename "$0") --dump <dir> --out <dir> [--windows] [--to-intel] [--unused-filter 0|1|2] [-- <relinker args>...]
EOF
}
while [ $# -gt 0 ]; do
  case "$1" in
    --dump) DUMP="${2:?}"; shift 2;;
    --out) OUT="${2:?}"; shift 2;;
    --windows) WINDOWS=1; shift;;
    --to-intel) TO_INTEL=1; shift;;
    --unused-filter) UNUSED_FILTER="${2:?}"; shift 2;;
    --help|-h) usage; exit 0;;
    --) shift; EXTRA_ARGS+=("$@"); break;;
    *) echo "unknown option: $1" >&2; usage >&2; exit 2;;
  esac
done
if [ -z "$DUMP" ] || [ -z "$OUT" ]; then
  usage >&2
  exit 2
fi
case "$UNUSED_FILTER" in
  0|1|2) ;;
  *) echo "unused-filter must be 0, 1 or 2" >&2; exit 2;;
esac
python3 "$ROOT/tools/check_dump.py" "$DUMP"
INPUT=""
for candidate in "$DUMP/eboot.bin" "$DUMP/input.elf"; do
  if [ -f "$candidate" ]; then
    INPUT="$candidate"
    break
  fi
done
if [ -z "$INPUT" ]; then
  INPUT="$(find "$DUMP" -maxdepth 1 -type f \( -name '*.elf' -o -name 'eboot*' \) | sort | head -n 1 || true)"
fi
if [ -z "$INPUT" ]; then
  echo "FAIL: no executable candidate under $DUMP" >&2
  exit 2
fi
mkdir -p "$OUT"
NAME="$(basename "$INPUT")"
if [ "$WINDOWS" -eq 1 ]; then
  OUTPUT="$OUT/${NAME%.*}.exe"
else
  OUTPUT="$OUT/${NAME%.*}.elf"
fi
RELINKER="$ROOT/build-relinker/core/relinker/relinker"
if [ ! -x "$RELINKER" ]; then
  RELINKER="$ROOT/build/core/relinker/relinker"
fi
if [ ! -x "$RELINKER" ]; then
  echo "FAIL: relinker binary not found (configure build-relinker or build first)" >&2
  exit 2
fi
ARGS=()
if [ "$WINDOWS" -eq 1 ]; then
  ARGS+=(--windows)
fi
if [ "$TO_INTEL" -eq 1 ]; then
  ARGS+=(--to-intel)
fi
ARGS+=("unused-filter=$UNUSED_FILTER" --registry)
ARGS+=("${EXTRA_ARGS[@]}")
echo "input: $INPUT"
echo "output: $OUTPUT"
"$RELINKER" "${ARGS[@]}" "$INPUT" "$OUTPUT"

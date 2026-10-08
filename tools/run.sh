#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
GAME=""
LIBS="$ROOT/build/core/libs/libs"
APP0=""
usage() {
  cat <<EOF
Usage: $(basename "$0") --game <app.elf> [--libs <dir>] [--app0 <dir>]
EOF
}
while [ $# -gt 0 ]; do
  case "$1" in
    --game) GAME="${2:?}"; shift 2;;
    --libs) LIBS="${2:?}"; shift 2;;
    --app0) APP0="${2:?}"; shift 2;;
    --help|-h) usage; exit 0;;
    *) echo "unknown option: $1" >&2; usage >&2; exit 2;;
  esac
done
if [ -z "$GAME" ]; then
  usage >&2
  exit 2
fi
DIR="$(dirname "$GAME")"
mkdir -p "$DIR/libs" "$DIR/app0"
if [ -n "$APP0" ]; then
  cp -r "$APP0"/. "$DIR/app0/"
fi
if [ -d "$LIBS" ]; then
  cp -n "$LIBS"/*.prx "$DIR/libs/" 2>/dev/null || true
fi
if ! ls "$DIR/libs"/*.prx >/dev/null 2>&1; then
  echo "FAIL: no .prx in $DIR/libs (build libs target or pass --libs)" >&2
  exit 2
fi
chmod +x "$GAME"
exec "$GAME"


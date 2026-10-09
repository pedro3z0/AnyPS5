#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REGISTRY=""
LIBS="$ROOT/build/core/libs/libs"
LIBS_SET=0
MODULES=()
usage() {
  cat <<EOF
Usage: $(basename "$0") --registry <json> [--libs <dir>] [--modules <dir>]...
EOF
}
while [ $# -gt 0 ]; do
  case "$1" in
    --registry) REGISTRY="${2:?}"; shift 2;;
    --libs) LIBS="${2:?}"; LIBS_SET=1; shift 2;;
    --modules) MODULES+=("--modules" "${2:?}"); shift 2;;
    --help|-h) usage; exit 0;;
    *) echo "unknown option: $1" >&2; usage >&2; exit 2;;
  esac
done
if [ -z "$REGISTRY" ]; then
  usage >&2
  exit 2
fi
if [ "$LIBS_SET" -eq 0 ] && [ ! -d "$LIBS" ] && [ -d "$ROOT/lib/anyps5/libs" ]; then
  LIBS="$ROOT/lib/anyps5/libs"
fi
python3 "$ROOT/tools/import_audit.py" "$REGISTRY" --libs "$LIBS" "${MODULES[@]}"

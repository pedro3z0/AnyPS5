#!/usr/bin/env bash
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [ "${1:-}" = "--help" ] || [ "${1:-}" = "-h" ]; then
  cat <<EOF
Usage: $(basename "$0") [--prefix dir] [--system]
Removes the AnyPS5 launcher binaries and installed tree. Games, library,
settings, and logs are kept.
EOF
  exit 0
fi
exec "$HERE/install.sh" --uninstall "$@"

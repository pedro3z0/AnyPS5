#!/usr/bin/env bash
set -euo pipefail
REPO="${ANYPS5_REPO:-pedro3z0/AnyPS5}"
API="${ANYPS5_API_URL:-https://api.github.com}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
usage() {
  cat <<EOF
Usage: $(basename "$0") [--repo owner/name] [--tag v0.1.0] [--check] [--yes]
Updates an installed AnyPS5 tree in place. With --check, only reports:
exit 0 when up to date, 100 when an update is available, 2 on error.
EOF
}
TAG=""
CHECK=0
YES=0
while [ $# -gt 0 ]; do
  case "$1" in
    --repo) REPO="${2:?}"; shift 2;;
    --tag) TAG="${2:?}"; shift 2;;
    --check) CHECK=1; shift;;
    --yes) YES=1; shift;;
    --help|-h) usage; exit 0;;
    *) echo "unknown option: $1" >&2; usage >&2; exit 2;;
  esac
done
current() {
  "$ROOT/bin/launcher" --version 2>/dev/null || echo "unknown"
}
latest_tag() {
  if [ -n "$TAG" ]; then
    echo "$TAG"
    return 0
  fi
  curl -sSfL "$API/repos/$REPO/releases/latest" | grep -o '"tag_name": *"[^"]*"' | head -n 1 | cut -d'"' -f4
}
version_gt() {
  [ "$(printf '%s\n%s\n' "$1" "$2" | sort -V | tail -n 1)" = "$2" ] && [ "$1" != "$2" ]
}
latest_sum_url() {
  curl -sSfL "$API/repos/$REPO/releases/tags/$1" | grep -o '"browser_download_url": *"[^"]*SHA256SUMS.txt"' | head -n 1 | cut -d'"' -f4
}
verify_archive() {
  local archive="$1"
  local sums="$2"
  local name
  name="$(basename "$archive")"
  (cd "$(dirname "$archive")" && grep -F " $name" "$sums" | sha256sum -c --status --strict - 2>/dev/null)
}
CURRENT="$(current)"
LATEST="$(latest_tag)"
if [ -z "$LATEST" ]; then
  echo "FAIL: could not reach releases for $REPO" >&2
  exit 2
fi
CURRENT_TRIMMED="${CURRENT#v}"
LATEST_TRIMMED="${LATEST#v}"
if [ "$CURRENT" = "unknown" ] || version_gt "$CURRENT_TRIMMED" "$LATEST_TRIMMED"; then
  echo "ANYPS5_UPDATE_AVAILABLE $CURRENT -> $LATEST"
  if [ "$CHECK" -eq 1 ]; then
    exit 100
  fi
else
  echo "ANYPS5_UP_TO_DATE $CURRENT"
  exit 0
fi
URL="$(curl -sSfL "$API/repos/$REPO/releases/tags/$LATEST" | grep -o '"browser_download_url": *"[^"]*Linux.tar.gz"' | head -n 1 | cut -d'"' -f4)"
if [ -z "$URL" ]; then
  echo "FAIL: no Linux package asset in release $LATEST" >&2
  exit 2
fi
if [ "$YES" -eq 0 ]; then
  echo -n "install $LATEST over $ROOT? [y/N] "
  read -r answer
  case "$answer" in
    y|Y) ;;
    *) echo "cancelled"; exit 0;;
  esac
fi
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
echo "downloading $URL"
ARCHIVE="$WORK/$(basename "$URL")"
  curl -sSfL -o "$ARCHIVE" "$URL"
SUMS="$(latest_sum_url "$LATEST")"
if [ -z "$SUMS" ]; then
  echo "FAIL: no SHA256SUMS.txt in release $LATEST" >&2
  exit 2
fi
curl -sSfL -o "$WORK/SHA256SUMS.txt" "$SUMS"
verify_archive "$ARCHIVE" "$WORK/SHA256SUMS.txt" || {
  echo "FAIL: checksum mismatch for $URL" >&2
  exit 2
}
STAGE="$(mktemp -d)"
tar -xzf "$ARCHIVE" -C "$STAGE" --strip-components=1
if [ ! -x "$STAGE/bin/launcher" ] || [ ! -x "$STAGE/bin/relinker" ]; then
  echo "FAIL: $URL is not a launcher package" >&2
  exit 2
fi
"$STAGE/bin/launcher" --version >/dev/null || {
  echo "FAIL: packaged launcher does not start" >&2
  exit 2
}
rm -rf "$ROOT/bin" "$ROOT/tools" "$ROOT/lib" "$ROOT/core" "$ROOT/docs"
mv "$STAGE/bin" "$STAGE/tools" "$ROOT/"
for dir in lib core docs; do
  if [ -d "$STAGE/$dir" ]; then
    mv "$STAGE/$dir" "$ROOT/"
  fi
done
rmdir "$STAGE"
echo "installed $LATEST; restart the launcher if it is running"

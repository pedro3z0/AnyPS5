#!/usr/bin/env bash
set -euo pipefail
REPO="${ANYPS5_REPO:-pedro3z0/AnyPS5}"
PREFIX="${HOME}/.local"
TAG=""
SOURCE=""
SYSTEM=0
usage() {
  cat <<EOF
Usage: $(basename "$0") [--repo owner/name] [--prefix dir] [--tag v0.1.0] [--source <dir>] [--system] [--uninstall]
Installs the AnyPS5 launcher from a GitHub release asset, or from a source tree with --source.
EOF
}
MODE="install"
while [ $# -gt 0 ]; do
  case "$1" in
    --repo) REPO="${2:?}"; shift 2;;
    --prefix) PREFIX="${2:?}"; shift 2;;
    --tag) TAG="${2:?}"; shift 2;;
    --source) SOURCE="${2:?}"; shift 2;;
    --system) SYSTEM=1; shift;;
    --uninstall) MODE="uninstall"; shift;;
    --help|-h) usage; exit 0;;
    *) echo "unknown option: $1" >&2; usage >&2; exit 2;;
  esac
done
if [ "$SYSTEM" -eq 1 ]; then
  PREFIX="/usr/local"
fi
API="${ANYPS5_API_URL:-https://api.github.com}"
BIN="$PREFIX/bin"
ROOT="$PREFIX/lib/anyps5"
asset_url() {
  local tag="$1"
  curl -sSfL "$API/repos/$REPO/releases/tags/$tag" | grep -o '"browser_download_url": *"[^"]*Linux.tar.gz"'
}
sum_url() {
  local tag="$1"
  curl -sSfL "$API/repos/$REPO/releases/tags/$tag" | grep -o '"browser_download_url": *"[^"]*SHA256SUMS.txt"' | head -n 1 | cut -d'"' -f4
}
verify_archive() {
  local archive="$1"
  local sums="$2"
  local name
  name="$(basename "$archive")"
  (cd "$(dirname "$archive")" && grep -F " $name" "$sums" | sha256sum -c --status --strict - 2>/dev/null)
}
version_of() {
  "$1" --version 2>/dev/null || echo "unknown"
}
if [ "$MODE" = "uninstall" ]; then
  echo "removing $ROOT and $BIN/anyps5-launcher and $BIN/relinker"
  rm -rf "$ROOT" "$BIN/anyps5-launcher" "$BIN/relinker"
  echo "your games, library, and settings are kept:"
  echo "  dumps and converted games stay under ~/anyps5-launcher"
  echo "  ~/.config/anyps5 holds your library and launcher settings"
  echo "  ~/.local/share/anyps5 holds command logs"
  echo "remove those directories yourself if you want them gone too"
  exit 0
fi
if [ -n "$SOURCE" ]; then
  if [ -d "$SOURCE/.git" ]; then
    SOURCE_VERSION="$(git -C "$SOURCE" describe --tags --exact-match 2>/dev/null || git -C "$SOURCE" rev-parse --short HEAD)"
  fi
  if [ ! -x "$SOURCE/build/core/launcher/anyps5-launcher" ] || [ ! -x "$SOURCE/build/core/relinker/relinker" ] || ! ls "$SOURCE/build/core/libs/libs/"*.prx >/dev/null 2>&1; then
    echo "FAIL: build the source tree first (cmake --build $SOURCE/build --target launcher relinker libs)" >&2
    exit 2
  fi
  mkdir -p "$BIN"
  cmake --install "$SOURCE/build" --prefix "$ROOT"
  ln -sf "$ROOT/bin/anyps5-launcher" "$BIN/anyps5-launcher"
  ln -sf "$ROOT/bin/relinker" "$BIN/relinker"
  if [ -z "${SOURCE_VERSION:-}" ]; then
    echo "installed $(version_of "$BIN/anyps5-launcher") from $SOURCE to $ROOT"
  else
    echo "installed $SOURCE_VERSION from $SOURCE to $ROOT"
  fi
else
  if [ -z "$TAG" ]; then
    TAG="$(curl -sSfL "$API/repos/$REPO/releases/latest" | grep -o '"tag_name": *"[^"]*"' | head -n 1 | cut -d'"' -f4)"
  fi
  if [ -z "$TAG" ]; then
    echo "FAIL: no releases found for $REPO" >&2
    exit 2
  fi
  URL="$(asset_url "$TAG" | head -n 1 | cut -d'"' -f4)"
  if [ -z "$URL" ]; then
    echo "FAIL: no Linux package asset in release $TAG" >&2
    exit 2
  fi
  WORK="$(mktemp -d)"
  trap 'rm -rf "$WORK"' EXIT
  echo "downloading $URL"
  ARCHIVE="$WORK/$(basename "$URL")"
  curl -sSfL -o "$ARCHIVE" "$URL"
  SUMS="$(sum_url "$TAG")"
  if [ -z "$SUMS" ]; then
    echo "FAIL: no SHA256SUMS.txt in release $TAG" >&2
    exit 2
  fi
  curl -sSfL -o "$WORK/SHA256SUMS.txt" "$SUMS"
  verify_archive "$ARCHIVE" "$WORK/SHA256SUMS.txt" || {
    echo "FAIL: checksum mismatch for $URL" >&2
    exit 2
  }
  STAGE="$(mktemp -d)"
  tar -xzf "$ARCHIVE" -C "$STAGE" --strip-components=1
  if [ ! -x "$STAGE/bin/anyps5-launcher" ] || [ ! -x "$STAGE/bin/relinker" ]; then
    echo "FAIL: $URL is not a launcher package" >&2
    exit 2
  fi
  rm -rf "$ROOT"
  mkdir -p "$ROOT"
  mv "$STAGE/bin" "$STAGE/tools" "$ROOT/"
  if [ -d "$STAGE/lib" ]; then
    mv "$STAGE/lib" "$STAGE/core" "$STAGE/docs" "$ROOT/"
  fi
  rmdir "$STAGE"
  mkdir -p "$BIN"
  ln -sf "$ROOT/bin/anyps5-launcher" "$BIN/anyps5-launcher"
  ln -sf "$ROOT/bin/relinker" "$BIN/relinker"
  echo "installed $(version_of "$BIN/anyps5-launcher") ($TAG) to $ROOT"
fi
case ":$PATH:" in
  *":$BIN:"*) ;;
  *) echo "add $BIN to your PATH to run launcher from anywhere";;
esac

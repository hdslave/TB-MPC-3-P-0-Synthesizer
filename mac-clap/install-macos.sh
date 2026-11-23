#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$ROOT/build/macos/clap"
DEST="/Library/Audio/Plug-Ins/CLAP"

if [ "${1:-}" = "--user" ]; then
    DEST="$HOME/Library/Audio/Plug-Ins/CLAP"
fi

"$ROOT/build-macos.sh"

SUDO=""
if [ ! -w "$DEST" ] && [ "$DEST" = "/Library/Audio/Plug-Ins/CLAP" ]; then
    SUDO="sudo"
fi

BUNDLE="$BUILD_DIR/TB-MPC-3-P-0.clap"
$SUDO mkdir -p "$DEST"
$SUDO rm -rf "$DEST/TB-MPC-3-P-0.clap"
$SUDO cp -R "$BUNDLE" "$DEST/TB-MPC-3-P-0.clap"
if [ "$DEST" = "/Library/Audio/Plug-Ins/CLAP" ] && [ -e "$HOME/Library/Audio/Plug-Ins/CLAP/TB-MPC-3-P-0.clap" ]; then
    rm -rf "$HOME/Library/Audio/Plug-Ins/CLAP/TB-MPC-3-P-0.clap"
fi
echo "Installed $DEST/TB-MPC-3-P-0.clap"

if pgrep -x "OsxFL" >/dev/null 2>&1; then
    echo "NOTE: FL Studio (OsxFL) is currently running. Please Quit (Cmd+Q) and reopen FL Studio so macOS loads the updated .clap binary."
else
    echo "Done! Open your CLAP host (e.g., FL Studio -> Options -> Manage Plugins -> Find installed plugins)."
fi

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

BUNDLE="$BUILD_DIR/Slink.clap"
$SUDO mkdir -p "$DEST"
installed=Slink.clap
if [ -e "$DEST/TB-MPC-3-P-0.clap" ]; then
    installed=TB-MPC-3-P-0.clap
fi
staging=$($SUDO mktemp -d "$DEST/.slink-install.XXXXXXXX")
trap '$SUDO rm -rf "$staging"' EXIT
$SUDO cp -R "$BUNDLE" "$staging/new.clap"
codesign --verify --deep --strict "$staging/new.clap"
if [ -e "$DEST/$installed" ]; then
    $SUDO mv "$DEST/$installed" "$staging/previous.clap"
fi
if ! $SUDO mv "$staging/new.clap" "$DEST/$installed"; then
    if [ -e "$staging/previous.clap" ]; then
        $SUDO mv "$staging/previous.clap" "$DEST/$installed"
    fi
    exit 1
fi
if [ "$installed" != Slink.clap ]; then
    $SUDO rm -rf "$DEST/Slink.clap"
fi
if [ "$DEST" = "/Library/Audio/Plug-Ins/CLAP" ]; then
    for name in Slink.clap TB-MPC-3-P-0.clap; do
        rm -rf "$HOME/Library/Audio/Plug-Ins/CLAP/$name"
    done
fi
echo "Installed $DEST/$installed"

if pgrep -x "OsxFL" >/dev/null 2>&1; then
    echo "NOTE: FL Studio (OsxFL) is currently running. Please Quit (Cmd+Q) and reopen FL Studio so macOS loads the updated .clap binary."
else
    echo "Done! Open your CLAP host (e.g., FL Studio -> Options -> Manage Plugins -> Find installed plugins)."
fi

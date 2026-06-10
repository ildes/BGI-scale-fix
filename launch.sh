#!/bin/bash
GAME_DIR="$(dirname "$(readlink -f "$0")")"
WINEPREFIX="$HOME/path/to/your/wine-prefix"

cd "$GAME_DIR"

LC_ALL=ja_JP.UTF-8 WINEDLLOVERRIDES="version=n,b" \
    WINEPREFIX="$WINEPREFIX" \
    wine BGI.exe
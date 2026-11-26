#!/usr/bin/env bash
set -euo pipefail

app_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if ! pkg-config --exists sdl2 SDL2_ttf; then
    echo "Instale pkg-config, libsdl2-dev e libsdl2-ttf-dev para a previa no PC." >&2
    exit 2
fi

read -r -a sdl_flags <<< "$(pkg-config --cflags --libs sdl2 SDL2_ttf)"
mkdir -p "$app_dir/desktop"
g++ -std=c++17 -O2 -Wall -Wextra \
    "$app_dir/src/main.cpp" "${sdl_flags[@]}" \
    -o "$app_dir/desktop/desktop-pc"
printf 'Binario de previa: %s\n' "$app_dir/desktop/desktop-pc"

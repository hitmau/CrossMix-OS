#!/usr/bin/env bash
set -euo pipefail

app_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
output="$app_dir/desktop/bigpicture-pc"

if ! command -v pkg-config >/dev/null 2>&1; then
    echo "pkg-config is required. Install build-essential and pkg-config." >&2
    exit 2
fi
if [ -d /usr/lib/x86_64-linux-gnu/pkgconfig ]; then
    export PKG_CONFIG_LIBDIR=/usr/lib/x86_64-linux-gnu/pkgconfig:/usr/share/pkgconfig
    unset PKG_CONFIG_SYSROOT_DIR
fi

if ! pkg-config --exists sdl2 SDL2_image SDL2_ttf; then
    echo "SDL2 development packages are required: libsdl2-dev libsdl2-image-dev libsdl2-ttf-dev." >&2
    exit 2
fi

read -r -a sdl_flags <<< "$(pkg-config --cflags --libs sdl2 SDL2_image SDL2_ttf)"
"${BIGPICTURE_PC_CXX:-g++}" -std=c++17 -O2 -Wall -Wextra \
    "$app_dir/src/main.cpp" -I"$app_dir/src/vendor" \
    "${sdl_flags[@]}" -o "$output"
printf 'Desktop binary: %s\n' "$output"

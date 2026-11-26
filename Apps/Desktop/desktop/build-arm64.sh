#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
docker_image="${TRIMUI_DESKTOP_BUILD_IMAGE:-trimui-dev-gui:tg5040}"

docker run --rm -v "$repo_root:/workspace" "$docker_image" bash -lc '
  set -euo pipefail
  app=/workspace/Apps/Desktop
  sysroot="$TRIMUI_TOOLCHAIN/sysroot"
  mkdir -p "$app/bin"
  trimui-g++ -std=c++17 -O2 -Wall -Wextra \
    -I"$sysroot/usr/include/SDL2" \
    -I/opt/trimui/SDL2-2.26.1/include \
    "$app/src/main.cpp" \
    -L"$sysroot/usr/lib" \
    -L"$sysroot/usr/lib/aarch64-linux-gnu" \
    -lSDL2_ttf -lSDL2 -o "$app/bin/desktop"
  file "$app/bin/desktop"
'

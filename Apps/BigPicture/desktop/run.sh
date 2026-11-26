#!/usr/bin/env bash
set -euo pipefail
export TZ="${TZ:-America/Sao_Paulo}"

app_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
repo_root="$(cd "$app_dir/../../.." && pwd)"
reference_root="${1:-$repo_root}"
reference_root="$(cd "$reference_root" && pwd)"
runtime_root="$app_dir/desktop/.runtime"
mkdir -p "$runtime_root/Apps"

link_path() {
    local source="$1" target="$2"
    [ -e "$source" ] || return 0
    if [ -L "$target" ]; then
        [ "$(readlink "$target")" = "$source" ] || { echo "Unexpected link: $target" >&2; return 1; }
        return 0
    fi
    [ ! -e "$target" ] || { echo "Runtime path already exists: $target" >&2; return 1; }
    ln -s "$source" "$target"
}

# Reuse the full local CrossMix catalog without copying or changing ROMs.
for name in Emus Roms Best Imgs Icons; do
    link_path "$reference_root/$name" "$runtime_root/$name"
done
for source in "$reference_root"/Apps/*; do
    [ -e "$source" ] || continue
    [ "$(basename "$source")" = BigPicture ] && continue
    link_path "$source" "$runtime_root/Apps/$(basename "$source")"
done
link_path "$app_dir" "$runtime_root/Apps/BigPicture"

mkdir -p "$runtime_root/Data"
shopt -s nullglob dotglob
for source in "$reference_root"/Data/*; do
    [ -e "$source" ] || continue
    [ "$(basename "$source")" = BigPicture ] && continue
    link_path "$source" "$runtime_root/Data/$(basename "$source")"
done
mkdir -p "$runtime_root/Data/BigPicture"
settings="$runtime_root/Data/BigPicture/settings.json"
if [ ! -e "$settings" ] && [ -f "$repo_root/Data/BigPicture/settings.json" ]; then
    cp "$repo_root/Data/BigPicture/settings.json" "$settings"
fi
link_path "$repo_root/Data/BigPicture/videos" "$runtime_root/Data/BigPicture/videos"

if [ ! -x "$app_dir/desktop/bigpicture-pc" ]; then
    "$app_dir/desktop/build.sh"
fi
export BIGPICTURE_PLATFORM=desktop
export BIGPICTURE_SDCARD="$runtime_root"
python3 "$app_dir/catalog.py"
cd "$app_dir"
exec "$app_dir/desktop/bigpicture-pc"

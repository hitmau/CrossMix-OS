#!/usr/bin/env bash
set -euo pipefail

app_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
repo_root="$(cd "$app_dir/../.." && pwd)"
runtime="$app_dir/desktop/.runtime"
mkdir -p "$runtime/Data/Desktop/Workspace"

if [ ! -x "$app_dir/desktop/desktop-pc" ]; then
    "$app_dir/desktop/build-pc.sh"
fi

export CROSSMIX_DESKTOP_PLATFORM=preview
export CROSSMIX_DESKTOP_ROOT="$repo_root"
export CROSSMIX_DESKTOP_APP="$app_dir"
export CROSSMIX_DESKTOP_DATA="$runtime/Data/Desktop"
export CROSSMIX_DESKTOP_PYTHON="$(command -v python3 || true)"
exec "$app_dir/desktop/desktop-pc"

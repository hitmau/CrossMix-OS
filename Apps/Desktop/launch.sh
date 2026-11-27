#!/bin/sh

APP=/mnt/SDCARD/Apps/Desktop
DATA=/mnt/SDCARD/Data/Desktop
export CROSSMIX_DESKTOP_ROOT=/mnt/SDCARD
export CROSSMIX_DESKTOP_APP="$APP"
export CROSSMIX_DESKTOP_DATA="$DATA"
export CROSSMIX_DESKTOP_PYTHON=/mnt/SDCARD/System/bin/python3.11
export CROSSMIX_DESKTOP_PLATFORM=console
export PATH="/mnt/SDCARD/System/bin:$PATH"
export LD_LIBRARY_PATH="/usr/trimui/lib:/mnt/SDCARD/System/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS=1

mkdir -p "$DATA/Workspace"

if [ ! -x "$APP/bin/desktop" ]; then
    echo "O Desktop ainda nao foi compilado para ARM64. Consulte Apps/Desktop/README.md." >&2
    exit 1
fi

exec "$APP/bin/desktop"

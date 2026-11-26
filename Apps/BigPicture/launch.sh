#!/bin/sh
APP=/mnt/SDCARD/Apps/BigPicture
export PATH="/mnt/SDCARD/System/bin:$PATH"
export LD_LIBRARY_PATH="/usr/trimui/lib:/mnt/SDCARD/System/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export BIGPICTURE_PLATFORM=trimui
if [ ! -f /etc/openwrt_release ]; then
    echo 'Use a previa PC neste computador; este launcher e para o firmware TrimUI.' >&2
    exit 1
fi
if [ ! -x "$APP/bin/bigpicture" ]; then
    echo 'Compile primeiro o executavel ARM64 do Big Picture.' >&2
    exit 1
fi
PYTHON=/mnt/SDCARD/System/bin/python3
if [ -x "$PYTHON" ]; then "$PYTHON" "$APP/catalog.py"; fi
cd "$APP" || exit 1
exec "$APP/bin/bigpicture"

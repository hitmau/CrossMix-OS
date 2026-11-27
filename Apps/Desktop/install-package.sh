#!/bin/sh
set -eu

APP_SOURCE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
SDCARD_ROOT=${CROSSMIX_DESKTOP_TARGET:-/mnt/SDCARD}
APP_DEST=$SDCARD_ROOT/Apps/Desktop

if [ ! -d "$SDCARD_ROOT/System" ] || [ ! -d "$SDCARD_ROOT/Apps" ]; then
    echo "Nao encontrei o CrossMix em $SDCARD_ROOT. Extraia o pacote no cartao e tente novamente." >&2
    exit 1
fi
if [ ! -x "$APP_SOURCE/bin/desktop" ]; then
    echo "Binario ARM64 ausente em $APP_SOURCE/bin/desktop." >&2
    exit 1
fi

if [ "$APP_SOURCE" != "$APP_DEST" ]; then
    mkdir -p "$APP_DEST"
    cp -R "$APP_SOURCE"/. "$APP_DEST"/
fi

chmod +x "$APP_DEST/launch.sh" "$APP_DEST/bin/desktop"
echo "CrossMix Desktop instalado em $APP_DEST"
echo "Abra Desktop no menu Apps do CrossMix."

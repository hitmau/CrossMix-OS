#!/bin/sh
set -eu

PACKAGE_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PAYLOAD="$PACKAGE_DIR/payload"
SDCARD_ROOT=${TRIMUI_SD_ROOT:-/mnt/SDCARD}
APP_SOURCE="$PAYLOAD/Apps/Desktop"
DATA_SOURCE="$PAYLOAD/Data/Desktop"

fail() {
    printf 'CrossMix Desktop: %s\n' "$1" >&2
    exit 1
}

[ -d "$SDCARD_ROOT/System" ] || fail "Nao encontrei System em $SDCARD_ROOT. Extraia o pacote para o cartao CrossMix e execute novamente."
[ -d "$SDCARD_ROOT/Apps" ] || fail "Nao encontrei Apps em $SDCARD_ROOT."
[ -f "$APP_SOURCE/config.json" ] || fail "Payload Apps/Desktop ausente ao lado do instalador."
[ -f "$APP_SOURCE/bin/desktop" ] || fail "Binario ARM64 do Desktop ausente no pacote."
[ -f "$APP_SOURCE/icon.png" ] || fail "Icone do Desktop ausente no pacote."
[ -f "$DATA_SOURCE/Workspace/hello.py" ] || fail "Exemplos do workspace ausentes no pacote."

mkdir -p "$SDCARD_ROOT/Apps/Desktop" "$SDCARD_ROOT/Data/Desktop/Workspace"
cp -R "$APP_SOURCE/." "$SDCARD_ROOT/Apps/Desktop/"

# Keep any workspace files the user has already created.
for sample in hello.py hello.js; do
    if [ ! -e "$SDCARD_ROOT/Data/Desktop/Workspace/$sample" ]; then
        cp "$DATA_SOURCE/Workspace/$sample" "$SDCARD_ROOT/Data/Desktop/Workspace/$sample"
    fi
done
if [ ! -e "$SDCARD_ROOT/Data/Desktop/README.md" ]; then
    cp "$DATA_SOURCE/README.md" "$SDCARD_ROOT/Data/Desktop/README.md"
fi

# FAT/exFAT mounts may ignore chmod; CrossMix launchers still invoke scripts via sh.
chmod +x "$SDCARD_ROOT/Apps/Desktop/launch.sh" "$SDCARD_ROOT/Apps/Desktop/bin/desktop" 2>/dev/null || true

printf '\nCrossMix Desktop instalado em Apps/Desktop.\n'
printf 'Workspace preservado em Data/Desktop/Workspace.\n'
printf 'Volte para Apps no CrossMix; se a lista nao atualizar, reinicie apenas a interface.\n'

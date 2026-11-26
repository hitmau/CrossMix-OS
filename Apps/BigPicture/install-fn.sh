#!/bin/sh
set -eu
[ -f /etc/openwrt_release ] || { echo 'Execute no TrimUI.' >&2; exit 1; }
app=/mnt/SDCARD/Apps/BigPicture
editor=/usr/trimui/apps/fn_editor
[ -f "$editor/scripts.json" ] || { echo 'Editor Fn nao encontrado.' >&2; exit 1; }
[ -f "$editor/scripts.json.before-bigpicture" ] || cp "$editor/scripts.json" "$editor/scripts.json.before-bigpicture"
cp "$app/com.crossmix.bigpicture.sh" "$editor/"
chmod +x "$editor/com.crossmix.bigpicture.sh"
python=/mnt/SDCARD/System/bin/python3
"$python" "$app/register_fn.py" "$editor/scripts.json"
echo 'Abra Configuracoes > Fn key editor e selecione Big Picture: standby.'

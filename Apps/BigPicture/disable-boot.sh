#!/bin/sh
set -eu
[ -f /etc/openwrt_release ] || { echo 'Execute no TrimUI.' >&2; exit 1; }
target=/mnt/SDCARD/trimui/app/MainUI
if [ -f "$target" ] && grep -q 'BIGPICTURE_BOOT_WRAPPER' "$target"; then
    if [ -f "$target.before-bigpicture" ]; then
        mv "$target.before-bigpicture" "$target"
    else
        rm "$target"
    fi
fi
rm -f /mnt/SDCARD/trimui/app/.bigpicture-boot-installed

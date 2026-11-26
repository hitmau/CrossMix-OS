#!/bin/sh
set -eu
root=/mnt/SDCARD
target="$root/trimui/app/MainUI"
marker="$root/trimui/app/.bigpicture-boot-installed"
[ -f /etc/openwrt_release ] || { echo 'Execute no TrimUI.' >&2; exit 1; }
[ -x "$root/Apps/BigPicture/bin/bigpicture" ] || exit 1
mkdir -p "$root/trimui/app" "$root/Data/BigPicture"
if [ -f "$target" ] && ! grep -q 'BIGPICTURE_BOOT_WRAPPER' "$target"; then
    [ ! -e "$target.before-bigpicture" ] || { echo 'Backup de outro launcher ja existe.' >&2; exit 1; }
    cp "$target" "$target.before-bigpicture"
fi
cat > "$target.tmp" <<'WRAPPER'
#!/bin/sh
# BIGPICTURE_BOOT_WRAPPER
if [ ! -f /mnt/SDCARD/Data/BigPicture/disable-boot ]; then
    /bin/sh /mnt/SDCARD/Apps/BigPicture/launch.sh >> /mnt/SDCARD/Data/BigPicture/startup.log 2>&1
fi
exec /usr/trimui/bin/MainUI
WRAPPER
chmod +x "$target.tmp"
mv "$target.tmp" "$target"
touch "$marker"

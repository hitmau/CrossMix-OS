#!/bin/sh
# Backlight-only standby keeps input processing alive, allowing any-button wake.
# Deep suspend is deliberately not enabled before on-device wake validation.
ACTION=${1:-}
STATE=/tmp/bigpicture-power
mkdir -p "$STATE"
if [ "${BIGPICTURE_PLATFORM:-desktop}" != trimui ] || [ ! -f /etc/openwrt_release ]; then
    echo "desktop:$ACTION" >> "$STATE/events.log"
    exit 0
fi
write_backlight() {
    if [ -w /sys/class/disp/disp/attr/backlight ]; then
        printf '%s\n' "$1" > /sys/class/disp/disp/attr/backlight
    elif [ -w /sys/kernel/debug/dispdbg/start ]; then
        printf 'lcd0\n' > /sys/kernel/debug/dispdbg/name
        printf 'setbl\n' > /sys/kernel/debug/dispdbg/command
        printf '%s\n' "$1" > /sys/kernel/debug/dispdbg/param
        printf '1\n' > /sys/kernel/debug/dispdbg/start
    else
        echo 'Backlight interface unavailable' >&2
        return 1
    fi
}
case "$ACTION" in
  off)
    if [ ! -f "$STATE/brightness" ]; then
        value=$(cat /sys/class/disp/disp/attr/backlight 2>/dev/null)
        value=${value%% *}
        case "$value" in
            ''|*[!0-9]*|0)
                level=$(/usr/trimui/bin/shmvar brightness 2>/dev/null || true)
                case "$level" in ''|*[!0-9]*) value=200;; *) value=$((level * 23));; esac
                [ "$value" -gt 0 ] && [ "$value" -le 255 ] || value=200
                ;;
        esac
        printf '%s\n' "$value" > "$STATE/brightness"
    fi
    write_backlight 0
    ;;
  on)
    value=$(cat "$STATE/brightness" 2>/dev/null)
    case "$value" in ''|*[!0-9]*|0) value=200;; esac
    write_backlight "$value" && rm -f "$STATE/brightness"
    ;;
  deep)
    echo 'Deep suspend requires on-device validation; staying in screen-off mode.' >&2
    exit 2
    ;;
  *) exit 1;;
esac

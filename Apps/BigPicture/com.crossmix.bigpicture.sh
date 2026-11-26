#!/bin/sh
# Install through install-fn.sh; 0 sleeps, 1 resumes the Big Picture launcher.
case "${1:-}" in
    0|1)
        printf '%s\n' "$1" > /tmp/bigpicture-fn.tmp
        mv /tmp/bigpicture-fn.tmp /tmp/bigpicture-fn
        ;;
esac

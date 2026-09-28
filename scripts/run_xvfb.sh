#!/usr/bin/env bash
# Run a command against a headless X server (Xvfb + Mesa llvmpipe OpenGL).
#   scripts/run_xvfb.sh build/linux/src/app/Cadjitsu --selftest=smoke --out out/
set -euo pipefail

DISPLAY_NUM="${CADJITSU_XVFB_DISPLAY:-:99}"
if ! xdpyinfo -display "$DISPLAY_NUM" >/dev/null 2>&1 && ! [[ -e "/tmp/.X11-unix/X${DISPLAY_NUM#:}" ]]; then
    Xvfb "$DISPLAY_NUM" -screen 0 1920x1200x24 -nolisten tcp +extension GLX >/dev/null 2>&1 &
    for _ in $(seq 1 50); do
        [[ -e "/tmp/.X11-unix/X${DISPLAY_NUM#:}" ]] && break
        sleep 0.1
    done
fi

export DISPLAY="$DISPLAY_NUM"
export LANG="${LANG:-C.UTF-8}" LC_ALL="${LC_ALL:-C.UTF-8}"
export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-xcb}"
export QT_XCB_GL_INTEGRATION="${QT_XCB_GL_INTEGRATION:-xcb_glx}"
export LIBGL_ALWAYS_SOFTWARE=1
export __GLX_VENDOR_LIBRARY_NAME="${__GLX_VENDOR_LIBRARY_NAME:-mesa}"
exec "$@"

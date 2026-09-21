#!/bin/sh
# Populates .toolchains/device-sysroot/ for UI (LVGL + DRM) cross-compiling.
#
# UI targets link against the K230's *actual* on-device liblvgl.so/libdrm.so
# rather than a self-built LVGL, since the stock image already ships a
# proven, working combination (k230_phone_ui uses it) - see
# README.md. This script pulls those .so files plus matching
# upstream headers, so a caller only needs to re-run it, not follow a
# multi-step manual recipe.
#
# lv_conf.h in particular is NOT a static file fetchable from a repo path -
# an earlier version of this script fetched
# kendryte/k230_sdk's src/little/buildroot-ext/package/lvgl/port_src/lv_conf.h,
# which is a stale v8.2.0 config, silently wrong for this device's real
# v9.5.0 liblvgl.so (would very likely crash lv_init() - see README.md's
# boot-splash section for this exact device's prior history with a
# mismatched config crashing the app). The real generation path, confirmed
# working end-to-end and deployed to real hardware:
#   1. LVGL source pinned by LILYGO's own BSP overlay, at the exact commit
#      (not tag) Xinyuan-LilyGO/T-Display-K230's
#      k230_bsp/overlay/buildroot-overlay/package/lvgl/lvgl.mk names -
#      59dc7e436ae97a25e32656739ea6a943f9f11b6a in upstream lvgl/lvgl.
#   2. That overlay also carries this device's own lv_conf.defaults (board
#      value overrides) plus 4 patches, one of which
#      (0002-lvgl-linux-drm-add-k230-plane-rotation.patch) is the source of
#      the lv_linux_drm_set_rotation symbol README.md documents finding via
#      `nm -D` against the vendor's own liblvgl.so.
#   3. lv_conf.h itself is generated, not copied, via LVGL's own
#      scripts/generate_lv_conf.py against that exact commit's
#      lv_conf_template.h + the overlay's lv_conf.defaults - this is what
#      the overlay's own CMakeLists.txt does at build time, reproduced here.
#
# Usage: ./fetch-device-sysroot.sh [device-ssh-host]
# Default device-ssh-host is root@sightline.local.

set -e

DEVICE="${1:-root@sightline.local}"
ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
SYSROOT="$ROOT_DIR/.toolchains/device-sysroot"
LVGL_SRC="$ROOT_DIR/.toolchains/lvgl-src"
LIBDRM_SRC="$ROOT_DIR/.toolchains/libdrm-src"
LIBGPIOD_SRC="$ROOT_DIR/.toolchains/libgpiod-src"
LVGL_PINNED_COMMIT="59dc7e436ae97a25e32656739ea6a943f9f11b6a"

mkdir -p "$SYSROOT/lib" "$SYSROOT/include/drm"

echo "== LVGL source (pinned commit $LVGL_PINNED_COMMIT, not a release tag - see header) =="
if [ ! -f "$LVGL_SRC/lv_conf_template.h" ]; then
  mkdir -p "$LVGL_SRC"
  curl -fsL "https://github.com/lvgl/lvgl/archive/$LVGL_PINNED_COMMIT.tar.gz" \
    | tar xz -C "$LVGL_SRC" --strip-components=1
fi
mkdir -p "$SYSROOT/include/lvgl"
cp -r "$LVGL_SRC"/* "$SYSROOT/include/lvgl/"

echo "== lv_conf.h (generated from LILYGO's board overlay defaults, not copied) =="
curl -fsL "https://raw.githubusercontent.com/Xinyuan-LilyGO/T-Display-K230/main/k230_bsp/overlay/buildroot-overlay/package/lvgl/$LVGL_PINNED_COMMIT/lv_conf.defaults" \
  -o "$ROOT_DIR/.toolchains/lv_conf.defaults"
python3 "$LVGL_SRC/scripts/generate_lv_conf.py" \
  --template "$LVGL_SRC/lv_conf_template.h" \
  --defaults "$ROOT_DIR/.toolchains/lv_conf.defaults" \
  --config "$SYSROOT/include/lv_conf.h"

echo "== libdrm headers (2.4.124, matching the device's libdrm.so.2.124.0) =="
if [ ! -d "$LIBDRM_SRC/.git" ]; then
  git clone --depth 1 --branch libdrm-2.4.124 https://gitlab.freedesktop.org/mesa/drm.git "$LIBDRM_SRC"
fi
cp "$LIBDRM_SRC/xf86drm.h" "$LIBDRM_SRC/xf86drmMode.h" "$SYSROOT/include/"
cp "$LIBDRM_SRC"/include/drm/*.h "$SYSROOT/include/drm/"

echo "== libgpiod header (v2.x C API - TCA8418 bit-banged I2C; device ships libgpiod.so.3.1.1) =="
# No v2.1.1 tag exists in the GitHub mirror; any v2.x tag has the same
# stable gpiod.h API surface (confirmed against every gpiod_* symbol this
# codebase actually calls before relying on this).
if [ ! -f "$LIBGPIOD_SRC/include/gpiod.h" ]; then
  git clone --depth 1 --branch v2.3.1 https://github.com/brgl/libgpiod.git "$LIBGPIOD_SRC"
fi
cp "$LIBGPIOD_SRC/include/gpiod.h" "$SYSROOT/include/"

echo "== Shared libraries from the device (liblvgl and its transitive deps) =="
# Pulling the actual .so files, not just headers, is deliberate: it
# guarantees exact ABI/symbol-version compatibility with what will really
# be loaded at runtime, rather than trusting a same-numbered but
# independently-built library to match. liblvgl.so pulls in freetype,
# ffmpeg (av*/sws*), libevdev and libgpiod at this build's configuration;
# their own further transitive deps (OpenSSL, libpng, liblzma, ...) are
# deliberately NOT pulled here - see --allow-shlib-undefined in
# toolchain-riscv64-linux-gnu.cmake for why that's fine.
scp "$DEVICE:/usr/lib/liblvgl.so*" \
    "$DEVICE:/usr/lib/liblvgl_linux.so" \
    "$DEVICE:/usr/lib/liblvgl_thorvg.so*" \
    "$DEVICE:/usr/lib/liblvgl_examples.so*" \
    "$DEVICE:/usr/lib/liblvgl_demos.so*" \
    "$DEVICE:/usr/lib/libdrm.so*" \
    "$DEVICE:/usr/lib/libfreetype.so*" \
    "$DEVICE:/usr/lib/libavcodec.so*" \
    "$DEVICE:/usr/lib/libavformat.so*" \
    "$DEVICE:/usr/lib/libavutil.so*" \
    "$DEVICE:/usr/lib/libswscale.so*" \
    "$DEVICE:/usr/lib/libevdev.so*" \
    "$DEVICE:/usr/lib/libgpiod.so*" \
    "$SYSROOT/lib/"

echo "Done. Build UI targets with -DK230_DEVICE_SYSROOT=$SYSROOT (the default)."

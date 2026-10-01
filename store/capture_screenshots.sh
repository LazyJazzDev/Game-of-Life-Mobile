#!/bin/sh
# Capture raw App Store screenshots of Game of Life on booted simulators.
# Usage: capture_screenshots.sh <iphone-udid> <ipad-udid> <raw-dir>
# Install the simulator build first and keep both simulators in portrait.
# The LONGMARCH_SMOKE_* hooks tap the rendered controls and open patterns
# (see ios/README.md); tap points are fractions of the game view.
set -eu
IPHONE=$1 IPAD=$2 RAW=$3
mkdir -p "$RAW"
shot() { # device name language seconds env...
  d=$1 n=$2 l=$3 w=$4; shift 4
  env "$@" xcrun simctl launch --terminate-running-process "$d" net.lazyjazz.gameoflife -AppleLanguages "($l)" >/dev/null
  sleep "$w"
  xcrun simctl io "$d" screenshot "$RAW/$n.png" >/dev/null 2>&1
}
# device prefix language bottom-row-y top-row-y open-x speed-x play-x
run() {
  d=$1 p=$2 l=$3 yb=$4 yt=$5 xo=$6 xs=$7 xp=$8
  shot "$d" "$p-$l-1-running" "$l" 32 SIMCTL_CHILD_LONGMARCH_SMOKE_PATTERN="Gosper glider gun" \
    SIMCTL_CHILD_LONGMARCH_SMOKE_TAPS="$xo,$yb;$xs,$yb;$xp,$yb"
  shot "$d" "$p-$l-2-soup" "$l" 9 SIMCTL_CHILD_LONGMARCH_SMOKE_TAPS="$xp,$yt;$xs,$yb;$xs,$yb;$xs,$yb;$xp,$yb"
  shot "$d" "$p-$l-3-lib" "$l" 4 SIMCTL_CHILD_LONGMARCH_SMOKE_FILE=open
  shot "$d" "$p-$l-4-guns" "$l" 4 SIMCTL_CHILD_LONGMARCH_SMOKE_FILE=open SIMCTL_CHILD_LONGMARCH_SMOKE_FOLDER=gun
}
# Control rows measured on iPhone 18 Pro Max and iPad Pro 13-inch (M5).
for l in zh-Hans en; do
  run "$IPHONE" iphone "$l" 0.928 0.063 0.10 0.765 0.895 &
  run "$IPAD" ipad "$l" 0.954 0.04 0.053 0.874 0.945
  wait
done

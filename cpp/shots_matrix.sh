#!/usr/bin/env bash
# shots_matrix.sh — 多分辨率 / 多 DPI 的界面截图矩阵（Lead 验收用）
# 用法: ./shots_matrix.sh [前缀]
set -u
PREFIX="${1:-m}"
cd "$(dirname "$0")/build/bin" || exit 1
OUT="../../shots/matrix"
mkdir -p "$OUT"

# 场景: 物理宽 高 缩放  标签
CONFIGS=(
  "2520 1575 1.75 user-2560x1600-175"
  "2880 1620 2.0  fhd-4k-200"
  "1920 1080 1.0  hd-1080p-100"
  "3440 1440 1.0  uw-3440x1440"
  "1708 960  1.25 laptop-1366x768-125"
  "1424 861  1.0  design-1440x900"
)

SCENES=(
  "menu    "
  "levels  "
  "play    --level=3-3 --expr=x/(1+x*x)"
  "result  --level=3-3 --expr=x/(1+x*x)"
  "endless "
)

for cfg in "${CONFIGS[@]}"; do
  set -- $cfg
  W=$1; H=$2; SC=$3; TAG=$4
  for sc in "${SCENES[@]}"; do
    set -- $sc
    SCENE=$1; shift
    NAME="${PREFIX}_${TAG}_${SCENE}.png"
    ./FunctionCraft.exe --shot="$OUT/$NAME" --frames=20 --size="${W}x${H}" \
        --dpi-scale="$SC" --scene="$SCENE" "$@" >/dev/null 2>&1
    rm -f save.json
    if [ -f "$OUT/$NAME" ]; then
      SZ=$(python -c "from PIL import Image;print('%dx%d'%Image.open(r'$OUT/$NAME').size)" 2>/dev/null)
      printf "  %-28s %-8s %s\n" "$TAG" "$SCENE" "$SZ"
    else
      printf "  %-28s %-8s MISSING\n" "$TAG" "$SCENE"
    fi
  done
done

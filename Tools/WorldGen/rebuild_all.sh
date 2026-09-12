#!/usr/bin/env bash
# 전체 파이프라인: 생성 → (선택) 머티리얼 재생성 → 에디터 베이크 → 셰이더 대기 → 캡처
# 사용: bash Tools/WorldGen/rebuild_all.sh [--material] [--seed N] [--wait 초] [캡처 인자...]
set -u
cd "$(dirname "$0")/../.." || exit 1
MATERIAL=0; SEED=7; WAIT=240; CAPS=()
while [ $# -gt 0 ]; do
  case "$1" in
    --material) MATERIAL=1; shift;;
    --seed) SEED="$2"; shift 2;;
    --wait) WAIT="$2"; shift 2;;
    *) CAPS+=("$1"); shift;;
  esac
done
python Tools/WorldGen/generate_ashen_vale.py --seed "$SEED" 2>&1 | tail -2 || exit 1
if [ "$MATERIAL" = "1" ]; then
  PYTHONUTF8=1 python Tools/run_in_editor.py Tools/WorldGen/editor_make_landscape_material.py 2>&1 | grep "DONE\|reassigned\|FAILED\|Exception\|Error" | cut -c1-300
fi
PYTHONUTF8=1 python Tools/run_in_editor.py Tools/WorldGen/editor_build_open_world.py 2>&1 | grep "TDWorldGen\|FAILED\|Exception" | cut -c1-200 | tail -4
sleep "$WAIT"
echo "material compile failures in log: $(grep -c 'Failed to compile Material for platform' Saved/Logs/TDGame.log)"
if [ ${#CAPS[@]} -gt 0 ]; then
  PYTHONUTF8=1 python Tools/WorldGen/capture_views_mcp.py "${CAPS[@]}" 2>&1 | tail -1
fi

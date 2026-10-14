#!/usr/bin/env bash
# Git Bash에서 실행(에디터 실행 중): 메인 지역 전체 파이프라인 — 생성 → (선택) 머티리얼 재생성 → 에디터 베이크 → (선택) 이계 베이크 → 탑다운 재질 적용(P3-17) → 셰이더 대기 → 캡처
# 실행: bash Tools/WorldGen/rebuild_all.sh [--material] [--realms] [--seed N] [--wait 초] [캡처 인자...] (--realms: 메인 베이크 뒤 Saved/WorldGen/Realms 가 있으면 editor_build_realm.py 이어 실행, 이계 산출물은 generate_realm.py 로 미리 만든다)
# 출력: Saved/WorldGen/AshenVale/, 레벨 LV_DarkFantasy_OpenWorld 저장, (--realms) Saved/WorldGen/Realms/<RealmId>/build_report.json, (캡처 인자) Saved/WorldGen/Captures/<이름>.png
# 상태: 현행
set -u
cd "$(dirname "$0")/../.." || exit 1
MATERIAL=0; REALMS=0; SEED=7; WAIT=240; CAPS=()
while [ $# -gt 0 ]; do
  case "$1" in
    --material) MATERIAL=1; shift;;
    --realms) REALMS=1; shift;;
    --seed) SEED="$2"; shift 2;;
    --wait) WAIT="$2"; shift 2;;
    *) CAPS+=("$1"); shift;;
  esac
done
python Tools/WorldGen/generate_ashen_vale.py --seed "$SEED" 2>&1 | tail -2 || exit 1
if [ "$MATERIAL" = "1" ]; then
  PYTHONUTF8=1 python Tools/run_in_editor.py Tools/WorldGen/editor_make_landscape_material.py 2>&1 | grep "DONE\|reassigned\|reparented\|FAILED\|Exception\|Error" | cut -c1-300
fi
PYTHONUTF8=1 python Tools/run_in_editor.py Tools/WorldGen/editor_build_open_world.py 2>&1 | grep "TDWorldGen\|FAILED\|Exception" | cut -c1-200 | tail -4
if [ "$REALMS" = "1" ]; then
  if [ -d Saved/WorldGen/Realms ]; then
    PYTHONUTF8=1 python Tools/run_in_editor.py Tools/WorldGen/editor_build_realm.py 2>&1 | grep -i "realm\|FAILED\|Exception\|Error" | cut -c1-300 | tail -8
  else
    echo "--realms: Saved/WorldGen/Realms 없음, 이계 베이크 건너뜀 (python Tools/WorldGen/generate_realm.py --realm all 먼저)"
  fi
fi
PYTHONUTF8=1 python Tools/run_in_editor.py Tools/WorldGen/editor_apply_topdown_materials.py 2>&1 | grep "DONE\|FAILED\|Exception" | cut -c1-300
sleep "$WAIT"
echo "material compile failures in log: $(grep -c 'Failed to compile Material for platform' Saved/Logs/TDGame.log)"
if [ ${#CAPS[@]} -gt 0 ]; then
  PYTHONUTF8=1 python Tools/WorldGen/capture_views_mcp.py "${CAPS[@]}" 2>&1 | tail -1
fi

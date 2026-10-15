#!/usr/bin/env bash
# Git Bash에서 실행(에디터 실행 중): 연속 세계(D-45, P3-19) 전체 파이프라인 — (선택) 재질 → 필드 생성 → 이계 생성 → 연속 세계 검사기(실패하면 레벨을 건드리기 전에 멈춤) → 데이터 레이어 준비 → 필드 베이크 → 이계 베이크 → 정의 갱신 → 탑다운 재질 적용 → 셰이더 대기 → 캡처
# 실행: bash Tools/WorldGen/rebuild_all.sh [--material] [--seed N] [--wait 초] [캡처 인자...] (--realms 는 예전 호환용으로 받고 무시한다. 필드 베이크 모드는 Saved/WorldGen/open_world_build_request.json 을 따른다)
# 출력: Saved/WorldGen/AshenVale/, Saved/WorldGen/Realms/<RealmId>/(build_report.json 포함), Saved/WorldGen/data_layers_report.json, Saved/WorldGen/open_world_build_report.json, 레벨 LV_DarkFantasy_OpenWorld 저장, (캡처 인자) Saved/WorldGen/Captures/<이름>.png
# 상태: 현행(P3-19 순서, 에디터 실행 검증 전). 에디터 단계는 실패하면 그 자리에서 멈춘다
set -u
cd "$(dirname "$0")/../.." || exit 1
MATERIAL=0; SEED=7; WAIT=240; CAPS=()
while [ $# -gt 0 ]; do
  case "$1" in
    --material) MATERIAL=1; shift;;
    --realms) shift;;
    --seed) SEED="$2"; shift 2;;
    --wait) WAIT="$2"; shift 2;;
    *) CAPS+=("$1"); shift;;
  esac
done
run_editor() {
  local script="$1" pattern="$2" lines="$3" out code
  out=$(PYTHONUTF8=1 python Tools/run_in_editor.py "$script" 2>&1)
  code=$?
  printf '%s\n' "$out" | grep -E "$pattern" | cut -c1-300 | tail -n "$lines"
  if [ "$code" -ne 0 ]; then
    echo "FAILED: $script (exit $code)"
    exit "$code"
  fi
}
if [ "$MATERIAL" = "1" ]; then
  run_editor Tools/WorldGen/editor_make_topdown_materials.py "DONE|FAILED|Exception|Error" 6
  run_editor Tools/WorldGen/editor_make_landscape_material.py "DONE|reassigned|reparented|FAILED|Exception|Error" 6
fi
python Tools/WorldGen/generate_ashen_vale.py --seed "$SEED" 2>&1 | tail -2
[ "${PIPESTATUS[0]}" -eq 0 ] || { echo "FAILED: generate_ashen_vale.py"; exit 1; }
python Tools/WorldGen/generate_realm.py --realm all 2>&1 | tail -3
[ "${PIPESTATUS[0]}" -eq 0 ] || { echo "FAILED: generate_realm.py"; exit 1; }
[ -f Tools/WorldGen/check_continuous_world.py ] || { echo "FAILED: Tools/WorldGen/check_continuous_world.py missing"; exit 1; }
python Tools/WorldGen/check_continuous_world.py 2>&1 | tail -6
[ "${PIPESTATUS[0]}" -eq 0 ] || { echo "FAILED: check_continuous_world.py (level untouched)"; exit 1; }
run_editor Tools/WorldGen/editor_setup_data_layers.py "TDDataLayers|FAILED|Exception|Error" 4
run_editor Tools/WorldGen/editor_build_open_world.py "TDWorldGen|FAILED|Exception|Error" 6
run_editor Tools/WorldGen/editor_build_realm.py "TDRealm|FAILED|Exception|Error" 10
run_editor Tools/WorldGen/editor_make_definitions.py "definitions created|field return|FAILED|Exception|Error" 6
run_editor Tools/WorldGen/editor_apply_topdown_materials.py "DONE|FAILED|Exception" 4
sleep "$WAIT"
echo "material compile failures in log: $(grep -c 'Failed to compile Material for platform' Saved/Logs/TDGame.log)"
if [ ${#CAPS[@]} -gt 0 ]; then
  PYTHONUTF8=1 python Tools/WorldGen/capture_views_mcp.py "${CAPS[@]}" 2>&1 | tail -1
fi

"""실행 환경: 시스템 Python(언리얼 에디터가 TDGame을 열고 MCP 포트 8000·원격 실행이 켜진 상태, 에디터 앞창 유지 L-editor-06). FluidFX 무대 PIE 하나에서 변형(노출·조명·불빛·불꽃 카드/볼륨 재질 값·활성 프리셋 임시 복제본·폰 위치·카메라 거리·액터 위치·컴포넌트 숨김·콘솔 명령·깨어 있는 유체 액터·폭발 트리거)을 차례로 적용하고 정착 시간 뒤 또는 게임 시간 예약 시각마다 게임 화면을 찍어 이름표 붙은 비교 격자를 만든다.
실행 명령: python Tools/FluidFX/pie_lookdev_fluid.py --variants <변형 JSON> [--level /Game/FluidFX/Maps/LV_TDFluidFireStage] [--settle-seconds 3] [--stop-pie] [--fresh-pie] [--output-dir <폴더>] [--run-id <실행 식별자>] (PowerShell). 변형 JSON = {"base": {적용}, "variants": [{"name": "영문", "apply": {적용}, "settle": 초}]}, 적용 키(이 순서로 적용) = tuning_reload({"tuning": 조절 사본, "look": 모양 사본(빈 문자열이면 정본, 둘 다 Saved 아래), "reset": "reinit"(기본, 모든 라이브 액터 init_system_state_from_preset 뒤 같은 단계 apply 원격 호출 첫머리에서 모든 액터의 TDFluidLiveComponent.reset_simulation_state()(솔버·젖음·웅덩이·비 충격·식생 눌림), 결과 tuning_reload.state_reset{actors, component_count, errors, game_time}·state_reset_actor_count, report.state_reset_actor_count 는 C++ 재적용의 구조 변경 초기화만 센다)|"keep", "relative_to_base": true(기본, 먼저 정본을 다시 읽어 보고서 세대를 기다림), "read_back": [{"struct", "field", "kinds": [종류]}] 그 종류 액터의 실효 값을 다시 읽음} 다른 키보다 먼저 ReloadPresetTuningFromPaths 뒤 GetLastReapplyReport 세대를 기다리고, 하나라도 쓰면 실행 끝 finally 에서 정본을 다시 읽음)·awake({라벨: bool}이면 적힌 것만, 목록이면 목록 외는 재움)·pawn([x, y]면 현재 z 유지, [x, y, z]면 그 높이로)·arm·view([pitch, yaw] 조종 회전, 붐이 따라감)·props({라벨: {속성: 값}} 액터 속성. 값이 dict면 현재 값이 구조체일 때 필드를 고쳐 되넣고, UObject(컴포넌트)면 그 객체의 필드만 고치고 되넣지 않으며, 배열이면 정수 문자열 키의 원소를 고쳐 배열을 되넣는다 예 {"targets": {"1": {"burst_radius_scale": 1.5}}}, "append:<i>" 키는 같은 호출의 정수 키 편집 전 i번 원소를 복제(구조체 copy, UObject 원소는 오류)해 고친 뒤 끝에 붙인다 예 {"targets": {"0": {"receives_trails": false}, "append:0": {"receives_burst": false, "heat_scale": 0.4}}})·hide({라벨: bool} 게임 숨김)·location({라벨: [x, y, z]} 액터 위치, null 성분은 현재 값 유지 예 [null, null, 0.0], 다시 읽은 값 기록)·hide_components({라벨 또는 "pawn": [컴포넌트 이름 일부(대소문자 무시)]} 맞는 씬 컴포넌트 set_visibility(False), 예 ShieldMesh)·preset({라벨: {점 경로 또는 중첩 dict: 값}} 라이브 컴포넌트 활성 프리셋의 임시 복제본을 고쳐 active_preset에 넣음, 원본 에셋은 건드리지 않음. simulation_config·plane_settings·volume_render 는 비 Custom·비 override 프리셋이면 R2 로 무시되므로 오류(종료 1)이고 "force_override": true 면 복제본의 override_preset_defaults 를 켬. plane_settings·volume_render·volume_height_cm은 C++가 매 스텝 다시 읽어 그대로 반영되고, 반영 여부는 다음 촬영의 preset_effect(볼륨 MID 매개변수 값, camera_depth_bias_cm은 내부 표면 메시 이동 거리)로 보고)·preset_reinit([라벨] 복제본으로 init_system_state_from_preset 재초기화, 시뮬 상태와 앞선 sim 편집이 초기화되므로 프리셋의 시뮬 설정·출력 재질처럼 초기화 때만 읽는 필드를 바꿀 때만)·exposure_ev100(모든 후처리 볼륨의 min/max 밝기 = EV100, 효과는 자동 노출 볼륨만. AEM_MANUAL 볼륨은 엔진이 min/max 를 무시하고(PostProcessEyeAdaptation.cpp 수동 분기) 기본은 바이어스를 건드리지 않아 mode manual_ignored·현재 auto_exposure_bias 만 기록, 같은 apply 의 exposure_manual 이 "bias" 일 때만 auto_exposure_bias = 원래 바이어스 + 물리 카메라 EV100(auto_exposure_apply_physical_camera_exposure 꺼짐이면 0) − EV100(mode manual), 결과 exposure_applied[{label, mode, fields, ...}])·exposure_manual("ignore"(기본)|"bias", exposure_ev100 없이 쓰거나 다른 값이면 키 오류)·lights·fire_light·flame(액터의 모든 불꽃 카드 MID)·volume(볼륨 MID)·ember({라벨: {재질 매개변수: 값}} ember_component 0번 재질 사슬에 FluidEmber 필요, MID 아니면 만들어 설정, 부모에 없는 이름은 missing, settle 촬영마다 result.json에 ember_mid)·sim(simulation_config 필드, apply_simulation_config)·emitter(source_emitter 필드, 비 Custom·비 override 프리셋이면 오류(종료 1), "force_override": true 면 override 켠 프리셋 복제본을 active_preset 에 넣고 진행)·surface_material(라이브 컴포넌트 출력 재질 교체 후 내부 메시 재연결)·console([명령] PIE 플레이어 컨트롤러로 실행, "이름 값" 꼴 cvar는 다시 읽은 값 기록, Shot 결과는 result.json console_screenshots)·speed·move([goal_x, goal_y] 는 옛 절대 목표(내비 있으면 simple_move_to, 없으면 순간이동), {"distance_cm": d, "yaw": 도(기본 현재 조종 회전 yaw)} 는 내비 없이 예약과 다른 슬레이트 틱 콜백이 2D 이동 ≥ d 까지 매 틱 pawn.add_movement_input(방향, 1, True)(Mover 폰은 ProduceInput 의 ConsumeMoveIntent 로 소비), 순간이동 없음, 시한 2d/최대 속도 + 3 게임 초 초과는 오류. 바깥 Python 이 그 변형 촬영 뒤(base 는 곧바로) 끝을 기다려 move 결과를 {mode, yaw, goal, moved_cm, timed_out, error?} 로 바꿔 적음)·trigger(같은 호출의 게임 시간을 trigger_game_time으로 기록)·time_dilation(전역 시간 배율)·schedule({"times": [기준 뒤 게임 초], "reference": "trigger"|"now", "prefix": "영문", "fixed_fps": 60(0이면 끔, 예약 동안 t.OverrideFPS 고정 간격 뒤 복구 L-editor-45), "file_wait_ticks": 30(1이면 그려진 다음 틱에 바로 다음 장), "timeout": 초} 원격 호출 하나에서 슬레이트 틱 콜백을 걸어 기준 뒤 게임 시간마다 HighResShot을 요청하고, 앞 장이 그려지고 파일이 생기거나 틱 한도가 지나야 다음 장을 건 뒤 스스로 해제, 이 변형은 settle 촬영을 하지 않음, reference trigger는 같은 apply의 trigger 필요). props·emitter·sim·preset 값 변환: 현재 값이 Vector·Vector2D면 목록 [x, y(, z)], Rotator면 [pitch, yaw, roll], LinearColor면 [r, g, b(, a)], Transform이면 {"location": [], "rotation": [pitch, yaw, roll], "scale": []}(빠진 키는 현재 값 유지). CAL-0 키(spec-cal0-v2 C9·1.3): view_target(액터 라벨, 컨트롤러 set_view_target_with_blend)·call({라벨: {function, component?, args?}} 함수 호출, 첫 성공 게임 시각 call_game_time)·wait_until({label, component?, property, equals, timeout} 바깥에서 조건이 맞을 때까지 기다린 뒤 apply, 시간 초과는 키 오류)·look_live({materials: {/Game 패키지: {Scalars|Vectors: {이름: 값}}}, niagara: {패키지: {Floats|Ints|Bools|Vectors2|Vectors3|Colors: {이름: 값}}}, niagara_reset, base: {패키지: {Scalars|Vectors: {이름: 원 MI 값}}}} 직접 부모만 맞춤. 소유자 primitive_slot(MI 슬롯은 MID 로 교체 뒤 설정)·decal(MI면 MID 만들어 설정)은 파이썬이 설정, cpp_mid(CPP_LOOK_OWNER_CLASS_NAMES 액터의 MID)·cloud_component·foliage_flow(B5 get_flow_copy_material_instance)·post_process 는 교체·설정 없이 tuning_reload.look 사본으로 C++ 가 다시 적용, niagara 는 set_variable_* 뒤 niagara_reset 이면 reinitialize_system. 1초 뒤 MID 를 다시 읽어 단언, base 는 MI 에셋 원래 값과 대조)·probe({stats: [{label|subsystem, component?, function, prefix}], rt: [{label, component?, property|function(둘 중 하나, function 은 렌더 타깃을 돌려주는 게터), box: [u0, v0, u1, v1], step, channel: r|g|b|a|rg_len|rgb_len, radial_center?, threshold?, prefix}], times: [apply 뒤 게임 초]})·schedule.reference "call"·schedule.events([{time, console?, call?, move?}] 틱 콜백 안에서 기준 뒤 게임 시각에 실행, move 는 위 두 형식). 예약 reference now 의 시간 원점은 단계 apply 원격 호출의 게임 시각(PIE 시작 아님)이고 reinit 상태 초기화와 같은 호출이라 같은 시각·tuning_reload.base({tuning, look} 대조 사본, relative_to_base 때 정본 대신 먼저 적재)·tuning_reload.read_back[] 의 owner(live_component|actor|volume_driver|subsystem:<클래스>|actor_class:<클래스>)·access(<속성>|call:<함수>, 함수 없으면 "게터 없음: B5 필요"). 모르는 적용 키·하위 키는 find_key_errors(apply) 순수 함수(에디터 접근 없음, calib_sweep 건식이 import)가 키 오류로 낸다
출력: Saved/FluidFX/lookdev/<시각>/<순번>_<이름>.png, 예약 촬영은 <순번>_<prefix>_<기준 뒤 ms>ms.png와 <순번>_<move|eventNNNNNms>_walk.json(사전 move 걷기 상태), <순번>_<prefix>_schedule.json(장마다 요청·실제 찍힌 게임 시간, 늦음, 틱 사이 벽시계 간격 real_dt(요청 틱)·real_dt_mean(앞 장 뒤 촬영 대기 아닌 틱 평균, 실제 fps 대용)·capture_real_dt(촬영 끊김 포함), world_dt, 파일 대기 틱, 고정 간격 복구, preset_effect), grid.png, result.json(변형별 적용 결과·shot_game_time·preset_effect·schedule·console_screenshots). 단계 apply 결과에 cloud_flow_restart({actors, errors, game_time}: 레벨에 TDFluidCloudLayersActor 가 있으면 reset 방식(reinit·keep)과 상관없이 같은 apply 원격 호출 첫머리에서 restart_cloud_flow_clock() 으로 구름 흐름 시계(재질 CloudFlowSeconds) 원점을 다시 잡아 예약 reference now 원점과 같은 게임 시각, 구름 액터가 없으면 키 없음). result.json 에 tuning_reload(보고서·다시 읽기, 요청 read_back 이 [] 이면 [])·canonical_reload·key_errors. 종료 코드 0 기대한 장 전부 찍음·키 오류 없음/1 일부 실패 또는 키 오류(tuning_reload 실패·R2 무시 키)/2 환경 문제. CAL-0: 단계 apply 결과에 look_applied({패키지: {이름: 소유자 수}})·look_owners·look_base({checked, mismatches})·look_read_back({패키지: {구역: {이름: {expected, read[], matches}}}})·owner_checks([{package, owner, by: python|cpp, actor, component, slot, matches, reason?}])·look_errors·skipped_look(look_live.skip_if_no_owner 패키지에 소유자가 없어 적용·다시 읽기를 건너뜀, 키 오류 아님)·probe({samples: [{time, elapsed, metrics, errors, grid_paths}], metrics: probe.<prefix>.<필드 파스칼 이름 또는 mean·p50·p95·max·fraction_above·radial_alignment_mean> 표본 중앙값, errors})와 <순번>_probe_<prefix>_<k>.json 격자·call·call_game_time·view_target·wait, 예약 상태 events_done. 키 오류(종료 1)에 apply 원격 호출 자체 오류(base 포함)·schedule 시작·끝 오류·move 형식(목록 2개 숫자 또는 {distance_cm > 0, yaw?})·걷기 오류(시한 초과·폰 없음)·schedule.events move 오류·state_reset 오류·cloud_flow_restart 오류·모르는 키·view_target 없음·call 함수 없음·wait_until 시간 초과·look 적용 대상 없음·base 불일치·다시 읽기 불일치·"C++ 소유 MID가 모양 사본을 다시 적용하지 않음"·게터 없음·probe 함수·렌더 타깃 없음이 더해진다. result.json 첫머리에 run_id(--run-id)·started_at·variants_sha256(변형 JSON sha256). 원격 호출 결과가 4096 바이트를 넘으면 에디터가 Saved/FluidFX/lookdev/remote_results/<시각>_<uuid>_<action>.json 에 쓰고 표식 뒤엔 {result_file} 만 내며 바깥이 읽은 뒤 지운다(UE stdout TextIOWrapper 가 8192자 덩어리에서 표식과 본문을 다른 로그 항목으로 가르고, 원격 실행 TCP 수신은 큰 응답을 자를 수 있다). 표식 뒤 응답은 뒤 출력 항목까지 모아 해석하고, 해석 실패·결과 파일 없음·원격 실행 예외는 죽지 않고 그 호출의 error(탐침이면 표본 오류 → 키 오류). probe.rt 렌더 타깃 없음은 소유자 없음·게터 None·속성 None(TSoftObjectPtr 는 get_editor_property 가 동기 적재를 시도하므로 경로가 비었거나 적재 실패)으로 나눠 적는다
상태: 검증됨(2026-10-18 모닥불·횃불·폭발 변형 9회 실행, campfire_v1~v5·torch_v1·explosion_v1. 2026-10-20 view·hide·props 추가와 flame 전 카드 적용으로 측면 룩데브 6회 실행). 2026-10-20 추가한 awake dict·game_time·time_dilation과 console·schedule·location·hide_components·preset·preset_reinit·값 변환·배열 원소·UObject 필드 편집은 미검증(PIE 미실행, 정적 검토만). PIE 런타임 값만 바꾸므로 에셋은 바뀌지 않는다. 고른 값은 재질·무대 생성 스크립트에 옮겨 적는다. 2026-10-21 FX-8c 11차 ember 키는 미검증(PIE 미실행), EmberTint* 는 재질 재생성 뒤에만 효과. 2026-10-21 FX-8c 12차 props 배열 "append:<i>" 키는 2026-10-21 FX-8c 12차 AP 세션 PIE 9회에서 확인(복제 원소가 배열 끝에 붙고 수정 필드가 다시 읽기와 같음, 가짜 unreal 오프라인 시험에서 기존 정수 키 결과 불변). 2026-10-25 P3(spec-wave0-data-bridge-v2 3.9) tuning_reload·--output-dir·R2 무시 키 오류·force_override 는 가짜 unreal 오프라인 시험만(PIE 미실행). 2026-10-30 CAL-0 B3(view_target·call·schedule.events·reference call·wait_until·look_live 소유자 7종·probe stats·rt·tuning_reload.base·다시 읽기 owner·access·find_key_errors)와 CAL-0 통합 probe.rt function(B5 텍스처 게터)은 calib_offline_test.py check_cal0_lookdev 가짜 unreal 오프라인 시험만(PIE 미실행). 실제 엔진에서 미검증: RenderingLibrary.read_render_target_raw_uv 격자 읽기 속도, 월드 서브시스템 찾기(2026-10-30 UE 5.8 에 SubsystemBlueprintLibrary 가 없어 get_engine_subsystem 뒤 ObjectIterator 월드 대조로 바꿈, 에디터 월드 원격 질의만 확인·PIE 미확인), 후처리 blendables·구름 material 속성 이름, Niagara 사용자 변수 이름 접두(User.), 다시 읽기 1초는 실시간 sleep(시간 배율 1 가정). 2026-10-31 frame 결함 4건(사전 move 입력 걷기·조용한 base/단계/이벤트 오류·수동 노출 바이어스(반박 뒤 exposure_manual "bias" 명시 선택일 때만, 전역 변환은 구름 무대 EV 12 를 −12 스톱으로 만들었다)·reinit 전체 상태 초기화)은 calib_offline_test.py check_lookdev_frame_fixes 가짜 unreal 오프라인 시험만(PIE 미실행). 2026-10-31 탐침 응답 파싱(frame g4 painter_stroke·k1 k1_recovery 의 표식 뒤 빈 줄 IndexError)·큰 결과 파일 전달·원격 실행 예외 흡수·렌더 타깃 없음 사유·--run-id 는 calib_offline_test.py check_capture_tool_fixes(가짜 UE 로그 스트림) 오프라인 시험만(PIE 미실행). 2026-10-31 K3 구름 흐름 시계 재시작(cloud_flow_restart)은 calib_offline_test.py check_lookdev_frame_fixes F10 가짜 unreal 오프라인 시험만(PIE 미실행, C++ RestartCloudFlowClock 빌드 전에는 구름 액터마다 키 오류).
"""
import argparse
import difflib
import hashlib
import json
import statistics
import sys
import time
import uuid
from datetime import datetime
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "Tools"))
sys.path.insert(0, str(ROOT / "Tools" / "FluidFX"))
import pie_check_fluid  # noqa: E402
import pie_showcase_fluid  # noqa: E402
import run_in_editor  # noqa: E402
import uemcp  # noqa: E402

RUN_TOKEN = datetime.now().strftime("%Y%m%d-%H%M%S")
OUTPUT_DIRECTORY = ROOT / "Saved" / "FluidFX" / "lookdev" / RUN_TOKEN
REMOTE_RESULT_DIRECTORY = ROOT / "Saved" / "FluidFX" / "lookdev" / "remote_results"
REMOTE_RESULT_INLINE_LIMIT_BYTES = 4096
REMOTE_RESULT_PRINT = "print(RESULT_MARKER, json.dumps(result, ensure_ascii=False, default=str))"
REMOTE_RESULT_EMIT = "emit_lookdev_result(result)"
SCREENSHOT_DIRECTORY = ROOT / "Saved" / "Screenshots" / "WindowsEditor"
DEFAULT_LEVEL_PATH = "/Game/FluidFX/Maps/LV_TDFluidFireStage"
SHOT_SIZE = "1280x720"
GRID_TILE_WIDTH = 640
GRID_COLUMNS = 2
FRAME_FILE_WAIT_SECONDS = 10.0
SCHEDULE_POLL_SECONDS = 0.5
SCHEDULE_POLL_MARGIN_SECONDS = 15.0
EXIT_PARTIAL = 1
EXIT_ENVIRONMENT = 2
TUNING_RELOAD_WAIT_SECONDS = 20.0
TUNING_RELOAD_POLL_SECONDS = 0.25
TUNING_RESET_MODES = ("reinit", "keep")
LOOK_READ_BACK_DELAY_SECONDS = 1.0
PROBE_POLL_SECONDS = 0.25
PROBE_SLEEP_MARGIN_SECONDS = 0.02
PROBE_WAIT_MARGIN_SECONDS = 30.0
WAIT_UNTIL_POLL_SECONDS = 0.25
WAIT_UNTIL_DEFAULT_TIMEOUT_SECONDS = 60.0
OUTER_APPLY_KEYS = ("tuning_reload", "wait_until", "probe")
APPLY_KEYS = ("tuning_reload", "wait_until", "awake", "pawn", "arm", "view", "view_target", "props", "hide", "location", "hide_components", "preset", "preset_reinit",
              "exposure_ev100", "exposure_manual", "lights", "fire_light", "flame", "volume", "ember", "sim", "emitter", "surface_material", "console", "move", "speed", "trigger", "call",
              "time_dilation", "look_live", "schedule", "probe")
APPLY_OBJECT_KEYS = {
    "tuning_reload": ("tuning", "look", "reset", "relative_to_base", "read_back", "base"),
    "schedule": ("times", "reference", "prefix", "fixed_fps", "file_wait_ticks", "timeout", "events"),
    "wait_until": ("label", "component", "property", "equals", "timeout"),
    "look_live": ("materials", "niagara", "niagara_reset", "base", "skip_if_no_owner"),
    "probe": ("stats", "rt", "times"),
}
APPLY_ITEM_KEYS = {
    "tuning_reload.read_back": ("struct", "field", "kinds", "owner", "access", "json_path", "expected"),
    "tuning_reload.base": ("tuning", "look"),
    "schedule.events": ("time", "console", "call", "move"),
    "probe.stats": ("label", "component", "subsystem", "function", "prefix"),
    "probe.rt": ("label", "component", "property", "function", "box", "step", "channel", "radial_center", "threshold", "prefix"),
}
CALL_KEYS = ("component", "function", "args")
SCHEDULE_REFERENCES = ("now", "trigger", "call")
MOVE_KEYS = ("distance_cm", "yaw")
EXPOSURE_MANUAL_MODES = ("ignore", "bias")
LOOK_GROUP_SECTIONS = {"materials": ("Scalars", "Vectors"), "niagara": ("Floats", "Ints", "Bools", "Vectors2", "Vectors3", "Colors"), "base": ("Scalars", "Vectors")}
PROBE_CHANNELS = ("r", "g", "b", "a", "rg_len", "rgb_len")
READ_BACK_OWNER_KINDS = ("live_component", "actor", "volume_driver")
READ_BACK_OWNER_PREFIXES = ("subsystem:", "actor_class:")

LOOKDEV_ACTIONS = r'''
import inspect
import os
import sys
import time

SCHEDULE_ATTRIBUTE = "td_lookdev_schedule"
PRESET_CHECK_ATTRIBUTE = "td_lookdev_preset_checks"
FIXED_STEP_VARIABLE = "t.OverrideFPS"
DEFAULT_SCHEDULE_FIXED_FPS = 60.0
DEFAULT_SCHEDULE_FILE_WAIT_TICKS = 30
PRESET_FIELD_NAMES = ("preset_kind", "override_preset_defaults", "simulation_config", "default_channel", "output_material", "surface_mesh", "external_niagara_system", "parameter_collection", "external_renderer_actor_tag", "volume_material", "volume_mesh", "volume_height_cm", "plane_settings", "translucency_sort_priority", "volume_render")
VOLUME_PARAMETER_NAMES = {"volume_size_multiplier": "VolumeBoxScale"}
TRANSFORM_KEYS = {"location", "rotation", "scale"}
APPEND_KEY_PREFIX = "append:"
CUSTOM_KIND_NAME = "CUSTOM"
R2_COPY_FIELDS = ("simulation_config", "plane_settings", "volume_render")
REAPPLY_REPORT_FIELDS = ("table_generation", "tuning_source_path", "look_source_path", "reapplied_actor_count", "state_reset_actor_count", "changed_field_count", "live_material_count", "bake_only_changed_count")
READ_BACK_SOURCES = {
    "TDFluidSimulationConfig": ("live_component", "simulation_config"),
    "TDFluidSourceEmitterSettings": (None, "source_emitter"),
    "TDFluidDownstreamFeedSettings": (None, "downstream_feed"),
    "TDFluidFlameCardLayerSettings": (None, "flame_card_layers"),
    "TDFluidEmberSettings": ("ember_component", None),
    "TDFluidFireLightSettings": ("fire_light_component", None),
}
MISSING_GETTER_ERROR = "게터 없음: B5 필요"
CPP_LOOK_OWNER_CLASS_NAMES = ("TDFluidLiveActor", "TDFluidCloudLayersActor", "TDFluidFoliageFlowActor", "TDFluidFragmentEmitterActor", "TDFluidSurfaceOutputActor", "TDFluidTrackDeformMeshActor", "TDFluidWaterActor")
CPP_LOOK_OWNER_COMPONENT_CLASS_NAMES = ("TDFluidVolumeDriverComponent",)
CPP_ONLY_LOOK_HOLDERS = ("cloud", "foliage", "post_process")
LOOK_HOLDER_OWNERS = {"slot": ("primitive_slot", "cpp_mid"), "decal": ("decal", "decal"), "cloud": ("cloud_component", "cloud_component"),
                      "foliage": ("foliage_flow", "foliage_flow"), "post_process": ("post_process", "post_process")}
FOLIAGE_FLOW_GETTER = "get_flow_copy_material_instance"
LOOK_MATERIAL_SECTIONS = ("Scalars", "Vectors")
LOOK_READ_BACK_TOLERANCE = 1e-4
CPP_LOOK_MISMATCH_REASON = "C++ 소유 MID가 모양 사본을 다시 적용하지 않음"
PYTHON_LOOK_MISMATCH_REASON = "다시 읽기 불일치"
PYTHON_MID_ATTRIBUTE = "td_lookdev_python_mids"
WALK_ATTRIBUTE = "td_lookdev_walk"
WALK_TIMEOUT_MARGIN_SECONDS = 3.0
WALK_WALL_MARGIN_SECONDS = 60.0
MINIMUM_WALL_TIME_DILATION = 0.1
EXPOSURE_BIAS_ATTRIBUTE = "td_lookdev_exposure_bias"
MANUAL_EXPOSURE_METHOD = "AEMMANUAL"
EXPOSURE_BIAS_TOLERANCE = 1e-6
EXPOSURE_MANUAL_IGNORE = "ignore"
EXPOSURE_MANUAL_BIAS = "bias"
MANUAL_IGNORED_EXPOSURE_FIELDS = ["auto_exposure_min_brightness", "auto_exposure_max_brightness"]


def find_actor_by_label(world, label):
    for actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor):
        if actor.get_actor_label() == label:
            return actor
    return None


def set_material_values(material, values):
    applied = {}
    if not isinstance(material, unreal.MaterialInstanceDynamic):
        return {"error": "MID 아님: " + str(material)}
    for name, value in values.items():
        if isinstance(value, list):
            material.set_vector_parameter_value(name, unreal.LinearColor(*(value + [1.0] * (4 - len(value)))))
        else:
            material.set_scalar_parameter_value(name, float(value))
        applied[name] = value
    return applied


def apply_awake(world, requested):
    states = {}
    for actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.TDFluidLiveActor):
        label = actor.get_actor_label()
        if isinstance(requested, dict) and label not in requested:
            continue
        is_awake = bool(requested[label]) if isinstance(requested, dict) else label in requested
        actor.get_editor_property("live_component").set_simulation_awake(is_awake)
        states[label] = is_awake
    return states


def is_manual_exposure(settings):
    return bool(settings.get_editor_property("override_auto_exposure_method")) and normalized_enum_name(settings.get_editor_property("auto_exposure_method")) == MANUAL_EXPOSURE_METHOD


def physical_camera_ev100(settings):
    if not settings.get_editor_property("auto_exposure_apply_physical_camera_exposure"):
        return 0.0
    fstop = float(settings.get_editor_property("depth_of_field_fstop"))
    return math.log2(fstop * fstop * float(settings.get_editor_property("camera_shutter_speed")) * 100.0 / max(1.0, float(settings.get_editor_property("camera_iso"))))


def original_exposure_bias(volume, settings):
    current = float(settings.get_editor_property("auto_exposure_bias"))
    record = (getattr(unreal, EXPOSURE_BIAS_ATTRIBUTE, None) or {}).get(volume.get_path_name())
    if record is not None and abs(current - record["applied"]) < EXPOSURE_BIAS_TOLERANCE:
        return record["original"]
    return current


def apply_manual_exposure(volume, settings, ev100):
    original_bias = original_exposure_bias(volume, settings)
    camera_ev100 = physical_camera_ev100(settings)
    bias = original_bias + camera_ev100 - float(ev100)
    settings.set_editor_property("override_auto_exposure_bias", True)
    settings.set_editor_property("auto_exposure_bias", bias)
    records = getattr(unreal, EXPOSURE_BIAS_ATTRIBUTE, None) or {}
    records[volume.get_path_name()] = {"original": original_bias, "applied": bias}
    setattr(unreal, EXPOSURE_BIAS_ATTRIBUTE, records)
    return {"mode": "manual", "fields": {"auto_exposure_bias": bias}, "original_bias": original_bias, "physical_camera_ev100": camera_ev100,
            "ignored_by_engine": MANUAL_IGNORED_EXPOSURE_FIELDS, "formula": "original_bias + physical_camera_ev100 - ev100"}


def describe_ignored_manual_exposure(settings):
    return {"mode": "manual_ignored", "auto_exposure_bias": float(settings.get_editor_property("auto_exposure_bias")), "ignored_by_engine": MANUAL_IGNORED_EXPOSURE_FIELDS,
            "opt_in": {"exposure_manual": EXPOSURE_MANUAL_BIAS}}


def apply_exposure(world, ev100, manual_mode=EXPOSURE_MANUAL_IGNORE):
    applied = []
    for volume in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.PostProcessVolume):
        settings = volume.get_editor_property("settings")
        settings.set_editor_property("override_auto_exposure_min_brightness", True)
        settings.set_editor_property("override_auto_exposure_max_brightness", True)
        settings.set_editor_property("auto_exposure_min_brightness", ev100)
        settings.set_editor_property("auto_exposure_max_brightness", ev100)
        entry = {"label": volume.get_actor_label(), "ev100": ev100, "mode": "auto", "fields": {"auto_exposure_min_brightness": ev100, "auto_exposure_max_brightness": ev100}}
        if is_manual_exposure(settings):
            entry.update(apply_manual_exposure(volume, settings, ev100) if manual_mode == EXPOSURE_MANUAL_BIAS else describe_ignored_manual_exposure(settings))
        volume.set_editor_property("settings", settings)
        applied.append(entry)
    return applied


def apply_component_values(world, mapping, component_property, apply_values):
    outcome = {}
    for label, values in mapping.items():
        actor = find_actor_by_label(world, label)
        if actor is None:
            outcome[label] = {"error": "액터 없음"}
            continue
        component = actor.get_editor_property(component_property)
        outcome[label] = apply_values(component, values)
    return outcome


def set_properties(component, values):
    for name, value in values.items():
        component.set_editor_property(name, value)
    return values


def is_flame_card_material(material):
    while material is not None:
        if "FluidFlame" in material.get_name():
            return True
        material = material.get_editor_property("parent") if isinstance(material, unreal.MaterialInstance) else None
    return False


def set_mesh_material_values(component, values):
    if component is None:
        return {"error": "컴포넌트 없음"}
    return set_material_values(component.get_material(0), values)


def has_material_in_chain(material, token):
    while material is not None:
        if token in material.get_name():
            return True
        material = material.get_editor_property("parent") if isinstance(material, unreal.MaterialInstance) else None
    return False


def set_ember_material_values(component, values):
    if component is None:
        return {"error": "컴포넌트 없음"}
    material = component.get_material(0)
    if not has_material_in_chain(material, "FluidEmber"):
        return {"error": "불티 재질 미로드: " + str(material)}
    if not isinstance(material, unreal.MaterialInstanceDynamic):
        material = component.create_dynamic_material_instance(0, material)
    applied = set_material_values(material, values)
    if "error" in applied:
        return applied
    base = material.get_base_material()
    known = {str(name) for name in unreal.MaterialEditingLibrary.get_scalar_parameter_names(base)} | {str(name) for name in unreal.MaterialEditingLibrary.get_vector_parameter_names(base)}
    return {"applied": applied, "mid": material.get_name(), "missing": [name for name in values if name not in known]}


def describe_ember_materials(world):
    names = {}
    for actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.TDFluidFireActor):
        component = actor.get_editor_property("ember_component")
        if component is not None and component.get_material(0) is not None:
            names[actor.get_actor_label()] = component.get_material(0).get_name()
    return names


def make_rotator(value):
    padded = list(value) + [0.0] * (3 - len(value))
    return unreal.Rotator(roll=padded[2], pitch=padded[0], yaw=padded[1])


def make_transform(current, value):
    unknown = set(value) - TRANSFORM_KEYS
    if unknown:
        raise ValueError("Transform 키는 location·rotation·scale 뿐이다: " + ", ".join(sorted(unknown)))
    location = unreal.Vector(*value["location"]) if "location" in value else current.translation
    rotation = make_rotator(value["rotation"]) if "rotation" in value else current.rotation.rotator()
    scale = unreal.Vector(*value["scale"]) if "scale" in value else current.scale3d
    return unreal.Transform(location=location, rotation=rotation, scale=scale)


def convert_list(current, value):
    if isinstance(current, unreal.Vector):
        return unreal.Vector(*value)
    if isinstance(current, unreal.Vector2D):
        return unreal.Vector2D(*value)
    if isinstance(current, unreal.Rotator):
        return make_rotator(value)
    if isinstance(current, unreal.LinearColor):
        return unreal.LinearColor(*(list(value) + [1.0] * (4 - len(value))))
    return value


def duplicate_array_element(current, index):
    element = current[index]
    if isinstance(element, unreal.Object):
        raise ValueError("UObject 배열 원소는 복제할 수 없다")
    return element.copy() if isinstance(element, unreal.StructBase) else element


def resolve_value(current, value):
    if isinstance(value, list):
        return convert_list(current, value)
    if not isinstance(value, dict):
        return value
    if isinstance(current, unreal.Transform):
        return make_transform(current, value)
    if isinstance(current, unreal.Array):
        appended = [(duplicate_array_element(current, int(key[len(APPEND_KEY_PREFIX):])), item_value) for key, item_value in value.items() if key.startswith(APPEND_KEY_PREFIX)]
        for key, item_value in value.items():
            if key.startswith(APPEND_KEY_PREFIX):
                continue
            current[int(key)] = resolve_value(current[int(key)], item_value)
        for element, item_value in appended:
            current.append(resolve_value(element, item_value))
        return current
    if current is None:
        raise ValueError("현재 값이 None이라 필드를 고칠 수 없다")
    for field, field_value in value.items():
        assign_field(current, field, field_value)
    return current


def assign_field(owner, name, value):
    current = owner.get_editor_property(name)
    resolved = resolve_value(current, value)
    if isinstance(current, unreal.Object) and isinstance(value, dict):
        return
    owner.set_editor_property(name, resolved)


def apply_each_label(world, apply, outcome, key, handler, *extra):
    if key not in apply:
        return
    results = outcome.setdefault(key, {})
    for label, values in apply[key].items():
        try:
            results[label] = handler(world, label, values, *extra)
        except Exception as error:
            results[label] = {"error": str(error)[-300:]}


def apply_props(world, label, properties):
    actor = find_actor_by_label(world, label)
    if actor is None:
        return "액터 없음"
    for name, value in properties.items():
        assign_field(actor, name, value)
    return list(properties)


def apply_location(world, label, coordinates):
    actor = find_actor_by_label(world, label)
    if actor is None:
        return "액터 없음"
    current = actor.get_actor_location()
    target = [old if new is None else new for old, new in zip((current.x, current.y, current.z), coordinates)]
    actor.set_actor_location(unreal.Vector(*target), False, True)
    location = actor.get_actor_location()
    return [location.x, location.y, location.z]


def apply_hidden_components(world, label, name_parts):
    actor = unreal.GameplayStatics.get_player_pawn(world, 0) if label == "pawn" else find_actor_by_label(world, label)
    if actor is None:
        return "액터 없음"
    lowered_parts = [part.lower() for part in name_parts]
    hidden = []
    for component in actor.get_components_by_class(unreal.SceneComponent):
        if any(part in component.get_name().lower() for part in lowered_parts):
            component.set_visibility(False)
            hidden.append(component.get_name())
    return hidden


def apply_sim(world, label, values):
    actor = find_actor_by_label(world, label)
    if actor is None:
        return "액터 없음"
    component = actor.get_editor_property("live_component")
    config = component.get_editor_property("simulation_config")
    for name, value in values.items():
        assign_field(config, name, value)
    component.apply_simulation_config(config)
    return values


def normalized_enum_name(value):
    return str(getattr(value, "name", value)).split(".")[-1].replace("_", "").upper()


def uses_own_preset_copies(preset):
    return normalized_enum_name(preset.get_editor_property("preset_kind")) == CUSTOM_KIND_NAME or bool(preset.get_editor_property("override_preset_defaults"))


def ignored_copy_error(source, ignored_paths):
    return {"error": "R2: 비 Custom·비 override 프리셋이면 이 사본 값은 무시되고 조절 표 값이 쓰인다(force_override true 면 임시 복제의 override 를 켠다)",
            "ignored_copy_paths": ignored_paths, "source": source.get_path_name()}


def apply_emitter(world, label, values):
    actor = find_actor_by_label(world, label)
    if actor is None:
        return "액터 없음"
    values = dict(values)
    force_override = bool(values.pop("force_override", False))
    live = actor.get_editor_property("live_component")
    preset = live.get_editor_property("active_preset")
    is_forced = False
    if preset is not None and not uses_own_preset_copies(preset):
        if not force_override:
            return ignored_copy_error(preset, sorted("source_emitter." + name for name in values))
        clone, _ = clone_preset(preset)
        clone.set_editor_property("override_preset_defaults", True)
        live.set_editor_property("active_preset", clone)
        is_forced = True
    emitter = actor.get_editor_property("source_emitter")
    for name, value in values.items():
        assign_field(emitter, name, value)
    actor.set_editor_property("source_emitter", emitter)
    return dict(values, forced_override=True) if is_forced else values


def flatten_paths(values, prefix=""):
    paths = {}
    for name, value in values.items():
        if isinstance(value, dict):
            paths.update(flatten_paths(value, prefix + name + "."))
        else:
            paths[prefix + name] = value
    return paths


def read_path(owner, path):
    for part in path.split("."):
        owner = owner.get_editor_property(part)
    return owner


def assign_path(owner, parts, value):
    if len(parts) == 1:
        assign_field(owner, parts[0], value)
        return
    child = owner.get_editor_property(parts[0])
    assign_path(child, parts[1:], value)
    if not isinstance(child, unreal.Object):
        owner.set_editor_property(parts[0], child)


def clone_preset(source):
    clone = unreal.new_object(type(source))
    discovered = {name for name, member in inspect.getmembers(type(source)) if inspect.isgetsetdescriptor(member) and not name.startswith("_")}
    failures = {}
    for name in sorted(set(PRESET_FIELD_NAMES) | discovered):
        try:
            clone.set_editor_property(name, source.get_editor_property(name))
        except Exception as error:
            failures[name] = str(error)[-160:]
    return clone, failures


def surface_location(actor):
    surface = actor.get_editor_property("internal_surface_mesh")
    if surface is None:
        return None
    location = surface.get_world_location()
    return [location.x, location.y, location.z]


def volume_expectations(paths):
    expectations = {}
    for path, value in paths.items():
        if isinstance(value, bool) or not isinstance(value, (int, float)):
            continue
        if path == "volume_height_cm":
            expectations[path] = ["VolumeHeightCm", float(value)]
        elif path.startswith("volume_render.") and path.count(".") == 1:
            leaf = path.split(".")[1]
            expectations[path] = [VOLUME_PARAMETER_NAMES.get(leaf, "".join(part.capitalize() for part in leaf.split("_"))), float(value)]
    return expectations


def apply_preset(world, label, edits, reinitialized_labels):
    actor = find_actor_by_label(world, label)
    if actor is None:
        return "액터 없음"
    live = actor.get_editor_property("live_component")
    source = live.get_editor_property("active_preset")
    if source is None:
        return {"error": "활성 프리셋 없음"}
    edits = dict(edits)
    force_override = bool(edits.pop("force_override", False))
    paths = flatten_paths(edits)
    previous = {path: read_path(source, path) for path in paths}
    clone, copy_failures = clone_preset(source)
    for path, value in paths.items():
        assign_path(clone, path.split("."), value)
    ignored_paths = sorted(path for path in paths if path.split(".")[0] in R2_COPY_FIELDS)
    is_forced = False
    if ignored_paths and not uses_own_preset_copies(clone):
        if not force_override:
            return ignored_copy_error(source, ignored_paths)
        clone.set_editor_property("override_preset_defaults", True)
        is_forced = True
    bias_path = "plane_settings.camera_depth_bias_cm"
    check = {"clone": clone.get_name(), "volume": volume_expectations(paths), "surface_location": surface_location(actor),
             "depth_bias_shift_cm": abs(float(paths[bias_path]) - float(previous[bias_path])) if bias_path in paths else None}
    live.set_editor_property("active_preset", clone)
    should_reinitialize = label in reinitialized_labels
    if should_reinitialize:
        live.init_system_state_from_preset(clone)
    pending = getattr(unreal, PRESET_CHECK_ATTRIBUTE, None) or {}
    pending[label] = check
    setattr(unreal, PRESET_CHECK_ATTRIBUTE, pending)
    active = live.get_editor_property("active_preset")
    return {"source": source.get_path_name(), "clone": clone.get_name(), "assigned": active is not None and active.get_name() == clone.get_name(),
            "values": {path: {"before": previous[path], "after": read_path(clone, path)} for path in paths},
            "reinitialized": should_reinitialize, "copy_failures": copy_failures, "forced_override": is_forced}


def measure_preset_effect(actor, check):
    active = actor.get_editor_property("live_component").get_editor_property("active_preset")
    effect = {"active_is_clone": active is not None and active.get_name() == check["clone"]}
    volume = actor.get_editor_property("volume_mesh_component") if check["volume"] else None
    material = volume.get_material(0) if volume is not None else None
    names = {str(name) for name in unreal.MaterialEditingLibrary.get_scalar_parameter_names(material)} if material is not None else set()
    missing_reason = "볼륨 메시 재질 없음" if material is None else "볼륨 재질에 이 매개변수가 없다"
    for path, (parameter, expected) in check["volume"].items():
        if parameter not in names:
            effect[path] = {"parameter": parameter, "took_effect": None, "reason": missing_reason}
            continue
        actual = material.get_scalar_parameter_value(parameter)
        effect[path] = {"parameter": parameter, "expected": expected, "actual": actual, "took_effect": abs(actual - expected) <= 0.001 * max(1.0, abs(expected))}
    expected_shift = check["depth_bias_shift_cm"]
    if expected_shift is not None:
        before, after = check["surface_location"], surface_location(actor)
        shift = math.dist(before, after) if before is not None and after is not None else None
        effect["plane_settings.camera_depth_bias_cm"] = {"expected_shift_cm": expected_shift, "surface_shift_cm": shift,
                                                         "took_effect": None if shift is None else abs(shift - expected_shift) <= max(1.0, 0.25 * expected_shift)}
    return effect


def report_preset_effects(world):
    pending = getattr(unreal, PRESET_CHECK_ATTRIBUTE, None) or {}
    setattr(unreal, PRESET_CHECK_ATTRIBUTE, {})
    report = {}
    for label, check in pending.items():
        actor = find_actor_by_label(world, label)
        try:
            report[label] = measure_preset_effect(actor, check) if actor is not None else {"error": "액터 없음"}
        except Exception as error:
            report[label] = {"error": str(error)[-300:]}
    return report


def run_console_commands(world, commands):
    controller = unreal.GameplayStatics.get_player_controller(world, 0)
    executed = []
    for command in commands:
        unreal.SystemLibrary.execute_console_command(world, command, controller)
        parts = command.split()
        if len(parts) == 2 and "." in parts[0]:
            executed.append({"command": command, "read_back": unreal.SystemLibrary.get_console_variable_float_value(parts[0])})
        else:
            executed.append({"command": command})
    return executed


def set_console_float(name, value):
    world = game_world() or unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    unreal.SystemLibrary.execute_console_command(world, f"{name} {value:g}", None)
    return unreal.SystemLibrary.get_console_variable_float_value(name)


def enable_fixed_step(state, fps):
    if fps <= 0.0:
        return {"requested_fps": 0.0}
    state["fixed_step_before"] = unreal.SystemLibrary.get_console_variable_float_value(FIXED_STEP_VARIABLE)
    return {"requested_fps": fps, "before": state["fixed_step_before"], "applied_fps": set_console_float(FIXED_STEP_VARIABLE, fps)}


def restore_fixed_step(state):
    before = state.get("fixed_step_before")
    if before is None:
        return None
    state["fixed_step_before"] = None
    return set_console_float(FIXED_STEP_VARIABLE, before)


def write_schedule_status(state, attempts=1):
    for _ in range(attempts):
        try:
            with open(state["status_path"], "w", encoding="utf-8") as status_file:
                json.dump(state["status"], status_file, ensure_ascii=False, default=str)
            return True
        except OSError:
            time.sleep(0.05)
    return False


def finish_schedule(state, error):
    if getattr(unreal, SCHEDULE_ATTRIBUTE, None) is state:
        setattr(unreal, SCHEDULE_ATTRIBUTE, None)
    if state.get("handle") is not None:
        unreal.unregister_slate_post_tick_callback(state["handle"])
        state["handle"] = None
    status = state["status"]
    try:
        status["fixed_step"]["restored_fps"] = restore_fixed_step(state)
        world = game_world()
        status["preset_effect"] = report_preset_effects(world) if world is not None else {}
    except Exception as cleanup_error:
        status["cleanup_error"] = str(cleanup_error)[-300:]
    status["error"] = error
    status["done"] = True
    write_schedule_status(state, 5)
    return status


def stop_schedule(reason):
    state = getattr(unreal, SCHEDULE_ATTRIBUTE, None)
    if state is None:
        return None
    return finish_schedule(state, reason)


def issue_scheduled_shot(state, world, elapsed, real_time, delta_seconds):
    target = state["remaining"].pop(0)
    path = f"{state['output']}/{state['tag']}_{state['prefix']}_{round(target * 1000):05d}ms.png"
    controller = unreal.GameplayStatics.get_player_controller(world, 0)
    unreal.SystemLibrary.execute_console_command(world, "HighResShot " + state["size"] + " filename=" + path, controller)
    state["pending"] = {"time": target, "path": path, "issued_game_time": elapsed, "late_seconds": elapsed - target, "issued_real_time": real_time,
                        "issued_tick": state["ticks"], "real_dt": delta_seconds,
                        "real_dt_mean": state["real_dt_sum"] / state["real_dt_count"] if state["real_dt_count"] else None,
                        "world_dt": unreal.GameplayStatics.get_world_delta_seconds(world), "captured_game_time": None, "saved": False}
    state["real_dt_sum"], state["real_dt_count"] = 0.0, 0
    state["status"]["shots"].append(state["pending"])
    write_schedule_status(state)


def settle_pending_shot(state, pending, elapsed, real_time, delta_seconds):
    if pending["captured_game_time"] is None:
        if real_time <= pending["issued_real_time"]:
            return
        pending["captured_game_time"] = elapsed
        pending["capture_real_dt"] = delta_seconds
    waited_ticks = state["ticks"] - pending["issued_tick"]
    is_saved = os.path.exists(pending["path"]) and os.path.getsize(pending["path"]) > 0
    if not is_saved and waited_ticks < state["file_wait_ticks"]:
        return
    pending["saved"] = is_saved
    pending["wait_ticks"] = waited_ticks
    state["pending"] = None
    write_schedule_status(state)


def advance_schedule(state, slate_delta_seconds):
    world = game_world()
    if world is None:
        finish_schedule(state, "PIE 게임 월드가 사라졌다")
        return
    if time.monotonic() > state["deadline"]:
        finish_schedule(state, "에디터 쪽 시간 초과")
        return
    wall_time = time.perf_counter()
    wall_delta_seconds = wall_time - state["last_wall_time"] if state["last_wall_time"] is not None else slate_delta_seconds
    state["last_wall_time"] = wall_time
    state["ticks"] += 1
    elapsed = unreal.GameplayStatics.get_time_seconds(world) - state["reference"]
    real_time = unreal.GameplayStatics.get_real_time_seconds(world)
    while state["events"] and elapsed >= float(state["events"][0]["time"]):
        run_schedule_event(state, world, state["events"].pop(0), elapsed)
    if state["pending"] is not None:
        settle_pending_shot(state, state["pending"], elapsed, real_time, wall_delta_seconds)
        return
    if not state["remaining"] and not state["events"]:
        finish_schedule(state, None)
        return
    if not state["remaining"]:
        return
    state["real_dt_sum"] += wall_delta_seconds
    state["real_dt_count"] += 1
    if elapsed >= state["remaining"][0]:
        issue_scheduled_shot(state, world, elapsed, real_time, wall_delta_seconds)


def start_schedule(schedule, reference_time):
    stop_schedule("새 예약이 이전 예약을 대체했다")
    times = sorted(float(value) for value in schedule.get("times", []))
    events = sorted(schedule.get("events", []), key=lambda event: float(event["time"]))
    prefix = schedule.get("prefix", "schedule")
    timeout = float(schedule.get("timeout", 120.0 + 10.0 * max(times + [float(event["time"]) for event in events] + [0.0])))
    state = {"remaining": list(times), "events": events, "pending": None, "ticks": 0, "real_dt_sum": 0.0, "real_dt_count": 0, "last_wall_time": None, "handle": None,
             "file_wait_ticks": int(schedule.get("file_wait_ticks", DEFAULT_SCHEDULE_FILE_WAIT_TICKS)), "deadline": time.monotonic() + timeout,
             "reference": reference_time, "size": PARAMS["size"], "output": PARAMS["output"], "tag": PARAMS["tag"], "prefix": prefix,
             "status_path": f"{PARAMS['output']}/{PARAMS['tag']}_{prefix}_schedule.json",
             "status": {"times": times, "reference": schedule.get("reference", "now"), "reference_game_time": reference_time, "prefix": prefix, "shots": [], "events_done": [], "done": False}}
    state["status"]["fixed_step"] = enable_fixed_step(state, float(schedule.get("fixed_fps", DEFAULT_SCHEDULE_FIXED_FPS)))
    write_schedule_status(state)

    def tick(delta_seconds):
        try:
            advance_schedule(state, delta_seconds)
        except Exception:
            finish_schedule(state, traceback.format_exc()[-600:])

    state["handle"] = unreal.register_slate_post_tick_callback(tick)
    setattr(unreal, SCHEDULE_ATTRIBUTE, state)
    return {"status_path": state["status_path"], "times": times, "reference_game_time": reference_time, "timeout_seconds": timeout, "fixed_step": state["status"]["fixed_step"]}


def begin_schedule(world, schedule, outcome):
    reference = schedule.get("reference", "now")
    if reference == "now":
        return start_schedule(schedule, unreal.GameplayStatics.get_time_seconds(world))
    if reference == "call":
        if "call_game_time" not in outcome:
            return {"error": "reference call은 같은 apply에서 call이 하나 이상 성공해야 한다"}
        return start_schedule(schedule, outcome["call_game_time"])
    if reference != "trigger":
        return {"error": "reference는 trigger·now·call: " + str(reference)}
    trigger = outcome.get("trigger")
    if not isinstance(trigger, dict) or "error" in trigger or "trigger_game_time" not in outcome:
        return {"error": "reference trigger는 같은 apply에서 trigger가 성공해야 한다"}
    return start_schedule(schedule, outcome["trigger_game_time"])


def call_function(world, label, request):
    actor = find_actor_by_label(world, label)
    if actor is None:
        return {"error": "액터 없음"}
    owner = actor.get_editor_property(request["component"]) if request.get("component") else actor
    function = getattr(owner, request["function"], None)
    if not callable(function):
        return {"error": "함수 없음: " + request["function"]}
    returned = function(*request.get("args", []))
    return {"function": request["function"], "returned": describe_read_value(returned), "game_time": unreal.GameplayStatics.get_time_seconds(world)}


def apply_calls(world, calls, outcome):
    results = {}
    for label, request in calls.items():
        try:
            results[label] = call_function(world, label, request)
        except Exception as error:
            results[label] = {"error": str(error)[-300:]}
        if "error" not in results[label] and "call_game_time" not in outcome:
            outcome["call_game_time"] = results[label]["game_time"]
    return results


def run_schedule_event(state, world, event, elapsed):
    done = {"time": float(event["time"]), "game_time": elapsed}
    if "console" in event:
        done["console"] = run_console_commands(world, event["console"])
    if "call" in event:
        done["call"] = apply_calls(world, event["call"], {})
    if "move" in event:
        done["move"] = begin_pawn_walk(world, event["move"], f"{state['output']}/{state['tag']}_event{round(float(event['time']) * 1000):05d}ms")
    state["status"]["events_done"].append(done)
    write_schedule_status(state)


def finish_pawn_walk(state, error):
    if getattr(unreal, WALK_ATTRIBUTE, None) is state:
        setattr(unreal, WALK_ATTRIBUTE, None)
    if state.get("handle") is not None:
        unreal.unregister_slate_post_tick_callback(state["handle"])
        state["handle"] = None
    if error:
        state["status"]["error"] = error
    state["status"]["done"] = True
    write_schedule_status(state, 5)
    return state["status"]


def stop_pawn_walk(reason):
    state = getattr(unreal, WALK_ATTRIBUTE, None)
    if state is None:
        return None
    return finish_pawn_walk(state, reason)


def advance_pawn_walk(state):
    world = game_world()
    if world is None:
        finish_pawn_walk(state, "PIE 게임 월드가 사라졌다")
        return
    pawn = unreal.GameplayStatics.get_player_pawn(world, 0)
    if pawn is None:
        finish_pawn_walk(state, "플레이어 폰이 사라졌다")
        return
    status = state["status"]
    location = pawn.get_actor_location()
    status["moved_cm"] = math.hypot(location.x - status["start"][0], location.y - status["start"][1])
    status["elapsed_game_seconds"] = unreal.GameplayStatics.get_time_seconds(world) - status["start_game_time"]
    if status["moved_cm"] >= status["distance_cm"]:
        finish_pawn_walk(state, None)
        return
    if status["elapsed_game_seconds"] > status["timeout_seconds"] or time.monotonic() > state["deadline"]:
        status["timed_out"] = True
        finish_pawn_walk(state, f"시한 초과: {status['timeout_seconds']:.2f} 게임 초 안에 {status['moved_cm']:.0f}/{status['distance_cm']:.0f} cm")
        return
    pawn.add_movement_input(state["direction"], 1.0, True)


def move_to_goal(world, goal):
    PARAMS["start_x"] = None
    PARAMS["goal_x"], PARAMS["goal_y"] = goal
    return action_fire_move(world)


def begin_pawn_walk(world, move, status_stem):
    if isinstance(move, list) and len(move) == 2:
        return move_to_goal(world, move)
    distance = move.get("distance_cm") if isinstance(move, dict) else None
    if isinstance(distance, bool) or not isinstance(distance, (int, float)) or distance <= 0:
        return {"error": "move 는 [goal_x, goal_y] 또는 {distance_cm > 0, yaw?}: " + json.dumps(move)}
    pawn, controller = player_pawn_and_controller(world)
    if pawn is None or controller is None:
        return {"error": "플레이어 폰·컨트롤러 없음"}
    if not hasattr(pawn, "get_max_move_speed") or not hasattr(pawn, "add_movement_input"):
        return {"error": "폰에 get_max_move_speed·add_movement_input 이 없다"}
    max_speed = float(pawn.get_max_move_speed())
    if max_speed <= 0.0:
        return {"error": f"최대 속도 {max_speed:g} 로는 걸을 수 없다"}
    yaw = float(move["yaw"]) if move.get("yaw") is not None else float(controller.get_control_rotation().yaw)
    start = pawn.get_actor_location()
    direction = (math.cos(math.radians(yaw)), math.sin(math.radians(yaw)))
    timeout = 2.0 * float(distance) / max_speed + WALK_TIMEOUT_MARGIN_SECONDS
    wall_timeout = timeout / max(unreal.GameplayStatics.get_global_time_dilation(world), MINIMUM_WALL_TIME_DILATION) + WALK_WALL_MARGIN_SECONDS
    stop_pawn_walk("새 걷기가 이전 걷기를 대체했다")
    status = {"mode": "input", "yaw": yaw, "distance_cm": float(distance), "start": [start.x, start.y], "goal": [start.x + direction[0] * distance, start.y + direction[1] * distance],
              "max_speed": max_speed, "timeout_seconds": timeout, "wall_timeout_seconds": wall_timeout, "start_game_time": unreal.GameplayStatics.get_time_seconds(world),
              "moved_cm": 0.0, "elapsed_game_seconds": 0.0, "timed_out": False, "done": False, "status_path": f"{status_stem}_walk.json"}
    state = {"status": status, "status_path": status["status_path"], "direction": unreal.Vector(direction[0], direction[1], 0.0), "handle": None,
             "deadline": time.monotonic() + wall_timeout}
    write_schedule_status(state)

    def tick(delta_seconds):
        try:
            advance_pawn_walk(state)
        except Exception:
            finish_pawn_walk(state, traceback.format_exc()[-600:])

    state["handle"] = unreal.register_slate_post_tick_callback(tick)
    setattr(unreal, WALK_ATTRIBUTE, state)
    return dict(status)


def reset_fluid_state(world):
    actors, errors, component_count = [], [], 0
    for actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor):
        components = actor.get_components_by_class(unreal.TDFluidLiveComponent)
        if not components:
            continue
        try:
            for component in components:
                component.reset_simulation_state()
        except Exception as error:
            errors.append(f"{actor.get_actor_label()}: {str(error)[-200:]}")
            continue
        component_count += len(components)
        actors.append(actor.get_actor_label())
    return {"actors": actors, "component_count": component_count, "errors": errors, "game_time": unreal.GameplayStatics.get_time_seconds(world)}


def restart_cloud_flow_clocks(world):
    cloud_actors = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.TDFluidCloudLayersActor)
    if not cloud_actors:
        return None
    actors, errors = [], []
    for actor in cloud_actors:
        try:
            actor.restart_cloud_flow_clock()
        except Exception as error:
            errors.append(f"{actor.get_actor_label()}: {str(error)[-200:]}")
            continue
        actors.append(actor.get_actor_label())
    return {"actors": actors, "errors": errors, "game_time": unreal.GameplayStatics.get_time_seconds(world)}


def apply_view_target(world, label):
    actor = find_actor_by_label(world, label)
    if actor is None:
        return {"label": label, "ok": False, "error": "액터 없음"}
    unreal.GameplayStatics.get_player_controller(world, 0).set_view_target_with_blend(actor, 0.0)
    return {"label": label, "ok": True}


def material_package(material):
    return str(material.get_path_name()).split(".")[0] if material is not None else ""


def is_cpp_look_owner(actor):
    for name in CPP_LOOK_OWNER_CLASS_NAMES:
        owner_class = getattr(unreal, name, None)
        if isinstance(owner_class, type) and isinstance(actor, owner_class):
            return True
    for name in CPP_LOOK_OWNER_COMPONENT_CLASS_NAMES:
        component_class = getattr(unreal, name, None)
        if isinstance(component_class, type) and len(actor.get_components_by_class(component_class)) > 0:
            return True
    return False


def match_look_material(material, package):
    if material is None:
        return None
    if isinstance(material, unreal.MaterialInstanceDynamic):
        return "mid" if material_package(material.get_editor_property("parent")) == package else None
    return "asset" if material_package(material) == package else None


def look_holders(actor):
    holders = []
    for component in actor.get_components_by_class(unreal.PrimitiveComponent):
        holders += [{"holder": "slot", "component": component, "slot": slot, "material": component.get_material(slot)} for slot in range(component.get_num_materials())]
    for component in actor.get_components_by_class(unreal.DecalComponent):
        holders.append({"holder": "decal", "component": component, "slot": 0, "material": component.get_decal_material()})
    for component in actor.get_components_by_class(unreal.VolumetricCloudComponent):
        holders.append({"holder": "cloud", "component": component, "slot": 0, "material": component.get_editor_property("material")})
    if isinstance(actor, unreal.TDFluidFoliageFlowActor):
        getter = getattr(actor, FOLIAGE_FLOW_GETTER, None)
        holders.append({"holder": "foliage", "component": None, "slot": 0, "material": getter() if callable(getter) else None,
                        "error": None if callable(getter) else f"{MISSING_GETTER_ERROR} ({FOLIAGE_FLOW_GETTER})"})
    if isinstance(actor, unreal.PostProcessVolume):
        blendables = actor.get_editor_property("settings").get_editor_property("weighted_blendables").get_editor_property("array")
        holders += [{"holder": "post_process", "component": None, "slot": index, "material": blendable.get_editor_property("object")} for index, blendable in enumerate(blendables)]
    return holders


def find_look_owners(world, package):
    owners = []
    for actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor):
        is_cpp_actor = is_cpp_look_owner(actor)
        for holder in look_holders(actor):
            match = match_look_material(holder["material"], package)
            if match is None and not holder.get("error"):
                continue
            is_cpp = holder["holder"] in CPP_ONLY_LOOK_HOLDERS or (match == "mid" and is_cpp_actor and str(holder["material"].get_path_name()) not in python_mid_names())
            owners.append(dict(holder, package=package, actor=actor.get_actor_label(), by="cpp" if is_cpp else "python", owner=LOOK_HOLDER_OWNERS[holder["holder"]][1 if is_cpp else 0]))
    return owners


def describe_look_owner(owner):
    component = owner["component"]
    described = {"package": owner["package"], "owner": owner["owner"], "by": owner["by"], "actor": owner["actor"], "component": component.get_name() if component is not None else None, "slot": owner["slot"]}
    if owner.get("error"):
        described["error"] = owner["error"]
    return described


def python_mid_names():
    names = getattr(unreal, PYTHON_MID_ATTRIBUTE, None)
    if names is None:
        names = set()
        setattr(unreal, PYTHON_MID_ATTRIBUTE, names)
    return names


def make_python_mid(owner):
    material = owner["material"]
    if isinstance(material, unreal.MaterialInstanceDynamic):
        return material
    if owner["holder"] == "decal":
        created = owner["component"].create_dynamic_material_instance()
    else:
        created = owner["component"].create_dynamic_material_instance(owner["slot"], material)
    python_mid_names().add(str(created.get_path_name()))
    return created


def read_look_value(material, section, name):
    if isinstance(material, unreal.MaterialInstanceDynamic):
        value = material.get_scalar_parameter_value(name) if section == "Scalars" else material.get_vector_parameter_value(name)
    elif section == "Scalars":
        value = unreal.MaterialEditingLibrary.get_material_instance_scalar_parameter_value(material, name)
    else:
        value = unreal.MaterialEditingLibrary.get_material_instance_vector_parameter_value(material, name)
    return [value.r, value.g, value.b, value.a] if section == "Vectors" else float(value)


def look_value_matches(expected, value):
    expected_values = list(expected) + [1.0] * (4 - len(expected)) if isinstance(expected, list) else [expected]
    read_values = value if isinstance(value, list) else [value]
    return all(abs(float(want) - float(got)) <= LOOK_READ_BACK_TOLERANCE * max(1.0, abs(float(want))) for want, got in zip(expected_values, read_values))


def check_look_base(base):
    checked, mismatches = 0, []
    for package, sections in base.items():
        asset = unreal.load_asset(package)
        if asset is None:
            mismatches.append({"package": package, "error": "에셋 없음"})
            continue
        for section in LOOK_MATERIAL_SECTIONS:
            for name, expected in sections.get(section, {}).items():
                checked += 1
                value = read_look_value(asset, section, name)
                if not look_value_matches(expected, value):
                    mismatches.append({"package": package, "section": section, "name": name, "expected": expected, "asset": value})
    return {"checked": checked, "mismatches": mismatches}


def niagara_value(section, value):
    if section == "Floats":
        return "set_variable_float", float(value)
    if section == "Ints":
        return "set_variable_int", int(value)
    if section == "Bools":
        return "set_variable_bool", bool(value)
    if section == "Vectors2":
        return "set_variable_vec2", unreal.Vector2D(*value)
    if section == "Vectors3":
        return "set_variable_vec3", unreal.Vector(*value)
    return "set_variable_linear_color", unreal.LinearColor(*(list(value) + [1.0] * (4 - len(value))))


def find_niagara_components(world, package):
    found = []
    for actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor):
        found += [(actor, component) for component in actor.get_components_by_class(unreal.NiagaraComponent) if material_package(component.get_asset()) == package]
    return found


def apply_look_materials(world, package, sections, look, skips_without_owner=False):
    owners = find_look_owners(world, package)
    if not owners and skips_without_owner:
        look.setdefault("skipped_look", []).append(package)
        return
    values = dict(sections.get("Scalars", {}), **sections.get("Vectors", {}))
    look["look_errors"] += sorted({owner["error"] for owner in owners if owner.get("error")})
    usable = [owner for owner in owners if not owner.get("error")]
    for owner in usable:
        if owner["by"] == "python":
            set_material_values(make_python_mid(owner), values)
    look["look_applied"][package] = {name: len(usable) for name in values}
    look["look_owners"] += [describe_look_owner(owner) for owner in owners]
    if not usable:
        look["look_errors"].append("look 적용 대상 없음: " + package)


def apply_look_niagara(world, package, sections, is_reset, look, skips_without_owner=False):
    components = find_niagara_components(world, package)
    if not components and skips_without_owner:
        look.setdefault("skipped_look", []).append(package)
        return
    for actor, component in components:
        for section, values in sections.items():
            for name, value in values.items():
                setter, converted = niagara_value(section, value)
                getattr(component, setter)(name, converted)
        if is_reset:
            component.reinitialize_system()
    look["look_applied"][package] = {name: len(components) for values in sections.values() for name in values}
    look["look_owners"] += [{"package": package, "owner": "niagara", "by": "python", "actor": actor.get_actor_label(), "component": component.get_name(), "reset": bool(is_reset)} for actor, component in components]
    if not components:
        look["look_errors"].append("look 적용 대상 없음: " + package)


def apply_look_live(world, look_live):
    look = {"look_applied": {}, "look_owners": [], "look_errors": []}
    optional_packages = set(look_live.get("skip_if_no_owner") or [])
    if look_live.get("base"):
        look["look_base"] = check_look_base(look_live["base"])
        look["look_errors"] += [f"base 불일치: {item['package']} {item.get('section', '')}.{item.get('name', '')} 기대 {item.get('expected')} 원 MI {item.get('asset', item.get('error'))}" for item in look["look_base"]["mismatches"]]
    for package, sections in (look_live.get("materials") or {}).items():
        apply_look_materials(world, package, sections, look, package in optional_packages)
    for package, sections in (look_live.get("niagara") or {}).items():
        apply_look_niagara(world, package, sections, look_live.get("niagara_reset", False), look, package in optional_packages)
    return look


def action_lookdev_apply(world):
    apply = PARAMS["apply"]
    outcome = {}
    cloud_flow_restart = restart_cloud_flow_clocks(world)
    if cloud_flow_restart is not None:
        outcome["cloud_flow_restart"] = cloud_flow_restart
    if PARAMS.get("reset_state"):
        outcome["state_reset"] = reset_fluid_state(world)
    if "awake" in apply:
        outcome["awake"] = apply_awake(world, apply["awake"] if isinstance(apply["awake"], dict) else set(apply["awake"]))
    if "pawn" in apply:
        pawn, _ = teleport_pawn(world, apply["pawn"][0], apply["pawn"][1])
        if len(apply["pawn"]) > 2:
            pawn.set_actor_location(unreal.Vector(apply["pawn"][0], apply["pawn"][1], apply["pawn"][2]), False, True)
        outcome["pawn"] = apply["pawn"]
    if "arm" in apply:
        pawn, controller = player_pawn_and_controller(world)
        boom = pawn.get_component_by_class(unreal.SpringArmComponent)
        boom.set_editor_property("target_arm_length", apply["arm"])
        outcome["arm"] = boom.get_editor_property("target_arm_length")
    if "view" in apply:
        pawn, controller = player_pawn_and_controller(world)
        controller.set_control_rotation(unreal.Rotator(roll=0.0, pitch=apply["view"][0], yaw=apply["view"][1]))
        boom = pawn.get_component_by_class(unreal.SpringArmComponent)
        boom.set_editor_property("use_pawn_control_rotation", True)
        boom.set_editor_property("do_collision_test", False)
        outcome["view"] = [controller.get_control_rotation().pitch, controller.get_control_rotation().yaw]
    if "view_target" in apply:
        outcome["view_target"] = apply_view_target(world, apply["view_target"])
    apply_each_label(world, apply, outcome, "props", apply_props)
    for label, is_hidden in apply.get("hide", {}).items():
        actor = find_actor_by_label(world, label)
        if actor is None:
            outcome.setdefault("hide", {})[label] = "액터 없음"
            continue
        actor.set_actor_hidden_in_game(is_hidden)
        outcome.setdefault("hide", {})[label] = is_hidden
    apply_each_label(world, apply, outcome, "location", apply_location)
    apply_each_label(world, apply, outcome, "hide_components", apply_hidden_components)
    apply_each_label(world, apply, outcome, "preset", apply_preset, set(apply.get("preset_reinit", [])))
    if "exposure_ev100" in apply:
        outcome["exposure_applied"] = apply_exposure(world, apply["exposure_ev100"], apply.get("exposure_manual", EXPOSURE_MANUAL_IGNORE))
        outcome["exposure"] = [entry["label"] for entry in outcome["exposure_applied"]]
    for label, intensity in apply.get("lights", {}).items():
        actor = find_actor_by_label(world, label)
        if actor is None:
            outcome.setdefault("lights", {})[label] = "액터 없음"
            continue
        actor.get_component_by_class(unreal.LightComponentBase).set_intensity(intensity)
        outcome.setdefault("lights", {})[label] = intensity
    if "fire_light" in apply:
        outcome["fire_light"] = apply_component_values(world, apply["fire_light"], "fire_light_component", set_properties)
    for label, values in apply.get("flame", {}).items():
        actor = find_actor_by_label(world, label)
        if actor is None:
            outcome.setdefault("flame", {})[label] = {"error": "액터 없음"}
            continue
        cards = [component for component in actor.get_components_by_class(unreal.StaticMeshComponent) if is_flame_card_material(component.get_material(0))]
        for card in cards:
            set_material_values(card.get_material(0), values)
        outcome.setdefault("flame", {})[label] = {"cards": len(cards)}
    if "volume" in apply:
        outcome["volume"] = apply_component_values(world, apply["volume"], "volume_mesh_component", set_mesh_material_values)
    if "ember" in apply:
        outcome["ember"] = apply_component_values(world, apply["ember"], "ember_component", set_ember_material_values)
    apply_each_label(world, apply, outcome, "sim", apply_sim)
    apply_each_label(world, apply, outcome, "emitter", apply_emitter)
    for label, material_path in apply.get("surface_material", {}).items():
        actor = find_actor_by_label(world, label)
        if actor is None:
            outcome.setdefault("surface_material", {})[label] = "액터 없음"
            continue
        live = actor.get_editor_property("live_component")
        live.set_editor_property("output_material", unreal.load_asset(material_path))
        live.bind_internal_mesh_component(actor.get_editor_property("internal_surface_mesh"))
        outcome.setdefault("surface_material", {})[label] = actor.get_editor_property("internal_surface_mesh").get_material(0).get_name()
    if "console" in apply:
        outcome["console"] = run_console_commands(world, apply["console"])
    if "speed" in apply:
        pawn, controller = player_pawn_and_controller(world)
        pawn.set_max_move_speed(apply["speed"])
        outcome["speed"] = pawn.get_max_move_speed()
    if "move" in apply:
        outcome["move"] = begin_pawn_walk(world, apply["move"], f"{PARAMS['output']}/{PARAMS['tag']}_move")
    if "trigger" in apply:
        PARAMS["hit_dx"], PARAMS["hit_dy"], PARAMS["hit_dz"] = apply["trigger"]
        outcome["trigger"] = action_fire_trigger(world)
        outcome["trigger_game_time"] = unreal.GameplayStatics.get_time_seconds(world)
    if "call" in apply:
        outcome["call"] = apply_calls(world, apply["call"], outcome)
    if "time_dilation" in apply:
        unreal.GameplayStatics.set_global_time_dilation(world, apply["time_dilation"])
        outcome["time_dilation"] = unreal.GameplayStatics.get_global_time_dilation(world)
    if "look_live" in apply:
        outcome.update(apply_look_live(world, apply["look_live"]))
    if "schedule" in apply:
        try:
            outcome["schedule"] = begin_schedule(world, apply["schedule"], outcome)
        except Exception:
            outcome["schedule"] = {"error": traceback.format_exc()[-600:]}
    outcome["game_time"] = unreal.GameplayStatics.get_time_seconds(world)
    return outcome


def action_lookdev_shot(world):
    game_time = unreal.GameplayStatics.get_time_seconds(world)
    unreal.SystemLibrary.execute_console_command(world, "HighResShot " + PARAMS["size"] + " filename=" + PARAMS["path"])
    outcome = {"game_time": game_time}
    outcome["ember_mid"] = describe_ember_materials(world)
    effects = report_preset_effects(world)
    if effects:
        outcome["preset_effect"] = effects
    return outcome


def action_lookdev_schedule_stop(world):
    return stop_schedule("바깥 Python 대기 시간 초과") or {"stopped": False}


def action_lookdev_walk_stop(world):
    return stop_pawn_walk("바깥 Python 대기 시간 초과") or {"stopped": False}


def action_lookdev_tuning_load(world):
    subsystem = unreal.get_engine_subsystem(unreal.TDFluidPresetTuningSubsystem)
    error_count = int(subsystem.reload_preset_tuning_from_paths(PARAMS["tuning"], PARAMS["look"]))
    return {"error_count": error_count, "load_errors": [str(line) for line in subsystem.get_load_errors()] if error_count else [],
            "table_generation": subsystem.get_table_generation(), "tuning_source_path": subsystem.get_tuning_source_path(),
            "look_source_path": subsystem.get_look_source_path(), "is_using_override_paths": subsystem.is_using_override_paths()}


def action_lookdev_reapply_report(world):
    live = unreal.TDFluidLiveSubsystem.get_fluid_live_subsystem(world)
    if live is None:
        return {"error": "TDFluidLiveSubsystem 없음"}
    report = live.get_last_reapply_report()
    return {name: report.get_editor_property(name) for name in REAPPLY_REPORT_FIELDS}


def live_actors_with_presets(world):
    pairs = []
    for actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.TDFluidLiveActor):
        live = actor.get_editor_property("live_component")
        preset = live.get_editor_property("active_preset") if live is not None else None
        if preset is not None:
            pairs.append((actor, live, preset))
    return pairs


def action_lookdev_tuning_reinit(world):
    reinitialized = []
    for actor, live, preset in live_actors_with_presets(world):
        live.init_system_state_from_preset(preset)
        reinitialized.append(actor.get_actor_label())
    return {"reinitialized": reinitialized}


def python_attribute_for(owner, field_name):
    wanted = (field_name[1:] if len(field_name) > 1 and field_name[0] == "b" and field_name[1].isupper() else field_name).lower()
    for name in dir(owner):
        if not name.startswith("_") and name.replace("_", "").lower() == wanted:
            return name
    return None


def describe_read_value(value):
    if value is None or isinstance(value, (bool, int, float, str)):
        return value
    if isinstance(value, unreal.EnumBase):
        return str(value.name)
    if isinstance(value, unreal.Vector2D):
        return {"X": value.x, "Y": value.y}
    if isinstance(value, unreal.Vector):
        return {"X": value.x, "Y": value.y, "Z": value.z}
    if isinstance(value, unreal.LinearColor):
        return {"R": value.r, "G": value.g, "B": value.b, "A": value.a}
    return str(value)


def find_world_subsystem(world, subsystem_class):
    if world is None:
        return None
    return next((candidate for candidate in unreal.ObjectIterator(subsystem_class) if candidate.get_world() == world), None)


def find_subsystem(world, class_name):
    subsystem_class = getattr(unreal, class_name, None)
    if not isinstance(subsystem_class, type):
        return None
    for getter in (lambda: unreal.get_engine_subsystem(subsystem_class), lambda: find_world_subsystem(world, subsystem_class)):
        try:
            found = getter()
        except Exception:
            found = None
        if found is not None:
            return found
    return None


def resolve_read_back_owner(actor, owner_kind):
    if owner_kind == "live_component":
        return actor.get_editor_property("live_component")
    if owner_kind == "volume_driver":
        return actor.get_component_by_class(unreal.TDFluidVolumeDriverComponent)
    return actor


def read_owner_field(owner, struct_name, field_name, access):
    if owner is None:
        return {"error": "소유자 없음"}
    if access.startswith("call:"):
        getter_name = access[len("call:"):]
        getter = getattr(owner, getter_name, None)
        if not callable(getter):
            return {"error": f"{MISSING_GETTER_ERROR} ({getter_name})"}
        owner = getter()
    elif access:
        owner = owner.get_editor_property(access)
    attribute = python_attribute_for(owner, field_name)
    if attribute is None:
        return {"error": f"{struct_name}.{field_name}: 파이썬 속성 없음"}
    return {"value": describe_read_value(owner.get_editor_property(attribute))}


def read_effective_field(actor, request):
    if request.get("owner"):
        return read_owner_field(resolve_read_back_owner(actor, request["owner"]), request["struct"], request["field"], request.get("access", ""))
    component_name, property_name = READ_BACK_SOURCES[request["struct"]]
    owner = actor.get_editor_property(component_name) if component_name else actor
    return read_owner_field(owner, request["struct"], request["field"], property_name or "")


def safe_read(read):
    try:
        return read()
    except Exception as error:
        return {"error": str(error)[-200:]}


def read_class_owners(world, request, entry):
    actor_class = getattr(unreal, request["owner"][len("actor_class:"):], None)
    if not isinstance(actor_class, type):
        entry["error"] = "클래스 없음"
        return
    for actor in unreal.GameplayStatics.get_all_actors_of_class(world, actor_class):
        entry["actors"][actor.get_actor_label()] = safe_read(lambda: read_owner_field(actor, request["struct"], request["field"], request.get("access", "")))


def action_lookdev_tuning_read_back(world):
    pairs = live_actors_with_presets(world)
    results = []
    for request in PARAMS["requests"]:
        owner_kind = request.get("owner", "")
        entry = {"struct": request["struct"], "field": request["field"], "kinds": request.get("kinds", []), "actors": {}}
        if owner_kind:
            entry.update(owner=owner_kind, access=request.get("access", ""))
        results.append(entry)
        if owner_kind.startswith("subsystem:"):
            subsystem = find_subsystem(world, owner_kind[len("subsystem:"):])
            entry["actors"][owner_kind] = safe_read(lambda: read_owner_field(subsystem, request["struct"], request["field"], request.get("access", "")))
            continue
        if owner_kind.startswith("actor_class:"):
            read_class_owners(world, request, entry)
            continue
        if not owner_kind and request["struct"] not in READ_BACK_SOURCES:
            entry["error"] = "다시 읽기 미지원 구조체"
            continue
        wanted = {normalized_enum_name(kind) for kind in request.get("kinds", [])}
        for actor, live, preset in pairs:
            kind = normalized_enum_name(preset.get_editor_property("preset_kind"))
            if kind not in wanted:
                continue
            read = safe_read(lambda: read_effective_field(actor, request))
            entry["actors"][actor.get_actor_label()] = dict(read, kind=kind, uses_own_copies=uses_own_preset_copies(preset))
    return {"read_back": results}


def action_lookdev_look_read_back(world):
    look_live = PARAMS["look_live"]
    read_back, checks, errors = {}, [], []
    for package, sections in (look_live.get("materials") or {}).items():
        for owner in find_look_owners(world, package):
            if owner.get("error"):
                continue
            check = describe_look_owner(owner)
            mismatched = []
            for section in LOOK_MATERIAL_SECTIONS:
                for name, expected in sections.get(section, {}).items():
                    value = read_look_value(owner["material"], section, name)
                    entry = read_back.setdefault(package, {}).setdefault(section, {}).setdefault(name, {"expected": expected, "read": [], "matches": True})
                    entry["read"].append(value)
                    if not look_value_matches(expected, value):
                        entry["matches"] = False
                        mismatched.append(f"{section}.{name}")
            check["matches"] = not mismatched
            if mismatched:
                check["reason"] = CPP_LOOK_MISMATCH_REASON if owner["by"] == "cpp" else PYTHON_LOOK_MISMATCH_REASON
                errors.append(f"{check['reason']}: {package} {owner['actor']} {', '.join(mismatched)}")
            checks.append(check)
    return {"look_read_back": read_back, "owner_checks": checks, "look_errors": errors, "game_time": unreal.GameplayStatics.get_time_seconds(world)}


def probe_owner(world, entry):
    if entry.get("subsystem"):
        return find_subsystem(world, entry["subsystem"])
    actor = find_actor_by_label(world, entry["label"])
    if actor is None or not entry.get("component"):
        return actor
    return actor.get_editor_property(entry["component"])


def pascal_name(name):
    return "".join(part[:1].upper() + part[1:] for part in str(name).split("_"))


def flatten_probe_value(value, prefix, depth=0):
    described = describe_read_value(value)
    if isinstance(described, (bool, int, float)):
        return {prefix: described}
    if isinstance(described, dict):
        return {f"{prefix}.{key}": item for key, item in described.items()}
    if depth >= 2:
        return {}
    flat = {}
    for name in dir(value):
        if name.startswith("_"):
            continue
        try:
            item = getattr(value, name)
        except Exception:
            continue
        if not callable(item):
            flat.update(flatten_probe_value(item, f"{prefix}.{pascal_name(name)}", depth + 1))
    return flat


def read_probe_stats(world, entry):
    name = entry.get("label") or entry.get("subsystem")
    owner = probe_owner(world, entry)
    if owner is None:
        return {}, f"probe.stats {name}: 소유자 없음"
    function = getattr(owner, entry["function"], None)
    if not callable(function):
        return {}, f"probe.stats {name}.{entry['function']}: 함수 없음"
    return flatten_probe_value(function(), "probe." + entry["prefix"]), None


def grid_positions(start, end, step):
    count = max(1, int(round((end - start) / step)) + 1)
    return [min(end, start + index * step) for index in range(count)]


def channel_value(color, channel):
    if channel == "rg_len":
        return math.hypot(color.r, color.g)
    if channel == "rgb_len":
        return math.sqrt(color.r ** 2 + color.g ** 2 + color.b ** 2)
    return float(getattr(color, channel))


def radial_alignment(samples, center):
    alignments = []
    for u, v, x, y in samples:
        radius, length = math.hypot(u - center[0], v - center[1]), math.hypot(x, y)
        if radius > 1e-6 and length > 1e-6:
            alignments.append(((u - center[0]) * x + (v - center[1]) * y) / (radius * length))
    return sum(alignments) / len(alignments) if alignments else None


def probe_render_target(owner, entry):
    if owner is None:
        return None
    if entry.get("function"):
        return getattr(owner, entry["function"])()
    return owner.get_editor_property(entry["property"])


def is_texture_2d(target):
    texture_class = getattr(unreal, "Texture2D", None)
    return isinstance(texture_class, type) and isinstance(target, texture_class)


def read_probe_render_target(world, entry):
    owner = probe_owner(world, entry)
    source = f"probe.rt {entry['label']}.{entry.get('function') or entry.get('property')}"
    if owner is None:
        return {}, None, f"{source}: 렌더 타깃 없음(소유자 없음)"
    target = probe_render_target(owner, entry)
    if target is None and entry.get("function"):
        return {}, None, f"{source}: 렌더 타깃 없음(게터가 None)"
    if target is None:
        return {}, None, f"{source}: 렌더 타깃 없음(속성 None: TSoftObjectPtr 면 get_editor_property 의 동기 적재가 실패했거나 경로가 비었다)"
    if is_texture_2d(target):
        return {}, None, f"{source}: 렌더 타깃이 아닌 Texture2D(CPU 백엔드 출력이면 GPU 백엔드 필요)"
    step = float(entry["step"])
    us, vs = grid_positions(entry["box"][0], entry["box"][2], step), grid_positions(entry["box"][1], entry["box"][3], step)
    rows, samples = [], []
    for v in vs:
        colors = [unreal.RenderingLibrary.read_render_target_raw_uv(world, target, u, v) for u in us]
        rows.append([channel_value(color, entry["channel"]) for color in colors])
        samples += [(u, v, color.r, color.g) for u, color in zip(us, colors)]
    values = sorted(value for row in rows for value in row)
    prefix = "probe." + entry["prefix"]
    metrics = {prefix + ".mean": sum(values) / len(values), prefix + ".p50": values[int(round(0.5 * (len(values) - 1)))],
               prefix + ".p95": values[int(round(0.95 * (len(values) - 1)))], prefix + ".max": values[-1]}
    if entry.get("threshold") is not None:
        metrics[prefix + ".fraction_above"] = sum(1 for value in values if value > float(entry["threshold"])) / len(values)
    if entry.get("radial_center"):
        metrics[prefix + ".radial_alignment_mean"] = radial_alignment(samples, entry["radial_center"])
    return metrics, {"u": us, "v": vs, "values": rows}, None


def action_lookdev_probe(world):
    probe = PARAMS["probe"]
    elapsed = unreal.GameplayStatics.get_time_seconds(world) - PARAMS["reference"]
    if elapsed < PARAMS["at"]:
        return {"wait_seconds": PARAMS["at"] - elapsed}
    sample = {"time": PARAMS["at"], "elapsed": elapsed, "metrics": {}, "grids": {}, "errors": []}
    for entry in probe.get("stats", []):
        try:
            metrics, error = read_probe_stats(world, entry)
        except Exception as caught:
            metrics, error = {}, f"probe.stats {entry.get('function')}: {str(caught)[-200:]}"
        sample["metrics"].update(metrics)
        if error:
            sample["errors"].append(error)
    for entry in probe.get("rt", []):
        try:
            metrics, grid, error = read_probe_render_target(world, entry)
        except Exception as caught:
            metrics, grid, error = {}, None, f"probe.rt {entry.get('function') or entry.get('property')}: 렌더 타깃 읽기 실패 {str(caught)[-200:]}"
        sample["metrics"].update(metrics)
        if grid is not None:
            sample["grids"][entry["prefix"]] = grid
        if error:
            sample["errors"].append(error)
    return sample


def action_lookdev_wait_check(world):
    condition = PARAMS["wait_until"]
    actor = find_actor_by_label(world, condition["label"])
    if actor is None:
        return {"error": "액터 없음: " + condition["label"]}
    owner = actor.get_editor_property(condition["component"]) if condition.get("component") else actor
    attribute = python_attribute_for(owner, condition["property"]) or condition["property"]
    value = describe_read_value(owner.get_editor_property(attribute))
    return {"value": value, "matched": value == condition["equals"]}


def emit_lookdev_result(result):
    text = json.dumps(result, ensure_ascii=False, default=str)
    size = len(text.encode("utf-8"))
    path = PARAMS.get("result_path")
    if path and size > PARAMS.get("result_inline_limit", size):
        try:
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, "w", encoding="utf-8") as handle:
                handle.write(text)
            text = json.dumps({"result_file": path, "result_bytes": size})
        except OSError as caught:
            text = json.dumps({"error": "결과 파일 쓰기 실패 " + path + ": " + str(caught)[-200:]}, ensure_ascii=False)
    sys.stdout.write(RESULT_MARKER + " " + text + "\n")
    sys.stdout.flush()


def run():'''


def build_editor_code(action, fields, result_path):
    code = pie_showcase_fluid.EDITOR_CODE.replace("\ndef run():", LOOKDEV_ACTIONS, 1).replace(REMOTE_RESULT_PRINT, REMOTE_RESULT_EMIT, 1)
    params = {"action": action, **fields, "result_path": result_path.as_posix(), "result_inline_limit": REMOTE_RESULT_INLINE_LIMIT_BYTES}
    return code.replace("__PARAMS__", repr(json.dumps(params)))


def remote_output_text(line):
    if line.startswith("[") and "] " in line:
        return line.split("] ", 1)[1]
    return line


def parse_editor_result(lines):
    marker = pie_showcase_fluid.RESULT_MARKER
    for position, line in enumerate(lines):
        index = line.find(marker)
        if index < 0:
            continue
        text = "".join([line[index + len(marker):]] + [remote_output_text(following) for following in lines[position + 1:]]).lstrip()
        try:
            value = json.JSONDecoder().raw_decode(text)[0]
        except ValueError as error:
            return {"error": f"결과 해석 실패(표식 뒤 {len(text)}자, {error}): {text[:200]}"}
        return value if isinstance(value, dict) else {"error": "결과가 객체가 아니다: " + text[:200]}
    return {"error": "결과 표식 없음: " + " | ".join(lines)[-300:]}


def read_remote_result_file(path):
    try:
        loaded = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        return {"error": f"결과 파일 읽기 실패 {path.as_posix()}: {error}"}
    path.unlink(missing_ok=True)
    return loaded if isinstance(loaded, dict) else {"error": f"결과 파일이 객체가 아니다: {path.as_posix()}"}


def editor_action(action, **fields):
    result_path = REMOTE_RESULT_DIRECTORY / f"{RUN_TOKEN}_{uuid.uuid4().hex}_{action}.json"
    try:
        outcome = run_in_editor.run_code(build_editor_code(action, fields, result_path))
    except Exception as error:
        return {"error": f"에디터 원격 실행 예외 {type(error).__name__}: {str(error)[-200:]}"}
    if not outcome["connected"]:
        return {"error": "에디터 원격 실행에 연결하지 못했다"}
    result = parse_editor_result(outcome["lines"])
    if "result_file" in result:
        return read_remote_result_file(result_path)
    return result


def apply_variant(apply, tag, resets_state=False):
    return editor_action("lookdev_apply", apply=apply, tag=tag, output=OUTPUT_DIRECTORY.as_posix(), size=SHOT_SIZE, reset_state=resets_state)


def resolve_tuning_path(path):
    if not path:
        return ""
    candidate = Path(path)
    return (candidate if candidate.is_absolute() else ROOT / candidate).resolve().as_posix()


def load_tuning_and_wait(tuning, look):
    loaded = editor_action("lookdev_tuning_load", tuning=tuning, look=look)
    if "error" in loaded:
        return loaded
    if loaded.get("error_count"):
        return dict(loaded, error="조절·모양 표 다시 읽기 실패(이전 표 유지)")
    target_generation = loaded["table_generation"]
    deadline = time.monotonic() + TUNING_RELOAD_WAIT_SECONDS
    while time.monotonic() < deadline:
        report = editor_action("lookdev_reapply_report")
        if "error" in report:
            return dict(loaded, error=report["error"])
        if report.get("table_generation", -1) >= target_generation:
            return dict(loaded, report=report)
        time.sleep(TUNING_RELOAD_POLL_SECONDS)
    return dict(loaded, error=f"재적용 보고서가 {TUNING_RELOAD_WAIT_SECONDS:g}초 안에 세대 {target_generation} 에 닿지 않았다")


def run_tuning_reload(request):
    if not isinstance(request, dict) or not all(isinstance(request.get(key, ""), str) for key in ("tuning", "look")):
        return {"error": "tuning_reload 는 {tuning, look, reset, read_back, base} 객체여야 한다"}
    base = request.get("base", {})
    if not isinstance(base, dict) or not all(isinstance(base.get(key, ""), str) for key in ("tuning", "look")):
        return {"error": "tuning_reload.base 는 {tuning, look} 대조 사본 객체여야 한다"}
    reset = request.get("reset", TUNING_RESET_MODES[0])
    if reset not in TUNING_RESET_MODES:
        return {"error": f"tuning_reload.reset 은 {'|'.join(TUNING_RESET_MODES)}: {reset}"}
    outcome = {"reset": reset, "tuning": resolve_tuning_path(request.get("tuning", "")), "look": resolve_tuning_path(request.get("look", ""))}
    base_paths = {key: resolve_tuning_path(base.get(key, "")) for key in ("tuning", "look")}
    if "base" in request:
        outcome["base_copy"] = base_paths
    if request.get("relative_to_base", True) and (outcome["tuning"] or outcome["look"]):
        outcome["base"] = load_tuning_and_wait(base_paths["tuning"], base_paths["look"])
        if "error" in outcome["base"]:
            return dict(outcome, error=("대조 사본" if "base" in request else "정본") + " 다시 읽기: " + str(outcome["base"]["error"]))
    outcome["load"] = load_tuning_and_wait(outcome["tuning"], outcome["look"])
    if "error" in outcome["load"]:
        return dict(outcome, error=str(outcome["load"]["error"]))
    outcome["report"] = outcome["load"]["report"]
    if reset == TUNING_RESET_MODES[0]:
        outcome["reinit"] = editor_action("lookdev_tuning_reinit")
        if "error" in outcome["reinit"]:
            return dict(outcome, error="재초기화: " + str(outcome["reinit"]["error"]))
    if not request.get("read_back"):
        return dict(outcome, read_back=[]) if "read_back" in request else outcome
    read_back = editor_action("lookdev_tuning_read_back", requests=request["read_back"])
    if "error" in read_back:
        return dict(outcome, error="실효 값 다시 읽기: " + str(read_back["error"]))
    outcome["read_back"] = read_back["read_back"]
    return outcome


def is_number(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool)


def move_key_errors(where, move):
    if isinstance(move, list):
        return [] if len(move) == 2 and all(is_number(value) for value in move) else [f"{where}: 목록이면 [goal_x, goal_y] 숫자 2개여야 한다"]
    if not isinstance(move, dict):
        return [f"{where}: [goal_x, goal_y] 또는 {{distance_cm, yaw?}} 여야 한다"]
    errors = unknown_key_errors(where, move, MOVE_KEYS)
    if not (is_number(move.get("distance_cm")) and move["distance_cm"] > 0):
        errors.append(f"{where}.distance_cm: 0 초과 숫자여야 한다")
    if move.get("yaw") is not None and not is_number(move["yaw"]):
        errors.append(f"{where}.yaw: 숫자(도)여야 한다")
    return errors


def exposure_manual_key_errors(apply):
    errors = [] if apply["exposure_manual"] in EXPOSURE_MANUAL_MODES else [f"exposure_manual: {' | '.join(EXPOSURE_MANUAL_MODES)} 중 하나여야 한다"]
    if "exposure_ev100" not in apply:
        errors.append("exposure_manual: 같은 apply 에 exposure_ev100 이 필요하다")
    return errors


def unknown_key_errors(where, mapping, allowed):
    errors = []
    for key in mapping:
        if key in allowed:
            continue
        close = difflib.get_close_matches(str(key), allowed, n=1)
        errors.append(f"{where}: 모르는 키 {key}" + (f" ({close[0]}?)" if close else ""))
    return errors


def object_key_errors(where, value, allowed):
    if not isinstance(value, dict):
        return [f"{where}: 객체여야 한다"]
    return unknown_key_errors(where, value, allowed)


def item_key_errors(where, items, allowed, required):
    if not isinstance(items, list):
        return [f"{where}: 목록이어야 한다"]
    errors = []
    for index, item in enumerate(items):
        errors += object_key_errors(f"{where}[{index}]", item, allowed)
        if isinstance(item, dict):
            errors += [f"{where}[{index}]: {key} 필요" for key in required if key not in item]
    return errors


def dict_items(items):
    return [(index, item) for index, item in enumerate(items) if isinstance(item, dict)] if isinstance(items, list) else []


def call_key_errors(where, calls):
    if not isinstance(calls, dict):
        return [f"{where}: {{라벨: {{function, component?, args?}}}} 객체여야 한다"]
    errors = []
    for label, request in calls.items():
        errors += object_key_errors(f"{where}.{label}", request, CALL_KEYS)
        if isinstance(request, dict) and not request.get("function"):
            errors.append(f"{where}.{label}: function 필요")
    return errors


def read_back_owner_errors(items):
    errors = []
    for index, item in dict_items(items):
        owner = item.get("owner")
        if owner is None or owner in READ_BACK_OWNER_KINDS or any(isinstance(owner, str) and owner.startswith(prefix) and len(owner) > len(prefix) for prefix in READ_BACK_OWNER_PREFIXES):
            continue
        errors.append(f"tuning_reload.read_back[{index}]: owner {owner} 모름({'|'.join(READ_BACK_OWNER_KINDS)}|subsystem:<클래스>|actor_class:<클래스>)")
    return errors


def tuning_reload_key_errors(request):
    errors = object_key_errors("tuning_reload", request, APPLY_OBJECT_KEYS["tuning_reload"])
    if not isinstance(request, dict):
        return errors
    if request.get("reset", TUNING_RESET_MODES[0]) not in TUNING_RESET_MODES:
        errors.append(f"tuning_reload.reset: {request['reset']} ({'|'.join(TUNING_RESET_MODES)})")
    if "base" in request:
        errors += object_key_errors("tuning_reload.base", request["base"], APPLY_ITEM_KEYS["tuning_reload.base"])
    if "read_back" in request:
        errors += item_key_errors("tuning_reload.read_back", request["read_back"], APPLY_ITEM_KEYS["tuning_reload.read_back"], ("struct", "field"))
        errors += read_back_owner_errors(request["read_back"])
    return errors


def schedule_key_errors(apply):
    schedule = apply["schedule"]
    errors = object_key_errors("schedule", schedule, APPLY_OBJECT_KEYS["schedule"])
    if not isinstance(schedule, dict):
        return errors
    reference = schedule.get("reference", "now")
    if reference not in SCHEDULE_REFERENCES:
        errors.append(f"schedule.reference: {reference} ({'|'.join(SCHEDULE_REFERENCES)})")
    elif reference != "now" and reference not in apply:
        errors.append(f"schedule.reference {reference}: 같은 apply에 {reference} 키 필요")
    if "events" in schedule:
        errors += item_key_errors("schedule.events", schedule["events"], APPLY_ITEM_KEYS["schedule.events"], ("time",))
        for index, event in dict_items(schedule["events"]):
            if "call" in event:
                errors += call_key_errors(f"schedule.events[{index}].call", event["call"])
            if "move" in event:
                errors += move_key_errors(f"schedule.events[{index}].move", event["move"])
    return errors


def look_live_key_errors(look_live):
    errors = object_key_errors("look_live", look_live, APPLY_OBJECT_KEYS["look_live"])
    if not isinstance(look_live, dict):
        return errors
    optional_packages = look_live.get("skip_if_no_owner", [])
    if not isinstance(optional_packages, list) or not all(isinstance(package, str) for package in optional_packages):
        errors.append("look_live.skip_if_no_owner: 패키지 경로 문자열 목록이어야 한다")
    for group, sections in LOOK_GROUP_SECTIONS.items():
        packages = look_live.get(group, {})
        if not isinstance(packages, dict):
            errors.append(f"look_live.{group}: {{패키지: {{구역: {{이름: 값}}}}}} 객체여야 한다")
            continue
        for package, values in packages.items():
            if not str(package).startswith("/Game/") or "." in str(package):
                errors.append(f"look_live.{group}: 패키지 {package} 는 /Game/ 로 시작하고 . 이 없어야 한다")
            if isinstance(values, dict) and "Switches" in values:
                errors.append(f"look_live.{group}.{package}: Switches 는 실행 중 바꿀 수 없다")
            errors += object_key_errors(f"look_live.{group}.{package}", {key: value for key, value in values.items() if key != "Switches"} if isinstance(values, dict) else values, sections)
    return errors


def probe_key_errors(probe):
    errors = object_key_errors("probe", probe, APPLY_OBJECT_KEYS["probe"])
    if not isinstance(probe, dict):
        return errors
    if not probe.get("stats") and not probe.get("rt"):
        errors.append("probe: stats 나 rt 가 하나 이상 필요")
    if "stats" in probe:
        errors += item_key_errors("probe.stats", probe["stats"], APPLY_ITEM_KEYS["probe.stats"], ("function", "prefix"))
        errors += [f"probe.stats[{index}]: label 또는 subsystem 필요" for index, item in dict_items(probe["stats"]) if not item.get("label") and not item.get("subsystem")]
    if "rt" in probe:
        errors += item_key_errors("probe.rt", probe["rt"], APPLY_ITEM_KEYS["probe.rt"], ("label", "box", "step", "channel", "prefix"))
        for index, item in dict_items(probe["rt"]):
            if ("property" in item) == ("function" in item):
                errors.append(f"probe.rt[{index}]: property(렌더 타깃 속성) 나 function(텍스처 게터) 중 하나만 둔다")
            if item.get("channel") not in PROBE_CHANNELS:
                errors.append(f"probe.rt[{index}].channel: {item.get('channel')} ({'|'.join(PROBE_CHANNELS)})")
            if not (isinstance(item.get("box"), list) and len(item["box"]) == 4 and all(isinstance(value, (int, float)) for value in item["box"])):
                errors.append(f"probe.rt[{index}].box: [u0, v0, u1, v1] 숫자 4개여야 한다")
            if not (isinstance(item.get("step"), (int, float)) and item["step"] > 0):
                errors.append(f"probe.rt[{index}].step: 0 초과 숫자여야 한다")
    if "times" in probe and not (isinstance(probe["times"], list) and all(isinstance(value, (int, float)) and value >= 0 for value in probe["times"])):
        errors.append("probe.times: 0 이상 게임 초 목록이어야 한다")
    return errors


def wait_until_key_errors(condition):
    errors = object_key_errors("wait_until", condition, APPLY_OBJECT_KEYS["wait_until"])
    if isinstance(condition, dict):
        errors += [f"wait_until: {key} 필요" for key in ("label", "property", "equals") if key not in condition]
    return errors


def find_key_errors(apply):
    if not isinstance(apply, dict):
        return ["apply: 객체여야 한다"]
    errors = unknown_key_errors("apply", apply, APPLY_KEYS)
    nested_checks = {"tuning_reload": tuning_reload_key_errors, "look_live": look_live_key_errors, "probe": probe_key_errors, "wait_until": wait_until_key_errors}
    for key, check in nested_checks.items():
        if key in apply:
            errors += check(apply[key])
    if "schedule" in apply:
        errors += schedule_key_errors(apply)
    if "call" in apply:
        errors += call_key_errors("call", apply["call"])
    if "move" in apply:
        errors += move_key_errors("move", apply["move"])
    if "exposure_manual" in apply:
        errors += exposure_manual_key_errors(apply)
    if "view_target" in apply and not (isinstance(apply["view_target"], str) and apply["view_target"]):
        errors.append("view_target: 액터 라벨 문자열이어야 한다")
    return errors


def find_apply_failure_errors(outcome):
    if not isinstance(outcome, dict):
        return []
    errors = ["apply: " + str(outcome["error"])] if outcome.get("error") else []
    schedule = outcome.get("schedule")
    if isinstance(schedule, dict) and schedule.get("error"):
        errors.append("schedule: " + str(schedule["error"]))
    reload_outcome = outcome.get("tuning_reload")
    state_reset = reload_outcome.get("state_reset") if isinstance(reload_outcome, dict) else None
    if isinstance(state_reset, dict):
        errors += ["state_reset: " + str(line) for line in state_reset.get("errors", [])]
    cloud_flow_restart = outcome.get("cloud_flow_restart")
    if isinstance(cloud_flow_restart, dict):
        errors += ["cloud_flow_restart: " + str(line) for line in cloud_flow_restart.get("errors", [])]
    return errors


def find_move_errors(outcome):
    move = outcome.get("move") if isinstance(outcome, dict) else None
    return ["move: " + str(move["error"])] if isinstance(move, dict) and move.get("error") else []


def find_outcome_errors(outcome):
    if not isinstance(outcome, dict):
        return []
    errors = find_apply_failure_errors(outcome)
    reload_outcome = outcome.get("tuning_reload")
    if isinstance(reload_outcome, dict) and "error" in reload_outcome:
        errors.append("tuning_reload: " + str(reload_outcome["error"]))
    for key in ("preset", "emitter"):
        for label, entry in (outcome.get(key) or {}).items():
            if isinstance(entry, dict) and entry.get("ignored_copy_paths"):
                errors.append(f"{key}.{label}: R2 로 무시되는 사본 키 {', '.join(entry['ignored_copy_paths'])}")
    view_target = outcome.get("view_target")
    if isinstance(view_target, dict) and not view_target.get("ok"):
        errors.append(f"view_target {view_target.get('label')}: {view_target.get('error')}")
    errors += [f"call.{label}: {result['error']}" for label, result in (outcome.get("call") or {}).items() if isinstance(result, dict) and "error" in result]
    if isinstance(outcome.get("wait"), dict) and "error" in outcome["wait"]:
        errors.append("wait_until: " + str(outcome["wait"]["error"]))
    errors += ["look_live: " + line for line in outcome.get("look_errors", [])]
    if isinstance(outcome.get("probe"), dict):
        errors += ["probe: " + line for line in outcome["probe"].get("errors", [])]
    return errors


def find_schedule_event_errors(status):
    if not isinstance(status, dict):
        return []
    errors = ["schedule: " + str(status["error"])] if status.get("error") else []
    for event in status.get("events_done", []):
        errors += [f"schedule.events t={event['time']:g} call.{label}: {result['error']}" for label, result in (event.get("call") or {}).items() if isinstance(result, dict) and "error" in result]
        errors += [f"schedule.events t={event['time']:g} {line}" for line in find_move_errors(event)]
    return errors


def wait_until_condition(condition):
    timeout = float(condition.get("timeout", WAIT_UNTIL_DEFAULT_TIMEOUT_SECONDS))
    deadline = time.monotonic() + timeout
    checks = 0
    while True:
        checked = editor_action("lookdev_wait_check", wait_until=condition)
        checks += 1
        if "error" in checked or checked.get("matched"):
            return dict(checked, checks=checks)
        if time.monotonic() >= deadline:
            return dict(checked, checks=checks, error=f"시간 초과 {timeout:g}초: {condition['property']} 가 {condition['equals']!r} 이 아니다")
        time.sleep(WAIT_UNTIL_POLL_SECONDS)


def read_probe_sample(probe, reference, at):
    deadline = time.monotonic() + at + PROBE_WAIT_MARGIN_SECONDS
    while True:
        sample = editor_action("lookdev_probe", probe=probe, reference=reference, at=at)
        if "error" in sample:
            return {"time": at, "metrics": {}, "grids": {}, "errors": [str(sample["error"])]}
        if "wait_seconds" not in sample:
            return sample
        if time.monotonic() > deadline:
            return {"time": at, "metrics": {}, "grids": {}, "errors": [f"t={at:g} 게임 시각에 {PROBE_WAIT_MARGIN_SECONDS:g}초 여유 안에 닿지 않았다"]}
        time.sleep(min(sample["wait_seconds"], PROBE_POLL_SECONDS) + PROBE_SLEEP_MARGIN_SECONDS)


def median_probe_metrics(samples):
    metrics = {}
    for key in sorted({key for sample in samples for key in sample["metrics"]}):
        values = [sample["metrics"][key] for sample in samples if isinstance(sample["metrics"].get(key), (int, float))]
        if values:
            metrics[key] = statistics.median(values)
    return metrics


def collect_probe(probe, reference, tag):
    samples = []
    for index, at in enumerate(probe.get("times") or [0.0]):
        sample = read_probe_sample(probe, reference, float(at))
        for prefix, grid in sample.pop("grids", {}).items():
            grid_path = OUTPUT_DIRECTORY / f"{tag}_probe_{prefix}_{index:02d}.json"
            grid_path.parent.mkdir(parents=True, exist_ok=True)
            grid_path.write_text(json.dumps(grid), encoding="utf-8")
            sample.setdefault("grid_paths", {})[prefix] = grid_path.as_posix()
        samples.append(sample)
    return {"samples": samples, "metrics": median_probe_metrics(samples), "errors": [error for sample in samples for error in sample["errors"]]}


def read_back_look_live(look_live):
    time.sleep(LOOK_READ_BACK_DELAY_SECONDS)
    read_back = editor_action("lookdev_look_read_back", look_live=look_live)
    errors = read_back.get("look_errors", []) + ([str(read_back["error"])] if "error" in read_back else [])
    return {"look_read_back": read_back.get("look_read_back", {}), "owner_checks": read_back.get("owner_checks", []), "look_errors": errors}


def apply_variant_with_tuning(apply, tag, run_state):
    run_state["key_errors"].extend(f"{tag}: {line}" for line in find_key_errors(apply))
    reload_outcome = None
    if "tuning_reload" in apply:
        run_state["uses_tuning_reload"] = True
        reload_outcome = run_tuning_reload(apply["tuning_reload"])
    wait_outcome = wait_until_condition(apply["wait_until"]) if isinstance(apply.get("wait_until"), dict) else None
    resets_state = isinstance(reload_outcome, dict) and "error" not in reload_outcome and reload_outcome.get("reset") == TUNING_RESET_MODES[0]
    outcome = apply_variant({key: value for key, value in apply.items() if key not in OUTER_APPLY_KEYS}, tag, resets_state)
    if resets_state:
        state_reset = outcome.pop("state_reset", None) if isinstance(outcome, dict) else None
        state_reset = state_reset if isinstance(state_reset, dict) else {"actors": [], "errors": ["apply 원격 호출이 상태 초기화 결과를 돌려주지 않았다"]}
        reload_outcome = dict(reload_outcome, state_reset=state_reset, state_reset_actor_count=len(state_reset.get("actors", [])))
    if reload_outcome is not None:
        outcome = dict(outcome, tuning_reload=reload_outcome)
    if wait_outcome is not None:
        outcome["wait"] = wait_outcome
    if isinstance(apply.get("look_live"), dict) and "error" not in outcome:
        read_back = read_back_look_live(apply["look_live"])
        outcome.update(read_back, look_errors=outcome.get("look_errors", []) + read_back["look_errors"])
    if isinstance(apply.get("probe"), dict) and "error" not in outcome:
        outcome["probe"] = collect_probe(apply["probe"], outcome.get("game_time", 0.0), tag)
    run_state["key_errors"].extend(f"{tag}: {line}" for line in find_outcome_errors(outcome))
    return outcome


def wait_for_file(path):
    deadline = time.monotonic() + FRAME_FILE_WAIT_SECONDS
    while time.monotonic() < deadline:
        if path.exists() and path.stat().st_size > 0:
            return True
        time.sleep(0.5)
    return False


def list_editor_screenshots():
    return set(SCREENSHOT_DIRECTORY.glob("*.png")) if SCREENSHOT_DIRECTORY.exists() else set()


def wait_for_schedule(status_path, timeout_seconds):
    deadline = time.monotonic() + timeout_seconds
    status = {}
    while time.monotonic() < deadline:
        try:
            status = json.loads(status_path.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            pass
        if status.get("done"):
            return status
        time.sleep(SCHEDULE_POLL_SECONDS)
    return dict(status, outer_timeout=True)


def collect_walk(walk):
    if not isinstance(walk, dict) or "status_path" not in walk:
        return walk
    status = wait_for_schedule(Path(walk["status_path"]), walk.get("wall_timeout_seconds", 0.0) + SCHEDULE_POLL_MARGIN_SECONDS)
    if status.get("done"):
        return status
    stopped = editor_action("lookdev_walk_stop")
    return dict(stopped if isinstance(stopped, dict) and "mode" in stopped else dict(walk, **status), error="바깥 Python 대기 시간 초과", outer_timeout=True)


def collect_walks(outcome, schedule_status):
    if isinstance(outcome, dict) and "move" in outcome:
        outcome["move"] = collect_walk(outcome["move"])
    for event in (schedule_status or {}).get("events_done", []):
        if "move" in event:
            event["move"] = collect_walk(event["move"])


def find_walk_errors(outcome, schedule_status):
    collect_walks(outcome, schedule_status)
    return find_move_errors(outcome) + find_schedule_event_errors(schedule_status)


def describe_seconds(value):
    return f"{value:.3f}" if isinstance(value, (int, float)) else "?"


def collect_schedule_shots(outcome, label, shots):
    schedule = outcome.get("schedule") if isinstance(outcome, dict) else None
    if not isinstance(schedule, dict) or "status_path" not in schedule:
        return None
    status = wait_for_schedule(Path(schedule["status_path"]), schedule["timeout_seconds"] + SCHEDULE_POLL_MARGIN_SECONDS)
    if not status.get("done"):
        stopped = editor_action("lookdev_schedule_stop")
        status = dict(stopped, outer_timeout=True) if isinstance(stopped, dict) and "shots" in stopped else dict(status, stop_result=stopped)
    for shot in status.get("shots", []):
        path = Path(shot["path"])
        if wait_for_file(path):
            shots.append((f"{label} t={describe_seconds(shot['time'])} game={describe_seconds(shot.get('captured_game_time'))}", path))
    return status


def scheduled_shot_count(apply):
    return len(apply["schedule"].get("times", [])) if isinstance(apply.get("schedule"), dict) else 0


def build_grid(shots, grid_path):
    tiles = []
    for name, path in shots:
        image = Image.open(path).convert("RGB")
        tile_height = int(image.height * GRID_TILE_WIDTH / image.width)
        tile = image.resize((GRID_TILE_WIDTH, tile_height))
        draw = ImageDraw.Draw(tile)
        draw.rectangle((0, 0, GRID_TILE_WIDTH, 22), fill=(0, 0, 0))
        draw.text((6, 4), name, fill=(255, 255, 255))
        tiles.append(tile)
    if not tiles:
        return False
    rows = (len(tiles) + GRID_COLUMNS - 1) // GRID_COLUMNS
    grid = Image.new("RGB", (GRID_TILE_WIDTH * GRID_COLUMNS, tiles[0].height * rows))
    for index, tile in enumerate(tiles):
        grid.paste(tile, ((index % GRID_COLUMNS) * GRID_TILE_WIDTH, (index // GRID_COLUMNS) * tiles[0].height))
    grid.save(grid_path)
    return True


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--variants", required=True, type=Path)
    parser.add_argument("--level", default=DEFAULT_LEVEL_PATH)
    parser.add_argument("--settle-seconds", type=float, default=3.0)
    parser.add_argument("--stop-pie", action="store_true")
    parser.add_argument("--fresh-pie", action="store_true", help="시작할 때 남은 PIE 를 멈추고 --level 을 새로 열어 시작(앞 실행이 남긴 다른 무대 월드를 찍지 않게)")
    parser.add_argument("--output-dir", type=Path, help="출력 폴더(기본 Saved/FluidFX/lookdev/<시각>, 상대 경로는 프로젝트 루트 기준)")
    parser.add_argument("--run-id", help="실행 식별자(result.json run_id 로 되적어 calib_sweep 이 이번 실행 결과인지 대조)")
    arguments = parser.parse_args()
    started_at = datetime.now().isoformat(timespec="seconds")
    global OUTPUT_DIRECTORY
    if arguments.output_dir is not None:
        OUTPUT_DIRECTORY = arguments.output_dir if arguments.output_dir.is_absolute() else ROOT / arguments.output_dir
    specification_bytes = arguments.variants.read_bytes()
    specification = json.loads(specification_bytes.decode("utf-8"))
    print("경고: 측정 동안 UnrealEditor 창을 앞창으로 두고 건드리지 않는다(L-editor-06).", flush=True)
    OUTPUT_DIRECTORY.mkdir(parents=True, exist_ok=True)

    session = None
    environment = {}
    if arguments.fresh_pie and pie_check_fluid.wait_for_pie_state(True, 0.5):
        try:
            session = uemcp.connect()
        except (OSError, ValueError, KeyError) as error:
            print("MCP 서버에 연결하지 못했다:", error)
            return EXIT_ENVIRONMENT
        if not pie_check_fluid.stop_pie(session, environment):
            print("남은 PIE 를 멈추지 못했다:", json.dumps(environment, ensure_ascii=False)[:300])
            return EXIT_ENVIRONMENT
        environment["stopped_leftover_pie"] = True
    if not pie_check_fluid.wait_for_pie_state(True, 0.5):
        level_status = pie_showcase_fluid.open_stage_level(arguments.level)
        if not level_status.get("loaded"):
            print("레벨을 열지 못했다:", json.dumps(level_status, ensure_ascii=False))
            return EXIT_ENVIRONMENT
        try:
            session = uemcp.connect()
        except (OSError, ValueError, KeyError) as error:
            print("MCP 서버에 연결하지 못했다:", error)
            return EXIT_ENVIRONMENT
        environment["navigation_ready"] = pie_check_fluid.wait_for_editor_navigation()
        if not pie_check_fluid.start_pie(session, environment) or not pie_check_fluid.wait_for_pie_state(True, pie_check_fluid.PIE_START_WAIT_SECONDS):
            print("PIE를 시작하지 못했다:", json.dumps(environment, ensure_ascii=False)[:300])
            return EXIT_ENVIRONMENT
        time.sleep(3.0)

    try:
        return capture_variants(arguments, specification, specification_bytes, started_at, environment)
    finally:
        if arguments.stop_pie:
            if session is None:
                session = uemcp.connect()
            pie_check_fluid.stop_pie(session, environment)


def capture_variants(arguments, specification, specification_bytes, started_at, environment):
    shots = []
    base_apply = specification.get("base", {})
    run_state = {"uses_tuning_reload": False, "key_errors": []}
    result = {"run_id": arguments.run_id, "started_at": started_at, "variants_sha256": hashlib.sha256(specification_bytes).hexdigest(), "level": arguments.level,
              "stopped_leftover_pie": bool(environment.get("stopped_leftover_pie")), "base": None, "variants": []}
    expected_shots = scheduled_shot_count(base_apply)
    try:
        result["base"] = apply_variant_with_tuning(base_apply, "base", run_state)
        base_schedule = collect_schedule_shots(result["base"], "base", shots)
        run_state["key_errors"].extend(f"base: {line}" for line in find_walk_errors(result["base"], base_schedule))
        if base_schedule is not None:
            result["base_schedule"] = base_schedule
        for index, variant in enumerate(specification["variants"]):
            apply = variant.get("apply", {})
            screenshots_before = list_editor_screenshots()
            outcome = apply_variant_with_tuning(apply, f"{index:02d}", run_state)
            entry = {"name": variant["name"], "apply": outcome}
            if "schedule" in apply:
                expected = scheduled_shot_count(apply)
                expected_shots += expected
                shots_before = len(shots)
                entry["schedule"] = collect_schedule_shots(outcome, variant["name"], shots)
                entry["saved"] = len(shots) - shots_before == expected
                summary = f"예약 {len(shots) - shots_before}/{expected}장"
            else:
                expected_shots += 1
                time.sleep(variant.get("settle", arguments.settle_seconds))
                path = OUTPUT_DIRECTORY / f"{index:02d}_{variant['name']}.png"
                shot = editor_action("lookdev_shot", size=SHOT_SIZE, path=path.as_posix())
                outcome["shot_game_time"] = shot.get("game_time") if isinstance(shot, dict) else None
                if isinstance(shot, dict) and "preset_effect" in shot:
                    outcome["preset_effect"] = shot["preset_effect"]
                if isinstance(shot, dict) and "ember_mid" in shot:
                    outcome["ember_mid"] = shot["ember_mid"]
                is_saved = wait_for_file(path)
                if is_saved:
                    shots.append((variant["name"], path))
                entry["saved"] = is_saved
                summary = "찍음" if is_saved else "실패"
            run_state["key_errors"].extend(f"{index:02d}: {line}" for line in find_walk_errors(outcome, entry.get("schedule")))
            if "console" in apply:
                entry["console_screenshots"] = sorted(path.relative_to(ROOT).as_posix() for path in list_editor_screenshots() - screenshots_before)
            result["variants"].append(entry)
            print(variant["name"], summary, json.dumps(outcome, ensure_ascii=False)[:200], flush=True)
    finally:
        if run_state["uses_tuning_reload"]:
            result["canonical_reload"] = editor_action("lookdev_tuning_load", tuning="", look="")
    result["key_errors"] = run_state["key_errors"]

    has_grid = build_grid(shots, OUTPUT_DIRECTORY / "grid.png")
    (OUTPUT_DIRECTORY / "result.json").write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    print("격자", (OUTPUT_DIRECTORY / "grid.png").as_posix() if has_grid else "없음")
    for line in run_state["key_errors"]:
        print("키 오류:", line)
    return 0 if len(shots) == expected_shots and not run_state["key_errors"] else EXIT_PARTIAL


if __name__ == "__main__":
    sys.exit(main())

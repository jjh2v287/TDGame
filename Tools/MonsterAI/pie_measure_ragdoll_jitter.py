"""실행 환경: 시스템 Python 3.12(언리얼 에디터가 TDGame을 열고 원격 실행이 켜진 상태, 에디터 앞창 유지 L-editor-06). 맵을 저장하지 않는다. 헤드리스 시험과 동시에 돌리지 않는다(L-editor-20). 시트 합치기에 PIL이 있으면 쓴다(없으면 파일 목록만).
실행 명령: python Tools/MonsterAI/pie_measure_ragdoll_jitter.py --label baseline [--task M3-40] [--species Goblin ...] [--force-species Hyena ...] [--no-player] [--repeats 2] [--kill-strength weak|strong] [--critical] [--screenshot-times '0,0.1,0.2,0.35,0.6,1.0' [--screenshot-clock ragdoll|kill] [--screenshot-fixed-fps 60]] [--camera-arm 400] [--auto-rerun-load-ms 30] [--count 20 [--frame-time-only]] [--count 5 --spread '250,450'] [--count 3 --pile-spacing-scale 2 | --pile-gaps 2.4 3.4] [--flight-hold 2.6] [--freeze] [--cvar TD.Death.ImmediateRagdoll=0 | --cvar TD.Feedback.SlowMo=0(보스 처치 슬로모 대조군)] [--analyze-only] | --summarize <label> ... (마크다운 비교표만) | --contact-sheet <label> ... (시간 지정 스크린샷 시트만) (PowerShell, L-repo-01. 쉼표로 이은 값 --screenshot-times·--spread·--assist·--emulate-*는 작은따옴표로 감싼다 '0,0.1,0.2': 따옴표가 없으면 PowerShell이 배열로 펼쳐 공백으로 나눠 넘기므로 unrecognized arguments)
출력: Docs/MonsterAI_CombatSim/measurements/<task>/<label>/report.json(대상마다 정착 창 게임 스레드 뼈 위치 RMS(render)·뼈 개정 번호 증가율(bone_revision)·프레임 이동 RMS·고주파 RMS·최대 각속도·잠들기 초·잠들 때마다 바디 속도·다시 깨어난 횟수·슬로모 겹침·다른 시체 정착, launch: 시작 지연 실시간·D 오차·2초 COM 이동·착지 감지·착지 뒤 미끄러짐·착지 각·0.6·1.0초 순간 몸 각 instant_angle_deg·물기 동안 몸통 굴림 최대 bite_torso_roll_max_deg_s(착지 굴림 상한 작동 확인용, 효과 판정은 정지 각 rest_angle_deg·instant_angle_deg)·첫 프레임 바디 상대 속도·서 있는/잠든 COM 높이 비율, multi_kill: 동시 처치 래그돌 시작 시각과 퍼짐(최대−최소)·--spread 처치 거리, rerun: 부하 재측정 사유, frame_time: 처치 전 1초·뒤 3초 p50·p95(고정 시간 간격 작업은 생략), 지표 정의 definitions, 닫은 에셋 편집기 asset_editors_closed, 부하 재측정 목록 load_reruns), <대상>-<n>_shot<k>.png 연사·<대상>-1_<ragdoll|kill><ms>ms.png 시간 지정 스크린샷(실제로 그려진 프레임의 게임 시각 captured_*)·contact_sheet.png, 원본 표본 Saved/AgentOps/<날짜>/ragdoll-<task>/<label>/raw/(M3-38은 20261018/ragdoll-jitter, 부하로 다시 잰 첫 원본은 raw/load_rerun/). 종료 코드 0 측정 완료 / 2 환경 문제
상태: 현행(M3-40). 정착 창 = 착지(모든 바디 선속도 60cm/s 미만, 래그돌 1.5초 뒤 상한) 0.5초 뒤부터 2.5초(가라앉기 시작 전까지). 목표 게임 스레드 뼈 위치 RMS(render 키, 화면 스키닝 아님) 0.5cm 이하·래그돌 3초 안 잠들기·재우는 순간 바디 속도 30cm/s 이하. 착지 감지는 C++ 날림 감시 v2 규칙(vz_min ≤ −60, 반등 ≥ max(35, 0.25·|vz_min|), 지면+누운 높이+25cm, 대체 T+0.25초·COM≤지면+누운 높이+75cm, 1.5초 뒤 높이 조건만(overdue)·3초 포기(abandoned))의 파이썬 재현, 지면 높이는 시작 때 하나로 고정(C++는 매 프레임 재트레이스, 평지에서 같음). --count 2 이상이면 함께 처치한 몬스터도 <대상>-<n>.<k>로 모두 분석하고, --spread MIN,MAX면 처치 틱 직전에 처치 순서(다른 몬스터들→대상)대로 가해자 거리 MIN→MAX 부채꼴로 옮겨 처치 순서별 시차(첫 처치 0, 나머지 0.02~0.07초 결정적 변주, 거리와 무관; 거리 물결은 지면 장판 처치만)를 multi_kill로 본다. PIE 시작마다 저장 확인 창이 없는 에셋 편집기(물리 에셋·스켈레톤·애니메이션·블루프린트)를 닫는다(HighResShot·Shot이 먼저 그려지는 편집기 뷰포트를 찍고 부하를 더함, 실패해도 warning만 내고 진행, 메시·머티리얼·나이아가라 편집기는 확인 창 위험으로 닫지 않으니 직접 닫는다, 더티 상태는 메모리에 남고 저장 안 함). --auto-rerun-load-ms(기본 30, 0 끔): 처치 전 1초 프레임 p50이 넘으면 그 대상을 한 번 다시 재고(플레이어는 PIE 재시작 뒤) 재측정 결과로 바꾸며 사유를 rerun에 남김(고정 시간 간격 촬영 작업은 frame_time이 없어 제외). --screenshot-times 촬영 작업(첫 회차)은 t.OverrideFPS 고정 시간 간격(기본 60)으로 돌고 작업이 끝나면 이전 값으로 되돌린다: 게임 시간이 촬영 끊김만큼 건너뛰지 않는 대신 그 작업의 월드 실시간은 벽시계가 아니라 frame_time을 생략하므로 성능은 촬영 없는 실행으로 잰다. --screenshot-fixed-fps 0이면 예전처럼 끊김만큼 게임 시간이 건너뛰니 촬영 실행과 지표 실행을 나눈다. --camera-arm은 플레이어 카메라 팔 길이를 바꾸고 대상을 화면 가운데에 두며(피치·요 유지) 작업마다 되돌린다. 작업 대기는 JOB_STATUS의 game_seconds(게임 시간)가 늘어나는 동안 계속하고, 최소 대기(observe+kill_delay+kill_timeout+25초)가 지난 뒤 30초 동안 늘지 않을 때만 job did not finish로 본다. 측정 반복 중 예외·중단(Ctrl+C)에도 finally에서 콘솔 변수를 되돌리고(실패해도) RESTORE_CODE(종·바디·고정 간격·카메라를 항목마다 따로, 하나가 실패해도 나머지 복구)와 PIE 종료를 하며, 복구 결과가 dict(오류·다시 읽은 값 불일치)인 항목은 warning: 줄로 출력한다
"""
import argparse
import functools
import json
import math
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
RUN_IN_EDITOR = ROOT / "Tools" / "run_in_editor.py"
SAMPLER_PATH = ROOT / "Tools" / "MonsterAI" / "editor_ragdoll_jitter_sampler.py"
SPECIES_TABLE_PATH = ROOT / "Tools" / "MonsterAI" / "species_table.json"
SHOT_DIRECTORY = ROOT / "Saved" / "Screenshots" / "WindowsEditor"
MEASUREMENT_ROOT = ROOT / "Docs" / "MonsterAI_CombatSim" / "measurements"
M338_RAW_ROOT = ROOT / "Saved" / "AgentOps" / "20261018" / "ragdoll-jitter"
RESULT_MARKER = "TD_RAGDOLL_JITTER"
DEFAULT_SPECIES = ["Goblin", "SkeletonGuard", "StoneGolem", "BookHeadBrute", "Greystone", "Wukong", "Swordsman"]
PIE_START_WAIT_SECONDS = 120.0
SPAWN_WAIT_SECONDS = 15.0
SPAWN_SETTLE_SECONDS = 1.0
JOB_EXTRA_WAIT_SECONDS = 25.0
JOB_STALL_SECONDS = 30.0
JOB_POLL_SECONDS = 0.5
SHOT_WAIT_SECONDS = 15.0
DEFAULT_SCREENSHOT_FIXED_FPS = 60.0
FIXED_STEP_VARIABLE = "t.OverrideFPS"
SAMPLER_RESTORE_ITEMS = (("species", "restore_species"), ("overrides", "restore_overrides"), ("fixed_step_fps", "restore_fixed_step"), ("camera", "restore_camera"))
LAND_SPEED_CM_S = 60.0
LAND_MIN_SECONDS = 0.2
LAND_MAX_SECONDS = 1.5
WINDOW_DELAY_SECONDS = 0.5
WINDOW_LENGTH_SECONDS = 2.5
MIN_WINDOW_SECONDS = 1.0
TARGET_POSITION_RMS_CM = 0.5
TARGET_SLEEP_SECONDS = 3.0
TARGET_FREEZE_BODY_SPEED_CM_S = 30.0
SLEEP_CHECK_TOLERANCE_SECONDS = 0.05
FREEZE_LOOKBACK_SECONDS = 0.35
DEFAULT_AUTO_RERUN_LOAD_MS = 30.0
LOAD_RERUN_DIRECTORY = "load_rerun"
INSTANT_ANGLE_SECONDS = (0.6, 1.0)
EXIT_ENVIRONMENT = 2
PLAYER_KILLER_SPECIES = "Goblin"
PLAYER_KILLER_DISTANCE = 1200.0
WEAK_KILL_FRACTION = 0.05
FRAME_TIME_BEFORE_SECONDS = 1.0
FRAME_TIME_AFTER_SECONDS = 3.0
KILL_DELAY_MARGIN_SECONDS = 0.2
DIRECTION_SAMPLE_SECONDS = 0.1
TRAVEL_SECONDS = 2.0
SLIDE_SECONDS = 2.0
CONTACT_SEARCH_SECONDS = 1.5
FIRST_FRAME_COUNT = 3
GRAVITY_CM_S2 = 980.0
EXTRA_GRAVITY_FRACTION = 0.6
EFFECTIVE_GRAVITY_CM_S2 = GRAVITY_CM_S2 * (1.0 + EXTRA_GRAVITY_FRACTION)
LAUNCH_UP_SPEED_CM_S = 100.0
MIN_DROP_HEIGHT_CM = 10.0
UPRIGHT_REST_HEIGHT_RATIO = 0.18
HORIZONTAL_REST_HEIGHT_RATIO = 0.45
LANDING_MIN_FALL_SPEED_CM_S = -60.0
LANDING_REBOUND_MIN_SPEED_CM_S = 35.0
LANDING_REBOUND_FRACTION = 0.25
LANDING_HEIGHT_MARGIN_CM = 25.0
LANDING_FALLBACK_EXTRA_SECONDS = 0.25
LANDING_FALLBACK_HEIGHT_MARGIN_CM = 75.0
LANDING_OVERDUE_SECONDS = 1.5
LANDING_ABANDON_SECONDS = 3.0
LANDING_BITE_SECONDS = 0.35
CONTACT_CELL_WIDTH = 480
CONTACT_CAPTION_HEIGHT = 22
LAUNCH_COLUMNS = ("측정", "대상", "몸", "처치", "시작 지연 실시간 초", "다수 처치 시작 퍼짐 실시간 초", "D 오차 도", "2초 COM 이동 cm", "착지 감지 초(*대체)", "예측 체공 T 초",
                  "착지 뒤 미끄러짐 cm", "착지 각 도", *(f"{seconds:.1f}초 몸 각 도" for seconds in INSTANT_ANGLE_SECONDS), "첫 프레임 상대 속도 cm/s", "서 있는 COM ÷h",
                  "잠든 COM ÷h", "잠들기 초", "프레임 p50/p95 ms 전→후")
LAUNCH_HEADER = "| " + " | ".join(LAUNCH_COLUMNS) + " |"
LAUNCH_SEPARATOR = "|" + "---|" * len(LAUNCH_COLUMNS)
METRIC_DEFINITIONS = {
    "ragdoll_delay_real_seconds": "처치(체력 0) 틱 월드 실시간부터 첫 시뮬 프레임 실시간까지(처치 정지는 실시간이라 ragdoll_delay_seconds 게임 시간과 다를 수 있음)",
    "launch_direction_error_deg": "래그돌 시작 0.1초 뒤(게임 시간) 첫 프레임의 질량 가중 COM 수평 속도와 R(처치 순간 가해자→피해자 수평 단위 방향)의 각",
    "com_travel_2s_cm": "래그돌 시작 프레임 COM과 2초 뒤(게임 시간) 첫 프레임 COM의 수평 거리",
    "land_detect_seconds": "C++ 날림 감시 v2 재현. 래그돌 시작부터 매 프레임 COM 수직 속도 vz, vz_min = min(0, 지금까지 vz). 경과 ≥ 3초면 착지 없음(abandoned), 경과 ≥ 1.5초면 COM ≤ 지면+누운 높이+75cm일 때만 착지(overdue), 경과 ≥ T+0.25초이고 COM ≤ 지면+누운 높이+75cm면 대체(*), 아니면 vz_min ≤ −60이고 vz − vz_min ≥ max(35, 0.25·|vz_min|)이고 COM ≤ 지면+누운 높이+25cm일 때. T = (U+√(U²+2·1.6g·Δz))/1.6g, U 100, Δz = max(10, 시작 COM − 지면 − 누운 높이), 누운 높이 = 종 누운 비율(0이면 직립 0.18·수평 몸 0.45) × 캡슐 반높이",
    "lowest_fall_speed_seconds": "래그돌 1.5초 안 COM vz 최저 시각(실제 접촉 근사). fallback_before_lowest_fall_speed는 대체 감지가 그보다 먼저 걸림(공중 감지)",
    "post_land_slide_cm": "착지 감지 프레임 COM부터 min(감지+2초, 최종 잠들기, 가라앉기 시작) 프레임 COM까지 수평 거리",
    "landing_angle_deg": "착지 감지 프레임의 몸 각(rest_angle_deg는 최종 잠든 프레임). 직립: 첫 바디(골반)→머리 바디(이름에 head, 없으면 neck, 없으면 처치 프레임에서 가장 높은 바디) 질량 중심 벡터와 Up의 각. 수평 몸: 질량 상위 바디 3개마다 처치 프레임의 월드 Up을 그 바디의 앞·옆·위 축 좌표로 고정해 그 프레임 축으로 옮긴 벡터와 Up의 각을 평균(굴림 각 근사, 코 들림이 있으면 피치도 섞임)",
    "render": "정착 창(착지 0.5초 뒤부터 2.5초, 가라앉기 시작 전까지) 게임 스레드 뼈 위치 RMS: 표본기 매 프레임 GetSocketLocation(물리 블렌드 뒤 컴포넌트 공간 뼈 × 컴포넌트 변환). 화면에 그려진 GPU 스키닝 자세가 아니다 — 뼈 개정 번호가 그대로면 렌더러가 이전 스키닝 결과를 다시 써서 화면 자세가 굳으므로 그것은 bone_revision으로 본다. physics는 같은 창의 바디 질량 중심 RMS",
    "bone_revision": "메시 뼈 개정 번호 rev(C++ GetMeshBoneRevisionForTest = GetBoneTransformRevisionNumber, 추종 메시는 리더 값)의 연속 표본 쌍(게임 시간이 늘어난 쌍만). 창은 래그돌 시작부터 최종 잠들기(없으면 가라앉기 시작, 그것도 없으면 끝)까지. awake_frames = 앞 표본이 깨어 있던 쌍 수, increased_frames·increase_fraction = 그중 rev가 늘어난 쌍(목표 1.0: 렌더러가 매 프레임 새 물리 자세로 다시 스키닝. C++는 0.1초 안에 그려지지 않은(WasRecentlyRendered 거짓, 그림자 포함) 시체는 밀지 않고 다시 그려진 첫 프레임에 밀므로 화면 밖에 있던 대상은 1.0 미만이 정상), first_unchanged_seconds = rev가 처음 늘지 않은 쌍의 래그돌 기준 게임 시각. asleep_increased_frames = 최종 잠든 뒤 가라앉기 전까지 앞뒤 표본이 모두 잠든 쌍 중 rev가 는 쌍(잠든 직후 한 번 더 미는 1개까지 정상, 더 많으면 잠든 시체도 계속 다시 스키닝). rev가 없으면(C++ 빌드 전) null",
    "first_frame_relative_speed_cm_s": "첫 시뮬 프레임 max|vᵢ − v_COM|(질량 가중), relative_speed_first_frames_cm_s는 첫 3프레임",
    "standing_com_ratio": "(처치 프레임 COM − 지면) / 캡슐 반높이",
    "rest_height_ratio": "(최종 잠든 프레임 COM − 지면) / 캡슐 반높이, 종 표 corpse_rest_height_ratio 채움용",
    "ground": "처치 순간 COM에서 아래로 300cm WorldStatic·WorldDynamic 오브젝트 트레이스(못 맞히면 액터 z − 캡슐 반높이, ground_source capsule)",
    "frame_time": "슬레이트 틱마다 월드 실시간 차이(ms). 처치 틱 전 1초·뒤 3초에 시작한 프레임의 p50·p95, after_sampler_ms는 그 프레임 안 표본기 비용(빼서 볼 것). before_advancing_ms는 처치 전 틱 중 월드 실시간이 늘어난 틱만(before_stalled_ticks = 월드가 돌지 않은 0ms 슬레이트 틱 수, 편집기 뷰포트·멈춤 오염 신호). fixed_step 작업은 계산하지 않음",
    "timed_shots": "--screenshot-times 시각은 --screenshot-clock 기준 게임 시간(GetTimeSeconds). ragdoll(기본): 대상 첫 시뮬 프레임의 게임 시간이 0이라 처치 정지(처치 틱→래그돌 시작)는 포함하지 않음, 정지 길이는 captured_game_seconds_after_kill − captured_game_seconds_after_ragdoll. kill: 처치 틱이 0이라 처치 정지 포함. 그 시각 뒤 첫 틱에 명령(issued_game_seconds)하고 사진은 다음 월드 프레임의 그림이라 captured_*가 실제 찍힌 프레임의 게임 시각. 앞 사진이 그려지기 전에는 다음 명령을 미룸(HighResShot 요청 덮어쓰기 방지). 보스 처치 슬로모가 걸리면 게임 시간이 느려져 같은 간격도 실시간으로는 길어짐",
    "fixed_step": "시간 지정 스크린샷 작업 동안 t.OverrideFPS=fps(UEngine::UpdateTimeAndHandleMaxTickRate가 매 프레임 시간 간격을 1/fps로 덮어씀)로 HighResShot 끊김에도 게임 시간이 1/fps씩만 감, 작업 끝에 이전 값으로 복구(restored.fixed_step_fps, 다시 읽은 값이 다르면 {read_back, expected, kept_for_retry}이고 다음 복구가 다시 시도). 이 동안 월드 실시간(GetRealTimeSeconds)도 1/fps씩 늘어 벽시계가 아니므로 frame_time은 생략하고 *_real_seconds·fps는 게임 안 실시간(처치 정지·슬로모 상한이 쓰는 시계)",
    "bite_torso_roll_max_deg_s": "C++ 착지 굴림 상한이 작동하는지 확인하는 용도이고 효과 판정 지표가 아니다(넘어감·뒤집힘 효과는 정지 각 rest_angle_deg와 instant_angle_deg로 판정). 수평 몸만 계산: 착지 감지 프레임 다음부터 0.35초(물기) 동안 몸통(이름에 pelvis·spine이 든 바디, C++ IsLandingRollTorsoBone과 같은 규칙) 질량 가중 각속도의 긴 축(마지막 몸통 바디 − 첫 몸통 바디 질량 중심 방향) 성분 절댓값 최대. 상한(200°/s, 물기 동안 매 프레임 초과분을 몸 전체에서 강체로 뺌)은 C++ ShouldLimitLandingRoll = 수평 몸 && 캡슐 반높이 ≤ 70 && 몸통 질량 비율 ≥ 0.3인 종(Hyena·Barghest·BarghestAlpha)에만 걸리고, 그 종에서 작동하면 약 200 이하(축 정의가 C++의 처치 순간 Forward와 조금 달라 몇 °/s 차이 가능). 날개·큰 수평 몸(Griffon·GriffonAlpha·Centaur·CentaurHunter·MountainDragon)은 상한이 없어 참고값이며, 상한 전 값은 넘어짐 걸림 직후 수백 °/s",
    "instant_angle_deg": "래그돌 시작 뒤 0.6·1.0초(게임 시간) 첫 프레임의 몸 각(landing_angle_deg와 같은 정의), 착지 뒤 배 쪽 과회전·순간 뒤집힘 확인용(rest_angle_deg와 함께 착지 굴림 상한의 효과 판정 지표)",
    "multi_kill": "--count 2 이상 동시 처치(others_killed): 처치 순서(다른 몬스터들→대상)마다 처치 틱부터 첫 시뮬 프레임까지 게임·실시간 초와 그 최대−최소(ragdoll_start_spread_*), --spread면 kill_distances_cm(처치 직전 배치 뒤 가해자 3D 거리). 래그돌이 시작되지 않은 몬스터는 ragdoll_missing",
    "rerun": "--auto-rerun-load-ms: 처치 전 1초 프레임 p50(frame_time.before_ms.p50과 before_advancing_ms.p50 중 큰 값, 0ms 틱이 절반을 넘어 before_ms.p50이 0이 되는 월드 멈춤도 잡음)이 한도를 넘으면 그 대상을 한 번 다시 재고 결과를 바꿈. 사유·첫 측정 frame_time·첫 원본(raw/load_rerun/)·재측정 p50·still_over_limit을 기록, 재측정이 실패하면 첫 결과를 남기고 rerun_error. 고정 시간 간격(촬영) 작업은 frame_time이 없어 대상 아님",
    "asset_editors_closed": "PIE 시작마다 원격 실행으로 닫은 에셋 편집기(closed 에셋 경로), 실패(failed), 닫기 확인 창 위험으로 닫지 않은 종류(left_open_prompting_classes, 열려 있으면 직접 닫을 것)",
    "camera": "--camera-arm: 작업 동안 플레이어 폰 SpringArm target_arm_length를 그 값으로, 카메라 지연을 끄고, 매 틱 target_offset(월드)을 처치 전에는 대상 액터 위치, 처치 뒤에는 처치 순간 대상 위치에 맞춰 대상이 화면 가운데(피치·요 유지, 날아간 시체는 그 기준점에서 멀어짐). 작업 끝에 원래 값 복구(restored.camera). 피드백 흔들림·떨림 배율(ResolveCameraArmLength)도 팔 길이를 따름",
}

PRELUDE = '''import unreal, json, sys
def _td_world():
    return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
def _td_print(payload):
    print('{marker}', json.dumps(payload))
'''

START_CODE = '''def _td_start():
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if editor.get_game_world() is None:
        if not editor.get_editor_world().get_path_name().startswith('{level}'):
            levels.load_level('{level}')
        levels.editor_request_begin_play()
    _td_print({{'requested': True}})
_td_start()
del _td_start
'''

STATUS_CODE = '''def _td_status():
    world = _td_world()
    pawn = unreal.GameplayStatics.get_player_pawn(world, 0) if world else None
    _td_print({'pie': world is not None, 'pawn': pawn is not None})
_td_status()
del _td_status
'''

LOAD_SAMPLER_CODE = '''def _td_load():
    import importlib.util
    previous = sys.modules.get('td_ragdoll_sampler')
    restored = {{}}
    if previous and previous.STATE.get('handle') is not None:
        try:
            unreal.unregister_slate_post_tick_callback(previous.STATE['handle'])
            previous.STATE['handle'] = None
            restored['tick_handle'] = True
        except Exception as error:
            restored['tick_handle'] = {{'error': repr(error)}}
    for key, name in {restore_items!r}:
        restore = getattr(previous, name, None)
        if restore is None:
            continue
        try:
            restored[key] = restore()
        except Exception as error:
            restored[key] = {{'error': repr(error)}}
    spec = importlib.util.spec_from_file_location('td_ragdoll_sampler', r'{path}')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    sys.modules['td_ragdoll_sampler'] = module
    _td_print({{'loaded': True, 'previous_restore': restored}})
_td_load()
del _td_load
'''

RESET_CODE = '''def _td_reset():
    world = _td_world()
    for actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.TDMonsterCharacter):
        actor.destroy_actor()
    for actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.TDDamageEntity):
        actor.destroy_actor()
    pawn = unreal.GameplayStatics.get_player_pawn(world, 0)
    combat = pawn.get_component_by_class(unreal.TDCombatComponent) if pawn else None
    if combat:
        combat.set_stats(combat.get_stats(), True)
    if '{species}':
        unreal.SystemLibrary.execute_console_command(world, 'TDSpawnMonster {species} {count} {distance}', unreal.GameplayStatics.get_player_controller(world, 0))
    _td_print({{'reset': True}})
_td_reset()
del _td_reset
'''

COUNT_CODE = '''def _td_count():
    world = _td_world()
    monsters = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.TDMonsterCharacter) if world else []
    _td_print({'monsters': [actor.get_species().get_name() if actor.get_species() else None for actor in monsters]})
_td_count()
del _td_count
'''

JOB_CODE = '''_td_print(sys.modules['td_ragdoll_sampler'].start_job({label!r}, {kind!r}, r{out!r}, {kill_fraction}, {kill_delay}, {observe}, {shots}, {force}, {assist!r}, {kill_others}, {scenario!r}))
'''

CLOSE_EDITORS_CODE = '''_td_print(sys.modules['td_ragdoll_sampler'].close_asset_editors())
'''

CVAR_CODE = '''def _td_cvar():
    world = _td_world()
    names = {names!r}
    before = {{name: unreal.SystemLibrary.get_console_variable_float_value(name) for name in names}}
    for name, value in {values!r}.items():
        unreal.SystemLibrary.execute_console_command(world, name + ' ' + str(value), None)
    _td_print({{'before': before, 'after': {{name: unreal.SystemLibrary.get_console_variable_float_value(name) for name in names}}}})
_td_cvar()
del _td_cvar
'''

OVERRIDE_CODE = '''_td_print(sys.modules['td_ragdoll_sampler'].apply_overrides({meshes!r}, {player}, {body!r}, {self_collision_off}, {species!r}))
'''

JOB_STATUS_CODE = '''_td_print(sys.modules['td_ragdoll_sampler'].status())
'''

RESTORE_CODE = '''def _td_restore():
    sampler = sys.modules.get('td_ragdoll_sampler')
    restored = {{}}
    for key, name in {restore_items!r}:
        try:
            restored[key] = getattr(sampler, name)()
        except Exception as error:
            restored[key] = {{'error': repr(error)}}
    _td_print(restored)
_td_restore()
del _td_restore
'''

STOP_CODE = '''def _td_stop():
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()
    _td_print({'stopped': True})
_td_stop()
del _td_stop
'''


def run_editor_code(body):
    code = PRELUDE.format(marker=RESULT_MARKER) + body
    completed = subprocess.run([sys.executable, str(RUN_IN_EDITOR), "-c", code], capture_output=True, text=True, encoding="utf-8", errors="replace", cwd=ROOT)
    for line in completed.stdout.splitlines():
        index = line.find(RESULT_MARKER)
        if index >= 0:
            return json.loads(line[index + len(RESULT_MARKER):])
    return {"error": completed.stdout[-600:] + completed.stderr[-300:]}


def wait_until(predicate, timeout_seconds, poll_seconds=0.4):
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        value = predicate()
        if value:
            return value
        time.sleep(poll_seconds)
    return None


def wait_for_job(minimum_seconds):
    started_at = time.monotonic()
    progressed_at = started_at
    last_game_seconds = None
    while True:
        job_status = run_editor_code(JOB_STATUS_CODE)
        if job_status.get("done"):
            return job_status
        now = time.monotonic()
        game_seconds = job_status.get("game_seconds")
        if game_seconds is not None and (last_game_seconds is None or game_seconds > last_game_seconds):
            last_game_seconds = game_seconds
            progressed_at = now
        if now - started_at >= minimum_seconds and now - progressed_at >= JOB_STALL_SECONDS:
            return dict(job_status, seconds_without_progress=round(now - progressed_at, 1))
        time.sleep(JOB_POLL_SECONDS)


def warn_unrestored_items(context, restored):
    for name, value in (restored or {}).items():
        if isinstance(value, dict) or name == "error":
            print(f"warning: {context}: {name} not restored {json.dumps(value, ensure_ascii=False)}")


def close_asset_editors():
    closed = run_editor_code(CLOSE_EDITORS_CODE)
    if "closed" not in closed or closed.get("failed"):
        print(f"warning: asset editors not fully closed, continuing {json.dumps(closed, ensure_ascii=False)}")
    elif closed["closed"]:
        print(json.dumps({"asset_editors_closed": closed["closed"]}, ensure_ascii=False))
    return closed


def start_pie(map_path, overrides, editor_closures):
    loaded = run_editor_code(LOAD_SAMPLER_CODE.format(path=SAMPLER_PATH.as_posix(), restore_items=SAMPLER_RESTORE_ITEMS))
    warn_unrestored_items("previous sampler restore", loaded.get("previous_restore"))
    if "loaded" not in loaded:
        return False
    editor_closures.append(close_asset_editors())
    if overrides:
        applied = run_editor_code(OVERRIDE_CODE.format(**overrides))
        print(json.dumps(applied, ensure_ascii=False))
        if "overridden" not in applied:
            return False
    if "error" in run_editor_code(START_CODE.format(level=map_path)):
        return False
    if not wait_until(lambda: run_editor_code(STATUS_CODE).get("pawn"), PIE_START_WAIT_SECONDS, 2.0):
        return False
    time.sleep(3.0)
    return True


def set_cvars(values):
    if not values:
        return {}
    values = {name: int(value) if float(value).is_integer() else value for name, value in values.items()}
    result = run_editor_code(CVAR_CODE.format(names=list(values), values=values))
    print(json.dumps({"cvar": result}, ensure_ascii=False))
    return result.get("before", {})


def stop_pie():
    warn_unrestored_items("PIE stop restore", run_editor_code(RESTORE_CODE.format(restore_items=SAMPLER_RESTORE_ITEMS)))
    run_editor_code(STOP_CODE)
    wait_until(lambda: not run_editor_code(STATUS_CODE).get("pie", True), 30.0, 1.0)


def list_shots():
    return {path.name: path for path in SHOT_DIRECTORY.glob("ScreenShot*.png")} if SHOT_DIRECTORY.exists() else {}


def collect_shots(before, expected, evidence_directory, stem):
    deadline = time.monotonic() + SHOT_WAIT_SECONDS
    fresh = []
    while time.monotonic() < deadline:
        fresh = sorted((path for name, path in list_shots().items() if name not in before), key=lambda path: path.stat().st_mtime)
        if len(fresh) >= expected and all(path.stat().st_size > 0 for path in fresh):
            break
        time.sleep(0.5)
    time.sleep(0.5)
    moved = []
    for index, path in enumerate(fresh, start=1):
        target = evidence_directory / f"{stem}_shot{index}.png"
        target.unlink(missing_ok=True)
        shutil.move(str(path), str(target))
        moved.append(target.relative_to(ROOT).as_posix())
    return moved


def is_written(path):
    return path.exists() and path.stat().st_size > 0


def wait_for_timed_shots(planned):
    deadline = time.monotonic() + SHOT_WAIT_SECONDS
    while planned and time.monotonic() < deadline and not all(is_written(Path(shot["path"])) for shot in planned):
        time.sleep(0.5)


def describe_timed_shots(planned):
    described = []
    for shot in planned:
        path = Path(shot["path"])
        described.append(dict(shot, path=path.relative_to(ROOT).as_posix() if is_written(path) else None))
    return described


def split_subjects(raw):
    split = []
    for index, subject_info in enumerate(raw["info"].get("subjects") or []):
        frames = []
        for frame in raw["frames"]:
            rows = frame.get("subjects") or []
            if index >= len(rows) or rows[index] is None:
                break
            frames.append(dict(rows[index], t=frame["t"], rt=frame["rt"], dilation=frame["dilation"]))
        ragdoll_time = next((frame["t"] for frame in frames if frame["sim"]), None)
        split.append({"label": raw["label"], "target_kind": raw["target_kind"], "result": raw["result"], "t_kill": raw["t_kill"], "rt_kill": raw.get("rt_kill"),
                      "t_ragdoll": ragdoll_time, "t_assist_sleep": None, "shots": [], "info": dict(subject_info, scenario=raw["info"].get("scenario", {})),
                      "frames": frames})
    return split


def analyze_with_subjects(raw, stem, details):
    if raw.get("rerun"):
        details = dict(details, rerun=raw["rerun"])
    result = analyze(raw)
    result.update(dict(details, target=stem, timed_shots=describe_timed_shots(raw.get("timed_shots") or [])))
    results = [result]
    for index, subject_raw in enumerate(split_subjects(raw), start=2):
        subject_result = analyze(subject_raw)
        subject_result.update(dict(details, target=f"{stem}.{index}", shots=[]))
        results.append(subject_result)
    return results


def target_stem(species_name, repeat_index):
    return f"{species_name or 'Player'}-{repeat_index}"


def run_target(arguments, kind, species_name, repeat_index, evidence_directory, raw_directory):
    stem = target_stem(species_name, repeat_index)
    raw_path = raw_directory / f"{stem}.json"
    raw_path.unlink(missing_ok=True)
    if kind == "monster":
        run_editor_code(RESET_CODE.format(species=species_name, count=arguments.count, distance=arguments.distance))
        if not wait_until(lambda: species_name and f"DA_TDMonster_{species_name}" in (run_editor_code(COUNT_CODE).get("monsters") or []), SPAWN_WAIT_SECONDS):
            return [{"target": stem, "error": "spawn not observed"}]
        time.sleep(SPAWN_SETTLE_SECONDS)
    else:
        run_editor_code(RESET_CODE.format(species=PLAYER_KILLER_SPECIES, count=1, distance=PLAYER_KILLER_DISTANCE))
        if not wait_until(lambda: f"DA_TDMonster_{PLAYER_KILLER_SPECIES}" in (run_editor_code(COUNT_CODE).get("monsters") or []), SPAWN_WAIT_SECONDS):
            return [{"target": stem, "error": "player killer spawn not observed"}]
    before = list_shots()
    shot_offsets = arguments.shots if repeat_index == 1 else []
    force = kind == "monster" and species_name in (arguments.force_species or [])
    scenario = build_scenario(arguments) if kind == "monster" else {}
    if repeat_index == 1 and arguments.screenshot_times:
        scenario["screenshots"] = {"times": sorted(arguments.screenshot_times), "directory": evidence_directory.as_posix(), "stem": stem,
                                   "clock": arguments.screenshot_clock, "fixed_fps": arguments.screenshot_fixed_fps}
    if arguments.camera_arm > 0.0:
        scenario["camera_arm_cm"] = arguments.camera_arm
    kill_delay = effective_kill_delay(arguments)
    started = run_editor_code(JOB_CODE.format(label=stem, kind="player" if kind == "player" else "monster", out=str(raw_path), kill_fraction=effective_kill_fraction(arguments),
                                              kill_delay=kill_delay, observe=arguments.observe, shots=shot_offsets, force=force, assist=parse_assist(arguments.assist),
                                              kill_others=arguments.count > 1 and not arguments.pile_gaps, scenario=scenario))
    if "started" not in started:
        return [{"target": stem, "error": f"job not started: {started}"}]
    job_status = wait_for_job(arguments.observe + kill_delay + scenario.get("kill_timeout", 0.0) + JOB_EXTRA_WAIT_SECONDS)
    if not job_status.get("done") or not raw_path.exists():
        return [{"target": stem, "error": f"job did not finish: {job_status}"}]
    shots = collect_shots(before, len(shot_offsets), evidence_directory, stem) if shot_offsets else []
    raw = json.loads(raw_path.read_text(encoding="utf-8"))
    warn_unrestored_items(f"{stem} job restore", raw["info"].get("restored"))
    wait_for_timed_shots(raw.get("timed_shots") or [])
    return analyze_with_subjects(raw, stem, {"kind": kind, "forced_ragdoll": force, "shots": shots})


def pre_kill_frame_p50_ms(results):
    frame_time = (results[0].get("frame_time") or {}) if results else {}
    p50_values = [block["p50"] for block in (frame_time.get("before_ms"), frame_time.get("before_advancing_ms")) if block]
    return max(p50_values) if p50_values else None


def mark_rerun(results, rerun):
    for result in results:
        result["rerun"] = rerun
    return results


def record_rerun_in_raw(raw_path, rerun):
    if not raw_path.exists():
        return
    raw = json.loads(raw_path.read_text(encoding="utf-8"))
    raw["rerun"] = rerun
    raw_path.write_text(json.dumps(raw), encoding="utf-8")


def measure_target(arguments, kind, species_name, repeat_index, evidence_directory, raw_directory, prepare_rerun):
    results = run_target(arguments, kind, species_name, repeat_index, evidence_directory, raw_directory)
    load_ms = pre_kill_frame_p50_ms(results)
    limit_ms = arguments.auto_rerun_load_ms
    if limit_ms <= 0.0 or load_ms is None or load_ms <= limit_ms:
        return results
    stem = target_stem(species_name, repeat_index)
    raw_path = raw_directory / f"{stem}.json"
    loaded_path = raw_directory / LOAD_RERUN_DIRECTORY / raw_path.name
    loaded_path.parent.mkdir(parents=True, exist_ok=True)
    loaded_path.unlink(missing_ok=True)
    shutil.move(str(raw_path), str(loaded_path))
    rerun = {"target": stem, "reason": f"pre-kill 1s frame p50 {load_ms:.1f}ms > {limit_ms:g}ms (load contamination)", "pre_kill_frame_p50_ms": load_ms,
             "limit_ms": limit_ms, "first_frame_time": results[0]["frame_time"], "first_raw": loaded_path.relative_to(ROOT).as_posix()}
    print(f"rerun: {stem} {rerun['reason']}")
    rerun_results = run_target(arguments, kind, species_name, repeat_index, evidence_directory, raw_directory) if prepare_rerun() else [{"error": "PIE restart for rerun failed"}]
    if "error" in rerun_results[0]:
        raw_path.unlink(missing_ok=True)
        shutil.move(str(loaded_path), str(raw_path))
        rerun.update({"rerun_error": rerun_results[0]["error"], "first_raw": raw_path.relative_to(ROOT).as_posix()})
        print(f"warning: {stem} rerun failed, keeping the loaded first measurement: {rerun['rerun_error']}")
        record_rerun_in_raw(raw_path, rerun)
        return mark_rerun(results, rerun)
    rerun_load_ms = pre_kill_frame_p50_ms(rerun_results)
    rerun.update({"rerun_pre_kill_frame_p50_ms": rerun_load_ms, "still_over_limit": rerun_load_ms is not None and rerun_load_ms > limit_ms})
    record_rerun_in_raw(raw_path, rerun)
    return mark_rerun(rerun_results, rerun)


def effective_kill_fraction(arguments):
    return WEAK_KILL_FRACTION if arguments.kill_strength == "weak" else arguments.kill_fraction


def effective_kill_delay(arguments):
    if arguments.count > 1:
        return max(arguments.kill_delay, FRAME_TIME_BEFORE_SECONDS + KILL_DELAY_MARGIN_SECONDS)
    return arguments.kill_delay


def build_scenario(arguments):
    scenario = {}
    if arguments.flight_hold is not None:
        scenario.update({"flight_hold": arguments.flight_hold, "kill_timeout": arguments.kill_timeout})
    if arguments.freeze:
        scenario["freeze"] = True
    if arguments.pile_spacing_scale:
        scenario["pile_spacing_scale"] = arguments.pile_spacing_scale
    if arguments.pile_gaps:
        scenario["pile_gaps"] = sorted(arguments.pile_gaps)
    if arguments.spread:
        scenario["spread_cm"] = list(arguments.spread)
    if arguments.kill_strength:
        scenario["kill_strength"] = arguments.kill_strength
    if arguments.critical:
        scenario["critical"] = True
    if arguments.frame_time_only:
        scenario["frame_time_only"] = True
    elif arguments.count > 1 and not arguments.pile_gaps:
        scenario["measure_all"] = True
    if arguments.emulate_launch_clamp:
        seconds, relative_cm_s, angular_deg_s = (float(value) for value in arguments.emulate_launch_clamp.split(","))
        scenario["emulate_launch_clamp"] = {"seconds": seconds, "relative_cm_s": relative_cm_s, "angular_deg_s": angular_deg_s}
    if arguments.emulate_firm_damping:
        start, extra_damping = (float(value) for value in arguments.emulate_firm_damping.split(","))
        scenario["emulate_firm_damping"] = {"start": start, "extra_damping": extra_damping}
    return scenario


def describe_screenshot_settings(arguments):
    if not arguments.screenshot_times:
        return None
    return {"times": sorted(arguments.screenshot_times), "clock": arguments.screenshot_clock, "unit": "game seconds",
            "kill_stop_included": arguments.screenshot_clock == "kill", "fixed_step_console_variable": FIXED_STEP_VARIABLE,
            "fixed_fps": arguments.screenshot_fixed_fps or None, "first_repeat_only": True}


def parse_cvars(pairs):
    values = {}
    for pair in pairs or []:
        name, value = pair.split("=", 1)
        values[name] = float(value) if "." in value else int(value)
    return values


def parse_assist(text):
    if not text:
        return None
    min_seconds, speed_cm_s, hold_seconds = (float(value) for value in text.split(","))
    return {"min_seconds": min_seconds, "speed_cm_s": speed_cm_s, "hold_seconds": hold_seconds}


def parse_seconds_list(text):
    return [float(value) for value in text.split(",") if value.strip()]


def parse_distance_range(text):
    values = [float(value) for value in text.split(",") if value.strip()]
    if len(values) != 2 or values[0] <= 0.0 or values[0] > values[1]:
        raise argparse.ArgumentTypeError("MIN,MAX cm with 0 < MIN <= MAX, e.g. '250,450'")
    return values


def parse_body_values(pairs):
    values = {}
    for pair in pairs or []:
        name, value = pair.split("=", 1)
        if value.lower() in ("true", "false"):
            values[name] = value.lower() == "true"
            continue
        try:
            values[name] = int(value) if value.isdigit() else float(value)
        except ValueError:
            values[name] = value
    return values


def build_overrides(arguments, species_names):
    body = parse_body_values(arguments.body)
    if not body and not arguments.self_collision_off:
        return None
    table = json.loads(SPECIES_TABLE_PATH.read_text(encoding="utf-8"))
    meshes = sorted({entry["mesh"] for entry in table["species"] if entry["name"] in species_names})
    return {"meshes": meshes, "player": not arguments.no_player, "body": body, "self_collision_off": arguments.self_collision_off, "species": sorted(species_names)}


def length(vector):
    return math.sqrt(sum(component * component for component in vector))


def difference(first, second):
    return [a - b for a, b in zip(first, second)]


def cross(first, second):
    return [first[1] * second[2] - first[2] * second[1], first[2] * second[0] - first[0] * second[2], first[0] * second[1] - first[1] * second[0]]


def planar_distance(first, second):
    return math.hypot(first[0] - second[0], first[1] - second[1])


def angle_to_up_degrees(vector):
    size = length(vector)
    if size < 1e-6:
        return None
    return math.degrees(math.acos(max(-1.0, min(1.0, vector[2] / size))))


def mass_weighted(frame, masses, slot):
    total_mass = max(sum(masses), 1e-6)
    return [sum(mass * row[slot][axis] for mass, row in zip(masses, frame["bones"])) / total_mass for axis in range(3)]


def first_index_at(frames, time_seconds):
    return next((index for index, frame in enumerate(frames) if frame["t"] >= time_seconds), None)


def percentile(values, fraction):
    ordered = sorted(values)
    return ordered[max(0, math.ceil(fraction * len(ordered)) - 1)]


@functools.lru_cache(maxsize=1)
def species_table_entries():
    if not SPECIES_TABLE_PATH.exists():
        return {}
    table = json.loads(SPECIES_TABLE_PATH.read_text(encoding="utf-8"))
    return {entry["name"]: entry for entry in table["species"]}


def species_entry(info):
    return species_table_entries().get((info.get("species") or "").replace("DA_TDMonster_", ""), {})


def resolve_horizontal_body_plan(info):
    if info.get("horizontal_body_plan") is not None:
        return bool(info["horizontal_body_plan"])
    return species_entry(info).get("body_plan") == "horizontal"


def resolve_rest_height_ratio(info, is_horizontal):
    ratio = info.get("corpse_rest_height_ratio")
    if ratio is None:
        ratio = species_entry(info).get("corpse_rest_height_ratio", 0.0)
    if ratio and ratio > 0.0:
        return ratio
    return HORIZONTAL_REST_HEIGHT_RATIO if is_horizontal else UPRIGHT_REST_HEIGHT_RATIO


def head_body_index(bones, reference):
    names = [bone.lower() for bone in bones]
    for key in ("head", "neck"):
        index = next((position for position, name in enumerate(names) if key in name), None)
        if index is not None:
            return index
    return max(range(len(bones)), key=lambda position: reference["bones"][position][1][2])


def torso_tip_degrees(reference, frame):
    angles = []
    for (forward_start, up_start), (forward, up) in zip(reference.get("torso") or [], frame.get("torso") or []):
        side_start = cross(up_start, forward_start)
        side = cross(up, forward)
        carried_up = [forward_start[2] * forward[axis] + side_start[2] * side[axis] + up_start[2] * up[axis] for axis in range(3)]
        angle = angle_to_up_degrees(carried_up)
        if angle is not None:
            angles.append(angle)
    return sum(angles) / len(angles) if angles else None


def body_angle_degrees(frame, reference, is_horizontal, head_index):
    if is_horizontal:
        angle = torso_tip_degrees(reference, frame)
    else:
        angle = angle_to_up_degrees(difference(frame["bones"][head_index][1], frame["bones"][0][1]))
    return round(angle, 1) if angle is not None else None


def body_angle_at(after, time_seconds, reference, is_horizontal, head_index):
    index = first_index_at(after, time_seconds)
    return body_angle_degrees(after[index], reference, is_horizontal, head_index) if index is not None else None


def detect_landing(after, centers, velocities, ragdoll_time, height_gate, fall_seconds):
    lowest_vertical_speed = 0.0
    for index, frame in enumerate(after):
        elapsed = frame["t"] - ragdoll_time
        is_within_fallback_height = centers[index][2] <= height_gate - LANDING_HEIGHT_MARGIN_CM + LANDING_FALLBACK_HEIGHT_MARGIN_CM
        if elapsed >= LANDING_ABANDON_SECONDS:
            return None, "abandoned"
        if elapsed >= LANDING_OVERDUE_SECONDS:
            if is_within_fallback_height:
                return index, "overdue"
            continue
        if elapsed >= fall_seconds + LANDING_FALLBACK_EXTRA_SECONDS and is_within_fallback_height:
            return index, "fallback"
        vertical_speed = velocities[index][2]
        lowest_vertical_speed = min(lowest_vertical_speed, vertical_speed)
        if lowest_vertical_speed > LANDING_MIN_FALL_SPEED_CM_S:
            continue
        if vertical_speed - lowest_vertical_speed < max(LANDING_REBOUND_MIN_SPEED_CM_S, LANDING_REBOUND_FRACTION * abs(lowest_vertical_speed)):
            continue
        if centers[index][2] <= height_gate:
            return index, "rule"
    return None, None


def landing_torso_indices(bones):
    return [index for index, bone in enumerate(bones) if "pelvis" in bone.lower() or "spine" in bone.lower()]


def torso_roll_degrees_per_second(frame, torso_indices, masses):
    axis = difference(frame["bones"][torso_indices[-1]][1], frame["bones"][torso_indices[0]][1])
    axis_length = length(axis)
    torso_mass = sum(masses[index] for index in torso_indices)
    if axis_length < 1e-6 or torso_mass <= 0.0:
        return None
    weighted = sum(masses[index] * sum(frame["bones"][index][3][axis_index] * axis[axis_index] for axis_index in range(3)) for index in torso_indices)
    return weighted / (torso_mass * axis_length)


def bite_torso_roll_max(after, land_time, bones, masses):
    torso_indices = landing_torso_indices(bones)
    if len(torso_indices) < 2:
        return None
    speeds = [torso_roll_degrees_per_second(frame, torso_indices, masses) for frame in after if land_time < frame["t"] <= land_time + LANDING_BITE_SECONDS]
    speeds = [abs(speed) for speed in speeds if speed is not None]
    return round(max(speeds), 1) if speeds else None


def direction_metrics(after, velocities, ragdoll_time, attack_direction):
    index = first_index_at(after, ragdoll_time + DIRECTION_SAMPLE_SECONDS)
    if index is None or not attack_direction:
        return {"launch_direction_error_deg": None}
    velocity = velocities[index]
    planar_speed = math.hypot(velocity[0], velocity[1])
    reference_length = math.hypot(attack_direction[0], attack_direction[1])
    if planar_speed < 1.0 or reference_length < 1e-6:
        return {"launch_direction_error_deg": None, "launch_planar_speed_cm_s": round(planar_speed, 1)}
    cosine = (velocity[0] * attack_direction[0] + velocity[1] * attack_direction[1]) / (planar_speed * reference_length)
    return {"launch_direction_error_deg": round(math.degrees(math.acos(max(-1.0, min(1.0, cosine)))), 1), "launch_planar_speed_cm_s": round(planar_speed, 1)}


def landing_metrics(raw, after, centers, velocities, ground, half_height, is_horizontal, sleep_time, sink_time):
    info = raw["info"]
    ragdoll_time = raw["t_ragdoll"]
    reference = raw["frames"][0]
    rest_ratio = resolve_rest_height_ratio(info, is_horizontal)
    rest_height = rest_ratio * half_height
    drop_height = max(MIN_DROP_HEIGHT_CM, centers[0][2] - ground - rest_height)
    fall_seconds = (LAUNCH_UP_SPEED_CM_S + math.sqrt(LAUNCH_UP_SPEED_CM_S ** 2 + 2.0 * EFFECTIVE_GRAVITY_CM_S2 * drop_height)) / EFFECTIVE_GRAVITY_CM_S2
    land_index, land_rule = detect_landing(after, centers, velocities, ragdoll_time, ground + rest_height + LANDING_HEIGHT_MARGIN_CM, fall_seconds)
    contact_index = min((index for index, frame in enumerate(after) if frame["t"] <= ragdoll_time + CONTACT_SEARCH_SECONDS), key=lambda index: velocities[index][2])
    head_index = head_body_index(info["bones"], reference)
    standing_height = mass_weighted(reference, info["bone_mass_kg"], 1)[2] - ground
    metrics = {
        "rest_height_ratio_used": round(rest_ratio, 3), "drop_height_cm": round(drop_height, 1), "expected_fall_seconds": round(fall_seconds, 4),
        "lowest_fall_speed_seconds": round(after[contact_index]["t"] - ragdoll_time, 4), "landing_angle_kind": "horizontal_torso_roll" if is_horizontal else "upright_pelvis_head",
        "standing_com_height_cm": round(standing_height, 1), "standing_com_ratio": round(standing_height / half_height, 3),
        "land_detect_seconds": None,
        "instant_angle_deg": {f"{seconds:.1f}": body_angle_at(after, ragdoll_time + seconds, reference, is_horizontal, head_index) for seconds in INSTANT_ANGLE_SECONDS},
    }
    if land_index is not None:
        land_time = after[land_index]["t"]
        end_time = max(land_time, min(value for value in (land_time + SLIDE_SECONDS, sleep_time, sink_time) if value is not None))
        end_index = max(index for index, frame in enumerate(after) if frame["t"] <= end_time)
        metrics.update({
            "land_detect_seconds": round(land_time - ragdoll_time, 4), "land_detect_rule": land_rule,
            "fallback_before_lowest_fall_speed": land_rule == "fallback" and land_index < contact_index,
            "post_land_slide_cm": round(planar_distance(centers[land_index], centers[end_index]), 1),
            "landing_angle_deg": body_angle_degrees(after[land_index], reference, is_horizontal, head_index),
        })
        if is_horizontal:
            metrics["bite_torso_roll_max_deg_s"] = bite_torso_roll_max(after, land_time, info["bones"], info["bone_mass_kg"])
    rest_index = first_index_at(after, sleep_time) if sleep_time is not None else None
    if rest_index is not None:
        rest_com_height = centers[rest_index][2] - ground
        metrics.update({"rest_com_height_cm": round(rest_com_height, 1), "rest_height_ratio": round(rest_com_height / half_height, 3),
                        "rest_angle_deg": body_angle_degrees(after[rest_index], reference, is_horizontal, head_index)})
    return metrics


def launch_metrics(raw, after, sleep_time, sink_time):
    info = raw["info"]
    masses = info["bone_mass_kg"]
    geometry = info.get("geometry") or {}
    ragdoll_time = raw["t_ragdoll"]
    is_horizontal = resolve_horizontal_body_plan(info)
    centers = [mass_weighted(frame, masses, 1) for frame in after]
    velocities = [mass_weighted(frame, masses, 2) for frame in after]
    relative = [round(max(length(difference(row[2], velocity)) for row in frame["bones"]), 1) for frame, velocity in zip(after[:FIRST_FRAME_COUNT], velocities)]
    metrics = {"body_plan": "horizontal" if is_horizontal else "upright", "launch_up_speed_cm_s": round(velocities[0][2], 1),
               "first_frame_relative_speed_cm_s": relative[0], "relative_speed_first_frames_cm_s": relative}
    if raw.get("rt_kill") is not None:
        metrics["ragdoll_delay_real_seconds"] = round(after[0]["rt"] - raw["rt_kill"], 4)
    metrics.update(direction_metrics(after, velocities, ragdoll_time, geometry.get("attack_direction")))
    travel_index = first_index_at(after, ragdoll_time + TRAVEL_SECONDS)
    metrics["com_travel_2s_cm"] = round(planar_distance(centers[0], centers[travel_index]), 1) if travel_index is not None else None
    half_height = geometry.get("capsule_half_height_cm")
    ground = geometry.get("ground_z")
    if ground is None or not half_height:
        return metrics
    metrics.update(landing_metrics(raw, after, centers, velocities, ground, half_height, is_horizontal, sleep_time, sink_time))
    return metrics


def frame_time_metrics(raw):
    clock = raw.get("frame_clock") or []
    kill_real = raw.get("rt_kill")
    if kill_real is None or len(clock) < 2:
        return None
    before, after, sampler = [], [], []
    for previous, current in zip(clock, clock[1:]):
        delta_ms = (current[0] - previous[0]) * 1000.0
        if kill_real - FRAME_TIME_BEFORE_SECONDS <= previous[0] < kill_real:
            before.append(delta_ms)
        elif kill_real <= previous[0] < kill_real + FRAME_TIME_AFTER_SECONDS:
            after.append(delta_ms)
            sampler.append(current[1])
    metrics = {"before_covered_seconds": round(kill_real - max(clock[0][0], kill_real - FRAME_TIME_BEFORE_SECONDS), 3)}
    advancing = [value for value in before if value > 0.0]
    for name, values in (("before_ms", before), ("before_advancing_ms", advancing), ("after_ms", after), ("after_sampler_ms", sampler)):
        metrics[name] = {"p50": round(percentile(values, 0.5), 3), "p95": round(percentile(values, 0.95), 3), "frames": len(values)} if values else None
    metrics["before_stalled_ticks"] = len(before) - len(advancing)
    if metrics["before_ms"] and metrics["after_ms"]:
        metrics["p95_increase_ms"] = round(metrics["after_ms"]["p95"] - metrics["before_ms"]["p95"], 3)
    return metrics


def position_metrics(window, slot):
    bone_count = len(window[0]["bones"])
    per_bone_rms = []
    step_squares = []
    high_frequency_squares = []
    for bone_index in range(bone_count):
        positions = [frame["bones"][bone_index][slot] for frame in window]
        mean = [sum(position[axis] for position in positions) / len(positions) for axis in range(3)]
        per_bone_rms.append(math.sqrt(sum(length(difference(position, mean)) ** 2 for position in positions) / len(positions)))
        for index in range(1, len(positions)):
            step_squares.append(length(difference(positions[index], positions[index - 1])) ** 2)
        for index in range(1, len(positions) - 1):
            midpoint = [(positions[index - 1][axis] + positions[index + 1][axis]) * 0.5 for axis in range(3)]
            high_frequency_squares.append(length(difference(positions[index], midpoint)) ** 2)
    return {
        "position_rms_cm": round(math.sqrt(sum(value * value for value in per_bone_rms) / bone_count), 4),
        "step_rms_cm": round(math.sqrt(sum(step_squares) / max(len(step_squares), 1)), 4),
        "high_frequency_rms_cm": round(math.sqrt(sum(high_frequency_squares) / max(len(high_frequency_squares), 1)), 4),
        "per_bone_rms_cm": per_bone_rms,
    }


def bone_revision_metrics(frames, ragdoll_time, sleep_time, sink_time):
    recorded = [frame for frame in frames if frame["t"] >= ragdoll_time and frame.get("rev") is not None]
    pairs = [(previous, current) for previous, current in zip(recorded, recorded[1:]) if current["t"] > previous["t"]]
    if not pairs:
        return None
    window_end = sleep_time if sleep_time is not None else sink_time
    awake = [(previous, current) for previous, current in pairs if previous["awake"] and (window_end is None or current["t"] <= window_end)]
    unchanged = [current for previous, current in awake if current["rev"] <= previous["rev"]]
    asleep = [(previous, current) for previous, current in pairs if sleep_time is not None and previous["t"] >= sleep_time
              and (sink_time is None or current["t"] < sink_time) and not previous["awake"] and not current["awake"]]
    return {
        "awake_frames": len(awake),
        "increased_frames": len(awake) - len(unchanged),
        "increase_fraction": round((len(awake) - len(unchanged)) / len(awake), 4) if awake else None,
        "first_unchanged_seconds": round(unchanged[0]["t"] - ragdoll_time, 4) if unchanged else None,
        "asleep_increased_frames": sum(1 for previous, current in asleep if current["rev"] > previous["rev"]),
    }


def find_sleep_time(frames, ragdoll_time, sink_time):
    candidates = [frame for frame in frames if frame["t"] >= ragdoll_time and (sink_time is None or frame["t"] < sink_time)]
    sleep_time = None
    for frame in candidates:
        is_asleep = frame["awake"] == 0
        if is_asleep and sleep_time is None:
            sleep_time = frame["t"]
        elif not is_asleep:
            sleep_time = None
    return sleep_time


def measure_freeze(frames, ragdoll_time):
    after = [frame for frame in frames if frame["t"] >= ragdoll_time]
    index = next((position for position in range(1, len(after)) if after[position]["awake"] == 0 and after[position - 1]["awake"] == 1), None)
    if index is None:
        return {"first_sleep_seconds": None}
    last_awake = after[index - 1]
    sleep_time = after[index]["t"]
    earlier = next((frame for frame in after if frame["t"] >= sleep_time - FREEZE_LOOKBACK_SECONDS), last_awake)
    moves = [length(difference(last_awake["bones"][bone][0], earlier["bones"][bone][0])) for bone in range(len(last_awake["bones"]))]
    return {
        "first_sleep_seconds": round(sleep_time - ragdoll_time, 4),
        "freeze_body_speed_cm_s": round(max(length(row[2]) for row in last_awake["bones"]), 1),
        "pre_sleep_move_cm": round(max(moves), 2),
    }


def sleep_transitions(frames, start_time, end_time, awake_of, speed_of):
    events = []
    wakes = []
    previous = None
    for frame in frames:
        if frame["t"] < start_time or (end_time is not None and frame["t"] >= end_time):
            continue
        awake = awake_of(frame)
        if previous is not None and previous[0] == 1 and awake == 0:
            events.append([round(frame["t"] - start_time, 3), round(previous[1], 1)])
        if previous is not None and previous[0] == 0 and awake == 1:
            wakes.append(round(frame["t"] - start_time, 3))
        previous = (awake, speed_of(frame))
    return events, wakes


def summarize_sleep(events, wakes, final_sleep_seconds):
    rewake_settle = []
    for wake in wakes:
        later = [event[0] for event in events if event[0] > wake]
        rewake_settle.append(round(later[0] - wake, 3) if later else None)
    max_freeze = max((event[1] for event in events), default=None)
    first_sleep = events[0][0] if events else None
    return {
        "sleep_events": events, "rewake_seconds": wakes, "rewake_settle_seconds": rewake_settle,
        "max_freeze_body_speed_cm_s": max_freeze,
        "meets_sleep_target": bool(final_sleep_seconds is not None and first_sleep is not None and first_sleep <= TARGET_SLEEP_SECONDS + SLEEP_CHECK_TOLERANCE_SECONDS
                                   and max_freeze <= TARGET_FREEZE_BODY_SPEED_CM_S
                                   and all(value is not None and value <= TARGET_SLEEP_SECONDS + SLEEP_CHECK_TOLERANCE_SECONDS for value in rewake_settle)),
    }


def first_ragdoll_frame_of_other(frames, index):
    return next((frame for frame in frames if len(frame.get("others") or []) > index and frame["others"][index][0]), None)


def multi_kill_metrics(raw):
    frames = raw["frames"]
    info = raw["info"]
    if not info.get("others_killed") or not frames:
        return None
    others = [first_ragdoll_frame_of_other(frames, index) for index in range(len(frames[0].get("others") or []))]
    target = next((frame for frame in frames if frame["sim"]), None)
    starts = [frame for frame in others + [target] if frame is not None]
    metrics = {"monsters": len(others) + 1, "ragdoll_missing": len(others) + 1 - len(starts), "order": "kill order: others then target",
               "ragdoll_start_after_kill_seconds": [round(frame["t"] - raw["t_kill"], 4) if frame else None for frame in others + [target]]}
    if raw.get("rt_kill") is not None:
        metrics["ragdoll_start_after_kill_real_seconds"] = [round(frame["rt"] - raw["rt_kill"], 4) if frame else None for frame in others + [target]]
    if info.get("spread_placed"):
        metrics["kill_distances_cm"] = [placed["distance_cm"] for placed in info["spread_placed"]]
    if len(starts) < 2:
        return metrics
    metrics["ragdoll_start_spread_seconds"] = round(max(frame["t"] for frame in starts) - min(frame["t"] for frame in starts), 4)
    if raw.get("rt_kill") is not None:
        metrics["ragdoll_start_spread_real_seconds"] = round(max(frame["rt"] for frame in starts) - min(frame["rt"] for frame in starts), 4)
    return metrics


def analyze_others(frames, t_kill, observe_end):
    if not frames or not frames[0].get("others"):
        return []
    results = []
    for index in range(len(frames[0]["others"])):
        rows = [(frame["t"], frame["others"][index]) for frame in frames if len(frame.get("others", [])) > index]
        ragdoll = next((time for time, row in rows if row[0]), None)
        if ragdoll is None:
            results.append({"index": index, "ragdoll": False})
            continue
        sink = next((time for time, row in rows if len(row) > 3 and row[3] and time > ragdoll), None)
        own = [{"t": time, "awake": row[1], "speed": row[2]} for time, row in rows if time >= ragdoll]
        events, wakes = sleep_transitions(own, ragdoll, sink, lambda frame: frame["awake"], lambda frame: frame["speed"])
        final_sleep = find_sleep_time(own, ragdoll, sink)
        final_sleep_seconds = round(final_sleep - ragdoll, 3) if final_sleep is not None else None
        result = {"index": index, "ragdoll_after_target_kill_seconds": round(ragdoll - t_kill, 3), "sleep_seconds": final_sleep_seconds,
                  "observed_seconds": round((sink or observe_end) - ragdoll, 3)}
        result.update(summarize_sleep(events, wakes, final_sleep_seconds))
        results.append(result)
    return results


def analyze(raw):
    frames = raw["frames"]
    info = raw["info"]
    ragdoll_time = raw["t_ragdoll"]
    summary = {"result": raw["result"], "species": info["species"], "physics_asset": info["physics_asset"], "body_count": len(info["bones"]),
               "bone_mass_kg": dict(zip(info["bones"], info["bone_mass_kg"])), "frames": len(frames)}
    for key in ("fixed_step", "camera", "restored"):
        if info.get(key):
            summary[key] = info[key]
    frame_time = None if info.get("fixed_step") else frame_time_metrics(raw)
    if frame_time:
        summary["frame_time"] = frame_time
    if ragdoll_time is None or not frames or not info["bones"]:
        summary["ragdoll"] = False
        return summary
    summary["ragdoll"] = True
    summary["ragdoll_delay_seconds"] = round(ragdoll_time - raw["t_kill"], 4)
    for key in ("at_kill", "geometry", "forced_immediate_ragdoll", "freeze_applied", "pile_spacing_cm", "pile_placed", "pile_dropped_seconds", "others_killed", "spread_placed"):
        if key in info:
            summary[key] = info[key]
    multi_kill = multi_kill_metrics(raw)
    if multi_kill:
        summary["multi_kill"] = multi_kill
    if info.get("scenario", {}).get("frame_time_only"):
        return summary
    after = [frame for frame in frames if frame["t"] >= ragdoll_time]
    summary["launch_speed_cm_s"] = round(max(length(row[2]) for row in after[min(1, len(after) - 1)]["bones"]), 1)
    land_time = next((frame["t"] for frame in after if frame["t"] >= ragdoll_time + LAND_MIN_SECONDS
                      and max(length(row[2]) for row in frame["bones"]) < LAND_SPEED_CM_S), None)
    land_time = min(land_time, ragdoll_time + LAND_MAX_SECONDS) if land_time is not None else ragdoll_time + LAND_MAX_SECONDS
    sink_time = next((frame["t"] for frame in frames if frame["sink"]), None)
    window_start = land_time + WINDOW_DELAY_SECONDS
    window_end = window_start + WINDOW_LENGTH_SECONDS
    if sink_time is not None:
        window_end = min(window_end, sink_time)
    window = [frame for frame in frames if window_start <= frame["t"] <= window_end]
    observed_seconds = frames[-1]["t"] - ragdoll_time
    sleep_time = find_sleep_time(frames, ragdoll_time, sink_time)
    summary.update(measure_freeze(frames, ragdoll_time))
    summary.update({
        "land_seconds": round(land_time - ragdoll_time, 4),
        "sink_seconds": round(sink_time - ragdoll_time, 4) if sink_time is not None else None,
        "window_seconds": [round(window_start - ragdoll_time, 4), round(window_end - ragdoll_time, 4)],
        "observed_seconds": round(observed_seconds, 4),
        "sleep_seconds": round(sleep_time - ragdoll_time, 4) if sleep_time is not None else None,
        "fps": round((len(frames) - 1) / max(frames[-1]["rt"] - frames[0]["rt"], 1e-6), 1),
        "min_time_dilation": min(frame["dilation"] for frame in frames),
        "assist_sleep_seconds": round(raw["t_assist_sleep"] - ragdoll_time, 4) if raw.get("t_assist_sleep") is not None else None,
    })
    ragdoll_frame = after[0]
    sleep_frame = next((frame for frame in after if sleep_time is not None and frame["t"] >= sleep_time), None)
    slowed = [frame for frame in after if frame["dilation"] < 0.999]
    summary.update({
        "sleep_real_seconds": round(sleep_frame["rt"] - ragdoll_frame["rt"], 4) if sleep_frame else None,
        "ragdoll_in_slowmo": any(frame["t"] <= ragdoll_time + 1.0 for frame in slowed),
        "slowmo_after_ragdoll_real_seconds": round(slowed[-1]["rt"] - slowed[0]["rt"], 3) if slowed else 0.0,
        "min_actor_dilation_after_ragdoll": min((frame.get("actor_dilation", 1.0) for frame in after), default=None),
    })
    events, wakes = sleep_transitions(frames, ragdoll_time, sink_time, lambda frame: frame["awake"], lambda frame: max(length(row[2]) for row in frame["bones"]))
    summary.update(summarize_sleep(events, wakes, summary["sleep_seconds"]))
    summary["others"] = analyze_others(frames, raw["t_kill"], frames[-1]["t"])
    summary["launch"] = launch_metrics(raw, after, sleep_time, sink_time)
    summary["bone_revision"] = bone_revision_metrics(frames, ragdoll_time, sleep_time, sink_time)
    if len(window) < 3 or window[-1]["t"] - window[0]["t"] < MIN_WINDOW_SECONDS:
        summary["window_short"] = True
    if len(window) < 3:
        return summary
    render = position_metrics(window, 0)
    physics = position_metrics(window, 1)
    worst = sorted(zip(info["bones"], render.pop("per_bone_rms_cm")), key=lambda item: -item[1])[:4]
    physics.pop("per_bone_rms_cm")
    component_steps = [length(difference(window[index]["component"], window[index - 1]["component"])) for index in range(1, len(window))]
    summary.update({
        "render": render,
        "physics": physics,
        "worst_bones_render_rms_cm": [[bone, round(value, 4)] for bone, value in worst],
        "component_step_rms_cm": round(math.sqrt(sum(step * step for step in component_steps) / len(component_steps)), 4),
        "max_angular_deg_s": round(max(length(row[3]) for frame in window for row in frame["bones"]), 2),
        "max_linear_cm_s": round(max(length(row[2]) for frame in window for row in frame["bones"]), 2),
        "awake_fraction": round(sum(1 for frame in window if frame["awake"]) / len(window), 3),
    })
    summary["meets_target"] = bool(render["position_rms_cm"] <= TARGET_POSITION_RMS_CM and summary["sleep_seconds"] is not None
                                   and summary["sleep_seconds"] <= TARGET_SLEEP_SECONDS
                                   and summary.get("freeze_body_speed_cm_s", 0.0) <= TARGET_FREEZE_BODY_SPEED_CM_S)
    return summary


def print_table(results):
    print("target | ragdoll | launch | land | render rms | hf rms | max ang | sleep | sleep real | sleeps | max freeze | rewake settle | slowmo | others sleep/max freeze | sleep target | target")
    for result in results:
        if "render" not in result:
            print(f"{result.get('target')} | {result.get('error') or result.get('result')} | ragdoll={result.get('ragdoll')} window_short={result.get('window_short')}")
            continue
        others = " ".join(f"{other.get('sleep_seconds')}/{other.get('max_freeze_body_speed_cm_s')}" for other in result.get("others", []))
        print(f"{result['target']} | {result['ragdoll_delay_seconds']} | {result['launch_speed_cm_s']} | {result['land_seconds']} | "
              f"{result['render']['position_rms_cm']} | {result['render']['high_frequency_rms_cm']} | {result['max_angular_deg_s']} | "
              f"{result['sleep_seconds']} | {result.get('sleep_real_seconds')} | {len(result.get('sleep_events', []))} | {result.get('max_freeze_body_speed_cm_s')} | "
              f"{result.get('rewake_settle_seconds')} | {result.get('ragdoll_in_slowmo')} | {others or '-'} | {result.get('meets_sleep_target')} | {result['meets_target']}")


def join_values(values):
    return "/".join("–" if value is None else (f"{value:.2f}".rstrip("0").rstrip(".") if isinstance(value, float) else str(value)) for value in values)


def describe_land_detect(launch):
    if launch.get("land_detect_seconds") is None:
        return None
    return f"{launch['land_detect_seconds']:.2f}{'*' if launch.get('land_detect_rule') == 'fallback' else ''}"


def describe_frame_time(frame_time):
    if not frame_time or not frame_time.get("before_ms") or not frame_time.get("after_ms"):
        return None
    before, after = frame_time["before_ms"], frame_time["after_ms"]
    return f"{before['p50']:.1f}/{before['p95']:.1f}→{after['p50']:.1f}/{after['p95']:.1f}"


def describe_kill_style(results):
    at_kill = next((result["at_kill"] for result in results if result.get("at_kill")), {})
    return f"{at_kill.get('kill_strength') or '기본'}{'·치명' if at_kill.get('critical') else ''}"


def describe_group_name(name, results, failed=0):
    reruns = sum(1 for result in results if (result.get("rerun") or {}).get("target") == result.get("target"))
    return f"{name}{f' (실패 {failed})' if failed else ''}{f' (부하 재측정 {reruns})' if reruns else ''}"


def describe_values(values):
    return join_values(values) if values else "–"


def print_launch_rows(label, results):
    groups = {}
    for result in results:
        if result.get("launch") or result.get("multi_kill"):
            groups.setdefault(result["target"].rsplit("-", 1)[0], []).append(result)
    for name, measured in groups.items():
        launches = [result.get("launch") or {} for result in measured]
        frame_times = [describe_frame_time(result.get("frame_time")) for result in measured if result.get("frame_time")]
        spreads = [result["multi_kill"].get("ragdoll_start_spread_real_seconds") for result in measured if result.get("multi_kill")]
        instant_angles = [describe_values([(launch.get("instant_angle_deg") or {}).get(f"{seconds:.1f}") for launch in launches if launch]) for seconds in INSTANT_ANGLE_SECONDS]
        body_plan = next((launch["body_plan"] for launch in launches if launch), "–")
        print(f"| {label} | {describe_group_name(name, measured)} | {body_plan} | {describe_kill_style(measured)} | "
              f"{join_values([launch.get('ragdoll_delay_real_seconds') for launch in launches])} | {describe_values(spreads)} | "
              f"{join_values([launch.get('launch_direction_error_deg') for launch in launches])} | "
              f"{join_values([launch.get('com_travel_2s_cm') for launch in launches])} | {join_values([describe_land_detect(launch) for launch in launches])} | "
              f"{join_values([launch.get('expected_fall_seconds') for launch in launches])} | {join_values([launch.get('post_land_slide_cm') for launch in launches])} | "
              f"{join_values([launch.get('landing_angle_deg') for launch in launches])} | {' | '.join(instant_angles)} | "
              f"{join_values([launch.get('first_frame_relative_speed_cm_s') for launch in launches])} | "
              f"{join_values([launch.get('standing_com_ratio') for launch in launches])} | {join_values([launch.get('rest_height_ratio') for launch in launches])} | "
              f"{join_values([result.get('sleep_seconds') for result in measured])} | {describe_values(frame_times)} |")


def summarize_reports(task, labels):
    reports = {label: json.loads((MEASUREMENT_ROOT / task / label / "report.json").read_text(encoding="utf-8")) for label in labels}
    print("| 측정 | 대상 | 래그돌 시작 초 | 시작 최대 속도 cm/s | 처치 높이 cm | 첫 잠들기 초 | 잠들기 실시간 초 | 잠든 횟수 | 재우는 순간 최대 바디 속도 cm/s | 다시 깨어 정착 초 | 위치 RMS cm | 슬로모 중 시작 | 다른 시체 잠들기 초/재우는 순간 cm/s | 목표(3초·30cm/s) |")
    print("|---|---|---|---|---|---|---|---|---|---|---|---|---|---|")
    for label, report in reports.items():
        groups = {}
        for result in report["results"]:
            groups.setdefault(result["target"].rsplit("-", 1)[0], []).append(result)
        for name, results in groups.items():
            measured = [result for result in results if "render" in result]
            failed = len(results) - len(measured)
            others = [f"{other.get('sleep_seconds')}/{other.get('max_freeze_body_speed_cm_s')}" for result in measured for other in result.get("others", []) if other.get("ragdoll", True)]
            print(f"| {label} | {describe_group_name(name, results, failed)} | {join_values([result['ragdoll_delay_seconds'] for result in measured])} | "
                  f"{join_values([result['launch_speed_cm_s'] for result in measured])} | {join_values([(result.get('at_kill') or {}).get('height_cm') for result in measured])} | "
                  f"{join_values([result['sleep_events'][0][0] if result.get('sleep_events') else None for result in measured])} | "
                  f"{join_values([result.get('sleep_real_seconds') for result in measured])} | {join_values([len(result.get('sleep_events', [])) for result in measured])} | "
                  f"{join_values([result.get('max_freeze_body_speed_cm_s') for result in measured])} | "
                  f"{join_values([max((value for value in result.get('rewake_settle_seconds') or [] if value is not None), default=None) for result in measured])} | "
                  f"{join_values([result['render']['position_rms_cm'] for result in measured])} | {join_values(['예' if result.get('ragdoll_in_slowmo') else '아니오' for result in measured])} | "
                  f"{' '.join(others) or '–'} | {sum(1 for result in measured if result.get('meets_sleep_target'))}/{len(results)} |")
    if not any(result.get("launch") or result.get("multi_kill") for report in reports.values() for result in report["results"]):
        return
    print()
    print(LAUNCH_HEADER)
    print(LAUNCH_SEPARATOR)
    for label, report in reports.items():
        print_launch_rows(label, report["results"])


def compose_contact_sheet(image_module, draw_module, rows, out_path):
    offsets = sorted({shot["offset"] for _, shots in rows for shot in shots})
    with image_module.open(ROOT / rows[0][1][0]["path"]) as first:
        cell_height = round(first.height * CONTACT_CELL_WIDTH / first.width)
    sheet = image_module.new("RGB", (CONTACT_CELL_WIDTH * len(offsets), (cell_height + CONTACT_CAPTION_HEIGHT) * len(rows)), "white")
    draw = draw_module.Draw(sheet)
    for row_index, (target, shots) in enumerate(rows):
        top = row_index * (cell_height + CONTACT_CAPTION_HEIGHT)
        for shot in shots:
            left = offsets.index(shot["offset"]) * CONTACT_CELL_WIDTH
            with image_module.open(ROOT / shot["path"]) as picture:
                sheet.paste(picture.convert("RGB").resize((CONTACT_CELL_WIDTH, cell_height)), (left, top + CONTACT_CAPTION_HEIGHT))
            draw.text((left + 4, top + 4), describe_shot_caption(target, shot), fill="black")
    sheet.save(out_path)
    return out_path


def describe_shot_caption(target, shot):
    if "clock" not in shot:
        return f"{target} +{shot['offset']:g}s (real {shot.get('real_seconds_after_kill')})"
    return (f"{target} {shot['clock']}+{shot['offset']:g}s: game rag+{shot.get('captured_game_seconds_after_ragdoll')} "
            f"kill+{shot.get('captured_game_seconds_after_kill')}")


def make_contact_sheets(task, labels):
    try:
        from PIL import Image, ImageDraw
    except ImportError:
        Image = ImageDraw = None
    for label in labels:
        directory = MEASUREMENT_ROOT / task / label
        report = json.loads((directory / "report.json").read_text(encoding="utf-8"))
        rows = [(result["target"], [shot for shot in result.get("timed_shots") or [] if shot.get("path")]) for result in report["results"]]
        rows = [(target, shots) for target, shots in rows if shots]
        if not rows:
            print(f"{label}: 시간 지정 스크린샷 없음")
            continue
        if Image is None:
            print(f"{label}: PIL 없음, 파일 목록만")
            for target, shots in rows:
                print(f"  {target}: " + " ".join(f"+{shot['offset']:g}s={shot['path']}" for shot in shots))
            continue
        sheet_path = compose_contact_sheet(Image, ImageDraw, rows, directory / "contact_sheet.png")
        print(f"-> {sheet_path.relative_to(ROOT).as_posix()}")


def analyze_existing(raw_directory):
    results = []
    for raw_path in sorted(raw_directory.glob("*.json")):
        results.extend(analyze_with_subjects(json.loads(raw_path.read_text(encoding="utf-8")), raw_path.stem, {}))
    return results


def print_result(result):
    print(json.dumps({key: value for key, value in result.items() if key not in ("shots", "timed_shots", "bone_mass_kg")}, ensure_ascii=False))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--label", default="")
    parser.add_argument("--summarize", nargs="*", default=[], help="measurements/<task>/<label>/report.json들을 마크다운 비교표로 출력하고 끝냄(launch 지표가 있으면 날림 표도)")
    parser.add_argument("--contact-sheet", nargs="*", default=[], help="measurements/<task>/<label>/report.json의 시간 지정 스크린샷을 대상×시각 한 장(contact_sheet.png)으로 합치고 끝냄, PIL 없으면 파일 목록만")
    parser.add_argument("--species", nargs="*", default=DEFAULT_SPECIES)
    parser.add_argument("--force-species", nargs="*", default=[])
    parser.add_argument("--no-player", action="store_true")
    parser.add_argument("--repeats", type=int, default=1)
    parser.add_argument("--map", default="/Game/Combat/Maps/LV_TDMegaMagicArena")
    parser.add_argument("--distance", type=float, default=350.0)
    parser.add_argument("--kill-fraction", type=float, default=0.25)
    parser.add_argument("--kill-delay", type=float, default=0.6, help="처치까지 게임 초(--count 2 이상이면 처치 전 프레임 시간 1초를 위해 최소 1.2)")
    parser.add_argument("--observe", type=float, default=7.0)
    parser.add_argument("--shots", type=float, nargs="*", default=[1.6, 1.75, 1.9, 2.05])
    parser.add_argument("--analyze-only", action="store_true")
    parser.add_argument("--count", type=int, default=1, help="종마다 같은 자리에 부를 마릿수(2 이상이면 같은 순간 모두 처치해 시체 더미, 모두 측정하고 처치 전 1초·뒤 3초 프레임 시간 기록)")
    parser.add_argument("--spread", type=parse_distance_range, default=None, help="--count 2 이상 동시 처치: 처치 틱에 처치 순서(다른 몬스터들→대상)대로 가해자와의 거리를 MIN→MAX cm로 늘려 부채꼴(25° 간격)로 옮김(처치 순서별 시차 측정, 첫 처치 0·나머지 0.02~0.07초), PowerShell에서는 작은따옴표로 '250,450'")
    parser.add_argument("--auto-rerun-load-ms", type=float, default=DEFAULT_AUTO_RERUN_LOAD_MS, help="처치 전 1초 프레임 p50이 이 ms를 넘으면 그 대상을 한 번 다시 잼(부하 오염 차단, 사유는 결과 rerun과 report load_reruns), 0이면 끔")
    parser.add_argument("--frame-time-only", action="store_true", help="--count N 성능 측정: 바디 표본을 끄고 프레임 시간만(표본기 비용이 프레임 시간에 섞이지 않게)")
    parser.add_argument("--kill-strength", choices=["weak", "strong"], default="", help="weak: 남은 체력 5%%에서 남은 체력+1 피해로 처치(초과 비율 O≈0.05), strong: 최대 체력 이상 한 방(O=1)")
    parser.add_argument("--critical", action="store_true", help="마무리 일격을 치명타로(그 일격 동안 가해자 치명 확률 1, 피해는 치명 배율로 나눠 처치 세기 유지)")
    parser.add_argument("--screenshot-times", type=parse_seconds_list, default=[], help="--screenshot-clock 기준 게임 초마다 HighResShot 1 filename=<증거 폴더>/<대상>-1_<clock><ms>ms.png, 예 '0,0.1,0.2,0.35,0.6,1.0'(PowerShell은 작은따옴표 필수, 첫 회차만, 그 작업은 고정 시간 간격이라 frame_time 생략)")
    parser.add_argument("--screenshot-clock", choices=["ragdoll", "kill"], default="ragdoll", help="촬영 시각 0: ragdoll 대상 첫 시뮬 프레임(처치 정지 미포함), kill 처치 틱(처치 정지 포함, 사망 클립 대조군용)")
    parser.add_argument("--screenshot-fixed-fps", type=float, default=DEFAULT_SCREENSHOT_FIXED_FPS, help="촬영 작업 동안 t.OverrideFPS 값(월드 프레임 간격 1/fps 고정, 끝나면 복구), 0이면 끄고 촬영 끊김만큼 게임 시간이 건너뜀")
    parser.add_argument("--camera-arm", type=float, default=0.0, help="작업 동안 플레이어 카메라 SpringArm 길이 cm(예 400)로 바꾸고 대상(처치 뒤는 처치 위치)을 화면 가운데에 둠, 피치·요 유지, 작업마다 복구, 0이면 그대로")
    parser.add_argument("--body", nargs="*", default=[], help="물리 에셋 바디 기본값 메모리 재정의(저장 안 함), 예: angular_damping=1.0 sleep_family=SENSITIVE")
    parser.add_argument("--self-collision-off", action="store_true", help="물리 에셋 바디 자기 충돌 끄기(메모리만)")
    parser.add_argument("--assist", default="", help="정착 재우기 흉내: 최소초,평균속도cm/s,유지초 예 '1.0,15,0.3'")
    parser.add_argument("--task", default="M3-38", help="증거 폴더 measurements/<task>/<label>, 사망 즉시 래그돌 날림 측정은 M3-40")
    parser.add_argument("--flight-hold", type=float, default=None, help="비행 중으로 바뀐 뒤 이 초만큼 날면 처치(kill-fraction을 비행 페이즈 문턱 아래로)")
    parser.add_argument("--kill-timeout", type=float, default=20.0, help="--flight-hold 처치 조건을 기다리는 최대 초")
    parser.add_argument("--freeze", action="store_true", help="처치 kill-delay초 전에 빙결(TDFreezeStatusEffect 1초)")
    parser.add_argument("--pile-spacing-scale", type=float, default=0.0, help="--count 2 이상일 때 처치 순간 다른 몬스터를 대상 뒤·옆 캡슐 반지름×이 값 간격에 붙임")
    parser.add_argument("--pile-gaps", type=float, nargs="*", default=[], help="대상 처치 뒤 이 초마다 다른 몬스터를 대상 시체 위로 옮겨 처치(순차 더미)")
    parser.add_argument("--cvar", nargs="*", default=[], help="PIE 시작 뒤 설정하고 끝나면 되돌릴 콘솔 변수, 예 TD.Feedback.SlowMo=0(보스 처치·승리·페이즈 슬로모 끔) TD.Death.ImmediateRagdoll=0")
    parser.add_argument("--emulate-launch-clamp", default="", help="C++ 수정 흉내: 초,질량중심 대비 선속도 상한cm/s,각속도 상한도/초 예 '0.4,600,1440'")
    parser.add_argument("--emulate-firm-damping", default="", help="C++ 수정 흉내: 시작초,추가 감쇠 예 '2.5,8'")
    arguments = parser.parse_args()
    if arguments.summarize:
        summarize_reports(arguments.task, arguments.summarize)
        return 0
    if arguments.contact_sheet:
        make_contact_sheets(arguments.task, arguments.contact_sheet)
        return 0
    if not arguments.label:
        parser.error("--label이 필요하다")
    if arguments.spread and (arguments.count < 2 or arguments.pile_gaps or arguments.pile_spacing_scale):
        parser.error("--spread는 --count 2 이상 동시 처치에만 쓰고 --pile-gaps·--pile-spacing-scale과 함께 쓰지 않는다")
    evidence_directory = MEASUREMENT_ROOT / arguments.task / arguments.label
    raw_root = M338_RAW_ROOT if arguments.task == "M3-38" else ROOT / "Saved" / "AgentOps" / time.strftime("%Y%m%d") / f"ragdoll-{arguments.task.lower()}"
    raw_directory = raw_root / arguments.label / "raw"
    cvars = parse_cvars(arguments.cvar)
    evidence_directory.mkdir(parents=True, exist_ok=True)
    raw_directory.mkdir(parents=True, exist_ok=True)

    editor_closures = []
    if arguments.analyze_only:
        results = analyze_existing(raw_directory)
    else:
        monster_targets = list(arguments.species) + [name for name in arguments.force_species if name not in arguments.species]
        overrides = build_overrides(arguments, monster_targets)
        if not start_pie(arguments.map, overrides, editor_closures):
            print("editor remote execution or PIE unavailable")
            stop_pie()
            return EXIT_ENVIRONMENT
        cvars_before = {}
        results = []

        def restart_pie_for_player():
            stop_pie()
            if not start_pie(arguments.map, overrides, editor_closures):
                return False
            set_cvars(cvars)
            return True

        try:
            cvars_before = set_cvars(cvars)
            for repeat_index in range(1, arguments.repeats + 1):
                for species_name in monster_targets:
                    for result in measure_target(arguments, "monster", species_name, repeat_index, evidence_directory, raw_directory, lambda: True):
                        results.append(result)
                        print_result(result)
            for repeat_index in range(1, 0 if arguments.no_player else arguments.repeats + 1):
                if not restart_pie_for_player():
                    break
                for result in measure_target(arguments, "player", None, repeat_index, evidence_directory, raw_directory, restart_pie_for_player):
                    results.append(result)
                    print_result(result)
        finally:
            try:
                set_cvars(cvars_before)
            finally:
                stop_pie()

    report = {"label": arguments.label, "map": arguments.map, "distance": arguments.distance, "kill_fraction": effective_kill_fraction(arguments), "count": arguments.count,
              "kill_strength": arguments.kill_strength or None, "critical": arguments.critical, "screenshot_times": arguments.screenshot_times,
              "screenshots": describe_screenshot_settings(arguments), "camera_arm_cm": arguments.camera_arm or None,
              "experiment": {"body": parse_body_values(arguments.body), "self_collision_off": arguments.self_collision_off, "assist": parse_assist(arguments.assist)},
              "scenario": build_scenario(arguments), "cvars": cvars, "kill_delay": effective_kill_delay(arguments), "observe": arguments.observe,
              "auto_rerun_load_ms": arguments.auto_rerun_load_ms or None,
              "load_reruns": [result["rerun"] for result in results if (result.get("rerun") or {}).get("target") == result.get("target")],
              "asset_editors_closed": editor_closures,
              "window": {"land_speed_cm_s": LAND_SPEED_CM_S, "delay_seconds": WINDOW_DELAY_SECONDS, "length_seconds": WINDOW_LENGTH_SECONDS},
              "landing_rule": {"min_fall_speed_cm_s": LANDING_MIN_FALL_SPEED_CM_S, "rebound_min_speed_cm_s": LANDING_REBOUND_MIN_SPEED_CM_S,
                               "rebound_fraction": LANDING_REBOUND_FRACTION, "height_margin_cm": LANDING_HEIGHT_MARGIN_CM,
                               "fallback_extra_seconds": LANDING_FALLBACK_EXTRA_SECONDS, "fallback_height_margin_cm": LANDING_FALLBACK_HEIGHT_MARGIN_CM, "overdue_seconds": LANDING_OVERDUE_SECONDS, "abandon_seconds": LANDING_ABANDON_SECONDS, "up_speed_cm_s": LAUNCH_UP_SPEED_CM_S,
                               "effective_gravity_cm_s2": EFFECTIVE_GRAVITY_CM_S2, "rest_height_ratio_default": [UPRIGHT_REST_HEIGHT_RATIO, HORIZONTAL_REST_HEIGHT_RATIO]},
              "target": {"position_rms_cm": TARGET_POSITION_RMS_CM, "sleep_seconds": TARGET_SLEEP_SECONDS, "freeze_body_speed_cm_s": TARGET_FREEZE_BODY_SPEED_CM_S},
              "definitions": METRIC_DEFINITIONS, "results": results}
    (evidence_directory / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=1), encoding="utf-8")
    print_table(results)
    if any(result.get("launch") or result.get("multi_kill") for result in results):
        print(LAUNCH_HEADER)
        print(LAUNCH_SEPARATOR)
        print_launch_rows(arguments.label, results)
    print(f"-> {(evidence_directory / 'report.json').relative_to(ROOT).as_posix()}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

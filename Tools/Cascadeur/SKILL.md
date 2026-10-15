---
name: td-cascadeur-animation
description: TDGame에서 텍스트로 요청한 캐릭터 동작을 Cascadeur로 만들 때 사용한다. UE 캐릭터 FBX 내보내기, Cascadeur 퀵 리깅, 동작 명세(키 포즈) 작성·풀이, Cascadeur 씬 제작·렌더·점검, FBX 내보내기와 UE AnimSequence 가져오기를 Tools/Cascadeur 도구로 실행한다. Blender 편집 경로나 게임플레이 구현에는 사용하지 않는다.
---

# TD Cascadeur 애니메이션 제작

텍스트 요청을 사람이 읽을 수 있는 동작 명세(`*.motion.json`)로 옮기면, 도구가 UE5 Manny 기준 자세 위에서 FK·2관절 IK로 풀어 Cascadeur 리그(점 43·손가락 박스·루트) 키로 쓰고 Cascadeur가 보간·리그 풀이·저장·렌더를 한다. 동작 해석(어느 쪽으로 돌고 칼이 어디를 지나는지)은 에이전트의 일이고, 숫자 풀이·키 쓰기·점검은 도구의 일이다.

## 0. 준비 (처음 한 번, 이후 확인만)

1. Cascadeur 2026.2.3 이상 설치. `python Tools/Cascadeur/setup_cascadeur.py` → `python Tools/Cascadeur/cascadeur_bridge.py restart` → `health`에 `"ok": true`(L-anim-09).
2. 라이선스 확인: `python Tools/Cascadeur/cascadeur_bridge.py call csc_inspect.describe_application`. `is_export_available=false`(Free/basic)면 FBX 내보내기·UE 가져오기는 못 하고 `.casc`까지만 만든다(L-anim-08). 우회 추출은 하지 않는다.
3. UE 단계가 필요하면 `python Tools/ue_editor.py ensure`.
4. MCP로 직접 다룰 때는 서버 `cascadeur`(http://127.0.0.1:8765/mcp, 도구 `run_script` 하나, 30초 대기). 결정론 작업은 `cascadeur_bridge.py call`을 쓴다(작업 결과를 JSON으로 받고 30초 넘는 작업도 기다린다).

## 1. 텍스트 요청 → 동작 명세

명세 위치: `AnimationSources/Cascadeur/<에셋 이름>.motion.json`(예: `AS_TD_Player_SwordSpinSlash01.motion.json`). 원문 `request`와 해석 `interpretation`을 반드시 적고, 모호한 방향(좌우·회전 방향)은 캐릭터 자신의 좌우로 해석하며 근거를 한 문장 남긴다.

좌표·각도 규약
- 위치: 캐릭터 좌표 `[오른쪽, 위, 정면]` cm. 0프레임 루트가 원점에서 정면(+정면)을 본다. 이후 위치는 같은 세계 좌표.
- 각도(도): `yaw` + = 왼쪽으로 돎(위에서 반시계), `pitch` + = 앞으로 숙임(발은 발끝 내림), `roll` + = 오른쪽으로 기욺. 합성은 yaw·pitch·roll 순.
- 방향 벡터: `blade`(칼끝 방향), `edge`(참날 방향), `elbow`·`knee`(굽는 쪽).
- `facing`(키 단위, 도): `*_body` 필드(`position_body`, `blade_body`, `edge_body`, `elbow_body`, `knee_body`, `look_at_body`)를 루트 위치 기준, facing만큼 돈 몸 좌표로 읽는다. 손은 몸 좌표, 발은 세계 좌표가 쓰기 쉽다.

키 필드(첫 키는 전부 필요, 이후 키는 바뀌는 것만; 나머지는 이전 키를 잇는다)
- `root.position`(루트모션; 회전은 넣지 않는다 — Mover가 루트 회전을 폰 회전으로 쓴다)
- `pelvis{position, yaw, pitch, roll}`, `spine{yaw, pitch, roll}`(골반 대비 가슴, spine_01~05에 나눔), `head{look_at | yaw, pitch, roll}`
- `clavicle_l|r{raise, forward}`
- `hand_r`(무기 손): `position|position_body`, `blade|blade_body`, `edge|edge_body|"auto"|"sweep_cw"|"sweep_ccw"`, `elbow`(기본 `"auto"`: 손목 부담 최소), 키 단위 옵션 `edge_tolerance`(도), `position_search`(cm 반지름)
- `hand_l`: `position|position_body`, `elbow|elbow_body`, `wrist{yaw, pitch, roll}`
- `foot_l|r`: `position | ball | pivot:"ball"`(이전 키의 볼 위치 고정, 발볼 회전), `yaw, pitch, roll, toe`(+ = 발가락 올림; 뒤꿈치를 든 볼 회전이면 `toe = pitch`), `knee`(기본 `"auto"`)
- `fingers_l|r{curl}`(0~1, 1 = 칼 쥔 주먹)
- 키 단위: `name`, `facing`, `interpolation`(기본 `clamped_bezier`; `bezier`, `linear`, `step`), `inbetween: "fk"` + `inbetween_ease`(`linear|in|out|inout`: 다음 키까지 관절 구면 보간 중간 키 생성, 발은 IK로 접지 유지)
- 명세 단위: `fps`(30), `frame_count`(마지막 프레임 번호+1; 도구가 유지 프레임 1개를 덧붙인다), `events{hit_window_frames, input_buffer_frames, combo_branch_frames}`, 선택 `character{mesh, skeleton, template, weapon_profile, destination_folder, rig_scene}`, 선택 `ai_motion_generation: [{"frames": [a, b]}]`(L-anim-11 주의)
- `character.rig_scene`(예: 사용자가 만든 `AnimationSources/Cascadeur/UE5_Manny.casc`)를 주면 퀵 리깅 대신 그 리그를 새 탭으로 불러 쓰고 원본 파일·열린 탭은 건드리지 않는다. 리그에 루트 제약(`root_constraint`)이 있으면 루트 키를 쓰지 않고 루트가 골반의 지면 투영을 따르게 두며 `Follow rotation`은 끈다(명세의 `root.position`은 몸 좌표 기준으로만 쓰인다).

전투 동작 작성 기준
- 준비(감기) 느리게 → 타격 빠르게 → 팔로스루 감속 → 회수. 골반이 먼저, 가슴·팔·칼 순서로 풀리게 각도를 시차를 두고 준다.
- 회전량이 큰 구간(칼 방위각이 키 사이에 60° 넘게 변함)은 매 프레임 키를 쓰고, 칼 방위각을 회전 방향으로 단조 증가시킨다. 키 보간·관절 보간은 긴 호를 직선으로 질러간다(L-anim-12).
- 칼 쥐기는 칼날이 손 축과 약 100°다. 백핸드 타격 순간에는 아래팔이 진행 방향(바깥)을 향하고 참날이 아래팔 방향이다. 타격 키에는 `edge: "sweep_cw|ccw"`, `edge_tolerance: 25~40`, `position_search: 10~16`을 준다.
- 디딤발은 착지 직전 키를 착지점 1cm 안으로 붙이고, 볼 회전 중 떼는 발은 발볼을 3cm 넘게 든다(미끄러짐 판정 기준).
- 횡베기는 팔만 휘두르지 않고 몸통 전체가 돌며 벤다(사용자 지시 2026-10-08). 팔을 편 뒤 타격 구간 매 프레임 키에서 `hand_r`의 `position_body`·`blade_body`·`edge`를 같은 값으로 두어 팔·칼을 가슴에 고정하고, 골반·척추 yaw(합 = facing)를 프레임당 20~25°씩 돌린다. 고정 팔 자세는 키 하나짜리 명세에 `position_search: 16`, `edge: "sweep_cw"`, `edge_tolerance: 30`을 주고 풀어 팔꿈치 내각 150° 이상·손목 swing 30° 이하인 조합을 먼저 찾는다(첫 시험: 손 몸 좌표 (56, 125, 46), 칼 가슴 기준 −30°).
- 왼팔(빈손)은 팔꿈치 내각 55° 미만으로 접지 않는다. 몸 좌표에서 어깨로부터 35cm 넘게 둔다.

## 2. 실행·점검 루프

```powershell
python Tools/Cascadeur/build_cascadeur_animation.py --spec AnimationSources/Cascadeur/<이름>.motion.json --stages character,keys
python Tools/Cascadeur/build_cascadeur_animation.py --spec AnimationSources/Cascadeur/<이름>.motion.json --stages scene,capture
python Tools/Cascadeur/check_cascadeur_animation.py --spec AnimationSources/Cascadeur/<이름>.motion.json
```

- `keys` 보고(`Saved/Cascadeur/<이름>/keys.solve.json`): 손 도달 오차 0, 무기 손 `wrist_degrees.swing` 60° 안팎 이하, 비틀림 약 75° 이하, `ground.below_ground` 없음, 다리 도달 오차 0.
- `check` 보고(`Saved/Cascadeur/<이름>/check/check.json`, 막대 인형+칼 GIF): 타격 구간 회전 방향·정면 통과 프레임·칼끝 높이가 요청과 맞는지, 발 볼 미끄러짐 2cm/프레임 이하, `bone_stretch.within_tolerance`(손가락 등 뼈 길이 변화 1cm 이하, L-anim-13), 최저 관절 높이 0 이상, 루트 이동량.
- 손가락·팔꿈치는 전신 렌더와 막대 인형으로는 안 보이므로 손 근접 렌더(카메라를 손 위치 근처에 두고 `csc_capture`)로 주요 프레임을 확인한다.
- Cascadeur 렌더 시트(`captures/`)로 실루엣·체중을 본다. 고친 뒤 `keys,scene`만 다시 돌린다.
- 라이선스가 허용되면 `--stages export,import [--replace]` → `/Game/Characters/Mannequins/Anims/Sword/<이름>`(루트모션 켜짐) → PIE 콘솔 `TDPlayMeleeAction <경로>`, 루트모션 측정은 `Tools/Movement/pie_check_player_movement.py`.

## 3. 실행할 스크립트

| 스크립트 | 환경 | 용도 |
|---|---|---|
| `setup_cascadeur.py` | 시스템 Python | Cascadeur 사용자 설정에 `csc_scripts`·`td_events` 등록(MCP 자동 기동), `--check` |
| `cascadeur_bridge.py` | 시스템 Python | `health`·`ensure`·`restart`·`call <모듈>.<함수> --args-file`·`eval` |
| `build_motion_keys.py` | 시스템 Python | `describe`(기준 자세·길이·칼 정보), `solve`(명세 → 키 JSON·보고) |
| `build_cascadeur_animation.py` | 시스템 Python | 단계 실행: character·keys·scene·capture·export·import |
| `check_cascadeur_animation.py` | 시스템 Python | Cascadeur 결과 관절 + 칼 → 수치·GIF·시트 |
| `csc_scripts/csc_character.py` | Cascadeur Python | FBX 가져오기·리그 모드·UE5 퀵 리깅·저장 |
| `csc_scripts/csc_motion.py` | Cascadeur Python | 씬 열기·키 쓰기·보간·탭 정리·AI 모션 생성·동작 호출 |
| `csc_scripts/csc_capture.py` | Cascadeur Python | 렌더 준비·프레임 설정·PNG 렌더(별도 호출) |
| `csc_scripts/csc_export.py` | Cascadeur Python | 라이선스 확인 후 관절 FBX 내보내기 |
| `csc_scripts/csc_inspect.py` | Cascadeur Python | 앱·탭·씬·자세 읽기 |
| `csc_scripts/csc_runtime.py` | Cascadeur Python | 브리지 작업 실행기(30초 넘는 작업 결과 보관) |
| `csc_scripts/td_events/scene_idle/td_autostart_mcp.py` | Cascadeur 이벤트 | 내장 MCP 서버 자동 기동 |
| `weapons/SM_Sword.json` | 데이터 | HandGrip_R 소켓·칼 부착값·칼 특징점(다른 무기는 같은 형식으로 추가) |

## 4. 읽기용 참고

- 교훈: L-anim-08(라이선스), L-anim-09(자동 기동), L-anim-10(키 쓰기·렌더 함정), L-anim-11(AI 인비트위닝), L-anim-12(칼 쥐기·호), L-anim-13(손가락 박스 위치·clavicle_l 읽기), L-repo-09(PowerShell 따옴표)
- API 조사 원문: `Saved/AgentOps/20261008/cascadeur-authoring-api.md`, `cascadeur-app-integration.md`(생성물, 커밋 안 함)
- 첫 시험 증거: `Docs/Validation/Cascadeur/H-01-cascadeur-pipeline-2026-10-08.md`
- 좌표: Cascadeur 전역은 X = 캐릭터 왼쪽, Y = 위, Z = 정면(cm). UE 메시 좌표와는 Y·Z만 바뀌고, 관절 로컬 좌표는 UE 뼈 로컬의 Y 부호만 반대다.

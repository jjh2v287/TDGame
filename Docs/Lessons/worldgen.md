# Lessons — worldgen (월드·이계·심리스 이동)

[← 인덱스로](../AgentCollaboration_Plan.md)
종류: 교훈 · 형식은 `Docs/AgentRules.md` 3절 A.

### L-worldgen-01 이계에서 필드로 돌아갈 때 준비가 9~24초 걸린다(도착 직후에도)
- 증상: PIE에서 이계 도착 1~2초 뒤 출구 상자에 들어가도 `SeamlessTravel: '<Gate>' ready after 9~24s`, 20초 넘으면 `still not ready ... continuing to wait`. 이계 진입은 선로딩 뒤 0.03~3.7초로 빠르다.
- 원인: 순간이동한 프레임부터 출구 관문이 다음 틱에 필드 귀환 선로딩을 걸기 전까지, 필드를 붙잡는 스트리밍 소스가 없다. 그 사이 필드 셀이 내려가기 시작해 셀 재로드와 동적 내비메시 재생성을 다시 한다.
- 해결: 이계 진입 순간이동 직후 `RealmDepartureHoldSeconds`(DefaultGame.ini, 10초) 동안 그 관문의 필드 귀환점을 두 번째 스트리밍 소스로 붙잡는다(`UTDSeamlessTravelSubsystem::BeginDepartureHold`). 결과 0~2.7초. 필드가 완전히 내려간 뒤 귀환은 11~13초이므로 이계 쪽 출구 선로딩 반경을 150m(`exit_preload_distance_cm`)로 둔다(600cm/s로 약 25초 걸음).
- 범위: UE 5.8 월드 파티션 + Dynamic 내비(D-06), LV_DarkFantasy_OpenWorld
- 증거: `Docs/Validation/P3-13-realms.md`, `Saved/AgentOps/20261013/parent_pie_1.log`·`parent_pie_2.log`
- 날짜·상태: 2026-10-14 active
- 발견: claude

### L-worldgen-02 DarkFantasyTopDown 재질을 이계 색으로 바꿀 때의 함정 3가지
- 증상: (1) `MI_Rocks`·`MI_Cliffs1` 복제본에 `DiffuseMultiplier`를 스칼라로 넣으면 적용되지 않는다(로그 missing). (2) `MI_EmissiveRed`(M_Emissive, 무조명 가산) 계열은 Color 2.0만 줘도 하얀 원반처럼 탄다. (3) 랜드스케이프를 밝히면 바위흙·진흙길 텍스처가 주황빛 흙색이 된다.
- 해결: (1) `DiffuseMultiplier`는 벡터 파라미터다. (2) 이 프로젝트는 수동 노출 보정 +10.4라 무조명 방출이 과하다. 발광체는 `MI_Dreadplants` 자식의 `AmbientColor`(조명받는 재질)로 만들고, 무조명 방출은 0.05 수준만 쓴다. (3) `M_TD_Landscape`의 `RealmDesaturation`(기본 0)으로 채도를 뺀다(고원 0.78).
- 범위: `/Game/DarkFantasyTopDown/Materials`, `Tools/WorldGen/generate_realm.py` 프리셋
- 증거: `Docs/Validation/P3-13-realms.md` '분위기 조정 기록', `Docs/Validation/P3-13-realms/*.jpg`
- 날짜·상태: 2026-10-14 active
- 발견: claude

### L-worldgen-03 숨은 이음매 통로: 이음매에서 태양 회전을 한 번에 바꾸면 통과 직후 조명이 튄다
- 증상: 통로 이음매를 넘은 뒤 12m 지점 화면이 넘기 전보다 갑자기 새까맣다(고원 통로). 필드 태양은 통로를 따라 비추고 이계 태양은 통로와 직각이라 22m 벽이 바닥을 가린다.
- 해결: 태양 회전도 통로 진행도 w로 보간한다(쿼터니언 Slerp, 1/32 단계 양자화로 가상 섀도 맵 무효화 횟수 제한). 이음매에서 양쪽 w=0.5라 같은 각도가 된다(`UTDRealmSubsystem::UpdateAtmosphere`).
- 함께 알아 둘 것: 이음매 앞뒤 ±30m는 높이·소품을 정수 오프셋으로 똑같이 만들어도 랜드스케이프 재질 색조는 이계마다 다르다 → 바닥은 같은 재질 평면으로 덮고 이음매에서 전역 높이 안개를 올려 가린다. 시험에서 폰을 순간이동 걸음으로 옮기면 오르막 경사에서 겹침 보정으로 멈추므로 실제 이동 입력(`add_movement_input`)으로 검증한다.
- 범위: `Tools/WorldGen/realm_passages.py` 통로, P3-15
- 증거: `Docs/Validation/P3-15-passages.md`, `Docs/Validation/P3-15-passages/PaleHeightsNortheast_*.jpg`
- 날짜·상태: 2026-10-14 active
- 발견: claude

### L-worldgen-04 PIE 도구가 폰을 멀리 순간이동한 직후 걷기를 시작하면 칸이 아직 없어 폰이 지형을 뚫고 떨어진다
- 증상: `pie_walk_passages.py` 첫 실행에서 별밭 두 통로의 폰 높이가 649cm → −54,644cm, 속도 2,014cm/s(낙하)로 기록되고 이음매 통과가 없다. 에디터 월드에서는 같은 좌표 아래에 `LandscapeHeightfieldCollisionComponent`가 있다. 고원 통로는 정상(이전 걷기 끝 위치에서 가까움). 시작점 위에 나무 다리가 있으면 랜드스케이프 전용 지면 판정이 다리에 가로막혀 `None`을 돌려준다(`line_trace_multi`는 첫 막힘에서 멈춘다).
- 원인: 월드 파티션 칸은 폰(스트리밍 소스)이 도착한 뒤에 로드된다. 실험 도구에는 있던 대기가 정식화 과정에서 빠졌다. P3-15 화면 촬영 때도 같은 낙하가 있었다(두 번째).
- 해결: 순간이동 뒤 시작점 아래에 걸을 수 있는 막힘 표면(법선 z>0.5, 랜드스케이프에 한정하지 않음)이 잡힐 때까지 매 틱 폰을 시작점에 붙잡아 두고(최대 60초), 잡히면 그 위로 옮긴 다음 걷기를 시작한다(`editor_pie_probe.py` `hold_until_ground_loaded`·`find_support_z`, 결과 `stream_wait_s`). 지점 촬영은 지면을 못 찾으면 2초 간격으로 10회까지 다시 이동한다(`pie_capture_spots.py`).
- 범위: PIE에서 폰을 수백 m 이상 순간이동하는 모든 검사 도구
- 증거: `Saved/WorldGen/PieWalk/walk_p3-17_after`(낙하) ↔ `walk_p3-17_after2`·`after3_pale`(6/6 통과, 대기 3.8초)
- 날짜·상태: 2026-10-14 active
- 발견: claude

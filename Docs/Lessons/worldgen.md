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

### L-worldgen-05 PIE 걷기 도구: 시작점이 나무 판자 모서리면 바닥을 못 찾고, 순간 배치 직후 카메라 따라잡기가 '카메라 점프'로 잡힌다
- 증상: ① 고원 북동 걷기가 `ground not loaded under walk start after 60s`로 실패. 시작점(31508,41131) 아래 첫 막힘이 `TDGen_Boardwalk_115`의 판자 모서리(법선 z 0.39)라 걸을 수 있는 면 판정(z>0.5)을 못 넘는다(`line_trace_multi`는 첫 막힘에서 멈춘다, L-worldgen-04의 두 번째 변형). ② 남서 걷기에서 카메라 점프 3회(최대 26,091cm)와 4.7초 끊김이 걷기 0.24초에 나온다 — 도구가 폰을 수백 m 옮긴 직후 카메라가 따라온 것이지 게임의 이음매가 아니다.
- 해결: `editor_pie_probe.py` `find_support_z`가 시작점과 ±30·60cm 8점을 차례로 본다. 배치 뒤 폰 둘레 격자 칸이 Loaded(월드 파티션 서브시스템은 Python에서 `unreal.ObjectIterator(unreal.WorldPartitionSubsystem)` 중 `get_world()==PIE 월드`로 얻는다, `SubsystemBlueprintLibrary`는 Python에 없음)이고 카메라가 10프레임 연속 20cm 이하로 움직이며 틱이 100ms 이하일 때까지(최대 30초) 기록을 시작하지 않는다(`settle_s`).
- 함께 본 것: 시작점이 미리 불러오기 구역(접근 + 40m) 안이면 배치하는 순간 미리 불러오기가 시작돼 로그 'ready after'에 배치·스트리밍 시간이 섞인다(북동 3.06s = 대기 2.55s + 0.49s). 첫 프레임이 이미 PRELOADING이면 걷기 시작부터의 상태 시간으로 판정한다. 지연 모의의 감속·트레드밀은 접근 걷기가 지연보다 길면 안 걸리므로(북동 접근 196m ≈ 33초 > 30초) 실행 전체로 센다.
- 범위: PIE에서 폰을 순간 배치한 뒤 프레임 단위로 카메라·끊김을 재는 도구
- 증거: `Saved/WorldGen/PieWalk/p3-19_walk`(실패) ↔ `p3-19_walk2`·`p3-19_walk3`·`p3-19_pace30`·`p3-19_tread15`(전부 통과)
- 날짜·상태: 2026-10-15 active
- 발견: claude

### L-worldgen-06 그림자를 남기는 탑다운 컷아웃 + 카메라 뒤 태양 = 비운 가림물의 그림자만 폰 위에 남는다. 태양 후보는 저장 없이 PIE로 비교한다
- 증상: 사용자 "플레이어를 가리는 오브젝트를 투명 처리하면 그림자만 보여 부자연스럽다". 필드 태양 빛 진행 yaw 38°, 카메라 시선 yaw 45°(3D 각도 6.6°)라 컷아웃 원통 안 물체의 그림자가 폰을 덮는다(모델 계산: 폰 300cm 안 지면 그림자 88.9%, E1 촬영 중앙 휘도 0.02).
- 해결: 빛 진행 수평 방향을 카메라 반대 ±30°로(D-46, 필드 (−70°, 205°)). 고도가 낮은 역광(−55°)은 벽·절벽 그림자가 화면을 덮어, 다섯 후보 촬영 중 −70°가 평균 어두운 비율이 가장 낮았다(0.218 → 0.193). 후보 비교는 `editor_set_sun.py` `save:false`로 에디터 메모리만 바꾸고 `pie_capture_spots.py`를 돌린다. PIE가 미저장 액터·데이터 에셋 변경을 그대로 복제한다.
- 함께 본 것: `EditorLoadingAndSavingUtils.get_dirty_content_packages()`는 OFPA 외부 액터 패키지(태양)의 미저장 변경을 돌려주지 않았다. 저장 대상은 패키지를 직접 지정하고, 미리보기로 이미 바뀐 데이터 에셋은 "값 같음"이어도 미저장이면 저장해야 한다(첫 저장에서 고원 정의가 빠졌다).
- 범위: 탑다운 카메라 + `MF_TD_OcclusionCutout`(그림자 패스 원래 재질) + 위치별 태양 혼합
- 증거: `Saved/WorldGen/PieShots/sun038_before`·`sun225_p55`·`sun225_p65`·`sun205_p65`·`sun205_p70`, `Saved/AgentOps/20261016/sun/geometry.md`, `Docs/Validation/P3-20-sun.md`
- 날짜·상태: 2026-10-16 active
- 발견: claude

### L-worldgen-07 런타임에 스폰한 `ANavMeshBoundsVolume`은 경계가 0이라 오류 없이 내비 타일 0개로 끝나고, 인보커가 활성인 동안 경계를 추가하면 경계 안 모든 타일을 한 프레임에 순회해 멈춘다
- 증상: 무한 세계 벤치(IWBench)에서 런타임 스폰 경계 볼륨 `registered_bounds_box ... Min=(0,0,0) Max=(0,0,0)`, 타일 0, 30m 앞 경로 0/9, 로그 오류 0줄. 인보커 활성 중 ±1e6cm 경계를 추가하자 단일 프레임 89~113ms(타일 798·988cm), 35~39ms(1596cm).
- 원인: 경계는 `GetComponentsBoundingBox(true)`(NavigationSystem.cpp:4163-4179)인데 런타임 스폰 볼륨은 브러시가 없다. 경계가 더러워지면 생성기가 활성 타일 집합과 상관없이 경계 상자 전 타일 이중 루프를 돈다(RecastNavMeshGenerator.cpp:6804-6815, 타일당 17~22ns). `RebuildAll`도 `MarkNavBoundsDirty`로 같은 경로를 탄다(:5785-5806, :6697-6712).
- 해결: 볼륨에 박스 컴포넌트(±1.1e8cm)를 붙여 등록한 뒤 공개 `OnNavigationBoundsUpdated`(NavigationSystem.h:1096)를 부른다. 거대 경계는 인보커 등록 전에 한 번만 만들고 바꾸지 않으며, 인보커 활성 중 RebuildAll·RebuildNavigation 콘솔 명령을 쓰지 않는다. `bFixedTilePoolSize=True`(풀 1024)에서 'too large' 오류 0건.
- 범위: UE 5.8 Dynamic 내비 + 인보커 전용 생성, 런타임 생성 세계(에디터 배치 볼륨은 해당 없음). ±1.1e8 경계 활성 중 추가는 외삽(380~1,330초)만 했다.
- 증거: `Docs/InfiniteWorld/measurements/bench/results/nav-smoke.json`·`nav-T{800,1000,1600}-r{1,2}.json`, `Docs/InfiniteWorld/measurements/bench/REPORT-dynamic.md`, 소스 `Tools/InfiniteWorld/IWBench/Source/IWBench/TDBenchStream.cpp`(SpawnBoundsVolume·TickLateBounds)
- 날짜·상태: 2026-10-23 active
- 발견: claude(무한 세계 가능성 검토)

### L-worldgen-08 런타임 메시 지형 충돌 함정 4가지: 비동기 쿠킹 순서, 삼각형 감김, `FHeightField` 경계 캐시, 정점에 구운 원거리 오프셋
- 증상: ① `UDynamicMeshComponent`에서 비동기 쿠킹을 켜고 SetMesh → RegisterComponent 순서면 등록 때 동기 쿠킹이 한 번 더 일어났다(33x33 등록 2.43ms ≈ 동기 2.49ms). ② 삼각형을 (V00,V10,V11)·(V00,V11,V01)로 감으면 위→아래 수직 트레이스 0/200 적중, 아래→위만 맞는다. ③ `Chaos::FHeightField`를 생성자만으로 만들면 Raycast 0/200. ④ 원거리 오프셋을 정점 좌표에 구우면 1e8cm에서 트레이스 오차 1.7cm, 1e9cm에서 11.6~13.4cm.
- 해결: ① RegisterComponent → SetMesh 순서(등록 0.05~0.07ms, DynamicMeshComponent.cpp:1601-1666, 비동기는 게임 월드에서만). ② (V00,V11,V10)·(V00,V01,V11)로 감아 위를 향하게 한다(bCullsBackFaceRaycast=false인데도 한쪽 면만 맞음, 원인 미확인). ③ 생성 뒤 `BoundingBox()`를 한 번 호출해 CachedBounds를 채운다(HeightField.h:342-347) → 200/200, 오차 ≤0.04cm. ④ 정점은 청크 국소 좌표, 오프셋은 액터 변환에 둔다 → 1e9cm까지 0.0001cm.
- 범위: UE 5.8 런타임 생성 지형(동적 메시·Chaos 높이장), 편집기 빌드 -game 측정. 렌더 정밀도는 미측정.
- 증거: `Docs/InfiniteWorld/measurements/bench/REPORT-static.md`, `.../bench/results/terrain-r{1,2}.json`·`diag-r{1,2}.json`
- 날짜·상태: 2026-10-23 active(②는 원인 미확인)
- 발견: claude(무한 세계 가능성 검토)

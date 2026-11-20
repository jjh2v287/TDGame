# Lessons — gameplay (게임플레이 기반·한 판)

[← 인덱스로](../AgentCollaboration_Plan.md)
종류: 교훈 · 형식은 `Docs/AgentRules.md` 3절 A. 색인: `rg -n "^### L-" --glob '*.md' Docs/Lessons`

### L-gameplay-01 시험 월드에서 UWorldSubsystem이 게임 인스턴스를 조건으로 하면 만들어지지 않는다
- 증상: 사건 기록기 연결 서브시스템(`UTDGameplayRecorderBridge`)이 `ShouldCreateSubsystem`에서 게임 인스턴스 유무를 보자, `InitializeStandalone` 시험 월드에서 서브시스템이 생기지 않았다.
- 원인: 월드 서브시스템은 `UWorld::CreateWorld` 안에서 만들어지는데, 그때는 게임 인스턴스가 아직 월드에 붙지 않는다.
- 해결: `ShouldCreateSubsystem`은 월드 종류(Game·PIE)만 보고, 게임 인스턴스 확인은 `OnWorldBeginPlay`에서 한다. 메시지 서브시스템도 게임 인스턴스가 있어야 `Get`이 되므로 시험 월드는 게임 인스턴스를 만든다.
- 범위: UE 5.8, `Source/TDGame/Framework/TDGameplayRecorderBridge.*`, 게임 인스턴스를 쓰는 모든 월드 서브시스템
- 증거: `Saved/AgentOps/20261118/R-recorder-tests.json`(작업자 관찰, 독립 확인 없음 — 미검증)
- 날짜·상태: 2026-11-18 active

### L-gameplay-02 시험 월드에서 같은 프레임에 World->Tick 을 여러 번 부르면 타이머가 진행하지 않는다
- 증상: 일시정지 메뉴 시험에서 `World->Tick`을 연달아 불러도 타이머가 제때 터지지 않았다.
- 원인: 타이머 관리자는 같은 프레임 번호(`GFrameCounter`)의 중복 틱을 무시한다.
- 해결: 시험에서 틱 사이마다 `++GFrameCounter`를 한다. 함께 본 것: 게임 인스턴스 없이 `CreateWorld`로 만든 월드에서 `UWorld::SetGameMode`는 크래시하고, `UGameInstance::Shutdown`은 월드 컨텍스트를 남긴다.
- 범위: UE 5.8 자동화 시험(`Source/TDGame/Tests/TDPauseMenuTests.cpp`)
- 증거: `Saved/AgentOps/20261118/P1-tests-*`(작업자 관찰), `Saved/AgentOps/20261118/stage1-gp11/R4-refute.md`(셧다운 부분) — 프레임 번호 부분은 미검증
- 날짜·상태: 2026-11-18 active

### L-gameplay-03 에디터 PIE에서 Esc는 PIE 중지 단축키라 게임 입력보다 먼저 받는다
- 증상: Esc 일시정지 메뉴를 PIE에서 확인하려 하면 메뉴 대신 PIE가 꺼진다.
- 원인: 에디터의 PIE 중지 명령이 Esc에 묶여 있어 게임 입력보다 먼저 처리된다(엔진 `DebuggerCommands.cpp:358`, `PlayLevel.cpp:3092`).
- 해결: Esc를 쓰는 화면은 `-game` 실행으로 확인한다.
- 범위: UE 5.8 에디터 PIE
- 증거: `Saved/AgentOps/20261118/stage1-gp11/R4-refute.md`(반박자가 엔진 소스 줄 확인)
- 날짜·상태: 2026-11-18 active

### L-gameplay-04 Mover 폰 부활이 'SpawnActor failed because of collision'으로 취소돼 컨트롤러가 폰 없이 남는다
- 증상: `SpawnActor failed because of collision at [-8303.128,-20015.354,34.424] for BP_TDCombatCharacter_C` → `SpawnDefaultPawnAtTransform: Couldn't spawn` 뒤 플레이어 컨트롤러가 폰 없이 Inactive로 남아 진행이 멈춘다(북 공터 체크포인트, 바닥+100cm).
- 원인: 기본 `SpawnDefaultPawnAtTransform`은 APawn 기본값 `AdjustIfPossibleButDontSpawnIfColliding`을 쓴다(엔진 Pawn.cpp:93). Mover 폰은 캡슐뿐 아니라 질의 충돌이 켜진 메시까지 겹침 검사에 들어가고, 메시 쪽 보정량(MTD, 최소 이동 거리)이 0이라 엔진(UWorld::FindTeleportSpot)이 다른 자리를 시도하지 않고 생성을 취소한다. 캡슐 반높이가 88cm라 바닥+100이면 캡슐 바닥이 지면 위 12cm다.
- 해결: `ATDGameGameMode::SpawnDefaultPawnAtTransform_Implementation` 재정의 — 기본 폰 CDO의 루트 캡슐로 요청 위치 +0/25/75/150cm → 플레이어 스타트 순으로 빈자리를 찾고, 생성은 `AdjustIfPossibleButAlwaysSpawn`(취소 없음). 체크포인트 높이는 캡슐 쓸기 정지 높이 +25cm(배치 도구).
- 범위: UE 5.8, Mover 기반 플레이어 폰의 부활·생성(`Source/TDGame/Framework/TDGameGameMode.cpp`, 시험 `Source/TDGame/Tests/TDRespawnTests.cpp`)
- 증거: `Saved/AgentOps/20261119/gp03/d1-respawn.md`, 수정 전 빨강 `Saved/AgentOps/20261119/GP03-F-red2-tests.json`, PIE `Saved/Gameplay/FieldEncounters/final2/result.json`(세 공터 체크포인트 부활). 실제로 겹친 컴포넌트는 미검증
- 날짜·상태: 2026-11-19 active

### L-gameplay-05 오픈 월드 PCG 볼륨 브러시(WorldStatic, Pawn은 막지 않음)가 WorldStatic 객체 질의에 잡혀 지면 찾기·겹침 검사를 속인다
- 증상: 아레나 몬스터가 지형과 무관하게 한 높이(지면 위 약 2.5m)에서 스폰돼 떨어진다. 배치 도구의 체크포인트 겹침 검사(`capsule_overlap_components`)는 모든 후보를 막힘으로 본다(반박이 예측, 가짜 모듈로 재현).
- 원인: 오픈 월드 전체를 덮는 PCG 볼륨 `TDGen_PCG_BiomeForest`의 브러시는 WorldStatic 객체 유형이고 Pawn은 막지 않는다(엔진 AVolume 기본 프로필은 OverlapAll — Volume.cpp 26-27, 이 인스턴스의 실측 프로필 이름은 Custom). WorldStatic 객체 유형 선 추적은 시작점 초기 겹침으로 그 브러시에 맞고, 객체 유형 겹침 질의는 응답이 겹침이든 막힘이든 컴포넌트를 돌려준다(KismetSystemLibrary.cpp 1806-1821).
- 해결: C++ 지면 찾기는 몬스터 캡슐 자신의 콜리전으로 Pawn 채널 쓸기(`SweepSingleByChannel`), 시작 겹침·법선 Z<0.7은 거부. 파이썬 겹침 검사는 `get_collision_response_to_channel(Pawn)`이 Block인 컴포넌트만 막힘으로 센다(BlockingVolume은 세야 하므로 브러시 클래스째 빼지 않는다).
- 범위: UE 5.8 `LV_DarkFantasy_OpenWorld`의 지면·겹침 질의 전반(`Source/TDGame/World/Generation/TDArenaEncounter.cpp`, `Tools/Gameplay/editor_place_field_encounters.py`). 소환 졸개 `FindGroundHeight`(TDMonsterCharacter.cpp)에는 같은 결함이 남아 있다
- 증거: `Saved/AgentOps/20261119/gp03/d2-spawn.md`(낙하 적분 예측과 관측 z 일치), `fix-refute.md` M1, `f2_checkpoint_filter_check.py` 11/11, PIE final·final2 접지 3/3(발과 지면 차 2~3cm). 에디터 직접 질의로 확인(2026-11-20, `Saved/AgentOps/20261119/gp03/d2-confirm.txt`: 북 공터 두 점에서 WorldStatic 선 추적이 PCGVolume BrushComponent에 시작점 겹침·시간 0으로 맞음)
- 날짜·상태: 2026-11-19 active

### L-gameplay-06 엔진 `Shot`(스크린샷)은 PIE 게임 스레드를 장당 약 200ms(첫 장 약 0.8초) 멈춘다 — 움직이는 화면 측정은 엔진 밖 영역 복사로 한다
- 증상: GF-07 스모크에서 걷는 중 8Hz 로 `Shot filename=<경로> -nosuffix` 를 찍자 걷기 구간 프레임이 20ms 에서 200~245ms 로 늘고, 촬영 간격보다 프레임이 길어 매 프레임 사진이 찍혀 6fps 로 떨어졌다(`smoke1/frames.csv`, 사진 요청 프레임 다음 프레임 길이).
- 원인: Shot 은 다음 그리기에서 백버퍼를 읽어 파일로 쓰며 그 프레임이 통째로 길어진다(실측만, 엔진 안 지연 지점은 미확인).
- 해결: 걷는 동안의 촬영은 `Tools/Gameplay/floor_popping_capture.py`(GDI BitBlt 로 뷰포트 영역만 복사, 1612×715 장당 약 17ms, PNG 압축 1단계·쓰기 스레드 3개, 12Hz)로 하고, 엔진 Shot 은 시작 대기 중 1장만 찍어 에디터 창 캡처 안에서 뷰포트 위치를 찾는 데 쓴다(정규 상관 0.94~0.95). 화면 복사에는 HUD(체력 막대)와 화면 메시지가 들어가므로 측정 동안 `DisableAllScreenMessages`, 분석에서 HUD 영역을 가린다. Pillow `ImageGrab.grab(bbox)` 는 전체 가상 화면을 찍고 잘라 느리다(6400×2160 에서 약 150ms).
- 범위: UE 5.8 에디터 PIE 화면 측정 전반.
- 증거: `Saved/Gameplay/FloorPopping/smoke1/frames.csv`·`result.json`, `smoke2/capture.json`(복사 109장·누락 0, 걷기 프레임 중앙 20.4ms).
- 날짜·상태: 2026-11-20 active

### L-gameplay-07 Mover 플레이어 폰을 파이썬 `set_actor_location`(teleport)으로 매 틱 옮기면 실제보다 훨씬 강한 모션 블러가 나고, 한 번이라도 순간이동시킨 뒤에는 `add_movement_input` 이 폰을 움직이지 않는다
- 증상: GF-07 걷기 흉내(550cm/s 로 매 틱 위치 지정) 화면이 50~100px 줄무늬로 번졌다(카메라 이동 프레임당 약 11cm·기본 모션 블러로 계산하면 약 7~15px). 같은 자리를 실제 이동 입력으로 걸으면 또렷하다. 또 시작 대기에서 매 틱 위치를 고정했거나 `TeleportPawn` 으로 한 번 옮긴 뒤에는 `add_movement_input((-1,0,0))` 을 매 틱 줘도 폰이 그대로였다(아무것도 옮기지 않은 PIE 에서는 약 600cm/s 로 걷는다).
- 원인: 미검증(블러는 스프링 암 지연·Mover 보정·동작 벡터의 상호작용, 입력 무시는 순간이동 뒤 Mover 상태로 추정).
- 해결: 화면을 보는 걷기 측정은 순간이동 없이 PIE 시작 위치에서 실제 입력으로 한다(`pie_floor_popping.py --drive input`). 순간이동 측정은 도착 뒤 정지 화면만 쓴다. 흉내 걷기로 잰 스트리밍 지표(카메라 위치만의 함수)는 그대로 쓸 수 있다.
- 범위: UE 5.8 Mover 기반 `BP_TDCombatCharacter`, 에디터 파이썬 PIE 도구(`editor_pie_probe.py` 의 hold 뒤 walk 도 같은 위험).
- 증거: `Saved/AgentOps/20261120/gf07/walk_direct_grid.png`(입력 걷기 또렷)·`base_a_v2_big.png`(흉내 걷기 번짐)·`input_test.json`·`input_test2.json`, `Saved/Gameplay/FloorPopping/mb_input`(이동 0).
- 날짜·상태: 2026-11-20 active(원인 미검증)

### L-gameplay-08 파이썬 `add_movement_input` 방향은 단위 길이로 준다 — 길이가 큰 벡터를 주면 폰이 멈춘다
- 증상: 방향 (−1,0,0) 은 약 600cm/s 로 걷는데, (−150,0,0) 을 주자 0.6초 안에 멈춰 그 뒤 움직이지 않았다.
- 원인: 미검증(입력 벡터 크기 제한·정규화 경로 추정).
- 해결: `length = hypot(dx, dy)` 로 나눈 단위 벡터와 배율 1.0 을 준다(`editor_floor_popping_probe.py` `steer_pawn`, `editor_pie_probe.py` `walk_toward` 와 같은 꼴).
- 범위: UE 5.8 Mover 기반 플레이어 폰.
- 증거: `Saved/AgentOps/20261120/gf07/input_test.json`(0~1초 이동, 1~2초 정지).
- 날짜·상태: 2026-11-20 active

### L-gameplay-09 12Hz 화면 복사를 PNG 로 남기면 실행당 3~4GB — 측정 몇 번에 C: 가 차서 실행이 실패한다
- 증상: GF-07 2차 창에서 걷기 실행이 중간에 멈추고 C: 남은 공간 0GB. 화면 복사 1612×715 PNG 장당 약 3.3MB, 걷기 한 번(약 1,300장)에 4GB 넘게 쌓였고, 작업 중단으로 남은 nvidia-smi 기록 프로세스도 그대로 돌고 있었다.
- 해결: 화면 복사를 JPEG 품질 95·색 축소 없음(4:4:4)으로 바꿨다(장당 약 0.86MB, PNG 와의 평균 차 0.009 — 팝 판정 문턱 0.303 보다 훨씬 작음). 다 쓴 실행의 `grabs/` 는 분석 뒤 지운다(이번에 45.6GB 정리). 측정 일괄 실행 전 `df -h /c` 로 실행 수 × 1GB 이상 남았는지 본다. 드라이버 정리 순서는 GPU 기록기 종료 → 화면 복사 종료 → PIE 정지로 해, 중간 실패에도 자식 프로세스가 남지 않게 했다.
- 범위: `Tools/Gameplay/floor_popping_capture.py`·`pie_floor_popping.py`, 같은 방식의 다른 화면 복사 도구.
- 증거: `Saved/AgentOps/20261120/gf07/`(창 2 로그), 같은 장면 한 장 PNG 3.27MB·JPEG 855KB(2026-11-20 직접 비교), JPEG 로 찍은 실행 `Saved/Gameplay/FloorPopping/w2_tp_off2` 이후.
- 날짜·상태: 2026-11-20 active

### L-gameplay-10 '도착 뒤 정착 시간'(마지막 사진 대비 T_pop 넘는 블록 1% 이상인 마지막 시각)은 바람에 흔들리는 식물 화면에서 응시 길이로 포화한다
- 증상: GF-07 순간이동 응시 정착 시간이 원래·최종 모두 5.0~5.1초(응시 길이)로 나와 등록 판정이 '악화'로 보였다. 지점별 곡선은 0.2~4.9초 내내 0.03~0.08 로 평평 — 연속 사진끼리도 덤불잎·풀이 T_pop(0.303)을 넘어 1% 를 넘는다. 모션 블러를 끄면 흔들리는 잎이 또렷해져 포화 지점이 2곳 늘었다.
- 해결: 등록 전 시운전에서 응시 마지막 두 사진끼리 바뀐 블록 비율이 문턱(1%)보다 충분히 작은지 먼저 본다. 넘으면 '마지막 사진 대비' 대신 '늦은 구간 잡음 수준 + 여유'를 기준으로 하거나, 도착 0.1·0.3·0.5초의 화면 칸 변화 비율(마지막 4장 평균 기준, `judge_floor_popping.py` 보조 지표)로 잰다.
- 범위: `analyze_floor_popping.py` 응시 정착, 식물이 많은 필드 화면의 모든 '정착' 지표.
- 증거: `Saved/AgentOps/20261120/gf07/judge_same_session/judgment.md`·`judge_cross/judgment.md`, `Docs/Validation/Gameplay/GF-07-floor-popping.md` 6.3.
- 날짜·상태: 2026-11-20 active

### L-gameplay-11 에디터 PIE 프레임 p95 는 세션 간에 비교하지 않는다 — 엔진 범주 밖 게임 스레드 시간이 에디터 재시작·다른 세션 GPU 시험에 따라 튄다
- 증상: GF-07 수정 전(2차 창)·후(에디터 재시작 직후) 걷기 FrameTime p95 23.0 → 27.4ms 로 회귀 판정선(1.5ms)을 넘었다. 범주별로 보면 변경 코드가 도는 범주(Tickables·TickActors·Camera·RenderAssetStreaming 대기) 합은 0.98~1.04ms 로 같았고, 늘어난 것은 어느 Exclusive/GameThread 범주에도 안 잡히는 시간(p95 7.4·9.4 → 15.6·10.9ms)뿐이었다.
- 해결: 회귀 판정은 같은 에디터 세션 안에서 원래 설정(콘솔 변수로 끔)과 최종을 순서 교대(원·최·최·원)로 잰다 — 같은 세션에서는 22.63 → 22.50ms 로 차이가 없었다. 범주 귀속은 `Tools/Gameplay/judge_floor_popping.py` 실행별 표(범주 밖 GT p95·변경 코드 범주 합).
- 범위: PIE 성능 비교 전반(CSV 프로파일러 FrameTime·GameThreadTime). GPUTime 은 덜 흔들렸다.
- 증거: `Saved/AgentOps/20261120/gf07/judge_cross/`·`judge_same_session/`, 측정 w2_ww_base_*·w3_final_ww_*·w5_ww_*.
- 날짜·상태: 2026-11-20 active

### L-gameplay-12 목적지 텍스처 미리 요청: 엔진은 안 그려진 물체에 배율을 1.5 까지만 주고, 측정 도구가 다음 목적지 요청을 도착 순간에 걸면 '도착 뒤 대기'에 다음 지점 미리 받기가 섞인다
- 증상: `IStreamingManager::AddViewLocation` 배율 2 로 미리 요청을 켜자 서쪽 공터 도착 대기(71MB·1.5초)는 0 이 됐지만 다른 지점에서 도착 직후 32~71MB 대기가 새로 보였다. 꼬리 2초→0, 배율 2→1.5 로 바꿔도 그대로였다.
- 원인: (1) 엔진 `FAsyncRenderAssetStreamingData` 는 배율이 `r.Streaming.MaxHiddenPrimitiveViewBoost`(기본 1.5)를 넘으면 안 그려진 물체에는 1.5 만, 보이는 물체에는 나머지를 더 준다 — 숨은 목적지에는 1.5 넘는 배율이 이득 없이 보이는 물체만 밉을 더 요청한다. (2) 탐침(`editor_floor_popping_probe.py`)의 `prestream_next` 는 직전 구간이 시작될 때(= 현재 지점 도착 순간) 다음 목적지를 요청해, 도착 직후 대기가 '다음 지점 미리 받기'였다(화면 변화 곡선은 스냅만 켠 것과 같음).
- 해결: 기본 배율 1.5(`TD.Streaming.PrestreamBoost`). 미리 요청 효과는 '목적지 도착 뒤 대기'만 따로 보거나, 요청 시각을 구간 끝 N초 전으로 옮겨 잰다.
- 범위: `UTDTexturePrestreamSubsystem`, 미리 요청을 쓰는 측정 전반.
- 증거: `Saved/AgentOps/20261120/gf07/prestream_variants_quick.json`, 측정 w5_tp_*.
- 날짜·상태: 2026-11-20 active

### L-gameplay-13 Mover 의 '제자리 방향 바꾸기' 순간이동 효과도 `AActor::TeleportTo` → `TeleportSucceeded` 를 부른다 — 순간이동 훅은 이동 거리를 보고, 컷 플래그는 그리는 실행에서만 켠다
- 증상: GF-07 의 순간이동 카메라 스냅·컷(`ATDGameCharacter::TeleportSucceeded`)이 공격 시작·구르기·키보드 조준마다 걸려 카메라가 덜컥이고 카메라 컷이 TSR·Lumen·볼륨 안개 이력과 자동 노출을 초기화했다(감독 세션 독립 검토). 카메라 컷 0회를 단언하는 이계 PIE 시험(RealmConnectorWalk·RealmLiftRide·RealmLiftTreadmill)이 컷 2,145~3,220회로 실패했다.
- 원인: `ATDCombatCharacter::FaceRotationImmediately` 의 `FTeleportEffect`(현재 위치 그대로)를 Mover 가 `OwnerActor->TeleportTo` 로 적용하고, 엔진 `AActor::TeleportTo` 는 이동 0 이어도 성공하면 `TeleportSucceeded(false)` 를 부른다. 시험 걷기도 매 프레임 `TeleportPawnToDestination`(60cm 걸음)이다. 헤드리스(`-NullRHI`)는 그리기가 없어 `bGameCameraCutThisFrame` 을 지우는 쪽이 없다(L-editor-18) — 빙의 때 한 번 켠 컷이 끝까지 남았다.
- 해결: 스냅·컷은 직전 틱 위치(`BeginPlay` 에서 초기화)에서 10m 넘게 옮겨졌을 때만, 빙의 컷 제거(부활은 새 폰이라 효과 없었음), 엔진 컷 플래그는 `FApp::CanEverRender()` 일 때만 켠다(요청 횟수는 시험용으로 셈). 시험 `TDGame.Camera.TurnInPlaceTeleportKeepsCameraLag`·`SmallTeleportStepKeepsCameraLag`·`LongTeleportSnapsCameraLag`·`HeadlessLeavesNoCameraCutFlag`(빨강 3 → 초록), 이계 PIE 헤드리스 6/6·GUI 컷 0회.
- 범위: `TeleportSucceeded`·`TeleportTo` 를 훅으로 쓰는 모든 코드(Mover 순간이동 효과는 회전에도 쓰인다), 컷 플래그를 켜는 코드.
- 증거: `Saved/AgentOps/20261120/gf07-camfix-red-tests.json`·`gf07-camfix-seamless-tests.json`·`gf07/gui_realm_tests.txt`, 검토 `Saved/AgentOps/20261120/gf07-review/correctness.md`.
- 날짜·상태: 2026-11-20 active

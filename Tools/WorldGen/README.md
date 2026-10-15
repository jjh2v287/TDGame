# WorldGen — 잿빛 골짜기(Ashen Vale) 메인 지역 생성 파이프라인

설계서(`Docs/UE5_탑다운_ARPG_월드_던전_PCG_설계서.docx`) 4장 원칙대로 **월드 뼈대(마을·던전 입구·POI·도로·강)는 코드 안에 수작업으로 정의**하고, 환경(숲·바위·덤불·잔해·물·안개)은 시드 결정론 생성기로 채운다. 결과는 에디터 안에서 베이크되어 `LV_DarkFantasy_OpenWorld`(월드 파티션)에 저장된다.

## 한 번에 실행

```bash
python Tools/WorldGen/generate_ashen_vale.py --seed 7          # 1) 높이맵·레이어·배치 → Saved/WorldGen/AshenVale/
python Tools/run_in_editor.py Tools/WorldGen/editor_build_open_world.py   # 2) 에디터에 반영 + 저장 (에디터 실행 중, Python 원격 실행 켜짐 필요)
```

처음 한 번(또는 머티리얼을 바꿀 때):
```bash
python Tools/run_in_editor.py Tools/WorldGen/editor_make_landscape_material.py   # M_TD_Landscape, MI_TD_Landscape, 풀 타입 4종
```

## 파일

| 파일 | 역할 |
|---|---|
| `generate_ashen_vale.py` | numpy 생성기. 지형(고원·고지대·습지·강·호수·테두리 산맥·도로 평탄화), 6개 레이어 가중치, 숲/바위/덤불/잔해 산포, 마을·POI 8종·던전 입구 3개·전투 공터 3개·교량·널길 프리팹, 물·안개·물웅덩이 평면, 조명, 나이아가라, 마커. `preview.png`로 상공 검토. |
| `editor_build_open_world.py` | 에디터 Python. 레벨 정리 → C++ `UTDLandscapeEditorLibrary.CreateLandscapeFromRawFiles`로 랜드스케이프 생성(레이어 인포 자동 생성, 월드 파티션 프록시 분할) → 대기·조명·후처리 → `ATDInstancedMeshActor`(HISM) 셀 액터 → 고유 액터/평면/조명/FX/마커 → 저장. 모든 생성물은 라벨 `TDGen_*`, 태그 `TDGen`, 아웃라이너 폴더 `TDGen/…`. |
| `editor_make_landscape_material.py` | 레이어 블렌드 머티리얼(Soil/Moss/Mud/Road/Rock/Bedrock) + 거시 변화 + 랜드스케이프 풀 출력. 풀 타입: 재 풀, 숲 바닥(균사·버섯·낙엽), 습지 갈대, 자갈. |
| `realm_passages.py` / `generate_realm.py` / `editor_build_realm.py` | 연속 세계 공유 정의(이계 배치·연결로·승강 갱도, 난수 없음) / 이계 생성(에디터 밖) / 이계 베이크. 아래 '연속 세계'. |
| `check_continuous_world.py` | 연속 세계 결정론 검사기(필드·두 이계 출력을 이어붙여 K2~K8·시야·거리 보장·능선 차단·라이너·재질 혼합·옛 키), 실패 시 종료 코드 1. |
| `editor_setup_data_layers.py` | 데이터 레이어 5개(Editor 지역 3·Runtime 게임플레이 2)와 레벨 인스턴스를 멱등 생성하고 빌더가 쓰는 DL 문맥 함수를 제공. |
| `editor_make_pcg_biome.py` / `editor_check_pcg_determinism.py` | P3-07. `/Game/World/PCG/PCG_TDBiome_Forest`+서브그래프 5종·`/Game/Dungeon/PCG/PCG_TDRoomDressing` 생성, `TDGen_PCG_BiomeForest` PCGVolume(파티션+HiGen) 배치·생성 / 같은 시드 2회 생성 후 ISM 수·위치 해시 비교 → `Saved/WorldGen/pcg_determinism.json`. 결과 `Docs/Validation/P3-07-pcg-biome.md`. |
| `../run_in_editor.py` | 엔진 `remote_execution.py`로 에디터 안에서 스크립트 실행. |

C++ 도구: `Source/TDGameEditor/Landscape/TDLandscapeEditorLibrary.*`(에디터 모듈), `Source/TDGame/World/Generation/TDInstancedMeshActor.*`(런타임 컨테이너).

## 좌표·규격
- 필드 랜드스케이프 1009×1009 정점, 쿼드 1m(스케일 100), 16×16 컴포넌트(63쿼드·1섹션), 월드 파티션 그리드 2컴포넌트 → 8×8 스트리밍 프록시(각 126m). 중심 (0,0), 범위 ±504m(필드 상자 ±50400cm). 이계 두 곳은 이 상자 남쪽·동쪽에 붙어 있다(아래 '연속 세계').
- 높이 인코딩: 32768 + 미터×128 (Z 스케일 100). 물 높이 −1.75m. 이계 `height.r16`은 이계 로컬 높이이고 월드 높이 = 로컬 + Zoff(`layout.json` realm.center_cm의 z)다.
- 마을 중심 (−110, 20)m, 반지름 92m. 외곽 던전 입구 표식 3곳(생성기 `MAIN_DUNGEON`·`SIDE_DUNGEON_NW`·`SIDE_DUNGEON_SE`): MainCrypt (296, −268), HollowCave (−372, −262), SunkenCrypt (318, 412). P3-19 이후 연결로는 이 표식과 따로 있다(북동 연결로만 SunkenCrypt 표식 옆에서 시작). 옛 남서 협곡·남동 구덩이는 되메웠다.
- 도로: 평탄화 폭 3.5~7m + 6m 완만, `Road` 레이어. 강: 바닥 −3.1m, 폭 약 9m, 주변 계곡 완화. 남쪽 가장자리 30~60m 안에서는 강바닥을 테두리 높이로 되메워 협곡 끝 벽으로 끝내고, 강 물판은 필드 상자 안 Y ≥ −439m만 남긴다(U-4).

## 지형·도로·강·바이옴 마스크 절차 (P3-00, 2026-09-30 정본 결정)
- 정본: 높이맵·레이어는 numpy 생성기 `generate_ashen_vale.py`가 만든다(C++ `FTDHeightmapGenerator`는 만들지 않음, 사용자 결정 2026-09-30). 같은 시드면 `height.r16`·`layer_*.r8`·`layout.json`(출력 경로 문자열 제외)이 바이트 단위로 같다.
- 랜드스케이프 생성: C++ `UTDLandscapeEditorLibrary::CreateLandscapeFromRawFiles(월드, FTDLandscapeCreateRequest)`가 raw 높이맵(uint16)·레이어(uint8)를 읽어 `ALandscapeProxy::Import` → 레이어 인포 자동 생성(`/Game/World/Landscape/Layers`) → `ULandscapeSubsystem::ChangeGridSize`로 월드 파티션 스트리밍 프록시 분할(그리드 2 = 8×8 프록시). 배열 직접 입력·텍스처 임포트 함수는 두지 않는다(raw 파일 경로로 대체).
- 도로·강: 스플라인이 아니라 생성기가 높이맵에 직접 새긴다(도로 평탄화 3.5~7m + 6m 완만 경사와 `Road` 레이어, 강 바닥 −3.1m·폭 약 9m 계곡). 사후 손질이 필요하면 에디터 랜드스케이프 스플라인 도구로 고치고, 재생성 시 덮어써지므로 생성기 좌표(코드 상수)에 반영한다.
- 바이옴 마스크: 레이어 가중치 raw 6종(Soil/Moss/Mud/Road/Rock/Bedrock)을 임포트해 웨이트맵으로 쓰고, PCG 바이옴 그래프가 레이어 이름으로 샘플한다(P3-07). 렌더 타깃 경유 임포트는 쓰지 않는다.
- 결정론 확인: `python Tools/WorldGen/generate_ashen_vale.py --seed 7 --out <A>`와 `--out <B>`를 실행해 두 폴더 파일의 SHA-256을 비교한다(증거 `Docs/Validation/P3-00-landscape.md`).

## 다시 만들 때
`editor_build_open_world.py`(필드 빌더)는 랜드스케이프를 `TDRealm` 태그가 없는 것만 지운다(C++ `DestroyLandscapeActorsWithoutTag`, 위치 상자 없음). 그 밖의 액터는 `DL_TDRegion_Field` 소속, 전환기 규칙(지역 DL 없음 + 이전·새 layout 라벨과 `TDGen` 태그, 그 밖의 layout 라벨·`TDGen_ISM_`는 ±50450cm 상자 안), `TDGen_SeamPcgExclusion_`만 지운다. 이계(`TDRealm` 접두·태그·이계 DL)·던전·PCG·대기 5종·PlayerStart·`KEEP_CLASSES`는 보존한다. 안전장치: 이계마다 랜드스케이프가 17개(부모 1 + 프록시 16)가 아니면 지우기 전에 멈추고, 지운 뒤 필드 랜드스케이프는 65개(부모 1 + 프록시 64)여야 한다. 보존 기준은 라벨이 아니라 DL과 태그다. 이미 있는 랜드스케이프 프록시에 DL을 나중에 붙이지 않는다(이름 해시가 어긋나 랜드스케이프 작업 때 편집기가 멈출 수 있다, 명세 2.4). 손으로 다듬은 액터를 남기는 잠금 기능은 할 일 P3-06.

## 연속 세계: 이계 배치·연결로·승강 갱도·데이터 레이어(P3-19, D-45)
이계 2곳(각 504m×504m, 정점 505×505, 컴포넌트 8×8, 프록시 16)은 같은 월드 파티션 레벨에서 필드 상자 바로 옆에 붙어 있다. 경계에서 두 랜드스케이프의 정점이 정확히 겹치고(공유 줄 u16 차이 0, 이계 생성기가 필드 `height.r16`·레이어를 복사), 걸어서(남서·북동) 또는 실제로 40m 내려가는 승강기(남동)로 넘어간다. 순간이동·카메라 컷·암전·평행이동 이음매는 없다. 콘솔 `TDTravelToDungeon <GateId>`는 디버그용 암전 이동으로 남아 있다(도착 = 관문 Arrival, 승강 관문은 하단 착지 앞 400cm라 XY가 필드 상자 안이다). 던전 생성기·아틀라스·슬롯 안 출구(`TDGen_Exit_*`)는 그대로다.

좌표의 정본은 공유 모듈 `realm_passages.py` 한 곳이다: `REALM_PLACEMENT`(이계 중심·Zoff), `CONNECTOR_ROWS`(걷는 연결로 중심선), `SHAFT_ROWS`(승강 갱도), `LIFT_MOTION`(운행 수치). 난수가 없고 두 생성기가 같은 해석식으로 각자 격자를 깎는다.

| 이계 | 중심(m) / Zoff(m) | 범위 X / Y(m) | 공유 변(필드 ↔ 이계 정점) | u16 규칙 | 지형 재질 원점 오프셋(m) |
|---|---|---|---|---|---|
| `SunkenStarfield` 가라앉은 별밭 | (−63, −756) / −34 | −315..189 / −1008..−504 | 필드 iy 0, ix 189..693 ↔ 이계 iy 504 (505개) | 이계 = 필드 + 4352 | (189, −504) |
| `PaleHeights` 창백한 고원 | (756, 452) / +18 | 504..1008 / 200..704 | 필드 ix 1008, iy 704..1008 ↔ 이계 ix 0, iy 0..304 (305개). 이계 북쪽 Y 505..704는 필드 밖 | 이계 = 필드 − 2304 | (1008, 704) |

- 이계 쪽 공유 변 45m 경계 띠가 필드 가장자리 높이로 이어진다(필드는 바꾸지 않음). 필드 남·동 테두리 안쪽 30m는 걸을 수 없는 급경사(46° 이상, Mover 한계 44.77°)라 걸어서 넘을 수 있는 곳은 연결로 회랑뿐이다(보이지 않는 벽 없음, U-6).
- 이계 지형 MI는 `MF_TD_RegionBlend`로 필드 상자에서 멀어질수록(폭 60m) 이계 색을 섞는다(공유 변 w = 0). 이계 바위 MI는 탑다운 거울(`MI_TD_Cliffs1`·`MI_TD_Rocks`) 자식, 연결로 소품은 경계 양쪽에서 같은 `MI_TDRealm_<R>_Connector*` MI를 쓴다.

연결로(월드 cm, 정본 `Saved/WorldGen/Realms/<R>/layout.json` realm.gates[], 필드 `layout.json` connectors[]와 같다. FieldReturn·Arrival z는 바닥 + 120):

| GateId | 종류 | 지선(폭) | 필드 쪽 시작 FieldReturn (yaw) | 경계 통과 B(바닥 z) | 이계 도착 Arrival (yaw) | 접근 / 이탈 길이 | 바닥 |
|---|---|---|---|---|---|---|---|
| `SunkenStarfieldSouthwest` | 걷기 | (−25000,−30000)→(−25000,−44000), 5m | (−25000, −44000, 1365) (270) | (−25000, −50400) (−35) | (−25000, −67234, −3280) (270) | 6400 / 16834 | 협곡 20% 122m → 단 −12m 28m → 분지 경사로 26.8%(15°) 82m |
| `PaleHeightsNortheast` | 걷기 | 없음(SunkenCrypt 표식 옆) | (31508, 41131, 369) (16) | (50400, 46560) (1796) | (59300, 49100, 2558) (16) | 19657 / 9255 | 8.0·7.5·6.5% 오르막, 고원 쪽은 구름 바닥 위 바위 다리(둑길) |
| `SunkenStarfieldSoutheast` | 승강 | (4000,−32000)→(5452,−44052), 4.5m | (5452, −44052, 720) (315) | 출구 슬롯 (6600, −50400) (−3400) | (893, −56107, −3280) (225) | 5300 / 11748 | 상단 협곡 +6m 평평 → 갱도 40m → 출구 슬롯 −34m 평평(폭 12m) |

남동 승강 갱도와 트레드밀:
- 축 (9200, −47800)cm, 틀 yaw 45. 상단 착지 z 600(출구 yaw 135, 상단 협곡), 하단 착지 z −3400(출구 yaw 225, 출구 슬롯), 낙차 40m. 덱 윗면 반폭 300cm.
- 정상 운행: 순항 300cm/s, 가속 100, 감속 80cm/s²(약 16.7초), 탑승 대기 1.2초, 빈 덱 호출 600cm/s. 런타임 `ATDRealmLiftDeck`가 덱을 실제로 움직인다(대체 순간이동 없음).
- `ATDRealmShaftLiner`가 깊이 6~37.5m에 바위벽 링 7개(모듈 450cm, 패널 `/Engine/BasicShapes/Plane` + `MI_TD_RockLocal_ShaftLiner`, UV 기준 무늬)를 둔다. 정적 소품·조명은 깊이 0~6m와 37.5~40m에만 있다. 덱 아래 600cm에 검은 심연 가림막(바닥 600cm 안·정지 중에는 숨김).
- 트레드밀: 덱이 진행 1350cm(= 대기 깊이 1912.5 − 300²/(2·80))에 닿을 때 목적지가 준비되지 않았으면 덱은 감속해 1912.5cm에 서고 링이 대신 움직인다(덱 속도 + 링 속도 = 300cm/s). 준비되거나 20초가 지나면(로그 "treadmill timeout, proceeding") 다음 모듈 경계에 맞춰 링을 감속하고 덱을 가속해 운행을 잇는다. 로그 "RealmLift '<GateId>': treadmill engaged/released".

런타임(C++):
- `UTDRealmSubsystem`: 걷는 연결로는 FieldReturn→Arrival 직선 축 진행도로 가중치 w를 정한다(−접근 길이 0, B 0.5, +이탈 길이 1, 두 smoothstep). 갱도는 덱 깊이 smoothstep, 출구 슬롯은 1, 이계 상자 안은 1. w로 대기·태양(32단계)·후처리를 섞는다. 진행 방향 쪽 선로딩을 걸고, 경계 −25m~−3m에서 준비가 안 됐을 때만 걷기 감속(최저 배율 0.3, 드문 안전망, 로그 "Realm: pacing engaged/released gate=… along=… ready=… elapsed=…")을 건다. 연결로 w > 0 또는 이계 상자 안이면 그 이계의 `DL_TDGameplay_*`를 Activated로 한 번 바꾸고 로그를 남긴다("Realm: gameplay data layer '…' of realm '…' set to Activated").
- `UTDSeamlessTravelSubsystem`: 이계 관문 준비 판정은 Arrival(들어갈 때)·FieldReturn(나올 때) 반경 200m 질의 소스 + 활성 DL로 `IsStreamingCompleted`를 본다(로그 "SeamlessTravel: '<GateId>' ready after …s"). 시험용 지연 `td.Realm.SimulatedPreloadDelaySeconds`로 트레드밀·감속을 강제할 수 있다.

데이터 레이어(`/Game/World/DataLayers/`, `editor_setup_data_layers.py`가 멱등으로 만든다):

| 자산 | 형식 | 초기 상태 | 담는 것 |
|---|---|---|---|
| `DL_TDRegion_Field` | Editor | — | 필드 빌더가 만든 모든 액터(랜드스케이프와 프록시 64개 포함), PCG 배제, 필드 내비 경계 |
| `DL_TDRegion_SunkenStarfield` / `DL_TDRegion_PaleHeights` | Editor | — | 그 이계의 랜드스케이프·소품·후처리·표식과 그 이계로 가는 연결로 소품(필드 상자 안 부분 포함) |
| `DL_TDGameplay_SunkenStarfield` / `DL_TDGameplay_PaleHeights` | Runtime | Loaded | 보이지 않는 논리 액터(스포너·트리거)만. 지금은 비어 있다 |

- DL 밖: `AWorldDataLayers`, WorldSettings, RecastNavMesh, 전역 대기 5종, PlayerStart, LevelBounds, MiniMap, PCG 월드·분할 액터. 빌더가 만든 그 밖의 액터는 지역 DL을 정확히 1개 가진다.
- 지역 DL이 Editor 형식이라 런타임 스트리밍은 지금처럼 거리(셀 252m, 로딩 범위 768m)로 한다. 빌더는 지역 R을 만드는 동안만 R의 DL을 편집 문맥에 넣는다. 끄기: 에디터 Python 안에서 `os.environ['TD_WORLDGEN_DATA_LAYERS'] = '0'`(전체) 또는 `TD_WORLDGEN_LANDSCAPE_DATA_LAYER = '0'`(랜드스케이프만 태그 키로 관리). PowerShell에서 설정한 환경 변수는 이미 떠 있는 에디터에 전달되지 않는다.

라벨·폴더·태그 규약:

| 대상 | 라벨 | 아웃라이너 폴더 | 태그 / DL |
|---|---|---|---|
| 필드 랜드스케이프(부모 + 프록시 64) | 엔진 기본(빌더가 정하지 않음) | — | `TDGen`, `TDRegion:Field` / `DL_TDRegion_Field` |
| 이계 생성물(지형·ISM·액터·평면·조명·FX·마커·내비 경계·후처리) | `TDRealm_<RealmId>_*` | `TDRealm/<RealmId>/<종류>` | `TDGen`, `TDRealm`, `TDRealm:<RealmId>`(랜드스케이프는 이 태그로 지운다) / `DL_TDRegion_<RealmId>` |
| 연결로 소품(경계 양쪽, 이계 소유) | `TDRealm_Connector_<GateId>_*` | `TDRealm/Connectors/<GateId>` | `TDRealm`, `TDRealm:<RealmId>`, `TDConnector` / `DL_TDRegion_<RealmId>` |
| 연결로 PCG 배제 상자(필드) | `TDGen_ConnectorPcgExclusion_<GateId>`(TriggerBox, 충돌 없음) | `TDGen/Markers` | `TDExclusion` / `DL_TDRegion_Field` |
| 에셋 | `DA_TDRealm_<RealmId>`(관문·승강 lift·`gameplay_data_layer`), `MI_TD_Landscape_<RealmId>`, `/Game/World/Realms/<RealmId>/Materials/MI_TDRealm_<RealmId>_*` | — | — |

옛 이음매 잔재(`TDRealm_Passage_*`, `TDGen_SeamPcgExclusion_*`, `TDRealm_Gate_*`, 먼 좌표 (−302400, ±302400)cm의 옛 이계)는 S4 재베이크에서 지웠다.

다시 만드는 순서(S4, 2026-10-15 실행 완료. 명령·확인 항목 전문은 `Saved/AgentOps/20261015/p3-19/s4_runbook.md`, 에디터 단계는 PowerShell, 먼저 `python Tools/ue_editor.py ensure`):
0. C++ 빌드 뒤 `python Tools/ue_editor.py restart`(새 클래스, L-build-01). 다른 세션의 헤드리스 시험이 없는지 본다(L-build-05). `Saved/WorldGen/AshenVale`·`Realms`를 `*_prev_p3-19`로 복사한다.
1. 생성(에디터 밖): `python Tools/WorldGen/generate_ashen_vale.py --seed 7` → `python Tools/WorldGen/generate_realm.py --realm all` → `python Tools/WorldGen/check_continuous_world.py`(셋 다 종료 코드 0). 1을 다시 하면 이계 생성과 검사도 다시 한다.
2. `python Tools/run_in_editor.py Tools/WorldGen/editor_setup_data_layers.py`(두 번 실행해도 인스턴스 5개).
3. 재질: `editor_make_topdown_materials.py` → `editor_make_landscape_material.py`.
4. 필드 부분 재베이크: `Saved/WorldGen/open_world_build_request.json`에 `{"mode":"partial","previous_layout":"<AshenVale_prev_p3-19>/layout.json"}`를 BOM 없이 쓰고(`[System.IO.File]::WriteAllText`, L-repo-09) `editor_build_open_world.py` → `Saved/WorldGen/open_world_build_report.json`의 `landscape.mode`(`data_layer` 정상)·랜드스케이프 수(필드 65, 이계 17씩) 확인 → 요청 파일 삭제.
5. 이계 베이크 `editor_build_realm.py`(대상 목록 `Saved/WorldGen/Realms/build_targets.json`이 없으면 두 이계 전부) → `Saved/WorldGen/Realms/<R>/build_report.json` 확인.
6. `editor_make_definitions.py`(HollowCave z 갱신) → `editor_setup_navmesh_dynamic.py`.
7. PCG: `Saved/WorldGen/topdown_apply_request.json`에 `{"patch_pcg": true}` → `editor_apply_topdown_materials.py` → `Saved/WorldGen/pcg_filter.txt`에 BOM 없이 `TDGen_PCG_BiomeForest` → `Tools/editor_regenerate_pcg.py` → 필터 파일 삭제. `editor_make_pcg_biome.py`는 다시 돌리지 않는다(다시 돌리면 TDRealm 랜드스케이프를 볼륨 범위에서 뺀다).
8. 요청 파일을 지우고 `editor_apply_topdown_materials.py`(베이크가 팩 재질로 되돌리므로 베이크마다 다시) → HLOD 액터가 있으면 재생성 → 저장, `git status`(Saved·`__pycache__`·의도하지 않은 .uasset 없음)·`git lfs status`.
`bash Tools/WorldGen/rebuild_all.sh [--material]`는 1~6과 8의 재질 적용을 한 줄로 돈다(검사기가 실패하면 레벨을 건드리기 전에 멈춘다, 필드 베이크 모드는 요청 파일을 따른다).

검증:
- 결정론·형상(에디터 밖): `python Tools/WorldGen/check_continuous_world.py`(K2~K8·K15 재질 혼합·라이너 부등식·옛 키, 실패 시 종료 코드 1, 보고 `Saved/WorldGen/ContinuousCheck/report.json`).
- 헤드리스: `TDGame.WorldGen.Realm.*`, `TDGame.RealmLift.*`, `TDGame.Realm.*`, PIE `TDGame.SeamlessTravel.*`(연결로 걷기·승강 운행·트레드밀·콘솔 왕복).
- 실제 이동 입력(에디터 창을 앞에 둔다, L-editor-06): `python Tools/WorldGen/pie_walk_passages.py --run <이름>`(SW·NE 지선 끝↔도착 왕복, SE 승강 왕복, K1·K11~K14·K17 참고 판정), 감속 강제 `--gates SunkenStarfieldSouthwest,PaleHeights --simulate-delay 30`(K13), 트레드밀 강제 `--gates SunkenStarfieldSoutheast --simulate-delay 15`(K11).
- 페이드: `APlayerCameraManager::FadeAmount`는 UPROPERTY가 아니어서 Python(`editor_pie_probe.py`의 `fade_amount`)으로 읽을 수 없고, 걷기 도구 보고의 페이드 칸은 늘 "측정 불가"다. 페이드 0회와 카메라 컷 0회(`OnWorldPostActorTick`에서 카메라 갱신 뒤 `bGameCameraCutThisFrame` 집계)는 C++ PIE 시험 `TDGame.SeamlessTravel.RealmConnectorWalk`·`RealmLiftRide`·`RealmLiftTreadmill`이 판정한다.
- 시야·경계 촬영: `python Tools/WorldGen/pie_capture_spots.py --run <이름> [--ids B01,B02,B03] [--compare <이전 run>]`(지점 36곳: 연결로 입구 C01~C03, 경계 통과점 B01~B03(K15), 갱도 상단 F12·하단 S05, 분지 S01·S04, 메사 P04 등).
- P1 왕복: `python Tools/WorldGen/pie_p1_checks.py --ids MainCrypt,SunkenStarfieldSouthwest,SunkenStarfieldSoutheast,PaleHeightsNortheast`(승강 관문은 하단 착지 근처를 도착으로 본다, 필드 상자 ±50400cm).

재실행·주의:
- 평면(안개·바닥 덮개·물웅덩이)은 충돌 프로필 `NoCollision`으로 만든다(L-editor-15). `set_collision_enabled`만 쓰면 PIE에서 다시 막는다.
- `editor_relight.py` 이계 미리보기는 저장하지 않지만, 복원 전에 저장 스크립트나 PIE를 돌리면 이계 값이 필드 대기에 남는다. 미리보기 뒤에는 대상 파일을 지우고 다시 실행해 복원한다.
- 메인 시드, `REALM_PLACEMENT`, 연결로·갱도 정의를 바꾸면 1→2→4→5→6→7→8을 다시 한다. S2(생성) 뒤 S4(베이크) 전에는 Saved 출력과 레벨이 어긋나므로 PIE 도구 결과를 쓰지 않는다.

## 검증
- 생성기 로그의 개수(나무·인스턴스·액터·조명)와 `preview.png`.
- 에디터 반영 후 `Docs/Validation/`에 캡처를 남긴다.

## 검증·시드 선택
- `validate_world.py [--dir Saved/WorldGen/AshenVale] [--report 경로] [--json 경로] [--slots 3]` — 설계서 11.2(R-91) 여섯 항목을 검사해 `report.md`(통과/실패, 위치 cm, 원인)와 `validation.json`(점수 0~100)을 쓴다. 종료 코드 0 = 전부 통과, 1 = 실패 있음.
  - 던전 입구 간격(기본 ≥ 150 m), POI 밀도(6~12개, 서로 ≥ 40 m), 도로 도달성(`layer_Road.r8` 가중치 ≥ 100을 도로로 보고 PlayerStart에서 연결된 도로가 각 POI·입구 40 m 안에 있는지; `layout.json`에 `roads` 폴리라인이 있으면 그것을 씀), 지형 적합성(반지름 8 m 표본, 중심 경사 ≤ 25°, 표본의 급경사 비율 ≤ 50 %, 수면 −1.75 m 아래 금지), 스트리밍 안전(아틀라스 슬롯 원점 (300000 + K×30000, 300000) cm, 로딩 반경 12800 cm 원이 서로·야외 지역과 겹치지 않는지), 플레이 밀도(콘텐츠에서 120 m 이상 떨어진 연속 도로 ≤ 180 m).
  - 임계값은 `--entrance-min-m --poi-min-m --road-reach-m --road-weight --max-slope-deg --steep-frac --slot-pitch-cm --load-radius-cm --content-far-m --empty-max-m`로 바꾼다. 점수 가중치: 입구 15, POI 15, 도로 25, 지형 20, 스트리밍 10, 밀도 15(도로·지형은 대상별 통과 비율로 부분 점수).
- `select_seed.py --seeds 1-5 [--top 3] [--skip-generate]` — 시드마다 `generate_ashen_vale.py --seed N --out Saved/WorldGen/Candidates/seed_N`을 실행하고 검증해 `Saved/WorldGen/candidates.md`(시드, 점수, 실패 항목, 항목별 점수, 나무·인스턴스·액터 수, 생성 시간)에 표로 쓰고 상위 N개를 추천한다. 시드당 생성 약 15초 + 검증 약 3초이므로 20개면 6분쯤 걸린다. 채택할 시드는 `generate_ashen_vale.py --seed N`(기본 폴더)으로 다시 만든 뒤 에디터 반영 스크립트를 돌린다.
- `generate_ashen_vale.py --out 경로`로 출력 폴더를 바꿀 수 있다(기본 `Saved/WorldGen/AshenVale`).

## PCG 그래프 스크립팅 실측 (UE 5.8, P3-07)
- 그래프: `AssetTools.create_asset(name, path, unreal.PCGGraph, unreal.PCGGraphFactory())`, `graph.add_node_of_type(cls)` → `(node, settings)`, `graph.add_edge(a, "Out", b, "In")`, 핀 라벨은 `node.input_pins[i].properties.label`(Surface Sampler `Surface`/`Bounding Shape`, Difference `Source`/`Differences`, Get* `BoundingShape`, Attribute Filter 출력 `InsideFilter`/`OutsideFilter`). 재실행 시 `delete_asset`이 거부되면 `remove_node` 반복으로 비우고 재사용.
- 속성 선택자(`PCGAttributePropertyInputSelector`)는 `set_attribute_name`이 무시된다 → `sel.import_text("PCGBegin(Road)PCGEnd")` 후 `settings.set_editor_property(...)`. 랜드스케이프 레이어 가중치 속성 이름 = 레이어 이름(Soil/Moss/Mud/Road/Rock/Bedrock).
- HiGen: `graph.use_hierarchical_generation`, `hi_gen_grid_size`(기본 = 가장 큰 격자), 작은 격자는 `PCGHiGenGridSizeSettings.hi_gen_grid_size` 노드로. 컴포넌트 `is_component_partitioned`, `seed`, `generate_local(True)`/`cleanup(True)`; 파티션 컴포넌트는 `generated`가 항상 False → `on_pcg_graph_generated_external.add_callable` + `register_slate_post_tick_callback`.
- 그래프 사용자 파라미터는 Python에서 만들 수 없다(`set_float_parameter`는 없는 이름을 무시). 값은 노드 프로퍼티에 두고 C++ 바인더가 주입해야 한다.
- 메시: `PCGSoftISMComponentDescriptor().set_editor_property("static_mesh", 로드된 메시)` → `PCGMeshSelectorWeightedEntry(descriptor, weight)` → `spawner_settings.mesh_selector_parameters.mesh_entries`. 점 데이터를 Surface Sampler `Bounding Shape`/Intersection에 넣어도 점 bounds로 제한되지 않는다 → Difference 두 번으로 "존 안 점"을 얻는다.
- 생성 결과 읽기: `PCGDataFunctionLibrary.get_typed_inputs(comp.get_generated_graph_output(), unreal.PCGBasePointData)`, 속성 목록 `data.const_metadata().get_attributes()`. `PCGVolume` 스케일은 먼저 10배로 놓고 `get_actor_bounds`로 단위 extent를 재서 계산.

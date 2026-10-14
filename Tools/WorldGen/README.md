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
| `editor_make_pcg_biome.py` / `editor_check_pcg_determinism.py` | P3-07. `/Game/World/PCG/PCG_TDBiome_Forest`+서브그래프 5종·`/Game/Dungeon/PCG/PCG_TDRoomDressing` 생성, `TDGen_PCG_BiomeForest` PCGVolume(파티션+HiGen) 배치·생성 / 같은 시드 2회 생성 후 ISM 수·위치 해시 비교 → `Saved/WorldGen/pcg_determinism.json`. 결과 `Docs/Validation/P3-07-pcg-biome.md`. |
| `../run_in_editor.py` | 엔진 `remote_execution.py`로 에디터 안에서 스크립트 실행. |

C++ 도구: `Source/TDGameEditor/Landscape/TDLandscapeEditorLibrary.*`(에디터 모듈), `Source/TDGame/World/Generation/TDInstancedMeshActor.*`(런타임 컨테이너).

## 좌표·규격
- 랜드스케이프 1009×1009 정점, 쿼드 1m(스케일 100), 16×16 컴포넌트(63쿼드·1섹션), 월드 파티션 그리드 2컴포넌트 → 8×8 스트리밍 프록시(각 126m). 중심 (0,0), 범위 ±504m.
- 높이 인코딩: 32768 + 미터×128 (Z 스케일 100). 물 높이 −1.75m.
- 마을 중심 (−110, 20)m, 반지름 92m. 외곽 입구 3곳(생성기 `MAIN_DUNGEON`·`SIDE_DUNGEON_NW`·`SIDE_DUNGEON_SE`): MainCrypt (296, −268), HollowCave (−372, −262), SunkenCrypt (318, 412). D-42 이후 이 세 자리는 이계로 가는 통로 입구다(아래 '이계 지역과 통로').
- 도로: 평탄화 폭 3.5~7m + 6m 완만, `Road` 레이어. 강: 바닥 −3.1m, 폭 약 9m, 주변 계곡 완화.

## 지형·도로·강·바이옴 마스크 절차 (P3-00, 2026-09-30 정본 결정)
- 정본: 높이맵·레이어는 numpy 생성기 `generate_ashen_vale.py`가 만든다(C++ `FTDHeightmapGenerator`는 만들지 않음, 사용자 결정 2026-09-30). 같은 시드면 `height.r16`·`layer_*.r8`·`layout.json`(출력 경로 문자열 제외)이 바이트 단위로 같다.
- 랜드스케이프 생성: C++ `UTDLandscapeEditorLibrary::CreateLandscapeFromRawFiles(월드, FTDLandscapeCreateRequest)`가 raw 높이맵(uint16)·레이어(uint8)를 읽어 `ALandscapeProxy::Import` → 레이어 인포 자동 생성(`/Game/World/Landscape/Layers`) → `ULandscapeSubsystem::ChangeGridSize`로 월드 파티션 스트리밍 프록시 분할(그리드 2 = 8×8 프록시). 배열 직접 입력·텍스처 임포트 함수는 두지 않는다(raw 파일 경로로 대체).
- 도로·강: 스플라인이 아니라 생성기가 높이맵에 직접 새긴다(도로 평탄화 3.5~7m + 6m 완만 경사와 `Road` 레이어, 강 바닥 −3.1m·폭 약 9m 계곡). 사후 손질이 필요하면 에디터 랜드스케이프 스플라인 도구로 고치고, 재생성 시 덮어써지므로 생성기 좌표(코드 상수)에 반영한다.
- 바이옴 마스크: 레이어 가중치 raw 6종(Soil/Moss/Mud/Road/Rock/Bedrock)을 임포트해 웨이트맵으로 쓰고, PCG 바이옴 그래프가 레이어 이름으로 샘플한다(P3-07). 렌더 타깃 경유 임포트는 쓰지 않는다.
- 결정론 확인: `python Tools/WorldGen/generate_ashen_vale.py --seed 7 --out <A>`와 `--out <B>`를 실행해 두 폴더 파일의 SHA-256을 비교한다(증거 `Docs/Validation/P3-00-landscape.md`).

## 다시 만들 때
`editor_build_open_world.py`는 `KEEP_CLASSES`(WorldSettings·WorldDataLayers·WorldPartitionMiniMap·Brush·LevelBounds)와 태그 `TDRealm`이 붙은 액터를 제외한 **로드된 모든 액터를 지우고** 다시 만든다. 랜드스케이프는 필드 상자(XY ±60000cm) 안 프록시만 지운다. 보존 기준은 라벨이 아니라 클래스와 `TDRealm` 태그다 — 라벨 접두어를 바꿔도 보존되지 않는다. 손으로 다듬은 액터를 남기는 잠금 기능은 할 일 P3-06.

## 이계 지역과 통로(P3-13·P3-15, D-41·D-42)
메인 외곽 입구 3곳에서 바깥으로 이어지는 통로 A를 걸어가면 이음매에서 같은 모양의 이계 통로 B로 평행이동한다(숨은 이음매, 암전 없음). 이계 2곳(각 504m×504m)은 같은 월드 파티션 레벨의 먼 좌표에 있다. 이음매 앞뒤 ±30m 핵심 구간은 정수 미터 오프셋으로 높이·소품이 같다. 던전 생성기·아틀라스·슬롯 안 출구(`TDGen_Exit_*`)는 남아 있고 콘솔 `TDTravelToDungeon MainCrypt`로 들어간다. 콘솔 `TDTravelToDungeon <GateId>`는 디버그용 암전 이동(도착 = 이계 이음매).

| GateId | 이계 | 필드 입구 | 통로 yaw | 필드 이음매(m) | 이계 이음매(이계 로컬 m) | 높이 |
|---|---|---|---|---|---|---|
| `SunkenStarfieldSouthwest` | 가라앉은 별밭 (−302400, 302400)cm | HollowCave | 166 | (−456, −241) | (216, 61) | 하강 |
| `SunkenStarfieldSoutheast` | 가라앉은 별밭 | MainCrypt | 315 | (365, −337) | (−216, 41) | 하강 |
| `PaleHeightsNortheast` | 창백한 고원 (−302400, −302400)cm | SunkenCrypt | 16 | (450, 450) | (−216, 24) | 상승 |

통로 정의·단면·소품은 공유 모듈 `realm_passages.py` 한 곳에 있다(난수 없음). 두 생성기가 이 모듈로 통로를 깎는다.

순서(에디터 단계는 PowerShell, 먼저 `python Tools/ue_editor.py ensure`):
1. 필드 생성(에디터 밖): 현재 출력을 `Saved/WorldGen/AshenVale_prev_<표시>/`로 복사한 뒤 `python Tools/WorldGen/generate_ashen_vale.py --seed 7`(통로 A 깎기, 입구 터널 없음, `passages`·`PcgExclusion` 마커 출력).
   탑다운 배치 규칙(P3-17, `topdown_rules.py`, 난수 소비 불변): 걷는 칸의 카메라 시선 가림 제거·축소(R1), 바위·절벽 기울기 0·카메라 쪽 틈 묻기·가장자리 걸침 제거(R2), 이계 기둥 높이 상한·빛기둥 `collision: none`(R3), 통로 카메라 쪽 벽 낮춤(R4, `realm_passages.py`), 통로 입구 상인방·덮개 제거(R5), 고원 구름 확장(R6). 출력 폴더에 `topdown_rules_report.json`. 끄기 `--no-topdown`, 수치 `python Tools/WorldGen/topdown_rules.py --measure --label <이름>`.
2. 이계 생성(에디터 밖, 1 뒤): `python Tools/WorldGen/generate_realm.py --realm all` — 필드 새 `height.r16`과 통로 형상 일치(≤1/128m)를 검사한다. 1을 다시 하면 2도 다시 한다.
3. 재질(처음 한 번): `editor_make_landscape_material.py` — `LayerTint_<Layer>`·`RealmTint`·`RealmBrightness`·`RealmDesaturation`(기본값이면 메인 색 불변), 부모가 `M_TD_Landscape`인 인스턴스 재부모화, 프록시별 원래 재질 복원.
4. 메인 부분 재베이크: `Saved/WorldGen/open_world_build_request.json`에 `{"mode": "partial", "previous_layout": "<1의 복사본>/layout.json"}`를 쓰고 `editor_build_open_world.py` 실행 → 실행 뒤 요청 파일을 지운다(남아 있으면 계속 부분 모드). 지형·layout 라벨 액터·`TDGen_ISM_`만 교체하고 던전·PCG·상자·도구·출구·`TDRealm`·대기 5종·PlayerStart·RecastNavMesh는 보존한다(삭제·보존 개수 로그). 요청 파일이 없으면 예전 전체 모드(레벨 전체 정리)다.
5. `editor_make_definitions.py`(아틀라스 슬롯 귀환점을 입구 마커 앞 7m로).
6. 이계 베이크: `editor_build_realm.py`(옛 포탈 `TDRealm_Gate_*`·`TDRealm_<Realm>_Exit_*` 일반 접두어로 삭제, 통로 소품 필드 사본 `TDRealm_Passage_<GateId>_*` 생성, `DA_TDRealm_*`에 통로 길이·안개 값 기록).
7. 내비 동적 설정 `editor_setup_navmesh_dynamic.py` → (탑다운 재질, P3-17) `Saved/WorldGen/topdown_apply_request.json`에 `{"patch_pcg": true}` → `editor_apply_topdown_materials.py`(PCG 그래프 메시 항목을 거울 MI로) → PCG 재생성: `Saved/WorldGen/pcg_filter.txt`에 BOM 없이 `TDGen_PCG_BiomeForest` → `Tools/editor_regenerate_pcg.py` → 필터 파일 삭제. `editor_make_pcg_biome.py`는 다시 돌리지 않는다.
8. 탑다운 재질 적용: 요청 파일을 지우고 `editor_apply_topdown_materials.py`(배치물 슬롯 → 거울 MI, 이계 MI 부모 → 거울; 베이크가 팩 재질로 되돌리므로 베이크마다 다시) → 저장. 재질 체계 자체를 바꿀 때만 먼저 `editor_make_topdown_materials.py`. 설계 `Docs/WorldDungeonPCG/05-topdown-visuals.md`.
9. 검증: 헤드리스 `TDGame.SeamlessTravel.RealmSeamWalk`(관문 3곳 왕복 연속성)·`TDGame.TopDown`, 실제 이동 입력 걷기 `python Tools/WorldGen/pie_walk_passages.py --run <이름>`, 탑다운 지점 촬영·전후 비교 `python Tools/WorldGen/pie_capture_spots.py --run <이름> [--compare <이전 run>]`(에디터 창을 앞에 둔다, L-editor-06).

런타임(C++ `UTDRealmSubsystem`): 통로 구간(필드 측 입구−선로딩 여유 ~ 이음매+버퍼, 이계 측 이음매−버퍼 ~ 통로 끝+여유)에서 반대편 선로딩, 이음매 ±50cm를 넘고 준비되면 평행이동(속도·회전 유지, 카메라 컷, 스프링암 보정, 클릭 이동 목적지 이동), 진행도 w로 분위기·태양 회전(1/32 단계)·이계 후처리 가중치 혼합, 이음매 부근 전역 안개 강화(`SeamMistDensity`·`SeamMistRadiusCm`). 붙잡기 `RealmDepartureHoldSeconds`(10초)는 떠난 쪽 이음매를 붙잡는다.

라벨·폴더·태그 규약:

| 대상 | 라벨 | 아웃라이너 폴더 | 태그 |
|---|---|---|---|
| 이계 생성물(지형·ISM·액터·평면·조명·FX·마커·내비 경계·후처리·통로 B 소품) | `TDRealm_<RealmId>_*`(통로 소품 `TDRealm_<RealmId>_Passage_<GateId>_*`) | `TDRealm/<RealmId>/<종류>` | `TDGen`, `TDRealm`, `TDRealm:<RealmId>` |
| 필드 통로 A 소품 | `TDRealm_Passage_<GateId>_*` | `TDRealm/FieldPassages` | `TDGen`, `TDRealm`, `TDRealmPassage` |
| 필드 통로 PCG 배제 상자 | `TDGen_SeamPcgExclusion_<GateId>`(TriggerBox, 충돌 없음) | `TDGen/Markers` | `TDExclusion` |
| 에셋 | `DA_TDRealm_<RealmId>`, `MI_TD_Landscape_<RealmId>`, `/Game/World/Realms/<RealmId>/Materials/MI_TDRealm_<RealmId>_*` | — | — |

재실행·주의:
- 평면(안개·바닥 덮개·물웅덩이)은 충돌 프로필 `NoCollision`으로 만든다(L-editor-15). `set_collision_enabled`만 쓰면 PIE에서 다시 막는다.
- `editor_relight.py` 이계 미리보기는 저장하지 않지만, 복원 전에 저장 스크립트나 PIE를 돌리면 이계 값이 필드 대기에 남는다. 미리보기 뒤에는 대상 파일을 지우고 다시 실행해 복원한다.
- 메인 시드나 통로 정의를 바꾸면 1→2→4→6→7→8을 다시 한다.

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

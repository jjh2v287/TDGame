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
- 마을 중심 (−110, 20)m, 반지름 92m. 메인 던전 (352, −318), 사이드 던전 NW (−372, −262), SE (318, 412).
- 도로: 평탄화 폭 3.5~7m + 6m 완만, `Road` 레이어. 강: 바닥 −3.1m, 폭 약 9m, 주변 계곡 완화.

## 다시 만들 때
`editor_build_open_world.py`는 `WorldSettings/WorldDataLayers/WorldPartitionMiniMap`을 제외한 **모든 액터를 지우고** 다시 만든다. 손으로 다듬은 액터를 보존하려면 라벨 접두어를 `TDGen_`이 아닌 것으로 바꾸고 `KEEP_CLASSES`/보존 규칙을 추가할 것(할 일 P3-06 잠금 기능).

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

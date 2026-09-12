[← 인덱스로](../../WorldDungeonPCG_Plan.md)

# 테스트: MCP로 랜드스케이프 생성·편집이 되는가 (2026-09-11, UE 5.8, 실측)

## 결론
- **생성: 가능(우회 경로).** 공식 MCP 툴셋에 랜드스케이프 전용 도구는 없지만, `SlateInspectorToolset`(에디터 UI 자동화)로 모드 콤보박스를 "랜드스케이프"로 바꾸고 "새 랜드스케이프 > 생성" 버튼을 눌러 실제로 생성됐다. 결과: `Landscape` 액터 1개, 컴포넌트 256개(16×16, 해상도 1009×1009, 스케일 100 → 가로세로 약 1,008m).
- **편집: 가능(Python 원격 실행).** `landscape_export_heightmap_to_render_target` / `landscape_import_heightmap_from_render_target`로 높이맵을 렌더타깃 경유로 읽고 쓸 수 있고, `editor_apply_spline`으로 스플라인 경로를 따라 지형을 깎거나 올릴 수 있다. 렌더타깃에 R=0x90(RG 인코딩)을 채워 임포트하자 전체 높이가 3200cm로 올라갔고(계산: (0x9000−0x8000)/128×100), 스플라인 적용 후 바운드 Z 범위가 ±100cm로 바뀌어 편집이 반영됐다.
- **한계:** 에디터 Python·블루프린트에는 랜드스케이프 **생성 API가 없다**(`ALandscape::Import`는 C++ 전용, `LandscapeEditorSubsystem`·`LandscapeImportHelper` 미노출). UI 자동화는 한국어 라벨과 위젯 배치에 의존해 깨지기 쉽다. 컴포넌트 수 같은 스핀박스는 접근성 트리에 잡히지 않아 기본값(16×16)만 눌렀다.

## 확인한 API (에디터 Python, `unreal.Landscape`/`LandscapeProxy`)
- 편집: `landscape_import_heightmap_from_render_target(rt, import_from_rg=True, edit_layer_index=0)`, `landscape_export_heightmap_to_render_target(rt, export_to_rg=True, export_proxies=True)`, `landscape_import_weightmap_from_render_target(rt, layer_name, edit_layer_index)`, `editor_apply_spline(spline, start_width, end_width, start_falloff, end_falloff, start_roll, end_roll, subdivisions, raise, lower, paint_layer, edit_layer_name)`, `set_landscape_material_*_parameter_value`, `get_edit_layers_bp()`, `get_target_layer_names()`, `render_heightmap(...)`, `force_layers_full_update()`.
- 없음: 생성, 컴포넌트 추가/삭제, 리사이즈, 편집 레이어 추가(블루프린트 노출 아님), 랜드스케이프 스플라인 제어점 생성.
- 엔진 C++(`Runtime/Landscape/Classes/LandscapeProxy.h:1418`): `ALandscape::Import(Guid, MinX, MinY, MaxX, MaxY, NumSubsections, SubsectionSizeQuads, HeightData, HeightmapFileName, LayerInfos, AlphamapType, ImportLayers)` — `TDGameEditor` 모듈에서 감싸면 Python/MCP에서 결정론적으로 호출 가능.

## 재현 절차 (MCP + Python)
1. Python 원격 실행 켜기: ConfigSettingsToolset `SetSectionProperties Project/Plugins/Python {"bRemoteExecution": true}`.
2. Python: `LevelEditorSubsystem.new_level("/Game/Tests/L_TDLandscapeMcpTest")`.
3. SlateInspector: `Observe("")` → `Snapshot("", 14)`에서 모드 콤보박스(`"선택 모드"` 텍스트가 있는 combobox, 여기서는 `co1`) → `SelectOption(co1, "랜드스케이프")` → `Snapshot(뷰포트 스플리터)`에서 "새 랜드스케이프" 패널의 `"월드 채우기 / 데이터에 맞추기 / 생성 / 임포트"` 행 → 그 안의 `"생성"` 위젯 `Click`.
4. Python: `get_all_actors_of_class(world, unreal.Landscape)`로 확인, 렌더타깃/스플라인으로 편집, `save_current_level()`.
5. 정리: 모드 되돌리기, `load_level("/Game/Level/LV-World")`, 원격 실행 끄기, `git checkout -- Config/DefaultEngine.ini Config/DefaultEditor.ini`.

## 증거
- `Docs/Validation/landscape-mcp-test-editor.png`(에디터 전체), `landscape-mcp-test-viewport.png`(뷰포트 확대). 아웃라이너에 `Landscape`, `TDTest_RidgeSpline`.
- 테스트 레벨 `Content/Tests/L_TDLandscapeMcpTest.umap`(새 에셋, 저장됨). 필요 없으면 삭제해도 된다.

## 이 프로젝트에 권장하는 "월드 기본 바탕" 제작 경로
1. 랜드스케이프 **생성은 C++ 에디터 함수**(`UTDLandscapeEditorLibrary::CreateLandscape(World, Transform, ComponentsX, ComponentsY, QuadsPerSection, SectionsPerComponent, HeightmapPath|Flat)`)로 감싸 결정론·재현성을 확보한다. UI 자동화는 임시 검증용.
2. 지형 형태는 **높이맵 텍스처(16비트 PNG/R16)**를 생성기(`TDWorldGen`)가 만들고 렌더타깃 경유로 임포트한다. 지역별 고도·절벽·수면은 생성기 데이터에서 온다(설계서 4.1 "지역별 고도 방향은 수작업"이므로 수작업 앵커 + 노이즈 혼합).
3. 도로·강은 `editor_apply_spline`(또는 랜드스케이프 스플라인)로 깎고, 바이옴 마스크는 웨이트맵 렌더타깃 임포트로 쓴다 → PCG가 레이어 가중치를 읽는다.
4. 월드 파티션 큰 월드는 랜드스케이프를 `LandscapeStreamingProxy`로 분할해야 하므로, 생성 함수에서 월드 파티션 그리드 크기에 맞춘 프록시 분할 옵션을 둔다(Phase 3 P3-00으로 추가).

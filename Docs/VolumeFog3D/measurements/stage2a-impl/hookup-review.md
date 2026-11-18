# 2a 게임 연결 코드 독립 검토 (읽기 전용, 2026-11-15)

판정: **불합격** — 막아야 함 1, 고치면 좋음 3, 화면 확인에서 볼 것 3.
읽은 것: 계약 `Docs/VolumeFog3D/stage2/impl-2a-contract.md` 전문, 넘김 글 step1·5·6(+3a·3b 의 땅 자원 수명 줄), `TDFog3DGround.h/.cpp`, `TDFog3DZoneActor.h/.cpp`(땅 관련 전부·Tick·EndPlay·RefreshSettings·ConfigureLayer·재질), `TDFog3DLayer.cpp`(창 계산·PackStep·SubmitStep·Release), `TDFluidTerrainCapture.cpp`, 2D 드라이버 diff, `TDFog3DTypes` 땅 필드·검사, JSON `Globals.TDFog3DSettings` 땅 8줄, `TDFog3DGroundTests.cpp`, `probe-p1.json`, 엔진 소스(아래 근거). `TDFluidGpuFog3D.cpp`·셰이더는 지시대로 읽지 않았고, 변환 식은 step5 넘김 글(`H = Depth < 문턱 ? 해독 − Depth : 못 찾음 값`)을 그대로 믿었다.

---

## 막아야 함

### B1. 땅 못 찾음 판정 문턱(5e8)이 실제 캡처 출력에서 한 번도 걸리지 않는다 → 구멍·땅 밖 기둥이 약 −2.6km 높이가 된다

- 자리: `Content/FluidFX/PresetData/TDFluidPresetTuning.json:2422`(`GroundMissingDepthThresholdCm: 5e8`), `TDFog3DTypes.h:189`, `TDFog3DZoneActor.cpp:55·907`(지우기 1e9 를 "땅 없음"으로 전제), `TDFog3DZoneActor.cpp:876~893`(근·원 평면을 정하지 않음).
- 실패 줄거리: 캡처 사각형(N=128 이면 40m, N=256 이면 60m) 안에 랜드스케이프·`TDFluidTerrain` 태그 액터가 없는 텍셀(무대 바닥 판 밖, 구멍, 랜드스케이프 끝)이 있다 → 깊이 캡처가 그 텍셀에 지우기 값 1e9 가 아니라 **원 평면 거리 262144cm** 를 쓴다 → 262144 < 5e8 이라 "못 찾음"이 아니다 → H = 해독(약 800) − 262144 ≈ **−261,000cm** 가 정본·재질 사본에 들어간다 → (가) 그 기둥의 안개 칸은 2.6km 아래에 놓여 안 보이고, (나) 4.2 벽 규칙(H − H_n > 30)으로 구멍에 닿은 모든 땅 기둥이 벽(통째 막힘)이 되며, (다) 재질 쌍선형이 이웃 반 칸까지 끌어내리고, (라) ⓪2 의 "사각형 밖은 가장자리 값으로 늘어짐"에서 가장자리 텍셀이 구멍이면 사각형 밖 안개가 전부 사라진다. 계약 ⓪1-3·§5.8("못 찾은 기둥 = 구역 바닥 값, 1단계처럼 바닥 높이 안개")과 정반대다. 태그 액터가 하나도 없는 레벨에서 땅 따라가기를 켜면 안개 전체가 사라진다.
- 근거(엔진 5.8):
  - 직교 캡처의 근·원 평면: `Runtime/Renderer/Private/SceneCaptureRendering.cpp:1213~1215` — 근 0, 원 = `MaxViewDistanceOverride > 0 ? 그 값 : UE_FLOAT_HUGE_DISTANCE / 4`. `UE_FLOAT_HUGE_DISTANCE = 1048576.0`(`Runtime/Engine/Public/EngineDefines.h:59`) → 원 평면 262144cm. `bUpdateOrthoPlanes` 기본 false(`Engine/Private/Components/SceneCaptureComponent.cpp:644`)라 `UpdateOrthoPlanes`(SceneCaptureRendering.cpp:887~889)는 안 돈다. `bAutoCalculateOrthoPlanes` 는 `GetCameraView`(SceneCaptureComponent.cpp:802)에서만 쓰이고 캡처 렌더러의 투영에는 들어가지 않는다.
  - 직교 역깊이 → 장면 깊이: `Engine/Private/SceneView.cpp:531~532·566~571` — SceneDepth = DeviceZ / M22 − M32 / M22. `BuildOrthoMatrix`(SceneCaptureRendering.cpp:546~565): ZScale = 1/(원−근), ZOffset = −근 → 배경(역깊이 DeviceZ = 0) 장면 깊이 = 원 평면 = 262144.
  - 캡처 결과는 텍셀 전부를 덮어쓴다: 복사 패스가 불투명 혼합(`SceneCaptureRendering.cpp:300~302`)·`ENoAction`(318줄)으로 화면 전체를 그리고, 그 전에 검정으로 지운다(472~474줄). 깊이 모드 셰이더는 `CalcSceneDepth` 를 그대로 쓴다(`Engine/Shaders/Private/SceneCapturePixelShader.usf:48~49`). 즉 렌더 타깃의 `ClearColor`(1e9)는 캡처가 실제로 그려진 뒤에는 남지 않는다.
  - 방증: P1 탐침이 잰 깊이 양자화 1/64cm(`step1-handoff.md` 4절)는 원 평면 262144 에서 역깊이 float32(1 근처 ulp 5.96e-8)가 만드는 해상도 5.96e-8 × 262144 = 0.0156cm 와 정확히 같다.
  - 시험이 놓친 까닭: P1 은 판 하나가 캡처 전체를 덮어 `missingTexelCount 0`, `rawDepthMax 779.98`(`probe-p1.json`) — 배경 텍셀이 없었다. RHI `GroundConvertCapture` 는 손으로 만든 1e9 텍스처를 썼다(step5 6절). 조사 메모 `research-height-source.md:23` 의 "땅 없는 텍셀은 1e9" 는 측정이 아니라 지우기 값에서 나온 전제다.
- 확인 못 함: 주 렌더러 안 캡처(`bRenderInMainRenderer`, PIE 기본 경로)의 깊이 출력도 같은 투영으로 같은 값을 쓰는지는 코드로 끝까지 따라가지 않았다(같은 `BuildOrthoMatrix` 투영을 `FCustomRenderPassRendererInput.ProjectionMatrix` 로 넘기는 것은 확인, SceneCaptureRendering.cpp:1312~1317).
- 고치는 방향: 캡처에 `MaxViewDistanceOverride` 를 설정값(예: 해독 오프셋 + 구역 아래 여유)으로 주고 문턱을 그 원 평면보다 조금 작게 두거나(설정 검사로 문턱 < 원 평면 강제), 문턱 대신 "깊이 ≥ 원 평면 − ε" 로 판정한다. 그리고 태그 판이 사각형 일부만 덮는 RHI 캡처 시험(실제 캡처 → 변환 → 못 찾은 텍셀 = `GroundMissingHeightCm`)을 더한다.

---

## 고치면 좋음

### N1. 지연을 "프레임 번호"로만 세서, 그 사이 장면이 한 번도 그려지지 않으면 지난 내용(또는 지우기 값)을 새 사각형으로 변환한다 — 그리고 창이 움직이기 전까지 다시 캡처하지 않는다

- 자리: `TDFog3DGround.cpp:214~218`(`IsRequestReady` = `GFrameCounter ≥ F + 지연`), `TDFog3DZoneActor.cpp:975·1003`.
- 줄거리: 게임 뷰포트가 있으면 `bRenderInMainRenderer = true`(`TDFluidTerrainCapture.cpp:25`) → `CaptureScene()` 은 사용자 렌더 패스를 장면에 걸어 둘 뿐이고 "다음에 그 장면을 그리는 렌더러"가 실제로 그린다(엔진 주석 `SceneCaptureRendering.cpp:1403~1404`). 그 프레임들에 주 장면이 안 그려지면(창 최소화로 뷰포트 크기 0 — `Engine/Private/UnrealClient.cpp:1820`, `FViewport::SetGameRenderingEnabled(false)` 로딩 화면 — 같은 파일 1802줄, 렌더 일시 중단 — `GameEngine.cpp:2027~2035`) `GFrameCounter` 만 늘어 F+지연 에 변환이 돈다 → 순간 이동·첫 진입이면 지우기 값 1e9 → 전부 못 찾음(평지) 또는 직전 사각형의 내용을 새 최소 칸으로 해독(창 이동 폭만큼 어긋난 땅) → `ConfirmStep` 으로 "변환됨"이 되어 플레이어가 창 안에 머무는 동안 고쳐지지 않는다.
- 고치는 방향: 지연을 "이 월드가 실제로 그려진 프레임 수"로 센다(예: `UWorld::ViewLocationsRenderedLastFrame` 가 비지 않은 프레임만 셈 — `UnrealClient.cpp:1780` 에서 그린 프레임에만 채워짐). 화면 확인 항목에도 넣는다.

### N2. 크기가 안 바뀌는 땅 설정 변경이 추적기·정본을 무효로 만들지 않는다

- 자리: `TDFog3DZoneActor.cpp:431·450·897~906`(크기가 같으면 `EnsureGroundTargets` 가 일찍 돌아가 `GroundTracker.Reset()` 안 함).
- 줄거리: 설정 표 다시 읽기로 `GroundCaptureHeightAboveZoneCm`·`GroundMissingHeightCm`·`GroundMissingDepthThresholdCm` 를 바꾸면 이미 변환된 정본은 옛 값으로 남고, 창이 16칸 움직일 때까지 재캡처가 없다. `GroundWallStepCm` 은 걸음마다 넘어가지만 벽은 재굽기(`bRebakes`) 때만 다시 계산되므로 역시 창이 움직일 때까지 옛 문턱의 벽이 남는다. 튜닝 중 "값을 바꿨는데 안 바뀐다"는 오판을 만든다(실행 중 게임 동작에는 영향 없음).
- 고치는 방향: `RefreshSettings` 가 설정을 새로 읽은 틱에 땅 켬이면 `GroundTracker.Reset()`(재캡처·변환 걸음 = 재굽기)을 부른다.

### N3. `bFollowsGround = true` 인데 `GroundFollowVolumeMaterial` 이 비어 있으면 계산은 땅 기준, 그리기는 평평한 재질로 조용히 어긋난다

- 자리: `TDFog3DZoneActor.cpp:353~358`(`ChooseVolumeMaterial` 대체), `TDFog3DTypes.cpp:221~228`(검사 없음), JSON `:2423`(`""`).
- 줄거리: 지금 JSON 의 재질 경로가 빈 문자열이라(에셋 미생성) 2a-3 에서 `bFollowsGround` 만 켜면 → 땅 자원·캡처·변환·땅 기준 걸음은 돌고 → 재질은 액터의 flat 재질(높이 = 구역 바닥 기준)이라 비탈·절벽에서 안개 칸이 땅 기준으로 계산된 것과 다른 자리에 그려진다. 경고·검사 문제가 없다. 계약 §5.6 이 대체를 허용하지만, 켬 + 빈 경로는 의도한 조합이 아니다.
- 고치는 방향: 설정 검사에서 `bFollowsGround && GroundFollowVolumeMaterial.IsNull()` 을 문제로 올리거나 최소한 한 번 경고한다.

---

## 화면 확인에서 볼 것

- S1. 캡처 뒤에 땅이 스트리밍되어 들어오면(월드 파티션 셀, 순간 이동 직후) 보이기 목록·깊이 모두 요청 시점 것이라(`TDFluidTerrainCapture.cpp:22~47`, 요청은 창 가운데가 바뀔 때만 `TDFog3DGround.cpp:174`) 플레이어가 창 안에 머무는 동안 그 땅이 B1/§5.8 의 못 찾음으로 남는다. 랜드스케이프 확인(⓪4)에서 순간 이동 직후를 같이 본다.
- S2. 요청이 하나뿐이라 창 가운데가 지연 프레임보다 자주 바뀌면(`TDFog3DGround.cpp:174~181` 이 진행 중 요청을 버림) 변환이 계속 밀린다. 기본값(지연 2, 창 16칸 = 2.5m)에서는 약 75m/s(60fps)·19m/s(15fps) 이상 이동에서만 생기고, 그때는 옛 그림이 덮는 동안(테두리 64 → 약 48칸) 걸음이 계속되다가 그 뒤 멈춘다. 저프레임·빠른 이동에서 안개 정지 여부를 본다.
- S3. GPU 가 변환 걸음을 거부하면 `ConfirmGroundStep`(`TDFog3DZoneActor.cpp:1035`)이 이미 재질 `Fog3DGround` 를 새 사각형으로 바꿨는데 사본 복사는 안 됐을 수 있어, 거부를 알아챌 때까지(1~2프레임) 재질이 옛 내용을 새 오프셋으로 읽는다. 일시적이며 재캡처로 복구된다. 거부 경고 로그가 되풀이되는지만 본다.

---

## 문제없다고 확인한 것

- 수명·스레드: `FTextureRenderTargetResource*` 는 걸음 렌더 명령(`TDFog3DLayer.cpp:146~154`) 안에서 RDG 를 만들고 바로 `Execute` 하므로 그 명령 밖으로 나가지 않는다. 타깃을 놓을 때는 `UPROPERTY` 를 null 로 할 뿐이고, 실제 해제는 GC 의 `UTexture::BeginDestroy` → 렌더 명령 큐 뒤쪽(FIFO)이라 앞서 넣은 걸음 명령이 먼저 끝난다. 캡처 컴포넌트·두 타깃 모두 `UPROPERTY(Transient)`(`TDFog3DZoneActor.h:175~182`), 생성 `NewObject(this, RF_Transient)`. `EndPlay` 가 `ReleaseGroundResources` → `Layer.Release` 순(191~216줄)으로 놓는다. 재질 사본은 MID 도 참조한다.
- 땅 끔 = 1단계: 끔이면 `UpdateGroundResources` 가 자원을 만들지 않고(864~874줄) `TrackGround` 는 보류 없이 1단계 결정(964~969줄), `MakeGroundStep` 은 기본값(끔) → `PackGroundStep` 이 `bFollowsGround=false`. 켰다 끄면 GPU 가 끔 걸음에서 땅 텍스처를 놓고 재굽기한다(step3b 넘김 글 32·7줄의 `bRebakes ||= bGroundTexturesChanged`), 재질은 기본 텍스처로 되돌림(917~940줄). 남는 것은 MID 가 바뀌기 전까지의 `Fog3DGround`=(0,0,0,0) 뿐이고 셰이더가 `max(S,1)` 로 막는다(step6).
- 상태 기계: 같은 틱에 새 요청과 변환이 겹치지 않는다(지연 ≥ 1 강제, `IsRequestReady`). 변환은 항상 가장 최근 요청만 한다. 준비됨인데 시계가 걸음을 안 내거나 `SubmitStep` 이 실패하면 확정하지 않아 다음 걸음에서 변환한다(785~817줄). 보류 때 `ConsumeStepClock` 이 불리지 않는다(`CommitStep` 안에서만). 덮음 판정의 창(`PreviewNextWindowMinCell`)과 걸음의 창(`PackStep`)은 같은 틱·같은 층 상태의 같은 함수라 같다. 품질 128↔256 전환은 `ConfigureLayer` → 타깃 크기 불일치 → 새 타깃 + `Reset`. 구역 이탈·숨김·비활성 틱은 `Reset`(245줄). GPU 거부는 `RestartGroundAfterRejection` 으로 재캡처. 영영 보류되는 경로는 찾지 못했다(원하는 사각형은 테두리 ≥ 17 이라 항상 자기 창 + 고리를 덮음).
- 좌표: 가운데 칸 = 창 최소칸 + N/2, 최소칸 = 가운데 − S/2(N·S 양수라 음수 좌표에서도 정수 나눗셈 문제 없음), 창 칸은 `FloorToInt32` 와 음수 내림 나눗셈(`TDFog3DLayer.cpp:25~34·316`, L-build-25 대응). 캡처 자리 = 가운데 칸 × h(사각형 가운데), `OrthoWidth` = S × h, 해독 오프셋·재질 `Fog3DGround`·`Fog3DWindow` 는 double 로 뺀 뒤 float. 기준 Z = 구역 바닥 Z 가 캡처 해독(캡처 Z − 바닥 Z)·`PackSpheres`·재질 `Window.z` 에서 같은 뜻. 계약 §5.3·5.6 과 같다.
- 2D 드라이버: 목록·폰 숨김·주 렌더러 판정이 옮기기 전과 같은 순서·조건(`TerrainOnly` 가드가 호출자 607줄에 그대로 있어 하드코딩된 `TerrainOnly` 와 같은 결과).
- 규칙: 새 동기 로드 없음(재질 `RequestAsyncLoad`, 타깃 `NewObject`), 새 파일에 주석 없음, 값은 `FTDFog3DSettings`·JSON(코드 상수는 지우기 값·회전처럼 계약이 정한 것뿐).

## 시험의 빈틈 (가장 값진 셋)

1. 실제 캡처에 땅이 없는 텍셀이 섞인 RHI 시험(태그 판이 사각형 일부만 덮음 → 변환 → 그 텍셀 H = `GroundMissingHeightCm`). 있었으면 B1 을 잡았다.
2. 구역 액터 수준의 품질 전환(128 → 256)과 GPU 거부 뒤 재시작: 새 타깃 크기·`Reset`·다음 걸음 보류·지연 뒤 변환 걸음까지(지금 `GroundTrackerStates` 는 순수 추적기만).
3. `SimulateStep` 통합: 보류 틱에 걸음이 안 나가고 시계 몫이 남는지, 변환 걸음 뒤에만 `Fog3DGround` 가 바뀌는지.

## 확인 못 한 것

- 주 렌더러 안 캡처(PIE 기본)의 배경 깊이 값이 독립 렌더러와 같은지(B1 의 같은 결론일 것으로 보지만 출력 패스를 끝까지 보지 않음).
- GPU 쪽 `IsFog3DFrameInputValid`·리셋 때 `bHasGround` 유지 여부는 금지 파일이라 넘김 글(step3a 16줄 "리셋으로는 지우지 않음")만 믿었다.
- 창 최소화 때 Windows 에서 뷰포트 크기가 실제로 0 이 되는지(N1 의 한 갈래), PIE 종료 순간 대기 중인 사용자 렌더 패스가 놓인 캡처 타깃을 읽는지.

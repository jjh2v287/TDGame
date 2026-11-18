# 2a 구현 계약 — 땅 높이 따라가기 (감독 확정본 v1)

확정: 2026-11-15 claude(감독·결정). 초안은 Opus 하위 에이전트가 코드·에셋·장부를 고치지 않고 읽기만 해서 썼고(`Saved/AgentOps/20261115/fog3d/stage2a/impl-2a-contract-draft.md`), 감독이 ⓪절의 결정을 더해 확정했다. 빌더(Opus·Sonnet)가 설계 판단 없이 구현하도록 쓴다. ⓪절이 아래 절과 다르면 ⓪절이 우선이다.
근거: 설계 `Docs/VolumeFog3D/stage2/prereg-2a-design.md` 1절(식 1~9)·2절·7절, 덧붙임 1 D절·H절(R4), 덧붙임 2 D절(R5, **판정 전**), 기준 모의 `Tools/FluidFX/fog3d_ref/fog3d_terrain.py`·`fog3d_terrain_add1.py`·`fog3d_terrain_add2.py`, 조사 메모 `Saved/AgentOps/20261115/fog3d/stage2a/research-height-source.md`.
표기: [확인: 파일:줄] 열어 본 것, [추정] 추론, [확인 못 함] 근거 없음. 경로는 저장소 루트 기준. 용어: 기둥 = 가로 칸 하나의 8층, H = 기둥 땅 높이, N = 창 칸 수(Resolution), h = 15.625cm, L = 층 30cm, T = 벽 문턱 30cm, 고리 = 창 밖 한 칸 둘레.
**본체는 R4 다. R5 는 [판정 뒤 확정] 표시를 단 덧층(§4.6, §11 단계 7)이며 R5 가 떨어지면 그 부분은 짜지 않는다.**

## ⓪ 감독 확정 (2026-11-15)

1. 초안의 이의 1(재질은 계산 핵심의 그림을 직접 못 읽으므로 구역 액터 소유 렌더 타깃으로 한 번 복사), 2(기준 Z = 구역 바닥 Z, 구 높이 식은 그대로), 3(땅을 못 찾은 기둥 = 구역 바닥 값, 막지 않음), 4(창이 테두리보다 많이 움직이면 걸음 보류), 6(기준 덤프의 H 는 float32)을 채택한다. 3 은 2a-3 화면 확인 항목에 넣는다.
2. 이의 5(구역이 캡처 사각형보다 넓을 때): 캡처는 창을 따라 움직이는 사각형 하나로 두고 테두리 기본값을 64칸(10m)으로 한다(S = N + 128). 사각형 밖의 평소 안개가 가장자리 높이로 늘어지는 것은 알려진 한계로 2a-3 화면 확인 항목에 넣는다. 구역 전체 캡처는 구역 크기에 따라 메모리가 제곱으로 늘어 고르지 않는다. 먼 곳용 거친 높이 그림은 화면 확인에서 필요하다고 판정되면 2d 나 3단계에서 다룬다.
3. 이의 7(그리는 식 파일): 기존 `TDFluidGpuFog3DDensity.ush` 는 한 글자도 바꾸지 않는다. 새 함수는 새 파일 `Source/TDFluidGpu/Shaders/Private/TDFluidGpuFog3DDensityGround.ush`(기존 파일을 include)에 두고, 땅 변형 재질은 include 방식만 쓴다. inline 방식이 필요해지면 멈추고 감독에게 알린다.
4. 탐침 P3(랜드스케이프 상세 단계)은 2a-2 의 구현 단계에서는 하지 않는다(3D 안개 무대에 랜드스케이프가 없다). 2a-3 무대는 `TDFluidTerrain` 태그를 단 정적 메시로 만든다. **랜드스케이프 확인은 2a-3 직후(늦어도 2d 의 2단계 닫음 판정 전)에 한다**(감독 세션(claude)의 결정 2026-11-15, 사용자가 뒤집을 수 있음): ① P3 — 정본 높이 대 `GetHeightAtLocation`(탐침 코드에서만 호출) ② 창을 여러 번 옮기며 같은 세계 기둥의 높이와 벽 표시가 재캡처마다 같은지 ③ 걷기 촬영 한 번. 랜드스케이프가 있는 기존 레벨에서 PIE 로 구역을 실행 중에 띄워서 하고 레벨과 에셋은 저장하지 않는다. 실행 중에 띄울 수 없으면 까닭을 적고 대안을 정한다. 까닭: 사용자의 목표가 오픈월드 지형이고, 직교 캡처가 고르는 상세 단계에 따라 높이가 달라지면 30cm 벽 문턱 근처 기둥이 재캡처마다 벽·비벽으로 바뀌어 안개가 튈 수 있는데, 태그를 단 정적 메시 무대로는 이것을 볼 수 없다.
5. 탐침 P1 을 시험 코드의 임시 월드에서 할 수 없으면, 2a-3 무대(새 맵)를 앞당겨 만들어 거기서 PIE 로 한다. 기존 무대 `LV_TDFog3DStage` 는 고치지 않는다.
6. R5 덧층(§4.6, 단계 7)은 덧붙임 2 의 판정 뒤 이 절에 한 줄로 확정한다(고름 → 넣는다 / 못 고름 → 짜지 않는다). **확정(2026-11-15 20:07): 덧붙임 2 가 R5 를 골랐다(판정선 열넷 통과, 독립 반박 합격) → R5 덧층을 넣는다(단계 7). 기준 덤프는 `fog3d_replay_s2a.py --candidate R5` 로 다시 뽑아 2A-I1·I3 을 다시 잰다.**
7. 새 에셋(재질 `/Game/FluidFX/Materials/M_TD_Fog3DVolumeGround`, 2a-3 무대 맵)은 만들기 직전에 감독이 경로와 내용을 알린다. 빌더는 감독의 지시 없이 에디터에서 에셋을 만들거나 저장하지 않는다. 기존 에셋은 덮어쓰지 않는다.
8. 빌드·시험은 `python Tools/build_and_test.py`(다른 세션의 빌드·시험을 기다리고 `Saved/AgentOps/build_hold.json` 을 지킨다, L-build-05·08)로 한다. GPU 시험은 `--rhi offscreen`. 아래 ⑪절의 `ue_editor.py restart` 는 에디터 GUI 가 꼭 필요한 단계(재질 에셋, PIE 탐침)에서만 쓰고 감독에게 먼저 알린다. 남의 프로세스는 끝내지 않는다.
9. 차례와 모델: 단계 1(공유 목록 + 탐침 P1·P4) Opus / 단계 2(기준 도구·재생 장면) Opus(재생 도구)·Sonnet(대조 도구), 단계 1 과 병렬 / 단계 3(GPU 핵심) Opus / 단계 4(GPU 대조) Sonnet(시험 연결) + Opus(고침) / 단계 5(게임 연결) Opus / 단계 6(재질) Sonnet(도구·검사기) + Opus(식) / 단계 7(R5 덧층·PIE 탐침·비용) Opus + Sonnet. 단계마다 넘김 글을 `Saved/AgentOps/20261115/fog3d/stage2a/impl/step<N>-handoff.md` 에 남긴다. 독립 반박(Opus)은 단계 3·4 뒤와 5·6 뒤에 한다(셰이더에 결함을 일부러 넣는 변이 시험 포함).
10. 통과 기준 숫자(GPU 대 기준 덤프의 장별 허용, 탐침 P1·P4 의 상한, 재질 명령 수 상한)는 단계 2 가 기준 덤프를 뽑은 뒤·GPU 를 재기 전에 감독이 `Docs/VolumeFog3D/stage2/prereg-2a-impl.md` 로 잠근다.
11. 새 코드에는 주석을 달지 않는다(초안의 구조체 예시에 있는 설명 주석은 코드에 넣지 않는다). 클래스·구조체 이름에 TD 를 넣는다. 값은 `FTDFog3DSettings` 와 JSON 정본에 둔다.

## ① 범위와 하지 않는 것

한다: (1) 땅 높이 그림 경계(평면 R32F, 텍셀 = 세계 기둥) (2) 굽기에서 기둥 높이·기울기·벽을 만들고 막힌 칸에 벽을 합치며 상자 막힘을 세계 높이로 가림 (3) 입력(구)의 틀을 GPU 에서 구해 R4 식으로 무게를 계산하고 윗면 목표값을 변환 (4) 게임: 3D 안개 전용 직교 깊이 캡처 → 변환 패스 → 높이 그림 (5) 새 재질(땅 기준 그리기) (6) 기준 덤프 도구·재생 장면·GPU 대조 (7) 탐침·비용 재기.
하지 않는다: 2b(물체 자동 수집)·2c(움직이는 물체·파편), 움직이거나 부서지는 땅, 동굴·복층·지붕 아래, 구운 높이 캐시 원본(같은 경계 뒤에 나중에 붙일 수 있게만 둠), 기존 재질 `/Game/FluidFX/Materials/M_TD_Fog3DVolume` 덮어쓰기, 설계 등록의 식·판정선 변경, 1단계 계산(옮기기·압력·몰림·정리)의 식 변경.
고르지 않은 길과 까닭(한 줄씩):
- 랜드스케이프 직접 질의(`ALandscapeProxy::GetHeightAtLocation`, LandscapeProxy.h:1101): 정적 메시 바닥을 못 보고, 창 이동마다 수천 번 게임 스레드 동기 호출.
- 수직 선 추적: `ECC_WorldStatic` 전부를 맞춰 상자 윗면이 땅이 된다(HVOL 사례, 6830ced2).
- 2D 캡처 공유: 텍셀 약 29.8cm·중심 비정렬·2D 안개가 없으면 캡처도 없음(TDFluidVolumeDriverComponent.cpp:737·756, 407).
- 구운 높이 캐시(`UTDFluidHeightCacheAsset`): 정적 땅 전용. 시험 경로와 같은 "올리기" 입력으로 나중에 붙일 수 있다.

## ② 자료

| 이름 | 형식·크기 | 원환 | 쓰는 쪽 | 읽는 쪽 | 비고 |
|---|---|---|---|---|---|
| 높이 그림(정본) `FTDFluidGpuFog3DState::GroundHeightTarget` | 2D `PF_R32_FLOAT`, S×S (S = N + 2B, B = 캡처 테두리 칸) | 아니오(평면) | 올리기 패스(시험) 또는 변환 패스(게임) | 굽기(`Fog3DBakeTerrainCS`) | 값 = 기준 Z 에서 잰 H(cm). 텍셀 (u,v) = 세계 기둥 `GroundMinCell + (u,v)`. 상태가 소유(풀 렌더 타깃), 창 이동 재굽기 때 다시 읽으므로 유지 |
| 재질용 높이 그림 사본 | `UTextureRenderTarget2D`, R32F, S×S, `bCanCreateUAV`(TextureRenderTarget.h:33), 필터 쌍선형 | 아니오 | 정본을 만든 걸음에서 복사 패스 | 새 재질 | 구역 액터 소유(`RF_Transient`). 시험 경로는 없음(널 허용) |
| 캡처 깊이 타깃 | `UTextureRenderTarget2D` `RTF_R32f`, 지우기 1e9, S×S | 아니오 | `USceneCaptureComponent2D`(깊이) | 변환 패스 | 2D 와 같은 설정(Driver.cpp:417~432) |
| 기둥 땅 그림 `ColumnTerrainTarget` | 2D `PF_A32B32G32R32F`, (N+2)×(N+2) | 아니오(창 기준, 칸 (i,j) 는 텍셀 (i+1, j+1), i ∈ [−1, N]) | `Fog3DBakeTerrainCS` | 막힘 굽기, 틀 패스, 옮기기 패스 | x = H, y = Sx, z = Sy, w = 벽(0/1). 고리 텍셀은 x 만 의미(y·z·w = 0) |
| 입력 틀 버퍼 | 구조적 버퍼 `FTDFluidGpuFog3DSphereFrame` × 16 | — | `Fog3DInputFrameCS` | 옮기기 패스 | 걸음마다 새로 만든다(RDG 임시) |
| 구·상자 버퍼 | 그대로(48·32바이트) | — | — | — | **레이아웃 바꾸지 않는다**(TDFluidGpuFog3D.h:16~31) |

새 구조체(셰이더와 같은 꼴, `Public/TDFluidGpuFog3D.h`):
```
struct FTDFluidGpuFog3DSphereFrame   // GPU 만 쓰고 읽는다. 크기 확인용 거울
{ FVector4f TiltedStartCmReferenceHeightCm; FVector4f TiltedEndCm; FVector4f MidpointCmFrameSlope; FVector4f ReferenceColumnCm; };
static_assert(sizeof(FTDFluidGpuFog3DSphereFrame) == 64, "...");
struct FTDFluidGpuFog3DGroundUpload  { TArray<float> HeightsCm; FIntPoint MinCell; FIntPoint Size; };            // 행 = y, 인덱스 = y*Size.X + x
struct FTDFluidGpuFog3DGroundCapture { FTextureRenderTargetResource* DepthTarget = nullptr; FIntPoint MinCell; int32 Size = 0; float DecodeOffsetCm = 0; float MissingHeightCm = 0; };
```
`FTDFluidGpuFog3DFrameInput` 에 더함: `bool bFollowsGround=false; float GroundWallStepCm=30; bool bGroundChanged=false; const FTDFluidGpuFog3DGroundUpload* GroundUpload=nullptr; FTDFluidGpuFog3DGroundCapture GroundCapture; FTextureRenderTargetResource* GroundMaterialTarget=nullptr;`. 상태에 더함: `GroundHeightTarget, ColumnTerrainTarget, FIntPoint GroundMinCell, int32 GroundSize, bool bHasGround`. `Release()` 에서 함께 놓는다.
메모리(N=256, B=64, S=384): 정본 0.59MB + 사본 0.59MB + 캡처 0.59MB(+캡처 내부 깊이 버퍼 [확인 못 함]) + 기둥 땅 1.07MB ≈ 2.8MB. 지금 상태 256²×8×74바이트 = 38.8MB 대비 +7%. N=128(S=256): 약 1.1MB. `EstimateStateBytes` 에 (N+2)²×16 + S²×4 항을 더한다(S 는 상태 인수로).

## ③ 패스 목록 (한 걸음, `AddTDFluidGpuFog3DPasses`)

| 순서 | 패스 | 도는 때 | 읽음 → 씀 | 지금과 비교 |
|---|---|---|---|---|
| 0a | `Fog3DUploadGround`(복사, 셰이더 없음 `GraphBuilder.QueueTextureUpload` 또는 버퍼→텍스처 [구현자 선택]) | `bFollowsGround && bGroundChanged && GroundUpload` | 업로드 배열 → 정본 | 새 |
| 0b | `Fog3DConvertGroundCS` (8×8) | `bFollowsGround && bGroundChanged && GroundCapture.DepthTarget` | 캡처 깊이 → 정본, 그다음 정본 → 재질 사본(복사 패스) | 새 |
| 1 | `Fog3DBakeTerrainCS` (8×8, (N+2)²) | `bRebakes` | 정본 → 기둥 땅 | 새. `bFollowsGround` 꺼짐이면 정본을 읽지 않고 0 을 쓴다 |
| 2 | `Fog3DBakeBlockedCS` | `bRebakes` | 상자·**기둥 땅** → CellBlocked | 바뀜: 칸 가운데 z = H + (k+½)L, 벽 OR |
| 3 | `Fog3DBakeFaceFlagsCS`, `Fog3DCleanCS` | `bRebakes` | 그대로 | 그대로 |
| 4 | `Fog3DOverrideCS` | 덮어쓰기 있을 때 | 그대로 | 그대로 |
| 5 | `Fog3DInputFrameCS` (64×1, 한 그룹) | 매 걸음, 구가 1개 이상 | 구·기둥 땅 → 틀 버퍼 | 새 |
| 6 | `Fog3DAdvectCS` | 매 걸음 | + 기둥 땅·틀 버퍼 | 바뀜: `BlendFog3DSphereWind`·`ErodeFog3DFill` 만 |
| 7 | 몰림·압력·기울기·덤프 | 그대로 | 그대로 | 그대로(덤프에 기둥 장 따로, §8) |

`bRebakes = (지금 식, TDFluidGpuFog3D.cpp:639) || FrameInput.bGroundChanged`. 정본은 "올리기/변환이 한 번이라도 됐는가"(`State.bHasGround`)를 지키고, `bFollowsGround` 인데 정본이 없거나 창+고리가 정본 사각형 밖이면 `IsFog3DFrameInputValid` 가 거부한다(호출자가 미리 막는다, §5).
틀 패스를 옮기기 패스 안에 넣지 않는 까닭: 입력마다 한 번(기준 모의 `_input_frame` 이 입력마다 한 번)이라 일치가 쉽고, 칸마다 네 칸 보간을 되풀이하지 않는다. 비용은 스레드 16개 한 디스패치.

## ④ 식 (셰이더 ↔ 기준 모의)

칸 (i,j,k) 는 창 지역 번호. 지역 위치는 지금처럼 창 최소칸 모서리 원점 cm(xy), z 는 기준 Z 에서 잰 cm. CT(i,j) = 기둥 땅 텍셀 (i+1, j+1).

4.1 기둥 높이·면 높이 — `fog3d_terrain.py:174~187`(`_build_columns`), `:189~196`(`world_height`)
- 굽기: 세계 기둥 c = WindowMinCell + (p,q), p,q ∈ [−1, N]. 정본 텍셀 = c − GroundMinCell 에서 `Load`(보간 없음). 값 = H.
- 칸 가운데 z_c = H(i,j) + (k+½)L, 윗면 z_w = H(i,j) + (k+1)L, +x 면 z_u = ½(H(i,j) + H(i+1,j)) + (k+½)L, +y 면 z_v = ½(H(i,j) + H(i,j+1)) + (k+½)L. 가로 자리는 지금 그대로(가운데 (i+½)h, 동쪽 면 (i+1)h, TDFluidGpuFog3D.usf:804~809).
4.2 벽 — `steep_columns` `:124~130`: 창 안 기둥마다 네 면 이웃 n 에 대해 `H − H_n > T` 하나라도 참이면 w = 1. 엄격한 `>`.
4.3 기울기 — `_axis_slope`·`column_slopes` `:133~146`: up = H(i+1) − H(i), down = H(i) − H(i−1). |up| ≤ T 이고 |down| ≤ T 면 (H(i+1) − H(i−1)) ÷ 2h, 아니면 up 만 되면 up ÷ h, down 만 되면 down ÷ h, 아니면 0. y 도 같다. 비교는 `<=`.
4.4 상자 막힘 — `_obstacle_mask` `:198~213`: CellBlocked = w(i,j) | (상자 안: 지금 `IsFog3DCellCenterInsideObstacle`(usf:53~60)에 z 만 z_c 로). 부풀림은 지금처럼 `MakeTDFluidGpuFog3DUprightBox` 에서.
4.5 입력 틀(R4) — `fog3d_terrain_add1.py:63~83`, 보간 `fog3d_terrain.py:325~333`
- 구 s 의 a = Start, b = End(지역 cm). m = ½(a + b) (가로).
- 기울기 읽는 자리 x* = clamp(m.x, ½h, (N−½)h), y* 도. fx = (x* − ½h) ÷ h, i0 = clamp(floor(fx), 0, N−2), tx = fx − i0. Sc = 네 기둥 (i0..i0+1, j0..j0+1) 의 (Sx, Sy) 쌍선형(벽 기둥이어도 그 값). 식의 꼴은 `sample_columns` 와 같게: (f00(1−tx) + f10 tx)(1−ty) + (f01(1−tx) + f11 tx)ty.
- 기울이기 원점은 **자르지 않은** m. 끝점 ã.z = a.z − Scx(a.x − m.x) − Scy(a.y − m.y), b̃ 도 같다. 가로 자리는 그대로.
- 틀 버퍼: TiltedStart = (ã, Hr), TiltedEnd = (b̃, 0), MidpointCmFrameSlope = (m.x, m.y, Scx, Scy), ReferenceColumnCm = (rx, ry, 0, 0)(R5 용, R4 는 써도 안 읽음).
- 표본 (x, y, z)(4.1 의 면·가운데)마다 z̃ = z − Scx(x − m.x) − Scy(y − m.y). 무게 = 지금 `ComputeFog3DSweptBrushWeight`(usf:721~733) 에 (x, y, z̃) 와 ã·b̃ 를 넣은 값. 깎는 양 = 지금 `ComputeFog3DDwellAmount`(usf:735~782)에 칸 가운데 (x, y, z̃_c) 와 ã·b̃. 근처 판정 `IsFog3DCellNearSphere` 는 가로만 보므로 그대로(기준 모의 `_bbox` 도 가로만, fog3d_core.py:1245~1253).
4.6 [판정 뒤 확정] R5 덧층 — `fog3d_terrain_add2.py:73~116`
- 기준 기둥 ri = clamp(floor((m.x − ½h) ÷ h + 0.5), 0, N−1), rj 도. Hr = CT(ri,rj).x, (rx, ry) = ((ri+½)h, (rj+½)h). 식의 꼴을 이대로 써야 경계 위의 m(예: 1000cm = 64h)에서 기준 모의와 같은 칸이 나온다(두 쪽 모두 정확한 이진 소수 계산).
- 표본의 땅 높이 Hs: u·v 면은 그 면의 평균 높이, 윗면·가운데는 H(i,j). e = Hs − Hr − Scx(x − rx) − Scy(y − ry), g = saturate((2T − |e|) ÷ T).
- g ≥ 1 → 기울인 무게 Wt 만, g ≤ 0 → 세계 무게 Ww(원래 a·b, 원래 z) 만, 사이면 g·Wt + (1 − g)·Ww. 깎는 양도 같은 g 로(칸 가운데 Hs = H(i,j)).
4.7 목표값 — `_wind_target` `fog3d_terrain.py:238~244`: u 면 Vx, v 면 Vy, 윗면 Vz − Vx·Sx(i,j) − Vy·Sy(i,j)(표본 칸의 기둥, 틀 기울기 아님). 섞기 꼴은 지금 `BlendFog3DFaceWind` 그대로(바람 += (목표 − 바람) × 무게 × 세기).
4.8 그리기 — `drawn_ground` `:335~339`: 높이 = (구역 기준 z − Window.z) − H(x, y), H 는 재질 사본을 하드웨어 쌍선형(가장자리 고정)으로 읽음. 나머지는 지금 `TDFog3DDensity`(TDFluidGpuFog3DDensity.ush:115~124) 와 같은 식에 높이만 바꿈.
4.9 32비트에서 기준 모의(64비트)와 달라질 곳과 다루는 법
- H 반올림: 재생 파일이 H 를 float32 로 저장하고, 기준 덤프 도구도 **그 float32 값**을 기둥 높이로 쓴다(§8). 잠긴 판정 실행과는 ulp 크기만 다르다.
- 벽 문턱의 동률: 두 float32 의 차를 GPU 는 32비트로 반올림한다. 차가 T 에서 1ulp 안이면 판정이 갈릴 수 있다. 등록 지형 가운데 CURB30(차 정확히 30)·S60(29) 은 정확히 표현돼 갈리지 않는다[추정]. 재생 장면에서 그 밖의 동률을 만들지 않는다.
- 면 평균·기울기·z̃: 32비트 더하기·곱하기 반올림(상대 1e-7). 균일 비탈에서 z̃ 가 "땅에서의 높이 + 상수"와 비트까지 같지는 않다 → GPU 의 "균일 비탈 = 평지"는 허용 오차로만 본다(정보).
- 쌍선형 무게(재질): 하드웨어 보간 무게는 1/256 단위 양자화라 H 오차 ≤ (이웃 차) ÷ 512(절벽 450cm 에서 0.9cm, 2A-G3 의 3cm 안)[추정: 일반적인 GPU 동작, 이 장비 미측정 → 탐침 P1 에서 잰다].
- 창 가장자리 반 칸: 기준 모의 `sample_columns` 는 창 바깥 기둥 가운데 너머를 외삽하고(tx 를 자르지 않음), 재질은 고리·테두리의 실제 값으로 보간한다. 가장자리 반 칸 밖에서만 다르다(2A-G3 은 가장자리 8칸을 뺀다).

## ⑤ 게임 연결

5.1 기준 Z = 구역 바닥 Z(`ATDFog3DZoneActor::GetZoneFloorZ`, ZoneActor.cpp:776~779). 그래서 `PackSpheres`(Layer.cpp:393, 422~427)·`SelectAndPackObstacles`(:489~494)의 "바닥 Z 빼기"는 식이 그대로이고 뜻만 "기준 Z 에서 잰 세계 높이"가 된다. 재질 `Fog3DWindow.z`(Layer.cpp:513~519)도 그대로. 땅 따라가기 꺼짐 = H 가 모두 0 = 1단계와 같은 식(§7 의 시험이 비트까지 확인).
5.2 소유: 구역 액터가 `UPROPERTY(Transient)` 로 캡처 컴포넌트(`USceneCaptureComponent2D`, 2D 와 같은 설정: 직교, `SCS_SceneDepth`, 회전 (−90, −90, 0), `bCaptureEveryFrame=false`, `bCaptureOnMovement=false`, Driver.cpp:423~432)·캡처 깊이 타깃·재질 사본 타깃을 가진다. 만들기는 `Settings.bFollowsGround` 이고 대표 구역일 때, 지우기는 꺼지거나 `EndPlay`. 상태 기계는 월드 없이 시험할 수 있는 순수 구조체 `FTDFog3DGroundTracker`(새 파일 `TDFog3DGround.h/.cpp`)에 둔다.
5.3 캡처 사각형: 가운데 칸 C = WindowMinCell + N/2(16 배수), 최소칸 = C − S/2, S = N + 2B. 캡처 자리 XY = C × h(칸 경계), OrthoWidth = S × h, Z = 바닥 Z + 2 × ZoneHalfExtent.Z + `GroundCaptureHeightAboveZoneCm`. 해독 오프셋 = (캡처 Z − 바닥 Z) 를 double 로 빼서 float 로. 축: 2D 셰이더가 u ↔ +X, v ↔ +Y 로 읽고 화면 판정을 통과했다(Tools/FluidFX/Shaders/TDFluidFogVolume.hlsl:21~23) → 같은 회전에서 텍셀 (u,v) = 최소칸 + (u,v) 로 둔다. 반 텍셀·축 뒤집힘은 탐침 P1 이 확인한다.
5.4 내용: "땅"은 2D 와 같다(랜드스케이프 컴포넌트 + `TDFluidTerrain` 태그 액터, 소유 액터·폰 숨김). 목록 만들기를 한 곳으로 뺀다: 새 `FTDFluidTerrainCapture`(새 파일 `Source/TDGame/FluidFX/TDFluidTerrainCapture.h/.cpp`)의 `static void ApplyTerrainOnlyList(USceneCaptureComponent2D&, UWorld&)`(Driver.cpp:619~641 의 몸통을 옮김)과 `static void HideOwnerAndPawns(USceneCaptureComponent2D&, const AActor* Owner, bool bIgnoresPawns, UWorld&)`(:760~771). `IsLandscapeSurfaceComponent`·`ShouldRenderHeightCaptureInMainRenderer` 도 옮기고 2D 쪽은 위임만 남긴다(기존 2D 시험 이름 유지). `bRenderInMainRenderer` = 게임 뷰포트 있음(L-editor-93).
5.5 갱신 상태 기계(`FTDFog3DGroundTracker`, 프레임 번호는 `GFrameCounter`):
- 상태: 그림 없음 / 요청됨(사각형 R_p, 요청 프레임 F) / 준비됨(사각형 R_r).
- 매 틱(걸음 전): 다음 창(새 공개 함수 `FTDFog3DLayer::PreviewNextWindowMinCell`)의 가운데가 마지막 요청 가운데와 다르면 새 요청(캡처 자리 설정·목록 적용·`CaptureScene()`), R_p 기록.
- 요청은 `GroundCaptureLatencyFrames`(기본 2, 탐침 P2 가 1 을 허락하면 1) 프레임 뒤 준비됨으로 바뀌고, 그 걸음의 `FrameInput.bGroundChanged = true`, `GroundCapture` 는 R_r 로 채운다(변환은 그 걸음 렌더 명령 안. 같은 프레임 장면 렌더보다 먼저 돌므로 캡처가 끝난 내용만 읽는다[추정: L-editor-93 은 순서를 확인하지 않음]).
- 걸음을 낼지: 준비된 R_r 가 다음 창 + 고리를 덮으면 낸다. 못 덮으면 걸음을 내지 않고 시계 몫을 남긴다(`ConsumeStepClock` 안 함). 첫 프레임·순간 이동·구역 재진입은 이 규칙으로 최대 지연 프레임만큼 기다린다. 한 번에 테두리보다 많이 움직인 창은 같은 규칙으로 기다린다(테두리 기본 64칸이면 64칸 이상 움직였을 때). 창 칸 수 이상 뛰면 지금처럼 초기화(Layer.cpp:211).
- 구역 밖·비활성: 캡처하지 않는다. 다시 들어오면 그림 없음부터.
5.6 재질: 새 에셋 이름 제안 `/Game/FluidFX/Materials/M_TD_Fog3DVolumeGround`. 파라미터 둘: 텍스처 `Fog3DGroundHeight`(재질 사본), 벡터 `Fog3DGround` = (그림 최소 모서리 X − 구역 기준 X, Y − 기준 Y, S, S). 고르기: `FTDFog3DSettings` 에 `TSoftObjectPtr<UMaterialInterface> GroundFollowVolumeMaterial` 를 두고 `bFollowsGround` 이고 비어 있지 않으면 그것을, 아니면 액터의 `VolumeMaterial` 을 비동기로 불러 쓴다(지금 `RequestVolumeMaterial`, ZoneActor.cpp:431~466 꼴). 설정 표 검사기는 소프트 경로를 지원한다(check_fluid_preset_data.py:115).
5.7 설정(`FTDFog3DSettings`, Globals.TDFog3DSettings, JSON 정본에 같은 값):

| 필드 | 기본 | 검사 |
|---|---|---|
| `bFollowsGround` | false(화면 확인 전까지) | — |
| `GroundWallStepCm` | 30 | > 0 |
| `GroundCaptureBorderCells` | 64 | ≥ 17(16칸 이동 + 고리) |
| `GroundCaptureHeightAboveZoneCm` | 500 | ≥ 0 |
| `GroundCaptureLatencyFrames` | 2 | 1~4 |
| `GroundMissingHeightCm` | 0(= 구역 바닥) | 유한 |
| `GroundMissingDepthThresholdCm` | 5.0e8 | > 0 |
| `GroundFollowVolumeMaterial` | `/Game/FluidFX/Materials/M_TD_Fog3DVolumeGround` | 끄면 안 씀 |

5.8 땅을 못 찾은 기둥(제안): 변환 패스가 깊이 ≥ 문턱이면 H = `GroundMissingHeightCm`(구역 바닥)로 쓴다. 막지 않는다. 까닭: 정본이 R32F 한 채널이라 "막힘 + 그리지 않음"을 담으려면 둘째 채널 또는 특수값이 필요하고, 특수값은 재질 하드웨어 보간에 섞여 이웃까지 망가뜨린다. 결과: 구멍·랜드스케이프 밖은 1단계처럼 바닥 높이 안개가 되고, 둘레의 높은 땅은 4.2 규칙으로 저절로 벽이 된다. 단점: 바닥보다 낮은 실제 지형을 못 찾은 곳은 안개가 떠 보일 수 있다(화면 확인 항목). 다른 길(막힘 + 그리지 않음)의 값: 정본 RG32F·재질 네 번 읽기·비교 → 명령 수 증가, 뼈대 1(R32F)과 어긋남.

## ⑥ 파일별 변경

- `Source/TDFluidGpu/Public/TDFluidGpuFog3D.h`: §2 의 구조체·필드·static_assert. 공개 함수 시그니처는 그대로.
- `Source/TDFluidGpu/Private/TDFluidGpuFog3D.cpp`: `IsFog3DFrameInputValid`(땅 검사: 업로드 크기 = Size.X×Size.Y, 유한, 창+고리 ⊆ 사각형, 캡처 크기 일치), `HasFog3DStateTextures`·`CreateFog3DStateTextures`(기둥 땅 (N+2)², 정본은 S 가 바뀔 때 다시 만듦), `AddFog3DGroundPasses`(새, 0a·0b), `AddFog3DBakePasses`(1 추가, 2 에 기둥 땅 SRV), `AddFog3DInputFramePass`(새), `AddFog3DAdvectPass`(SRV 둘 추가), `EstimateStateBytes`, `Release`, 새 셰이더 클래스 `FTDFluidGpuFog3DConvertGroundCS`·`FTDFluidGpuFog3DBakeTerrainCS`·`FTDFluidGpuFog3DInputFrameCS`.
- `Shaders/Private/TDFluidGpuFog3DCommon.ush`: `GroundFollow`(uint)·`GroundWallStepCm` 인수, `ReadFog3DColumnTerrain(int2 LocalColumn)`.
- `Shaders/Private/TDFluidGpuFog3D.usf`: `Fog3DConvertGroundCS`, `Fog3DBakeTerrainCS`, `Fog3DInputFrameCS`, `Fog3DBakeBlockedCS`(z·벽), `BlendFog3DSphereWind`·`ErodeFog3DFill`(틀 버퍼·세계 높이·목표값). 새 함수 `GetFog3DSampleWorldPositionCm(int3 LocalCell, uint SampleKind)`, `ComputeFog3DFramedBrushWeight`, `ComputeFog3DFramedDwellAmount`, `ComputeFog3DTopWindTarget`. 1단계 함수의 식은 바꾸지 않는다.
- `Shaders/Private/TDFluidGpuFog3DDensityGround.ush`(새 파일, ⓪절 3): 기존 `TDFluidGpuFog3DDensity.ush` 를 include 하고 새 함수 `TDFog3DGroundUv`, `TDFog3DGroundStateUvw`, `TDFog3DGroundStateWeight`, `TDFog3DGroundDensity` 를 둔다. 기존 파일은 한 글자도 바꾸지 않는다. 땅 변형 재질은 include 방식만 쓴다.
- `Private/TDFluidGpuFog3DDensityProbe.*`: 땅 변형 진입점(높이 값을 표본마다 받는 꼴).
- `Source/TDGame/FluidFX/TDFog3DTypes.h/.cpp`: §5.7 필드, `CollectTDFog3DSettingsProblems` 검사.
- `TDFog3DGround.h/.cpp`(새): `FTDFog3DGroundTracker`, `FTDFog3DGroundRect`(최소칸·크기·덮음 판정), 사각형 계산 정적 함수.
- `TDFog3DLayer.h/.cpp`: `PreviewNextWindowMinCell` 공개, `FTDFog3DStepRequest` 에 `FTDFog3DGroundStep Ground`(켬, 바뀜, 캡처 자원, 사각형, 해독, 못 찾음 값, 재질 사본 자원) → `PackStep` 이 FrameInput 에 옮김.
- `TDFog3DZoneActor.h/.cpp`: 캡처·타깃 소유, 상태 기계 호출, 걸음 보류, 재질 고르기, `Fog3DGroundHeight`·`Fog3DGround` 쓰기(`FTDFog3DMaterialParameters` 에 `Ground` 벡터 추가).
- `TDFluidTerrainCapture.h/.cpp`(새), `TDFluidVolumeDriverComponent.cpp`(위임만).
- `Content/FluidFX/PresetData/TDFluidPresetTuning.json`: 새 필드.
- `Tools/FluidFX/editor_make_fog3d_material.py`: 요청 JSON `"variant": "flat"|"ground"`(기본 flat = 지금과 바이트까지 같은 동작). ground 변형의 자산 이름·몸통 줄·입력·텍스처 파라미터 표. 새 자산 만들기는 사용자에게 알린 뒤.
- `Tools/FluidFX/check_fog3d_material_hlsl.py`: 두 변형 모두 컴파일·이름 계약, flat 변형의 명령 수가 바뀌지 않았는지(기준값 대조).
- 새 도구 `Tools/FluidFX/fog3d_ref/fog3d_replay_s2a.py`·`fog3d_compare_s2a.py`·`fog3d_density_fixture_s2a.py`(잠긴 파일은 import 만).
- 시험: `Private/Tests/TDFluidGpuFog3DReplay.*`(선택 키 `ground`), `TDFluidGpuFog3DDumpFiles.*`(기둥 장), `TDFluidGpuFog3DGroundRhiTests.cpp`(새), `Source/TDGame/FluidFX/Tests/TDFog3DGroundTests.cpp`(새).

## ⑦ 시험 목록

자동화(RHI 없음, `TDGame.FluidFX.Fog3D.*`): `GroundSettingsValidation`, `GroundCaptureRect`(가운데 스냅·최소칸·OrthoWidth = S·h·세계 자리), `GroundCoverage`(16칸 이동 덮음, 32칸 못 덮음), `GroundTrackerStates`(첫 프레임 대기, 지연 프레임 뒤 준비, 이동 → 새 요청, 순간 이동 → 초기화 + 대기, 구역 이탈), `GroundFrameInputAssembly`(켬·끔), `GroundMaterialChoice`, `GroundMaterialParameters`, `TerrainCaptureListShared`(공유 목록 = 랜드스케이프 컴포넌트 + 태그 액터). 기존 `SettingsValidation`·`SettingsTable`·`FrameInputAssembly`·`MaterialWindowParameters`·`ZoneMaterialParameters`·2D `TDFluidVolumeDriverTests` 그대로 통과.
RHI(`TDGame.FluidFXRhi.Fog3D.*`): `GroundOffMatchesStage1`(꺼짐 대 켬 + H≡0 그림, 덤프 비트까지 같음), `GroundBakeColumns`(계단·절벽·비탈 업로드 → 기둥 땅 H 정확·S ≤ 1e-6·벽 정확, 고리 포함), `GroundBlockedWorldHeight`(비탈 위 상자), `GroundConvertCapture`(손으로 만든 R32F 깊이 텍스처에 1e9 텍셀 섞음 → 정본 H·못 찾음 값·축), `GroundInputFrames`(틀 버퍼 되읽기 ↔ CPU 식), `Stage2aReplayDumps`, `GroundDensityMatchesReference`, `GroundStepTiming`, 탐침 `GroundCaptureAlignmentProbe`(§9). 기존 `ReplayDumps`·`AddendumReplayDumps`·`Stage1dReplayDumps`·`ModuleSmoke`·`RestStaysZero`·`Determinism`·`FarOrigin`·`WindowShiftOutput`·`ClosedFaces`·`SealedColumn`·`PreviousWindowCheck`·`BlockedCellDrawFill`(+`AtWindowEdge`)·`DensityMatchesReference`·`LayerOutputFrames` 그대로 통과(뼈대 8).
검사기: `check_fog3d_material_hlsl.py`(두 변형), `python Tools/check_sync_loads.py`(새 동기 로드 0: 렌더 타깃은 `NewObject`, 재질은 비동기), `check_fluid_preset_data.py`(새 필드·소프트 경로), `python Tools/check_cook_coverage.py`(새 재질 경로) [확인 못 함: JSON 정본의 소프트 경로가 이 검사 대상인지].
Python: 기존 `fog3d_compare_v2.py` 가 기존 덤프에 그대로 통과, 새 `fog3d_compare_s2a.py`.

## ⑧ 기준 덤프 도구와 재생 장면

- `fog3d_replay_s2a.py`: `fog3d_replay.py` 의 `Replay`·`execute` 를 쓰고 `rp.make_sim` 을 `make_sim_s2a` 로 바꾼다(`fog3d_replay_v2.py:123~134` 와 같은 수법). 모의 = `TerrainSimR4`(R5 가 고르면 `TerrainSimR5`), 지형 = `GridTerrain`(재생 파일의 float32 높이 그림을 기둥 가운데에서만 돌려주는 `Terrain`; 기둥 가운데가 아닌 자리를 물으면 예외).
- 재생 파일: 형식 `fog3d-replay-1` + 선택 키 `ground` = {`terrain`: 이름, `candidate`: "R4"|"R5", `min_cell`: [x,y](세계 칸), `size`: [sx,sy], `reference_z_cm`: 0, `file`: `<장면>.ground.bin`(float32 리틀엔디언, 행 = y), `sha256`}. 그림은 모든 걸음의 창 + 고리를 덮는다. 구 z 는 그 자리 해석식 땅 + 위 높이(설계 2절).
- 덤프: `fog3d-dump-1` 그대로 + 선택 메타 `column_fields`: ["ground_h","slope_x","slope_y","wall"] 와 `<장면>__<덤프>.column.bin`(N×N float32, 창 기둥). 입력 틀 확인용 `frames`(구 16 × 16 float)는 선택.
- 비교(꼴만, 숫자는 감독이 기준 덤프를 뽑은 뒤·GPU 를 재기 전에 잠근다): 장마다 유체 칸 최대 |GPU − 기준| ≤ 장별 허용(1e-4cm 흔든 기준 덤프와의 차로 정함, 1단계 방식), 유체 표시·막힘 정확히 같음, ground_h 정확히 같음, 기울기 상대 ≤ 허용, wall 정확히 같음.
- 재생 장면 후보(전부 128칸, 1/60초):

| 이름 | 지형 | 내용 | 노리는 결함 |
|---|---|---|---|
| s2a_slope_walk_up / _down | S30 | 2절 걷기 +x / −x | 면 높이·틀·목표값 |
| s2a_slope_y_walk | S30Y | +y 걷기 | x·y 뒤바뀜 |
| s2a_cliff_walk_blade | CLIFF | 2A-G4 (가) 걷기 + 칼 | 벽·반대편 0 |
| s2a_curb30_cross | CURB30 | +x 걷기 | 동률 T, 턱 옆 기울기 0.96 |
| s2a_hill_run_window | HILL | 2A-G9 꼴 달리기 2초, 창 따라감 | 창 이동 재굽기·고리 |
| s2a_still_rough | ROUGH | 입력 없이 120번, 30번마다 +16칸 | 정지 0(정확히 0) |
| s2a_slope_curb | S30CURB100 | 정보 ⑭ 장면 | R4/R5 차이 |
| s2a_flat_offset | F1 | 1단계 walk 장면 + 137.5 | 일정 H |
| s2a_box_on_slope | S30 | 비탈 위 상자 + 걷기 | 막힘 세계 높이 |
| s2a_cliff_at_edge | CLIFF | 절벽선이 창 가장자리 기둥에 오게 창 고정 | 고리 읽기 |
| s2a_frame_at_edge | S30 | 구 가운데가 창 가장자리 반 칸 안 | 틀 자르기 |

## ⑨ 위험 먼저 탐침과 물러설 길 (단계 1·7)

| 탐침 | 어디서·어떻게 | 통과 | 실패 시 |
|---|---|---|---|
| P1 반 텍셀·축·깊이 정밀도 | RHI 시험 `GroundCaptureAlignmentProbe`: 임시 게임 월드에 `TDFluidTerrain` 태그 계단(단 20cm, 단 경계를 기둥 경계에서 ±¼칸 엇갈려 둠) → 독립 렌더러 캡처 → 변환 → 정본 되읽기, 해석식 H 와 대조. 큐브 메시는 시험 시작 전에 비동기 로드 | 모든 기둥 \|e\| ≤ 0.5cm, 단 경계가 맞는 기둥에 떨어짐 | 변환 패스에 반 텍셀 이동·축 뒤집기 인수(탐침이 정한 상수). 깊이 정밀도 부족이면 `bAutoCalculateOrthoPlanes`(SceneCaptureComponent2D.h:60) 끄고 근·원 평면을 구역 높이로 좁힘. 그래도 안 되면 정적 무대는 구운 캐시를 같은 "올리기" 입력으로 |
| P2 캡처 완료와 걸음 순서 | PIE(3D 무대): 태그 상자를 옮긴 뒤 요청 프레임 F 와 정본에 새 자리가 나타난 걸음 프레임을 로그(개발용 콘솔 변수 뒤의 되읽기 한 텍셀) | F+1 에 나타남 → 지연 1 | 지연 2~4(설정값). 주 렌더러에서 끝내 안 되면 독립 렌더러(재캡처 프레임 +0.585ms, L-editor-93) |
| P3 랜드스케이프 상세 단계 | PIE(랜드스케이프 무대 [확인 못 함: 어느 무대]): 정본 대 `GetHeightAtLocation` 200기둥(탐침 코드에서만 호출) | 최대 차 ≤ 2cm | `LODDistanceFactor`(SceneCaptureComponent.h:152) 조정 [효과 확인 못 함] → 안 되면 랜드스케이프 무대만 CPU 질의로 띠를 채워 "올리기" 입력으로(같은 경계) |
| P4 캡처 비용 | §10 | 재캡처 프레임 GPU 증가 ≤ 감독이 잠글 상한 | 테두리를 줄이거나(최소 17칸), 창이 사각형 가장자리 17칸 안으로 올 때만 재캡처, 또는 독립 렌더러 금지 |

## ⑩ 비용 재기

- 계산 쪽: RHI `GroundStepTiming`(기존 `StepTiming` 꼴, 128·256, 땅 켬/끔, 재굽기 있는 걸음과 없는 걸음 따로). 더 드는 것[추정]: 굽기에 (N+2)² 스레드 한 번 + 막힘 굽기에서 칸마다 한 번 읽기, 매 걸음 틀 패스 1 그룹, 구 근처 칸마다 면 높이 세 번 읽기 + 곱셈 넷(R5 는 무게 두 번).
- 캡처: S² = 256²(N=128)·384²(N=256), 견줌용 162²·290²·512², 주 렌더러/독립 렌더러, 강제 재캡처 프레임의 GPUTime 차. 방법은 `Saved/AgentOps/20261110/fog-terrain/cost.md` 와 같은 꼴, 도구 `Tools/FluidFX/pie_profile_fluid.py --csv-gpu`. 목록 수집 비용(`HeightCaptureList` 와 같은 CSV 통계 이름을 3D 용으로 새로)도.
- 재질: `check_fog3d_material_hlsl.py` 의 명령 수·표본 수(flat 대 ground). 제안 상한: 표본 +1, 명령 +40 이하(감독이 잠금).

## ⑪ 구현 차례 (같은 빌드 트리, 빌더가 차례로)

1. **공유 목록 + 탐침 P1·P4.** `TDFluidTerrainCapture` 로 옮기고 2D 위임, 탐침 시험. 끝난 모습: 2D 시험 전부 통과, P1 결과 표. 명령: `python Tools/ue_editor.py restart` → `python Tools/check_automation_tests.py --filter TDGame.FluidFX` → `--filter TDGame.FluidFXRhi.Fog3D.GroundCaptureAlignmentProbe`.
2. **기준 도구·재생 장면(파이썬, 빌드 불필요, 1 과 병렬 가능).** `fog3d_replay_s2a.py` 로 `replays_s2a/` 와 `Saved/FluidFX/Fog3D/ref_s2a/`·흔든 덤프. 끝난 모습: 장면 11개 덤프, 감독이 허용 숫자 잠금. 명령: `PYTHONUTF8=1 python -B Tools/FluidFX/fog3d_ref/fog3d_replay_s2a.py --jobs 8`.
3. **GPU 핵심(R4).** §2·§3·§4.1~4.5·4.7, 올리기 경로만. 끝난 모습: `GroundOffMatchesStage1`·`GroundBakeColumns`·`GroundBlockedWorldHeight`·`GroundInputFrames` 와 기존 RHI 전부 통과. 명령: restart → `--filter TDGame.FluidFXRhi.Fog3D`.
4. **GPU 대조.** 재생 실행기 `ground` 키·기둥 덤프, `Stage2aReplayDumps` → `PYTHONUTF8=1 python -B Tools/FluidFX/fog3d_ref/fog3d_compare_s2a.py`(종료 0 통과·1 실패·2 덤프 없음). 끝난 모습: 잠긴 허용 안.
5. **게임 연결.** 설정·`FTDFog3DGroundTracker`·캡처 소유·변환 패스·걸음 보류·재질 파라미터. 끝난 모습: `TDGame.FluidFX.Fog3D.*` 와 `GroundConvertCapture` 통과, 검사기 셋 통과. 명령: restart → 두 필터 → `python Tools/check_sync_loads.py` → `python Tools/check_fluid_preset_data.py`.
6. **재질.** `TDFluidGpuFog3DDensityGround.ush` 새 파일, 도구 ground 변형, 새 자산(사용자에게 알린 뒤 `python Tools/run_in_editor.py Tools/FluidFX/editor_make_fog3d_material.py`, PowerShell), `fog3d_density_fixture_s2a.py`·`GroundDensityMatchesReference`. 끝난 모습: `PYTHONUTF8=1 python -B Tools/FluidFX/check_fog3d_material_hlsl.py` 통과(flat 명령 수 불변), 기존 재질 파일 미변경(`git status`).
7. **[판정 뒤 확정] R5 덧층 + PIE 탐침 P2·P3 + 비용.** R5 가 고르면 §4.6 을 넣고 단계 2 의 기준 덤프를 R5 로 다시 뽑아 단계 4 반복. 그다음 P2·P3, `GroundStepTiming`, 캡처 비용. 끝난 모습: 탐침 표·비용 표(`Saved/AgentOps/<날짜>/fog3d/stage2a/`). `bFollowsGround` 기본값은 화면 확인(2a-3) 전까지 false 로 둔다.

## ⑫ 조사 메모 열린 질문 11개에 대한 답(제안)

1. 땅 = 랜드스케이프 컴포넌트 + `TDFluidTerrain` 태그 액터를 한 캡처로 합친다(2D 와 같은 뜻, 목록 코드 공유).
2. 정본은 평면(원환 아님) R32F S×S(S = N + 2 × 테두리, 기본 N + 128), 계산용 기둥 땅은 창 기준 (N+2)² RGBA32F. 계산은 `Load`(보간 없음), 재질은 하드웨어 쌍선형.
3. 못 찾은 기둥 = 구역 바닥 값(설정), 막지 않음(§5.8).
4. 첫 걸음·순간 이동·큰 점프는 정본이 창+고리를 덮을 때까지 걸음을 보류(최대 지연 프레임), 창 칸 수 이상 점프는 지금처럼 초기화.
5. 지금처럼 창 전체를 다시 굽는다(재굽기 비용은 창 이동 때만). 고리는 정본에서 읽는다.
6. GPU 가 높이 그림에서 읽는다(틀 패스). 구 구조체는 그대로 48바이트.
7. `FloorZ` = 기준 Z. 빼기 식·재질 `Window.z` 는 그대로, 뜻만 바뀐다.
8. 재질 파라미터 둘(`Fog3DGroundHeight`, `Fog3DGround`), 가장자리 고정 쌍선형. 검사기는 두 변형을 컴파일하고 flat 명령 수 불변을 본다.
9. §5.7 표. 코드 상수 없음.
10. 새 동기 로드 없음(렌더 타깃 `NewObject`, 재질 `FStreamableManager`). 탐침의 큐브 메시도 비동기.
11. P1(계단 정렬)·P2(순서)·P3(상세 단계)·P4(비용)를 단계 1·7 에 두고, 계산 정확성은 재생 장면 11개의 GPU 대조로 본다.

## ⑬ 이의·확인 못 한 것

뼈대 1~5 가운데 불가능한 것은 없다. 뼈대와 어긋나거나 보탠 것:
1. (뼈대 1·5 보탬) 계산 핵심의 정본은 상태 소유 풀 텍스처라 재질이 직접 못 읽는다(재질은 `UTexture` 가 필요). 그래서 변환 걸음에 정본을 구역 액터 소유 렌더 타깃으로 한 번 복사한다(S²×4바이트). 대안: 재질이 캡처 타깃을 직접 해독 → 같은 프레임 장면 렌더 중 캡처가 덮어쓸 때 내용·파라미터가 한 프레임 어긋나고 1e9 텍셀이 보간에 섞여 비추천.
2. (뼈대 4) 기준 Z 를 구역 바닥 Z 로 두면 "구 자리를 기준 Z 에서 잰 높이로 올린다"는 지금 식(바닥 빼기) 그대로다. 코드 변경이 아니라 뜻 변경임을 확인만 한다.
3. (뼈대 6) "막힘 + 그리지 않음" 후보는 R32F 한 채널(뼈대 1)로는 특수값 없이 담을 수 없다 → 구역 바닥 값을 제안(§5.8).
4. (뼈대 2) "창이 움직이는 프레임에 새 띠 높이가 이미 있다"는 한 번의 16칸 이동까지만 보장된다. 32칸 이상 한 번에 움직이면 걸음 보류가 필요하다(§5.5).
5. (설계 7절 밖) 구역이 창+테두리보다 넓으면 그 바깥의 평소 안개는 그림 가장자리 값으로 늘어진다(재질 가장자리 고정). 설계 식 7은 격자 안만 정의한다. 화면 확인 항목으로 넘기거나, 캡처를 구역 전체로 키우는 것(구역 40m 면 S 약 256)을 감독이 고른다.
6. (정밀도) 기준 덤프 도구가 H 를 float32 로 반올림해 쓰는 것은 잠긴 판정 실행(64비트 H)과 ulp 만큼 다르다. 설계 변경이 아니라 대조용 정렬이다.
7. (재질) 새 함수를 기존 `TDFluidGpuFog3DDensity.ush` 에 덧붙이면 기존 재질이 include 하는 파일 내용이 바뀐다 → ⓪절 3 으로 닫음(처음부터 새 파일, include 방식만).
확인 못 한 것: 직교 캡처 텍셀 가운데 정렬·축(P1), 주 렌더러 캡처가 끝나는 프레임과 걸음 순서(P2), 랜드스케이프 상세 단계·`LODDistanceFactor` 효과(P3), 130²~290² 캡처 비용(P4), 캡처 내부 깊이 버퍼 메모리, 재질 Custom 노드에서 `Texture2D.Load` 사용 가능 여부(쓰지 않는 설계로 피함), JSON 정본 소프트 경로의 쿠킹 보장, 하드웨어 쌍선형 무게 정밀도(이 장비), 시험 코드에서 임시 게임 월드에 캡처가 그려지는지(P1 의 전제. 안 되면 P1 을 PIE 로 옮기고 무대에 계단 추가 = 에셋 수정이라 사용자 승인), 랜드스케이프가 있는 시험 무대 이름.

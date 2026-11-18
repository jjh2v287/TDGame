# 04. 기성품·내장 기능 조사 (진짜 3D 볼륨 안개: 가져다 쓸 수 있나?)

작성: 2026-11-12 / 읽기 전용 조사(코드·에셋·장부 변경 없음) / 개발 PC: RTX 2060 6GB
표기: [확인: 근거] = 원문을 직접 열어 봄, [요약만: URL] = 검색 요약만 봄, [추정] = 내 추론, 미확인 = 못 찾음.
약어: SDF = Signed Distance Field(물체까지의 거리를 격자에 저장한 값), GBuffer = 화면에 그려진 물체의 깊이·법선 정보 묶음, RDG = Render Dependency Graph(언리얼 렌더링 작업 그래프), VDB = 구름·연기 같은 부피 데이터를 담는 파일 형식, SVT = Sparse Volume Texture(희소 부피 텍스처).

## 0. 한 줄 결론

"그대로 가져다 쓰면 되는 것"은 없다. 내장 나이아가라 플루이드는 진짜 3D지만 빽빽한(밀집) 격자이고 우리 프로젝트 규칙과 충돌한다. 엔비디아 Flow는 우리가 원하는 구조(희소 블록)지만 언리얼 5 연동 선례가 없다. 상용 플러그인들은 "미리 구운 재생"이거나 "2D 계산으로 3D처럼 보이기"이거나 "아직 언리얼 판이 없음"이다. 따라서 **자체 구현이 필요하다.** 대신 Flow의 블록 구조와 나이아가라 플루이드의 모듈 목록은 "무엇을 만들어야 하는지" 점검표로 크게 쓸모가 있다.

## 1. 엔진 내장 나이아가라 플루이드(Niagara Fluids) 3D 기체(Grid3D Gas)

### 1-1. 5.8에서의 상태
- 플러그인 파일에 베타(`IsBetaVersion: true`)로 표시되어 있고 `EnabledByDefault`가 없다. 즉 기본으로 꺼져 있다. [확인: C:/Program Files/Epic Games/UE_5.8/Engine/Plugins/FX/NiagaraFluids/NiagaraFluids.uplugin]
- 우리 프로젝트 `TDGame.uproject`에는 NiagaraFluids가 켜져 있지 않다. [확인: grep "Niagara" TDGame.uproject 결과 없음]
- 플러그인의 C++는 파일 3개뿐(모듈 시작 코드)이고, 셰이더도 짧은 파일 2개(`NiagaraFluids.ush` 61줄, `NiagaraFFT.ush` 33줄)뿐이다. 실제 시뮬레이션은 전부 **나이아가라 모듈 에셋(.uasset 그래프)** 안에 있다. [확인: NiagaraFluids/Source, Shaders 폴더 목록]
- 3D 기체 템플릿: Smoke, Fire, ColoredSmoke, Explosion, DustExplosion, **MovingGridFire** 등. [확인: Content/Templates/Gas/3D 목록]

### 1-2. 격자 구조: 밀집(전부 칸을 잡음), 희소 아님
- 격자 데이터 인터페이스(Grid3DCollection)는 `NumCells`(가로·세로·높이 칸 수)로 한 덩어리 3D 텍스처를 잡고(`FNiagaraPooledRWTexture`), 읽기용·쓰기용 두 벌로 번갈아 쓴다. 희소(Sparse)에 대한 설정이나 코드는 없다. [확인: Niagara/Source/Niagara/Classes/NiagaraDataInterfaceGrid3DCollection.h 17, 23, 80줄, "Sparse" 검색 결과 0건]
- 해상도는 "가장 긴 축 기준 칸 수(Resolution Max Axis)"로 정하고, 높을수록 정확하지만 느리다. [확인: https://dev.epicgames.com/documentation/unreal-engine/niagara-fluids-reference-in-unreal-engine]
- 메모리 어림 [추정]: 128x128x128 = 약 210만 칸. 칸당 숫자 8개(밀도·온도·속도3·압력·발산·경계)를 4바이트씩이면 약 67MB, 이동(advection)용 두 벌이면 약 130MB. 256 한 변이면 8배(약 1GB). RTX 2060 6GB에서 안개 하나가 이만큼 먹으면 오픈월드 다른 자원과 다투게 된다.

### 1-3. 압력 풀이 방식
- 모듈 이름에 `Grid3D_PressureIteration`, `Grid3D_ProjectPressure`, `Grid3D_ComputeDivergence`, `Grid3D_MaccormackAdvectScalar/Vector`, `Grid3D_VorticityConfinementForce`가 있다. [확인: Content/Modules/Grid3D 목록]
- 설정값은 "Pressure Solve Iterations(횟수)"와 "Pressure Relaxation(0~1)"이다. [확인: Epic 문서 위 URL]
- 따라서 방식은 **반복 횟수를 정해 놓고 여러 번 돌리는 반복 풀이**다. 이름과 설정으로 보아 자코비(Jacobi) 계열일 가능성이 높으나 그래프 내부는 열어 보지 못했다. 다중격자(multigrid)는 아닌 것으로 보인다. [추정]

### 1-4. 충돌 수단
- 경계(막힘) 모듈: `Grid3D_Landscape_Boundary`(지형), `Grid3D_PhysicsAsset_Boundary`(물리 에셋), `Grid3D_GeometryCollection_Boundary`(부서지는 물체), `Grid3D_RigidMesh_Boundary`, `Grid3D_Particle_Boundary`, `Grid3D_AddSDFToBoundary`, 데이터 채널 `NDC_FluidCollision`. [확인: 파일 이름 목록]
- Epic 문서에는 "Collide Against로 객체 종류를 추가하고 액터를 지정"이라고만 나와 있고, 지형·거리장·GBuffer 같은 세부는 문서에 없다. [확인: Epic 문서]
- GBuffer를 쓰는 모듈 이름은 **2D 쪽에만** 있다(`Grid2D_Gas_GbufferSource`). 3D 기체가 화면 깊이/GBuffer로 충돌하는지는 **미확인**.
- 정리: 지형·부서지는 물체·물리 에셋 충돌 수단 이름은 갖춰져 있다. 하지만 "수천 개 정적 물체(나무·상자·바위·책상)를 매 프레임 칸에 반영"하는 효율적 경로(거리장 한 장을 칸에 굽기 등)가 쓸 만한지는 직접 측정해야 안다. [추정]

### 1-5. 플레이어를 따라 움직일 수 있나
- 가능성은 있다. `Grid3D_MovingGridFire` 템플릿, `Grid3D_ResampleFromOwnerMotion`, `Grid3D_OwnerMotionForce`, `Grid3D_ActorMotionForce` 모듈이 있다. [확인: 파일 이름 목록]
- 그러나 Epic 문서에는 따라 움직이는 격자에 대한 설명이 없다. [확인: Epic 문서] 격자가 움직일 때 옛 값을 새 위치로 다시 샘플링(resample)하는 방식으로 보이며, 이러면 매 번 이동마다 안개가 뭉개지거나(흐려짐) 칸 경계에서 튈 수 있다. 칸 단위로 격자를 "미끄러뜨리는(ring buffer 식)" 방식인지는 **미확인**. [추정]
- 2D 쪽에는 얕은 물용 `MovingWindow_Shift*` 모듈이 있어 "창이 움직이면 값을 옮김" 개념이 구현돼 있다. [확인: 파일 이름]

### 1-6. 알려진 비용 수준
- Epic 공식: "3D 시뮬레이션은 더 사실적이지만 메모리와 GPU 비용이 더 크다. 3D는 주인공급(hero) 이펙트나 영상용에 적합하고, 실시간에는 2D가 낫다." [확인: Epic 문서 위 URL]
- 해상도를 너무 높이면 충돌(크래시)했다는 포럼 글, 5.5→5.6에서 Grid3D 기체가 깨졌다는 포럼 글이 있다. [요약만: https://forums.unrealengine.com/t/how-to-fix-crashing-with-niagara-grid3d-gas-master-emitter-resolution-too-high/2087476/3 , https://forums.unrealengine.com/t/niagara-grid-3d-gas-broken-when-moving-from-5-5-to-5-6/2585944]
- RTX 2060에서의 밀리초 수치: 공식·커뮤니티 모두 **찾지 못함(미확인)**. 이 카드의 실제 비용은 직접 재야 한다.

### 1-7. 오픈월드 상시 안개로 적합한가: 부적합
이유 4가지.
1. 빽빽한 격자라서 "플레이어 주변 큰 면적"을 덮으려면 칸이 폭증한다. 넓게 하면 칸이 거칠어져 칼 휘두름 정도의 작은 움직임이 안 보이고, 촘촘하게 하면 메모리·시간이 폭증한다. [추정, 1-2 계산]
2. 베타 기능이고 버전 업(5.5→5.6)마다 깨진 사례가 있다. [요약만]
3. 공식 지침이 "영상·주인공 이펙트용"이다. [확인]
4. 파편 수백 개가 안개에 반응하는 입력 경로(데이터 채널로 받는 `NDC_FluidCollision` 등)의 확장성은 미확인.

### 1-8. TDGame 규칙과 부딪히는 곳
- 우리 규칙: 시스템 로직은 C++, 블루프린트·나이아가라 그래프에 새 로직 금지(작업 지시문의 요약). `AGENTS.md` 7절은 블루프린트에 새 분기·반복·계산·상태 기계를 넣지 말라고 한다. [확인: AGENTS.md 7절]
- **이미 같은 문제를 한 번 판정한 선례가 있다.** 결정 D-49: 선택지 (c) "NiagaraFluids 그래프"는 채택하지 않았고, 7절 해석을 "시뮬레이션·입력 판단은 C++·컴퓨트 셰이더, 나이아가라 에셋은 텍스처 샘플·색·이동 출력 전용(솔버 금지)"으로 못 박았다. [확인: C:/Project/TDGame/Docs/Tasks/decisions.md 356~360줄]
- 나이아가라 플루이드는 이동·압력·경계·힘이 전부 나이아가라 모듈 그래프 안에 있으므로, 이것을 쓰면 **솔버가 나이아가라 그래프에 있게 되어 D-49 해석과 정면 충돌**한다. 우회로(C++이 데이터 채널로 입력만 넣는 방식)는 입력만 C++일 뿐 솔버는 그대로 그래프다. [추정]
- 허용 가능한 쓰임은 하나: "해석 가능한 비교 대상 / 일회용 시연(출시 코드 아님)". 이마저 사용자 승인이 필요하다. [추정]

## 2. NVIDIA Flow

### 2-1. 구조
- 희소 부피(voxel) 기체 시뮬레이션이며 연소 모델(불·연기)을 포함한다. [확인: https://docs.omniverse.nvidia.com/kit/docs/omni.flowusd/latest/Overview.html]
- 구조 상세(omni.flowsdk 105.1.1 문서): 블록 크기 **32x16x16 칸**, 방출기(emitter)와 시뮬 피드백에 따라 블록을 동적으로 배정, 경계 상자 제한 없음(32비트 정밀도와 "최대 블록 수 풀"이 한계), 블록 위치 목록을 받아 활성 블록을 빠르게 찾는 구조(NvFlowSparse)를 만든다. 모든 것이 GPU HLSL. [확인: https://nvidia-omniverse.github.io/PhysX/flow/index.html]
- 이전 판(Flow 1.0.1)은 "동적 격자로 메모리를 아끼고, 하드웨어가 지원하면 Volume Tiled Resources(부피 타일 자원)를 최적화에 씀". [확인: https://archive.docs.nvidia.com/gameworks/content/artisttools/flow.htm]
- 쉬운 비유: 도시 전체 지도를 인쇄하는 대신 "사람이 있는 동네 블록 종이"만 필요할 때 가져다 붙이는 방식이다. 이것이 사용자가 말한 Sparse Voxel Grid의 실물이다.

### 2-2. 라이선스
- PhysX 저장소(Flow 포함, `flow/` 폴더)는 BSD-3-Clause, 저작권 2008~2025 NVIDIA. 상업 게임에 넣어도 되는 허용적 라이선스다. 단 하위 폴더 라이선스 파일을 따로 확인하라고 안내한다. [확인: https://github.com/NVIDIA-Omniverse/PhysX]
- 구판 `NVIDIAGameWorks/Flow`(1.0.1) 저장소의 LICENSE 본문은 **미확인**(페이지에 이름이 안 나옴). [확인: https://github.com/NVIDIAGameWorks/Flow 페이지에 라이선스 이름 없음]
- 빌드에 쓰는 외부 부품(DXC, GLFW, Dear ImGui, Slang 등)은 각자 라이선스를 가진다. [확인: flow/README.md]

### 2-3. 언리얼 통합 가능성과 선례
- 선례는 **언리얼 4**뿐이다. GameWorks 문서가 "언리얼 4에서 Flow 사용"을 다루고, 4.18 GameWorks 분기 README는 "대부분 플러그인으로 격리, 엔진 코드 최소 수정"이라고 한다. [요약만: https://forums.unrealengine.com/t/nvidia-gameworks-integration/16686/4056 및 https://toscode.mulanos.cn/mirrors/unreal-engine/blob/4.18-GameWorks/README_Flow.md]
- 언리얼 5용 Flow 플러그인은 **찾지 못함**. 엔비디아 현재 UE5 플러그인 목록(DLSS, ACE, Reflex 등)에 Flow는 없다. [요약만: https://developer.nvidia.com/unrealengine]
- 새 Flow는 Vulkan/DX12용 셰이더이고 자체 GPU 문맥(NvFlowContext)을 요구한다. 언리얼 RHI(렌더 하드웨어 인터페이스) 자원과 공유하려면 중간 다리(interop)를 직접 짜야 한다. [확인: 위 문서의 Vulkan/DX12·NvFlowContext 설명, 다리 작업량은 추정]
- 판정: **통째로 가져오는 것은 현실적이지 않다.** 가치는 "설계도"에 있다. 블록 크기 32x16x16, 활성 블록 목록, 방출기가 블록을 요청하는 방식, 최대 블록 풀 상한을 우리 RDG 컴퓨트 솔버에 맞게 재설계해 쓰는 것이 D-49에서 택한 "공개 원리로 자체 재구현"과 같은 길이다. [추정]
- 사용자 요구("남이 만든 알고리즘도 이 게임에 맞게 최적화")에 따라, Flow를 쓰든 참고하든 **탑다운 카메라(보이는 곳만), 플레이어·칼·파편 주변만 블록 배정, 먼 곳은 단순 2D 안개로 대체**하는 최적화는 직접 넣어야 한다. [추정]

## 3. 언리얼용 상용·공개 플러그인

| 이름 | 정체 | 사실 |
|---|---|---|
| Zibra Effects / Smoke & Fire | 실시간 연기·불 시뮬레이션 | **유니티 제품**. 언리얼 판은 로드맵에 있고 출시 예정 연도가 2027이다. 가격·격자 크기·ms 수치 공개 없음. 유니티 판 문서는 "다중격자 압력 풀이"를 언급하고, 사례 하나가 전체 화면 연기에 프레임당 약 2ms라고 한다(유니티, 조건 불명). [확인: https://www.zibra.ai/product-roadmap/smoke-fire-for-unreal-engine , https://zibra.ai/blog-posts/setting-new-standards-how-zibra-effects-advanced-real-time-simulations-bring-liquid-smoke-and-fire-to-life-in-gaming] |
| ZibraVDB (언리얼) | 미리 계산된 VDB를 압축해 재생 | **구운 것 재생**이지 실시간 계산이 아니다. 압축 최대 100배, 언리얼 SVT보다 2배 빠르다는 건 회사 주장. 인디(연매출 10만 달러 이하)는 무료, 256³ 초과 해상도는 유료, 가격은 월 10달러·연 150달러라는 보도(과거 29/299달러 기록과 상충). UE 5.3 이상·윈도우 전용. [요약만: https://www.cgchannel.com/2025/04/get-the-new-free-version-of-zibravdb-for-unreal-engine-and-houdini/ , https://digitalproduction.com/2025/08/19/zibra-ai-frees-zibravdb-for-indie-artists-briefly-because-less-is-more/] |
| JangaFX EmberGen | 별도 프로그램의 실시간급 연기·불 제작기 | 출력은 VDB·플립북·이미지. **공식 언리얼 플러그인은 찾지 못함**(JangaFX 포럼 답변이 "UE5는 아직 공식 지원 아님"). 가져오기는 서드파티 `unreal-vdb`(Eidos Montréal, 실험적, 크래시 보고 있음). 가격은 보도마다 상충($199.99 / $300 영구 등), 구독도 있음. VDB 내보내기에 유료 라이선스 필요. [요약만: https://forums.jangafx.com/t/embergen-to-unreal-5/1464 , https://github.com/eidosmontreal/unreal-vdb/blob/main/README.md , https://80.lv/articles/jangafx-reduces-the-price-of-embergen-for-indie-users] |
| FluidNinja LIVE 2 | 나이아가라 시뮬 단계(GPU) + 블루프린트 래퍼 | 보도 문구는 "2D 시뮬로 3D 시각을 구동"하는 작업 흐름을 언급. 지형·스플라인·본·텍스처가 입력, 입자·메시 거리장·카오스 파괴물도 입력. UE 5.6 이상, 현재 윈도우만. 개인(연매출 10만 달러 미만) 149달러 영구, 전문 2,999.99달러. 성능 수치 공개 없음(데모는 RTX 3080 중급 PC). 기사에서 "진짜 3D인지"는 명시되지 않음(우리 프로젝트의 2D 계산 방식이 이 제품을 목표로 삼음). [확인: https://www.cgchannel.com/2026/09/andras-ketzer-releases-fluidninja-live-2-for-unreal-engine/ , 우리 결정 D-49: decisions.md 356줄] |
| Fluid Flux (ImaginaryBlend) | 얕은 물(shallow water) 2D 시뮬 | 물 표면 도구이며 3D 기체 아님. 3.0 가격 349.99달러 보도. [요약만: https://digitalproduction.com/2024/11/25/fluid-flux-3-0-new-tools-forfluid-dynamics-in-unreal-engine/] |

이 표의 어디에도 "희소 격자로 실시간 3D 계산 + 언리얼 5.8 + 충돌/파편 반응"을 동시에 만족하는 제품은 없다. 본 조사에서 찾은 범위 안의 결론이며, 아직 모르는 제품이 있을 가능성은 남아 있다.

## 4. 선택지 비교 표

| 이름 | 실시간 3D 여부 | 희소 여부 | 라이선스·가격 | 이 용도 적합성 (탑다운 오픈월드, 플레이어·칼·파편 반응 상시 안개) |
|---|---|---|---|---|
| 나이아가라 플루이드 3D 기체 (엔진 내장) | 예, 진짜 3D | 아니오(밀집 텍스처) | 엔진 포함·무료, 베타 | **부분 적합(참고·시연만)**: 기능 이름은 다 있으나 밀집이라 넓은 면적 상시용으로 무겁고, 솔버가 그래프라 D-49와 충돌 |
| NVIDIA Flow (구판 1.0.1 / 새판 flow 2) | 예 | **예(32x16x16 블록)** | BSD-3 (PhysX 저장소). 구판 LICENSE는 미확인 | **부분 적합(설계 참고용)**: 구조는 정답에 가깝지만 UE5 연동 선례 없음, 직접 다리 작업 필요 |
| Zibra Smoke & Fire | 예(유니티) | 미확인 | 언리얼 판은 2027 로드맵, 가격 미공개 | **부적합**: 지금 언리얼에서 쓸 수 없음 |
| ZibraVDB | 아니오(구운 재생) | 압축된 부피(희소 취급 가능) | 인디 무료/유료 구독(보도 상충) | **부적합(상호작용 안 됨)**: 고정 연출(폭발 한 방 등)에는 보조로만 가능 |
| EmberGen | 별도 프로그램, 게임 내 실시간 아님 | 해당 없음 | 영구/구독(보도 상충) | **부적합**: 공식 언리얼 연동 없음, 결과는 구운 파일 |
| FluidNinja LIVE 2 | 2D 계산으로 3D처럼 보임(공식 문구 기준) | 해당 없음 | 149달러/2,999.99달러 | **부분 적합**: 현재 프로젝트가 닮으려는 대상이지만 진짜 3D 요구에는 해당 안 됨 |
| Fluid Flux | 아니오(2D 물) | 해당 없음 | 349.99달러(3.0 보도) | **부적합**: 물 표면 도구 |
| 자체 구현(희소 블록 + 우리 RDG 컴퓨트) | 예 | 예(설계에 따라) | 우리 코드 | **적합(유일한 규칙 합치 경로)**: Flow 구조와 나이아가라 플루이드 모듈 목록을 설계 점검표로 쓰면 됨. 단 RTX 2060 6GB에서의 비용은 아직 증명 안 됨 |

## 5. 구조 그림 (개념 모델)

```
[밀집 격자: 나이아가라 플루이드]        [희소 블록: Flow / 우리가 갈 길]
┌──────────────────────────┐          . . . . . . . . . . .
│ 전부 칸을 잡는다            │          . . . ┌──┐ . . . . .
│ 사람 없는 곳도 계산·저장    │          . . . │■■│┌──┐ . . .   ■ = 지금 필요한 동네 블록만
│ 크게 덮으면 칸이 거칠어짐   │          . . . └──┘│■■│ . . .       (플레이어·칼·파편 주변)
└──────────────────────────┘          . . . . . └──┘ . . .   . = 계산 안 함, 먼 곳은 단순 안개
```

## 6. 사용자에게 보여줄 판단 근거 요약

좋은 점
- 나이아가라 플루이드: 지금 당장 진짜 3D 기체 모양을 눈으로 볼 수 있음(시연용).
- Flow: 희소 블록이 실제로 돌아가는 증거(공개 문서)가 있고 BSD-3 허용 라이선스.
- 자체 구현: 규칙(D-49)과 맞고, 탑다운·3인칭·1인칭에 맞춰 "보이는 곳만" 최적화를 마음대로 넣을 수 있음.

나쁜 점
- 나이아가라 플루이드: 밀집·베타·그래프 솔버(규칙 충돌)·RTX 2060 수치 없음.
- Flow: 언리얼 5 선례 없음, 연동 다리 작업량 큼.
- 상용 플러그인: 구운 재생이거나 2D 기반이거나 언리얼 판이 아직 없음.
- 자체 구현: 일이 크다. 속도 수치는 만들어 재기 전까지 모른다.

## 7. 후속 조사로 남기는 것 (이번 단계 범위 밖, 미확인 항목)
1. RTX 2060에서 나이아가라 3D 기체 128³/96³ 의 실제 밀리초(시연 한 번으로 재면 됨; 사용자 승인 필요).
2. 나이아가라 플루이드 3D가 장면 거리장/GBuffer로 충돌하는지(모듈 그래프를 열어 봐야 함).
3. 구판 Flow 1.0.1의 LICENSE 본문, 새 Flow의 `flow/` 폴더 라이선스 파일.
4. Zibra 언리얼 출시 시점 변동, FluidNinja LIVE 2의 격자 차원(공식 제품 문서).

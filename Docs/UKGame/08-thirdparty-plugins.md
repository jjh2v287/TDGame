[← 인덱스로](../UKGame_FeatureFileMap.md)

# 8. 서드파티 플러그인 목록

자체 제작이 아닌 외부 플러그인입니다(.uplugin 설명 기준). 코드 수정 대상이 아니라 사용처 파악용입니다.

| 플러그인 | 제작자 | 용도 |
|---|---|---|
| LogicDriver | Recursoft | 블루프린트 그래프 기반 스테이트 머신. `Source/UKGame/StateMachine/*`의 SMState/SMTransition 클래스가 이 플러그인 위에 구현됨 |
| Hierarchacef69484091V10 (HTN Planner) | Maks Maisak | 비헤이비어 트리 대안인 HTN(Hierarchical Task Network) 플래너. `Source/UKGame/HTN/*`의 기반 |
| GASCompanion | Mickael Daniel | 게임플레이 어빌리티 시스템(GAS) 기반 템플릿. `Source/UKGame/AbilitySystem/*`가 확장 |
| GameplayMessageRouter | Epic Games | 느슨하게 결합된 게임플레이 메시지 버스 (`UKGameplayMessagePayload`에서 사용) |
| Ultimatea2c443fee334V17 | Aurora Devs | 3인칭 동적 카메라 시스템 |
| uRecoil | Hoax Games | 반동 시스템 |
| RTune | Kallisto | 차량 물리 (`UKVehicle`와 연관 추정) |
| MultiplayerClimbingSystem | Artem Chaika | 클라이밍 시스템 참고/의존 (추정) |
| KawaiiPhysics | pafuhana1213 | 머리카락·의상 흔들림 보조 물리 애님 노드 |
| VertexAnimationManager | Thomas Schneider | 버텍스 애니메이션 텍스처 관리 |
| NiagaraUIRenderer | Michal Smoleň | 나이아가라 파티클을 UI 위젯으로 렌더링 |
| UnrealImGUI | - | 인게임 ImGui (샷컷 디버그 툴, `UI/ImGUI/*`에서 사용) |
| KongRapidJson | Kong Studios | RapidJSON 래퍼 (UKGameDataManager가 사용) |
| RuntimeDataTable | Jared Therriault | CSV/구글 시트를 런타임에 구조체로 로드 |
| FastNoiseGenerator | Víctor Hernández | 노이즈 생성 라이브러리 |
| HoudiniEngine / HoudiniNiagara | SideFX | 후디니 엔진 연동, 후디니 포인트 클라우드 → 나이아가라 |
| SideFX_Labs | SideFX | 후디니 랩 에셋·유틸리티 |
| GraphNUnrealPlugin | Polygonflow | Dash(에셋 배치 도구) 연동 |
| WorldCreatorBridge | BiteTheBytes | World Creator 지형 연동 |
| ImpostorBaker | Ryan Brucks | 원거리 LOD용 임포스터 생성 |
| RMAFoliageTools | RMA | 폴리지 관리 도구 |
| BlockoutToolsPlugin | Dmitry Karpukhin | 레벨 블록아웃 도구 |
| SnappingHelper | UsefulCode | 액터 트랜스폼 스냅 도구 |
| ShapesVisualizer | rionix | 와이어프레임 도형 시각화 컴포넌트 |
| ElectronicNodes | Hugo Attal | 블루프린트·머티리얼 와이어 스타일 개선 |
| rdBPtools | Recourse Design | 블루프린트 작업 보조 도구 |
| MayaLiveLink | Autodesk | 마야 라이브 링크 |
| Hdr10PlusStandard | Samsung | HDR10+ 출력 지원 |
| Runtime (Substance) | Adobe | Substance 3D 머티리얼 플러그인 (`Plugins/Runtime/Substance`) |
| Dropper | Mark Webb | 물리 기반 에셋 뿌리기 에디터 모드 |
| LevelStreamingOptimizer | UsefulCode | 레벨 스트리밍 예산 설정 도구 |
| RegionalParameterTool | Kong Studios | 지역별 파라미터 볼륨 베이크 (7절에 상세) |
| GameFeatures | - | 게임 피처 플러그인 컨테이너 (`Source/UKGame/GameFeatures/*` 액션이 사용) |

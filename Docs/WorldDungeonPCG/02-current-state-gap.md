[← 인덱스로](../WorldDungeonPCG_Plan.md)

# 2. 현재 프로젝트 상태와 설계서의 차이

조사일 2026-09-09. TDGame 저장소를 직접 확인한 결과다.

## 2.1 지금 있는 것

| 영역 | 상태 | 근거 |
|---|---|---|
| 엔진·모듈 | UE 5.8, 런타임 모듈 `TDGame` 하나. 에디터 모듈 없음 | `TDGame.uproject` Modules, `Source/TDGame/TDGame.Build.cs` |
| 플러그인 | GameplayAbilities, ModelContextProtocol, AllToolsets, ModelingToolsEditorMode, StateTree, GameplayStateTree | `TDGame.uproject` |
| 전투 | GAS 기반 전투 캐릭터(`ATDCombatCharacter` 계열), 데미지 서브시스템·정의·엔티티, 상태이상, 근접 노티파이 스테이트, 자동화 테스트 28개 | `Source/TDGame/Combat/*`, `Docs/TDDamageSystemDesign.md`, `Docs/TDGASFoundation.md` |
| 플레이어 | `ATDGameCharacter`, `ATDGamePlayerController`(Exec 콘솔 명령 다수), `ATDGameGameMode`, 블루프린트 `BP_TDCombat*` | `Source/TDGame/*.h`, `Content/Combat/Blueprints` |
| 맵 | `Content/Level/LV-Game.umap`이 시작 맵. **World Partition이 아님**(외부 액터 폴더 없음, umap에 WorldPartition 문자열 0건). 템플릿 맵(TopDown, Variant_*)만 외부 액터(OFPA) 사용 | `Content/__ExternalActors__/`, `Config/DefaultEngine.ini` |
| 템플릿 잔재 | `Variant_Strategy`, `Variant_TwinStick` 소스·콘텐츠가 남아 있음(TD 접두어 없음) | `Source/TDGame/Variant_*` |
| 내비게이션 | 기본 RecastNavMesh 설정(타일 1000UU). 월드 파티션 내비 파티셔닝 설정 없음 | `Config/DefaultEngine.ini` |
| 도구 | 언리얼 공식 MCP로 에디터 조작(액터 배치, 프로퍼티, PIE, 자동화 테스트). 몽타주·콘솔·애니메이션 작업은 에디터 Python 원격 실행으로 보완 | `AGENTS.md` 11절, 메모리 |
| 문서 | 데미지 시스템 설계·가이드, GAS 기반, UKGame 기능 맵(참고용) | `Docs/` |
| 할 일 관리 | 없음 | — |

## 2.2 설계서가 요구하지만 없는 것 (차이 목록)

| ID | 필요한 것 | 현재 | 차이 크기 |
|---|---|---|---|
| G-01 | World Partition이 켜진 메인 월드와 Enable Streaming 확인 (R-10, R-11) | 없음. LV-Game은 일반 레벨 | 큼 |
| G-02 | 플레이어 외 목적지 선로딩 스트리밍 소스 (R-14, R-60) | 없음 | 큼 |
| G-03 | 던전 입구 액터와 전환 상태 머신 (R-61, R-62) | 없음 | 큼 |
| G-04 | 던전 아틀라스 슬롯 데이터와 배치 규칙 (R-50, R-51) | 없음 | 중간 |
| G-05 | 룸 모듈 규격과 레벨 인스턴스 제작 규칙 (R-41, R-70) | 없음. 룸 에셋도 없음 | 큼(아트 포함) |
| G-06 | 흐름 그래프 생성기 + 조립기 + 던전 검증기 (R-40, R-90) | 없음 | 큼 |
| G-07 | 월드 정의·지역·바이옴 데이터 에셋 (R-22, R-31) | 없음 | 중간 |
| G-08 | PCG 플러그인 활성화와 바이옴/도로/POI 그래프 (R-30~R-33) | PCG 플러그인 꺼져 있음 | 큼 |
| G-09 | 월드 그래프·도로·POI 생성기 + 월드 검증기 (R-21, R-91) | 없음 | 큼 |
| G-10 | 영속 상태 계층(안정 ID, 던전·월드 상태 서브시스템, 세이브) (R-80~R-82) | 없음. 세이브 시스템 자체가 없음 | 큼 |
| G-11 | 데이터 레이어 기반 퀘스트 상태 전환 (R-71) | 없음 | 중간 |
| G-12 | 에디터 툴(생성·검증·베이크 UI, 리포트 클릭 이동, 잠금, 버전) (R-100, R-101) | 에디터 모듈 자체가 없음 | 큼 |
| G-13 | 시드·생성기 버전 관리 (R-82, R-101) | 없음 | 작음(설계만 먼저) |
| G-14 | 검증기 회귀 테스트·배치 실행(커맨드릿) (R-92) | 전투 테스트 패턴만 존재 | 중간 |
| G-15 | 내비메시 파티셔닝·HLOD·프로파일링 절차 (R-111 Phase 4) | 없음 | 중간(후반) |
| G-16 | 할 일 목록과 에이전트 작업 절차 | 없음 | 이 문서 묶음이 채움 |

## 2.3 재사용할 수 있는 기존 자산

- 자동화 테스트 픽스처(`Source/TDGame/Combat/Tests`의 스코프 월드 구조체 패턴)와 실행 명령(`Docs`·메모리) → 생성기·검증기 테스트에 그대로 적용.
- `ATDGamePlayerController`의 Exec 명령 패턴 → 던전 진입·시드 재생성 디버그 명령에 재사용.
- MCP + 에디터 Python 절차 → 월드 파티션 맵 생성, 액터 배치, 데이터 에셋 값 설정, PIE 검증을 에이전트가 수행.
- 데미지 시스템의 "데이터 정의 에셋 + 서브시스템 실행" 구조 → 던전·바이옴 정의 에셋 설계의 선례.

## 2.4 정리해야 할 것 (선행 정리)

- 템플릿 잔재 `Variant_Strategy`, `Variant_TwinStick`: 새 월드 작업과 무관하고 이름 규칙(TD 접두어)에도 어긋난다. 삭제 또는 보존 여부는 사용자 결정 사항이므로 할 일 목록에 "결정 요청" 항목으로만 둔다.
- 시작 맵을 새 World Partition 월드로 바꿀지, LV-Game을 유지하고 별도 월드에서 개발할지 결정 필요. 권장: 새 월드 `L_TDWorld_Main`을 만들고 LV-Game은 전투 테스트용으로 유지.

## 2.5 지금 당장 필요한 것 (우선순위 순)

1. 에디터 전용 모듈(`TDGameEditor`) 신설과 PCG·관련 플러그인 활성화. 이후 모든 툴·검증기·커맨드릿의 자리다.
2. World Partition 메인 월드 생성과 스트리밍 설정 확인(Phase 1의 전제).
3. 던전 입구 액터 + 목적지 스트리밍 소스 + 전환 상태 머신 + 왕복 프로토타입(설계서가 "가장 먼저"로 못 박은 항목).
4. 안정 ID와 영속 상태 계층의 최소 구현(입구·상자 하나로 검증). 스트리밍 프로토타입과 함께 검증해야 나중에 구조를 바꾸지 않는다.
5. 룸 모듈 규격(출입구 소켓·그리드 단위) 문서화. 아트 제작이 이 규격에 묶이므로 코드보다 먼저 확정한다.

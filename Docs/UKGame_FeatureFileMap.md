# UKGame 기능 → 코드 파일 맵

- 대상: `C:\Project\UKGame` (언리얼 엔진 C++ 프로젝트, 게임 모듈 `UKGame` + 에디터 모듈 `UKEditor` + 플러그인 60여 개)
- 작성일: 2026-09-08. 헤더 파일 이름·클래스 선언·주석을 기준으로 자동 분석한 결과이며, "(추정)" 표시는 코드 본문까지 확인하지 않은 항목입니다.
- 경로 표기: `Foo.h/.cpp`는 같은 디렉터리의 헤더와 소스 쌍을 뜻합니다. 파일이 많은 묶음은 대표 파일만 적고 "외 N개(디렉터리)"로 줄였습니다.
- 빠른 찾기: 에디터에서 `Ctrl+F`로 기능 이름(예: "심리스 던전", "클라이밍", "가챠", "락온")을 검색하세요.

## 문서 구성

이 파일은 인덱스이며 본문은 `Docs/UKGame/` 아래 절별 파일에 있습니다.

| 절 | 파일 | 내용 |
|---|---|---|
| 1 | [월드 · 스트리밍 · 씬 흐름](UKGame/01-world-streaming.md) | 심리스 던전 로딩, 데이터 레이어, 씬 흐름, 월드 시간, 세이브, 컷신 |
| 2 | [어빌리티 · 전투 · 이동 액션](UKGame/02-ability-combat.md) | GAS 코어, 데미지 계산, 클라이밍, 파쿠르, 스킬 오브젝트, 공격 토큰 |
| 3 | [AI · NPC · 스테이트 머신](UKGame/03-ai-npc.md) | 비헤이비어 트리, 스테이트 트리, HTN, 스마트오브젝트, 보스 페이즈, 퀘스트 상태 머신 |
| 4 | [액터 · 컴포넌트 · 애니메이션 · 서브시스템](UKGame/04-actors-components-animation.md) | 상호작용 오브젝트, 트리거 볼륨, 카메라, 애님 노티파이, ECS, 사운드 |
| 5 | [게임 프레임워크 · 네트워크 · 데이터](UKGame/05-framework-network-data.md) | 서버 프록시, 유저 데이터, 데이터테이블, 입력, 서브시스템 목록 |
| 6 | [UI](UKGame/06-ui.md) | 베이스 위젯, 뷰모델, 팝업, HUD, 화면별 기능 |
| 7 | [에디터 모듈(UKEditor) · 자체 플러그인](UKGame/07-editor-plugins.md) | UKEditor 모듈, ZzAction, UKStoryGraph, UKTimeOfDay, 파이프라인 도구 |
| 8 | [서드파티 플러그인 목록](UKGame/08-thirdparty-plugins.md) | 외부 플러그인 제작자·용도·의존 관계 |
| 9 | [상세 흐름: 심리스 던전 로딩](UKGame/09-flow-seamless-dungeon-loading.md) | 통로 액터, 스트리밍 소스 프로바이더, 상태 머신, 반경 축소 공식, 위험 요소 |
| 10 | [상세 흐름: 부서지는 오브젝트 (Breakable · 폴리지 파손 · 생활 오브젝트)](UKGame/10-flow-breakable-objects.md) | 피격 판정 컴포넌트, 카오스 파괴, 폴리지 승격, 생활 오브젝트, 위험 요소 |
| 11 | [애니메이션 · 캐릭터 이동 최적화](UKGame/11-animation-movement-optimization.md) | 중요도(Significance)/URO, 군중 틱 예산(Budgeter), 이동 시 본 피직스바디 갱신 차단(SkipAllBones), 무브먼트 캐싱, HPA*, ECS, VAT |

## 상세 흐름 문서

- [심리스 던전 로딩](UKGame/09-flow-seamless-dungeon-loading.md) — 코드 본문 기준 단계별 흐름과 위험 요소
- [부서지는 오브젝트](UKGame/10-flow-breakable-objects.md) — 카오스 파괴, 폴리지 파손, 생활 오브젝트
- [애니메이션 · 캐릭터 이동 최적화](UKGame/11-animation-movement-optimization.md) — 중요도 틱/URO, 군중 틱 예산, 이동 시 본 피직스바디 갱신 차단, 지면 캐싱, HPA*, ECS 군집 이동, VAT

## 유지 방법

- 새 상세 흐름은 `Docs/UKGame/NN-flow-<주제>.md`로 추가하고 이 인덱스 표와 상세 흐름 목록에 한 줄씩 넣습니다.
- 각 절 파일 맨 위의 `[← 인덱스로]` 링크는 이 파일을 가리킵니다.

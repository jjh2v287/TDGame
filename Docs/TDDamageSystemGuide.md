# TD 데미지 시스템 사용 안내

## 바로 실행하기

UE 5.8에서 `TDGame.uproject`를 열고 `LV-Game`을 Play한다. 기본 게임 모드는 `BP_TDCombatGameMode`이며 `BP_TDCombatCharacter`와 `BP_TDCombatController`가 C++ 전투 클래스를 상속한다. 원래의 `BP_TopDownCharacter`와 `BP_TopDownController`는 이 C++ 클래스를 상속하지 않으므로 전투용 설정과 구분한다.

현재 코드는 GAS로 전환했으며, 이번 변경의 에디터 테스트는 사용자의 요청에 따라 보류했다. 기존 플레이 기록은 전환 전 결과다. GAS 구성과 제약은 [GAS 기반 문서](TDGASFoundation.md)를 먼저 확인한다.

콘솔에서 `TDSpawnDamageTargets`를 실행하면 커서 근처에 테스트 표적 5개가 생성된다. 커서를 월드 바닥이나 표적에 놓고 다음 키로 시전한다. 기존 좌클릭 이동은 유지된다.

이동은 마우스와 키보드를 함께 지원한다.

- `W/A/S/D`는 화면 기준 위/왼쪽/아래/오른쪽으로 이동한다. 카메라의 수평 회전을 기준으로 계산하며 대각선 이동 속도는 직선 이동과 같다.
- 키보드 이동 중 캐릭터는 마우스 커서를 바라본다. 예를 들어 커서가 오른쪽에 있을 때 `A`를 누르면 오른쪽을 바라본 채 왼쪽으로 후진한다.
- 키보드를 놓아도 커서 바라보기는 유지한다. 새 마우스 클릭이나 터치 이동을 시작하면 다시 이동 방향을 바라본다.
- 키보드 이동은 진행 중인 클릭 자동 이동을 취소한다. 키보드와 마우스를 동시에 누르면 키보드를 우선하고, 중단된 마우스 제스처의 나중 릴리스가 자동 이동을 다시 시작하지 않게 한다.
- 포커스 상실, 빙결, 사망, 이동 입력 차단 시 남은 이동 명령을 정리한다. 별도 입력 에셋 수정 없이 C++에서 Enhanced Input 매핑을 구성한다.

이 변경은 이동 방향과 캐릭터 회전을 분리한다. 애니메이션 에셋은 기존 설정을 사용한다. 실제 키 입력·커서·포커스 전환 동작은 후속 에디터 검증 대상이다.

| 키 | 예제 | 동작 |
| --- | --- | --- |
| 1 | Fireball | 적중 피해 후 소멸 위치에 화염 장판을 생성하고 반복 피해 |
| 2 | Blizzard | 범위 상공에서 얼음 투사체 낙하, 실제 받은 냉기 피해 누적으로 빙결, 해제 시 추가 피해 |
| 3 | Mine | 0.8초 준비 후 적 감지, 폭발 범위 피해 |
| 4 | Shockwave | 중심이 빈 띠를 확장하면서 대상별 한 번 피해 |
| 5 | Meteor | 상공에서 운석 낙하, 충돌 후 폭발 피해 |
| 6 | DelayedHoming | 1초 후 호밍 시작, 2초에 중지, 3초에 더 빠른 회전 속도로 재적용 |

표적에는 레벨, 체력과 FROZEN/DEFEATED 상태가 표시된다. `TDSetCasterLevel 10`으로 시전자 레벨을 바꿔 다음 시전의 위력을 확인한다. 이 명령은 체력을 회복시키지 않는다. 이미 시전한 효과에는 이전 능력치가 유지된다.

시각 표현은 개발용 도형과 표적 메시다. 에셋의 `VisualEffect`, `Mesh`, `Material`, `VisualScale`에 실제 연출 에셋을 연결할 수 있다. 디버그 도형은 배포용 시각 효과를 대신하지 않는다.

## 편집할 에셋

`Content/Combat/Examples`에 편집 가능한 DataAsset 12개가 있다.

- 주문 시작점: `DA_TDFireball`, `DA_TDBlizzard`, `DA_TDMine`, `DA_TDShockwave`, `DA_TDMeteor`, `DA_TDDelayedHoming`.
- 후속 엔티티: `DA_TDFlameField`, `DA_TDIceShard`, `DA_TDMineExplosion`, `DA_TDFallingMeteor`, `DA_TDMeteorExplosion`.
- 상태이상: `DA_TDFrostFreeze`.

플레이어의 `Combat / DamageSpells` 배열에 직접 지정하면 그 구성을 사용한다. 배열이 비어 있으면 위 6개 시작점 에셋을 불러온다. 저장된 예제 세트가 없으면 같은 그래프의 C++ 기본 설정을 메모리에 생성한다.

기본 예제 디렉터리는 패키징의 Always Cook 대상으로 등록했다. 다른 위치에 만든 주문 에셋은 캐릭터의 `DamageSpells` 같은 저장되는 프로퍼티에서 참조하거나 프로젝트의 쿠킹 규칙에 등록해야 한다.

## 새로운 공격 구성

1. `UTDDamageDefinition` 클래스의 DataAsset을 생성한다.
2. `Mode`에 Projectile, Area, Mine, Shockwave 중 하나를 지정한다.
3. 수명, 준비 시간, 반경, 높이, 속도, 적중 횟수/간격을 설정한다.
4. `Rules`에 이벤트를 추가하고, 각 이벤트의 `Actions`에 실행할 동작을 배열 순서대로 넣는다.
5. 다른 공격을 생성하는 액션에는 `Entity`로 후속 DataAsset을 연결한다.
6. 시전할 시작점 에셋을 캐릭터의 `DamageSpells`에 연결한다.

피해 공식은 `Magnitude`의 Base, PerLevel, AttackRatio, SpellRatio로 정한다. 지속 장판은 `MaxHitsPerTarget = 0`과 `HitInterval`, `PulseInterval`을 함께 설정한다. 반경 판정은 대상 액터의 중심을 기준으로 한다.

`SpawnAnchor`는 이벤트 위치, 대상 위치 또는 최초 시전 목표 위치다. `SpawnOffset`은 월드 좌표 오프셋이며 `ScatterRadius`는 수평 원판 안의 무작위 산포다. 공중 낙하는 양수 Z 오프셋과 `SpawnDirection = Down`으로 구성한다.

에셋 유효성 검사는 잘못된 수명, 반경, 누락된 후속 에셋, 잘못된 호밍 설정 등을 거부한다. 실행 시에도 정의를 확인한다.

## 지연 호밍 설정

`Mode = Projectile`인 정의에서 액션 `Type = ApplyHoming`과 `DelaySeconds`를 설정한다.

| 항목 | 설정 예 | 의미 |
| --- | --- | --- |
| Event | Spawn | 실제 생성 시점을 기준으로 예약 |
| DelaySeconds | 1.0 | 이벤트 발생 1초 후 적용 |
| Homing.TargetSelection | NearestEnemy | 현재 엔티티의 진영 필터를 통과하는 가장 가까운 대상 선택 |
| Homing.SearchRadius | 1200 | 목표 획득 시 3차원 탐색 반경 |
| Homing.TurnRateDegreesPerSecond | 180 | 초당 회전 상한 |
| Homing.RetargetInterval | 0.2 | 목표를 잃었을 때 재탐색 간격 |
| Homing.TargetLossPolicy | Reacquire | 목표 소멸 시 다른 대상 탐색 |
| Homing.bRetargetOnApply | true | 재적용 시 목표도 새로 선택 |

`TargetSelection = EventTarget`은 Hit 등의 이벤트가 전달한 대상을 선택한다. 전달 대상이 없거나 유효하지 않으면 Reacquire 정책에서 가까운 대상을 찾고, ContinueStraight 정책에서는 직진한다. 두 선택 방식 모두 진영 조건과 탐색 반경을 검사한다.

`StopHoming`은 이동을 멈추지 않고 마지막 방향으로 직진하게 한다. 이후에 예약한 ApplyHoming은 실행된다. 엔티티 자체가 끝나면 남아 있는 모든 지연 액션이 취소된다.

같은 시각의 액션은 배열에서 예약된 순서를 따른다. 수명 종료 시각과 같은 시각의 예약은 실행되지 않는다. End/Expire에는 지연을 지정할 수 없다. 종료 후 기다리는 연계가 필요하면 자식 엔티티를 즉시 생성하고 그 자식에 준비 시간이나 지연 액션을 설정한다.

런타임 C++ 진입점은 `ATDDamageEntity::ApplyHoming(Settings, EventTarget)`과 `StopHoming()`이다. 설정 교체와 중지는 데이터 액션도 동일한 함수를 사용한다. 상태는 `IsHoming()`, `GetHomingTarget()`, `GetTravelDirection()`으로 확인할 수 있다.

## 피해 대상 연결

실제 적의 C++ 생성자에 `UTDCombatComponent`를 기본 컴포넌트로 추가하고 능력치와 진영을 설정한다. 등록된 컴포넌트만 프레임워크의 대상이 된다.

| C++ 진입점 | 역할 |
| --- | --- |
| `GetStats()` / `SetStats(Stats, bResetHealth)` | 현재 GAS 수치 스냅샷 / 초기 성장 설정 갱신. 기본 SetStats는 체력과 소유 상태를 리셋한다. |
| `SetCombatLevel(Level)` | 초기 성장 계수를 보존하면서 GAS 기본 수치와 어빌리티 레벨 갱신 |
| `ReceiveDamage(...)` | 계산된 피해에 대상 방어를 적용하고 체력을 변경 |
| `OnDamaged` | 실제 피해량과 시전자 문맥 알림 |
| `OnDeath` | 사망 처리, 드롭, 점수 등 외부 C++ 시스템 연결 |
| `OnFreezeChanged` | 빙결 표시 등 외부 C++ 시스템 연결 |
| `UTDDamageSubsystem::Cast(...)` | 시전자 스냅샷을 만들고 첫 엔티티 생성 |

서브시스템의 Cast는 생성용 저수준 진입점이다. 실제 시전은 `ATDCombatCharacter::CastDamageSpell` 또는 ASC의 `TryCastDamageDefinition`을 통해 `UTDDamageGameplayAbility`로 전달한다. 사거리와 정의별 재사용 대기시간은 이 GAS 어빌리티에서 적용한다.

프레임워크는 Unreal의 일반 `TakeDamage`나 별도 TwinStick 템플릿의 즉사 함수를 자동으로 연결하지 않는다. 기존 적을 편입할 때는 컴포넌트의 OnDeath를 해당 적의 C++ 사망 처리에 연결한다.

## 근접 물리 공격 노티파이

`UTDAnimNotifyState_MeleeAttack`(`Source/TDGame/Combat/AnimNotify/`)은 공격 몽타주의 노티파이 트랙에 배치하는 노티파이 스테이트다. 렌더링된 포즈가 아니라 애니메이션 원본 데이터를 `SampleIntervalSeconds`(기본 1/60초) 간격으로 다시 샘플링하므로, 프레임이 길어져도 무기 궤적을 빠짐없이 스윕한다. 이전 샘플과 현재 샘플 사이는 액터 이동까지 보간해서 스피어 스윕한다.

| 프로퍼티 | 역할 |
| --- | --- |
| `WeaponBaseSocketName` / `WeaponTipSocketName` | 무기 손잡이와 날 끝 소켓(또는 본). 끝 소켓이 없으면 한 점만 스윕한다. |
| `BladeSampleCount`, `SweepRadius`, `SweepChannel` | 날 위의 샘플 점 개수, 스윕 반경, 콜리전 채널(기본 Pawn) |
| `MontageSlotName` | 포즈를 읽을 몽타주 슬롯. 비우면 첫 슬롯을 사용한다. |
| `TargetPolicy`, `HitRules` | 진영 필터와 적중 시 실행할 규칙. `UTDDamageSubsystem::ExecuteRules`로 Hit 이벤트를 실행한다. |
| `bDrawDebugSweep`, `DebugDrawDuration` | 스윕 경로와 적중점 디버그 표시 |

한 노티파이 구간 동안 같은 대상은 한 번만 적중한다. 시전자는 `UTDCombatComponent`가 있어야 하며, 시전 시점의 능력치 스냅샷으로 컨텍스트를 만든다. 엔티티가 없으므로 `HitRules`의 지연 액션은 실행되지 않는다. 몽타주 슬롯 안의 시퀀스에 배치하면 시간 좌표가 어긋나므로 몽타주 노티파이 트랙에 직접 배치한다. 미러 테이블이 적용된 재생은 반영하지 않는다.

## 상태이상 확장

`UTDStatusDefinition`에 지속 시간과 주기, 누적 임계값, 갱신 정책을 설정한다.

- Damage 액션의 `Status`는 방어와 체력을 반영한 실제 적용 피해를 누적한다.
- ApplyStatus 액션은 `Magnitude`로 계산한 값을 누적한다.
- 임계값 0은 즉시 발동한다. 양수는 해당 값까지 누적한다.
- 상태의 Spawn은 발동, Pulse는 주기 효과, Expire는 자연 만료 시점이다.
- 상태에 Pulse/Damage를 추가하면 대상에 붙어서 유지되는 도트 피해를 구성할 수 있다.
- 사망이나 명시적 스탯 리셋으로 제거된 상태는 자연 만료 연계를 실행하지 않는다.

## 에셋 생성과 검증

`TDDamageExamples` 명령렛은 C++ 기본 설정에서 예제 에셋을 저장한다. 기존 목적지 에셋이 하나라도 있으면 저장 전에 중단하므로 편집한 값을 덮어쓰지 않는다.

- 생성 인자: `-run=TDDamageExamples -unattended -nop4 -NullRHI`
- 저장된 에셋 검증 인자: `-run=TDDamageExamples -ValidateOnly -unattended -nop4 -NullRHI`
- 자동화 테스트 인자: `-ExecCmds="Automation RunTests TDGame.Combat" -TestExit="Automation Test Queue Empty" -unattended -NullRHI`

위 인자는 UE 5.8의 `UnrealEditor-Cmd.exe` 뒤에 `TDGame.uproject` 경로와 함께 전달한다. 테스트 보고서는 `-ReportOutputPath`로 별도 지정할 수 있다.

설계의 수치 정책, 수명, 이벤트 순서와 제한은 [설계 문서](TDDamageSystemDesign.md)에 정리되어 있다.

# TD 데미지 시스템 사용 안내

## 바로 실행하기

UE 5.8에서 `/Game/Combat/Maps/LV_TDMegaMagicArena`를 열고 Play한다. 표적 6개와 이동 가능한 시험장이 준비되어 있다. 기본 게임 모드는 `BP_TDCombatGameMode`이며 `BP_TDCombatCharacter`와 `BP_TDCombatController`가 C++ 전투 클래스를 상속한다. 원래의 `BP_TopDownCharacter`와 `BP_TopDownController`는 이 C++ 클래스를 상속하지 않으므로 전투용 설정과 구분한다.

주문 시전은 GAS를 거치며, 피해·재사용 대기시간·진영 필터·후속 공격은 C++에서 처리한다. GAS 구성과 제약은 [GAS 기반 문서](TDGASFoundation.md)를 참고한다. 현재 시스템은 로컬 게임용이며 네트워크 복제는 구현되어 있지 않다.

2026-09-24 검증: 전투 자동화 37개와 실제 PIE 주문 10종 통과. [검증 결과와 화면](Validation/combat/megamagic-validation-2026-09-24.md)을 참고한다.

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
| 7 | ThunderCage / 뇌전 결계 | 0.45초 예고 후 전기 장판 반복 피해, 3.5초에 마지막 폭발 |
| 8 | VenomBloom / 맹독 개화 | 0.7초 준비 후 적을 감지하는 마법 독 함정, 발동 뒤 4초간 피해 장판 |
| 9 | AstralLances / 성운 창 | 3개의 마법 창을 시간차로 낙하시켜 가까운 적 추적, 적중 시 소형 폭발 |
| 0 | PhoenixDive / 불사조 강하 | 예고 뒤 불덩이 낙하, 폭발·확장 불꽃 고리·화염 장판 연계 |

표적에는 레벨, 체력과 FROZEN/DEFEATED 상태가 표시된다. `TDSetCasterLevel 10`으로 시전자 레벨을 바꿔 다음 시전의 위력을 확인한다. 이 명령은 체력을 회복시키지 않는다. 이미 시전한 효과에는 이전 능력치가 유지된다.

10종 기본 주문은 `MegaMagicVFXBundle`의 Niagara 이펙트를 사용하며 디버그 도형은 꺼져 있다. 번들에는 얼음 전용 이펙트가 없어 눈보라는 차가운 색의 마법장과 물빛 투사체로 표현한다. 뇌전·맹독·성운의 피해 속성은 현행 속성 체계의 `Arcane`이다.

`VisualOffset`, `VisualRotation`, `VisualScale`로 표현을 조정한다. `VisualTailSeconds`는 피해가 종료된 후의 잔상 시간(최대 10초)이며 피해 수명을 늘리지 않는다. `bShowDuringActivationDelay`는 준비 중 효과를 표시하고, `bScaleVisualWithRadius`는 확장 충격파의 XY 크기를 판정 반경에 맞춰 바꾼다. `bProjectToGround`는 비투사체의 중심을 아래 지면으로 보정해 적중 후 장판이 공중에 남는 것을 방지한다.

## 편집할 에셋

실전용 DataAsset은 `Content/Combat/MegaMagic`에 있다. 기존 `Content/Combat/Examples`의 개발 예제 12개도 보존한다.

- 주문 시작점: `DA_TDFireball`, `DA_TDBlizzard`, `DA_TDMine`, `DA_TDShockwave`, `DA_TDMeteor`, `DA_TDDelayedHoming`.
- 새 주문 시작점: `DA_TDThunderCage`, `DA_TDVenomBloom`, `DA_TDAstralLances`, `DA_TDPhoenixDive`.
- 후속 엔티티: `DA_TDFlameField`, `DA_TDIceShard`, `DA_TDMineExplosion`, `DA_TDFallingMeteor`, `DA_TDMeteorExplosion`.
- 상태이상: `DA_TDFrostFreeze`.

플레이어의 `Combat / DamageSpells` 배열에 직접 지정하면 그 구성을 사용한다. 배열이 비어 있으면 MegaMagic 시작점 10개를 키 순서대로 불러온다. 저장된 세트가 없으면 같은 그래프의 C++ 기본 설정을 메모리에 생성한다.

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

## 근접 물리 공격 시간표

근접 판정은 애님 노티파이가 아니라 C++ 행동 시간표로 처리한다. 전투 행동과 반응 정의(`FTDCombatActionDefinition`, `FTDCombatReactionDefinition`)의 `Action` 필드는 `FTDActionAnimation`(`Source/TDGame/Combat/Skills/TDCombatActionTypes.h`)이며, 재생할 `UAnimSequence`와 애니메이션 시간(초) 기준의 창 목록을 가진다. 능력(`UTDCombatActionAbility`, `UTDReactionAbility`)은 `UTDAbilityTask_PlayActionTimeline`을 띄우고, 태스크가 `UTDCharacterAnimationComponent::PlayAction`으로 시퀀스를 재생한 뒤 매 틱 애니메이션 컴포넌트의 이전·현재 애니메이션 시간으로 창을 연다·닫는다.

| 필드 | 역할 |
| --- | --- |
| `Animation`, `StartSeconds`, `EndSeconds`, `PlayRate` | 재생할 시퀀스와 구간(`EndSeconds` 0이면 끝까지), 재생 속도 |
| `bUseRootMotion` | 켜면 시퀀스 루트 모션을 Mover 레이어드 무브로 캐릭터 이동에 반영 |
| `HitWindows` | 창마다 `FTDMeleeSweepSettings`로 무기 궤적 스윕(`FTDMeleeSweep`) |
| `TagWindows` | 창 안에 있는 동안 소유자 ASC에 루즈 게임플레이 태그 추가 |
| `InputBufferWindow` | 창 안에서 `UTDSkillComponent`의 전투 입력 버퍼 열기 |
| `JumpCapsuleWindow` | 창 안에서 `UTDCapsuleModifierComponent` 점프 캡슐 보정 켜기 |

`FTDMeleeSweep`(`Source/TDGame/Combat/TDMeleeSweep.h`)은 렌더링된 포즈가 아니라 시퀀스 원본 데이터를 `SampleIntervalSeconds`(기본 1/60초) 간격으로 다시 샘플링하므로, 프레임이 길어져도 무기 궤적을 빠짐없이 스윕한다. 이전 샘플과 현재 샘플 사이는 액터 이동까지 보간해서 스피어 스윕한다. 한 프레임 안에 창 전체를 건너뛰어도 그 프레임에서 창을 시작하고 끝까지 스윕한다.

| `FTDMeleeSweepSettings` 필드 | 역할 |
| --- | --- |
| `WeaponBaseSocketName` / `WeaponTipSocketName` | 무기 손잡이와 날 끝 소켓(또는 본). 끝 소켓이 없으면 한 점만 스윕한다. |
| `BladeSampleCount`, `SweepRadius`, `SweepChannel` | 날 위의 샘플 점 개수, 스윕 반경, 콜리전 채널(기본 Pawn) |
| `bLockHeightToOwner`, `LockedHeightOffset` | 날 점 높이를 소유자 높이+오프셋으로 고정해 탑다운 평면에서 판정 |
| `TargetPolicy`, `HitRules` | 진영 필터와 적중 시 실행할 규칙. `UTDDamageSubsystem::ExecuteRules`로 Hit 이벤트를 실행한다. |
| `bDrawDebugSweep`, `DebugDrawDuration` | 스윕 경로와 적중점 디버그 표시 |

한 타격 창 동안 같은 대상은 한 번만 적중한다. 시전자는 `UTDCombatComponent`가 있어야 하며, 능력 발동 시점의 능력치 스냅샷에 창 시작 시점의 위치·방향과 창 전용 연쇄 예산을 더해 컨텍스트를 만든다. 엔티티가 없으므로 `HitRules`의 지연 액션은 실행되지 않는다. 루트 본은 UAF 시퀀스 플레이어와 같이 루트 모션을 추출한 상태(시퀀스의 루트 모션이 켜져 있으면 루트 고정)로 샘플링한다. 행동이 끝나거나 중단되면 열린 창을 모두 닫는다. 미러 테이블이 적용된 재생은 반영하지 않는다.

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
- MegaMagic 생성/검증: 위 인자에 `-MegaMagic`을 추가한다. 별도 경로에 생성하며 기존 목적지가 있으면 덮어쓰지 않는다.
- 자동화 테스트 인자: `-ExecCmds="Automation RunTests TDGame.Combat" -TestExit="Automation Test Queue Empty" -unattended -NullRHI`

시험장 생성은 `python Tools/run_in_editor.py Tools/Damage/editor_make_megamagic_arena.py`, PIE 자동 검사는 시험장을 Play한 상태에서 `python Tools/run_in_editor.py Tools/Damage/editor_validate_megamagic_pie.py`로 실행한다. PIE 검사 결과는 `Saved/Damage/megamagic-pie.json`에 기록된다.

위 인자는 UE 5.8의 `UnrealEditor-Cmd.exe` 뒤에 `TDGame.uproject` 경로와 함께 전달한다. 테스트 보고서는 `-ReportOutputPath`로 별도 지정할 수 있다.

설계의 수치 정책, 수명, 이벤트 순서와 제한은 [설계 문서](TDDamageSystemDesign.md)에 정리되어 있다.

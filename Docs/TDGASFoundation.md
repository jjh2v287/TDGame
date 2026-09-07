# GAS 전투 기반과 데미지 오브젝트 연결

## 적용 범위

싱글 플레이 기준으로 플레이어, 동료, 몬스터가 같은 능력치·피해·상태이상·시전 경로를 사용한다. 이번 변경에서는 에디터 실행, PIE, 에디터 자동화 테스트를 수행하지 않는다. 컴파일과 코드 검토만 수행하며, 이전 GAS 전환 전 테스트 결과와 구분한다.

## 클래스 구성

| 클래스 | 역할 |
| --- | --- |
| `ATDCombatCharacter` | 공통 캐릭터. GAS 필수 `IAbilitySystemInterface`, 전투 컴포넌트와 액터 소유 AttributeSet, 시작 어빌리티/이펙트 제공 |
| `ATDGameCharacter` | 플레이어. 기존 카메라와 입력 연결 유지, 기본 팀 1 |
| `ATDCompanionCharacter` | 동료. 기본 팀 1, 기본 AI 컨트롤러 자동 소유 |
| `ATDMonsterCharacter` | 몬스터. 기본 팀 2, 기본 AI 컨트롤러 자동 소유 |
| `UTDCombatComponent` | `UAbilitySystemComponent`를 직접 상속한 유일한 ASC. 별도의 두 번째 ASC를 만들지 않음 |
| `UTDCombatAttributeSet` | Health, MaxHealth, Level, AttackPower, SpellPower, Armor, MagicResistance, CriticalChance, CriticalMultiplier |
| `UTDDamageGameplayAbility` | 데미지 정의를 시전하는 C++ GameplayAbility |

동료와 몬스터의 전투 판단, 추적, 공격 선택 AI는 아직 정의하지 않았다. 이 클래스의 데이터 전용 블루프린트에 메시/애니메이션, `DamageSpells`, `StartupAbilities`, `StartupEffects`와 초기 Stats를 설정하면 공통 전투 기반을 사용할 수 있다.

플레이어 설정 에셋은 `Content/Combat/Blueprints/BP_TDCombatCharacter`, `BP_TDCombatController`, `BP_TDCombatGameMode`다. 기존 템플릿 블루프린트는 보존하고 프로젝트 기본 게임 모드를 새 설정 전용 게임 모드로 연결했다. 이 에셋들은 GAS 전환 전에 작성했으므로, 다음 에디터 검증에서 변경된 C++ 부모 계층의 로드/재컴파일도 확인해야 한다.

## 실행 경로

`CastDamageSpell(Slot, Target)` → ASC `TryCastDamageDefinition` → `UTDDamageGameplayAbility` → `UTDDamageSubsystem` → 데미지 엔티티의 적중/주기 이벤트 → 피해 GameplayEffect → AttributeSet Health.

- 어빌리티는 정의 DataAsset을 SourceObject로 사용한다. 서로 다른 정의는 같은 C++ 어빌리티 클래스로 실행한다.
- 시전 시 사망/빙결, 사거리, 정의 유효성, 쿨다운을 검사한다.
- 쿨다운은 실제 `UTDDamageCooldownEffect`로 관리한다. 동일 정의를 여러 슬롯에서 사용하면 쿨다운을 공유하고, 다른 정의는 독립적이다.
- 시전 실패 시 이번 시전에서 적용한 쿨다운만 제거한다.
- 어빌리티는 엔티티를 만든 뒤 종료된다. 이미 발사된 투사체와 후속 장판은 원래 시전자 문맥으로 독립 실행된다.

플레이어와 AI 모두 공통 캐릭터의 `CastDamageSpell` 또는 ASC의 `TryCastDamageDefinition`을 사용한다. 서브시스템의 저수준 `Cast`는 능력치 문맥과 엔티티 생성에만 관여하므로, 직접 호출하면 GAS 쿨다운 경로를 사용하지 않는다.

## 능력치와 피해

`Stats`는 초기 수치와 레벨당 성장 계수를 보관하는 설정이다. 실행 중 체력과 전투 수치는 AttributeSet이 기준이며 별도의 CurrentHealth 변수를 유지하지 않는다.

- `GetStats()`는 버프/디버프가 반영된 현재 GAS 수치의 스냅샷을 반환한다. 이미 성장분을 포함했으므로 반환값의 레벨당 성장 계수는 0이다.
- 레벨 변경은 `SetCombatLevel`을 사용한다. 초기 성장 계수를 보존하면서 GAS 기본 수치를 갱신한다.
- `SetStats(Values, true)`는 초기 설정 교체 및 체력/소유 상태 리셋용이다. `GetStats()`의 정규화된 결과를 다시 초기 성장 설정으로 오해하지 않도록 한다.
- 시전 시점에 스냅샷을 복사하며 후속 엔티티, 도트, 상태이상이 이어받는다. 시전자 소멸 후에도 계산에 필요한 수치를 보관한다.
- 기존 피해 수식으로 치명타와 대상의 현재 Armor/MagicResistance를 계산하고, 음수 `Data.Damage` SetByCaller 값으로 `UTDInstantDamageEffect`를 적용한다.
- 실제 Health 변화량을 기준으로 피해 알림과 상태 누적량을 계산한다. 다른 GAS 이펙트가 Health를 변경하는 경우에도 체력/사망 알림 경로가 반응한다.

Health는 자원이다. 즉시 또는 주기 효과로 피해/회복을 처리하고, 지속 버프는 MaxHealth에 적용한다. 주기가 없는 Duration/Infinite Health 수정자는 적용 전에 거부한다. 양수 지속 Health 수정자가 사망할 수 없는 최소 체력을 만드는 문제를 방지하기 위한 정책이다. Health를 직접 변경하는 외부 GE의 값은 최종 변화량이며, 방어·치명타 계산이 필요하면 공통 `ReceiveDamage` 경로를 사용한다.

## 상태이상과 GAS 태그

| 태그/이펙트 | 용도 |
| --- | --- |
| `State.Dead` / `UTDDeadEffect` | 사망 상태와 시전 차단 |
| `State.Frozen` / `UTDFreezeStatusEffect` | 빙결 상태와 이동 정지/복원 |
| `UTDDurationStatusEffect` | 데이터 정의의 상태 지속 시간 |
| `Effect.Cooldown.Damage` / `UTDDamageCooldownEffect` | 정의별 시전 쿨다운 |

정확한 태그 문자열은 `TDGameplayTags.cpp`가 기준이다.

- 누적 임계값과 Spawn/Pulse/Expire 액션 조합은 기존 상태 DataAsset을 사용한다.
- 발동 후 지속 시간은 활성 GameplayEffect 핸들로 관리한다. 타이머는 미발동 누적 초기화와 데이터 액션의 주기 실행에만 사용한다.
- 자연 만료에서만 Expire 연계를 실행한다. GAS 핸들 제거에 의한 해제, 사망, 리셋에서는 해제 추가 피해를 발생시키지 않는다.
- 갱신할 때는 새 GE를 먼저 적용한 뒤 이전 핸들을 제거해 빙결이 순간적으로 풀리지 않게 한다.
- GAS Frozen 태그 변화가 실제 이동 정지/복원을 구동한다. 여러 빙결과 외부 GE도 태그 개수로 함께 처리한다.
- 대상의 CustomTimeDilation이 0이어도 GAS의 월드 시간 기반 지속 시간은 진행된다.
- OnDamaged 콜백에서 리셋되면 아직 발생하지 않은 사망 알림을 취소한다. OnDeath가 이미 발생한 뒤의 액터 파괴는 정상 사망 정리이며 Kill 이벤트의 사실을 취소하지 않는다.

ASC는 캐릭터에 소유되며 복제를 사용하지 않는다. PlayerState에 ASC를 두는 멀티플레이/리스폰 영속 구조는 현재 범위가 아니다.

## 액터 풀에 대한 판단

풀링은 GC를 없애는 방법이 아니다. 재사용할 객체를 살아 있게 유지해 반복 생성·파괴와 그에 따른 GC 대상 증가를 줄인다. 액터 생성에는 메모리 할당 외에도 컴포넌트 생성과 월드·렌더링·물리 등록이 포함된다. 대신 풀은 유휴 메모리를 계속 사용한다. [Epic 공식 성능 가이드](https://dev.epicgames.com/documentation/en-us/unreal-engine/common-memory-and-cpu-performance-considerations-in-unreal-engine)

현재 블리자드는 초당 16개의 낙하체를 생성하므로 데미지 엔티티는 풀링을 검토할 만한 대상이다. 다만 비용이 생성/파괴인지, 활성 투사체의 대상 탐색·충돌·Tick인지 아직 측정하지 않았다. 풀은 활성 로직 비용을 해결하지 않는다.

권장 순서는 다음과 같다.

1. 실제 동시 시전자/몬스터 수에서 Unreal Insights로 생성·파괴·GC·대상 탐색 시간을 측정한다.
2. 필요하면 `ATDDamageEntity` 전용의 수량 상한이 있는 풀부터 도입한다.
3. 플레이어·동료·몬스터는 ASC, 진행 중인 어빌리티, GE, AI, 소유권 등 초기화 범위가 커서 같은 범용 풀에 넣지 않는다.

이번 변경에는 풀을 구현하지 않았다. 풀을 도입할 때는 단순히 Destroy를 숨김으로 바꾸면 안 된다. 예약 액션, 적중 기록, 호밍 목표, 시전자와 예산 참조, Niagara/메시 상태를 모두 초기화하고, 재사용 세대 번호로 이전 실행의 지연 콜백을 차단해야 한다. BeginPlay/EndPlay는 재사용마다 호출되지 않으므로 별도의 획득/반환 수명 처리가 필요하다.

## 검증 상태와 다음 작업

GAS 전환 후 `TDGameEditor Win64 Development`와 `TDGame Win64 Development -DisableUnity` 빌드를 완료했다. 공통 클래스와 레벨 성장, GAS 시전/독립 쿨다운, 외부 상태 해제, 사망 태그 콜백 중 부활, Health 수정자 제한을 포함한 자동화 테스트 소스 27개를 컴파일했다. 테스트와 에디터 실행은 하지 않았다.

다음 설정 작업은 동료/몬스터의 데이터 전용 파생 블루프린트에서 메시와 애니메이션, 사용할 DamageSpells와 시작 어빌리티/이펙트를 지정하는 것이다. 다음 에디터 테스트 요청 시에는 저장된 플레이어 블루프린트 재컴파일, 플레이어/동료/몬스터 생성, 아군 필터, 스냅샷 피해, 빙결 해제와 GAS 쿨다운, 호밍 전환을 실제 PIE에서 확인한다.

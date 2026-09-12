# PJGame → TDGame 이식 지도 (2026-09-12)

이전 프로젝트 `C:\Project\PJGame\Source\PJGame`의 클래스를 TDGame으로 옮긴 결과. 방침은 [decisions.md D-11](Tasks/decisions.md). 이식 기준은 "컴파일이 되는 충실한 이식"이며 콘텐츠(입력 액션, 몽타주, 데이터 에셋) 연결은 하지 않았다.

## 1. 옮긴 클래스

| PJ 원본 | TD 클래스 | TD 파일 (`Source/TDGame/`) | 비고 |
|---|---|---|---|
| `PJAnimNotifyState_MeleeTrace` 높이 고정 | `UTDAnimNotifyState_MeleeAttack` (`bLockHeightToOwner`, `LockedHeightOffset`) | `Combat/AnimNotify/TDAnimNotifyState_MeleeAttack.*` | 기존 TD 노티파이에 플래그만 추가. 테스트 `TDGame.Combat.MeleeAttackNotifyLocksBladeHeightToOwner` |
| `PJGameplayTags` | `TDGameplayTags::*` | `Core/TDGameplayTags.*` | `TAG_` 접두어 제거, `Damage.*`·`SetByCaller.Damage` 제외 |
| `PJCoreTypes::FPJItemStack` | `FTDItemStack` | `Core/TDItemTypes.h` | |
| `PJMessageTypes` 3종 | `FTDDamageAppliedMessage`, `FTDActorDeathMessage`, `FTDCaravanDestroyedMessage` | `Core/TDGameplayMessages.h` | `DamageType` 태그 → `ETDDamageElement` |
| `PJCombatLibrary`, `FPJDamageSpec` | `UTDCombatLibrary`, `FTDDamageSpec` | `Combat/TDCombatLibrary.*` | `UTDCombatComponent::ReceiveDamage`로 위임, 팀은 `FTDCombatStats.TeamId` |
| `PJCooldownGameplayEffect` | `UTDActionCooldownEffect` | `Combat/GAS/TDCombatGameplayEffects.*` | SetByCaller `Data.Cooldown.Duration` |
| `PJCombatAttributeSet`의 Stamina/MaxStamina | `UTDCombatAttributeSet::Stamina/MaxStamina`, `FTDCombatStats::MaxStamina`, `UTDCombatComponent::GetCurrentStamina/GetMaxStamina/ConsumeStamina/RestoreStamina` | `Combat/GAS/TDCombatAttributeSet.*`, `Combat/TDCombatComponent.*` | 기존 어트리뷰트셋에 추가 |
| `PJCombatTypes` | `ETDCombatHitExecutionType`, `FTDCombatActionDefinition`, `FTDCombatReactionDefinition` | `Combat/Skills/TDCombatActionTypes.h` | |
| `PJCombatStyleDataAsset` | `UTDCombatStyleDefinition` | `Combat/Skills/TDCombatStyleDefinition.*` | |
| `PJSkillComponent` | `UTDSkillComponent` | `Combat/Skills/TDSkillComponent.*` | 콤보·입력 버퍼·액션 컨텍스트 |
| `PJGameplayAbility_CombatAction` + 파생 8 | `UTDCombatActionAbility` + `UTDPlayerPrimaryAttack(01/02/03)Ability`, `UTDPlayerSkillQ/EAbility`, `UTDMonsterPrimaryAttackAbility`, `UTDMonsterSkill01Ability` | `Combat/GAS/Abilities/TDCombatActionAbility.*` | |
| `PJGameplayAbility_Reaction` + 파생 2 | `UTDReactionAbility`, `UTDReactionHitAbility`, `UTDReactionDeathAbility` | `Combat/GAS/Abilities/TDReactionAbility.*` | |
| `PJGameplayAbility_PlayerMovement` | `UTDPlayerRollAbility`, `UTDPlayerJumpAbility` | `Combat/GAS/Abilities/TDPlayerMovementAbilities.*` | |
| `PJAnimNotifyState_AbilityTagWindow/InputBufferWindow/JumpCapsuleModifier` | `UTDAnimNotifyState_AbilityTagWindow/InputBufferWindow/JumpCapsuleModifier` | `Combat/AnimNotify/` | |
| `PJCapsuleModifierComponent` | `UTDCapsuleModifierComponent` | `Characters/TDCapsuleModifierComponent.*` | 주석 처리돼 있던 구르기 모디파이어 복원 |
| `PJPartComponent` | `UTDPartComponent` | `Characters/TDPartComponent.*` | |
| `PJPlayerCharacter` | 기존 `ATDGameCharacter`에 병합 | `Characters/TDGameCharacter.*` | 기본 어빌리티 자동 부여는 `bGrantDefaultActionAbilities`(기본 false) |
| `PJThirdPersonPlayerCharacter` | `ATDThirdPersonPlayerCharacter` | `Characters/TDThirdPersonPlayerCharacter.*` | |
| `PJMonsterCharacterBase` | 기존 `ATDMonsterCharacter`에 병합 | `Characters/TDMonsterCharacter.*` | `bUseNPCUpdateSubsystem`, `bGrantDefaultActionAbilities` 기본 false |
| `PJBossMonsterCharacter` | `ATDBossMonsterCharacter` | `Characters/TDBossMonsterCharacter.*` | |
| `PJTopDownPlayerController` | 기존 `ATDGamePlayerController`에 병합 | `Framework/TDGamePlayerController.*` | 입력 액션 프로퍼티가 null이면 바인딩 안 함 |
| `PJThirdPersonPlayerController`, `PJThirdPersonGameMode` | `ATDThirdPersonPlayerController`, `ATDThirdPersonGameMode` | `Framework/ThirdPerson/` | 입력 에셋을 C++에서 생성 |
| `PJBreakableActor` | `ATDBreakableActor` | `Actors/TDBreakableActor.*` | Chaos 모듈 3종 추가 |
| `PJCaravanActor` | `ATDCaravanActor` | `Actors/TDCaravanActor.*` | 콜리전 프로파일 `Caravan` 대신 `Pawn` |
| `PJCombatTokenManager`, `PJBTTask_CombatTokenRequestAndRelease` | `UTDCombatTokenSubsystem`, `UTDBTTask_CombatTokenRequestAndRelease` | `AI/CombatToken/` | BT 태스크는 컴파일용(TD AI는 StateTree) |
| `PJNPCUpdateInterface/Manager`, `PJSignificanceComponent` | `ITDNPCUpdatable`, `UTDNPCUpdateSubsystem`, `UTDSignificanceComponent` | `AI/NPC/` | CVar `td.NPCUpdate.*` |
| `PJBudgetTick*` 4종 | `ITDBudgetTickable`, `UTDBudgetTickParticipantComponent`, `UTDBudgetTickSubsystem`, `ATDBudgetTickTestActor` | `Performance/BudgetTick/` | CVar `td.BudgetTick.*` |
| `PJGameSmokeTests` | `TDGame.Smoke.PortedClassesExist` | `Tests/TDPortedClassesSmokeTests.cpp` | |
| 플러그인 `GameplayMessageRouter` | 동일 | `Plugins/GameplayMessageRouter/` | PJ 프로젝트 플러그인 복사, uproject에서 활성화. `SignificanceManager` 엔진 플러그인도 활성화 |

## 2. 옮기지 않은 것과 이유

| PJ | 이유 |
|---|---|
| `UPJAbilitySystemComponent`, `UPJCombatAttributeSet`, `UPJDamageGameplayEffect`, `UPJDamageExecution` | `UTDCombatComponent`·`UTDCombatAttributeSet`·`UTDInstantDamageEffect`·`ReceiveDamage`가 같은 역할. ASC 이중화 방지 |
| `UPJTeamComponent`, `EPJTeamId` | `FTDCombatStats.TeamId`로 통일 |
| `IPJDamageable` | TD는 `UTDCombatComponent` 보유 여부로 판정 |
| `IPJInteractable`, `UPJFeatureToggleSettings` | PJ 안에서도 사용처 없음 |
| `PJAnimNotifyState_MeleeTrace` | TD 노티파이가 상위 호환(본 사이 샘플·고정 간격 스윕). 높이 고정만 이식 |
| 플러그인 `EnhancedTick`, `SmartObjects`, `GameplayBehaviorSmartObjects` | PJ 코드에서 미사용 |

## 3. 실제로 쓰려면 해야 할 것 (콘텐츠)
- 입력 액션·매핑 컨텍스트 에셋을 만들어 `ATDGamePlayerController`의 `PrimaryAttackAction/RollAction/JumpAction/SkillQAction/SkillEAction`에 연결(3인칭 컨트롤러는 코드 생성이라 불필요).
- `UTDCombatStyleDefinition` 데이터 에셋(태그 → 몽타주·사거리·배율)을 만들어 캐릭터 `SkillComponent->SkillSet`에 연결, 몽타주에 `TD Input Buffer Window`·`TD Ability Tag Window`·`TD Melee Attack` 노티파이 배치.
- 구르기 몽타주를 `ATDGameCharacter::RollMontage`에 연결.
- `bGrantDefaultActionAbilities`를 켜거나 `StartupAbilities`에 어빌리티 클래스를 넣는다.
- 캐러밴 전용 콜리전 프로파일이 필요하면 `DefaultEngine.ini`에 `Caravan` 프로파일 추가.
- BT 태스크 대신 StateTree 태스크로 토큰 요청·반납을 다시 만든다.

## 4. 폴더 구조 (정리 후)
```
Source/TDGame/
  TDGame.h/.cpp/.Build.cs
  Core/            게임플레이 태그, 메시지, 아이템 타입
  Characters/      전투 캐릭터 베이스, 플레이어(탑다운·3인칭), 몬스터, 보스, 동료, 캡슐 모디파이어, 파츠 메시
  Framework/       게임모드, 탑다운 컨트롤러, ThirdPerson/
  Combat/          전투 컴포넌트(ASC), 전투 라이브러리
  Combat/Damage/   데미지 타입·정의·서브시스템·엔티티·상태·예제
  Combat/GAS/      어트리뷰트셋, 이펙트, 어빌리티(Abilities/)
  Combat/Skills/   액션 정의, 스타일 정의 에셋, 스킬 컴포넌트
  Combat/AnimNotify/ 근접 공격·태그 창·입력 버퍼·점프 캡슐 노티파이
  Combat/Tests/
  AI/CombatToken/, AI/NPC/
  Performance/BudgetTick/
  Actors/          부서지는 액터, 캐러밴
  World/Streaming/, World/Persistence/, World/Generation/, World/Tests/
  Tests/           스모크 테스트
  Variant_Strategy/, Variant_TwinStick/  (템플릿 잔재, 결정 D-01 대기)
```

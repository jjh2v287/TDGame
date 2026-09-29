#if WITH_DEV_AUTOMATION_TESTS

#include "Animation/AnimCurveFilter.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimationAsset.h"
#include "Animation/AnimationPoseData.h"
#include "Animation/AttributesRuntime.h"
#include "BonePose.h"
#include "Characters/TDCharacterAnimationComponent.h"
#include "Characters/TDGameCharacter.h"
#include "Characters/TDMonsterCharacter.h"
#include "Combat/GAS/Abilities/TDCombatActionAbility.h"
#include "Combat/Damage/TDStatusDefinition.h"
#include "Combat/Skills/TDCombatStyleDefinition.h"
#include "Combat/Skills/TDSkillComponent.h"
#include "Combat/TDCombatComponent.h"
#include "Component/AnimNextComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Core/TDGameplayTags.h"
#include "Engine/CollisionProfile.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameplayAbilitySpec.h"
#include "GameFramework/GameModeBase.h"
#include "Misc/AutomationTest.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/MemStack.h"
#include "UObject/UnrealType.h"

namespace TDMovementAnimationTestSupport
{
	constexpr float FrameSeconds = 1.f / 60.f;
	constexpr float FloorHalfThickness = 50.f;
	constexpr float PlayerSpawnHeight = 98.f;
	constexpr float MonsterSpawnHeight = 90.f;
	constexpr float ExpectedRootMotionDistance = 36.f;
	constexpr float RootMotionDistanceTolerance = 2.f;
	constexpr float HitWindowStartSeconds = 0.1f;
	constexpr float HitWindowDurationSeconds = 0.5f;
	constexpr float TimelineTargetRadius = 10.f;
	const FName BladeBaseBoneName(TEXT("lowerarm_r"));
	const FName BladeTipBoneName(TEXT("hand_r"));
	const TCHAR* SwordAttackSequencePath = TEXT("/Game/Characters/Mannequins/Anims/Sword/AS_TD_Player_SwordAttack01.AS_TD_Player_SwordAttack01");
	const TCHAR* UnarmedAttackSequencePath = TEXT("/Game/Characters/Mannequins/Anims/Unarmed/Attack/MM_Attack_01.MM_Attack_01");

	struct FTDScopedMovementWorld
	{
		FTDScopedMovementWorld()
		{
			const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDMovementAnimationTestWorld"));
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true);
			SpawnFloor();
		}

		~FTDScopedMovementWorld()
		{
			World->EndPlay(EEndPlayReason::Quit);
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}

		void SpawnFloor()
		{
			AActor* Floor = World->SpawnActor<AActor>();
			UBoxComponent* Box = NewObject<UBoxComponent>(Floor);
			Floor->AddInstanceComponent(Box);
			Floor->SetRootComponent(Box);
			Box->SetBoxExtent(FVector(5000.f, 5000.f, FloorHalfThickness));
			Box->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
			Box->RegisterComponent();
			Floor->SetActorLocation(FVector(0.f, 0.f, -FloorHalfThickness));
		}

		ATDGameCharacter* SpawnPlayer()
		{
			UClass* PlayerPawnClass = LoadDefaultPlayerPawnClass();
			if (!PlayerPawnClass)
			{
				return nullptr;
			}
			const FTransform SpawnTransform(FRotator::ZeroRotator, FVector(0.f, 0.f, PlayerSpawnHeight));
			return World->SpawnActor<ATDGameCharacter>(PlayerPawnClass, SpawnTransform);
		}

		static UClass* LoadDefaultPlayerPawnClass()
		{
			FString GameModePath;
			GConfig->GetString(TEXT("/Script/EngineSettings.GameMapsSettings"), TEXT("GlobalDefaultGameMode"), GameModePath, GEngineIni);
			const UClass* GameModeClass = LoadClass<AGameModeBase>(nullptr, *GameModePath);
			const AGameModeBase* GameModeDefaults = GameModeClass ? GameModeClass->GetDefaultObject<AGameModeBase>() : nullptr;
			UClass* PlayerPawnClass = GameModeDefaults ? GameModeDefaults->DefaultPawnClass.Get() : nullptr;
			return PlayerPawnClass && PlayerPawnClass->IsChildOf(ATDGameCharacter::StaticClass()) ? PlayerPawnClass : nullptr;
		}

		ATDMonsterCharacter* SpawnMonster()
		{
			const FTransform SpawnTransform(FRotator::ZeroRotator, FVector(0.f, 0.f, MonsterSpawnHeight));
			return World->SpawnActor<ATDMonsterCharacter>(ATDMonsterCharacter::StaticClass(), SpawnTransform);
		}

		UTDCombatComponent* SpawnTarget(const FVector& Location, int32 TeamId)
		{
			AActor* Actor = World->SpawnActor<AActor>();
			USphereComponent* Sphere = NewObject<USphereComponent>(Actor);
			Actor->AddInstanceComponent(Sphere);
			Actor->SetRootComponent(Sphere);
			Sphere->SetSphereRadius(TimelineTargetRadius);
			Sphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Sphere->SetCollisionObjectType(ECC_Pawn);
			Sphere->SetCollisionResponseToAllChannels(ECR_Overlap);
			Sphere->RegisterComponent();
			Actor->SetActorLocation(Location);
			UTDCombatComponent* Combatant = NewObject<UTDCombatComponent>(Actor);
			Actor->AddInstanceComponent(Combatant);
			FTDCombatStats Stats;
			Stats.TeamId = TeamId;
			Stats.BaseMaxHealth = 1000.f;
			Combatant->SetStats(Stats);
			Combatant->RegisterComponent();
			return Combatant;
		}

		void Tick(float Duration, const TFunctionRef<void()> BeforeFrame = [] {})
		{
			for (float Remaining = Duration; Remaining > UE_SMALL_NUMBER;)
			{
				const float Delta = FMath::Min(Remaining, FrameSeconds);
				BeforeFrame();
				++GFrameCounter;
				World->Tick(LEVELTICK_All, Delta);
				Remaining -= Delta;
			}
		}

		UWorld* World = nullptr;
	};

	bool SampleBoneInComponentSpace(const USkeletalMesh& Mesh, const UAnimSequence& Sequence, FName BoneName, double SampleTime, FVector& OutLocation)
	{
		const FReferenceSkeleton& ReferenceSkeleton = Mesh.GetRefSkeleton();
		const int32 BoneIndex = ReferenceSkeleton.FindBoneIndex(BoneName);
		if (BoneIndex == INDEX_NONE)
		{
			return false;
		}

		TArray<FBoneIndexType> RequiredBones;
		for (int32 ChainBoneIndex = BoneIndex; ChainBoneIndex != INDEX_NONE; ChainBoneIndex = ReferenceSkeleton.GetParentIndex(ChainBoneIndex))
		{
			RequiredBones.Add(static_cast<FBoneIndexType>(ChainBoneIndex));
		}
		RequiredBones.Sort();

		FBoneContainer BoneContainer;
		BoneContainer.InitializeTo(RequiredBones, UE::Anim::FCurveFilterSettings(UE::Anim::ECurveFilterMode::DisallowAll), Mesh);
		FMemMark Mark(FMemStack::Get());
		FCompactPose Pose;
		Pose.SetBoneContainer(&BoneContainer);
		Pose.ResetToRefPose();
		FBlendedCurve Curve;
		Curve.InitFrom(BoneContainer);
		UE::Anim::FStackAttributeContainer Attributes;
		FAnimationPoseData PoseData(Pose, Curve, Attributes);
		Sequence.GetAnimationPose(PoseData, FAnimExtractContext(SampleTime, true));

		FCSPose<FCompactPose> ComponentSpacePose;
		ComponentSpacePose.InitPose(MoveTemp(Pose));
		OutLocation = ComponentSpacePose.GetComponentSpaceTransform(BoneContainer.MakeCompactPoseIndex(FMeshPoseBoneIndex(BoneIndex))).GetLocation();
		return true;
	}

	void IgnoreEngineInjectionTimelineEnsure(FAutomationTestBase& Test)
	{
		Test.AddExpectedError(TEXT("TimelineTrait.GetState(Context, ChildState)"), EAutomationExpectedErrorFlags::Contains, -1, false);
		Test.AddExpectedError(TEXT("=== Handled ensure: ==="), EAutomationExpectedErrorFlags::Contains, -1, false);
		Test.AddExpectedError(TEXT("Stack:"), EAutomationExpectedErrorFlags::Contains, -1, false);
		Test.AddExpectedError(TEXT("[Callstack]"), EAutomationExpectedErrorFlags::Contains, -1, false);
		Test.AddExpectedError(TEXT(""), EAutomationExpectedErrorFlags::Exact, -1, false);
	}

	FTDCombatActionDefinition MakeTimelineSweepAction(UAnimSequence* Animation, float WindowEndSeconds)
	{
		FTDCombatActionDefinition Definition;
		Definition.Action.Animation = Animation;
		Definition.Action.bUseRootMotion = false;
		Definition.bRotateToTarget = false;
		Definition.bRequiresTarget = false;
		Definition.HitExecutionType = ETDCombatHitExecutionType::TimelineSweep;

		FTDMeleeHitWindow HitWindow;
		HitWindow.Window.StartSeconds = HitWindowStartSeconds;
		HitWindow.Window.EndSeconds = WindowEndSeconds;
		HitWindow.Sweep.WeaponBaseSocketName = BladeBaseBoneName;
		HitWindow.Sweep.WeaponTipSocketName = BladeTipBoneName;
		HitWindow.Sweep.SweepChannel = ECC_Pawn;
		FTDDamageAction DamageAction;
		DamageAction.Magnitude.Base = 10.f;
		FTDDamageRule HitRule;
		HitRule.Event = ETDDamageEvent::Hit;
		HitRule.Actions.Add(DamageAction);
		HitWindow.Sweep.HitRules.Add(HitRule);
		Definition.Action.HitWindows.Add(HitWindow);
		return Definition;
	}

	bool AssignSkillQAction(UTDSkillComponent& SkillComponent, UObject& Outer, const FTDCombatActionDefinition& Definition)
	{
		UTDCombatStyleDefinition* Style = NewObject<UTDCombatStyleDefinition>(&Outer);
		const FMapProperty* ActionMapProperty = FindFProperty<FMapProperty>(UTDCombatStyleDefinition::StaticClass(), TEXT("ActionMap"));
		const FObjectPropertyBase* SkillSetProperty = FindFProperty<FObjectPropertyBase>(UTDSkillComponent::StaticClass(), TEXT("SkillSet"));
		if (!ActionMapProperty || !SkillSetProperty)
		{
			return false;
		}
		TMap<FGameplayTag, FTDCombatActionDefinition>* ActionMap = ActionMapProperty->ContainerPtrToValuePtr<TMap<FGameplayTag, FTDCombatActionDefinition>>(Style);
		ActionMap->Add(TDGameplayTags::Action_Skill_Q, Definition);
		SkillSetProperty->SetObjectPropertyValue_InContainer(&SkillComponent, Style);
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDRootMotionActionMovesPawnTest, "TDGame.Movement.RootMotionActionMovesPawn", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDRootMotionActionMovesPawnTest::RunTest(const FString& Parameters)
{
	TDMovementAnimationTestSupport::IgnoreEngineInjectionTimelineEnsure(*this);
	UAnimSequence* SwordAttack = LoadObject<UAnimSequence>(nullptr, TDMovementAnimationTestSupport::SwordAttackSequencePath);
	if (!TestNotNull(TEXT("Sword attack sequence exists"), SwordAttack))
	{
		return false;
	}

	TDMovementAnimationTestSupport::FTDScopedMovementWorld Fixture;
	ATDGameCharacter* Player = Fixture.SpawnPlayer();
	if (!TestNotNull(TEXT("Player pawn spawns"), Player))
	{
		return false;
	}
	Fixture.Tick(0.5f);
	TestTrue(TEXT("Player pawn stands on the floor"), Player->IsMovingOnGround());

	const FVector StartLocation = Player->GetActorLocation();
	const float PlaySeconds = Player->GetCharacterAnimation()->PlayAction(SwordAttack, 1.f, 0.f, 0.f, true);
	if (!TestTrue(TEXT("Root motion action starts"), PlaySeconds > 0.f))
	{
		return false;
	}
	Fixture.Tick(PlaySeconds + 0.2f);

	const float MovedDistance = FVector::Dist2D(StartLocation, Player->GetActorLocation());
	AddInfo(FString::Printf(TEXT("Root motion moved the pawn %.2f cm over %.3f s"), MovedDistance, PlaySeconds));
	TestFalse(TEXT("Action finished"), Player->GetCharacterAnimation()->IsPlayingAction());
	TestEqual(TEXT("Root motion moves the pawn by the authored distance"), MovedDistance, TDMovementAnimationTestSupport::ExpectedRootMotionDistance, TDMovementAnimationTestSupport::RootMotionDistanceTolerance);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDFreezeHaltsMovementAndAnimationTest, "TDGame.Movement.FreezeHaltsMovementAndAnimation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFreezeHaltsMovementAndAnimationTest::RunTest(const FString& Parameters)
{
	TDMovementAnimationTestSupport::IgnoreEngineInjectionTimelineEnsure(*this);
	TDMovementAnimationTestSupport::FTDScopedMovementWorld Fixture;
	ATDGameCharacter* Player = Fixture.SpawnPlayer();
	if (!TestNotNull(TEXT("Player pawn spawns"), Player))
	{
		return false;
	}
	Fixture.Tick(0.3f);

	const auto PushForward = [Player]() { Player->AddMovementInput(FVector::ForwardVector, 1.f, true); };
	const FVector BeforeMoveLocation = Player->GetActorLocation();
	Fixture.Tick(0.3f, PushForward);
	const float MovedBeforeFreeze = FVector::Dist2D(BeforeMoveLocation, Player->GetActorLocation());
	TestTrue(TEXT("Movement input moves the pawn before the freeze"), MovedBeforeFreeze > 20.f);

	UTDStatusDefinition* Freeze = NewObject<UTDStatusDefinition>(Fixture.World);
	Freeze->Duration = 0.6f;
	Freeze->bFreezesTarget = true;
	FTDDamageContext FreezeContext;
	FreezeContext.Caster = Player;
	FreezeContext.Budget = MakeShared<FTDDamageChainBudget>();
	UTDCombatComponent* Combat = Player->GetCombatComponent();
	Combat->ApplyStatus(Freeze, FreezeContext);
	if (!TestTrue(TEXT("Freeze status is applied"), Combat->IsFrozen()))
	{
		return false;
	}

	const FVector FrozenLocation = Player->GetActorLocation();
	Fixture.Tick(0.5f, PushForward);
	const float MovedWhileFrozen = FVector::Dist2D(FrozenLocation, Player->GetActorLocation());
	AddInfo(FString::Printf(TEXT("Moved %.3f cm while frozen, %.2f cm before the freeze"), MovedWhileFrozen, MovedBeforeFreeze));
	TestTrue(TEXT("Frozen pawn does not move under input"), MovedWhileFrozen < 1.f);
	TestTrue(TEXT("Animation is frozen"), Player->GetCharacterAnimation()->IsAnimationFrozen());
	TestFalse(TEXT("UAF system is deactivated while frozen"), Player->GetUAFComponent()->IsActive());

	Fixture.Tick(0.3f);
	TestFalse(TEXT("Freeze expires"), Combat->IsFrozen());
	TestFalse(TEXT("Animation resumes"), Player->GetCharacterAnimation()->IsAnimationFrozen());
	TestTrue(TEXT("UAF system is active again"), Player->GetUAFComponent()->IsActive());

	const FVector ThawedLocation = Player->GetActorLocation();
	Fixture.Tick(0.5f, PushForward);
	const float MovedAfterThaw = FVector::Dist2D(ThawedLocation, Player->GetActorLocation());
	AddInfo(FString::Printf(TEXT("Moved %.2f cm after the thaw"), MovedAfterThaw));
	TestTrue(TEXT("Movement resumes after the thaw"), MovedAfterThaw > 20.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDMonsterMoveIntentMovesPawnTest, "TDGame.Movement.MonsterMoveIntentMovesPawn", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDMonsterMoveIntentMovesPawnTest::RunTest(const FString& Parameters)
{
	TDMovementAnimationTestSupport::FTDScopedMovementWorld Fixture;
	ATDMonsterCharacter* Monster = Fixture.SpawnMonster();
	if (!TestNotNull(TEXT("Monster pawn spawns"), Monster))
	{
		return false;
	}
	Fixture.Tick(0.3f);

	const FVector StartLocation = Monster->GetActorLocation();
	Fixture.Tick(1.f, [Monster]() { Monster->ApplyBodyMoveIntent(FVector2D(1.f, 0.f), 1.f); });
	const FVector Offset = Monster->GetActorLocation() - StartLocation;
	AddInfo(FString::Printf(TEXT("Monster moved X %.2f cm, Y %.2f cm, Z %.2f cm"), Offset.X, Offset.Y, Offset.Z));
	TestTrue(TEXT("Move intent carries the monster forward"), Offset.X > 100.f);
	TestTrue(TEXT("Move intent keeps the monster on its line"), FMath::Abs(Offset.Y) < 5.f);
	TestTrue(TEXT("Monster faces its movement direction"), Monster->GetActorForwardVector().X > 0.9f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDActionTimelineHitsOnceInWindowTest, "TDGame.Combat.ActionTimelineHitsOnceInWindow", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDActionTimelineHitsOnceInWindowTest::RunTest(const FString& Parameters)
{
	TDMovementAnimationTestSupport::IgnoreEngineInjectionTimelineEnsure(*this);
	UAnimSequence* Attack = LoadObject<UAnimSequence>(nullptr, TDMovementAnimationTestSupport::UnarmedAttackSequencePath);
	if (!TestNotNull(TEXT("Attack sequence exists"), Attack))
	{
		return false;
	}

	TDMovementAnimationTestSupport::FTDScopedMovementWorld Fixture;
	ATDGameCharacter* Player = Fixture.SpawnPlayer();
	if (!TestNotNull(TEXT("Player pawn spawns"), Player))
	{
		return false;
	}
	Fixture.Tick(0.3f);

	const USkeletalMesh* Mesh = Player->GetMesh()->GetSkeletalMeshAsset();
	if (!TestNotNull(TEXT("Player has a skeletal mesh"), Mesh))
	{
		return false;
	}

	const float WindowEndSeconds = FMath::Min(TDMovementAnimationTestSupport::HitWindowStartSeconds + TDMovementAnimationTestSupport::HitWindowDurationSeconds, Attack->GetPlayLength() - 0.05f);
	if (!TestTrue(TEXT("Skill Q action is assigned"), TDMovementAnimationTestSupport::AssignSkillQAction(*Player->GetSkillComponent(), *Fixture.World, TDMovementAnimationTestSupport::MakeTimelineSweepAction(Attack, WindowEndSeconds))))
	{
		return false;
	}

	FVector TipComponentLocation = FVector::ZeroVector;
	const double WindowMiddleSeconds = (TDMovementAnimationTestSupport::HitWindowStartSeconds + WindowEndSeconds) * 0.5;
	if (!TestTrue(TEXT("Blade tip can be sampled"), TDMovementAnimationTestSupport::SampleBoneInComponentSpace(*Mesh, *Attack, TDMovementAnimationTestSupport::BladeTipBoneName, WindowMiddleSeconds, TipComponentLocation)))
	{
		return false;
	}
	const FVector TipWorldLocation = Player->GetMesh()->GetComponentTransform().TransformPosition(TipComponentLocation);
	UTDCombatComponent* Enemy = Fixture.SpawnTarget(TipWorldLocation, 2);
	UTDCombatComponent* FarEnemy = Fixture.SpawnTarget(TipWorldLocation + FVector(0.f, 0.f, 1000.f), 2);

	UTDCombatComponent* PlayerCombat = Player->GetCombatComponent();
	TestTrue(TEXT("Skill component resolves the timeline action"), Player->GetSkillComponent()->HasActionDefinition(TDGameplayTags::Action_Skill_Q));
	if (!PlayerCombat->FindAbilitySpecFromClass(UTDPlayerSkillQAbility::StaticClass()))
	{
		PlayerCombat->GiveAbility(FGameplayAbilitySpec(UTDPlayerSkillQAbility::StaticClass(), 1, INDEX_NONE, Player));
	}
	TestNotNull(TEXT("Skill Q ability is granted"), PlayerCombat->FindAbilitySpecFromClass(UTDPlayerSkillQAbility::StaticClass()));
	AddInfo(FString::Printf(TEXT("Player tags before activation: %s, stamina %.1f"), *PlayerCombat->GetOwnedGameplayTags().ToStringSimple(), PlayerCombat->GetCurrentStamina()));
	TestTrue(TEXT("Timeline action ability activates"), Player->ActivateCombatAbility(TDGameplayTags::Action_Skill_Q, Enemy->GetOwner()));
	TestTrue(TEXT("Action animation is playing"), Player->GetCharacterAnimation()->IsPlayingAction(Attack));
	Fixture.Tick(Attack->GetPlayLength() + 0.3f);

	AddInfo(FString::Printf(TEXT("Enemy health %.1f, far enemy health %.1f"), Enemy->GetCurrentHealth(), FarEnemy->GetCurrentHealth()));
	TestEqual(TEXT("Enemy on the blade path is hit exactly once"), Enemy->GetCurrentHealth(), 990.f);
	TestEqual(TEXT("Enemy away from the blade path is untouched"), FarEnemy->GetCurrentHealth(), 1000.f);
	TestFalse(TEXT("Action animation finished"), Player->GetCharacterAnimation()->IsPlayingAction());
	TestFalse(TEXT("Skill state is cleared when the timeline completes"), Player->GetCombatComponent()->HasMatchingGameplayTag(TDGameplayTags::State_Skill));
	return true;
}

#endif

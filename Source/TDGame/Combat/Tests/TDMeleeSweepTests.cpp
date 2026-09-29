#if WITH_DEV_AUTOMATION_TESTS

#include "Animation/AnimCurveFilter.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimationAsset.h"
#include "Animation/AnimationPoseData.h"
#include "Animation/AttributesRuntime.h"
#include "BonePose.h"
#include "Combat/TDCombatComponent.h"
#include "Combat/TDMeleeSweep.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/MemStack.h"

namespace
{
	constexpr float MeleeWindowStartTime = 0.1f;
	constexpr float MeleeWindowDuration = 0.5f;
	constexpr float MeleeLongFrameAnimationTime = 0.5f;
	constexpr float MeleeTargetRadius = 10.f;
	const FName BladeBaseBoneName(TEXT("lowerarm_r"));
	const FName BladeTipBoneName(TEXT("hand_r"));
	const TCHAR* MeleeAttackerMeshPath = TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple");
	const TCHAR* MeleeAttackSequencePath = TEXT("/Game/Characters/Mannequins/Anims/Unarmed/Attack/MM_Attack_01.MM_Attack_01");

	struct FTDScopedMeleeWorld
	{
		FTDScopedMeleeWorld()
		{
			const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDMeleeSweepTestWorld"));
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true);
		}

		~FTDScopedMeleeWorld()
		{
			World->EndPlay(EEndPlayReason::Quit);
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}

		UTDCombatComponent* AddCombatant(AActor* Actor, int32 TeamId)
		{
			UTDCombatComponent* Combatant = NewObject<UTDCombatComponent>(Actor);
			Actor->AddInstanceComponent(Combatant);
			FTDCombatStats Stats;
			Stats.TeamId = TeamId;
			Stats.BaseMaxHealth = 1000.f;
			Combatant->SetStats(Stats);
			Combatant->RegisterComponent();
			return Combatant;
		}

		UTDCombatComponent* SpawnTarget(const FVector& Location, int32 TeamId)
		{
			AActor* Actor = World->SpawnActor<AActor>();
			USphereComponent* Sphere = NewObject<USphereComponent>(Actor);
			Actor->AddInstanceComponent(Sphere);
			Actor->SetRootComponent(Sphere);
			Sphere->SetSphereRadius(MeleeTargetRadius);
			Sphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Sphere->SetCollisionObjectType(ECC_Pawn);
			Sphere->SetCollisionResponseToAllChannels(ECR_Overlap);
			Sphere->RegisterComponent();
			Actor->SetActorLocation(Location);
			return AddCombatant(Actor, TeamId);
		}

		USkeletalMeshComponent* SpawnAttacker(USkeletalMesh* Mesh, int32 TeamId)
		{
			AActor* Actor = World->SpawnActor<AActor>();
			USkeletalMeshComponent* MeshComponent = NewObject<USkeletalMeshComponent>(Actor);
			Actor->AddInstanceComponent(MeshComponent);
			Actor->SetRootComponent(MeshComponent);
			MeshComponent->SetSkeletalMeshAsset(Mesh);
			MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			MeshComponent->RegisterComponent();
			AddCombatant(Actor, TeamId);
			return MeshComponent;
		}

		UWorld* World = nullptr;
	};

	float GetWindowEndTime(const UAnimSequence* AttackSequence)
	{
		return FMath::Min(MeleeWindowStartTime + MeleeWindowDuration, AttackSequence->GetPlayLength() - 0.05f);
	}

	FTDMeleeSweepSettings MakeSweepSettings(float DamageAmount, bool bLockHeight = false, float LockedHeightOffset = 0.f)
	{
		FTDMeleeSweepSettings Settings;
		Settings.WeaponBaseSocketName = BladeBaseBoneName;
		Settings.WeaponTipSocketName = BladeTipBoneName;
		Settings.SweepChannel = ECC_Pawn;
		Settings.bLockHeightToOwner = bLockHeight;
		Settings.LockedHeightOffset = LockedHeightOffset;
		FTDDamageAction DamageAction;
		DamageAction.Magnitude.Base = DamageAmount;
		FTDDamageRule HitRule;
		HitRule.Event = ETDDamageEvent::Hit;
		HitRule.Actions.Add(DamageAction);
		Settings.HitRules.Add(HitRule);
		return Settings;
	}

	FTDDamageContext MakeAttackerDamageContext(USkeletalMeshComponent* Attacker)
	{
		AActor* Owner = Attacker->GetOwner();
		FTDDamageContext DamageContext;
		DamageContext.Caster = Owner;
		DamageContext.Stats = Owner->FindComponentByClass<UTDCombatComponent>()->GetStats();
		DamageContext.CastTarget = Owner->GetActorLocation();
		DamageContext.Direction = Owner->GetActorForwardVector();
		DamageContext.Budget = MakeShared<FTDDamageChainBudget>();
		return DamageContext;
	}

	bool SweepAttackWindowWithLongFrame(USkeletalMeshComponent* Attacker, const UAnimSequence* AttackSequence, const FTDMeleeSweepSettings& Settings)
	{
		FTDMeleeSweep Sweep;
		if (!Sweep.Begin(Attacker, AttackSequence, Settings, MakeAttackerDamageContext(Attacker), MeleeWindowStartTime, false))
		{
			return false;
		}
		Sweep.Advance(MeleeLongFrameAnimationTime);
		Sweep.End(GetWindowEndTime(AttackSequence));
		return !Sweep.IsActive();
	}

	bool SampleBladeTipAtWindowMiddle(const USkeletalMesh* Mesh, const UAnimSequence* AttackSequence, FVector& OutTipLocation)
	{
		const FReferenceSkeleton& ReferenceSkeleton = Mesh->GetRefSkeleton();
		const int32 TipBoneIndex = ReferenceSkeleton.FindBoneIndex(BladeTipBoneName);
		if (TipBoneIndex == INDEX_NONE)
		{
			return false;
		}

		TArray<FBoneIndexType> RequiredBones;
		for (int32 BoneIndex = TipBoneIndex; BoneIndex != INDEX_NONE; BoneIndex = ReferenceSkeleton.GetParentIndex(BoneIndex))
		{
			RequiredBones.Add(static_cast<FBoneIndexType>(BoneIndex));
		}
		RequiredBones.Sort();

		FBoneContainer BoneContainer;
		BoneContainer.InitializeTo(RequiredBones, UE::Anim::FCurveFilterSettings(UE::Anim::ECurveFilterMode::DisallowAll), *Mesh);
		FMemMark Mark(FMemStack::Get());
		FCompactPose Pose;
		Pose.SetBoneContainer(&BoneContainer);
		Pose.ResetToRefPose();
		FBlendedCurve Curve;
		Curve.InitFrom(BoneContainer);
		UE::Anim::FStackAttributeContainer Attributes;
		FAnimationPoseData PoseData(Pose, Curve, Attributes);
		const double MiddleTime = MeleeWindowStartTime + MeleeWindowDuration * 0.5f;
		AttackSequence->GetAnimationPose(PoseData, FAnimExtractContext(MiddleTime, false));

		FCSPose<FCompactPose> ComponentSpacePose;
		ComponentSpacePose.InitPose(MoveTemp(Pose));
		OutTipLocation = ComponentSpacePose.GetComponentSpaceTransform(BoneContainer.MakeCompactPoseIndex(FMeshPoseBoneIndex(TipBoneIndex))).GetLocation();
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDMeleeSweepLongFrameTest, "TDGame.Combat.MeleeSweep.SweepsBladePathDuringLongFrames", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDMeleeSweepLongFrameTest::RunTest(const FString& Parameters)
{
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, MeleeAttackerMeshPath);
	UAnimSequence* AttackSequence = LoadObject<UAnimSequence>(nullptr, MeleeAttackSequencePath);
	if (!Mesh || !AttackSequence)
	{
		AddError(TEXT("Mannequin mesh or attack sequence asset is missing"));
		return false;
	}

	FVector TipLocation = FVector::ZeroVector;
	if (!SampleBladeTipAtWindowMiddle(Mesh, AttackSequence, TipLocation))
	{
		AddError(TEXT("Could not sample the blade tip location in the middle of the sweep window"));
		return false;
	}
	TestTrue(TEXT("Sampled tip location is away from the actor origin"), TipLocation.Size() > MeleeTargetRadius);

	FTDScopedMeleeWorld Fixture;
	UTDCombatComponent* Enemy = Fixture.SpawnTarget(TipLocation, 2);
	UTDCombatComponent* FarEnemy = Fixture.SpawnTarget(TipLocation + FVector(0.f, 0.f, 1000.f), 2);
	USkeletalMeshComponent* Attacker = Fixture.SpawnAttacker(Mesh, 1);
	TestTrue(TEXT("Sweep begins, advances over a long frame and ends"), SweepAttackWindowWithLongFrame(Attacker, AttackSequence, MakeSweepSettings(10.f)));
	TestEqual(TEXT("Blade path sampled at fixed intervals hits a target once even with half-second frames"), Enemy->GetCurrentHealth(), 990.f);
	TestEqual(TEXT("Targets away from the blade path are untouched"), FarEnemy->GetCurrentHealth(), 1000.f);
	Attacker->GetOwner()->Destroy();
	Enemy->GetOwner()->Destroy();

	UTDCombatComponent* Ally = Fixture.SpawnTarget(TipLocation, 1);
	USkeletalMeshComponent* SecondAttacker = Fixture.SpawnAttacker(Mesh, 1);
	SweepAttackWindowWithLongFrame(SecondAttacker, AttackSequence, MakeSweepSettings(10.f));
	TestEqual(TEXT("Allies on the blade path are not damaged"), Ally->GetCurrentHealth(), 1000.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDMeleeSweepHeightLockTest, "TDGame.Combat.MeleeSweep.LocksBladeHeightToOwner", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDMeleeSweepHeightLockTest::RunTest(const FString& Parameters)
{
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, MeleeAttackerMeshPath);
	UAnimSequence* AttackSequence = LoadObject<UAnimSequence>(nullptr, MeleeAttackSequencePath);
	if (!Mesh || !AttackSequence)
	{
		AddError(TEXT("Mannequin mesh or attack sequence asset is missing"));
		return false;
	}

	FVector TipLocation = FVector::ZeroVector;
	if (!SampleBladeTipAtWindowMiddle(Mesh, AttackSequence, TipLocation))
	{
		AddError(TEXT("Could not sample the blade tip location in the middle of the sweep window"));
		return false;
	}
	TestTrue(TEXT("Sampled tip is clearly above the actor origin so the lock moves it"), TipLocation.Z > MeleeTargetRadius * 4.f);

	constexpr float LockedOffset = 20.f;
	const FVector LockedTipLocation(TipLocation.X, TipLocation.Y, LockedOffset);
	FTDScopedMeleeWorld Fixture;
	UTDCombatComponent* EnemyOnLockedPlane = Fixture.SpawnTarget(LockedTipLocation, 2);
	UTDCombatComponent* EnemyAtRawTip = Fixture.SpawnTarget(TipLocation, 2);
	USkeletalMeshComponent* Attacker = Fixture.SpawnAttacker(Mesh, 1);
	SweepAttackWindowWithLongFrame(Attacker, AttackSequence, MakeSweepSettings(10.f, true, LockedOffset));
	TestEqual(TEXT("Height-locked sweep hits the target on the owner plane"), EnemyOnLockedPlane->GetCurrentHealth(), 990.f);
	TestEqual(TEXT("Height-locked sweep misses the target at the raw animated tip height"), EnemyAtRawTip->GetCurrentHealth(), 1000.f);
	Attacker->GetOwner()->Destroy();
	EnemyOnLockedPlane->GetOwner()->Destroy();
	EnemyAtRawTip->GetOwner()->Destroy();

	UTDCombatComponent* EnemyAtRawTipUnlocked = Fixture.SpawnTarget(TipLocation, 2);
	USkeletalMeshComponent* UnlockedAttacker = Fixture.SpawnAttacker(Mesh, 1);
	SweepAttackWindowWithLongFrame(UnlockedAttacker, AttackSequence, MakeSweepSettings(10.f, false));
	TestEqual(TEXT("Without the lock the raw animated tip height is hit"), EnemyAtRawTipUnlocked->GetCurrentHealth(), 990.f);
	return true;
}

#endif

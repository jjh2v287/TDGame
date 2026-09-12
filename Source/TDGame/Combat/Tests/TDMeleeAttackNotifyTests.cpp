#if WITH_DEV_AUTOMATION_TESTS

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Combat/AnimNotify/TDAnimNotifyState_MeleeAttack.h"
#include "Combat/TDCombatComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"

namespace
{
	constexpr float NotifyStartTime = 0.1f;
	constexpr float NotifyDuration = 0.5f;
	constexpr float TargetRadius = 10.f;
	const FName BladeBaseBoneName(TEXT("lowerarm_r"));
	const FName BladeTipBoneName(TEXT("hand_r"));
	const TCHAR* AttackerMeshPath = TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple");
	const TCHAR* AttackSequencePath = TEXT("/Game/Characters/Mannequins/Anims/Unarmed/Attack/MM_Attack_01.MM_Attack_01");

	struct FTDScopedMeleeWorld
	{
		FTDScopedMeleeWorld()
		{
			const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDMeleeTestWorld"));
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
			Sphere->SetSphereRadius(TargetRadius);
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
			MeshComponent->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
			MeshComponent->SetAnimationMode(EAnimationMode::AnimationSingleNode);
			MeshComponent->RegisterComponent();
			AddCombatant(Actor, TeamId);
			return MeshComponent;
		}

		void Tick(float Duration, float Step)
		{
			for (float Remaining = Duration; Remaining > UE_SMALL_NUMBER;)
			{
				const float Delta = FMath::Min(Remaining, Step);
				++GFrameCounter;
				World->Tick(LEVELTICK_All, Delta);
				Remaining -= Delta;
			}
		}

		UWorld* World = nullptr;
	};

	UAnimMontage* MakeAttackMontage(UAnimSequence* AttackSequence, float DamageAmount, bool bLockHeight = false, float LockedHeightOffset = 0.f)
	{
		UAnimMontage* Montage = UAnimMontage::CreateSlotAnimationAsDynamicMontage(AttackSequence, TEXT("DefaultSlot"), 0.f, 0.f);
		UTDAnimNotifyState_MeleeAttack* Notify = NewObject<UTDAnimNotifyState_MeleeAttack>(Montage);
		Notify->WeaponBaseSocketName = BladeBaseBoneName;
		Notify->WeaponTipSocketName = BladeTipBoneName;
		Notify->SweepChannel = ECC_Pawn;
		Notify->bLockHeightToOwner = bLockHeight;
		Notify->LockedHeightOffset = LockedHeightOffset;
		FTDDamageAction DamageAction;
		DamageAction.Magnitude.Base = DamageAmount;
		FTDDamageRule HitRule;
		HitRule.Event = ETDDamageEvent::Hit;
		HitRule.Actions.Add(DamageAction);
		Notify->HitRules.Add(HitRule);

		const float EndTime = FMath::Min(NotifyStartTime + NotifyDuration, Montage->GetPlayLength() - 0.05f);
		FAnimNotifyEvent& NotifyEvent = Montage->Notifies.AddDefaulted_GetRef();
		NotifyEvent.NotifyName = TEXT("TDMeleeAttack");
		NotifyEvent.NotifyStateClass = Notify;
		NotifyEvent.Link(Montage, NotifyStartTime);
		NotifyEvent.EndLink.Link(Montage, EndTime);
		NotifyEvent.SetDuration(EndTime - NotifyStartTime);
		return Montage;
	}

	bool RecordBladeTipAtWindowMiddle(FTDScopedMeleeWorld& Fixture, USkeletalMesh* Mesh, UAnimSequence* AttackSequence, FVector& OutTipLocation)
	{
		USkeletalMeshComponent* Attacker = Fixture.SpawnAttacker(Mesh, 1);
		UAnimMontage* Montage = MakeAttackMontage(AttackSequence, 0.f);
		Attacker->PlayAnimation(Montage, false);
		const float MiddleTime = NotifyStartTime + NotifyDuration * 0.5f;
		bool bRecorded = false;
		for (int32 Frame = 0; Frame < 400 && !bRecorded; ++Frame)
		{
			Fixture.Tick(0.005f, 0.005f);
			const UAnimInstance* AnimInstance = Attacker->GetAnimInstance();
			if (AnimInstance && AnimInstance->Montage_GetPosition(Montage) >= MiddleTime)
			{
				OutTipLocation = Attacker->GetSocketLocation(BladeTipBoneName);
				bRecorded = true;
			}
		}
		Attacker->GetOwner()->Destroy();
		return bRecorded;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDMeleeAttackNotifySweepTest, "TDGame.Combat.MeleeAttackNotifySweepsBladePathDuringLongFrames", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDMeleeAttackNotifySweepTest::RunTest(const FString& Parameters)
{
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, AttackerMeshPath);
	UAnimSequence* AttackSequence = LoadObject<UAnimSequence>(nullptr, AttackSequencePath);
	if (!Mesh || !AttackSequence)
	{
		AddError(TEXT("Mannequin mesh or attack sequence asset is missing"));
		return false;
	}

	FTDScopedMeleeWorld Fixture;
	FVector TipLocation = FVector::ZeroVector;
	if (!RecordBladeTipAtWindowMiddle(Fixture, Mesh, AttackSequence, TipLocation))
	{
		AddError(TEXT("Could not record the blade tip location during the notify window"));
		return false;
	}
	TestTrue(TEXT("Recorded tip location is away from the actor origin"), TipLocation.Size() > TargetRadius);

	UTDCombatComponent* Enemy = Fixture.SpawnTarget(TipLocation, 2);
	UTDCombatComponent* FarEnemy = Fixture.SpawnTarget(TipLocation + FVector(0.f, 0.f, 1000.f), 2);
	USkeletalMeshComponent* Attacker = Fixture.SpawnAttacker(Mesh, 1);
	Attacker->PlayAnimation(MakeAttackMontage(AttackSequence, 10.f), false);
	Fixture.Tick(1.5f, 0.5f);
	TestEqual(TEXT("Blade path sampled at fixed intervals hits a target once even with half-second frames"), Enemy->GetCurrentHealth(), 990.f);
	TestEqual(TEXT("Targets away from the blade path are untouched"), FarEnemy->GetCurrentHealth(), 1000.f);
	Attacker->GetOwner()->Destroy();
	Enemy->GetOwner()->Destroy();

	UTDCombatComponent* Ally = Fixture.SpawnTarget(TipLocation, 1);
	USkeletalMeshComponent* SecondAttacker = Fixture.SpawnAttacker(Mesh, 1);
	SecondAttacker->PlayAnimation(MakeAttackMontage(AttackSequence, 10.f), false);
	Fixture.Tick(1.5f, 0.5f);
	TestEqual(TEXT("Allies on the blade path are not damaged"), Ally->GetCurrentHealth(), 1000.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDMeleeAttackNotifyHeightLockTest, "TDGame.Combat.MeleeAttackNotifyLocksBladeHeightToOwner", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDMeleeAttackNotifyHeightLockTest::RunTest(const FString& Parameters)
{
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, AttackerMeshPath);
	UAnimSequence* AttackSequence = LoadObject<UAnimSequence>(nullptr, AttackSequencePath);
	if (!Mesh || !AttackSequence)
	{
		AddError(TEXT("Mannequin mesh or attack sequence asset is missing"));
		return false;
	}

	FTDScopedMeleeWorld Fixture;
	FVector TipLocation = FVector::ZeroVector;
	if (!RecordBladeTipAtWindowMiddle(Fixture, Mesh, AttackSequence, TipLocation))
	{
		AddError(TEXT("Could not record the blade tip location during the notify window"));
		return false;
	}
	TestTrue(TEXT("Recorded tip is clearly above the actor origin so the lock moves it"), TipLocation.Z > TargetRadius * 4.f);

	constexpr float LockedOffset = 20.f;
	const FVector LockedTipLocation(TipLocation.X, TipLocation.Y, LockedOffset);
	UTDCombatComponent* EnemyOnLockedPlane = Fixture.SpawnTarget(LockedTipLocation, 2);
	UTDCombatComponent* EnemyAtRawTip = Fixture.SpawnTarget(TipLocation, 2);
	USkeletalMeshComponent* Attacker = Fixture.SpawnAttacker(Mesh, 1);
	Attacker->PlayAnimation(MakeAttackMontage(AttackSequence, 10.f, true, LockedOffset), false);
	Fixture.Tick(1.5f, 0.5f);
	TestEqual(TEXT("Height-locked sweep hits the target on the owner plane"), EnemyOnLockedPlane->GetCurrentHealth(), 990.f);
	TestEqual(TEXT("Height-locked sweep misses the target at the raw animated tip height"), EnemyAtRawTip->GetCurrentHealth(), 1000.f);
	Attacker->GetOwner()->Destroy();
	EnemyOnLockedPlane->GetOwner()->Destroy();
	EnemyAtRawTip->GetOwner()->Destroy();

	UTDCombatComponent* EnemyAtRawTipUnlocked = Fixture.SpawnTarget(TipLocation, 2);
	USkeletalMeshComponent* UnlockedAttacker = Fixture.SpawnAttacker(Mesh, 1);
	UnlockedAttacker->PlayAnimation(MakeAttackMontage(AttackSequence, 10.f, false), false);
	Fixture.Tick(1.5f, 0.5f);
	TestEqual(TEXT("Without the lock the raw animated tip height is hit"), EnemyAtRawTipUnlocked->GetCurrentHealth(), 990.f);
	return true;
}

#endif

#if WITH_DEV_AUTOMATION_TESTS

#include "AssetCompilingManager.h"
#include "Characters/TDMonsterCharacter.h"
#include "Combat/Feedback/TDCombatFeedbackSettings.h"
#include "Combat/Feedback/TDCombatFeedbackSubsystem.h"
#include "Combat/TDCombatComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/AssetManager.h"
#include "Engine/CollisionProfile.h"
#include "Engine/Engine.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "MonsterAI/TDMonsterSpeciesAsset.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "Sound/SoundConcurrency.h"
#include "UObject/Package.h"

namespace TDDeathRagdollWorldTestsInternal
{
	constexpr float StepSeconds = 1.f / 60.f;
	constexpr float FloorHalfThickness = 50.f;
	constexpr float SpawnClearance = 5.f;
	constexpr float SettleSeconds = 0.5f;
	constexpr float SlowTimeDilation = 0.2f;
	constexpr float MaxRagdollDeferSeconds = 1.5f;
	constexpr float KillDamage = 100000.f;
	constexpr int32 FramesToEnterAfterSlowTime = 2;
	constexpr float MaxPosePushCheckSeconds = 3.5f;
	constexpr float PosePushCheckSinkClearanceSeconds = 0.5f;
	constexpr int32 MinAwakePosePushFrames = 30;
	constexpr int32 FramesUntilPosePushStops = 2;
	constexpr int32 FramesUntilAnimationOutputStops = 2;
	constexpr int32 OffscreenPosePushFrames = 6;
	const TCHAR* const GoblinSpeciesPath = TEXT("/Game/MonsterAI/Species/DA_TDMonster_Goblin.DA_TDMonster_Goblin");

	int32 SecondsToSteps(const float Seconds)
	{
		return FMath::Max(1, FMath::RoundToInt(Seconds / StepSeconds));
	}

	void IgnoreEngineInjectionEnsures(FAutomationTestBase& Test)
	{
		Test.AddExpectedError(TEXT("TimelineTrait.GetState(Context, ChildState)"), EAutomationExpectedErrorFlags::Contains, -1, false);
		Test.AddExpectedError(TEXT("Event.SerialNumber != 0"), EAutomationExpectedErrorFlags::Contains, -1, false);
		Test.AddExpectedError(TEXT("=== Handled ensure: ==="), EAutomationExpectedErrorFlags::Contains, -1, false);
		Test.AddExpectedError(TEXT("Stack:"), EAutomationExpectedErrorFlags::Contains, -1, false);
		Test.AddExpectedError(TEXT("[Callstack]"), EAutomationExpectedErrorFlags::Contains, -1, false);
		Test.AddExpectedError(TEXT(""), EAutomationExpectedErrorFlags::Exact, -1, false);
	}

	struct FTDScopedImmediateRagdoll
	{
		FTDScopedImmediateRagdoll()
			: Variable(IConsoleManager::Get().FindConsoleVariable(TEXT("TD.Death.ImmediateRagdoll")))
		{
			if (!Variable)
			{
				return;
			}
			PreviousValue = Variable->GetInt();
			Variable->Set(1, ECVF_SetByCode);
		}

		~FTDScopedImmediateRagdoll()
		{
			if (Variable)
			{
				Variable->Set(PreviousValue, ECVF_SetByCode);
			}
		}

		IConsoleVariable* Variable = nullptr;
		int32 PreviousValue = 1;
	};

	struct FTDDeathRagdollTestWorld
	{
		FTDDeathRagdollTestWorld()
		{
			const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDDeathRagdollTestWorld"));
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true);
			SpawnFloor();
		}

		~FTDDeathRagdollTestWorld()
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

		UTDMonsterSpeciesAsset* LoadGoblinSpecies()
		{
			const FSoftObjectPath SpeciesPath(GoblinSpeciesPath);
			SpeciesHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(SpeciesPath);
			if (SpeciesHandle.IsValid())
			{
				SpeciesHandle->WaitUntilComplete();
			}
			FAssetCompilingManager::Get().FinishAllCompilation();
			return Cast<UTDMonsterSpeciesAsset>(SpeciesPath.ResolveObject());
		}

		ATDMonsterCharacter* SpawnMonster(UTDMonsterSpeciesAsset& Species, const FVector& Location)
		{
			const FTransform SpawnTransform(FRotator::ZeroRotator, Location + FVector(0.f, 0.f, Species.CapsuleHalfHeight + SpawnClearance));
			ATDMonsterCharacter* Monster = World->SpawnActorDeferred<ATDMonsterCharacter>(ATDMonsterCharacter::StaticClass(), SpawnTransform);
			Monster->SetSpecies(&Species);
			Monster->FinishSpawning(SpawnTransform);
			return Monster;
		}

		UTDCombatComponent* SpawnAttacker(const FVector& Location)
		{
			AActor* Actor = World->SpawnActor<AActor>();
			USphereComponent* Sphere = NewObject<USphereComponent>(Actor);
			Actor->AddInstanceComponent(Sphere);
			Actor->SetRootComponent(Sphere);
			Sphere->SetSphereRadius(30.f);
			Sphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Sphere->RegisterComponent();
			Actor->SetActorLocation(Location);
			UTDCombatComponent* Combatant = NewObject<UTDCombatComponent>(Actor);
			Actor->AddInstanceComponent(Combatant);
			FTDCombatStats Stats;
			Stats.TeamId = 1;
			Stats.BaseMaxHealth = 1000.f;
			Combatant->SetStats(Stats);
			Combatant->RegisterComponent();
			return Combatant;
		}

		void SetTimeDilation(const float TimeDilation)
		{
			World->GetWorldSettings()->SetTimeDilation(TimeDilation);
		}

		void Step()
		{
			++GFrameCounter;
			World->Tick(LEVELTICK_All, StepSeconds);
		}

		void Tick(const float Seconds)
		{
			for (int32 StepIndex = SecondsToSteps(Seconds); StepIndex > 0; --StepIndex)
			{
				Step();
			}
		}

		void StepWithMeshRendered(UPrimitiveComponent& MeshComponent)
		{
			MeshComponent.SetLastRenderTime(World->GetTimeSeconds());
			Step();
		}

		void TickWithMeshRendered(UPrimitiveComponent& MeshComponent, const float Seconds)
		{
			for (int32 StepIndex = SecondsToSteps(Seconds); StepIndex > 0; --StepIndex)
			{
				StepWithMeshRendered(MeshComponent);
			}
		}

		void FinalizeRenderedBonesInNewFrame(USkeletalMeshComponent& MeshComponent)
		{
			++GFrameCounter;
			MeshComponent.SetLastRenderTime(World->GetTimeSeconds());
			MeshComponent.FinalizeBoneTransform();
		}

		UWorld* World = nullptr;
		TSharedPtr<FStreamableHandle> SpeciesHandle;
	};

	void KillMonster(ATDMonsterCharacter& Monster, const UTDCombatComponent& Attacker)
	{
		FTDDamageContext Context;
		Context.Caster = Attacker.GetOwner();
		Context.Stats = Attacker.GetStats();
		Monster.GetCombatComponent()->ReceiveDamage(KillDamage, ETDDamageElement::Physical, false, Context);
	}

	bool IsRagdollWaiting(const ATDMonsterCharacter& Monster)
	{
		return Monster.IsRagdollPendingOrActive() && !Monster.IsRagdolling();
	}

	bool DoesPosePushCheckEndBeforeSink(const UTDMonsterSpeciesAsset* Species)
	{
		if (!Species || Species->CorpseLifeSeconds <= 0.f || Species->CorpseSinkSeconds <= 0.f)
		{
			return true;
		}
		return MaxPosePushCheckSeconds + PosePushCheckSinkClearanceSeconds <= Species->CorpseLifeSeconds - Species->CorpseSinkSeconds;
	}

	void TestPosePushStopsWhileAsleep(FAutomationTestBase& Test, FTDDeathRagdollTestWorld& Fixture, const ATDMonsterCharacter& Monster)
	{
		USkeletalMeshComponent& MonsterMesh = *Monster.GetMesh();
		if (!Test.TestFalse(FString::Printf(TEXT("The goblin corpse falls asleep within %.1f seconds of the kill, before it starts sinking"), MaxPosePushCheckSeconds), MonsterMesh.IsAnyRigidBodyAwake()))
		{
			return;
		}
		Fixture.TickWithMeshRendered(MonsterMesh, FramesUntilPosePushStops * StepSeconds);
		const int64 SleepingRevision = Monster.GetMeshBoneRevisionForTest();
		Fixture.TickWithMeshRendered(MonsterMesh, FramesUntilPosePushStops * StepSeconds);
		Test.TestEqual(TEXT("A rendered sleeping ragdoll stops pushing its unchanged pose after one more frame"), Monster.GetMeshBoneRevisionForTest(), SleepingRevision);
	}

	ATDMonsterCharacter* SpawnSettledGoblin(FAutomationTestBase& Test, FTDDeathRagdollTestWorld& Fixture, const FVector& Location)
	{
		UTDMonsterSpeciesAsset* Species = Fixture.LoadGoblinSpecies();
		if (!Test.TestNotNull(TEXT("The goblin species loads"), Species))
		{
			return nullptr;
		}
		ATDMonsterCharacter* Monster = Fixture.SpawnMonster(*Species, Location);
		if (!Test.TestNotNull(TEXT("The goblin spawns"), Monster) || !Test.TestNotNull(TEXT("The goblin mesh uses a physics asset"), Monster->GetMesh()->GetPhysicsAsset()))
		{
			return nullptr;
		}
		Fixture.Tick(SettleSeconds);
		return Monster;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDDeathRagdollWaitsForNormalTimeTest, "TDGame.Combat.DeathRagdoll.World.WaitsForNormalTime", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDDeathRagdollWaitsForNormalTimeTest::RunTest(const FString& Parameters)
{
	using namespace TDDeathRagdollWorldTestsInternal;
	IgnoreEngineInjectionEnsures(*this);
	const FTDScopedImmediateRagdoll ImmediateRagdoll;
	FTDDeathRagdollTestWorld Fixture;
	ATDMonsterCharacter* Monster = SpawnSettledGoblin(*this, Fixture, FVector::ZeroVector);
	UTDCombatComponent* Attacker = Fixture.SpawnAttacker(FVector(-200.f, 0.f, 90.f));
	if (!Monster || !TestNotNull(TEXT("The attacker spawns"), Attacker))
	{
		return false;
	}

	Fixture.SetTimeDilation(SlowTimeDilation);
	KillMonster(*Monster, *Attacker);
	TestFalse(TEXT("The blow kills the goblin"), Monster->GetCombatComponent()->IsAlive());
	TestTrue(TEXT("A kill under 0.2 time dilation waits before the ragdoll"), IsRagdollWaiting(*Monster));
	TestTrue(TEXT("A waiting ragdoll keeps its retry timer running"), Monster->HasActiveDeathLaunchTimersForTest());
	Fixture.Tick(0.3f);
	TestTrue(TEXT("The ragdoll keeps waiting while time stays at 0.2"), IsRagdollWaiting(*Monster));
	TestFalse(TEXT("The waiting body does not simulate physics yet"), Monster->GetMesh()->IsSimulatingPhysics());

	Fixture.SetTimeDilation(1.f);
	for (int32 FrameIndex = 0; FrameIndex < FramesToEnterAfterSlowTime; ++FrameIndex)
	{
		Fixture.Step();
	}
	TestTrue(TEXT("The ragdoll starts within two frames of time returning to normal"), Monster->IsRagdolling());
	TestTrue(TEXT("The started ragdoll simulates physics"), Monster->GetMesh()->IsSimulatingPhysics());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDDeathRagdollDeferLimitTest, "TDGame.Combat.DeathRagdoll.World.DeferLimit", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDDeathRagdollDeferLimitTest::RunTest(const FString& Parameters)
{
	using namespace TDDeathRagdollWorldTestsInternal;
	IgnoreEngineInjectionEnsures(*this);
	const FTDScopedImmediateRagdoll ImmediateRagdoll;
	FTDDeathRagdollTestWorld Fixture;
	ATDMonsterCharacter* Monster = SpawnSettledGoblin(*this, Fixture, FVector::ZeroVector);
	UTDCombatComponent* Attacker = Fixture.SpawnAttacker(FVector(-200.f, 0.f, 90.f));
	if (!Monster || !TestNotNull(TEXT("The attacker spawns"), Attacker))
	{
		return false;
	}

	Fixture.SetTimeDilation(SlowTimeDilation);
	const double KillRealSeconds = Fixture.World->GetRealTimeSeconds();
	KillMonster(*Monster, *Attacker);
	TestTrue(TEXT("A kill under 0.2 time dilation waits before the ragdoll"), IsRagdollWaiting(*Monster));
	while (Fixture.World->GetRealTimeSeconds() - KillRealSeconds < MaxRagdollDeferSeconds - 2.f * StepSeconds)
	{
		Fixture.Step();
	}
	TestTrue(TEXT("The ragdoll still waits just before 1.5 real seconds"), IsRagdollWaiting(*Monster));
	Fixture.Tick(4.f * StepSeconds);
	TestTrue(FString::Printf(TEXT("The ragdoll starts by 1.5 real seconds even though time stays at 0.2 (waited %.3f s)"), Fixture.World->GetRealTimeSeconds() - KillRealSeconds), Monster->IsRagdolling());
	Fixture.SetTimeDilation(1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDDeathRagdollEndPlayStopsTimersTest, "TDGame.Combat.DeathRagdoll.World.EndPlayStopsTimers", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDDeathRagdollEndPlayStopsTimersTest::RunTest(const FString& Parameters)
{
	using namespace TDDeathRagdollWorldTestsInternal;
	IgnoreEngineInjectionEnsures(*this);
	const FTDScopedImmediateRagdoll ImmediateRagdoll;
	FTDDeathRagdollTestWorld Fixture;
	ATDMonsterCharacter* FallingMonster = SpawnSettledGoblin(*this, Fixture, FVector::ZeroVector);
	ATDMonsterCharacter* WaitingMonster = SpawnSettledGoblin(*this, Fixture, FVector(600.f, 0.f, 0.f));
	UTDCombatComponent* Attacker = Fixture.SpawnAttacker(FVector(-200.f, 0.f, 90.f));
	if (!FallingMonster || !WaitingMonster || !TestNotNull(TEXT("The attacker spawns"), Attacker))
	{
		return false;
	}

	KillMonster(*FallingMonster, *Attacker);
	TestTrue(TEXT("A kill at normal time starts the ragdoll at once"), FallingMonster->IsRagdolling());
	TestTrue(TEXT("A started ragdoll pushes its physics pose to the renderer"), FallingMonster->HasRagdollPosePushForTest());
	Fixture.Tick(0.1f);
	TestTrue(TEXT("The falling corpse keeps its launch watch or landing bite running"), FallingMonster->HasActiveDeathLaunchTimersForTest());
	FallingMonster->Destroy();
	TestFalse(TEXT("EndPlay stops the launch watch and the landing bite"), FallingMonster->HasActiveDeathLaunchTimersForTest());
	TestFalse(TEXT("EndPlay removes the ragdoll pose push from the mesh"), FallingMonster->HasRagdollPosePushForTest());

	Fixture.SetTimeDilation(SlowTimeDilation);
	KillMonster(*WaitingMonster, *Attacker);
	TestTrue(TEXT("A kill under 0.2 time dilation waits before the ragdoll"), IsRagdollWaiting(*WaitingMonster));
	TestTrue(TEXT("The waiting corpse keeps its retry timer running"), WaitingMonster->HasActiveDeathLaunchTimersForTest());
	WaitingMonster->Destroy();
	TestFalse(TEXT("EndPlay stops the ragdoll retry timer"), WaitingMonster->HasActiveDeathLaunchTimersForTest());
	TestFalse(TEXT("EndPlay leaves no ragdoll waiting"), WaitingMonster->IsRagdollPendingOrActive());
	Fixture.SetTimeDilation(1.f);
	Fixture.Tick(0.1f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDDeathRagdollPushesPoseEveryFrameTest, "TDGame.Combat.DeathRagdoll.World.PushesPoseEveryFrame", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FTDDeathRagdollPushesPoseEveryFrameTest::RunTest(const FString& Parameters)
{
	using namespace TDDeathRagdollWorldTestsInternal;
	IgnoreEngineInjectionEnsures(*this);
	const FTDScopedImmediateRagdoll ImmediateRagdoll;
	FTDDeathRagdollTestWorld Fixture;
	ATDMonsterCharacter* Monster = SpawnSettledGoblin(*this, Fixture, FVector::ZeroVector);
	UTDCombatComponent* Attacker = Fixture.SpawnAttacker(FVector(-200.f, 0.f, 90.f));
	if (!Monster || !TestNotNull(TEXT("The attacker spawns"), Attacker))
	{
		return false;
	}
	USkeletalMeshComponent* MonsterMesh = Monster->GetMesh();
	if (!TestNotNull(TEXT("The goblin mesh has a render mesh object to skin"), MonsterMesh->GetMeshObject())
		|| !TestTrue(TEXT("The pose push check window ends before the goblin corpse starts sinking"), DoesPosePushCheckEndBeforeSink(Monster->GetSpecies())))
	{
		return false;
	}

	KillMonster(*Monster, *Attacker);
	if (!TestTrue(TEXT("A kill at normal time starts the ragdoll at once"), Monster->IsRagdolling()))
	{
		return false;
	}
	int32 AwakeFrameCount = 0;
	int32 UnchangedAwakeFrameCount = 0;
	int64 PreviousRevision = Monster->GetMeshBoneRevisionForTest();
	for (int32 StepIndex = SecondsToSteps(MaxPosePushCheckSeconds); StepIndex > 0 && MonsterMesh->IsAnyRigidBodyAwake(); --StepIndex)
	{
		Fixture.StepWithMeshRendered(*MonsterMesh);
		const int64 Revision = Monster->GetMeshBoneRevisionForTest();
		++AwakeFrameCount;
		UnchangedAwakeFrameCount += Revision > PreviousRevision ? 0 : 1;
		PreviousRevision = Revision;
	}
	TestTrue(FString::Printf(TEXT("The launched corpse stays awake for at least %d frames (awake %d)"), MinAwakePosePushFrames, AwakeFrameCount), AwakeFrameCount >= MinAwakePosePushFrames);
	TestEqual(FString::Printf(TEXT("Every awake rendered ragdoll frame raises the bone revision so the renderer skins the new physics pose (unchanged %d of %d)"), UnchangedAwakeFrameCount, AwakeFrameCount), UnchangedAwakeFrameCount, 0);

	TestPosePushStopsWhileAsleep(*this, Fixture, *Monster);

	Monster->Destroy();
	TestFalse(TEXT("EndPlay removes the ragdoll pose push from the mesh"), Monster->HasRagdollPosePushForTest());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDDeathRagdollPushesPoseOnlyWhileRenderedTest, "TDGame.Combat.DeathRagdoll.World.PushesPoseOnlyWhileRendered", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FTDDeathRagdollPushesPoseOnlyWhileRenderedTest::RunTest(const FString& Parameters)
{
	using namespace TDDeathRagdollWorldTestsInternal;
	IgnoreEngineInjectionEnsures(*this);
	const FTDScopedImmediateRagdoll ImmediateRagdoll;
	FTDDeathRagdollTestWorld Fixture;
	ATDMonsterCharacter* Monster = SpawnSettledGoblin(*this, Fixture, FVector::ZeroVector);
	UTDCombatComponent* Attacker = Fixture.SpawnAttacker(FVector(-200.f, 0.f, 90.f));
	if (!Monster || !TestNotNull(TEXT("The attacker spawns"), Attacker))
	{
		return false;
	}
	USkeletalMeshComponent* MonsterMesh = Monster->GetMesh();
	if (!TestNotNull(TEXT("The goblin mesh has a render mesh object to skin"), MonsterMesh->GetMeshObject()))
	{
		return false;
	}

	KillMonster(*Monster, *Attacker);
	if (!TestTrue(TEXT("A kill at normal time starts the ragdoll at once"), Monster->IsRagdolling()))
	{
		return false;
	}
	Fixture.Tick(FramesUntilAnimationOutputStops * StepSeconds);
	const int64 OffscreenRevision = Monster->GetMeshBoneRevisionForTest();
	Fixture.Tick(OffscreenPosePushFrames * StepSeconds);
	if (!TestTrue(TEXT("The corpse is still falling after the off screen frames"), MonsterMesh->IsAnyRigidBodyAwake()))
	{
		return false;
	}
	TestEqual(TEXT("An awake corpse that is not rendered keeps its bone revision instead of pushing every pose"), Monster->GetMeshBoneRevisionForTest(), OffscreenRevision);
	Fixture.StepWithMeshRendered(*MonsterMesh);
	TestTrue(TEXT("The first rendered frame pushes the pose that changed off screen"), Monster->GetMeshBoneRevisionForTest() > OffscreenRevision);

	const int64 RevisionBeforeEndPlay = Monster->GetMeshBoneRevisionForTest();
	Fixture.FinalizeRenderedBonesInNewFrame(*MonsterMesh);
	TestTrue(TEXT("Finalizing the bones of an awake rendered corpse pushes its pose"), Monster->GetMeshBoneRevisionForTest() > RevisionBeforeEndPlay);
	Monster->RouteEndPlay(EEndPlayReason::Destroyed);
	TestFalse(TEXT("EndPlay removes the ragdoll pose push from the mesh"), Monster->HasRagdollPosePushForTest());
	TestTrue(TEXT("The mesh stays registered and awake after EndPlay, so a leftover pose push would still fire"), MonsterMesh->IsRegistered() && MonsterMesh->IsAnyRigidBodyAwake());
	const int64 RevisionAfterEndPlay = Monster->GetMeshBoneRevisionForTest();
	Fixture.FinalizeRenderedBonesInNewFrame(*MonsterMesh);
	TestEqual(TEXT("Finalizing the bones of the mesh after EndPlay no longer raises the bone revision"), Monster->GetMeshBoneRevisionForTest(), RevisionAfterEndPlay);
	Monster->Destroy();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDDeathRagdollMultiKillIndexTest, "TDGame.Combat.DeathRagdoll.World.MultiKillIndex", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDDeathRagdollMultiKillIndexTest::RunTest(const FString& Parameters)
{
	using namespace TDDeathRagdollWorldTestsInternal;
	FTDDeathRagdollTestWorld Fixture;
	UTDCombatFeedbackSubsystem* Feedback = NewObject<UTDCombatFeedbackSubsystem>(Fixture.World);
	const AActor* Attacker = Fixture.World->SpawnActor<AActor>();
	const AActor* OtherAttacker = Fixture.World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("The feedback subsystem exists"), Feedback) || !TestNotNull(TEXT("The attacker exists"), Attacker) || !TestNotNull(TEXT("The other attacker exists"), OtherAttacker))
	{
		return false;
	}

	TestEqual(TEXT("The first kill of an attacker has index 0"), Feedback->RecordAttackerKill(*Attacker), 0);
	TestEqual(TEXT("The second kill in the same window has index 1"), Feedback->RecordAttackerKill(*Attacker), 1);
	TestEqual(TEXT("The third kill in the same window has index 2"), Feedback->RecordAttackerKill(*Attacker), 2);
	TestEqual(TEXT("Another attacker keeps its own window"), Feedback->RecordAttackerKill(*OtherAttacker), 0);
	Fixture.Step();
	TestEqual(TEXT("A kill one frame later stays in the window"), Feedback->RecordAttackerKill(*Attacker), 3);

	const float WindowSeconds = FMath::Max(GetDefault<UTDCombatFeedbackSettings>()->AttackerKillHitStopSeconds, 0.f) + 0.05f;
	Fixture.Tick(WindowSeconds + 4.f * StepSeconds);
	TestEqual(TEXT("A kill after the window starts a new window at index 0"), Feedback->RecordAttackerKill(*Attacker), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDDeathRagdollCorpseLandSoundGroupTest, "TDGame.Combat.DeathRagdoll.CorpseLandSoundGroup", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDDeathRagdollCorpseLandSoundGroupTest::RunTest(const FString& Parameters)
{
	UTDCombatFeedbackSettings* FeedbackSettings = NewObject<UTDCombatFeedbackSettings>(GetTransientPackage());
	FeedbackSettings->ImpactSoundMaxCount = 10;
	FeedbackSettings->CorpseLandSoundMaxCount = 4;
	UTDCombatFeedbackSubsystem* Feedback = NewObject<UTDCombatFeedbackSubsystem>(GetTransientPackage());
	Feedback->CreateSoundSettings(*FeedbackSettings);
	const USoundConcurrency* Impact = Feedback->GetSoundConcurrency(ETDFeedbackSoundChannel::Impact);
	const USoundConcurrency* CorpseLand = Feedback->GetSoundConcurrency(ETDFeedbackSoundChannel::CorpseLand);
	if (!TestNotNull(TEXT("Impact sounds have a concurrency group"), Impact) || !TestNotNull(TEXT("Corpse landings have a concurrency group"), CorpseLand))
	{
		return false;
	}
	TestTrue(TEXT("Corpse landings do not share the impact concurrency group"), CorpseLand != Impact);
	TestEqual(TEXT("Corpse landings allow four voices"), CorpseLand->Concurrency.MaxCount, 4);
	TestTrue(TEXT("Corpse landings stop the oldest voice when full"), CorpseLand->Concurrency.ResolutionRule == EMaxConcurrentResolutionRule::StopOldest);
	return true;
}

#endif

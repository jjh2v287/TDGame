#if WITH_DEV_AUTOMATION_TESTS

#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "FluidFX/TDFluidBrushModel.h"
#include "FluidFX/TDFluidGrid2DSolver.h"
#include "FluidFX/TDFluidInputActorFilter.h"
#include "FluidFX/TDFluidInputPersistence.h"
#include "FluidFX/TDFluidLiveActor.h"
#include "FluidFX/TDFluidLiveComponent.h"
#include "FluidFX/TDFluidLiveSubsystem.h"
#include "FluidFX/TDFluidPlaneBasis.h"
#include "FluidFX/TDFluidPresetAsset.h"
#include "FluidFX/TDFluidTypes.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"

namespace TDFluidInputHarvestTestPrivate
{
	constexpr float HarvestStepSeconds = 0.05f;
	constexpr float HarvestRadiusTolerance = 1.0e-4f;
	const FVector HarvestMoveOffset(20.0, 0.0, 0.0);

	struct FTDFluidHarvestTestWorld
	{
		FTDFluidHarvestTestWorld()
		{
			const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDFluidHarvestTestWorld"));
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true);
		}

		~FTDFluidHarvestTestWorld()
		{
			if (World)
			{
				World->EndPlay(EEndPlayReason::Quit);
				World->DestroyWorld(false);
				GEngine->DestroyWorldContext(World);
			}
		}

		UWorld* World = nullptr;
	};

	ATDFluidLiveActor* SpawnHarvestLiveActor(UWorld* World, const FVector& Location, ETDFluidPresetKind Kind = ETDFluidPresetKind::Smoke)
	{
		ATDFluidLiveActor* LiveActor = World->SpawnActor<ATDFluidLiveActor>(ATDFluidLiveActor::StaticClass(), Location, FRotator::ZeroRotator);
		if (!LiveActor)
		{
			return nullptr;
		}

		UTDFluidPresetAsset* Preset = UTDFluidPresetAsset::CreateTransientPreset(World, Kind);
		Preset->bOverridePresetDefaults = true;
		Preset->SimulationConfig.GridResolution = 64;
		Preset->SimulationConfig.SimExtentCm = FVector2D(2000.0f, 2000.0f);
		Preset->SimulationConfig.ActorBrushDensityStrength = 1.0f;
		Preset->SimulationConfig.ActorBrushPunctureCmPerSec = 0.0f;
		LiveActor->InitializeFromPreset(Preset, 0.0f);
		return LiveActor;
	}

	AActor* SpawnBoxActor(UWorld* World, const FVector& Location, const FVector& BoxExtent, ECollisionChannel ObjectType = ECC_PhysicsBody)
	{
		AActor* Actor = World->SpawnActor<AActor>();
		if (!Actor)
		{
			return nullptr;
		}

		UBoxComponent* Box = NewObject<UBoxComponent>(Actor);
		Actor->AddInstanceComponent(Box);
		Actor->SetRootComponent(Box);
		Box->SetBoxExtent(BoxExtent);
		Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Box->SetCollisionObjectType(ObjectType);
		Box->SetCollisionResponseToAllChannels(ECR_Overlap);
		Box->SetGenerateOverlapEvents(true);
		Box->RegisterComponent();
		Actor->SetActorLocation(Location);
		return Actor;
	}

	void UpdateInputBrushSettings(ATDFluidLiveActor& LiveActor, TFunctionRef<void(FTDFluidInputBrushSettings&)> Mutate)
	{
		FTDFluidInputBrushSettings Settings = LiveActor.LiveComponent->GetInputBrushSettings();
		Mutate(Settings);
		LiveActor.LiveComponent->SetInputBrushSettingsForTests(Settings);
	}

	TArray<FTDFluidBrushStamp> CollectStampsSince(const ATDFluidLiveActor& LiveActor, int32 StartIndex, ETDFluidBrushSource Source)
	{
		TArray<FTDFluidBrushStamp> Stamps;
		const TArray<FTDFluidBrushStamp>& Pending = LiveActor.LiveComponent->GetPendingBrushStamps();
		for (int32 Index = StartIndex; Index < Pending.Num(); ++Index)
		{
			if (Pending[Index].Source == Source)
			{
				Stamps.Add(Pending[Index]);
			}
		}
		return Stamps;
	}

	TArray<FTDFluidBrushStamp> HarvestPivotStampsAfterMove(ATDFluidLiveActor& LiveActor, const TArray<AActor*>& Movers)
	{
		LiveActor.HarvestInteractionsForTests(HarvestStepSeconds);
		const int32 StartIndex = LiveActor.LiveComponent->GetPendingBrushStamps().Num();
		for (AActor* Mover : Movers)
		{
			Mover->AddActorWorldOffset(HarvestMoveOffset);
		}
		LiveActor.HarvestInteractionsForTests(HarvestStepSeconds);
		return CollectStampsSince(LiveActor, StartIndex, ETDFluidBrushSource::Pivot);
	}

	const FTDFluidBrushStamp* FindStampNearX(const TArray<FTDFluidBrushStamp>& Stamps, double LocationX)
	{
		return Stamps.FindByPredicate([LocationX](const FTDFluidBrushStamp& Stamp) { return FMath::Abs(Stamp.WorldLocation.X - LocationX) < 1.0; });
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidHarvestBoundsRadiusTest,
	"TDGame.FluidFX.InputHarvest.BoundsDoubleDoublesRadius",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidHarvestBoundsRadiusTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidInputHarvestTestPrivate;

	FTDFluidHarvestTestWorld TestWorld;
	ATDFluidLiveActor* LiveActor = SpawnHarvestLiveActor(TestWorld.World, FVector::ZeroVector);
	AActor* SmallBox = SpawnBoxActor(TestWorld.World, FVector(-300.0, 0.0, 50.0), FVector(50.0, 50.0, 20.0));
	AActor* LargeBox = SpawnBoxActor(TestWorld.World, FVector(300.0, 0.0, 50.0), FVector(100.0, 100.0, 20.0));
	if (!TestNotNull(TEXT("Spawned live actor"), LiveActor) || !TestNotNull(TEXT("Spawned small box"), SmallBox) || !TestNotNull(TEXT("Spawned large box"), LargeBox))
	{
		return false;
	}
	LiveActor->RegisterInteractingActor(SmallBox);
	LiveActor->RegisterInteractingActor(LargeBox);

	const TArray<FTDFluidBrushStamp> FixedStamps = HarvestPivotStampsAfterMove(*LiveActor, { SmallBox, LargeBox });
	const FTDFluidBrushStamp* FixedSmall = FindStampNearX(FixedStamps, -280.0);
	const FTDFluidBrushStamp* FixedLarge = FindStampNearX(FixedStamps, 320.0);
	if (!TestTrue(TEXT("Fixed mode stamps both pivots"), FixedSmall && FixedLarge))
	{
		return false;
	}
	TestNearlyEqual(TEXT("Fixed mode ignores the box size"), FixedLarge->RadiusCm, FixedSmall->RadiusCm, HarvestRadiusTolerance);

	UpdateInputBrushSettings(*LiveActor, [](FTDFluidInputBrushSettings& Settings) { Settings.ObjectSizeMode = ETDFluidObjectSizeMode::Bounds; });
	const TArray<FTDFluidBrushStamp> BoundsStamps = HarvestPivotStampsAfterMove(*LiveActor, { SmallBox, LargeBox });
	const FTDFluidBrushStamp* BoundsSmall = FindStampNearX(BoundsStamps, -260.0);
	const FTDFluidBrushStamp* BoundsLarge = FindStampNearX(BoundsStamps, 340.0);
	if (!TestTrue(TEXT("Bounds mode stamps both pivots"), BoundsSmall && BoundsLarge))
	{
		return false;
	}
	TestNearlyEqual(TEXT("Bounds mode radius equals the half width of the small box"), BoundsSmall->RadiusCm, 50.0f, 1.0e-2f);
	TestNearlyEqual(TEXT("A box twice as wide gives twice the radius"), BoundsLarge->RadiusCm / BoundsSmall->RadiusCm, 2.0f, HarvestRadiusTolerance);

	UpdateInputBrushSettings(*LiveActor, [](FTDFluidInputBrushSettings& Settings) { Settings.ObjectSizeMode = ETDFluidObjectSizeMode::ActorScale; });
	LargeBox->SetActorScale3D(FVector(2.0, 2.0, 1.0));
	const TArray<FTDFluidBrushStamp> ScaleStamps = HarvestPivotStampsAfterMove(*LiveActor, { SmallBox, LargeBox });
	const FTDFluidBrushStamp* ScaleSmall = FindStampNearX(ScaleStamps, -240.0);
	const FTDFluidBrushStamp* ScaleLarge = FindStampNearX(ScaleStamps, 360.0);
	if (!TestTrue(TEXT("Actor scale mode stamps both pivots"), ScaleSmall && ScaleLarge))
	{
		return false;
	}
	TestNearlyEqual(TEXT("Actor scale 2 doubles the fixed radius"), ScaleLarge->RadiusCm, 2.0f * FixedSmall->RadiusCm, HarvestRadiusTolerance);
	TestNearlyEqual(TEXT("Actor scale 1 keeps the fixed radius"), ScaleSmall->RadiusCm, FixedSmall->RadiusCm, HarvestRadiusTolerance);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidHarvestHardnessScalesTest,
	"TDGame.FluidFX.InputHarvest.HardnessScalesWithObjectSize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidHarvestHardnessScalesTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidInputHarvestTestPrivate;

	FTDFluidHarvestTestWorld TestWorld;
	ATDFluidLiveActor* LiveActor = SpawnHarvestLiveActor(TestWorld.World, FVector::ZeroVector);
	AActor* UnitBox = SpawnBoxActor(TestWorld.World, FVector(-300.0, 0.0, 50.0), FVector(30.0));
	AActor* DoubleBox = SpawnBoxActor(TestWorld.World, FVector(300.0, 0.0, 50.0), FVector(30.0));
	if (!TestNotNull(TEXT("Spawned live actor"), LiveActor) || !TestNotNull(TEXT("Spawned unit box"), UnitBox) || !TestNotNull(TEXT("Spawned double box"), DoubleBox))
	{
		return false;
	}
	DoubleBox->SetActorScale3D(FVector(2.0, 2.0, 1.0));
	LiveActor->RegisterInteractingActor(UnitBox);
	LiveActor->RegisterInteractingActor(DoubleBox);

	UpdateInputBrushSettings(*LiveActor, [](FTDFluidInputBrushSettings& Settings)
	{
		Settings.ObjectSizeMode = ETDFluidObjectSizeMode::ActorScale;
		Settings.bHardnessScalesWithObjectSize = true;
		Settings.BrushHardness = 0.3f;
	});
	const TArray<FTDFluidBrushStamp> Stamps = HarvestPivotStampsAfterMove(*LiveActor, { UnitBox, DoubleBox });
	const FTDFluidBrushStamp* UnitStamp = FindStampNearX(Stamps, -280.0);
	const FTDFluidBrushStamp* DoubleStamp = FindStampNearX(Stamps, 320.0);
	if (!TestTrue(TEXT("Both pivots stamp"), UnitStamp && DoubleStamp))
	{
		return false;
	}
	TestNearlyEqual(TEXT("Size ratio 1 keeps the brush hardness"), UnitStamp->ShapeHardness, 0.3f, 1.0e-5f);
	TestNearlyEqual(TEXT("Size ratio 2 doubles the hardness"), DoubleStamp->ShapeHardness, 0.6f, 1.0e-5f);

	UpdateInputBrushSettings(*LiveActor, [](FTDFluidInputBrushSettings& Settings) { Settings.BrushHardness = 0.6f; });
	const TArray<FTDFluidBrushStamp> CappedStamps = HarvestPivotStampsAfterMove(*LiveActor, { UnitBox, DoubleBox });
	const FTDFluidBrushStamp* CappedStamp = FindStampNearX(CappedStamps, 340.0);
	TestTrue(TEXT("Doubled hardness is capped at 0.95"), CappedStamp && FMath::IsNearlyEqual(CappedStamp->ShapeHardness, 0.95f, 1.0e-5f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidHarvestHistoryResetTest,
	"TDGame.FluidFX.InputHarvest.ResetInteractionHistoryDropsJumpStroke",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidHarvestHistoryResetTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidInputHarvestTestPrivate;

	FTDFluidHarvestTestWorld TestWorld;
	ATDFluidLiveActor* LiveActor = SpawnHarvestLiveActor(TestWorld.World, FVector::ZeroVector);
	AActor* Mover = SpawnBoxActor(TestWorld.World, FVector(-300.0, 0.0, 50.0), FVector(30.0));
	if (!TestNotNull(TEXT("Spawned live actor"), LiveActor) || !TestNotNull(TEXT("Spawned mover"), Mover))
	{
		return false;
	}
	LiveActor->RegisterInteractingActor(Mover);

	const TArray<FTDFluidBrushStamp> ControlStamps = HarvestPivotStampsAfterMove(*LiveActor, { Mover });
	if (!TestTrue(TEXT("A move between two harvests stamps the pivot"), ControlStamps.Num() > 0))
	{
		return false;
	}

	LiveActor->HarvestInteractionsForTests(HarvestStepSeconds);
	const int32 ResetStartIndex = LiveActor->LiveComponent->GetPendingBrushStamps().Num();
	Mover->AddActorWorldOffset(HarvestMoveOffset);
	LiveActor->ResetInteractionHistory();
	LiveActor->HarvestInteractionsForTests(HarvestStepSeconds);
	TestEqual(TEXT("After ResetInteractionHistory the jump to the new location stamps nothing"), CollectStampsSince(*LiveActor, ResetStartIndex, ETDFluidBrushSource::Pivot).Num(), 0);

	const TArray<FTDFluidBrushStamp> AfterResetStamps = HarvestPivotStampsAfterMove(*LiveActor, { Mover });
	TestTrue(TEXT("ResetInteractionHistory keeps the registration so the next move stamps again"), AfterResetStamps.Num() > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidHarvestFilterRulesTest,
	"TDGame.FluidFX.InputHarvest.FilterRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidHarvestFilterRulesTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidInputHarvestTestPrivate;

	FTDFluidHarvestTestWorld TestWorld;
	AActor* PawnBox = SpawnBoxActor(TestWorld.World, FVector(0.0, 0.0, 50.0), FVector(30.0), ECC_Pawn);
	AActor* BodyBox = SpawnBoxActor(TestWorld.World, FVector(200.0, 0.0, 50.0), FVector(30.0), ECC_PhysicsBody);
	if (!TestNotNull(TEXT("Spawned pawn box"), PawnBox) || !TestNotNull(TEXT("Spawned body box"), BodyBox))
	{
		return false;
	}
	const FName FluidTag(TEXT("FluidToucher"));
	PawnBox->Tags.Add(FluidTag);
	const FVector SmallExtent(30.0);

	FTDFluidInputActorFilterRules EmptyRules;
	TestFalse(TEXT("Empty rules are inactive"), FTDFluidInputActorFilter::HasActiveRules(EmptyRules));
	TestTrue(TEXT("Empty rules harvest everything"), FTDFluidInputActorFilter::ShouldHarvest(*BodyBox, false, SmallExtent, true, EmptyRules));

	const TArray<TEnumAsByte<EObjectTypeQuery>> PawnTypes = { UEngineTypes::ConvertToObjectType(ECC_Pawn) };
	FTDFluidInputActorFilterRules TypeRules;
	TypeRules.ObjectTypes = PawnTypes;
	TestTrue(TEXT("A pawn passes a pawn type filter"), FTDFluidInputActorFilter::ShouldHarvest(*PawnBox, false, SmallExtent, true, TypeRules));
	TestFalse(TEXT("A physics body fails a pawn type filter"), FTDFluidInputActorFilter::ShouldHarvest(*BodyBox, false, SmallExtent, true, TypeRules));
	TestTrue(TEXT("An explicit physics body skips the type filter"), FTDFluidInputActorFilter::ShouldHarvest(*BodyBox, true, SmallExtent, true, TypeRules));

	const TArray<FName> Tags = { FluidTag };
	FTDFluidInputActorFilterRules TagRules;
	TagRules.Tags = Tags;
	TestTrue(TEXT("A tagged actor passes the tag filter"), FTDFluidInputActorFilter::ShouldHarvest(*PawnBox, false, SmallExtent, true, TagRules));
	TestFalse(TEXT("An untagged actor fails the tag filter"), FTDFluidInputActorFilter::ShouldHarvest(*BodyBox, false, SmallExtent, true, TagRules));

	const TArray<TSoftObjectPtr<AActor>> Excluded = { TSoftObjectPtr<AActor>(PawnBox) };
	FTDFluidInputActorFilterRules ExcludeRules;
	ExcludeRules.ExcludedActors = Excluded;
	TestFalse(TEXT("An excluded actor fails even when explicit"), FTDFluidInputActorFilter::ShouldHarvest(*PawnBox, true, SmallExtent, true, ExcludeRules));
	TestTrue(TEXT("Another actor passes the exclusion list"), FTDFluidInputActorFilter::ShouldHarvest(*BodyBox, false, SmallExtent, true, ExcludeRules));

	FTDFluidInputActorFilterRules LargeRules;
	LargeRules.LargeObjectExcludeRatio = 0.5f;
	LargeRules.WindowExtentCm = FVector2D(2000.0, 2000.0);
	TestTrue(TEXT("An 800 cm object fits under half of a 2000 cm window"), FTDFluidInputActorFilter::ShouldHarvest(*BodyBox, false, FVector(400.0, 100.0, 50.0), true, LargeRules));
	TestFalse(TEXT("A 1200 cm object exceeds half of a 2000 cm window"), FTDFluidInputActorFilter::ShouldHarvest(*BodyBox, false, FVector(600.0, 100.0, 50.0), true, LargeRules));
	TestTrue(TEXT("Without bounds the large object rule is skipped"), FTDFluidInputActorFilter::ShouldHarvest(*BodyBox, false, FVector(600.0, 100.0, 50.0), false, LargeRules));
	TestTrue(TEXT("An explicit large object is still harvested"), FTDFluidInputActorFilter::ShouldHarvest(*BodyBox, true, FVector(600.0, 100.0, 50.0), true, LargeRules));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidHarvestPivotBottomTest,
	"TDGame.FluidFX.InputHarvest.PivotMovesToBoundsBottom",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidHarvestPivotBottomTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidInputHarvestTestPrivate;

	FTDFluidHarvestTestWorld TestWorld;
	ATDFluidLiveActor* LiveActor = SpawnHarvestLiveActor(TestWorld.World, FVector::ZeroVector);
	AActor* TallBox = SpawnBoxActor(TestWorld.World, FVector(100.0, 50.0, 100.0), FVector(40.0, 20.0, 30.0));
	if (!TestNotNull(TEXT("Spawned live actor"), LiveActor) || !TestNotNull(TEXT("Spawned tall box"), TallBox))
	{
		return false;
	}
	LiveActor->RegisterInteractingActor(TallBox);

	const TArray<FTDFluidBrushStamp> PivotStamps = HarvestPivotStampsAfterMove(*LiveActor, { TallBox });
	if (!TestEqual(TEXT("The default pivot stamps once"), PivotStamps.Num(), 1))
	{
		return false;
	}
	TestNearlyEqual(TEXT("The default pivot uses the actor location"), PivotStamps[0].WorldLocation.Z, 100.0, 1.0e-3);

	UpdateInputBrushSettings(*LiveActor, [](FTDFluidInputBrushSettings& Settings) { Settings.bMovePivotToBoundsBottom = true; });
	const TArray<FTDFluidBrushStamp> BottomStamps = HarvestPivotStampsAfterMove(*LiveActor, { TallBox });
	if (!TestEqual(TEXT("The bottom pivot stamps once"), BottomStamps.Num(), 1))
	{
		return false;
	}
	TestNearlyEqual(TEXT("The bottom pivot sits at the bounds minimum Z"), BottomStamps[0].WorldLocation.Z, 70.0, 0.5);
	TestNearlyEqual(TEXT("The bottom pivot keeps the bounds center X"), BottomStamps[0].WorldLocation.X, TallBox->GetActorLocation().X, 0.5);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidHarvestOwnerBonesTest,
	"TDGame.FluidFX.InputHarvest.OwnerModeTracksOwnerBones",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidHarvestOwnerBonesTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidInputHarvestTestPrivate;

	FTDFluidHarvestTestWorld TestWorld;
	USkeletalMesh* MannequinMesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple"));
	ATDFluidLiveActor* LiveActor = SpawnHarvestLiveActor(TestWorld.World, FVector::ZeroVector, ETDFluidPresetKind::Blood);
	AActor* OwnerActor = SpawnBoxActor(TestWorld.World, FVector(6000.0, 0.0, 0.0), FVector(30.0), ECC_WorldDynamic);
	if (!TestNotNull(TEXT("The Manny skeletal mesh loads"), MannequinMesh) || !TestNotNull(TEXT("Spawned live actor"), LiveActor) || !TestNotNull(TEXT("Spawned owner"), OwnerActor))
	{
		return false;
	}

	const FName FootBone(TEXT("foot_l"));
	const FName TrackedMeshName(TEXT("TrackedMesh"));
	TArray<USkeletalMeshComponent*> OwnerMeshes;
	for (const FName MeshName : { TrackedMeshName, FName(TEXT("IgnoredMesh")) })
	{
		USkeletalMeshComponent* OwnerMesh = NewObject<USkeletalMeshComponent>(OwnerActor, MeshName);
		OwnerActor->AddInstanceComponent(OwnerMesh);
		OwnerMesh->SetupAttachment(OwnerActor->GetRootComponent());
		OwnerMesh->SetSkeletalMeshAsset(MannequinMesh);
		OwnerMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		OwnerMesh->RegisterComponent();
		OwnerMesh->RefreshBoneTransforms();
		OwnerMeshes.Add(OwnerMesh);
	}

	const auto HarvestOwnerStampsAfterMove = [LiveActor, OwnerActor, &OwnerMeshes]()
	{
		LiveActor->HarvestInteractionsForTests(HarvestStepSeconds);
		const int32 StartIndex = LiveActor->LiveComponent->GetPendingBrushStamps().Num();
		OwnerActor->AddActorWorldOffset(HarvestMoveOffset);
		for (USkeletalMeshComponent* OwnerMesh : OwnerMeshes)
		{
			OwnerMesh->RefreshBoneTransforms();
		}
		LiveActor->HarvestInteractionsForTests(HarvestStepSeconds);
		return StartIndex;
	};

	LiveActor->SetOwner(OwnerActor);
	LiveActor->OwnerBoneNames = { FootBone };
	LiveActor->OwnerComponentNames = { TrackedMeshName };
	const int32 OffStart = HarvestOwnerStampsAfterMove();
	TestEqual(TEXT("Without owner mode an owner outside the volume stamps nothing"), LiveActor->LiveComponent->GetPendingBrushStamps().Num() - OffStart, 0);

	LiveActor->bInteractWithOwner = true;
	HarvestOwnerStampsAfterMove();
	const int32 OwnerStart = HarvestOwnerStampsAfterMove();
	TestEqual(TEXT("Owner mode stamps the owner pivot once"), CollectStampsSince(*LiveActor, OwnerStart, ETDFluidBrushSource::Pivot).Num(), 1);
	TestEqual(TEXT("Owner mode stamps the named bone on the named component only"), CollectStampsSince(*LiveActor, OwnerStart, ETDFluidBrushSource::Bone).Num(), 1);

	LiveActor->OwnerComponentNames.Reset();
	HarvestOwnerStampsAfterMove();
	const int32 AllMeshStart = HarvestOwnerStampsAfterMove();
	TestEqual(TEXT("Without a component name filter both owner meshes stamp the bone"), CollectStampsSince(*LiveActor, AllMeshStart, ETDFluidBrushSource::Bone).Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidHarvestMultiTouchTest,
	"TDGame.FluidFX.InputHarvest.MultiTouchSeparateStrokes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidHarvestMultiTouchTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidInputHarvestTestPrivate;

	FTDFluidHarvestTestWorld TestWorld;
	ATDFluidLiveActor* LiveActor = SpawnHarvestLiveActor(TestWorld.World, FVector::ZeroVector);
	if (!TestNotNull(TEXT("Spawned live actor"), LiveActor))
	{
		return false;
	}

	UpdateInputBrushSettings(*LiveActor, [](FTDFluidInputBrushSettings& Settings) { Settings.bInterpolatePivotAndPointerStrokes = true; });
	const int32 StartIndex = LiveActor->LiveComponent->GetPendingBrushStamps().Num();
	const double FirstFingerY = 0.0;
	const double SecondFingerY = 300.0;
	LiveActor->QueuePointerBrushAt(0, FVector(-200.0, FirstFingerY, 50.0), HarvestStepSeconds);
	LiveActor->QueuePointerBrushAt(1, FVector(-200.0, SecondFingerY, 50.0), HarvestStepSeconds);
	LiveActor->QueuePointerBrushAt(0, FVector(-160.0, FirstFingerY, 50.0), HarvestStepSeconds);
	LiveActor->QueuePointerBrushAt(1, FVector(-160.0, SecondFingerY, 50.0), HarvestStepSeconds);

	const TArray<FTDFluidBrushStamp> PointerStamps = CollectStampsSince(*LiveActor, StartIndex, ETDFluidBrushSource::Pointer);
	int32 FirstFingerStampCount = 0;
	int32 SecondFingerStampCount = 0;
	bool bAreAllOnOwnSegment = true;
	for (const FTDFluidBrushStamp& Stamp : PointerStamps)
	{
		const bool bIsOnSegmentX = Stamp.WorldLocation.X >= -200.0 - 0.01 && Stamp.WorldLocation.X <= -160.0 + 0.01;
		FirstFingerStampCount += FMath::IsNearlyEqual(Stamp.WorldLocation.Y, FirstFingerY, 0.01) ? 1 : 0;
		SecondFingerStampCount += FMath::IsNearlyEqual(Stamp.WorldLocation.Y, SecondFingerY, 0.01) ? 1 : 0;
		bAreAllOnOwnSegment &= bIsOnSegmentX;
	}

	const int32 ExpectedStrokeCount = ATDFluidLiveActor::ComputeStrokeStampCount(40.0, LiveActor->MouseBrushRadiusCm, LiveActor->LiveComponent->GetInputBrushSettings().StrokeSpacingRatio);
	TestEqual(TEXT("Every pointer stamp lies on one finger path"), FirstFingerStampCount + SecondFingerStampCount, PointerStamps.Num());
	TestTrue(TEXT("No stamp joins the two fingers"), bAreAllOnOwnSegment);
	TestEqual(TEXT("The first finger draws its own stroke"), FirstFingerStampCount, ExpectedStrokeCount);
	TestEqual(TEXT("The second finger draws its own stroke"), SecondFingerStampCount, ExpectedStrokeCount);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidHarvestFastPivotStrokeTest,
	"TDGame.FluidFX.InputHarvest.FastPivotStrokeHasNoGaps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidHarvestFastPivotStrokeTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidInputHarvestTestPrivate;

	constexpr float FrameSeconds = 1.0f / 30.0f;
	constexpr double SpeedCmPerSec = 2000.0;
	const double FrameStepCm = SpeedCmPerSec * FrameSeconds;
	const FVector StartLocation(-400.0, 0.0, 50.0);

	FTDFluidHarvestTestWorld TestWorld;
	const auto HarvestFastStroke = [&](bool bInterpolates, TArray<FTDFluidBrushStamp>& OutStamps, FTDFluidSimulationConfig& OutConfig)
	{
		ATDFluidLiveActor* LiveActor = SpawnHarvestLiveActor(TestWorld.World, FVector::ZeroVector);
		AActor* Mover = SpawnBoxActor(TestWorld.World, StartLocation, FVector(10.0));
		if (!LiveActor || !Mover)
		{
			return false;
		}
		LiveActor->ActorPivotBrushRadiusCm = 45.0f;
		LiveActor->RegisterInteractingActor(Mover);
		UpdateInputBrushSettings(*LiveActor, [bInterpolates](FTDFluidInputBrushSettings& Settings)
		{
			Settings.bInterpolatePivotAndPointerStrokes = bInterpolates;
			Settings.PivotBrushRadiusOverrideCm = 0.0f;
			Settings.PivotDensityStrength = 1.0f;
			Settings.RingInnerRadiusRatio = 0.0f;
			Settings.BrushHardness = 0.0f;
			Settings.StrokeSpacingRatio = 0.5f;
			Settings.TeleportSpeedCmPerSec = FMath::Max(Settings.TeleportSpeedCmPerSec, 5000.0f);
		});
		const int32 StartIndex = LiveActor->LiveComponent->GetPendingBrushStamps().Num();
		LiveActor->HarvestInteractionsForTests(FrameSeconds);
		for (int32 FrameIndex = 0; FrameIndex < 2; ++FrameIndex)
		{
			Mover->AddActorWorldOffset(FVector(FrameStepCm, 0.0, 0.0));
			LiveActor->HarvestInteractionsForTests(FrameSeconds);
		}
		OutStamps = CollectStampsSince(*LiveActor, StartIndex, ETDFluidBrushSource::Pivot);
		OutConfig = LiveActor->LiveComponent->SimulationConfig;
		return true;
	};

	TArray<FTDFluidBrushStamp> SingleStamps;
	TArray<FTDFluidBrushStamp> StrokeStamps;
	FTDFluidSimulationConfig Config;
	if (!TestTrue(TEXT("The unstroked scene spawns"), HarvestFastStroke(false, SingleStamps, Config)) || !TestTrue(TEXT("The stroked scene spawns"), HarvestFastStroke(true, StrokeStamps, Config)))
	{
		return false;
	}
	TestEqual(TEXT("Without strokes a fast pivot stamps once per frame"), SingleStamps.Num(), 2);
	TestTrue(TEXT("With strokes a fast pivot fills the path"), StrokeStamps.Num() > SingleStamps.Num());

	Config.GridResolution = 256;
	FTDFluidGrid2DSolver Solver;
	Solver.Initialize(Config, FVector2D::ZeroVector);
	Solver.ApplyBrushStamps(StrokeStamps, FrameSeconds);

	float MinimumDensity = TNumericLimits<float>::Max();
	float MaximumDensity = 0.0f;
	const double PathStartX = StartLocation.X + FrameStepCm;
	const double PathEndX = StartLocation.X + 2.0 * FrameStepCm;
	for (double SampleX = PathStartX; SampleX <= PathEndX; SampleX += 2.0)
	{
		FTDFluidCellSample Sample;
		Solver.SampleAtWorldPosition(FVector2D(SampleX, StartLocation.Y), Sample);
		MinimumDensity = FMath::Min(MinimumDensity, Sample.PrimaryDensity);
		MaximumDensity = FMath::Max(MaximumDensity, Sample.PrimaryDensity);
	}
	TestTrue(*FString::Printf(TEXT("The path has density (max %.4f)"), MaximumDensity), MaximumDensity > 0.0f);
	TestTrue(*FString::Printf(TEXT("The path minimum stays at least half of the maximum (%.4f / %.4f)"), MinimumDensity, MaximumDensity), MaximumDensity > 0.0f && MinimumDensity / MaximumDensity >= 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidHarvestPersistenceTest,
	"TDGame.FluidFX.InputHarvest.PersistenceHalfLifeFrameRateInvariant",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidHarvestPersistenceTest::RunTest(const FString& Parameters)
{
	constexpr float TauSeconds = 0.25f;
	const float ExpectedHalfLifeSeconds = TauSeconds * FMath::Loge(2.0f);
	FTDFluidBrushStamp SourceStamp;
	SourceStamp.DensityStrength = 1.0f;
	SourceStamp.HeatStrength = 1.0f;
	SourceStamp.RippleImpulseCm = 3.0f;

	for (const int32 FramesPerSecond : { 30, 60, 120 })
	{
		const float FrameSeconds = 1.0f / static_cast<float>(FramesPerSecond);
		FTDFluidInputPersistence Persistence;
		Persistence.RecordEmitted(FTDFluidInputSourceKey{FObjectKey(), NAME_None, 0}, SourceStamp);
		TArray<FTDFluidBrushStamp> Emitted;
		Persistence.EmitLingering(FrameSeconds, TauSeconds, Emitted);
		TestEqual(*FString::Printf(TEXT("%d fps: a source recorded this step does not linger yet"), FramesPerSecond), Emitted.Num(), 0);

		float HalfLifeSeconds = -1.0f;
		for (int32 FrameIndex = 1; FrameIndex <= 1000 && HalfLifeSeconds < 0.0f; ++FrameIndex)
		{
			Emitted.Reset();
			Persistence.EmitLingering(FrameSeconds, TauSeconds, Emitted);
			if (Emitted.Num() == 1 && Emitted[0].DensityStrength <= 0.5f)
			{
				HalfLifeSeconds = FrameIndex * FrameSeconds;
				TestNearlyEqual(*FString::Printf(TEXT("%d fps: heat fades with density"), FramesPerSecond), Emitted[0].HeatStrength, Emitted[0].DensityStrength, 1.0e-6f);
				TestEqual(*FString::Printf(TEXT("%d fps: lingering stamps carry no ripple"), FramesPerSecond), Emitted[0].RippleImpulseCm, 0.0f);
			}
		}
		TestTrue(*FString::Printf(TEXT("%d fps: half life %.4f s is tau ln2 within one frame"), FramesPerSecond, HalfLifeSeconds), HalfLifeSeconds >= 0.0f && FMath::Abs(HalfLifeSeconds - ExpectedHalfLifeSeconds) <= FrameSeconds);
	}

	FTDFluidInputPersistence OrderedPersistence;
	for (const int32 SourceIndex : { 3, 1, 2 })
	{
		FTDFluidBrushStamp IndexedStamp = SourceStamp;
		IndexedStamp.RadiusCm = static_cast<float>(SourceIndex);
		OrderedPersistence.RecordEmitted(FTDFluidInputSourceKey{FObjectKey(), NAME_None, SourceIndex}, IndexedStamp);
	}
	TArray<FTDFluidBrushStamp> Ordered;
	OrderedPersistence.EmitLingering(0.01f, TauSeconds, Ordered);
	OrderedPersistence.RecordEmitted(FTDFluidInputSourceKey{FObjectKey(), NAME_None, 3}, SourceStamp);
	OrderedPersistence.EmitLingering(0.01f, TauSeconds, Ordered);
	Ordered.Reset();
	OrderedPersistence.EmitLingering(0.01f, TauSeconds, Ordered);
	const bool bKeepsRecordOrder = Ordered.Num() == 3 && Ordered[0].RadiusCm == SourceStamp.RadiusCm && Ordered[1].RadiusCm == 1.0f && Ordered[2].RadiusCm == 2.0f;
	TestTrue(TEXT("Lingering stamps come out in first record order even after an update"), bKeepsRecordOrder);

	OrderedPersistence.EmitLingering(0.01f, 0.0f, Ordered);
	TestEqual(TEXT("A zero tau clears every lingering source"), OrderedPersistence.GetLingeringCount(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidHarvestSpawnedActorTest,
	"TDGame.FluidFX.InputHarvest.ActorSpawnedInsideVolumeStamps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidHarvestSpawnedActorTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidInputHarvestTestPrivate;

	FTDFluidHarvestTestWorld TestWorld;
	ATDFluidLiveActor* LiveActor = SpawnHarvestLiveActor(TestWorld.World, FVector::ZeroVector);
	if (!TestNotNull(TEXT("Spawned live actor"), LiveActor))
	{
		return false;
	}

	LiveActor->HarvestInteractionsForTests(HarvestStepSeconds);
	AActor* SpawnedProbe = SpawnBoxActor(TestWorld.World, FVector(150.0, 0.0, 50.0), FVector(20.0), ECC_Pawn);
	if (!TestNotNull(TEXT("Spawned probe inside the volume"), SpawnedProbe))
	{
		return false;
	}

	const TArray<FTDFluidBrushStamp> Stamps = HarvestPivotStampsAfterMove(*LiveActor, { SpawnedProbe });
	TestEqual(TEXT("An actor spawned inside the volume stamps without registration"), Stamps.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidHarvestSharedBusTest,
	"TDGame.FluidFX.InputHarvest.SharedBusReachesOptedInActorsOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidHarvestSharedBusTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidInputHarvestTestPrivate;

	FTDFluidHarvestTestWorld TestWorld;
	ATDFluidLiveActor* FirstReceiver = SpawnHarvestLiveActor(TestWorld.World, FVector::ZeroVector);
	ATDFluidLiveActor* SecondReceiver = SpawnHarvestLiveActor(TestWorld.World, FVector(100.0, 0.0, 0.0));
	ATDFluidLiveActor* Bystander = SpawnHarvestLiveActor(TestWorld.World, FVector(200.0, 0.0, 0.0));
	UTDFluidLiveSubsystem* Subsystem = UTDFluidLiveSubsystem::GetFluidLiveSubsystem(TestWorld.World);
	if (!TestNotNull(TEXT("Spawned first receiver"), FirstReceiver) || !TestNotNull(TEXT("Spawned second receiver"), SecondReceiver) || !TestNotNull(TEXT("Spawned bystander"), Bystander) || !TestNotNull(TEXT("Fluid subsystem exists"), Subsystem))
	{
		return false;
	}

	UpdateInputBrushSettings(*FirstReceiver, [](FTDFluidInputBrushSettings& Settings)
	{
		Settings.bReceivesSharedInput = true;
		Settings.SharedInputDensityScale = 0.5f;
	});
	UpdateInputBrushSettings(*SecondReceiver, [](FTDFluidInputBrushSettings& Settings) { Settings.bReceivesSharedInput = true; });

	const int32 FirstStart = FirstReceiver->LiveComponent->GetPendingBrushStamps().Num();
	const int32 SecondStart = SecondReceiver->LiveComponent->GetPendingBrushStamps().Num();
	const int32 BystanderStart = Bystander->LiveComponent->GetPendingBrushStamps().Num();
	FTDFluidBrushStamp SharedStamp;
	SharedStamp.WorldLocation = FVector(100.0, 0.0, 50.0);
	SharedStamp.DensityStrength = 0.8f;
	const TArray<FTDFluidBrushStamp> SharedStamps = { SharedStamp };
	TestEqual(TEXT("Two opted in actors receive the broadcast"), Subsystem->BroadcastSharedBrushStamps(SharedStamps), 2);

	const TArray<FTDFluidBrushStamp> FirstStamps = CollectStampsSince(*FirstReceiver, FirstStart, ETDFluidBrushSource::Shared);
	TestEqual(TEXT("The first receiver gets the stamp once"), FirstStamps.Num(), 1);
	TestEqual(TEXT("The second receiver gets the stamp once"), CollectStampsSince(*SecondReceiver, SecondStart, ETDFluidBrushSource::Shared).Num(), 1);
	TestEqual(TEXT("The bystander gets nothing"), Bystander->LiveComponent->GetPendingBrushStamps().Num(), BystanderStart);
	if (FirstStamps.Num() == 1)
	{
		FTDFluidBrushStamp ReceivedStamp = FirstStamps[0];
		FTDFluidBrushModel::ApplyToStamp(ReceivedStamp, FirstReceiver->LiveComponent->GetInputBrushSettings(), FirstReceiver->LiveComponent->GetPlaneBasis());
		TestNearlyEqual(TEXT("The receiver scales shared density"), ReceivedStamp.DensityStrength, SharedStamp.DensityStrength * 0.5f, 1.0e-5f);
	}
	return true;
}

#endif

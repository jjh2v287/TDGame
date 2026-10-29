#include "FluidFX/TDFluidLiveSubsystem.h"
#include "Engine/World.h"
#include "FluidFX/TDFluidFieldInputComponent.h"
#include "FluidFX/TDFluidLiveActor.h"
#include "FluidFX/TDFluidLiveComponent.h"

namespace
{
	bool FieldInputOverlapsWindow(const UTDFluidLiveComponent& LiveComponent, const FTDFluidFieldInputPlacement& Placement)
	{
		const FVector Offset = Placement.WorldCenter - LiveComponent.GetSimulationWindowCenterWorld();
		const FVector2D HalfExtentCm = LiveComponent.GetSimulationExtentCm() * 0.5;
		const double ReachCm = Placement.SizeCm.Size() * 0.5;
		if (!LiveComponent.GetPlaneBasis().IsWorldFacing())
		{
			FVector2D PlanePoint;
			return LiveComponent.TryProjectWorldPointIntoSlab(Placement.WorldCenter, PlanePoint) && Offset.Size() <= HalfExtentCm.Size() + ReachCm;
		}

		return FMath::Abs(Offset.Z) <= LiveComponent.GetRuntimeOpsSettings().InteractionVolumeHalfHeightCm
			&& FMath::Abs(Offset.X) <= HalfExtentCm.X + ReachCm
			&& FMath::Abs(Offset.Y) <= HalfExtentCm.Y + ReachCm;
	}

	TArray<FTDFluidFieldInputPlacement> FieldInputSelectPlacements(const UTDFluidLiveComponent& LiveComponent, TConstArrayView<FTDFluidFieldInputPlacement> Placements)
	{
		TArray<FTDFluidFieldInputPlacement> Selected;
		const FTDFluidFieldInputSettings& Settings = LiveComponent.GetFieldInputSettings();
		const int32 MaximumCount = FMath::Min(Settings.MaxFieldInputs, TDFluidGpuSolverConstants::MaximumFieldInputsPerActor);
		if (!Settings.bAcceptFieldInputs || MaximumCount <= 0 || Placements.IsEmpty() || !LiveComponent.IsSimulationAwake() || LiveComponent.IsAreaWrapped())
		{
			return Selected;
		}

		const FVector WindowCenterWorld = LiveComponent.GetSimulationWindowCenterWorld();
		TArray<TPair<double, int32>> Candidates;
		for (int32 Index = 0; Index < Placements.Num(); ++Index)
		{
			if (FieldInputOverlapsWindow(LiveComponent, Placements[Index]))
			{
				Candidates.Emplace(FVector::DistSquared(Placements[Index].WorldCenter, WindowCenterWorld), Index);
			}
		}

		Candidates.Sort([](const TPair<double, int32>& Left, const TPair<double, int32>& Right)
		{
			return Left.Key < Right.Key || (Left.Key == Right.Key && Left.Value < Right.Value);
		});
		const int32 SelectedCount = FMath::Min(Candidates.Num(), MaximumCount);
		Selected.Reserve(SelectedCount);
		for (int32 Index = 0; Index < SelectedCount; ++Index)
		{
			Selected.Add(Placements[Candidates[Index].Value]);
		}
		return Selected;
	}
}

void UTDFluidLiveSubsystem::RegisterFieldInputSource(UTDFluidFieldInputComponent* FieldInputSource)
{
	if (FieldInputSource)
	{
		FieldInputSources.AddUnique(FieldInputSource);
	}
}

void UTDFluidLiveSubsystem::UnregisterFieldInputSource(UTDFluidFieldInputComponent* FieldInputSource)
{
	FieldInputSources.Remove(FieldInputSource);
}

void UTDFluidLiveSubsystem::DistributeFieldInputs(float DeltaTime)
{
	FieldInputSources.RemoveAll([](const TWeakObjectPtr<UTDFluidFieldInputComponent>& Source) { return !Source.IsValid(); });
	if (FieldInputSources.IsEmpty() && !bHasDistributedFieldInputs)
	{
		return;
	}

	const UWorld* World = GetWorld();
	const double WorldTimeSeconds = World ? World->GetTimeSeconds() : 0.0;
	TArray<FTDFluidFieldInputPlacement> Placements;
	Placements.Reserve(FieldInputSources.Num());
	for (const TWeakObjectPtr<UTDFluidFieldInputComponent>& Source : FieldInputSources)
	{
		UTDFluidFieldInputComponent* FieldInputSource = Source.Get();
		FieldInputSource->UpdateMotion(DeltaTime);
		FTDFluidFieldInputPlacement Placement;
		if (FieldInputSource->BuildPlacement(WorldTimeSeconds, Placement))
		{
			Placements.Add(Placement);
		}
	}

	bHasDistributedFieldInputs = !Placements.IsEmpty();
	for (const TWeakObjectPtr<ATDFluidLiveActor>& RegisteredActor : RegisteredActors)
	{
		ATDFluidLiveActor* LiveActor = RegisteredActor.Get();
		UTDFluidLiveComponent* LiveComponent = LiveActor ? LiveActor->LiveComponent.Get() : nullptr;
		if (!LiveComponent)
		{
			continue;
		}

		LiveComponent->SetFieldInputPlacements(LiveActor->IsPooledInactive() ? TArray<FTDFluidFieldInputPlacement>() : FieldInputSelectPlacements(*LiveComponent, Placements));
	}
}

int32 UTDFluidLiveSubsystem::BroadcastSharedBrushStamps(TConstArrayView<FTDFluidBrushStamp> Stamps)
{
	if (Stamps.IsEmpty())
	{
		return 0;
	}

	int32 ReceiverCount = 0;
	for (int32 Idx = RegisteredActors.Num() - 1; Idx >= 0; --Idx)
	{
		ATDFluidLiveActor* LiveActor = RegisteredActors[Idx].Get();
		if (!LiveActor)
		{
			RegisteredActors.RemoveAtSwap(Idx);
			continue;
		}

		UTDFluidLiveComponent* LiveComponent = LiveActor->LiveComponent;
		if (LiveActor->IsPooledInactive() || !LiveComponent || !LiveComponent->IsSimulationAwake() || LiveComponent->IsAreaWrapped() || !LiveComponent->GetInputBrushSettings().bReceivesSharedInput)
		{
			continue;
		}

		bool bHasReceivedStamp = false;
		for (const FTDFluidBrushStamp& Stamp : Stamps)
		{
			FVector2D PlanePosition;
			if (!LiveComponent->TryProjectWorldPointIntoSlab(Stamp.WorldLocation, PlanePosition))
			{
				continue;
			}

			FTDFluidBrushStamp SharedStamp = Stamp;
			SharedStamp.Source = ETDFluidBrushSource::Shared;
			LiveComponent->QueueBrushStamp(SharedStamp);
			bHasReceivedStamp = true;
		}
		ReceiverCount += bHasReceivedStamp ? 1 : 0;
	}
	return ReceiverCount;
}

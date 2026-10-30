#include "FluidFX/TDFluidDebugDrawSubsystem.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FluidFX/TDFluidLiveActor.h"
#include "FluidFX/TDFluidLiveComponent.h"
#include "FluidFX/TDFluidPresetAsset.h"
#include "HAL/IConsoleManager.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "TDFluidGpuMassInput.h"

namespace TDFluidDebugDrawPrivate
{
	constexpr int32 DebugDrawWindowFlag = 1;
	constexpr int32 DebugDrawLabelFlag = 2;
	constexpr int32 DebugDrawCountersFlag = 4;
	constexpr int32 DebugDrawOverflowFlag = 8;
	constexpr int32 DebugDrawStepRateFlag = 16;
	constexpr float DebugDrawLineThickness = 4.0f;
	constexpr float DebugDrawSingleFrameSeconds = -1.0f;

	TAutoConsoleVariable<int32> CVarTDFluidDebug(
		TEXT("TD.FluidFX.Debug"),
		0,
		TEXT("Fluid debug display bit flags: 1 window bounds, 2 name label, 4 tracking counters, 8 overflow warnings, 16 step rate. 0 turns the display off."),
		ECVF_Default);

	struct FTDFluidDebugActorState
	{
		FString Label;
		FString Kind;
		bool bUsesGpuSolver = false;
		int32 Resolution = 0;
		bool bIsAwake = false;
		int32 Generation = 0;
		FVector2D CenterCm = FVector2D::ZeroVector;
		FVector2D ExtentCm = FVector2D::ZeroVector;
		int32 CombatBindingCount = 0;
		int32 LastStampCount = 0;
		int32 MaxStampCount = 0;
		int32 PendingMassPointCount = 0;
		int32 UploadedMassPointCount = 0;
		bool bIsMassSaturated = false;
		float StepRateHz = 0.0f;
	};

	FTDFluidDebugActorState MakeDebugActorState(const ATDFluidLiveActor& LiveActor, const UTDFluidLiveComponent& LiveComponent)
	{
		const UTDFluidPresetAsset* Preset = LiveComponent.ActivePreset;
		const ETDFluidPresetKind Kind = Preset ? Preset->PresetKind : ETDFluidPresetKind::Custom;
		FTDFluidDebugActorState State;
		State.Label = LiveActor.GetActorNameOrLabel();
		State.Kind = StaticEnum<ETDFluidPresetKind>()->GetNameStringByValue(static_cast<int64>(Kind));
		State.bUsesGpuSolver = LiveComponent.IsUsingGpuSolver();
		State.Resolution = LiveComponent.GetSimulationResolution();
		State.bIsAwake = LiveComponent.IsSimulationAwake();
		State.Generation = LiveComponent.GetSimulationGeneration();
		State.CenterCm = LiveComponent.GetSimulationCenterWorld();
		State.ExtentCm = LiveComponent.GetSimulationExtentCm();
		State.CombatBindingCount = LiveActor.GetBoundCombatComponentCount();
		State.LastStampCount = LiveComponent.GetLastProcessedPointCount();
		State.MaxStampCount = LiveComponent.SimulationConfig.MaxPointsPerTick;
		State.PendingMassPointCount = LiveComponent.GetPendingMassPointCount();
		State.UploadedMassPointCount = LiveComponent.GetLastUploadedMassPointCount();
		State.bIsMassSaturated = LiveComponent.GetMassInputCounters().bSaturated;
		State.StepRateHz = LiveActor.ResolveStepRateHz();
		return State;
	}

	bool IsDebugActorOverflowing(const FTDFluidDebugActorState& State)
	{
		return State.bIsMassSaturated || (State.MaxStampCount > 0 && State.LastStampCount >= State.MaxStampCount);
	}

	TSharedPtr<FJsonValue> MakeDebugVectorValue(const FVector2D& Value)
	{
		TArray<TSharedPtr<FJsonValue>> Components;
		Components.Add(MakeShared<FJsonValueNumber>(Value.X));
		Components.Add(MakeShared<FJsonValueNumber>(Value.Y));
		return MakeShared<FJsonValueArray>(Components);
	}

	TSharedRef<FJsonObject> MakeDebugActorJson(const FTDFluidDebugActorState& State)
	{
		TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("label"), State.Label);
		Entry->SetStringField(TEXT("kind"), State.Kind);
		Entry->SetStringField(TEXT("backend"), State.bUsesGpuSolver ? TEXT("GPU") : TEXT("CPU"));
		Entry->SetNumberField(TEXT("resolution"), State.Resolution);
		Entry->SetBoolField(TEXT("awake"), State.bIsAwake);
		Entry->SetNumberField(TEXT("generation"), State.Generation);
		Entry->SetField(TEXT("center_cm"), MakeDebugVectorValue(State.CenterCm));
		Entry->SetField(TEXT("extent_cm"), MakeDebugVectorValue(State.ExtentCm));
		Entry->SetNumberField(TEXT("combat_bindings"), State.CombatBindingCount);
		Entry->SetNumberField(TEXT("stamps_last"), State.LastStampCount);
		Entry->SetNumberField(TEXT("stamps_max"), State.MaxStampCount);
		Entry->SetNumberField(TEXT("mass_pending"), State.PendingMassPointCount);
		Entry->SetNumberField(TEXT("mass_uploaded"), State.UploadedMassPointCount);
		Entry->SetBoolField(TEXT("mass_saturated"), State.bIsMassSaturated);
		Entry->SetNumberField(TEXT("step_rate_hz"), State.StepRateHz);
		return Entry;
	}

	FString MakeDebugLabelText(const FTDFluidDebugActorState& State, int32 DebugFlags)
	{
		TArray<FString> Lines;
		if (DebugFlags & DebugDrawLabelFlag)
		{
			Lines.Add(FString::Printf(TEXT("%s %s %s %dx%d gen %d"), *State.Label, *State.Kind, State.bUsesGpuSolver ? TEXT("GPU") : TEXT("CPU"), State.Resolution, State.Resolution, State.Generation));
		}
		if (DebugFlags & DebugDrawCountersFlag)
		{
			Lines.Add(FString::Printf(TEXT("combat %d points %d/%d mass pending %d uploaded %d%s"), State.CombatBindingCount, State.LastStampCount, State.MaxStampCount, State.PendingMassPointCount, State.UploadedMassPointCount, State.bIsMassSaturated ? TEXT(" saturated") : TEXT("")));
		}
		if (DebugFlags & DebugDrawStepRateFlag)
		{
			Lines.Add(FString::Printf(TEXT("step rate %.1f Hz"), State.StepRateHz));
		}
		return FString::Join(Lines, TEXT("\n"));
	}
}

bool UTDFluidDebugDrawSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
#if ENABLE_DRAW_DEBUG
	return Super::ShouldCreateSubsystem(Outer);
#else
	return false;
#endif
}

bool UTDFluidDebugDrawSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

int32 UTDFluidDebugDrawSubsystem::GetDebugFlags()
{
	return TDFluidDebugDrawPrivate::CVarTDFluidDebug.GetValueOnGameThread();
}

bool UTDFluidDebugDrawSubsystem::IsTickable() const
{
	return GetDebugFlags() != 0;
}

TStatId UTDFluidDebugDrawSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UTDFluidDebugDrawSubsystem, STATGROUP_Tickables);
}

void UTDFluidDebugDrawSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	const int32 DebugFlags = GetDebugFlags();
	if (DebugFlags == 0)
	{
		return;
	}
	for (const ATDFluidLiveActor* LiveActor : CollectDebugActors())
	{
		DrawActorDebug(*LiveActor, DebugFlags);
	}
}

FString UTDFluidDebugDrawSubsystem::BuildDebugSnapshotJson() const
{
	TArray<TSharedPtr<FJsonValue>> Entries;
	for (const ATDFluidLiveActor* LiveActor : CollectDebugActors())
	{
		const TDFluidDebugDrawPrivate::FTDFluidDebugActorState State = TDFluidDebugDrawPrivate::MakeDebugActorState(*LiveActor, *LiveActor->LiveComponent);
		Entries.Add(MakeShared<FJsonValueObject>(TDFluidDebugDrawPrivate::MakeDebugActorJson(State)));
	}
	FString Output;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
	FJsonSerializer::Serialize(Entries, Writer);
	return Output;
}

TArray<ATDFluidLiveActor*> UTDFluidDebugDrawSubsystem::CollectDebugActors() const
{
	TArray<ATDFluidLiveActor*> LiveActors;
	UWorld* World = GetWorld();
	if (!World)
	{
		return LiveActors;
	}
	for (TActorIterator<ATDFluidLiveActor> It(World); It; ++It)
	{
		ATDFluidLiveActor* LiveActor = *It;
		if (!IsValid(LiveActor) || !LiveActor->LiveComponent || LiveActor->IsPooledInactive())
		{
			continue;
		}
		LiveActors.Add(LiveActor);
	}
	return LiveActors;
}

void UTDFluidDebugDrawSubsystem::DrawActorDebug(const ATDFluidLiveActor& LiveActor, int32 DebugFlags) const
{
	using namespace TDFluidDebugDrawPrivate;
	UWorld* World = GetWorld();
	const UTDFluidLiveComponent& LiveComponent = *LiveActor.LiveComponent;
	const FTDFluidDebugActorState State = MakeDebugActorState(LiveActor, LiveComponent);
	const FColor StateColor = State.bIsAwake ? FColor::Green : FColor(128, 128, 128);
	if (DebugFlags & DebugDrawWindowFlag)
	{
		const FVector2D HalfExtent = State.ExtentCm * 0.5;
		const FVector Corners[] = {
			LiveComponent.UnprojectSolverPlaneToWorld(State.CenterCm + FVector2D(-HalfExtent.X, -HalfExtent.Y)),
			LiveComponent.UnprojectSolverPlaneToWorld(State.CenterCm + FVector2D(HalfExtent.X, -HalfExtent.Y)),
			LiveComponent.UnprojectSolverPlaneToWorld(State.CenterCm + FVector2D(HalfExtent.X, HalfExtent.Y)),
			LiveComponent.UnprojectSolverPlaneToWorld(State.CenterCm + FVector2D(-HalfExtent.X, HalfExtent.Y))};
		constexpr int32 CornerCount = UE_ARRAY_COUNT(Corners);
		for (int32 CornerIndex = 0; CornerIndex < CornerCount; ++CornerIndex)
		{
			DrawDebugLine(World, Corners[CornerIndex], Corners[(CornerIndex + 1) % CornerCount], StateColor, false, DebugDrawSingleFrameSeconds, 0, DebugDrawLineThickness);
		}
	}
	const FString LabelText = MakeDebugLabelText(State, DebugFlags);
	if (!LabelText.IsEmpty())
	{
		DrawDebugString(World, LiveComponent.UnprojectSolverPlaneToWorld(State.CenterCm), LabelText, nullptr, StateColor, DebugDrawSingleFrameSeconds);
	}
	if (!(DebugFlags & DebugDrawOverflowFlag) || !IsDebugActorOverflowing(State) || !GEngine)
	{
		return;
	}
	GEngine->AddOnScreenDebugMessage(static_cast<uint64>(LiveActor.GetUniqueID()), 0.0f, FColor::Red, FString::Printf(TEXT("%s fluid input overflow: points %d/%d, mass saturated %s"), *State.Label, State.LastStampCount, State.MaxStampCount, State.bIsMassSaturated ? TEXT("yes") : TEXT("no")));
}

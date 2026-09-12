#include "Performance/BudgetTick/TDBudgetTickTestActor.h"
#include "Components/BillboardComponent.h"
#include "Components/SceneComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/World.h"
#include "Performance/BudgetTick/TDBudgetTickParticipantComponent.h"

ATDBudgetTickTestActor::ATDBudgetTickTestActor()
{
	PrimaryActorTick.bCanEverTick = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	MarkerComponent = CreateDefaultSubobject<UBillboardComponent>(TEXT("Marker"));
	MarkerComponent->SetupAttachment(Root);

	DebugTextComponent = CreateDefaultSubobject<UTextRenderComponent>(TEXT("DebugText"));
	DebugTextComponent->SetupAttachment(Root);
	DebugTextComponent->SetHorizontalAlignment(EHorizTextAligment::EHTA_Center);
	DebugTextComponent->SetVerticalAlignment(EVerticalTextAligment::EVRTA_TextBottom);
	DebugTextComponent->SetRelativeLocation(FVector(0.f, 0.f, 120.f));
	DebugTextComponent->SetWorldSize(40.f);
	DebugTextComponent->SetTextRenderColor(FColor::White);

	BudgetTickParticipant = CreateDefaultSubobject<UTDBudgetTickParticipantComponent>(TEXT("BudgetTickParticipant"));

	Tags.Add(TEXT("BudgetTickTest"));
}

void ATDBudgetTickTestActor::BeginPlay()
{
	Super::BeginPlay();

	InitialLocation = GetActorLocation();
	LastBudgetTickTimeSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;

	if (BudgetTickParticipant)
	{
		BudgetTickParticipant->SetRuntimeImportanceBias(RuntimeImportanceBias);
	}

	RefreshDebugText();
}

void ATDBudgetTickTestActor::OnBudgetTickManagedStateChanged(const bool bIsManaged)
{
	bIsManagedByScheduler = bIsManaged;
	RefreshDebugText();
}

void ATDBudgetTickTestActor::BudgetTick(const float DeltaTimeSinceLastUpdate)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TD_BudgetTickTestActor_BudgetTick);

	const double StartTimeSeconds = FPlatformTime::Seconds();

	BudgetTickCount++;
	LastBudgetDeltaTime = DeltaTimeSinceLastUpdate;

	const double CurrentTimeSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : LastBudgetTickTimeSeconds;
	LastGapBetweenUpdates = static_cast<float>(CurrentTimeSeconds - LastBudgetTickTimeSeconds);
	LastBudgetTickTimeSeconds = CurrentTimeSeconds;

	if (bSimulateWork)
	{
		volatile double WorkSink = 0.0;
		for (int32 Index = 0; Index < SimulatedWorkIterations; ++Index)
		{
			WorkSink += FMath::Sin((OrbitAngleDegrees + static_cast<float>(Index)) * 0.015f);
		}
	}

	OrbitAngleDegrees = FMath::Fmod(OrbitAngleDegrees + OrbitRateDegrees * DeltaTimeSinceLastUpdate, 360.f);

	const FVector OrbitOffset(
		FMath::Cos(FMath::DegreesToRadians(OrbitAngleDegrees)) * OrbitRadius,
		FMath::Sin(FMath::DegreesToRadians(OrbitAngleDegrees)) * OrbitRadius,
		0.f);

	SetActorLocation(InitialLocation + OrbitOffset);
	AddActorWorldRotation(FRotator(0.f, RotationRateDegrees * DeltaTimeSinceLastUpdate, 0.f));

	LastSimulatedCostMs = static_cast<float>((FPlatformTime::Seconds() - StartTimeSeconds) * 1000.0);
	RefreshDebugText();
}

bool ATDBudgetTickTestActor::CanBudgetTick() const
{
	return bAllowBudgetTick;
}

float ATDBudgetTickTestActor::GetBudgetImportanceBias() const
{
	return RuntimeImportanceBias;
}

void ATDBudgetTickTestActor::RefreshDebugText()
{
	if (!DebugTextComponent)
	{
		return;
	}

	const FString DebugText = FString::Printf(
		TEXT("%s\nManaged: %s\nTicks: %d\nBudgetDt: %.3f\nGap: %.3f\nCost: %.3f ms"),
		*GetName(),
		bIsManagedByScheduler ? TEXT("Yes") : TEXT("No"),
		BudgetTickCount,
		LastBudgetDeltaTime,
		LastGapBetweenUpdates,
		LastSimulatedCostMs);

	DebugTextComponent->SetText(FText::FromString(DebugText));
	DebugTextComponent->SetTextRenderColor(bIsManagedByScheduler ? FColor::Green : FColor::Red);
}

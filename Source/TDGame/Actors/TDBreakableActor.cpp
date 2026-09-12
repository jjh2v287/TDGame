#include "Actors/TDBreakableActor.h"
#include "Field/FieldSystemComponent.h"
#include "Field/FieldSystemObjects.h"
#include "GeometryCollection/GeometryCollectionComponent.h"
#include "TimerManager.h"

#if WITH_EDITORONLY_DATA
#include "Components/SphereComponent.h"
#endif

ATDBreakableActor::ATDBreakableActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = false;

	FieldSystemComponent = CreateDefaultSubobject<UFieldSystemComponent>(TEXT("FieldSystemComponent"));
	RootComponent = FieldSystemComponent;

	GeometryCollectionComponent = CreateDefaultSubobject<UGeometryCollectionComponent>(TEXT("GeometryCollectionComponent"));
	GeometryCollectionComponent->SetupAttachment(FieldSystemComponent);
	GeometryCollectionComponent->SetCollisionProfileName(TEXT("BlockAll"));
	GeometryCollectionComponent->SetGenerateOverlapEvents(false);

#if WITH_EDITORONLY_DATA
	DebugSphereComponent = CreateEditorOnlyDefaultSubobject<USphereComponent>(TEXT("DebugSphereComponent"));
	if (DebugSphereComponent)
	{
		DebugSphereComponent->SetupAttachment(FieldSystemComponent);
		DebugSphereComponent->SetSphereRadius(BreakForceRadius);
		DebugSphereComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		DebugSphereComponent->SetHiddenInGame(true);
		DebugSphereComponent->ShapeColor = DebugSphereColor;
	}
#endif
}

void ATDBreakableActor::BeginPlay()
{
	Super::BeginPlay();
	GeometryCollectionComponent->OnChaosBreakEvent.AddUniqueDynamic(this, &ATDBreakableActor::OnChaosBreakEvent);
}

void ATDBreakableActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GeometryCollectionComponent->OnChaosBreakEvent.RemoveAll(this);
	Super::EndPlay(EndPlayReason);
}

#if WITH_EDITOR
void ATDBreakableActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (!DebugSphereComponent)
	{
		return;
	}

	const FName PropertyName = PropertyChangedEvent.GetPropertyName();
	if (PropertyName == GET_MEMBER_NAME_CHECKED(ATDBreakableActor, BreakForceRadius))
	{
		DebugSphereComponent->SetSphereRadius(BreakForceRadius);
		return;
	}

	if (PropertyName == GET_MEMBER_NAME_CHECKED(ATDBreakableActor, bShowDebugSphere))
	{
		DebugSphereComponent->SetVisibility(bShowDebugSphere);
		return;
	}

	if (PropertyName == GET_MEMBER_NAME_CHECKED(ATDBreakableActor, DebugSphereColor))
	{
		DebugSphereComponent->ShapeColor = DebugSphereColor;
		DebugSphereComponent->MarkRenderStateDirty();
	}
}
#endif

void ATDBreakableActor::StartBreak(const FVector& HitLocation)
{
	if (bIsBroken)
	{
		return;
	}

	bIsBroken = true;

	GeometryCollectionComponent->SetCollisionObjectType(FragmentObjectChannel);
	GeometryCollectionComponent->WakeAllRigidBodies();

	if (bUseRadialForce && FieldSystemComponent)
	{
		ApplyRadialBreakForce(HitLocation);
	}
	else
	{
		ApplyImpulseBreakForce(HitLocation);
	}

	OnBreak(HitLocation);
	ScheduleAutoDestroy();
}

void ATDBreakableActor::ApplyRadialBreakForce(const FVector& HitLocation)
{
	URadialVector* RadialVector = NewObject<URadialVector>(this);
	RadialVector->Magnitude = BreakForce;
	RadialVector->Position = HitLocation;

	URadialFalloff* Falloff = NewObject<URadialFalloff>(this);
	Falloff->Position = HitLocation;
	Falloff->Radius = BreakForceRadius;
	Falloff->Magnitude = 1.0f;
	Falloff->MinRange = 0.0f;
	Falloff->MaxRange = BreakForceRadius;
	Falloff->Default = 0.0f;
	Falloff->Falloff = EFieldFalloffType::Field_Falloff_Linear;

	UOperatorField* FinalField = NewObject<UOperatorField>(this);
	FinalField->SetOperatorField(1.0f, RadialVector, Falloff, EFieldOperationType::Field_Multiply);

	UFieldSystemMetaDataFilter* MetaData = nullptr;
	if (bUseObjectTypeFilter)
	{
		MetaData = NewObject<UFieldSystemMetaDataFilter>(this);
		MetaData->SetMetaDataFilterType(
			EFieldFilterType::Field_Filter_Max,
			FieldObjectType,
			EFieldPositionType::Field_Position_CenterOfMass);
	}

	FieldSystemComponent->ApplyPhysicsField(
		true,
		EFieldPhysicsType::Field_LinearVelocity,
		MetaData,
		FinalField);
}

void ATDBreakableActor::ApplyImpulseBreakForce(const FVector& HitLocation)
{
	const FVector BreakDirection = (GeometryCollectionComponent->GetComponentLocation() - HitLocation).GetSafeNormal();
	GeometryCollectionComponent->AddImpulse(BreakDirection * BreakForce, NAME_None, true);
}

void ATDBreakableActor::ScheduleAutoDestroy()
{
	if (!bAutoDestroyActor)
	{
		return;
	}

	FTimerHandle TimerHandle;
	GetWorldTimerManager().SetTimer(TimerHandle, FTimerDelegate::CreateWeakLambda(this, [this]
	{
		Destroy();
	}), DestroyDelay, false);
}

void ATDBreakableActor::OnChaosBreakEvent(const FChaosBreakEvent& BreakEvent)
{
	if (!bIsBroken)
	{
		GeometryCollectionComponent->SetCollisionObjectType(FragmentObjectChannel);
		bIsBroken = true;

		OnBreak(GeometryCollectionComponent->GetComponentLocation());
		ScheduleAutoDestroy();
	}

	GeometryCollectionComponent->OnChaosBreakEvent.RemoveAll(this);
}

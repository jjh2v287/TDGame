#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "TDTopDownOcclusionSubsystem.generated.h"

class APawn;
class UMaterialParameterCollection;
class UMaterialParameterCollectionInstance;
class UTDTopDownSettings;
struct FStreamableHandle;

UCLASS()
class TDGAME_API UTDTopDownOcclusionSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	UMaterialParameterCollection* GetParameterCollection() const;

	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override;
	virtual TStatId GetStatId() const override;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	struct FTDOcclusionShape
	{
		float RadiusCm = 0.0f;
		float SoftnessCm = 0.0f;
		float KeepBaseCm = 0.0f;
		float MaxCut = 0.0f;

		bool operator==(const FTDOcclusionShape& Other) const
		{
			return RadiusCm == Other.RadiusCm && SoftnessCm == Other.SoftnessCm && KeepBaseCm == Other.KeepBaseCm && MaxCut == Other.MaxCut;
		}
	};

	void ApplyShape(UMaterialParameterCollectionInstance& Instance, const UTDTopDownSettings& Settings);
	void ApplyFocus(UMaterialParameterCollectionInstance& Instance, const UTDTopDownSettings& Settings, const APawn& Pawn);
	void ApplyDisabledFocus(UMaterialParameterCollectionInstance& Instance);
	void HandleParameterCollectionLoaded();

	UPROPERTY(Transient)
	TObjectPtr<UMaterialParameterCollection> ParameterCollection;

	TSharedPtr<FStreamableHandle> ParameterCollectionHandle;

	FTDOcclusionShape AppliedShape;
	bool bHasAppliedShape = false;
	bool bHasAppliedDisabledFocus = false;
};

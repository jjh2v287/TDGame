#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "TDMonsterSpeciesAsset.generated.h"

class UAnimationAsset;
class USkeletalMesh;

USTRUCT(BlueprintType)
struct TDGAME_API FTDMonsterAnimClip
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Animation")
	TObjectPtr<UAnimationAsset> Animation;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Animation", meta=(ClampMin="0"))
	float StartSeconds = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Animation", meta=(ClampMin="0", ToolTip="0 plays to the end of the asset."))
	float EndSeconds = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Animation", meta=(ClampMin="0.05"))
	float PlayRate = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Animation", meta=(ClampMin="0", ToolTip="Clip time, measured from StartSeconds, at which the strike visually lands."))
	float ImpactSeconds = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Animation", meta=(ClampMin="0"))
	float RecoverySeconds = 0.3f;

	bool IsValidClip() const
	{
		return Animation != nullptr;
	}
};

UCLASS(BlueprintType)
class TDGAME_API UTDMonsterSpeciesAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Brain", meta=(ToolTip="File name without extension in Content/MonsterAI/Definitions."))
	FName DefinitionId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Body")
	TObjectPtr<USkeletalMesh> Mesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Body", meta=(ClampMin="0.05"))
	float MeshScale = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Body")
	float MeshYaw = -90.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Body")
	float MeshHeightOffset = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Body", meta=(ClampMin="5"))
	float CapsuleRadius = 40.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Body", meta=(ClampMin="10"))
	float CapsuleHalfHeight = 90.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Animation")
	FTDMonsterAnimClip Idle;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Animation")
	FTDMonsterAnimClip Walk;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Animation")
	FTDMonsterAnimClip Run;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Animation", meta=(ClampMin="1", ToolTip="Ground speed in cm/s at which Walk plays at rate 1, before mesh scale."))
	float WalkClipSpeed = 150.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Animation", meta=(ClampMin="1", ToolTip="Ground speed in cm/s at which Run plays at rate 1, before mesh scale."))
	float RunClipSpeed = 450.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Animation")
	FTDMonsterAnimClip Hit;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Animation")
	FTDMonsterAnimClip Death;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Animation", meta=(ToolTip="Keyed by the ability name in the monster definition JSON."))
	TMap<FName, FTDMonsterAnimClip> AbilityClips;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Death")
	bool bUseRagdollWithoutDeathClip = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Death", meta=(ClampMin="0"))
	float CorpseLifeSeconds = 8.f;
};

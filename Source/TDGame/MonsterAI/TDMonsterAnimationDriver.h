#pragma once

#include "CoreMinimal.h"
#include "MonsterAI/TDMonsterSpeciesAsset.h"

class USkeletalMeshComponent;

class TDGAME_API FTDMonsterAnimationDriver
{
public:
	void Initialize(USkeletalMeshComponent* InMesh, const UTDMonsterSpeciesAsset* InSpecies);
	void UpdateLocomotion(float PlanarSpeed);
	float PlayAction(const FTDMonsterAnimClip& Clip, float PlayRate);
	void StopAction();
	float PlayDeath();
	void Tick(float DeltaSeconds);
	bool IsPlayingAction() const { return Mode == ETDMonsterAnimMode::Action; }

private:
	enum class ETDMonsterAnimMode : uint8
	{
		None,
		Idle,
		Walk,
		Run,
		Action,
		Death
	};

	void PlayClip(const FTDMonsterAnimClip& Clip, ETDMonsterAnimMode NewMode, bool bShouldLoop, float PlayRate);
	ETDMonsterAnimMode ChooseLocomotionMode(float PlanarSpeed) const;
	const FTDMonsterAnimClip* FindLocomotionClip(ETDMonsterAnimMode LocomotionMode) const;
	float GetClipEnd(const FTDMonsterAnimClip& Clip) const;

	TWeakObjectPtr<USkeletalMeshComponent> Mesh;
	TWeakObjectPtr<const UTDMonsterSpeciesAsset> Species;
	FTDMonsterAnimClip ActiveClip;
	ETDMonsterAnimMode Mode = ETDMonsterAnimMode::None;
	bool bIsLooping = false;
	float ScaledWalkSpeed = 150.f;
	float ScaledRunSpeed = 450.f;
};

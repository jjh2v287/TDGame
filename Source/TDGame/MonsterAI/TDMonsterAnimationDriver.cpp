#include "MonsterAI/TDMonsterAnimationDriver.h"
#include "Animation/AnimationAsset.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Components/SkeletalMeshComponent.h"

namespace
{
	constexpr float IdleSpeedThreshold = 15.f;
	constexpr float RunHysteresisRatio = 0.12f;
	constexpr float MinLocomotionRate = 0.5f;
	constexpr float MaxLocomotionRate = 2.f;
}

void FTDMonsterAnimationDriver::Initialize(USkeletalMeshComponent* InMesh, const UTDMonsterSpeciesAsset* InSpecies)
{
	Mesh = InMesh;
	Species = InSpecies;
	Mode = ETDMonsterAnimMode::None;
	if (!InSpecies)
	{
		return;
	}
	ScaledWalkSpeed = InSpecies->WalkClipSpeed * InSpecies->MeshScale;
	ScaledRunSpeed = InSpecies->RunClipSpeed * InSpecies->MeshScale;
	UpdateLocomotion(0.f);
}

void FTDMonsterAnimationDriver::UpdateLocomotion(const float PlanarSpeed)
{
	if (Mode == ETDMonsterAnimMode::Action || Mode == ETDMonsterAnimMode::Death)
	{
		return;
	}
	const ETDMonsterAnimMode DesiredMode = ChooseLocomotionMode(PlanarSpeed);
	const FTDMonsterAnimClip* Clip = FindLocomotionClip(DesiredMode);
	if (!Clip)
	{
		return;
	}
	const float ReferenceSpeed = DesiredMode == ETDMonsterAnimMode::Run ? ScaledRunSpeed : ScaledWalkSpeed;
	const float Rate = DesiredMode == ETDMonsterAnimMode::Idle ? Clip->PlayRate : FMath::Clamp(PlanarSpeed / FMath::Max(ReferenceSpeed, 1.f), MinLocomotionRate, MaxLocomotionRate) * Clip->PlayRate;
	if (DesiredMode != Mode)
	{
		PlayClip(*Clip, DesiredMode, true, Rate);
		return;
	}
	USkeletalMeshComponent* MeshComponent = Mesh.Get();
	UAnimSingleNodeInstance* Instance = MeshComponent ? MeshComponent->GetSingleNodeInstance() : nullptr;
	if (Instance)
	{
		Instance->SetPlayRate(Rate);
	}
}

float FTDMonsterAnimationDriver::PlayAction(const FTDMonsterAnimClip& Clip, const float PlayRate)
{
	if (!Clip.IsValidClip() || Mode == ETDMonsterAnimMode::Death)
	{
		return 0.f;
	}
	const float SafeRate = FMath::Max(PlayRate, 0.05f);
	PlayClip(Clip, ETDMonsterAnimMode::Action, false, SafeRate);
	return (GetClipEnd(Clip) - Clip.StartSeconds) / SafeRate;
}

void FTDMonsterAnimationDriver::StopAction()
{
	if (Mode == ETDMonsterAnimMode::Action)
	{
		Mode = ETDMonsterAnimMode::None;
	}
}

float FTDMonsterAnimationDriver::PlayDeath()
{
	const UTDMonsterSpeciesAsset* SpeciesAsset = Species.Get();
	if (!SpeciesAsset || !SpeciesAsset->Death.IsValidClip())
	{
		return 0.f;
	}
	const FTDMonsterAnimClip& Clip = SpeciesAsset->Death;
	const float SafeRate = FMath::Max(Clip.PlayRate, 0.05f);
	PlayClip(Clip, ETDMonsterAnimMode::Death, false, SafeRate);
	return FMath::Max((GetClipEnd(Clip) - Clip.StartSeconds) / SafeRate, 0.01f);
}

void FTDMonsterAnimationDriver::Tick(float DeltaSeconds)
{
	USkeletalMeshComponent* MeshComponent = Mesh.Get();
	UAnimSingleNodeInstance* Instance = MeshComponent ? MeshComponent->GetSingleNodeInstance() : nullptr;
	if (!Instance || Mode == ETDMonsterAnimMode::None)
	{
		return;
	}
	const float ClipEnd = GetClipEnd(ActiveClip);
	const float CurrentTime = Instance->GetCurrentTime();
	if (CurrentTime < ClipEnd)
	{
		return;
	}
	if (bIsLooping)
	{
		const float LoopLength = FMath::Max(ClipEnd - ActiveClip.StartSeconds, 0.01f);
		Instance->SetPosition(ActiveClip.StartSeconds + FMath::Fmod(CurrentTime - ClipEnd, LoopLength), false);
		return;
	}
	if (Mode == ETDMonsterAnimMode::Death)
	{
		Instance->SetPosition(ClipEnd, false);
		Instance->SetPlaying(false);
		return;
	}
	Mode = ETDMonsterAnimMode::None;
}

void FTDMonsterAnimationDriver::PlayClip(const FTDMonsterAnimClip& Clip, const ETDMonsterAnimMode NewMode, const bool bShouldLoop, const float PlayRate)
{
	USkeletalMeshComponent* MeshComponent = Mesh.Get();
	if (!MeshComponent || !Clip.Animation)
	{
		return;
	}
	MeshComponent->PlayAnimation(Clip.Animation, bShouldLoop);
	UAnimSingleNodeInstance* Instance = MeshComponent->GetSingleNodeInstance();
	if (!Instance)
	{
		return;
	}
	Instance->SetPosition(Clip.StartSeconds, false);
	Instance->SetPlayRate(PlayRate);
	ActiveClip = Clip;
	Mode = NewMode;
	bIsLooping = bShouldLoop;
}

FTDMonsterAnimationDriver::ETDMonsterAnimMode FTDMonsterAnimationDriver::ChooseLocomotionMode(const float PlanarSpeed) const
{
	if (PlanarSpeed < IdleSpeedThreshold)
	{
		return ETDMonsterAnimMode::Idle;
	}
	const float RunThreshold = (ScaledWalkSpeed + ScaledRunSpeed) * 0.5f;
	const float Margin = RunThreshold * RunHysteresisRatio;
	if (Mode == ETDMonsterAnimMode::Run)
	{
		return PlanarSpeed > RunThreshold - Margin ? ETDMonsterAnimMode::Run : ETDMonsterAnimMode::Walk;
	}
	return PlanarSpeed > RunThreshold + Margin ? ETDMonsterAnimMode::Run : ETDMonsterAnimMode::Walk;
}

const FTDMonsterAnimClip* FTDMonsterAnimationDriver::FindLocomotionClip(const ETDMonsterAnimMode LocomotionMode) const
{
	const UTDMonsterSpeciesAsset* SpeciesAsset = Species.Get();
	if (!SpeciesAsset)
	{
		return nullptr;
	}
	const FTDMonsterAnimClip* Candidates[] = { &SpeciesAsset->Run, &SpeciesAsset->Walk, &SpeciesAsset->Idle };
	const int32 FirstCandidate = LocomotionMode == ETDMonsterAnimMode::Run ? 0 : (LocomotionMode == ETDMonsterAnimMode::Walk ? 1 : 2);
	for (int32 Index = FirstCandidate; Index < UE_ARRAY_COUNT(Candidates); ++Index)
	{
		if (Candidates[Index]->IsValidClip())
		{
			return Candidates[Index];
		}
	}
	return nullptr;
}

float FTDMonsterAnimationDriver::GetClipEnd(const FTDMonsterAnimClip& Clip) const
{
	const float AssetLength = Clip.Animation ? Clip.Animation->GetPlayLength() : 0.f;
	if (Clip.EndSeconds > Clip.StartSeconds)
	{
		return AssetLength > 0.f ? FMath::Min(Clip.EndSeconds, AssetLength) : Clip.EndSeconds;
	}
	return AssetLength;
}

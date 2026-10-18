#if WITH_DEV_AUTOMATION_TESTS

#include "Animation/AnimCurveFilter.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimationAsset.h"
#include "Animation/AnimationPoseData.h"
#include "Animation/AttributesRuntime.h"
#include "AssetRegistry/ARFilter.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "BonePose.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/AutomationTest.h"
#include "Misc/MemStack.h"
#include "MonsterAI/TDMonsterDefinition.h"
#include "MonsterAI/TDMonsterDefinitionLibrary.h"
#include "MonsterAI/TDMonsterSpeciesAsset.h"

namespace TDMonsterSpeciesAnimationTestSupport
{
	constexpr int32 MinimumSpeciesCount = 6;
	constexpr float StaticStepCentimeters = 0.05f;
	constexpr float MaximumStaticTailFraction = 0.05f;
	constexpr float MaximumSeamPercentOfPoseSize = 3.f;
	constexpr float StartToleranceFrames = 0.5f;
	constexpr float EndToleranceFrames = 1.f;
	constexpr float MaximumSeamStepRatio = 3.f;
	constexpr float MinimumInteriorStepCentimeters = 0.5f;
	const TCHAR* SpeciesFolderPath = TEXT("/Game/MonsterAI/Species");

	struct FTDSpeciesClipMeasurement
	{
		int32 NumFrames = 0;
		float PlayLength = 0.f;
		int32 MotionEndFrame = 0;
		float StaticTailFraction = 0.f;
		float SeamCentimeters = 0.f;
		float SeamPercentOfPoseSize = 0.f;
		FName SeamBoneName;
		float SeamStepRatio = 0.f;
		FName SeamStepBoneName;
	};

	class FTDSpeciesClipPoseSampler
	{
	public:
		explicit FTDSpeciesClipPoseSampler(const USkeletalMesh& InMesh)
			: Mesh(InMesh)
		{
			const int32 NumBones = Mesh.GetRefSkeleton().GetNum();
			TArray<FBoneIndexType> RequiredBones;
			RequiredBones.Reserve(NumBones);
			for (int32 BoneIndex = 0; BoneIndex < NumBones; ++BoneIndex)
			{
				RequiredBones.Add(static_cast<FBoneIndexType>(BoneIndex));
			}
			BoneContainer.InitializeTo(RequiredBones, UE::Anim::FCurveFilterSettings(UE::Anim::ECurveFilterMode::DisallowAll), Mesh);
		}

		int32 GetNumBones() const
		{
			return Mesh.GetRefSkeleton().GetNum();
		}

		FName GetBoneName(int32 BoneIndex) const
		{
			return Mesh.GetRefSkeleton().GetBoneName(BoneIndex);
		}

		float ComputeReferencePoseSize() const
		{
			const FReferenceSkeleton& ReferenceSkeleton = Mesh.GetRefSkeleton();
			const TArray<FTransform>& LocalPose = ReferenceSkeleton.GetRefBonePose();
			TArray<FTransform> ComponentPose;
			ComponentPose.SetNum(LocalPose.Num());
			FBox Bounds(ForceInit);
			for (int32 BoneIndex = 0; BoneIndex < LocalPose.Num(); ++BoneIndex)
			{
				const int32 ParentIndex = ReferenceSkeleton.GetParentIndex(BoneIndex);
				ComponentPose[BoneIndex] = ParentIndex == INDEX_NONE ? LocalPose[BoneIndex] : LocalPose[BoneIndex] * ComponentPose[ParentIndex];
				Bounds += ComponentPose[BoneIndex].GetLocation();
			}
			return Bounds.GetSize().GetMax();
		}

		void SampleRootRelativeLocations(const UAnimSequence& Sequence, double SampleTime, TArray<FVector>& OutLocations) const
		{
			FMemMark Mark(FMemStack::Get());
			FCompactPose Pose;
			Pose.SetBoneContainer(&BoneContainer);
			Pose.ResetToRefPose();
			FBlendedCurve Curve;
			Curve.InitFrom(BoneContainer);
			UE::Anim::FStackAttributeContainer Attributes;
			FAnimationPoseData PoseData(Pose, Curve, Attributes);
			Sequence.GetAnimationPose(PoseData, FAnimExtractContext(SampleTime, false));

			FCSPose<FCompactPose> ComponentSpacePose;
			ComponentSpacePose.InitPose(MoveTemp(Pose));
			const int32 NumBones = GetNumBones();
			OutLocations.SetNum(NumBones);
			for (int32 BoneIndex = 0; BoneIndex < NumBones; ++BoneIndex)
			{
				OutLocations[BoneIndex] = ComponentSpacePose.GetComponentSpaceTransform(BoneContainer.MakeCompactPoseIndex(FMeshPoseBoneIndex(BoneIndex))).GetLocation();
			}
			const FVector RootLocation = OutLocations[0];
			for (FVector& Location : OutLocations)
			{
				Location -= RootLocation;
			}
		}

	private:
		const USkeletalMesh& Mesh;
		FBoneContainer BoneContainer;
	};

	float FindLargestBoneOffset(const TArray<FVector>& From, const TArray<FVector>& To, int32& OutBoneIndex)
	{
		float LargestOffset = 0.f;
		OutBoneIndex = INDEX_NONE;
		for (int32 BoneIndex = 1; BoneIndex < From.Num(); ++BoneIndex)
		{
			const float Offset = FVector::Dist(From[BoneIndex], To[BoneIndex]);
			if (Offset > LargestOffset)
			{
				LargestOffset = Offset;
				OutBoneIndex = BoneIndex;
			}
		}
		return LargestOffset;
	}

	void RecordLargestBoneSteps(const TArray<FVector>& From, const TArray<FVector>& To, TArray<float>& InOutLargestSteps)
	{
		for (int32 BoneIndex = 1; BoneIndex < From.Num(); ++BoneIndex)
		{
			InOutLargestSteps[BoneIndex] = FMath::Max(InOutLargestSteps[BoneIndex], static_cast<float>(FVector::Dist(From[BoneIndex], To[BoneIndex])));
		}
	}

	void FindLargestSeamStepRatio(const FTDSpeciesClipPoseSampler& Sampler, const TArray<float>& InteriorLargestSteps, const TArray<float>& EndSteps, FTDSpeciesClipMeasurement& InOutMeasurement)
	{
		for (int32 BoneIndex = 1; BoneIndex < EndSteps.Num(); ++BoneIndex)
		{
			if (InteriorLargestSteps[BoneIndex] < MinimumInteriorStepCentimeters)
			{
				continue;
			}
			const float Ratio = EndSteps[BoneIndex] / InteriorLargestSteps[BoneIndex];
			if (Ratio > InOutMeasurement.SeamStepRatio)
			{
				InOutMeasurement.SeamStepRatio = Ratio;
				InOutMeasurement.SeamStepBoneName = Sampler.GetBoneName(BoneIndex);
			}
		}
	}

	FTDSpeciesClipMeasurement MeasureSpeciesClip(const FTDSpeciesClipPoseSampler& Sampler, const UAnimSequence& Sequence, float PoseSize)
	{
		FTDSpeciesClipMeasurement Measurement;
		Measurement.NumFrames = FMath::Max(Sequence.GetNumberOfSampledKeys() - 1, 1);
		Measurement.PlayLength = Sequence.GetPlayLength();

		TArray<FVector> FirstLocations;
		TArray<FVector> PreviousLocations;
		TArray<FVector> CurrentLocations;
		Sampler.SampleRootRelativeLocations(Sequence, 0.0, FirstLocations);
		PreviousLocations = FirstLocations;
		TArray<float> InteriorLargestSteps;
		TArray<float> EndSteps;
		InteriorLargestSteps.Init(0.f, FirstLocations.Num());
		EndSteps.Init(0.f, FirstLocations.Num());
		for (int32 Frame = 1; Frame <= Measurement.NumFrames; ++Frame)
		{
			const bool bIsEndFrame = Frame == Measurement.NumFrames;
			const double SampleTime = bIsEndFrame ? Measurement.PlayLength : Measurement.PlayLength * Frame / Measurement.NumFrames;
			Sampler.SampleRootRelativeLocations(Sequence, SampleTime, CurrentLocations);
			RecordLargestBoneSteps(PreviousLocations, CurrentLocations, bIsEndFrame ? EndSteps : InteriorLargestSteps);
			int32 UnusedBoneIndex = INDEX_NONE;
			if (FindLargestBoneOffset(PreviousLocations, CurrentLocations, UnusedBoneIndex) > StaticStepCentimeters)
			{
				Measurement.MotionEndFrame = Frame;
			}
			Swap(PreviousLocations, CurrentLocations);
		}

		int32 SeamBoneIndex = INDEX_NONE;
		Measurement.SeamCentimeters = FindLargestBoneOffset(FirstLocations, PreviousLocations, SeamBoneIndex);
		Measurement.SeamBoneName = SeamBoneIndex == INDEX_NONE ? NAME_None : Sampler.GetBoneName(SeamBoneIndex);
		Measurement.SeamPercentOfPoseSize = PoseSize > UE_SMALL_NUMBER ? 100.f * Measurement.SeamCentimeters / PoseSize : 0.f;
		Measurement.StaticTailFraction = static_cast<float>(Measurement.NumFrames - Measurement.MotionEndFrame) / Measurement.NumFrames;
		FindLargestSeamStepRatio(Sampler, InteriorLargestSteps, EndSteps, Measurement);
		return Measurement;
	}

	TArray<UTDMonsterSpeciesAsset*> LoadAllSpecies()
	{
		IAssetRegistry& AssetRegistry = IAssetRegistry::GetChecked();
		AssetRegistry.ScanPathsSynchronous(TArray<FString>{ FString(SpeciesFolderPath) });
		FARFilter Filter;
		Filter.PackagePaths.Add(FName(SpeciesFolderPath));
		Filter.ClassPaths.Add(UTDMonsterSpeciesAsset::StaticClass()->GetClassPathName());
		Filter.bRecursivePaths = true;
		TArray<FAssetData> SpeciesAssets;
		AssetRegistry.GetAssets(Filter, SpeciesAssets);
		SpeciesAssets.Sort([](const FAssetData& Left, const FAssetData& Right) { return Left.AssetName.LexicalLess(Right.AssetName); });

		TArray<UTDMonsterSpeciesAsset*> Species;
		for (const FAssetData& SpeciesAsset : SpeciesAssets)
		{
			if (UTDMonsterSpeciesAsset* Loaded = Cast<UTDMonsterSpeciesAsset>(SpeciesAsset.GetAsset()))
			{
				Species.Add(Loaded);
			}
		}
		return Species;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDMonsterSpeciesLocomotionClipsLoopTest, "TDGame.MonsterAI.Species.LocomotionClipsLoop", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDMonsterSpeciesLocomotionClipsLoopTest::RunTest(const FString& Parameters)
{
	using namespace TDMonsterSpeciesAnimationTestSupport;

	const TArray<UTDMonsterSpeciesAsset*> AllSpecies = LoadAllSpecies();
	TestTrue(FString::Printf(TEXT("At least %d monster species are found, found %d"), MinimumSpeciesCount, AllSpecies.Num()), AllSpecies.Num() >= MinimumSpeciesCount);

	for (const UTDMonsterSpeciesAsset* Species : AllSpecies)
	{
		const FString SpeciesName = Species->GetName();
		if (!TestNotNull(*FString::Printf(TEXT("%s has a mesh"), *SpeciesName), Species->Mesh.Get()))
		{
			continue;
		}
		const FTDSpeciesClipPoseSampler Sampler(*Species->Mesh);
		const float PoseSize = Sampler.ComputeReferencePoseSize();
		const TPair<const TCHAR*, const FTDMonsterAnimClip*> Roles[] = {
			{ TEXT("Idle"), &Species->Idle },
			{ TEXT("Walk"), &Species->Walk },
			{ TEXT("Run"), &Species->Run }
		};
		for (const TPair<const TCHAR*, const FTDMonsterAnimClip*>& Role : Roles)
		{
			const UAnimSequence* Sequence = Cast<UAnimSequence>(Role.Value->Animation);
			if (!TestNotNull(*FString::Printf(TEXT("%s %s has a locomotion animation sequence"), *SpeciesName, Role.Key), Sequence))
			{
				continue;
			}
			const FTDSpeciesClipMeasurement Measurement = MeasureSpeciesClip(Sampler, *Sequence, PoseSize);
			const float FrameSeconds = Measurement.PlayLength / Measurement.NumFrames;
			const float StartSeconds = Role.Value->StartSeconds;
			const float EndSeconds = Role.Value->EndSeconds;
			const bool bStartsAtZero = StartSeconds <= FrameSeconds * StartToleranceFrames;
			const bool bEndsAtAssetEnd = FMath::IsNearlyZero(EndSeconds) || FMath::Abs(EndSeconds - Measurement.PlayLength) <= FrameSeconds * EndToleranceFrames;
			const FString ClipLabel = FString::Printf(TEXT("%s %s (%s, %.3f s, %d frames)"), *SpeciesName, Role.Key, *Sequence->GetName(), Measurement.PlayLength, Measurement.NumFrames);

			AddInfo(FString::Printf(TEXT("%s: range %.3f~%.3f, motion end frame %d, static tail %.4f, seam %.3f cm (%.2f%% of %.1f cm, %s), seam step ratio %.2f (%s)"),
				*ClipLabel, StartSeconds, EndSeconds, Measurement.MotionEndFrame, Measurement.StaticTailFraction,
				Measurement.SeamCentimeters, Measurement.SeamPercentOfPoseSize, PoseSize, *Measurement.SeamBoneName.ToString(),
				Measurement.SeamStepRatio, *Measurement.SeamStepBoneName.ToString()));
			TestTrue(FString::Printf(TEXT("%s plays the whole asset because locomotion ignores clip ranges: Start %.3f s, End %.3f s"), *ClipLabel, StartSeconds, EndSeconds), bStartsAtZero && bEndsAtAssetEnd);
			TestTrue(FString::Printf(TEXT("%s has no frozen tail: static tail fraction %.4f (limit %.2f), last motion at frame %d"), *ClipLabel, Measurement.StaticTailFraction, MaximumStaticTailFraction, Measurement.MotionEndFrame), Measurement.StaticTailFraction <= MaximumStaticTailFraction);
			TestTrue(FString::Printf(TEXT("%s loops without a seam: %.3f cm at %s is %.2f%% of pose size %.1f cm (limit %.1f%%)"), *ClipLabel, Measurement.SeamCentimeters, *Measurement.SeamBoneName.ToString(), Measurement.SeamPercentOfPoseSize, PoseSize, MaximumSeamPercentOfPoseSize), Measurement.SeamPercentOfPoseSize <= MaximumSeamPercentOfPoseSize);
			TestTrue(FString::Printf(TEXT("%s keeps bone speed across the loop seam: last step of %s is %.2f times its largest interior step (limit %.1f)"), *ClipLabel, *Measurement.SeamStepBoneName.ToString(), Measurement.SeamStepRatio, MaximumSeamStepRatio), Measurement.SeamStepRatio <= MaximumSeamStepRatio);
			if (Role.Value == &Species->Idle)
			{
				TestTrue(FString::Printf(TEXT("%s has bLoop on because the idle base asset follows the sequence loop flag and would freeze after one play"), *ClipLabel), Sequence->bLoop);
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDMonsterSpeciesClipsCoverDefinitionTest, "TDGame.MonsterAI.Species.ClipsCoverDefinition", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDMonsterSpeciesClipsCoverDefinitionTest::RunTest(const FString& Parameters)
{
	using namespace TDMonsterSpeciesAnimationTestSupport;

	UTDMonsterDefinitionLibrary* Library = UTDMonsterDefinitionLibrary::Get();
	if (!TestNotNull(TEXT("Definition library exists"), Library))
	{
		return false;
	}
	for (const UTDMonsterSpeciesAsset* Species : LoadAllSpecies())
	{
		const FString SpeciesName = Species->GetName();
		const TSharedPtr<const FTDResolvedMonsterDefinition> Definition = Library->FindDefinition(Species->DefinitionId);
		if (!TestTrue(FString::Printf(TEXT("%s definition %s resolves"), *SpeciesName, *Species->DefinitionId.ToString()), Definition.IsValid()))
		{
			continue;
		}
		TSet<FName> DefinitionAbilityNames;
		for (const FTDResolvedAbility& Ability : Definition->Abilities)
		{
			DefinitionAbilityNames.Add(Ability.Name);
			const FTDMonsterAnimClip* Clip = Species->AbilityClips.Find(Ability.Name);
			TestTrue(FString::Printf(TEXT("%s plays an animation for ability %s"), *SpeciesName, *Ability.Name.ToString()), Clip && Cast<UAnimSequence>(Clip->Animation) != nullptr);
		}
		for (const TPair<FName, FTDMonsterAnimClip>& Clip : Species->AbilityClips)
		{
			TestTrue(FString::Printf(TEXT("%s ability clip %s names an ability of definition %s"), *SpeciesName, *Clip.Key.ToString(), *Species->DefinitionId.ToString()), DefinitionAbilityNames.Contains(Clip.Key));
		}
		TestTrue(FString::Printf(TEXT("%s has a hit reaction clip"), *SpeciesName), Cast<UAnimSequence>(Species->Hit.Animation) != nullptr);
		TestTrue(FString::Printf(TEXT("%s has a death clip"), *SpeciesName), Cast<UAnimSequence>(Species->Death.Animation) != nullptr);
		for (int32 FollowerIndex = 0; FollowerIndex < Species->FollowerMeshes.Num(); ++FollowerIndex)
		{
			const USkeletalMesh* FollowerMesh = Species->FollowerMeshes[FollowerIndex];
			if (!TestNotNull(*FString::Printf(TEXT("%s follower mesh %d is set"), *SpeciesName, FollowerIndex), FollowerMesh))
			{
				continue;
			}
			TestTrue(FString::Printf(TEXT("%s follower mesh %s shares the body skeleton"), *SpeciesName, *FollowerMesh->GetName()), Species->Mesh && FollowerMesh->GetSkeleton() == Species->Mesh->GetSkeleton());
		}
	}
	return true;
}

#endif

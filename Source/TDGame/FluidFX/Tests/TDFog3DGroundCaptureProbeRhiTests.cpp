#if WITH_DEV_AUTOMATION_TESTS

#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "DynamicRHI.h"
#include "Engine/AssetManager.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/StreamableManager.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "FluidFX/TDFluidTerrainCapture.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RHIGlobals.h"
#include "RenderingThread.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "TextureResource.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

DEFINE_LOG_CATEGORY_STATIC(LogTDFog3DGroundCaptureProbe, Log, All);

namespace TDFog3DGroundCaptureProbePrivate
{
	const TCHAR* ProbeTestName = TEXT("TDGame.FluidFXRhi.Fog3D.GroundCaptureAlignmentProbe");
	const TCHAR* CubeMeshPath = TEXT("/Engine/BasicShapes/Cube.Cube");
	const TCHAR* ProbeOutputRelativePath = TEXT("AgentOps/20261115/fog3d/stage2a/impl/probe-p1.json");
	constexpr double ColumnCm = 15.625;
	constexpr int32 CaptureSize = 64;
	const FIntPoint CaptureCenterCell(-32, 48);
	constexpr double FloorZCm = 0.0;
	constexpr double CaptureZCm = 800.0;
	constexpr float ClearDepth = 1.0e9f;
	constexpr double MissingDepthThreshold = 5.0e8;
	constexpr int32 StepCount = 8;
	constexpr int32 StepColumns = 8;
	constexpr double StepRiseCm = 20.0;
	constexpr double StepBoundaryOffsetColumns = 0.25;
	constexpr double MarkerTopCm = 100.0;
	const FVector2D MarkerMinColumns(2.25, 2.25);
	const FVector2D MarkerMaxColumns(5.75, 9.75);
	constexpr double UntaggedTopCm = 300.0;
	const FVector2D UntaggedMinColumns(40.25, 50.25);
	const FVector2D UntaggedMaxColumns(44.25, 54.25);
	constexpr double UntaggedVisibleMarginCm = 50.0;
	constexpr double SlabMarginCm = 200.0;
	constexpr double SlabBottomCm = -100.0;
	constexpr double CubeEdgeCm = 100.0;
	constexpr double AlignmentToleranceCm = 0.5;
	constexpr double LoadTimeoutSeconds = 60.0;
	constexpr int32 SettleFrameCount = 3;
	constexpr float SettleFrameSeconds = 1.0f / 60.0f;
	const double CandidateShifts[] = { -0.5, 0.0, 0.5 };

	struct FTDGroundProbeWorld
	{
		UWorld* World = nullptr;

		FTDGroundProbeWorld()
		{
			const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDFog3DGroundProbeWorld"));
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true);
		}

		~FTDGroundProbeWorld()
		{
			World->EndPlay(EEndPlayReason::Quit);
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
			FlushRenderingCommands();
		}

		void StepWorld()
		{
			++GFrameCounter;
			World->Tick(LEVELTICK_All, SettleFrameSeconds);
			FlushRenderingCommands();
		}
	};

	struct FTDGroundProbeCandidate
	{
		bool bSwapsAxes = false;
		bool bFlipsX = false;
		bool bFlipsY = false;
		double ShiftXColumns = 0.0;
		double ShiftYColumns = 0.0;
	};

	struct FTDGroundProbeScore
	{
		FTDGroundProbeCandidate Candidate;
		double MaxAbsErrorCm = 0.0;
		double MeanAbsErrorCm = 0.0;
		int32 OverToleranceCount = 0;
		int32 ComparedCount = 0;
	};

	struct FTDGroundProbeLevelStats
	{
		double HeightCm = 0.0;
		int32 Count = 0;
		double MinCm = TNumericLimits<double>::Max();
		double MaxCm = TNumericLimits<double>::Lowest();
		double SumCm = 0.0;
		double SumSquaredCm = 0.0;
	};

	struct FTDGroundProbeCaptureResult
	{
		TArray<float> RawDepths;
		bool bRenderInMainRenderer = false;
		bool bAutoCalculateOrthoPlanes = false;
		int32 ShowOnlyActorCount = 0;
	};

	bool HasRenderingRhi()
	{
		return !GUsingNullRHI && FApp::CanEverRender();
	}

	FIntPoint GetCaptureMinCell()
	{
		return CaptureCenterCell - FIntPoint(CaptureSize / 2, CaptureSize / 2);
	}

	FVector2D GetCaptureMinCornerCm()
	{
		const FIntPoint MinCell = GetCaptureMinCell();
		return FVector2D(MinCell.X * ColumnCm, MinCell.Y * ColumnCm);
	}

	FVector GetCaptureLocationCm()
	{
		return FVector(CaptureCenterCell.X * ColumnCm, CaptureCenterCell.Y * ColumnCm, CaptureZCm);
	}

	double GetStepBoundaryColumns(int32 StepIndex)
	{
		const double Offset = StepIndex % 2 == 0 ? StepBoundaryOffsetColumns : -StepBoundaryOffsetColumns;
		return StepIndex * StepColumns + Offset;
	}

	bool IsInsideColumnRect(const FVector2D& Point, const FVector2D& MinColumns, const FVector2D& MaxColumns)
	{
		return Point.X >= MinColumns.X && Point.X < MaxColumns.X && Point.Y >= MinColumns.Y && Point.Y < MaxColumns.Y;
	}

	double ComputeStairHeightCm(double ColumnX)
	{
		int32 RaisedStepCount = 0;
		for (int32 StepIndex = 0; StepIndex < StepCount; ++StepIndex)
		{
			if (ColumnX >= GetStepBoundaryColumns(StepIndex))
			{
				++RaisedStepCount;
			}
		}
		return RaisedStepCount * StepRiseCm;
	}

	double ComputeExpectedHeightCm(const FVector2D& ColumnPoint)
	{
		const double StairHeightCm = ComputeStairHeightCm(ColumnPoint.X);
		if (IsInsideColumnRect(ColumnPoint, MarkerMinColumns, MarkerMaxColumns))
		{
			return FMath::Max(StairHeightCm, MarkerTopCm);
		}
		return StairHeightCm;
	}

	FVector2D ResolveSampleColumns(const FTDGroundProbeCandidate& Candidate, int32 U, int32 V)
	{
		int32 ColumnX = Candidate.bSwapsAxes ? V : U;
		int32 ColumnY = Candidate.bSwapsAxes ? U : V;
		if (Candidate.bFlipsX)
		{
			ColumnX = CaptureSize - 1 - ColumnX;
		}
		if (Candidate.bFlipsY)
		{
			ColumnY = CaptureSize - 1 - ColumnY;
		}
		return FVector2D(ColumnX + 0.5 + Candidate.ShiftXColumns, ColumnY + 0.5 + Candidate.ShiftYColumns);
	}

	FString DescribeCandidateMapping(const FTDGroundProbeCandidate& Candidate)
	{
		const TCHAR* XSource = Candidate.bSwapsAxes ? TEXT("v") : TEXT("u");
		const TCHAR* YSource = Candidate.bSwapsAxes ? TEXT("u") : TEXT("v");
		const FString XText = Candidate.bFlipsX ? FString::Printf(TEXT("S-1-%s"), XSource) : FString(XSource);
		const FString YText = Candidate.bFlipsY ? FString::Printf(TEXT("S-1-%s"), YSource) : FString(YSource);
		return FString::Printf(TEXT("column X = %s %+.1f, column Y = %s %+.1f"), *XText, Candidate.ShiftXColumns, *YText, Candidate.ShiftYColumns);
	}

	bool IsMissingDepth(float RawDepth)
	{
		return RawDepth >= MissingDepthThreshold;
	}

	double DecodeHeightCm(float RawDepth)
	{
		return CaptureZCm - static_cast<double>(RawDepth) - FloorZCm;
	}

	AActor* SpawnTerrainBox(UWorld& World, UStaticMesh& CubeMesh, const FVector& MinCm, const FVector& MaxCm, bool bIsTerrain)
	{
		AActor* BoxActor = World.SpawnActor<AActor>();
		UStaticMeshComponent* BoxMesh = NewObject<UStaticMeshComponent>(BoxActor);
		BoxMesh->SetStaticMesh(&CubeMesh);
		BoxMesh->SetMobility(EComponentMobility::Movable);
		BoxActor->SetRootComponent(BoxMesh);
		BoxMesh->RegisterComponent();
		BoxActor->SetActorScale3D((MaxCm - MinCm) / CubeEdgeCm);
		BoxActor->SetActorLocation(0.5 * (MinCm + MaxCm));
		if (bIsTerrain)
		{
			BoxActor->Tags.Add(FTDFluidTerrainCapture::TerrainActorTag);
		}
		return BoxActor;
	}

	FVector ToWorldCm(const FVector2D& Columns, double ZCm)
	{
		const FVector2D MinCornerCm = GetCaptureMinCornerCm();
		return FVector(MinCornerCm.X + Columns.X * ColumnCm, MinCornerCm.Y + Columns.Y * ColumnCm, ZCm);
	}

	void SpawnStairTerrain(UWorld& World, UStaticMesh& CubeMesh)
	{
		const double MarginColumns = SlabMarginCm / ColumnCm;
		const double LowColumns = -MarginColumns;
		const double HighColumns = CaptureSize + MarginColumns;
		SpawnTerrainBox(World, CubeMesh, ToWorldCm(FVector2D(LowColumns, LowColumns), SlabBottomCm), ToWorldCm(FVector2D(HighColumns, HighColumns), FloorZCm), true);
		for (int32 StepIndex = 0; StepIndex < StepCount; ++StepIndex)
		{
			const double TopCm = FloorZCm + (StepIndex + 1) * StepRiseCm;
			SpawnTerrainBox(World, CubeMesh, ToWorldCm(FVector2D(GetStepBoundaryColumns(StepIndex), LowColumns), SlabBottomCm), ToWorldCm(FVector2D(HighColumns, HighColumns), TopCm), true);
		}
		SpawnTerrainBox(World, CubeMesh, ToWorldCm(MarkerMinColumns, SlabBottomCm), ToWorldCm(MarkerMaxColumns, FloorZCm + MarkerTopCm), true);
		SpawnTerrainBox(World, CubeMesh, ToWorldCm(UntaggedMinColumns, SlabBottomCm), ToWorldCm(UntaggedMaxColumns, FloorZCm + UntaggedTopCm), false);
	}

	UTextureRenderTarget2D* CreateDepthTarget()
	{
		UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(GetTransientPackage(), NAME_None, RF_Transient);
		Target->RenderTargetFormat = RTF_R32f;
		Target->ClearColor = FLinearColor(ClearDepth, 0.0f, 0.0f, 0.0f);
		Target->InitAutoFormat(CaptureSize, CaptureSize);
		Target->UpdateResourceImmediate(true);
		return Target;
	}

	USceneCaptureComponent2D* SpawnDepthCapture(UWorld& World, UTextureRenderTarget2D& Target)
	{
		AActor* CaptureHost = World.SpawnActor<AActor>();
		USceneCaptureComponent2D* Capture = NewObject<USceneCaptureComponent2D>(CaptureHost);
		Capture->SetUsingAbsoluteLocation(true);
		Capture->SetUsingAbsoluteRotation(true);
		Capture->ProjectionType = ECameraProjectionMode::Orthographic;
		Capture->CaptureSource = ESceneCaptureSource::SCS_SceneDepth;
		Capture->bCaptureEveryFrame = false;
		Capture->bCaptureOnMovement = false;
		Capture->TextureTarget = &Target;
		Capture->OrthoWidth = static_cast<float>(CaptureSize * ColumnCm);
		CaptureHost->SetRootComponent(Capture);
		Capture->RegisterComponent();
		Capture->SetWorldLocationAndRotation(GetCaptureLocationCm(), FRotator(-90.0, -90.0, 0.0));
		return Capture;
	}

	bool CaptureGroundDepth(FAutomationTestBase& Test, FTDGroundProbeWorld& ProbeWorld, UStaticMesh& CubeMesh, FTDGroundProbeCaptureResult& OutResult)
	{
		UWorld& World = *ProbeWorld.World;
		SpawnStairTerrain(World, CubeMesh);
		TStrongObjectPtr<UTextureRenderTarget2D> Target(CreateDepthTarget());
		USceneCaptureComponent2D* Capture = SpawnDepthCapture(World, *Target);
		for (int32 FrameIndex = 0; FrameIndex < SettleFrameCount; ++FrameIndex)
		{
			ProbeWorld.StepWorld();
		}
		FTDFluidTerrainCapture::ApplyTerrainOnlyList(*Capture, World);
		FTDFluidTerrainCapture::HideOwnerAndPawns(*Capture, Capture->GetOwner(), true, World);
		OutResult.bRenderInMainRenderer = Capture->bRenderInMainRenderer;
		OutResult.bAutoCalculateOrthoPlanes = Capture->bAutoCalculateOrthoPlanes;
		OutResult.ShowOnlyActorCount = Capture->ShowOnlyActors.Num();
		Capture->CaptureScene();
		FlushRenderingCommands();

		FTextureRenderTargetResource* Resource = Target->GameThread_GetRenderTargetResource();
		if (!Test.TestNotNull(TEXT("The depth capture target has a resource"), Resource))
		{
			return false;
		}
		TArray<FLinearColor> Pixels;
		if (!Test.TestTrue(TEXT("Read back the R32F depth capture"), Resource->ReadLinearColorPixels(Pixels)))
		{
			return false;
		}
		if (!Test.TestEqual(TEXT("Depth capture texel count is S x S"), Pixels.Num(), CaptureSize * CaptureSize))
		{
			return false;
		}
		OutResult.RawDepths.Reserve(Pixels.Num());
		for (const FLinearColor& Pixel : Pixels)
		{
			OutResult.RawDepths.Add(Pixel.R);
		}
		return true;
	}

	FTDGroundProbeScore ScoreCandidate(const TArray<float>& RawDepths, const FTDGroundProbeCandidate& Candidate)
	{
		FTDGroundProbeScore Score;
		Score.Candidate = Candidate;
		double SumAbsErrorCm = 0.0;
		for (int32 V = 0; V < CaptureSize; ++V)
		{
			for (int32 U = 0; U < CaptureSize; ++U)
			{
				const float RawDepth = RawDepths[V * CaptureSize + U];
				if (IsMissingDepth(RawDepth))
				{
					continue;
				}
				const double AbsErrorCm = FMath::Abs(DecodeHeightCm(RawDepth) - ComputeExpectedHeightCm(ResolveSampleColumns(Candidate, U, V)));
				Score.MaxAbsErrorCm = FMath::Max(Score.MaxAbsErrorCm, AbsErrorCm);
				SumAbsErrorCm += AbsErrorCm;
				Score.OverToleranceCount += AbsErrorCm > AlignmentToleranceCm ? 1 : 0;
				++Score.ComparedCount;
			}
		}
		Score.MeanAbsErrorCm = Score.ComparedCount > 0 ? SumAbsErrorCm / Score.ComparedCount : 0.0;
		return Score;
	}

	TArray<FTDGroundProbeScore> ScoreAllCandidates(const TArray<float>& RawDepths)
	{
		TArray<FTDGroundProbeScore> Scores;
		for (int32 AxisIndex = 0; AxisIndex < 8; ++AxisIndex)
		{
			for (const double ShiftX : CandidateShifts)
			{
				for (const double ShiftY : CandidateShifts)
				{
					FTDGroundProbeCandidate Candidate;
					Candidate.bSwapsAxes = (AxisIndex & 4) != 0;
					Candidate.bFlipsX = (AxisIndex & 2) != 0;
					Candidate.bFlipsY = (AxisIndex & 1) != 0;
					Candidate.ShiftXColumns = ShiftX;
					Candidate.ShiftYColumns = ShiftY;
					Scores.Add(ScoreCandidate(RawDepths, Candidate));
				}
			}
		}
		return Scores;
	}

	const FTDGroundProbeScore& FindBestScore(const TArray<FTDGroundProbeScore>& Scores)
	{
		int32 BestIndex = 0;
		for (int32 Index = 1; Index < Scores.Num(); ++Index)
		{
			const FTDGroundProbeScore& Best = Scores[BestIndex];
			const FTDGroundProbeScore& Other = Scores[Index];
			const bool bIsBetterMean = Other.MeanAbsErrorCm < Best.MeanAbsErrorCm;
			const bool bIsTieWithBetterMax = Other.MeanAbsErrorCm == Best.MeanAbsErrorCm && Other.MaxAbsErrorCm < Best.MaxAbsErrorCm;
			if (bIsBetterMean || bIsTieWithBetterMax)
			{
				BestIndex = Index;
			}
		}
		return Scores[BestIndex];
	}

	bool IsLevelInterior(const FVector2D& ColumnPoint)
	{
		const double CenterHeightCm = ComputeExpectedHeightCm(ColumnPoint);
		const FVector2D Offsets[] = { FVector2D(1.0, 0.0), FVector2D(-1.0, 0.0), FVector2D(0.0, 1.0), FVector2D(0.0, -1.0) };
		for (const FVector2D& Offset : Offsets)
		{
			if (ComputeExpectedHeightCm(ColumnPoint + Offset) != CenterHeightCm)
			{
				return false;
			}
		}
		return true;
	}

	TArray<FTDGroundProbeLevelStats> CollectLevelStats(const TArray<float>& RawDepths, const FTDGroundProbeCandidate& Candidate)
	{
		TMap<int32, FTDGroundProbeLevelStats> StatsByLevel;
		for (int32 V = 0; V < CaptureSize; ++V)
		{
			for (int32 U = 0; U < CaptureSize; ++U)
			{
				const float RawDepth = RawDepths[V * CaptureSize + U];
				const FVector2D ColumnPoint = ResolveSampleColumns(Candidate, U, V);
				if (IsMissingDepth(RawDepth) || !IsLevelInterior(ColumnPoint))
				{
					continue;
				}
				const double ExpectedCm = ComputeExpectedHeightCm(ColumnPoint);
				FTDGroundProbeLevelStats& Stats = StatsByLevel.FindOrAdd(FMath::RoundToInt32(ExpectedCm));
				const double HeightCm = DecodeHeightCm(RawDepth);
				Stats.HeightCm = ExpectedCm;
				++Stats.Count;
				Stats.MinCm = FMath::Min(Stats.MinCm, HeightCm);
				Stats.MaxCm = FMath::Max(Stats.MaxCm, HeightCm);
				Stats.SumCm += HeightCm;
				Stats.SumSquaredCm += HeightCm * HeightCm;
			}
		}
		TArray<FTDGroundProbeLevelStats> Levels;
		StatsByLevel.GenerateValueArray(Levels);
		Levels.Sort([](const FTDGroundProbeLevelStats& A, const FTDGroundProbeLevelStats& B) { return A.HeightCm < B.HeightCm; });
		return Levels;
	}

	double ComputeStdDevCm(const FTDGroundProbeLevelStats& Stats)
	{
		if (Stats.Count <= 0)
		{
			return 0.0;
		}
		const double MeanCm = Stats.SumCm / Stats.Count;
		return FMath::Sqrt(FMath::Max(Stats.SumSquaredCm / Stats.Count - MeanCm * MeanCm, 0.0));
	}

	TArray<TSharedPtr<FJsonValue>> MakeNumberArray(std::initializer_list<double> Values)
	{
		TArray<TSharedPtr<FJsonValue>> Array;
		for (const double Value : Values)
		{
			Array.Add(MakeShared<FJsonValueNumber>(Value));
		}
		return Array;
	}

	TSharedRef<FJsonObject> MakeScoreJson(const FTDGroundProbeScore& Score)
	{
		TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetStringField(TEXT("mapping"), DescribeCandidateMapping(Score.Candidate));
		Json->SetBoolField(TEXT("swapsAxes"), Score.Candidate.bSwapsAxes);
		Json->SetBoolField(TEXT("flipsX"), Score.Candidate.bFlipsX);
		Json->SetBoolField(TEXT("flipsY"), Score.Candidate.bFlipsY);
		Json->SetNumberField(TEXT("shiftXColumns"), Score.Candidate.ShiftXColumns);
		Json->SetNumberField(TEXT("shiftYColumns"), Score.Candidate.ShiftYColumns);
		Json->SetNumberField(TEXT("maxAbsErrorCm"), Score.MaxAbsErrorCm);
		Json->SetNumberField(TEXT("meanAbsErrorCm"), Score.MeanAbsErrorCm);
		Json->SetNumberField(TEXT("overHalfCmCount"), Score.OverToleranceCount);
		Json->SetNumberField(TEXT("comparedTexelCount"), Score.ComparedCount);
		return Json;
	}

	TSharedRef<FJsonObject> MakeCaptureJson(const FTDGroundProbeCaptureResult& Capture)
	{
		const FIntPoint MinCell = GetCaptureMinCell();
		const FVector Location = GetCaptureLocationCm();
		TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetNumberField(TEXT("size"), CaptureSize);
		Json->SetArrayField(TEXT("minCell"), MakeNumberArray({ static_cast<double>(MinCell.X), static_cast<double>(MinCell.Y) }));
		Json->SetArrayField(TEXT("centerCell"), MakeNumberArray({ static_cast<double>(CaptureCenterCell.X), static_cast<double>(CaptureCenterCell.Y) }));
		Json->SetArrayField(TEXT("locationCm"), MakeNumberArray({ Location.X, Location.Y, Location.Z }));
		Json->SetArrayField(TEXT("rotationPitchYawRoll"), MakeNumberArray({ -90.0, -90.0, 0.0 }));
		Json->SetNumberField(TEXT("orthoWidthCm"), CaptureSize * ColumnCm);
		Json->SetNumberField(TEXT("columnCm"), ColumnCm);
		Json->SetNumberField(TEXT("floorZCm"), FloorZCm);
		Json->SetNumberField(TEXT("clearDepth"), ClearDepth);
		Json->SetBoolField(TEXT("renderInMainRenderer"), Capture.bRenderInMainRenderer);
		Json->SetBoolField(TEXT("autoCalculateOrthoPlanes"), Capture.bAutoCalculateOrthoPlanes);
		Json->SetNumberField(TEXT("showOnlyActorCount"), Capture.ShowOnlyActorCount);
		return Json;
	}

	TSharedRef<FJsonObject> MakeTerrainJson()
	{
		TArray<TSharedPtr<FJsonValue>> Boundaries;
		for (int32 StepIndex = 0; StepIndex < StepCount; ++StepIndex)
		{
			Boundaries.Add(MakeShared<FJsonValueNumber>(GetStepBoundaryColumns(StepIndex)));
		}
		TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetStringField(TEXT("formula"), TEXT("H(x, y) = 20 cm x (number of step boundaries <= x), then max with 100 cm inside the marker; x, y in columns from the capture minimum corner"));
		Json->SetNumberField(TEXT("stepRiseCm"), StepRiseCm);
		Json->SetArrayField(TEXT("stepBoundariesColumns"), Boundaries);
		Json->SetArrayField(TEXT("markerMinColumns"), MakeNumberArray({ MarkerMinColumns.X, MarkerMinColumns.Y }));
		Json->SetArrayField(TEXT("markerMaxColumns"), MakeNumberArray({ MarkerMaxColumns.X, MarkerMaxColumns.Y }));
		Json->SetNumberField(TEXT("markerTopCm"), MarkerTopCm);
		Json->SetArrayField(TEXT("untaggedMinColumns"), MakeNumberArray({ UntaggedMinColumns.X, UntaggedMinColumns.Y }));
		Json->SetArrayField(TEXT("untaggedMaxColumns"), MakeNumberArray({ UntaggedMaxColumns.X, UntaggedMaxColumns.Y }));
		Json->SetNumberField(TEXT("untaggedTopCm"), UntaggedTopCm);
		return Json;
	}

	TSharedRef<FJsonObject> MakeDepthPrecisionJson(const TArray<float>& RawDepths, const FTDGroundProbeCandidate& Candidate)
	{
		const TArray<FTDGroundProbeLevelStats> Levels = CollectLevelStats(RawDepths, Candidate);
		TArray<TSharedPtr<FJsonValue>> LevelValues;
		double MaxSpreadCm = 0.0;
		double MaxStdDevCm = 0.0;
		for (const FTDGroundProbeLevelStats& Stats : Levels)
		{
			const double StdDevCm = ComputeStdDevCm(Stats);
			MaxSpreadCm = FMath::Max(MaxSpreadCm, Stats.MaxCm - Stats.MinCm);
			MaxStdDevCm = FMath::Max(MaxStdDevCm, StdDevCm);
			TSharedRef<FJsonObject> LevelJson = MakeShared<FJsonObject>();
			LevelJson->SetNumberField(TEXT("expectedHeightCm"), Stats.HeightCm);
			LevelJson->SetNumberField(TEXT("count"), Stats.Count);
			LevelJson->SetNumberField(TEXT("minCm"), Stats.MinCm);
			LevelJson->SetNumberField(TEXT("maxCm"), Stats.MaxCm);
			LevelJson->SetNumberField(TEXT("meanCm"), Stats.SumCm / FMath::Max(Stats.Count, 1));
			LevelJson->SetNumberField(TEXT("stdDevCm"), StdDevCm);
			LevelValues.Add(MakeShared<FJsonValueObject>(LevelJson));
		}
		TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetStringField(TEXT("note"), TEXT("texels whose sample point and its four one-column neighbours share the same analytic height, grouped by that height, using the best mapping"));
		Json->SetNumberField(TEXT("maxSpreadCm"), MaxSpreadCm);
		Json->SetNumberField(TEXT("maxStdDevCm"), MaxStdDevCm);
		Json->SetArrayField(TEXT("levels"), LevelValues);
		return Json;
	}

	TSharedRef<FJsonObject> MakeUntaggedJson(const TArray<float>& RawDepths)
	{
		const FTDGroundProbeCandidate AssumedCandidate;
		int32 TexelCount = 0;
		double MaxDecodedCm = TNumericLimits<double>::Lowest();
		double MaxExpectedCm = 0.0;
		for (int32 V = 0; V < CaptureSize; ++V)
		{
			for (int32 U = 0; U < CaptureSize; ++U)
			{
				const FVector2D ColumnPoint = ResolveSampleColumns(AssumedCandidate, U, V);
				const float RawDepth = RawDepths[V * CaptureSize + U];
				if (!IsInsideColumnRect(ColumnPoint, UntaggedMinColumns, UntaggedMaxColumns) || IsMissingDepth(RawDepth))
				{
					continue;
				}
				++TexelCount;
				MaxDecodedCm = FMath::Max(MaxDecodedCm, DecodeHeightCm(RawDepth));
				MaxExpectedCm = FMath::Max(MaxExpectedCm, ComputeExpectedHeightCm(ColumnPoint));
			}
		}
		TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetNumberField(TEXT("texelCount"), TexelCount);
		Json->SetNumberField(TEXT("maxDecodedCm"), TexelCount > 0 ? MaxDecodedCm : 0.0);
		Json->SetNumberField(TEXT("maxExpectedTerrainCm"), MaxExpectedCm);
		Json->SetBoolField(TEXT("untaggedBoxVisible"), TexelCount > 0 && MaxDecodedCm > MaxExpectedCm + UntaggedVisibleMarginCm);
		return Json;
	}

	void FindRawDepthRange(const TArray<float>& RawDepths, int32& OutMissingCount, double& OutMinDepth, double& OutMaxDepth)
	{
		OutMissingCount = 0;
		OutMinDepth = TNumericLimits<double>::Max();
		OutMaxDepth = TNumericLimits<double>::Lowest();
		for (const float RawDepth : RawDepths)
		{
			if (IsMissingDepth(RawDepth))
			{
				++OutMissingCount;
				continue;
			}
			OutMinDepth = FMath::Min(OutMinDepth, static_cast<double>(RawDepth));
			OutMaxDepth = FMath::Max(OutMaxDepth, static_cast<double>(RawDepth));
		}
	}

	FString WriteProbeJson(const FTDGroundProbeCaptureResult& Capture)
	{
		int32 MissingCount = 0;
		double MinDepth = 0.0;
		double MaxDepth = 0.0;
		FindRawDepthRange(Capture.RawDepths, MissingCount, MinDepth, MaxDepth);
		const bool bIsDrawn = MissingCount < Capture.RawDepths.Num();

		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetStringField(TEXT("test"), ProbeTestName);
		Root->SetStringField(TEXT("rhi"), GDynamicRHI ? GDynamicRHI->GetName() : TEXT("none"));
		Root->SetBoolField(TEXT("drawn"), bIsDrawn);
		Root->SetNumberField(TEXT("missingTexelCount"), MissingCount);
		Root->SetNumberField(TEXT("missingDepthThreshold"), MissingDepthThreshold);
		Root->SetNumberField(TEXT("alignmentToleranceCm"), AlignmentToleranceCm);
		Root->SetObjectField(TEXT("capture"), MakeCaptureJson(Capture));
		Root->SetObjectField(TEXT("terrain"), MakeTerrainJson());
		if (bIsDrawn)
		{
			Root->SetNumberField(TEXT("rawDepthMin"), MinDepth);
			Root->SetNumberField(TEXT("rawDepthMax"), MaxDepth);
			const TArray<FTDGroundProbeScore> Scores = ScoreAllCandidates(Capture.RawDepths);
			const FTDGroundProbeScore& Best = FindBestScore(Scores);
			Root->SetObjectField(TEXT("assumed"), MakeScoreJson(ScoreCandidate(Capture.RawDepths, FTDGroundProbeCandidate())));
			Root->SetObjectField(TEXT("best"), MakeScoreJson(Best));
			TArray<TSharedPtr<FJsonValue>> CandidateValues;
			for (const FTDGroundProbeScore& Score : Scores)
			{
				CandidateValues.Add(MakeShared<FJsonValueObject>(MakeScoreJson(Score)));
			}
			Root->SetArrayField(TEXT("candidates"), CandidateValues);
			Root->SetObjectField(TEXT("depthPrecision"), MakeDepthPrecisionJson(Capture.RawDepths, Best.Candidate));
			Root->SetObjectField(TEXT("untagged"), MakeUntaggedJson(Capture.RawDepths));
		}

		FString JsonText;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonText);
		FJsonSerializer::Serialize(Root, Writer);
		const FString OutputPath = FPaths::Combine(FPaths::ProjectSavedDir(), ProbeOutputRelativePath);
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(OutputPath), true);
		FFileHelper::SaveStringToFile(JsonText, *OutputPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		return OutputPath;
	}

	void RunProbe(FAutomationTestBase& Test, UStaticMesh& CubeMesh)
	{
		FTDGroundProbeCaptureResult Capture;
		{
			FTDGroundProbeWorld ProbeWorld;
			if (!CaptureGroundDepth(Test, ProbeWorld, CubeMesh, Capture))
			{
				return;
			}
		}
		const FString OutputPath = WriteProbeJson(Capture);
		const int32 DrawnCount = Capture.RawDepths.FilterByPredicate([](float RawDepth) { return !IsMissingDepth(RawDepth); }).Num();
		Test.AddInfo(FString::Printf(TEXT("Ground capture alignment probe wrote %s (%d of %d texels drawn)"), *OutputPath, DrawnCount, Capture.RawDepths.Num()));
		Test.TestTrue(TEXT("The terrain-only depth capture drew something (not every texel is the clear depth)"), DrawnCount > 0);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFog3DGroundCaptureAlignmentProbeTest,
	"TDGame.FluidFXRhi.Fog3D.GroundCaptureAlignmentProbe",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFog3DGroundCaptureAlignmentProbeTest::RunTest(const FString& Parameters)
{
	using namespace TDFog3DGroundCaptureProbePrivate;

	if (!HasRenderingRhi())
	{
		AddError(TEXT("Fog3D GroundCaptureAlignmentProbe needs a rendering RHI. Run with -RenderOffScreen (build_and_test.py --rhi offscreen)."));
		return false;
	}

	const FSoftObjectPath CubePath(CubeMeshPath);
	TSharedPtr<FStreamableHandle> LoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(TArray<FSoftObjectPath>{ CubePath });
	const double LoadStartSeconds = FPlatformTime::Seconds();
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, LoadHandle, CubePath, LoadStartSeconds]()
	{
		UStaticMesh* CubeMesh = Cast<UStaticMesh>(CubePath.ResolveObject());
		const bool bHasLoadEnded = !LoadHandle.IsValid() || LoadHandle->HasLoadCompletedOrStalled();
		if (!CubeMesh && !bHasLoadEnded && FPlatformTime::Seconds() - LoadStartSeconds < LoadTimeoutSeconds)
		{
			return false;
		}
		if (!TestNotNull(TEXT("Engine cube mesh finished its asynchronous load"), CubeMesh))
		{
			return true;
		}
		RunProbe(*this, *CubeMesh);
		return true;
	}));
	return true;
}

#endif

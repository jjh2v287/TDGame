#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "HAL/PlatformTime.h"
#include "Math/Float16Color.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RHIGlobals.h"
#include "RenderGraphBuilder.h"
#include "RenderingThread.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "TDFluidGpuBatch.h"
#include "TDFluidGpuFieldInput.h"
#include "TDFluidGpuStepPasses.h"
#include "TextureResource.h"
#include "UObject/StrongObjectPtr.h"

namespace TDFluidGpuFieldInputRhiTestPrivate
{
	constexpr int32 FieldRhiResolution = 64;
	constexpr double FieldRhiExtentCm = 3200.0;
	constexpr float FieldRhiDeltaTimeSeconds = 1.0f / 60.0f;
	constexpr float FieldRhiVelocityResponsePerSec = 6.0f;
	constexpr float FieldRhiOutputSpeedReferenceCmPerSec = 600.0f;
	constexpr double FieldRhiHalfFloatTolerance = 1.0e-3;

	struct FTDFieldRhiTargets
	{
		TStrongObjectPtr<UTextureRenderTarget2D> Density;
		TStrongObjectPtr<UTextureRenderTarget2D> Velocity;
	};

	struct FTDFieldRhiPixels
	{
		TArray<FFloat16Color> Density;
		TArray<FFloat16Color> Velocity;
	};

	bool CanRunFieldRhiTest(FAutomationTestBase& Test, const TCHAR* TestName)
	{
		if (!GUsingNullRHI && FApp::CanEverRender())
		{
			return true;
		}
		Test.AddError(FString::Printf(TEXT("%s needs a rendering RHI. Run with build_and_test.py --rhi offscreen."), TestName));
		return false;
	}

	UTexture2D* CreateFieldTexture(int32 Size, EPixelFormat Format, TFunctionRef<FLinearColor(int32, int32)> MakeTexel)
	{
		UTexture2D* Texture = UTexture2D::CreateTransient(Size, Size, Format);
		Texture->SRGB = false;
		Texture->Filter = TF_Bilinear;
		FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
		void* MipData = Mip.BulkData.Lock(LOCK_READ_WRITE);
		for (int32 TexelY = 0; TexelY < Size; ++TexelY)
		{
			for (int32 TexelX = 0; TexelX < Size; ++TexelX)
			{
				const FLinearColor Value = MakeTexel(TexelX, TexelY);
				const int32 Index = TexelY * Size + TexelX;
				if (Format == PF_FloatRGBA)
				{
					static_cast<FFloat16Color*>(MipData)[Index] = FFloat16Color(Value);
					continue;
				}
				static_cast<FColor*>(MipData)[Index] = FColor(
					static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(Value.R * 255.0f), 0, 255)),
					static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(Value.G * 255.0f), 0, 255)),
					static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(Value.B * 255.0f), 0, 255)),
					static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(Value.A * 255.0f), 0, 255)));
			}
		}
		Mip.BulkData.Unlock();
		Texture->UpdateResource();
		return Texture;
	}

	UTextureRenderTarget2D* CreateFieldRhiTarget(int32 Resolution)
	{
		UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(GetTransientPackage(), NAME_None, RF_Transient);
		Target->RenderTargetFormat = RTF_RGBA16f;
		Target->ClearColor = FLinearColor::Transparent;
		Target->bAutoGenerateMips = false;
		Target->bSupportsUAV = true;
		Target->bCanCreateUAV = true;
		Target->InitCustomFormat(Resolution, Resolution, PF_FloatRGBA, true);
		Target->UpdateResourceImmediate(true);
		return Target;
	}

	FTDFieldRhiTargets CreateFieldRhiTargets(int32 Resolution)
	{
		FTDFieldRhiTargets Targets;
		Targets.Density = TStrongObjectPtr<UTextureRenderTarget2D>(CreateFieldRhiTarget(Resolution));
		Targets.Velocity = TStrongObjectPtr<UTextureRenderTarget2D>(CreateFieldRhiTarget(Resolution));
		return Targets;
	}

	FTDFluidGpuActorInput MakeFieldActor(uint64 ActorKey, int32 Resolution, const FTDFieldRhiTargets& Targets)
	{
		FTDFluidGpuActorInput Actor;
		Actor.ActorKey = ActorKey;
		Actor.Settings.Resolution = Resolution;
		Actor.Settings.ExtentCm = FVector2D(FieldRhiExtentCm, FieldRhiExtentCm);
		Actor.Settings.PressureIterations = 1;
		Actor.Settings.VorticityStrength = 0.0f;
		Actor.Settings.DensityDissipation = 0.0f;
		Actor.Settings.VelocityDamping = 0.0f;
		Actor.Settings.TerrainSlopeGravity = 0.0f;
		Actor.Settings.BorderFadeRatio = 0.0f;
		Actor.Settings.BrushVelocityResponsePerSec = FieldRhiVelocityResponsePerSec;
		Actor.DeltaTimeSeconds = FieldRhiDeltaTimeSeconds;
		Actor.DensityPressureOutput = Targets.Density->GameThread_GetRenderTargetResource();
		Actor.VelocityOutput = Targets.Velocity->GameThread_GetRenderTargetResource();
		return Actor;
	}

	FTDFluidGpuFieldInput MakeWindowField(UTexture2D* Texture, ETDFluidGpuFieldLayout Layout)
	{
		FTDFluidGpuFieldInput Field;
		Field.Texture = Texture->GetResource();
		Field.HalfSizeCm = FVector2D(0.5 * FieldRhiExtentCm, 0.5 * FieldRhiExtentCm);
		Field.LayoutRequest = static_cast<uint8>(Layout);
		return Field;
	}

	void RunFieldFrames(int32 FrameCount, const TArray<FTDFluidGpuActorInput>& Actors, TArray<double>* OutFrameMilliseconds = nullptr)
	{
		FlushRenderingCommands();
		TSharedRef<TArray<double>, ESPMode::ThreadSafe> FrameMilliseconds = MakeShared<TArray<double>, ESPMode::ThreadSafe>();
		ENQUEUE_RENDER_COMMAND(TDFluidGpuFieldInputRhiFrames)(
			[Actors, FrameCount, FrameMilliseconds](FRHICommandListImmediate& RHICmdList)
			{
				TArray<FTDFluidGpuBatch> Batches;
				for (int32 FrameIndex = 0; FrameIndex < FrameCount; ++FrameIndex)
				{
					const double StartSeconds = FPlatformTime::Seconds();
					FTDFluidGpuFrameInputs FrameInputs;
					FrameInputs.FrameNumber = static_cast<uint32>(FrameIndex + 1);
					FrameInputs.Actors = Actors;
					for (FTDFluidGpuActorInput& Actor : FrameInputs.Actors)
					{
						Actor.SimulationTimeSeconds = FrameIndex * FieldRhiDeltaTimeSeconds;
					}
					FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("TDFluidGpuFieldInputRhiTest"));
					AddTDFluidGpuFramePasses(GraphBuilder, FrameInputs, Batches);
					GraphBuilder.Execute();
					RHICmdList.SubmitAndBlockUntilGPUIdle();
					FrameMilliseconds->Add((FPlatformTime::Seconds() - StartSeconds) * 1000.0);
				}
				Batches.Empty();
			});
		FlushRenderingCommands();
		if (OutFrameMilliseconds)
		{
			*OutFrameMilliseconds = *FrameMilliseconds;
		}
	}

	bool ReadFieldRhiPixels(const FTDFieldRhiTargets& Targets, FTDFieldRhiPixels& OutPixels)
	{
		FRenderTarget* DensityTarget = Targets.Density->GameThread_GetRenderTargetResource();
		FRenderTarget* VelocityTarget = Targets.Velocity->GameThread_GetRenderTargetResource();
		return DensityTarget && VelocityTarget
			&& DensityTarget->ReadFloat16Pixels(OutPixels.Density) && VelocityTarget->ReadFloat16Pixels(OutPixels.Velocity)
			&& OutPixels.Density.Num() == FieldRhiResolution * FieldRhiResolution
			&& OutPixels.Velocity.Num() == FieldRhiResolution * FieldRhiResolution;
	}

	FLinearColor GetCell(const TArray<FFloat16Color>& Pixels, int32 CellX, int32 CellY)
	{
		return Pixels[CellY * FieldRhiResolution + CellX].GetFloats();
	}

	FVector2D DecodeVelocity(const TArray<FFloat16Color>& Pixels, int32 CellX, int32 CellY)
	{
		const FLinearColor Encoded = GetCell(Pixels, CellX, CellY);
		return FVector2D((Encoded.R - 0.5) * 2.0 * FieldRhiOutputSpeedReferenceCmPerSec, (Encoded.G - 0.5) * 2.0 * FieldRhiOutputSpeedReferenceCmPerSec);
	}

	FVector2D GetTexelLocalCm(int32 TexelX, int32 TexelY)
	{
		const double HalfExtent = 0.5 * FieldRhiExtentCm;
		return FVector2D(((TexelX + 0.5) / FieldRhiResolution * 2.0 - 1.0) * HalfExtent, ((TexelY + 0.5) / FieldRhiResolution * 2.0 - 1.0) * HalfExtent);
	}

	bool RunSingleActor(FAutomationTestBase& Test, const FString& Label, int32 FrameCount, TFunctionRef<void(FTDFluidGpuActorInput&)> ConfigureActor, FTDFieldRhiPixels& OutPixels)
	{
		const FTDFieldRhiTargets Targets = CreateFieldRhiTargets(FieldRhiResolution);
		FTDFluidGpuActorInput Actor = MakeFieldActor(0x46494C44ull, FieldRhiResolution, Targets);
		ConfigureActor(Actor);
		RunFieldFrames(FrameCount, { Actor });
		return Test.TestTrue(*(Label + TEXT(" read the GPU output targets")), ReadFieldRhiPixels(Targets, OutPixels));
	}

	uint8 GetGradientByte(int32 TexelX, int32 TexelY)
	{
		return static_cast<uint8>(TexelX * 3 + TexelY);
	}

	UTexture2D* CreateSolidTexture(const FLinearColor& Color)
	{
		return CreateFieldTexture(4, PF_B8G8R8A8, [Color](int32, int32) { return Color; });
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuFieldInputBitmapDensityRhiTest,
	"TDGame.FluidFXRhi.Input.BitmapDensityMatchesTexels",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuFieldInputBitmapDensityRhiTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuFieldInputRhiTestPrivate;
	if (!CanRunFieldRhiTest(*this, TEXT("TDGame.FluidFXRhi.Input.BitmapDensityMatchesTexels")))
	{
		return false;
	}

	TStrongObjectPtr<UTexture2D> Gradient(CreateFieldTexture(FieldRhiResolution, PF_B8G8R8A8, [](int32 TexelX, int32 TexelY)
	{
		return FLinearColor(GetGradientByte(TexelX, TexelY) / 255.0f, 0.0f, 0.0f, 1.0f);
	}));
	FlushRenderingCommands();

	FTDFieldRhiPixels Pixels;
	if (!RunSingleActor(*this, TEXT("Gradient"), 1, [&Gradient](FTDFluidGpuActorInput& Actor)
	{
		Actor.FieldInputs.Add(MakeWindowField(Gradient.Get(), ETDFluidGpuFieldLayout::Density));
	}, Pixels))
	{
		return false;
	}

	double MaximumError = 0.0;
	for (int32 CellY = 0; CellY < FieldRhiResolution; ++CellY)
	{
		for (int32 CellX = 0; CellX < FieldRhiResolution; ++CellX)
		{
			MaximumError = FMath::Max(MaximumError, FMath::Abs(GetCell(Pixels.Density, CellX, CellY).R - GetGradientByte(CellX, CellY) / 255.0));
		}
	}
	AddInfo(FString::Printf(TEXT("TDFluidFieldBitmapDensity: max_error=%.6f"), MaximumError));
	TestTrue(*FString::Printf(TEXT("Every cell density matches its texel within 1/255 (max %.6f)"), MaximumError), MaximumError <= 1.0 / 255.0 + FieldRhiHalfFloatTolerance);
	TestTrue(TEXT("Row 0 maps to the -Y edge and the last column to +X"), GetCell(Pixels.Density, FieldRhiResolution - 1, 0).R > GetCell(Pixels.Density, 0, FieldRhiResolution - 1).R);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuFieldInputBitmapVelocityRhiTest,
	"TDGame.FluidFXRhi.Input.BitmapVelocityFollowsEncoding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuFieldInputBitmapVelocityRhiTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuFieldInputRhiTestPrivate;
	if (!CanRunFieldRhiTest(*this, TEXT("TDGame.FluidFXRhi.Input.BitmapVelocityFollowsEncoding")))
	{
		return false;
	}

	constexpr float RangeCmPerSec = 1000.0f;
	TStrongObjectPtr<UTexture2D> Encoded(CreateSolidTexture(FLinearColor(1.0f, 0.5f, 0.0f, 1.0f)));
	FlushRenderingCommands();

	FTDFieldRhiPixels SignedPixels;
	FTDFieldRhiPixels UnsignedPixels;
	const auto ConfigureVelocity = [&Encoded, RangeCmPerSec](FTDFluidGpuActorInput& Actor, bool bSigned)
	{
		FTDFluidGpuFieldInput Field = MakeWindowField(Encoded.Get(), ETDFluidGpuFieldLayout::Velocity);
		Field.bSignedVelocity = bSigned;
		Field.VelocityRangeCmPerSec = RangeCmPerSec;
		Actor.FieldInputs.Add(Field);
	};
	if (!RunSingleActor(*this, TEXT("Signed"), 1, [&ConfigureVelocity](FTDFluidGpuActorInput& Actor) { ConfigureVelocity(Actor, true); }, SignedPixels)
		|| !RunSingleActor(*this, TEXT("Unsigned"), 1, [&ConfigureVelocity](FTDFluidGpuActorInput& Actor) { ConfigureVelocity(Actor, false); }, UnsignedPixels))
	{
		return false;
	}

	const int32 Center = FieldRhiResolution / 2;
	const FVector2D SignedVelocity = DecodeVelocity(SignedPixels.Velocity, Center, Center);
	const FVector2D UnsignedVelocity = DecodeVelocity(UnsignedPixels.Velocity, Center, Center);
	const double ExpectedSpeed = RangeCmPerSec * FMath::Min(1.0f, FieldRhiVelocityResponsePerSec * FieldRhiDeltaTimeSeconds);
	AddInfo(FString::Printf(TEXT("TDFluidFieldBitmapVelocity: signed=(%.3f, %.3f) unsigned=(%.3f, %.3f) expected=%.3f"), SignedVelocity.X, SignedVelocity.Y, UnsignedVelocity.X, UnsignedVelocity.Y, ExpectedSpeed));
	TestTrue(TEXT("Signed (1, 0.5) points along +X"), SignedVelocity.X > 0.5 * ExpectedSpeed && FMath::Abs(SignedVelocity.Y) <= 0.1 * SignedVelocity.X);
	TestTrue(TEXT("Signed speed stays at range times scale times blend"), SignedVelocity.X <= ExpectedSpeed * 1.05 + 1.0);
	TestTrue(TEXT("Unsigned encoding of the same texel turns toward +Y"), UnsignedVelocity.Y > 0.3 * UnsignedVelocity.X && UnsignedVelocity.X > 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuFieldInputCollisionMaskRhiTest,
	"TDGame.FluidFXRhi.Input.CollisionMaskMakesSolidCells",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuFieldInputCollisionMaskRhiTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuFieldInputRhiTestPrivate;
	if (!CanRunFieldRhiTest(*this, TEXT("TDGame.FluidFXRhi.Input.CollisionMaskMakesSolidCells")))
	{
		return false;
	}

	TStrongObjectPtr<UTexture2D> White(CreateSolidTexture(FLinearColor::White));
	TStrongObjectPtr<UTexture2D> LeftMask(CreateFieldTexture(FieldRhiResolution, PF_B8G8R8A8, [](int32 TexelX, int32)
	{
		return TexelX < FieldRhiResolution / 2 ? FLinearColor::White : FLinearColor::Black;
	}));
	FlushRenderingCommands();

	const auto ConfigureMask = [&White, &LeftMask](FTDFluidGpuActorInput& Actor, bool bWritesCollision)
	{
		Actor.bHasObstacleListChanged = true;
		Actor.FieldInputs.Add(MakeWindowField(White.Get(), ETDFluidGpuFieldLayout::Density));
		FTDFluidGpuFieldInput Mask = MakeWindowField(LeftMask.Get(), ETDFluidGpuFieldLayout::CollisionMask);
		Mask.bWritesCollision = bWritesCollision;
		Mask.CollisionEdgeCm = 50.0f;
		Actor.FieldInputs.Add(Mask);
	};
	FTDFieldRhiPixels SolidPixels;
	FTDFieldRhiPixels OpenPixels;
	if (!RunSingleActor(*this, TEXT("Collision"), 2, [&ConfigureMask](FTDFluidGpuActorInput& Actor) { ConfigureMask(Actor, true); }, SolidPixels)
		|| !RunSingleActor(*this, TEXT("No collision"), 2, [&ConfigureMask](FTDFluidGpuActorInput& Actor) { ConfigureMask(Actor, false); }, OpenPixels))
	{
		return false;
	}

	double MaximumSolidDensity = 0.0;
	double MaximumSolidAlpha = 0.0;
	double MinimumOpenDensity = 1.0;
	for (int32 CellY = 2; CellY < FieldRhiResolution - 2; ++CellY)
	{
		for (int32 CellX = 2; CellX < FieldRhiResolution / 2 - 1; ++CellX)
		{
			MaximumSolidDensity = FMath::Max<double>(MaximumSolidDensity, GetCell(SolidPixels.Density, CellX, CellY).R);
			MaximumSolidAlpha = FMath::Max<double>(MaximumSolidAlpha, GetCell(SolidPixels.Density, CellX, CellY).A);
		}
		for (int32 CellX = FieldRhiResolution / 2 + 1; CellX < FieldRhiResolution - 2; ++CellX)
		{
			MinimumOpenDensity = FMath::Min<double>(MinimumOpenDensity, GetCell(SolidPixels.Density, CellX, CellY).R);
		}
	}
	const int32 Row = FieldRhiResolution / 2;
	AddInfo(FString::Printf(TEXT("TDFluidFieldCollisionMask: solid_density=%.5f solid_alpha=%.5f open_density=%.5f"), MaximumSolidDensity, MaximumSolidAlpha, MinimumOpenDensity));
	TestTrue(TEXT("Mask cells above 0.5 hold no density"), MaximumSolidDensity <= FieldRhiHalfFloatTolerance);
	TestTrue(TEXT("Mask cells above 0.5 are solid in the output alpha"), MaximumSolidAlpha <= FieldRhiHalfFloatTolerance);
	TestTrue(TEXT("Mask cells below 0.5 keep the density field"), MinimumOpenDensity >= 0.99);
	TestTrue(TEXT("Mask cells above 0.5 keep zero velocity"), DecodeVelocity(SolidPixels.Velocity, FieldRhiResolution / 4, Row).Size() <= 1.0);
	TestTrue(TEXT("Without the collision flag the same cells keep density"), GetCell(OpenPixels.Density, FieldRhiResolution / 4, Row).R >= 0.99);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuFieldInputMeshDistanceRhiTest,
	"TDGame.FluidFXRhi.Input.MeshDistanceContourBand",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuFieldInputMeshDistanceRhiTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuFieldInputRhiTestPrivate;
	if (!CanRunFieldRhiTest(*this, TEXT("TDGame.FluidFXRhi.Input.MeshDistanceContourBand")))
	{
		return false;
	}

	constexpr double CircleRadiusCm = 600.0;
	TStrongObjectPtr<UTexture2D> Circle(CreateFieldTexture(FieldRhiResolution, PF_FloatRGBA, [CircleRadiusCm](int32 TexelX, int32 TexelY)
	{
		const FVector2D LocalCm = GetTexelLocalCm(TexelX, TexelY);
		const double DistanceCm = LocalCm.Size() - CircleRadiusCm;
		const FVector2D Normal = DistanceCm <= 0.0 ? FVector2D::ZeroVector : LocalCm.GetSafeNormal();
		return FLinearColor(static_cast<float>(DistanceCm), static_cast<float>(Normal.X), static_cast<float>(Normal.Y), 1.0f);
	}));
	FlushRenderingCommands();

	const auto ConfigureMesh = [&Circle](FTDFluidGpuActorInput& Actor, bool bWritesCollision)
	{
		Actor.bHasObstacleListChanged = true;
		FTDFluidGpuFieldInput Field = MakeWindowField(Circle.Get(), ETDFluidGpuFieldLayout::MeshDistance);
		Field.ContourBandCm = 200.0f;
		Field.ContourDensityStrength = 1.0f;
		Field.OutwardVelocityCmPerSec = 500.0f;
		Field.bFillsInterior = true;
		Field.bWritesCollision = bWritesCollision;
		Actor.FieldInputs.Add(Field);
	};
	FTDFieldRhiPixels OpenPixels;
	FTDFieldRhiPixels SolidPixels;
	if (!RunSingleActor(*this, TEXT("Contour"), 1, [&ConfigureMesh](FTDFluidGpuActorInput& Actor) { ConfigureMesh(Actor, false); }, OpenPixels)
		|| !RunSingleActor(*this, TEXT("Contour collision"), 2, [&ConfigureMesh](FTDFluidGpuActorInput& Actor) { ConfigureMesh(Actor, true); }, SolidPixels))
	{
		return false;
	}

	bool bAllFinite = true;
	for (int32 PixelIndex = 0; PixelIndex < OpenPixels.Density.Num(); ++PixelIndex)
	{
		const FLinearColor Density = OpenPixels.Density[PixelIndex].GetFloats();
		const FLinearColor Velocity = OpenPixels.Velocity[PixelIndex].GetFloats();
		bAllFinite &= FMath::IsFinite(Density.R) && FMath::IsFinite(Velocity.R) && FMath::IsFinite(Velocity.G);
	}

	const int32 Center = FieldRhiResolution / 2;
	const int32 BandCell = 45;
	const int32 FarCell = 60;
	const FVector2D BandVelocity = DecodeVelocity(OpenPixels.Velocity, BandCell, Center);
	const FVector2D BandVelocityMinusY = DecodeVelocity(OpenPixels.Velocity, Center, FieldRhiResolution - 1 - BandCell);
	AddInfo(FString::Printf(TEXT("TDFluidFieldMeshDistance: band=%.4f far=%.4f inside=%.4f band_velocity=(%.3f, %.3f) solid_alpha=%.4f solid_band=%.4f"),
		GetCell(OpenPixels.Density, BandCell, Center).R, GetCell(OpenPixels.Density, FarCell, Center).R, GetCell(OpenPixels.Density, Center, Center).R,
		BandVelocity.X, BandVelocity.Y, GetCell(SolidPixels.Density, Center, Center).A, GetCell(SolidPixels.Density, BandCell, Center).R));
	TestTrue(TEXT("Zero interior normals never produce NaN"), bAllFinite);
	TestTrue(TEXT("Cells inside the contour band gain density"), GetCell(OpenPixels.Density, BandCell, Center).R > 0.3f);
	TestTrue(TEXT("Cells beyond the contour band stay empty"), GetCell(OpenPixels.Density, FarCell, Center).R <= FieldRhiHalfFloatTolerance);
	TestTrue(TEXT("Filled interior gains full density"), GetCell(OpenPixels.Density, Center, Center).R >= 0.99f);
	TestTrue(TEXT("Band velocity follows the +X outward normal"), BandVelocity.X > 5.0 && FMath::Abs(BandVelocity.Y) < 0.5 * BandVelocity.X);
	TestTrue(TEXT("Band velocity follows the -Y outward normal"), BandVelocityMinusY.Y < -5.0);
	TestTrue(TEXT("Collision turns the interior solid"), GetCell(SolidPixels.Density, Center, Center).A <= FieldRhiHalfFloatTolerance && GetCell(SolidPixels.Density, Center, Center).R <= FieldRhiHalfFloatTolerance);
	TestTrue(TEXT("Collision keeps the outer band fluid"), GetCell(SolidPixels.Density, BandCell, Center).R > 0.3f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuFieldInputSlotOverflowRhiTest,
	"TDGame.FluidFXRhi.Input.FieldTextureSlotOverflowCounts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuFieldInputSlotOverflowRhiTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuFieldInputRhiTestPrivate;
	if (!CanRunFieldRhiTest(*this, TEXT("TDGame.FluidFXRhi.Input.FieldTextureSlotOverflowCounts")))
	{
		return false;
	}

	TArray<TStrongObjectPtr<UTexture2D>> Textures;
	for (int32 TextureIndex = 0; TextureIndex < 5; ++TextureIndex)
	{
		Textures.Emplace(CreateSolidTexture(FLinearColor(0.2f + 0.15f * TextureIndex, 0.0f, 0.0f, 1.0f)));
	}
	FlushRenderingCommands();

	const auto MakeQuadrantField = [&Textures](int32 TextureIndex, const FVector2D& CenterCm)
	{
		FTDFluidGpuFieldInput Field = MakeWindowField(Textures[TextureIndex].Get(), ETDFluidGpuFieldLayout::Density);
		Field.CenterPlaneCm = CenterCm;
		Field.HalfSizeCm = FVector2D(300.0, 300.0);
		return Field;
	};
	const TArray<FVector2D> Centers = { FVector2D(-800.0, -800.0), FVector2D(800.0, -800.0), FVector2D(-800.0, 800.0), FVector2D(800.0, 800.0) };
	const FTDFieldRhiTargets FirstTargets = CreateFieldRhiTargets(FieldRhiResolution);
	const FTDFieldRhiTargets SecondTargets = CreateFieldRhiTargets(FieldRhiResolution);
	FTDFluidGpuActorInput First = MakeFieldActor(0x464C4431ull, FieldRhiResolution, FirstTargets);
	FTDFluidGpuActorInput Second = MakeFieldActor(0x464C4432ull, FieldRhiResolution, SecondTargets);
	First.FieldInputs = { MakeQuadrantField(0, Centers[0]), MakeQuadrantField(1, Centers[1]), MakeQuadrantField(2, Centers[2]) };
	Second.FieldInputs = { MakeQuadrantField(3, Centers[0]), MakeQuadrantField(4, Centers[1]) };

	const FTDFluidGpuFieldInputBatchPlan Plan = TDFluidGpuFieldInput::BuildBatchPlan({ &First, &Second });
	TestEqual(TEXT("Four distinct textures fill the batch slots"), Plan.Textures.Num(), TDFluidGpuSolverConstants::MaximumFieldTexturesPerBatch);
	TestEqual(TEXT("Four fields are packed"), Plan.Fields.Num(), 4);
	TestEqual(TEXT("The fifth texture is dropped once"), Plan.DroppedCount, 1);
	TestTrue(TEXT("Second entry keeps only its first field"), Plan.EntryRanges.Num() == 2 && Plan.EntryRanges[1].X == 3u && Plan.EntryRanges[1].Y == 1u);

	RunFieldFrames(1, { First, Second });
	FTDFieldRhiPixels FirstPixels;
	FTDFieldRhiPixels SecondPixels;
	if (!TestTrue(TEXT("Read both actors"), ReadFieldRhiPixels(FirstTargets, FirstPixels) && ReadFieldRhiPixels(SecondTargets, SecondPixels)))
	{
		return false;
	}

	const auto CellAt = [](const FVector2D& CenterCm)
	{
		return FIntPoint(FMath::FloorToInt((CenterCm.X + 0.5 * FieldRhiExtentCm) / (FieldRhiExtentCm / FieldRhiResolution)), FMath::FloorToInt((CenterCm.Y + 0.5 * FieldRhiExtentCm) / (FieldRhiExtentCm / FieldRhiResolution)));
	};
	int32 AppliedCount = 0;
	for (int32 FieldIndex = 0; FieldIndex < 3; ++FieldIndex)
	{
		AppliedCount += GetCell(FirstPixels.Density, CellAt(Centers[FieldIndex]).X, CellAt(Centers[FieldIndex]).Y).R > 0.1f ? 1 : 0;
	}
	const FIntPoint SecondApplied = CellAt(Centers[0]);
	const FIntPoint SecondDropped = CellAt(Centers[1]);
	AppliedCount += GetCell(SecondPixels.Density, SecondApplied.X, SecondApplied.Y).R > 0.1f ? 1 : 0;
	AddInfo(FString::Printf(TEXT("TDFluidFieldSlotOverflow: applied=%d dropped=%d dropped_density=%.4f"), AppliedCount, Plan.DroppedCount, GetCell(SecondPixels.Density, SecondDropped.X, SecondDropped.Y).R));
	TestEqual(TEXT("Four fields reach the GPU"), AppliedCount, 4);
	TestTrue(TEXT("The dropped field writes nothing"), GetCell(SecondPixels.Density, SecondDropped.X, SecondDropped.Y).R <= FieldRhiHalfFloatTolerance);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidGpuFieldInputCostRhiTest,
	"TDGame.FluidFXRhi.Input.FieldInputCost",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidGpuFieldInputCostRhiTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidGpuFieldInputRhiTestPrivate;
	if (!CanRunFieldRhiTest(*this, TEXT("TDGame.FluidFXRhi.Input.FieldInputCost")))
	{
		return false;
	}

	constexpr int32 CostResolution = 256;
	constexpr int32 CostFrameCount = 40;
	constexpr int32 CostWarmupFrameCount = 5;
	TStrongObjectPtr<UTexture2D> Texture(CreateFieldTexture(FieldRhiResolution, PF_B8G8R8A8, [](int32 TexelX, int32 TexelY)
	{
		return FLinearColor(GetGradientByte(TexelX, TexelY) / 255.0f, 0.75f, 0.25f, 1.0f);
	}));
	FlushRenderingCommands();

	const auto MeasureAverage = [&Texture, CostResolution, CostFrameCount, CostWarmupFrameCount](int32 FieldCount, bool bWritesCollision)
	{
		const FTDFieldRhiTargets Targets = CreateFieldRhiTargets(CostResolution);
		FTDFluidGpuActorInput Actor = MakeFieldActor(0x434F5354ull, CostResolution, Targets);
		Actor.bHasObstacleListChanged = bWritesCollision;
		for (int32 FieldIndex = 0; FieldIndex < FieldCount; ++FieldIndex)
		{
			FTDFluidGpuFieldInput Field = MakeWindowField(Texture.Get(), static_cast<ETDFluidGpuFieldLayout>(FieldIndex % 3));
			Field.VelocityRangeCmPerSec = 200.0f;
			Field.bWritesCollision = bWritesCollision;
			Field.LayoutRequest = bWritesCollision ? static_cast<uint8>(ETDFluidGpuFieldLayout::CollisionMask) : Field.LayoutRequest;
			Actor.FieldInputs.Add(Field);
		}
		TArray<double> FrameMilliseconds;
		RunFieldFrames(CostFrameCount, { Actor }, &FrameMilliseconds);
		double Total = 0.0;
		for (int32 FrameIndex = CostWarmupFrameCount; FrameIndex < FrameMilliseconds.Num(); ++FrameIndex)
		{
			Total += FrameMilliseconds[FrameIndex];
		}
		return Total / FMath::Max(1, FrameMilliseconds.Num() - CostWarmupFrameCount);
	};

	const double BaselineMilliseconds = MeasureAverage(0, false);
	const double FieldMilliseconds = MeasureAverage(TDFluidGpuSolverConstants::MaximumFieldInputsPerActor, false);
	const double CollisionMilliseconds = MeasureAverage(1, true);

	TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
	Report->SetStringField(TEXT("test"), TEXT("TDGame.FluidFXRhi.Input.FieldInputCost"));
	Report->SetStringField(TEXT("method"), TEXT("frame wall time with GPU idle wait, 256 squared, warm-up frames excluded"));
	Report->SetNumberField(TEXT("resolution"), CostResolution);
	Report->SetNumberField(TEXT("frames"), CostFrameCount - CostWarmupFrameCount);
	Report->SetNumberField(TEXT("baseline_ms"), BaselineMilliseconds);
	Report->SetNumberField(TEXT("four_fields_ms"), FieldMilliseconds);
	Report->SetNumberField(TEXT("collision_rerasterize_ms"), CollisionMilliseconds);
	Report->SetNumberField(TEXT("four_fields_delta_ms"), FieldMilliseconds - BaselineMilliseconds);
	Report->SetNumberField(TEXT("target_delta_ms"), 0.05);
	FString Json;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
	FJsonSerializer::Serialize(Report, Writer);
	const FString ReportPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("FluidFX"), TEXT("fx15b-field-input-cost.json"));
	TestTrue(TEXT("Cost report saved"), FFileHelper::SaveStringToFile(Json, *ReportPath));
	AddInfo(FString::Printf(TEXT("TDFluidFieldInputCost: baseline=%.4f four_fields=%.4f collision=%.4f ms"), BaselineMilliseconds, FieldMilliseconds, CollisionMilliseconds));
	return true;
}

#endif

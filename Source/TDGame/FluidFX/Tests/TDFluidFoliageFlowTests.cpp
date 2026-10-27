#if WITH_DEV_AUTOMATION_TESTS

#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "FluidFX/TDFluidFoliageFlowActor.h"
#include "HAL/PlatformTime.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Math/Float16Color.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RenderingThread.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "TextureResource.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

namespace TDFluidFoliageFlowTestPrivate
{
	const TCHAR* FlowCopyMaterialPath = TEXT("/Game/FluidFX/Materials/M_TD_FluidFoliageFlowCopy.M_TD_FluidFoliageFlowCopy");
	constexpr int32 VelocityTextureSize = 64;
	constexpr float OutputSpeedReferenceCmPerSec = 600.0f;
	constexpr float BendMaxDegrees = 70.0f;
	constexpr float BendSpeedReferenceCmPerSec = 300.0f;
	constexpr float BendResponse = 1.0f;
	constexpr float CopyTolerance = 0.01f;
	constexpr int32 CostFrameCount = 120;

	bool CanRunFoliageRhiTest(FAutomationTestBase& Test, const FString& CaseLabel)
	{
		if (!GUsingNullRHI && FApp::CanEverRender())
		{
			return true;
		}
		Test.AddError(FString::Printf(TEXT("%s needs a rendering RHI but none is active, so it fails instead of passing as skipped. Run with -RenderOffScreen (build_and_test.py --rhi offscreen)."), *CaseLabel));
		return false;
	}

	struct FFoliageTestWorld
	{
		FFoliageTestWorld()
		{
			const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDFluidFoliageFlowWorld"));
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
		}

		~FFoliageTestWorld()
		{
			if (!World)
			{
				return;
			}
			World->EndPlay(EEndPlayReason::Quit);
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}

		UWorld* World = nullptr;
	};

	FLinearColor MakePackedVelocity(int32 PixelX, int32 PixelY)
	{
		return FLinearColor((PixelX + 0.5f) / VelocityTextureSize, (PixelY + 0.5f) / VelocityTextureSize, 0.0f, 1.0f);
	}

	FLinearColor ComputeExpectedFlow(const FLinearColor& Packed)
	{
		const FVector2f Velocity((2.0f * Packed.R - 1.0f) * OutputSpeedReferenceCmPerSec, (2.0f * Packed.G - 1.0f) * OutputSpeedReferenceCmPerSec);
		const float Speed = Velocity.Size();
		const float Magnitude = FMath::Pow(FMath::Clamp(Speed / BendSpeedReferenceCmPerSec, 0.0f, 1.0f), BendResponse);
		const float Degrees = FMath::Clamp(BendMaxDegrees, 0.0f, 85.0f) * Magnitude;
		const FVector2f Direction = Velocity / FMath::Max(Speed, 0.001f);
		return FLinearColor(0.5f + 0.5f * Direction.X * Degrees / 90.0f, 0.5f + 0.5f * Direction.Y * Degrees / 90.0f, Degrees / 90.0f, 1.0f);
	}

	UTexture2D* CreateVelocityTexture()
	{
		UTexture2D* Texture = UTexture2D::CreateTransient(VelocityTextureSize, VelocityTextureSize, PF_FloatRGBA);
		if (!Texture || !Texture->GetPlatformData() || Texture->GetPlatformData()->Mips.Num() == 0)
		{
			return nullptr;
		}

		Texture->SRGB = false;
		Texture->Filter = TF_Nearest;
		FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
		FFloat16Color* Pixels = static_cast<FFloat16Color*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
		for (int32 PixelY = 0; PixelY < VelocityTextureSize; ++PixelY)
		{
			for (int32 PixelX = 0; PixelX < VelocityTextureSize; ++PixelX)
			{
				Pixels[PixelY * VelocityTextureSize + PixelX] = FFloat16Color(MakePackedVelocity(PixelX, PixelY));
			}
		}
		Mip.BulkData.Unlock();
		Texture->UpdateResource();
		return Texture;
	}

	UTextureRenderTarget2D* CreateFlowTarget()
	{
		UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(GetTransientPackage(), NAME_None, RF_Transient);
		Target->RenderTargetFormat = RTF_RGBA16f;
		Target->ClearColor = FLinearColor(0.5f, 0.5f, 0.0f, 1.0f);
		Target->bAutoGenerateMips = false;
		Target->AddressX = TA_Clamp;
		Target->AddressY = TA_Clamp;
		Target->InitCustomFormat(VelocityTextureSize, VelocityTextureSize, PF_FloatRGBA, true);
		Target->UpdateResourceImmediate(true);
		return Target;
	}

	UMaterialInstanceDynamic* CreateCopyInstance(UMaterialInterface& CopyMaterial, UTexture2D& VelocityTexture)
	{
		UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(&CopyMaterial, GetTransientPackage());
		Instance->SetTextureParameterValue(TEXT("VelocityTexture"), &VelocityTexture);
		Instance->SetVectorParameterValue(TEXT("PlaneAxisU"), FLinearColor(1.0f, 0.0f, 0.0f, 0.0f));
		Instance->SetVectorParameterValue(TEXT("PlaneAxisV"), FLinearColor(0.0f, 1.0f, 0.0f, 0.0f));
		Instance->SetScalarParameterValue(TEXT("VelocitySpeedReferenceCmPerSec"), OutputSpeedReferenceCmPerSec);
		Instance->SetScalarParameterValue(TEXT("FoliageBendMaxDegrees"), BendMaxDegrees);
		Instance->SetScalarParameterValue(TEXT("FoliageBendSpeedReferenceCmPerSec"), BendSpeedReferenceCmPerSec);
		Instance->SetScalarParameterValue(TEXT("FoliageBendResponse"), BendResponse);
		return Instance;
	}

	double ReadPercentile(TArray<double> Values, double Fraction)
	{
		if (Values.IsEmpty())
		{
			return 0.0;
		}
		Values.Sort();
		const int32 Index = FMath::Clamp(FMath::FloorToInt32(Fraction * (Values.Num() - 1) + 0.5), 0, Values.Num() - 1);
		return Values[Index];
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidFoliageFlowWindowVectorsTest,
	"TDGame.FluidFX.FoliageFlow.WindowVectors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidFoliageFlowWindowVectorsTest::RunTest(const FString& Parameters)
{
	const FTDFluidFoliageWindowVectors Active = ATDFluidFoliageFlowActor::MakeWindowVectors(FVector2D(100.0, 200.0), FVector2D(700.0, 700.0), 0.1f, true, 600.0f);
	TestTrue(TEXT("active center"), Active.Center.Equals(FLinearColor(100.0f, 200.0f, 0.0f, 1.0f)));
	TestTrue(TEXT("active extents"), Active.Extents.Equals(FLinearColor(700.0f, 700.0f, 0.1f, 0.0f)));
	TestTrue(TEXT("active state"), Active.State.Equals(FLinearColor(1.0f, 600.0f, 0.0f, 0.0f)));

	const FTDFluidFoliageWindowVectors Inactive = ATDFluidFoliageFlowActor::MakeWindowVectors(FVector2D(100.0, 200.0), FVector2D(700.0, 700.0), 0.1f, false, 600.0f);
	TestEqual(TEXT("inactive state x"), Inactive.State.R, 0.0f);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidFoliageFlowSourceSelectionTest,
	"TDGame.FluidFX.FoliageFlow.SourceSelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidFoliageFlowSourceSelectionTest::RunTest(const FString& Parameters)
{
	auto MakeCandidate = [](const TCHAR* Name, bool bIsAwake, bool bIsInitialized)
	{
		FTDFluidFoliageSourceCandidate Candidate;
		Candidate.Name = FName(Name);
		Candidate.bIsAwake = bIsAwake;
		Candidate.bIsInitialized = bIsInitialized;
		return Candidate;
	};

	TArray<FTDFluidFoliageSourceCandidate> Candidates;
	Candidates.Add(MakeCandidate(TEXT("B"), true, true));
	Candidates.Add(MakeCandidate(TEXT("A"), false, true));
	Candidates.Add(MakeCandidate(TEXT("C"), true, false));
	TestEqual(TEXT("first awake initialized candidate by name"), ATDFluidFoliageFlowActor::SelectSourceIndex(Candidates), 0);

	Candidates.Add(MakeCandidate(TEXT("AA"), true, true));
	TestEqual(TEXT("lexically smaller ready candidate wins"), ATDFluidFoliageFlowActor::SelectSourceIndex(Candidates), 3);

	for (FTDFluidFoliageSourceCandidate& Candidate : Candidates)
	{
		Candidate.bIsAwake = false;
	}
	TestEqual(TEXT("all asleep"), ATDFluidFoliageFlowActor::SelectSourceIndex(Candidates), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("empty"), ATDFluidFoliageFlowActor::SelectSourceIndex(TArray<FTDFluidFoliageSourceCandidate>()), static_cast<int32>(INDEX_NONE));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidFoliageFlowCvarOverrideTest,
	"TDGame.FluidFX.FoliageFlow.CvarOverride",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidFoliageFlowCvarOverrideTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("-1 follows actor on"), ATDFluidFoliageFlowActor::ResolveFlowEnabled(-1, true));
	TestFalse(TEXT("-1 follows actor off"), ATDFluidFoliageFlowActor::ResolveFlowEnabled(-1, false));
	TestFalse(TEXT("0 forces off"), ATDFluidFoliageFlowActor::ResolveFlowEnabled(0, true));
	TestTrue(TEXT("1 forces on"), ATDFluidFoliageFlowActor::ResolveFlowEnabled(1, false));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidFoliageFlowRhiCopyMatchesVelocityTest,
	"TDGame.FluidFXRhi.FoliageFlow.CopyMatchesVelocity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidFoliageFlowRhiCopyMatchesVelocityTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidFoliageFlowTestPrivate;

	const FString CaseLabel = TEXT("TDGame.FluidFXRhi.FoliageFlow.CopyMatchesVelocity");
	if (!CanRunFoliageRhiTest(*this, CaseLabel))
	{
		return false;
	}

	UMaterialInterface* CopyMaterial = LoadObject<UMaterialInterface>(nullptr, FlowCopyMaterialPath);
	if (!TestNotNull(TEXT("flow copy material asset (editor_make_fluid_foliage_material.py)"), CopyMaterial))
	{
		return false;
	}

	FFoliageTestWorld TestWorld;
	TStrongObjectPtr<UTexture2D> VelocityTexture(CreateVelocityTexture());
	TStrongObjectPtr<UTextureRenderTarget2D> FlowTarget(CreateFlowTarget());
	FlushRenderingCommands();
	if (!TestTrue(TEXT("created velocity texture and flow target"), VelocityTexture.IsValid() && FlowTarget.IsValid()))
	{
		return false;
	}

	TStrongObjectPtr<UMaterialInstanceDynamic> CopyInstance(CreateCopyInstance(*CopyMaterial, *VelocityTexture.Get()));
	UKismetRenderingLibrary::DrawMaterialToRenderTarget(TestWorld.World, FlowTarget.Get(), CopyInstance.Get());
	FlushRenderingCommands();

	TArray<FLinearColor> Samples;
	if (!TestTrue(TEXT("read flow target"), UKismetRenderingLibrary::ReadRenderTargetRaw(TestWorld.World, FlowTarget.Get(), Samples, false)) ||
		!TestEqual(TEXT("flow sample count"), Samples.Num(), VelocityTextureSize * VelocityTextureSize))
	{
		return false;
	}

	float MaximumError = 0.0f;
	for (int32 PixelY = 0; PixelY < VelocityTextureSize; ++PixelY)
	{
		for (int32 PixelX = 0; PixelX < VelocityTextureSize; ++PixelX)
		{
			const FLinearColor Expected = ComputeExpectedFlow(MakePackedVelocity(PixelX, PixelY));
			const FLinearColor& Actual = Samples[PixelY * VelocityTextureSize + PixelX];
			MaximumError = FMath::Max3(MaximumError, FMath::Abs(Actual.R - Expected.R), FMath::Max(FMath::Abs(Actual.G - Expected.G), FMath::Abs(Actual.B - Expected.B)));
		}
	}
	AddInfo(FString::Printf(TEXT("%s: maximum decoded flow error %.5f"), *CaseLabel, MaximumError));
	TestTrue(TEXT("copy output matches the velocity mirror"), MaximumError <= CopyTolerance);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidFoliageFlowRhiOffscreenCostTest,
	"TDGame.FluidFXRhi.FoliageFlow.OffscreenCost",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FTDFluidFoliageFlowRhiOffscreenCostTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidFoliageFlowTestPrivate;

	const FString CaseLabel = TEXT("TDGame.FluidFXRhi.FoliageFlow.OffscreenCost");
	if (!CanRunFoliageRhiTest(*this, CaseLabel))
	{
		return false;
	}

	UMaterialInterface* CopyMaterial = LoadObject<UMaterialInterface>(nullptr, FlowCopyMaterialPath);
	if (!TestNotNull(TEXT("flow copy material asset (editor_make_fluid_foliage_material.py)"), CopyMaterial))
	{
		return false;
	}

	FFoliageTestWorld TestWorld;
	TStrongObjectPtr<UTexture2D> VelocityTexture(CreateVelocityTexture());
	TStrongObjectPtr<UTextureRenderTarget2D> FlowTarget(CreateFlowTarget());
	FlushRenderingCommands();
	if (!TestTrue(TEXT("created velocity texture and flow target"), VelocityTexture.IsValid() && FlowTarget.IsValid()))
	{
		return false;
	}

	TStrongObjectPtr<UMaterialInstanceDynamic> CopyInstance(CreateCopyInstance(*CopyMaterial, *VelocityTexture.Get()));
	TArray<double> FrameMilliseconds;
	for (int32 FrameIndex = 0; FrameIndex < CostFrameCount; ++FrameIndex)
	{
		const double StartSeconds = FPlatformTime::Seconds();
		UKismetRenderingLibrary::DrawMaterialToRenderTarget(TestWorld.World, FlowTarget.Get(), CopyInstance.Get());
		FlushRenderingCommands();
		FrameMilliseconds.Add((FPlatformTime::Seconds() - StartSeconds) * 1000.0);
	}

	const double MedianMilliseconds = ReadPercentile(FrameMilliseconds, 0.5);
	const double P95Milliseconds = ReadPercentile(FrameMilliseconds, 0.95);
	TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
	Report->SetStringField(TEXT("case"), CaseLabel);
	Report->SetNumberField(TEXT("frames"), CostFrameCount);
	Report->SetNumberField(TEXT("render_target_size"), VelocityTextureSize);
	Report->SetNumberField(TEXT("median_ms"), MedianMilliseconds);
	Report->SetNumberField(TEXT("p95_ms"), P95Milliseconds);
	Report->SetStringField(TEXT("note"), TEXT("draw + flush wall time per frame, record only"));
	FString ReportText;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ReportText);
	FJsonSerializer::Serialize(Report, Writer);
	const FString CostPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("AgentOps"), FDateTime::Now().ToString(TEXT("%Y%m%d")), TEXT("fx13r"), TEXT("foliage_flow_cost.json"));
	TestTrue(TEXT("wrote foliage flow cost report"), FFileHelper::SaveStringToFile(ReportText, *CostPath));
	AddInfo(FString::Printf(TEXT("%s: median %.4f ms, p95 %.4f ms -> %s"), *CaseLabel, MedianMilliseconds, P95Milliseconds, *CostPath));
	return !HasAnyErrors();
}

#endif

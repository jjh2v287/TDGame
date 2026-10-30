#if WITH_DEV_AUTOMATION_TESTS

#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "FluidFX/TDFluidLiveActor.h"
#include "FluidFX/TDFluidLiveComponent.h"
#include "FluidFX/TDFluidLiveSubsystem.h"
#include "FluidFX/TDFluidPresetAsset.h"
#include "FluidFX/TDFluidSimStateCacheAsset.h"
#include "FluidFX/TDFluidSimStateCacheEditorLibrary.h"
#include "FluidFX/TDFluidSimStateCacheTypes.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"

namespace TDFluidSimStateCacheEditorTestPrivate
{
	const TCHAR* const SimStateCacheEditorTestPackagePath = TEXT("/Game/FluidFX/AutomationTransient");
	constexpr float SimStateCacheEditorStepSeconds = 1.0f / 60.0f;

	TSharedPtr<FJsonObject> ParseResultJson(const FString& Text)
	{
		TSharedPtr<FJsonObject> Result;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		FJsonSerializer::Deserialize(Reader, Result);
		return Result;
	}

	bool IsResultOk(const TSharedPtr<FJsonObject>& Result)
	{
		bool bIsOk = false;
		return Result.IsValid() && Result->TryGetBoolField(TEXT("ok"), bIsOk) && bIsOk;
	}

	struct FTDFluidSimStateCacheEditorTestWorld
	{
		FTDFluidSimStateCacheEditorTestWorld()
		{
			const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDFluidSimStateCacheEditorTestWorld"));
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true);
		}

		~FTDFluidSimStateCacheEditorTestWorld()
		{
			World->EndPlay(EEndPlayReason::Quit);
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}

		void StepWorld()
		{
			++GFrameCounter;
			World->Tick(LEVELTICK_All, SimStateCacheEditorStepSeconds);
		}

		UWorld* World = nullptr;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTDFluidSimStateCacheFileImportTest,
	"TDGameEditor.FluidFX.SimStateCache.FileImport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTDFluidSimStateCacheFileImportTest::RunTest(const FString& Parameters)
{
	using namespace TDFluidSimStateCacheEditorTestPrivate;

	FTDFluidSimStateCacheEditorTestWorld TestWorld;
	UTDFluidLiveSubsystem* FluidSubsystem = TestWorld.World->GetSubsystem<UTDFluidLiveSubsystem>();
	UTDFluidPresetAsset* Preset = UTDFluidPresetAsset::CreateTransientPreset(TestWorld.World, ETDFluidPresetKind::Mud);
	ATDFluidLiveActor* LiveActor = FluidSubsystem ? FluidSubsystem->SpawnPooledFluidActor(Preset, FVector::ZeroVector, 0.0f) : nullptr;
	if (!TestNotNull(TEXT("Mud actor spawns"), LiveActor) || !TestNotNull(TEXT("Mud actor has a live component"), LiveActor->LiveComponent.Get()))
	{
		return false;
	}

	LiveActor->LiveComponent->SetSimulationAwake(true);
	for (int32 FrameIndex = 0; FrameIndex < 4; ++FrameIndex)
	{
		FTDFluidBrushStamp Stamp;
		Stamp.RadiusCm = 150.0f;
		Stamp.Channel = ETDFluidBrushChannel::ChannelR_Mud;
		LiveActor->LiveComponent->QueueBrushStamp(Stamp);
		TestWorld.StepWorld();
	}

	if (!TestFalse(TEXT("Mud runs on the CPU solver"), LiveActor->LiveComponent->IsUsingGpuSolver()))
	{
		return false;
	}

	const FString UniqueSuffix = FGuid::NewGuid().ToString(EGuidFormats::Digits);
	const FString OutputDirectory = FPaths::ProjectSavedDir() / TEXT("AutomationTransient") / TEXT("SimStateCache") / UniqueSuffix;
	const FString AssetName = FString::Printf(TEXT("SC_TD_FileImportTest_%s"), *UniqueSuffix);
	UTDFluidSimStateCacheAsset* CacheAsset = nullptr;
	ON_SCOPE_EXIT
	{
		IFileManager::Get().DeleteDirectory(*OutputDirectory, false, true);
		if (CacheAsset)
		{
			UPackage* CachePackage = CacheAsset->GetPackage();
			CacheAsset->ClearFlags(RF_Public | RF_Standalone);
			CacheAsset->MarkAsGarbage();
			CachePackage->MarkAsGarbage();
		}
	};

	const TSharedPtr<FJsonObject> BadNameResult = ParseResultJson(UTDFluidSimStateCacheEditorLibrary::CaptureSimStateCacheToFile(LiveActor, OutputDirectory, TEXT("bad/name")));
	TestTrue(TEXT("A path-like cache name is refused with a JSON error"), BadNameResult.IsValid() && !IsResultOk(BadNameResult));
	const TSharedPtr<FJsonObject> NullActorResult = ParseResultJson(UTDFluidSimStateCacheEditorLibrary::CaptureSimStateCacheToFile(nullptr, OutputDirectory, TEXT("Missing")));
	TestTrue(TEXT("A missing actor is refused with a JSON error"), NullActorResult.IsValid() && !IsResultOk(NullActorResult));

	const TSharedPtr<FJsonObject> CaptureResult = ParseResultJson(UTDFluidSimStateCacheEditorLibrary::CaptureSimStateCacheToFile(LiveActor, OutputDirectory, TEXT("MudCache")));
	if (!TestTrue(TEXT("Capture to file succeeds"), IsResultOk(CaptureResult)))
	{
		return false;
	}

	FTDFluidSimStateSnapshot DirectSnapshot;
	FString CaptureError;
	if (!TestTrue(TEXT("Direct capture matches the file capture moment"), LiveActor->LiveComponent->CaptureSimStateSnapshot(DirectSnapshot, CaptureError)))
	{
		return false;
	}

	const FString JsonPath = CaptureResult->GetStringField(TEXT("json_path"));
	FString HeaderText;
	TestTrue(TEXT("Header file is readable"), FFileHelper::LoadFileToString(HeaderText, *JsonPath));
	const TSharedPtr<FJsonObject> Header = ParseResultJson(HeaderText);
	const TArray<TSharedPtr<FJsonValue>>* HeaderChannels = nullptr;
	if (!TestTrue(TEXT("Header lists channels"), Header.IsValid() && Header->TryGetArrayField(TEXT("channels"), HeaderChannels) && HeaderChannels))
	{
		return false;
	}

	TestEqual(TEXT("Header backend is CPU"), Header->GetStringField(TEXT("backend")), FString(TEXT("Cpu")));
	TestEqual(TEXT("Header channel count matches the snapshot"), HeaderChannels->Num(), DirectSnapshot.Channels.Num());

	const TSharedPtr<FJsonObject> ImportResult = ParseResultJson(UTDFluidSimStateCacheEditorLibrary::CreateSimStateCacheAsset(JsonPath, SimStateCacheEditorTestPackagePath, AssetName, false));
	if (!TestTrue(TEXT("Import creates the cache asset"), IsResultOk(ImportResult)))
	{
		return false;
	}

	CacheAsset = FindObject<UTDFluidSimStateCacheAsset>(nullptr, *ImportResult->GetStringField(TEXT("object_path")));
	if (!TestNotNull(TEXT("Imported cache asset is in memory"), CacheAsset))
	{
		return false;
	}

	TestTrue(TEXT("Imported cache is usable"), CacheAsset->IsUsable());
	TestTrue(TEXT("Imported cache keeps the preset kind"), CacheAsset->PresetKind == ETDFluidPresetKind::Mud);
	TestEqual(TEXT("Imported source checksum matches the asset checksum"), CacheAsset->SourceChecksum, CacheAsset->ComputeChecksum());
	const TSharedRef<const FTDFluidSimStateSnapshot, ESPMode::ThreadSafe> ImportedSnapshot = CacheAsset->MakeSnapshot();
	TestTrue(*FString::Printf(TEXT("Imported header matches the live snapshot (%s)"), *TDFluidSimStateCache::DescribeHeaderMismatch(*ImportedSnapshot, DirectSnapshot)), TDFluidSimStateCache::DescribeHeaderMismatch(*ImportedSnapshot, DirectSnapshot).IsEmpty());
	TestEqual(TEXT("Imported time matches"), ImportedSnapshot->SimulationTimeSeconds, DirectSnapshot.SimulationTimeSeconds, 1.0e-9);
	if (!TestEqual(TEXT("Imported channel count matches"), ImportedSnapshot->Channels.Num(), DirectSnapshot.Channels.Num()))
	{
		return false;
	}

	for (int32 ChannelIndex = 0; ChannelIndex < DirectSnapshot.Channels.Num(); ++ChannelIndex)
	{
		const FTDFluidStateChannelBytes& Expected = DirectSnapshot.Channels[ChannelIndex];
		const FTDFluidStateChannelBytes& Imported = ImportedSnapshot->Channels[ChannelIndex];
		const uint32 HeaderCrc = static_cast<uint32>((*HeaderChannels)[ChannelIndex]->AsObject()->GetNumberField(TEXT("crc32")));
		TestTrue(*FString::Printf(TEXT("Channel %d name matches"), ChannelIndex), Imported.Name == Expected.Name);
		TestTrue(*FString::Printf(TEXT("Channel %s bytes match"), *Expected.Name.ToString()), Imported.Bytes == Expected.Bytes);
		TestTrue(*FString::Printf(TEXT("Channel %s crc32 matches the header"), *Expected.Name.ToString()), FCrc::MemCrc32(Imported.Bytes.GetData(), Imported.Bytes.Num()) == HeaderCrc);
	}

	const TSharedPtr<FJsonObject> RepeatResult = ParseResultJson(UTDFluidSimStateCacheEditorLibrary::CreateSimStateCacheAsset(JsonPath, SimStateCacheEditorTestPackagePath, AssetName, false));
	TestTrue(TEXT("An existing asset is kept without overwrite"), RepeatResult.IsValid() && !IsResultOk(RepeatResult));
	const TSharedPtr<FJsonObject> OverwriteResult = ParseResultJson(UTDFluidSimStateCacheEditorLibrary::CreateSimStateCacheAsset(JsonPath, SimStateCacheEditorTestPackagePath, AssetName, true));
	TestTrue(TEXT("Overwrite reuses the existing asset"), IsResultOk(OverwriteResult) && FindObject<UTDFluidSimStateCacheAsset>(nullptr, *OverwriteResult->GetStringField(TEXT("object_path"))) == CacheAsset);
	return true;
}

#endif

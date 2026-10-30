#include "FluidFX/TDFluidSimStateCacheEditorLibrary.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "FluidFX/TDFluidLiveActor.h"
#include "FluidFX/TDFluidLiveComponent.h"
#include "FluidFX/TDFluidPresetAsset.h"
#include "FluidFX/TDFluidSimStateCacheAsset.h"
#include "FluidFX/TDFluidSimStateCacheTypes.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/Package.h"

namespace TDFluidSimStateCacheEditorPrivate
{
	FString SerializeJsonObject(const TSharedRef<FJsonObject>& JsonObject, bool bPretty)
	{
		FString Text;
		if (bPretty)
		{
			const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
			FJsonSerializer::Serialize(JsonObject, Writer);
			return Text;
		}

		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text);
		FJsonSerializer::Serialize(JsonObject, Writer);
		return Text;
	}

	FString MakeFailureJson(const FString& Error)
	{
		const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("ok"), false);
		Result->SetStringField(TEXT("error"), Error);
		return SerializeJsonObject(Result, false);
	}

	TSharedRef<FJsonObject> MakeSuccessObject()
	{
		const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("ok"), true);
		Result->SetStringField(TEXT("error"), FString());
		return Result;
	}

	TArray<TSharedPtr<FJsonValue>> MakeVectorArray(const FVector2D& Value)
	{
		return { MakeShared<FJsonValueNumber>(Value.X), MakeShared<FJsonValueNumber>(Value.Y) };
	}

	bool ReadVectorArray(const FJsonObject& JsonObject, const TCHAR* FieldName, FVector2D& OutValue)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!JsonObject.TryGetArrayField(FieldName, Values) || !Values || Values->Num() != 2)
		{
			return false;
		}

		OutValue = FVector2D((*Values)[0]->AsNumber(), (*Values)[1]->AsNumber());
		return true;
	}

	FString DescribeBackend(ETDFluidSimStateCacheBackend Backend)
	{
		return Backend == ETDFluidSimStateCacheBackend::Gpu ? TEXT("Gpu") : TEXT("Cpu");
	}

	TSharedRef<FJsonObject> MakeGpuLayoutObject(const FTDFluidGpuStateLayout& Layout)
	{
		const TSharedRef<FJsonObject> LayoutObject = MakeShared<FJsonObject>();
		LayoutObject->SetNumberField(TEXT("resolution_tier"), Layout.ResolutionTier);
		LayoutObject->SetNumberField(TEXT("sim_mode"), Layout.SimMode);
		LayoutObject->SetBoolField(TEXT("uses_heat_channel"), Layout.bUsesHeatChannel);
		LayoutObject->SetBoolField(TEXT("uses_water_layer"), Layout.bUsesWaterLayer);
		LayoutObject->SetBoolField(TEXT("uses_float32_water_velocity"), Layout.bUsesFloat32WaterVelocity);
		LayoutObject->SetBoolField(TEXT("uses_float32_ripple"), Layout.bUsesFloat32Ripple);
		LayoutObject->SetBoolField(TEXT("wraps_area"), Layout.bWrapsArea);
		LayoutObject->SetBoolField(TEXT("writes_painter_velocity"), Layout.bWritesPainterVelocity);
		LayoutObject->SetBoolField(TEXT("uses_dense_water"), Layout.bUsesDenseWater);
		return LayoutObject;
	}

	FTDFluidGpuStateLayout ReadGpuLayoutObject(const FJsonObject& LayoutObject)
	{
		FTDFluidGpuStateLayout Layout;
		Layout.ResolutionTier = static_cast<int32>(LayoutObject.GetNumberField(TEXT("resolution_tier")));
		Layout.SimMode = static_cast<uint8>(LayoutObject.GetNumberField(TEXT("sim_mode")));
		Layout.bUsesHeatChannel = LayoutObject.GetBoolField(TEXT("uses_heat_channel"));
		Layout.bUsesWaterLayer = LayoutObject.GetBoolField(TEXT("uses_water_layer"));
		Layout.bUsesFloat32WaterVelocity = LayoutObject.GetBoolField(TEXT("uses_float32_water_velocity"));
		Layout.bUsesFloat32Ripple = LayoutObject.GetBoolField(TEXT("uses_float32_ripple"));
		Layout.bWrapsArea = LayoutObject.GetBoolField(TEXT("wraps_area"));
		Layout.bWritesPainterVelocity = LayoutObject.GetBoolField(TEXT("writes_painter_velocity"));
		LayoutObject.TryGetBoolField(TEXT("uses_dense_water"), Layout.bUsesDenseWater);
		return Layout;
	}

	FString DescribePresetKind(const ATDFluidLiveActor& LiveActor)
	{
		const UTDFluidPresetAsset* Preset = LiveActor.LiveComponent ? LiveActor.LiveComponent->ActivePreset.Get() : nullptr;
		const ETDFluidPresetKind PresetKind = Preset ? Preset->PresetKind : ETDFluidPresetKind::Custom;
		return StaticEnum<ETDFluidPresetKind>()->GetNameStringByValue(static_cast<int64>(PresetKind));
	}

	TSharedRef<FJsonObject> MakeHeaderObject(const FTDFluidSimStateSnapshot& Snapshot, const ATDFluidLiveActor& LiveActor, const FString& BinFileName, TArray<uint8>& OutPayload)
	{
		const TSharedRef<FJsonObject> Header = MakeShared<FJsonObject>();
		Header->SetNumberField(TEXT("layout_version"), Snapshot.LayoutVersion);
		Header->SetStringField(TEXT("backend"), DescribeBackend(Snapshot.Backend));
		Header->SetNumberField(TEXT("resolution"), Snapshot.Resolution);
		Header->SetArrayField(TEXT("sim_extent_cm"), MakeVectorArray(Snapshot.SimExtentCm));
		Header->SetArrayField(TEXT("center_plane_cm"), MakeVectorArray(Snapshot.CenterPlaneCm));
		Header->SetNumberField(TEXT("area_address_mode"), static_cast<int32>(Snapshot.AreaAddressMode));
		Header->SetNumberField(TEXT("sim_mode"), static_cast<int32>(Snapshot.SimMode));
		Header->SetBoolField(TEXT("uses_heat_channel"), Snapshot.bUsesHeatChannel);
		Header->SetBoolField(TEXT("uses_water_layer"), Snapshot.bUsesWaterLayer);
		Header->SetBoolField(TEXT("uses_dense_water"), Snapshot.bUsesDenseWater);
		Header->SetNumberField(TEXT("simulation_time_seconds"), Snapshot.SimulationTimeSeconds);
		Header->SetObjectField(TEXT("gpu_layout"), MakeGpuLayoutObject(Snapshot.GpuLayout));
		Header->SetStringField(TEXT("preset_kind"), DescribePresetKind(LiveActor));
		const UWorld* World = LiveActor.GetWorld();
		Header->SetStringField(TEXT("source_level"), World ? UWorld::RemovePIEPrefix(World->GetOutermost()->GetName()) : FString());
		Header->SetStringField(TEXT("source_actor_label"), LiveActor.GetActorLabel());
		Header->SetStringField(TEXT("bin_file"), BinFileName);

		TArray<TSharedPtr<FJsonValue>> ChannelValues;
		OutPayload.Reset();
		for (const FTDFluidStateChannelBytes& Channel : Snapshot.Channels)
		{
			const TSharedRef<FJsonObject> ChannelObject = MakeShared<FJsonObject>();
			ChannelObject->SetStringField(TEXT("name"), Channel.Name.ToString());
			ChannelObject->SetNumberField(TEXT("width"), Channel.Width);
			ChannelObject->SetNumberField(TEXT("height"), Channel.Height);
			ChannelObject->SetNumberField(TEXT("pixel_format"), Channel.PixelFormat);
			ChannelObject->SetNumberField(TEXT("offset"), OutPayload.Num());
			ChannelObject->SetNumberField(TEXT("size"), Channel.Bytes.Num());
			ChannelObject->SetNumberField(TEXT("crc32"), FCrc::MemCrc32(Channel.Bytes.GetData(), Channel.Bytes.Num()));
			OutPayload.Append(Channel.Bytes);
			ChannelValues.Add(MakeShared<FJsonValueObject>(ChannelObject));
		}

		Header->SetArrayField(TEXT("channels"), ChannelValues);
		return Header;
	}

	bool ReadHeaderScalars(const FJsonObject& Header, FTDFluidSimStateSnapshot& OutSnapshot, FString& OutError)
	{
		const FString BackendText = Header.GetStringField(TEXT("backend"));
		if (BackendText != TEXT("Cpu") && BackendText != TEXT("Gpu"))
		{
			OutError = FString::Printf(TEXT("Unknown backend '%s'"), *BackendText);
			return false;
		}

		const TSharedPtr<FJsonObject>* LayoutObject = nullptr;
		if (!Header.TryGetObjectField(TEXT("gpu_layout"), LayoutObject) || !LayoutObject || !LayoutObject->IsValid())
		{
			OutError = TEXT("Header has no gpu_layout object");
			return false;
		}

		if (!ReadVectorArray(Header, TEXT("sim_extent_cm"), OutSnapshot.SimExtentCm) || !ReadVectorArray(Header, TEXT("center_plane_cm"), OutSnapshot.CenterPlaneCm))
		{
			OutError = TEXT("Header needs two component sim_extent_cm and center_plane_cm arrays");
			return false;
		}

		OutSnapshot.LayoutVersion = static_cast<int32>(Header.GetNumberField(TEXT("layout_version")));
		OutSnapshot.Backend = BackendText == TEXT("Gpu") ? ETDFluidSimStateCacheBackend::Gpu : ETDFluidSimStateCacheBackend::Cpu;
		OutSnapshot.Resolution = static_cast<int32>(Header.GetNumberField(TEXT("resolution")));
		OutSnapshot.AreaAddressMode = static_cast<ETDFluidAreaAddressMode>(static_cast<uint8>(Header.GetNumberField(TEXT("area_address_mode"))));
		OutSnapshot.SimMode = static_cast<ETDFluidSimMode>(static_cast<uint8>(Header.GetNumberField(TEXT("sim_mode"))));
		OutSnapshot.bUsesHeatChannel = Header.GetBoolField(TEXT("uses_heat_channel"));
		OutSnapshot.bUsesWaterLayer = Header.GetBoolField(TEXT("uses_water_layer"));
		Header.TryGetBoolField(TEXT("uses_dense_water"), OutSnapshot.bUsesDenseWater);
		OutSnapshot.SimulationTimeSeconds = Header.GetNumberField(TEXT("simulation_time_seconds"));
		OutSnapshot.GpuLayout = ReadGpuLayoutObject(**LayoutObject);
		if (OutSnapshot.LayoutVersion != TDFluidSimStateCacheLayoutVersion)
		{
			OutError = FString::Printf(TEXT("Layout version %d does not match %d"), OutSnapshot.LayoutVersion, TDFluidSimStateCacheLayoutVersion);
			return false;
		}

		return true;
	}

	bool ReadHeaderChannels(const FJsonObject& Header, const TArray<uint8>& Payload, FTDFluidSimStateSnapshot& OutSnapshot, FString& OutError)
	{
		const TArray<TSharedPtr<FJsonValue>>* ChannelValues = nullptr;
		if (!Header.TryGetArrayField(TEXT("channels"), ChannelValues) || !ChannelValues || ChannelValues->IsEmpty())
		{
			OutError = TEXT("Header has no channels");
			return false;
		}

		for (const TSharedPtr<FJsonValue>& ChannelValue : *ChannelValues)
		{
			const TSharedPtr<FJsonObject> ChannelObject = ChannelValue.IsValid() ? ChannelValue->AsObject() : nullptr;
			if (!ChannelObject.IsValid())
			{
				OutError = TEXT("Channel entry is not an object");
				return false;
			}

			const FString ChannelName = ChannelObject->GetStringField(TEXT("name"));
			const int64 Offset = static_cast<int64>(ChannelObject->GetNumberField(TEXT("offset")));
			const int64 Size = static_cast<int64>(ChannelObject->GetNumberField(TEXT("size")));
			if (ChannelName.IsEmpty() || Offset < 0 || Size <= 0 || Offset + Size > Payload.Num())
			{
				OutError = FString::Printf(TEXT("Channel '%s' range %lld+%lld is outside the %d byte payload"), *ChannelName, Offset, Size, Payload.Num());
				return false;
			}

			const uint32 ExpectedCrc = static_cast<uint32>(ChannelObject->GetNumberField(TEXT("crc32")));
			const uint32 ActualCrc = FCrc::MemCrc32(Payload.GetData() + Offset, static_cast<int32>(Size));
			if (ExpectedCrc != ActualCrc)
			{
				OutError = FString::Printf(TEXT("Channel '%s' crc32 %u does not match %u"), *ChannelName, ActualCrc, ExpectedCrc);
				return false;
			}

			FTDFluidStateChannelBytes& Channel = OutSnapshot.Channels.AddDefaulted_GetRef();
			Channel.Name = FName(*ChannelName);
			Channel.Width = static_cast<int32>(ChannelObject->GetNumberField(TEXT("width")));
			Channel.Height = static_cast<int32>(ChannelObject->GetNumberField(TEXT("height")));
			Channel.PixelFormat = static_cast<uint8>(ChannelObject->GetNumberField(TEXT("pixel_format")));
			Channel.Bytes = TArray<uint8>(Payload.GetData() + Offset, static_cast<int32>(Size));
		}

		return true;
	}

	UTDFluidSimStateCacheAsset* FindOrCreateCacheAsset(const FString& PackagePath, const FString& AssetName, bool bOverwrite, bool& bOutIsNew, FString& OutError)
	{
		FString PackageRoot = PackagePath;
		PackageRoot.RemoveFromEnd(TEXT("/"));
		const FString LongPackageName = PackageRoot / AssetName;
		if (AssetName.IsEmpty() || !FPackageName::IsValidLongPackageName(LongPackageName))
		{
			OutError = FString::Printf(TEXT("Invalid package name %s"), *LongPackageName);
			return nullptr;
		}

		UPackage* ExistingPackage = FindPackage(nullptr, *LongPackageName);
		UObject* ExistingObject = ExistingPackage ? StaticFindObject(UObject::StaticClass(), ExistingPackage, *AssetName) : nullptr;
		const bool bExists = ExistingObject || FPackageName::DoesPackageExist(LongPackageName);
		if (bExists && !bOverwrite)
		{
			OutError = FString::Printf(TEXT("%s already exists and overwrite is false"), *LongPackageName);
			return nullptr;
		}

		if (ExistingObject && !ExistingObject->IsA<UTDFluidSimStateCacheAsset>())
		{
			OutError = FString::Printf(TEXT("%s holds a %s, not a sim state cache"), *LongPackageName, *ExistingObject->GetClass()->GetName());
			return nullptr;
		}

		bOutIsNew = ExistingObject == nullptr;
		if (ExistingObject)
		{
			return Cast<UTDFluidSimStateCacheAsset>(ExistingObject);
		}

		UPackage* Package = CreatePackage(*LongPackageName);
		if (!Package)
		{
			OutError = FString::Printf(TEXT("Package %s is unavailable"), *LongPackageName);
			return nullptr;
		}

		return NewObject<UTDFluidSimStateCacheAsset>(Package, FName(*AssetName), RF_Public | RF_Standalone);
	}
}

FString UTDFluidSimStateCacheEditorLibrary::CaptureSimStateCacheToFile(ATDFluidLiveActor* LiveActor, const FString& OutputDirectory, const FString& CacheName)
{
	using namespace TDFluidSimStateCacheEditorPrivate;

	if (!LiveActor || !LiveActor->LiveComponent)
	{
		return MakeFailureJson(TEXT("Live actor or its live component is missing"));
	}

	if (CacheName.IsEmpty() || CacheName != FPaths::MakeValidFileName(CacheName) || CacheName.Contains(TEXT("/")) || CacheName.Contains(TEXT("\\")))
	{
		return MakeFailureJson(FString::Printf(TEXT("Cache name '%s' is not a plain file name"), *CacheName));
	}

	if (OutputDirectory.IsEmpty() || !IFileManager::Get().MakeDirectory(*OutputDirectory, true))
	{
		return MakeFailureJson(FString::Printf(TEXT("Output directory '%s' is unavailable"), *OutputDirectory));
	}

	FTDFluidSimStateSnapshot Snapshot;
	FString CaptureError;
	if (!LiveActor->LiveComponent->CaptureSimStateSnapshot(Snapshot, CaptureError))
	{
		return MakeFailureJson(FString::Printf(TEXT("Capture failed: %s"), *CaptureError));
	}

	const FString BinFileName = CacheName + TEXT(".bin");
	const FString JsonPath = FPaths::ConvertRelativePathToFull(OutputDirectory / (CacheName + TEXT(".json")));
	const FString BinPath = FPaths::ConvertRelativePathToFull(OutputDirectory / BinFileName);
	TArray<uint8> Payload;
	const TSharedRef<FJsonObject> Header = MakeHeaderObject(Snapshot, *LiveActor, BinFileName, Payload);
	if (!FFileHelper::SaveArrayToFile(Payload, *BinPath))
	{
		return MakeFailureJson(FString::Printf(TEXT("Could not write %s"), *BinPath));
	}

	if (!FFileHelper::SaveStringToFile(SerializeJsonObject(Header, true), *JsonPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		return MakeFailureJson(FString::Printf(TEXT("Could not write %s"), *JsonPath));
	}

	const TSharedRef<FJsonObject> Result = MakeSuccessObject();
	Result->SetStringField(TEXT("json_path"), JsonPath);
	Result->SetStringField(TEXT("bin_path"), BinPath);
	Result->SetStringField(TEXT("backend"), DescribeBackend(Snapshot.Backend));
	Result->SetNumberField(TEXT("resolution"), Snapshot.Resolution);
	Result->SetNumberField(TEXT("channel_count"), Snapshot.Channels.Num());
	Result->SetNumberField(TEXT("total_bytes"), Payload.Num());
	return SerializeJsonObject(Result, false);
}

FString UTDFluidSimStateCacheEditorLibrary::CreateSimStateCacheAsset(const FString& SourceJsonPath, const FString& PackagePath, const FString& AssetName, bool bOverwrite)
{
	using namespace TDFluidSimStateCacheEditorPrivate;

	FString HeaderText;
	if (!FFileHelper::LoadFileToString(HeaderText, *SourceJsonPath))
	{
		return MakeFailureJson(FString::Printf(TEXT("Could not read %s"), *SourceJsonPath));
	}

	TSharedPtr<FJsonObject> Header;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(HeaderText);
	if (!FJsonSerializer::Deserialize(Reader, Header) || !Header.IsValid())
	{
		return MakeFailureJson(FString::Printf(TEXT("%s is not a JSON object"), *SourceJsonPath));
	}

	FString BinFileName;
	if (!Header->TryGetStringField(TEXT("bin_file"), BinFileName) || BinFileName.IsEmpty())
	{
		return MakeFailureJson(TEXT("Header has no bin_file"));
	}

	TArray<uint8> Payload;
	const FString BinPath = FPaths::GetPath(SourceJsonPath) / BinFileName;
	if (!FFileHelper::LoadFileToArray(Payload, *BinPath))
	{
		return MakeFailureJson(FString::Printf(TEXT("Could not read %s"), *BinPath));
	}

	FTDFluidSimStateSnapshot Snapshot;
	FString ReadError;
	if (!ReadHeaderScalars(*Header, Snapshot, ReadError) || !ReadHeaderChannels(*Header, Payload, Snapshot, ReadError))
	{
		return MakeFailureJson(ReadError);
	}

	bool bIsNewAsset = false;
	FString AssetError;
	UTDFluidSimStateCacheAsset* CacheAsset = FindOrCreateCacheAsset(PackagePath, AssetName, bOverwrite, bIsNewAsset, AssetError);
	if (!CacheAsset)
	{
		return MakeFailureJson(AssetError);
	}

	CacheAsset->Modify();
	CacheAsset->SetFromSnapshot(Snapshot);
	const int64 PresetKindValue = StaticEnum<ETDFluidPresetKind>()->GetValueByNameString(Header->GetStringField(TEXT("preset_kind")));
	CacheAsset->PresetKind = PresetKindValue == INDEX_NONE ? ETDFluidPresetKind::Custom : static_cast<ETDFluidPresetKind>(PresetKindValue);
	CacheAsset->SourceLevel = Header->GetStringField(TEXT("source_level"));
	CacheAsset->SourceActorLabel = Header->GetStringField(TEXT("source_actor_label"));
	CacheAsset->SourceChecksum = CacheAsset->ComputeChecksum();
	CacheAsset->MarkPackageDirty();
	if (bIsNewAsset)
	{
		FAssetRegistryModule::AssetCreated(CacheAsset);
	}

	const TSharedRef<FJsonObject> Result = MakeSuccessObject();
	Result->SetStringField(TEXT("asset_path"), CacheAsset->GetPackage()->GetName());
	Result->SetStringField(TEXT("object_path"), CacheAsset->GetPathName());
	Result->SetBoolField(TEXT("created"), bIsNewAsset);
	Result->SetBoolField(TEXT("is_usable"), CacheAsset->IsUsable());
	Result->SetNumberField(TEXT("channel_count"), CacheAsset->Channels.Num());
	Result->SetNumberField(TEXT("checksum"), static_cast<double>(CacheAsset->SourceChecksum));
	return SerializeJsonObject(Result, false);
}

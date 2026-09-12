#include "TDWorldGenEditorLibrary.h"

#include "Dungeon/TDDungeonDefinitions.h"
#include "Dungeon/TDDungeonGeneration.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Landscape.h"
#include "LandscapeHeightfieldCollisionComponent.h"
#include "LandscapeProxy.h"
#include "Logging/MessageLog.h"
#include "Logging/TokenizedMessage.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "PCGComponent.h"
#include "ScopedTransaction.h"
#include "TDDungeonBaker.h"
#include "TDWorldBaker.h"
#include "World/Generation/TDWorldAnchorActor.h"
#include "World/TDWorldDefinitions.h"
#include "World/TDWorldGeneration.h"

#define LOCTEXT_NAMESPACE "TDWorldGenEditorLibrary"

DEFINE_LOG_CATEGORY_STATIC(LogTDWorldGenEditor, Log, All);

namespace
{
	const FName MessageLogName(TEXT("TDWorldGen"));

	UWorld* ResolveWorld(UObject* WorldContextObject)
	{
		return GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	}

	bool IsPersistentAsset(const UObject* Object)
	{
		return Object && Object->GetOutermost() != GetTransientPackage() && !Object->HasAnyFlags(RF_Transient);
	}

	EMessageSeverity::Type ToMessageSeverity(ETDValidationSeverity Severity)
	{
		switch (Severity)
		{
		case ETDValidationSeverity::Error: return EMessageSeverity::Error;
		case ETDValidationSeverity::Warning: return EMessageSeverity::Warning;
		default: return EMessageSeverity::Info;
		}
	}

	void RecordSlotInAtlas(UTDDungeonAtlasDefinition& Atlas, const FTDDungeonLayout& Layout, const FVector& SlotOriginCm, int32 SlotIndex, FName DungeonId, UTDDungeonTheme* Theme, UTDDungeonFlowTemplate* FlowTemplate)
	{
		FTDDungeonSlot* Slot = Atlas.Slots.FindByPredicate([SlotIndex](const FTDDungeonSlot& Candidate) { return Candidate.SlotIndex == SlotIndex; });
		if (!Slot)
		{
			Slot = &Atlas.Slots.AddDefaulted_GetRef();
			Slot->SlotIndex = SlotIndex;
		}
		Atlas.Modify();
		Slot->DungeonId = DungeonId;
		Slot->WorldTransform = FTransform(SlotOriginCm);
		Slot->Seed = Layout.Seed;
		Slot->Size = Layout.Size;
		Slot->GeneratorVersion = Layout.GeneratorVersion;
		Slot->EntryTransform = FTransform(Layout.EntryTransform.GetRotation(), SlotOriginCm + Layout.EntryTransform.GetLocation());
		Slot->ExitTransform = FTransform(Layout.ExitTransform.GetRotation(), SlotOriginCm + Layout.ExitTransform.GetLocation());
		Slot->Bounds = FTDDungeonBaker::ComputeWorldBounds(Layout, SlotOriginCm);
		Slot->bValidationPassed = Layout.Validation.bPassed;
		if (IsPersistentAsset(Theme))
		{
			Slot->Theme = Theme;
		}
		if (IsPersistentAsset(FlowTemplate))
		{
			Slot->FlowTemplate = FlowTemplate;
		}
		Atlas.MarkPackageDirty();
	}
}

FVector UTDWorldGenEditorLibrary::ResolveSlotOriginCm(const UTDDungeonAtlasDefinition* Atlas, int32 SlotIndex)
{
	if (Atlas)
	{
		return Atlas->GetSlotOriginCm(SlotIndex);
	}
	return FVector(300000.0 + SlotIndex * 30000.0, 300000.0, 0.0);
}

void UTDWorldGenEditorLibrary::BuildLandscapeSampler(ALandscapeProxy* Landscape, FTDTerrainSampler& OutSampler)
{
	if (!Landscape)
	{
		return;
	}
	TWeakObjectPtr<ALandscapeProxy> WeakLandscape(Landscape);
	auto SampleHeight = [WeakLandscape](const FVector2D& LocationCm) -> float
	{
		ALandscapeProxy* Proxy = WeakLandscape.Get();
		if (!Proxy)
		{
			return 0.0f;
		}
		const TOptional<float> Height = Proxy->GetHeightAtLocation(FVector(LocationCm.X, LocationCm.Y, 0.0), EHeightfieldSource::Editor);
		return Height.IsSet() ? Height.GetValue() : 0.0f;
	};
	OutSampler.HeightCm = SampleHeight;
	OutSampler.SlopeDeg = [SampleHeight](const FVector2D& LocationCm) -> float
	{
		constexpr float StepCm = 100.0f;
		const float GradientX = (SampleHeight(LocationCm + FVector2D(StepCm, 0.0f)) - SampleHeight(LocationCm - FVector2D(StepCm, 0.0f))) / (2.0f * StepCm);
		const float GradientY = (SampleHeight(LocationCm + FVector2D(0.0f, StepCm)) - SampleHeight(LocationCm - FVector2D(0.0f, StepCm))) / (2.0f * StepCm);
		return FMath::RadiansToDegrees(FMath::Atan(FMath::Sqrt(GradientX * GradientX + GradientY * GradientY)));
	};
}

FString UTDWorldGenEditorLibrary::ResolveProjectRelativePath(const FString& Path)
{
	if (FPaths::IsRelative(Path))
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / Path);
	}
	return Path;
}

bool UTDWorldGenEditorLibrary::GenerateDungeonLayout(UTDDungeonTheme* Theme, UTDDungeonFlowTemplate* FlowTemplate, ETDDungeonSize Size, int32 Seed, FTDDungeonLayout& OutLayout, FString& OutError)
{
	if (!Theme)
	{
		OutError = TEXT("Theme is null");
		return false;
	}
	if (!FlowTemplate)
	{
		OutError = TEXT("FlowTemplate is null");
		return false;
	}
	return FTDDungeonGenerator::GenerateAndValidate(*Theme, *FlowTemplate, Size, Seed, OutLayout, OutError);
}

bool UTDWorldGenEditorLibrary::BakeDungeonToSlot(UObject* WorldContextObject, const FTDDungeonLayout& Layout, UTDDungeonAtlasDefinition* Atlas, int32 SlotIndex, FName DungeonId, bool bClearExisting, FString& OutError)
{
	UWorld* World = ResolveWorld(WorldContextObject);
	if (!World)
	{
		OutError = TEXT("No world from context object");
		return false;
	}
	if (SlotIndex < 0)
	{
		OutError = TEXT("SlotIndex must be non-negative");
		return false;
	}
	return FTDDungeonBaker::Bake(World, Layout, ResolveSlotOriginCm(Atlas, SlotIndex), DungeonId, SlotIndex, bClearExisting, OutError);
}

bool UTDWorldGenEditorLibrary::GenerateAndBakeDungeon(UObject* WorldContextObject, UTDDungeonTheme* Theme, UTDDungeonFlowTemplate* FlowTemplate, ETDDungeonSize Size, int32 Seed, UTDDungeonAtlasDefinition* Atlas, int32 SlotIndex, FName DungeonId, FString& OutReportMarkdown)
{
	const FString Title = FString::Printf(TEXT("Dungeon %s slot %d seed %d"), *DungeonId.ToString(), SlotIndex, Seed);
	FTDDungeonLayout Layout;
	FString Error;
	if (!GenerateDungeonLayout(Theme, FlowTemplate, Size, Seed, Layout, Error))
	{
		OutReportMarkdown = FString::Printf(TEXT("# %s\n\nGeneration failed: %s\n"), *Title, *Error);
		UE_LOG(LogTDWorldGenEditor, Error, TEXT("%s: generation failed: %s"), *Title, *Error);
		return false;
	}

	const FVector SlotOriginCm = ResolveSlotOriginCm(Atlas, SlotIndex);
	UWorld* World = ResolveWorld(WorldContextObject);
	if (!World)
	{
		OutReportMarkdown = Layout.Validation.ToMarkdown(Title) + TEXT("\nBake skipped: no world from context object\n");
		return false;
	}
	if (!FTDDungeonBaker::Bake(World, Layout, SlotOriginCm, DungeonId, SlotIndex, true, Error))
	{
		OutReportMarkdown = Layout.Validation.ToMarkdown(Title) + FString::Printf(TEXT("\nBake failed: %s\n"), *Error);
		UE_LOG(LogTDWorldGenEditor, Error, TEXT("%s: bake failed: %s"), *Title, *Error);
		return false;
	}
	if (Atlas)
	{
		RecordSlotInAtlas(*Atlas, Layout, SlotOriginCm, SlotIndex, DungeonId, Theme, FlowTemplate);
	}

	const FBox Bounds = FTDDungeonBaker::ComputeWorldBounds(Layout, SlotOriginCm);
	OutReportMarkdown = Layout.Validation.ToMarkdown(Title);
	OutReportMarkdown += FString::Printf(TEXT("\n- Rooms: %d, Doors: %d, Restarts: %d\n- Slot origin: (%.0f, %.0f, %.0f)\n- Bounds: (%.0f, %.0f) - (%.0f, %.0f)\n- Score: %.2f\n- Atlas updated: %s\n"),
		Layout.Rooms.Num(), Layout.Doors.Num(), Layout.LayoutRestarts, SlotOriginCm.X, SlotOriginCm.Y, SlotOriginCm.Z,
		Bounds.Min.X, Bounds.Min.Y, Bounds.Max.X, Bounds.Max.Y, FTDCandidateSelector::ScoreDungeon(Layout), Atlas ? TEXT("yes") : TEXT("no"));
	return Layout.Validation.bPassed;
}

FString UTDWorldGenEditorLibrary::ExportDungeonLayoutJson(const FTDDungeonLayout& Layout, FVector WorldOffsetCm)
{
	return FTDDungeonGenerator::ToJson(Layout, WorldOffsetCm);
}

bool UTDWorldGenEditorLibrary::GenerateWorldLayout(UObject* WorldContextObject, UTDRegionDefinition* Region, const TArray<FTDWorldAnchor>& Anchors, FBox2D BoundsCm, int32 Seed, ALandscapeProxy* LandscapeForSampling, UTDDungeonAtlasDefinition* Atlas, FTDWorldLayout& OutLayout, FString& OutError)
{
	return GenerateWorldLayoutWithLocked(WorldContextObject, Region, Anchors, FTDLockedLayoutElements(), BoundsCm, Seed, LandscapeForSampling, Atlas, OutLayout, OutError);
}

bool UTDWorldGenEditorLibrary::GenerateWorldLayoutWithLocked(UObject* WorldContextObject, UTDRegionDefinition* Region, const TArray<FTDWorldAnchor>& Anchors, const FTDLockedLayoutElements& Locked, FBox2D BoundsCm, int32 Seed, ALandscapeProxy* LandscapeForSampling, UTDDungeonAtlasDefinition* Atlas, FTDWorldLayout& OutLayout, FString& OutError)
{
	if (!Region)
	{
		OutError = TEXT("Region is null");
		return false;
	}
	if (!BoundsCm.bIsValid || BoundsCm.GetArea() <= 0.0)
	{
		OutError = TEXT("BoundsCm is empty");
		return false;
	}
	FTDTerrainSampler Sampler;
	BuildLandscapeSampler(LandscapeForSampling, Sampler);
	const FTDTerrainSampler* TerrainPtr = Sampler.IsValid() ? &Sampler : nullptr;
	return FTDWorldGenerator::GenerateAndValidate(*Region, Anchors, Locked, BoundsCm, Seed, TerrainPtr, Atlas, OutLayout, OutError);
}

bool UTDWorldGenEditorLibrary::BakeWorldLayoutInRegion(UObject* WorldContextObject, const FTDWorldLayout& Layout, FName RegionFilter, bool bClearExisting, FString& OutStats, FString& OutError)
{
	UWorld* World = ResolveWorld(WorldContextObject);
	if (!World)
	{
		OutError = TEXT("No world from context object");
		return false;
	}
	FTDWorldBakeOptions Options;
	Options.bClearExisting = bClearExisting;
	Options.RegionFilter = RegionFilter;
	FTDWorldBakeStats Stats;
	const bool bBaked = FTDWorldBaker::BakeWithOptions(World, Layout, Options, Stats, OutError);
	OutStats = Stats.ToString();
	return bBaked;
}

FTDLockedLayoutElements UTDWorldGenEditorLibrary::CollectLockedLayoutElements(UObject* WorldContextObject)
{
	FTDLockedLayoutElements Locked;
	FTDWorldBaker::CollectLockedElements(ResolveWorld(WorldContextObject), Locked);
	return Locked;
}

TArray<FTDWorldAnchor> UTDWorldGenEditorLibrary::CollectWorldAnchorActors(UObject* WorldContextObject)
{
	TArray<FTDWorldAnchor> Anchors;
	UWorld* World = ResolveWorld(WorldContextObject);
	if (!World)
	{
		return Anchors;
	}
	for (TActorIterator<ATDWorldAnchorActor> It(World); It; ++It)
	{
		FTDWorldAnchor& Anchor = Anchors.AddDefaulted_GetRef();
		Anchor.AnchorId = It->AnchorId.IsNone() ? FName(*It->GetActorLabel()) : It->AnchorId;
		Anchor.Kind = It->Kind;
		Anchor.LocationCm = It->GetActorLocation();
		Anchor.YawDeg = It->GetActorRotation().Yaw;
		Anchor.ExclusionRadiusCm = It->ExclusionRadiusCm;
		Anchor.bLocked = It->bLocked;
	}
	return Anchors;
}

bool UTDWorldGenEditorLibrary::ResolveRegionVolumeBounds(UObject* WorldContextObject, FName RegionId, FBox2D& OutBoundsCm)
{
	return FTDWorldBaker::ResolveRegionBounds(ResolveWorld(WorldContextObject), RegionId, OutBoundsCm);
}

ALandscapeProxy* UTDWorldGenEditorLibrary::FindLandscapeForSampling(UObject* WorldContextObject)
{
	UWorld* World = ResolveWorld(WorldContextObject);
	if (!World)
	{
		return nullptr;
	}
	ALandscapeProxy* Fallback = nullptr;
	for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
	{
		if (It->IsA<ALandscape>())
		{
			return *It;
		}
		Fallback = Fallback ? Fallback : *It;
	}
	return Fallback;
}

FTDValidationReport UTDWorldGenEditorLibrary::ValidateWorldLayout(const FTDWorldLayout& Layout, UTDDungeonAtlasDefinition* Atlas)
{
	return FTDWorldValidator::Validate(Layout, Atlas, nullptr, FTDWorldValidator::FSettings());
}

bool UTDWorldGenEditorLibrary::BakeWorldLayout(UObject* WorldContextObject, const FTDWorldLayout& Layout, bool bClearExisting, FString& OutError)
{
	UWorld* World = ResolveWorld(WorldContextObject);
	if (!World)
	{
		OutError = TEXT("No world from context object");
		return false;
	}
	return FTDWorldBaker::Bake(World, Layout, bClearExisting, OutError);
}

void UTDWorldGenEditorLibrary::LogReportToMessageLog(const FTDValidationReport& Report, const FString& Title)
{
	FMessageLog Log(MessageLogName);
	Log.NewPage(FText::FromString(Title));
	Log.Info(FText::FromString(FString::Printf(TEXT("%s: score %.2f, %s, errors %d, warnings %d"), *Title, Report.Score, Report.bPassed ? TEXT("passed") : TEXT("failed"),
		Report.CountBySeverity(ETDValidationSeverity::Error), Report.CountBySeverity(ETDValidationSeverity::Warning))));
	for (const FTDValidationItem& Item : Report.Items)
	{
		TSharedRef<FTokenizedMessage> Message = FTokenizedMessage::Create(ToMessageSeverity(Item.Severity));
		Message->AddToken(FTextToken::Create(FText::FromString(FString::Printf(TEXT("[%s]"), *Item.Code.ToString()))));
		Message->AddToken(FTextToken::Create(FText::FromString(Item.Message)));
		if (!Item.RelatedId.IsNone())
		{
			Message->AddToken(FTextToken::Create(FText::FromString(FString::Printf(TEXT("id=%s"), *Item.RelatedId.ToString()))));
		}
		if (!Item.WorldLocation.IsNearlyZero())
		{
			Message->AddToken(FTextToken::Create(FText::FromString(FString::Printf(TEXT("@ (%.0f, %.0f, %.0f)"), Item.WorldLocation.X, Item.WorldLocation.Y, Item.WorldLocation.Z))));
		}
		Log.AddMessage(Message);
	}
	if (Report.HasErrors() && GIsEditor && !IsRunningCommandlet())
	{
		Log.Notify(FText::FromString(Title), EMessageSeverity::Error, true);
	}
}

int32 UTDWorldGenEditorLibrary::RemoveActorsWithLabelPrefix(UObject* WorldContextObject, const FString& Prefix)
{
	UWorld* World = ResolveWorld(WorldContextObject);
	if (!World || Prefix.IsEmpty())
	{
		return 0;
	}
	const FName LockedTag = FTDWorldBaker::GetLockedTagName();
	TArray<AActor*> ToRemove;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->GetActorLabel().StartsWith(Prefix) && !It->Tags.Contains(LockedTag))
		{
			ToRemove.Add(*It);
		}
	}
	if (ToRemove.Num() == 0)
	{
		return 0;
	}
	const FScopedTransaction Transaction(LOCTEXT("RemoveByPrefix", "TD Remove Actors By Label Prefix"));
	for (AActor* Actor : ToRemove)
	{
		World->EditorDestroyActor(Actor, true);
	}
	return ToRemove.Num();
}

bool UTDWorldGenEditorLibrary::WriteTextFile(const FString& Path, const FString& Text)
{
	if (Path.IsEmpty())
	{
		return false;
	}
	const FString FullPath = ResolveProjectRelativePath(Path);
	return FFileHelper::SaveStringToFile(Text, *FullPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

namespace
{
	const TCHAR* DoorLabelPrefix = TEXT("Door_");
	const TCHAR* SouthWallLabelPrefix = TEXT("Wall_South_");
	const FName DoorSocketTag(TEXT("TDDoorSocket"));
	constexpr float MaxSouthWallHeightCm = 120.0f;
	constexpr float SocketPositionToleranceCm = 1.0f;

	bool ParseDirectionLetter(const FString& Letter, ETDDoorDirection& OutDirection)
	{
		if (Letter == TEXT("N")) { OutDirection = ETDDoorDirection::North; return true; }
		if (Letter == TEXT("E")) { OutDirection = ETDDoorDirection::East; return true; }
		if (Letter == TEXT("S")) { OutDirection = ETDDoorDirection::South; return true; }
		if (Letter == TEXT("W")) { OutDirection = ETDDoorDirection::West; return true; }
		return false;
	}

	bool ParseDirectionTag(const AActor& Actor, ETDDoorDirection& OutDirection)
	{
		const UEnum* DirectionEnum = StaticEnum<ETDDoorDirection>();
		for (const FName& Tag : Actor.Tags)
		{
			const int64 Value = DirectionEnum->GetValueByName(Tag);
			if (Value != INDEX_NONE)
			{
				OutDirection = static_cast<ETDDoorDirection>(Value);
				return true;
			}
		}
		return false;
	}

	bool TryParseDoorSocketFromLabel(const FString& Label, FTDDoorSocket& OutSocket)
	{
		if (!Label.StartsWith(DoorLabelPrefix))
		{
			return false;
		}
		TArray<FString> Parts;
		Label.ParseIntoArray(Parts, TEXT("_"), true);
		if (Parts.Num() != 4 || !Parts[2].IsNumeric() || !Parts[3].IsNumeric())
		{
			return false;
		}
		if (!ParseDirectionLetter(Parts[1], OutSocket.Direction))
		{
			return false;
		}
		OutSocket.Cell = FIntPoint(FCString::Atoi(*Parts[2]), FCString::Atoi(*Parts[3]));
		return true;
	}

	bool TryParseDoorSocketFromTags(const AActor& Actor, int32 CellSizeCm, FTDDoorSocket& OutSocket)
	{
		if (!Actor.Tags.Contains(DoorSocketTag) || !ParseDirectionTag(Actor, OutSocket.Direction))
		{
			return false;
		}
		const FIntPoint Offset = TDDungeon::DirectionOffset(OutSocket.Direction);
		const FVector CellCenter = Actor.GetActorLocation() - FVector(Offset.X, Offset.Y, 0) * (CellSizeCm * 0.5f);
		OutSocket.Cell = FIntPoint(FMath::FloorToInt(CellCenter.X / CellSizeCm), FMath::FloorToInt(CellCenter.Y / CellSizeCm));
		return true;
	}

	FVector ExpectedSocketLocation(const FTDDoorSocket& Socket, int32 CellSizeCm)
	{
		const FIntPoint Offset = TDDungeon::DirectionOffset(Socket.Direction);
		const float Half = CellSizeCm * 0.5f;
		return FVector((Socket.Cell.X + 0.5f) * CellSizeCm + Offset.X * Half, (Socket.Cell.Y + 0.5f) * CellSizeCm + Offset.Y * Half, 0.0f);
	}

	bool SocketMatches(const FTDDoorSocket& A, const FTDDoorSocket& B)
	{
		return A.Cell == B.Cell && A.Direction == B.Direction;
	}

	FString DescribeSocket(const FTDDoorSocket& Socket)
	{
		return FString::Printf(TEXT("(%d,%d) %s"), Socket.Cell.X, Socket.Cell.Y, *StaticEnum<ETDDoorDirection>()->GetNameStringByValue(static_cast<int64>(Socket.Direction)));
	}

	void ValidateRoomModuleLevel(const FTDRoomModuleDefinition& Module, int32 CellSizeCm, FTDValidationReport& Report, int32& InOutFailedModules)
	{
		const int32 ErrorsBefore = Report.CountBySeverity(ETDValidationSeverity::Error);
		if (Module.LevelAsset.IsNull())
		{
			Report.Add(ETDValidationSeverity::Warning, TEXT("RoomModule.LevelUnset"), TEXT("LevelAsset이 비어 있어 검사를 건너뜁니다"), FVector::ZeroVector, Module.ModuleId);
			return;
		}
		UWorld* LevelWorld = Module.LevelAsset.LoadSynchronous();
		if (!LevelWorld || !LevelWorld->PersistentLevel)
		{
			Report.Add(ETDValidationSeverity::Error, TEXT("RoomModule.LevelLoad"), FString::Printf(TEXT("레벨을 로드할 수 없습니다: %s"), *Module.LevelAsset.ToString()), FVector::ZeroVector, Module.ModuleId);
			++InOutFailedModules;
			return;
		}

		TArray<FTDDoorSocket> FoundSockets;
		bool bHasNavBounds = false;
		for (AActor* Actor : LevelWorld->PersistentLevel->Actors)
		{
			if (!Actor)
			{
				continue;
			}
			bHasNavBounds |= Actor->IsA<ANavMeshBoundsVolume>();
			const FString Label = Actor->GetActorLabel();
			if (Label.StartsWith(SouthWallLabelPrefix))
			{
				const float HeightCm = Actor->GetActorScale3D().Z * 100.0f;
				if (HeightCm > MaxSouthWallHeightCm + KINDA_SMALL_NUMBER)
				{
					Report.Add(ETDValidationSeverity::Error, TEXT("RoomModule.SouthWallHeight"), FString::Printf(TEXT("%s 남쪽 벽 높이 %.0f cm > %.0f cm"), *Label, HeightCm, MaxSouthWallHeightCm), Actor->GetActorLocation(), Module.ModuleId);
				}
			}
			FTDDoorSocket Socket;
			if (!TryParseDoorSocketFromLabel(Label, Socket) && !TryParseDoorSocketFromTags(*Actor, CellSizeCm, Socket))
			{
				continue;
			}
			FoundSockets.Add(Socket);
			const float OffsetCm = FVector::Dist2D(Actor->GetActorLocation(), ExpectedSocketLocation(Socket, CellSizeCm));
			if (OffsetCm > SocketPositionToleranceCm)
			{
				Report.Add(ETDValidationSeverity::Warning, TEXT("RoomModule.SocketOffset"), FString::Printf(TEXT("문 마커 %s 위치가 규격 위치와 %.0f cm 차이"), *DescribeSocket(Socket), OffsetCm), Actor->GetActorLocation(), Module.ModuleId);
			}
		}

		for (const FTDDoorSocket& Expected : Module.Sockets)
		{
			const bool bFound = FoundSockets.ContainsByPredicate([&Expected](const FTDDoorSocket& Found) { return SocketMatches(Found, Expected); });
			if (!bFound)
			{
				Report.Add(ETDValidationSeverity::Error, TEXT("RoomModule.SocketMissing"), FString::Printf(TEXT("테마 소켓 %s에 해당하는 문 마커가 레벨에 없습니다"), *DescribeSocket(Expected)), ExpectedSocketLocation(Expected, CellSizeCm), Module.ModuleId);
			}
		}
		for (const FTDDoorSocket& Found : FoundSockets)
		{
			const bool bDefined = Module.Sockets.ContainsByPredicate([&Found](const FTDDoorSocket& Expected) { return SocketMatches(Found, Expected); });
			if (!bDefined)
			{
				Report.Add(ETDValidationSeverity::Error, TEXT("RoomModule.SocketExtra"), FString::Printf(TEXT("레벨의 문 마커 %s가 테마 소켓에 없습니다"), *DescribeSocket(Found)), ExpectedSocketLocation(Found, CellSizeCm), Module.ModuleId);
			}
		}
		if (FoundSockets.Num() != Module.Sockets.Num())
		{
			Report.Add(ETDValidationSeverity::Error, TEXT("RoomModule.SocketCount"), FString::Printf(TEXT("문 마커 %d개, 테마 소켓 %d개"), FoundSockets.Num(), Module.Sockets.Num()), FVector::ZeroVector, Module.ModuleId);
		}
		if (!bHasNavBounds)
		{
			Report.Add(ETDValidationSeverity::Error, TEXT("RoomModule.NavBoundsMissing"), TEXT("NavMeshBoundsVolume이 없습니다"), FVector::ZeroVector, Module.ModuleId);
		}

		const bool bModuleFailed = Report.CountBySeverity(ETDValidationSeverity::Error) > ErrorsBefore;
		InOutFailedModules += bModuleFailed ? 1 : 0;
		if (!bModuleFailed)
		{
			Report.Add(ETDValidationSeverity::Info, TEXT("RoomModule.Ok"), FString::Printf(TEXT("소켓 %d개 일치, 내비 볼륨 있음"), FoundSockets.Num()), FVector::ZeroVector, Module.ModuleId);
		}
	}

	AActor* FindActorByLabel(UWorld* World, const FString& Label)
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->GetActorLabel() == Label)
			{
				return *It;
			}
		}
		return nullptr;
	}
}

FTDValidationReport UTDWorldGenEditorLibrary::ValidateRoomModuleLevels(UTDDungeonTheme* Theme)
{
	FTDValidationReport Report;
	if (!Theme)
	{
		Report.Add(ETDValidationSeverity::Error, TEXT("RoomModule.Theme"), TEXT("Theme이 null입니다"));
		return Report;
	}
	int32 FailedModules = 0;
	for (const FTDRoomModuleDefinition& Module : Theme->Modules)
	{
		ValidateRoomModuleLevel(Module, Theme->CellSizeCm, Report, FailedModules);
	}
	const int32 ModuleCount = FMath::Max(Theme->Modules.Num(), 1);
	Report.Score = 100.0f * static_cast<float>(ModuleCount - FailedModules) / ModuleCount;
	Report.bPassed = !Report.HasErrors();
	return Report;
}

FTDValidationReport UTDWorldGenEditorLibrary::ValidateDungeonNavigation(UObject* WorldContextObject, UTDDungeonAtlasDefinition* Atlas, int32 SlotIndex)
{
	FTDValidationReport Report;
	UWorld* World = ResolveWorld(WorldContextObject);
	if (!World)
	{
		Report.Add(ETDValidationSeverity::Error, TEXT("Nav.World"), TEXT("월드 컨텍스트가 없습니다"));
		return Report;
	}
	const FTDDungeonSlot* Slot = Atlas ? Atlas->Slots.FindByPredicate([SlotIndex](const FTDDungeonSlot& Candidate) { return Candidate.SlotIndex == SlotIndex; }) : nullptr;
	if (!Slot)
	{
		Report.Add(ETDValidationSeverity::Error, TEXT("Nav.Slot"), FString::Printf(TEXT("아틀라스에 슬롯 %d가 없습니다"), SlotIndex));
		return Report;
	}
	UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(World);
	if (!NavSys || !NavSys->GetDefaultNavDataInstance())
	{
		Report.Add(ETDValidationSeverity::Warning, TEXT("Nav.NoNavData"), TEXT("내비메시 없음: 빌드 필요"), Slot->EntryTransform.GetLocation(), Slot->DungeonId);
		Report.bPassed = true;
		return Report;
	}

	const FVector Start = Slot->EntryTransform.GetLocation();
	FVector End = Slot->ExitTransform.GetLocation();
	FString TargetName = TEXT("ExitTransform");
	if (End.Equals(Start, 1.0f) || End.IsNearlyZero())
	{
		AActor* SlotMarker = FindActorByLabel(World, FString::Printf(TEXT("TDDungeonSlot_%d"), SlotIndex));
		if (!SlotMarker)
		{
			Report.Add(ETDValidationSeverity::Error, TEXT("Nav.NoTarget"), FString::Printf(TEXT("ExitTransform이 비어 있고 마커 TDDungeonSlot_%d도 없습니다"), SlotIndex), Start, Slot->DungeonId);
			return Report;
		}
		End = SlotMarker->GetActorLocation();
		TargetName = FString::Printf(TEXT("TDDungeonSlot_%d"), SlotIndex);
	}

	UNavigationPath* Path = UNavigationSystemV1::FindPathToLocationSynchronously(World, Start, End);
	if (!Path || !Path->IsValid() || Path->IsPartial())
	{
		Report.Add(ETDValidationSeverity::Error, TEXT("Nav.PathFailed"), FString::Printf(TEXT("입구→%s 경로 %s"), *TargetName, Path && Path->IsPartial() ? TEXT("부분 경로(끊김)") : TEXT("없음")), Start, Slot->DungeonId);
		return Report;
	}
	Report.Add(ETDValidationSeverity::Info, TEXT("Nav.PathOk"), FString::Printf(TEXT("입구→%s 경로 길이 %.0f cm, 점 %d개"), *TargetName, Path->GetPathLength(), Path->PathPoints.Num()), Start, Slot->DungeonId);
	Report.Score = 100.0f;
	Report.bPassed = true;
	return Report;
}

int32 UTDWorldGenEditorLibrary::RegenerateAllPcg(UObject* WorldContextObject)
{
	UWorld* World = ResolveWorld(WorldContextObject);
	if (!World)
	{
		return 0;
	}
	int32 Count = 0;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		TArray<UPCGComponent*> Components;
		It->GetComponents<UPCGComponent>(Components);
		for (UPCGComponent* Component : Components)
		{
			Component->Generate(true);
			++Count;
		}
	}
	UE_LOG(LogTDWorldGenEditor, Display, TEXT("RegenerateAllPcg: %d components"), Count);
	return Count;
}

void UTDWorldGenEditorLibrary::CollectLockedPlacements(UWorld* World, TArray<FTDWorldAnchor>& OutAnchors)
{
	if (!World)
	{
		return;
	}
	const FString Prefix = FTDWorldBaker::GetLabelPrefix();
	const FString PoiPrefix = Prefix + TEXT("Poi_");
	const FString EntrancePrefix = Prefix + TEXT("Entrance_");
	const FName LockedTag = FTDWorldBaker::GetLockedTagName();
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (!It->Tags.Contains(LockedTag))
		{
			continue;
		}
		const FString Label = It->GetActorLabel();
		if (!Label.StartsWith(PoiPrefix) && !Label.StartsWith(EntrancePrefix))
		{
			continue;
		}
		FTDWorldAnchor& Anchor = OutAnchors.AddDefaulted_GetRef();
		Anchor.AnchorId = FName(*Label.Mid(Prefix.Len()));
		Anchor.Kind = ETDWorldAnchorKind::Landmark;
		Anchor.LocationCm = It->GetActorLocation();
		Anchor.ExclusionRadiusCm = 2000.0f;
		Anchor.bLocked = true;
	}
}

#undef LOCTEXT_NAMESPACE

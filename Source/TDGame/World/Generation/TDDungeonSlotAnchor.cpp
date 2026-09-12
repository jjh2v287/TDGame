#include "World/Generation/TDDungeonSlotAnchor.h"

#include "Components/SceneComponent.h"
#include "Dungeon/TDDungeonDefinitions.h"
#include "TDWorldGenEditorBridge.h"

DEFINE_LOG_CATEGORY_STATIC(LogTDDungeonSlotAnchor, Log, All);

ATDDungeonSlotAnchor::ATDDungeonSlotAnchor()
{
	PrimaryActorTick.bCanEverTick = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	Tags.Add(TEXT("TDDungeonSlotAnchor"));
}

#if WITH_EDITOR
bool ATDDungeonSlotAnchor::BeginEditorAction(const TCHAR* ActionName, UTDDungeonAtlasDefinition*& OutAtlas)
{
	Modify();
	OutAtlas = Atlas.LoadSynchronous();
	if (!FTDWorldGenEditorBridge::Get())
	{
		LastReport = FString::Printf(TEXT("%s: 에디터 브리지가 없습니다 (TDGameEditor 모듈이 로드되지 않음)"), ActionName);
		UE_LOG(LogTDDungeonSlotAnchor, Warning, TEXT("%s"), *LastReport);
		return false;
	}
	if (!OutAtlas)
	{
		LastReport = FString::Printf(TEXT("%s: Atlas 에셋이 비어 있습니다"), ActionName);
		UE_LOG(LogTDDungeonSlotAnchor, Warning, TEXT("%s"), *LastReport);
		return false;
	}
	return true;
}

int32 ATDDungeonSlotAnchor::ResolveSeed() const
{
	if (SeedOverride >= 0)
	{
		return SeedOverride;
	}
	const UTDDungeonAtlasDefinition* LoadedAtlas = Atlas.LoadSynchronous();
	if (!LoadedAtlas)
	{
		return 1;
	}
	const FTDDungeonSlot* Slot = LoadedAtlas->Slots.FindByPredicate([this](const FTDDungeonSlot& Candidate) { return Candidate.SlotIndex == SlotIndex; });
	return Slot ? Slot->Seed : 1;
}

void ATDDungeonSlotAnchor::RunGenerateAndBake(int32 Seed)
{
	UTDDungeonAtlasDefinition* LoadedAtlas = nullptr;
	if (!BeginEditorAction(TEXT("Generate"), LoadedAtlas))
	{
		return;
	}
	LastSeed = Seed;
	FTDWorldGenEditorBridge::Get()->GenerateAndBakeDungeon(GetWorld(), LoadedAtlas, SlotIndex, Seed, LastReport);
}

void ATDDungeonSlotAnchor::Generate()
{
	RunGenerateAndBake(ResolveSeed());
}

void ATDDungeonSlotAnchor::RegenerateUnlocked()
{
	SeedOverride = ResolveSeed() + 1;
	RunGenerateAndBake(SeedOverride);
}

void ATDDungeonSlotAnchor::Validate()
{
	UTDDungeonAtlasDefinition* LoadedAtlas = nullptr;
	if (!BeginEditorAction(TEXT("Validate"), LoadedAtlas))
	{
		return;
	}
	FTDWorldGenEditorBridge::Get()->ValidateDungeonSlot(GetWorld(), LoadedAtlas, SlotIndex, LastReport);
}

void ATDDungeonSlotAnchor::Bake()
{
	RunGenerateAndBake(LastSeed >= 0 ? LastSeed : ResolveSeed());
}
#endif

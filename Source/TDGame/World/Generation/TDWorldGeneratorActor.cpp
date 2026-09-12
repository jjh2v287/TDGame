#include "World/Generation/TDWorldGeneratorActor.h"

#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "PCGComponent.h"
#include "TDWorldGenEditorBridge.h"
#include "World/TDWorldDefinitions.h"

DEFINE_LOG_CATEGORY_STATIC(LogTDWorldGeneratorActor, Log, All);

ATDWorldGeneratorActor::ATDWorldGeneratorActor()
{
	PrimaryActorTick.bCanEverTick = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	Tags.Add(TEXT("TDWorldGenerator"));
}

#if WITH_EDITOR
bool ATDWorldGeneratorActor::BeginEditorAction(const TCHAR* ActionName, UTDWorldDefinition*& OutDefinition)
{
	Modify();
	OutDefinition = WorldDefinition.LoadSynchronous();
	if (!FTDWorldGenEditorBridge::Get())
	{
		LastReport = FString::Printf(TEXT("%s: 에디터 브리지가 없습니다 (TDGameEditor 모듈이 로드되지 않음)"), ActionName);
		UE_LOG(LogTDWorldGeneratorActor, Warning, TEXT("%s"), *LastReport);
		return false;
	}
	if (!OutDefinition)
	{
		LastReport = FString::Printf(TEXT("%s: WorldDefinition 에셋이 비어 있습니다"), ActionName);
		UE_LOG(LogTDWorldGeneratorActor, Warning, TEXT("%s"), *LastReport);
		return false;
	}
	return true;
}

void ATDWorldGeneratorActor::RunGenerate(bool bBake)
{
	UTDWorldDefinition* Definition = nullptr;
	if (!BeginEditorAction(bBake ? TEXT("Bake") : TEXT("GenerateOutdoor"), Definition))
	{
		return;
	}
	FTDWorldGenEditorBridge::Get()->GenerateOutdoor(GetWorld(), Definition, Seed, bBake, LastReport);
}

void ATDWorldGeneratorActor::GenerateOutdoor()
{
	RunGenerate(bBakeAfterGenerate);
}

void ATDWorldGeneratorActor::ValidateOutdoor()
{
	UTDWorldDefinition* Definition = nullptr;
	if (!BeginEditorAction(TEXT("ValidateOutdoor"), Definition))
	{
		return;
	}
	FTDWorldGenEditorBridge::Get()->ValidateOutdoor(GetWorld(), Definition, LastReport);
}

void ATDWorldGeneratorActor::RegeneratePcg()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
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
	Modify();
	LastReport = FString::Printf(TEXT("RegeneratePcg: PCG 컴포넌트 %d개 재생성 요청"), Count);
	UE_LOG(LogTDWorldGeneratorActor, Display, TEXT("%s"), *LastReport);
}

void ATDWorldGeneratorActor::BakeSelected()
{
	RunGenerate(true);
}
#endif

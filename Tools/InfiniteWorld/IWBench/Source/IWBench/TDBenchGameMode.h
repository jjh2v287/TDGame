#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "GameFramework/Actor.h"
#include "GameFramework/GameModeBase.h"
#include "TDBenchGameMode.generated.h"

class UStaticMesh;
class FTDBenchStreamRunner;
struct FStreamableHandle;

namespace UE::Geometry
{
	class FDynamicMesh3;
}

namespace TDBench
{
	struct FTerrainGrid;
	struct FSampleTable;
	enum class ECollisionMode : uint8;
}

UCLASS()
class ATDBenchChunkActor : public AActor
{
	GENERATED_BODY()

public:
	ATDBenchChunkActor();
};

UCLASS()
class ATDBenchGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ATDBenchGameMode();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	using FBenchStep = TFunction<bool()>;

	void QueueTerrainSuite();
	void QueueDiagnosticSteps();
	void QueueIsmSuite();
	void QueueStreamSuite(bool bNavigation);
	void QueueTerrainConfig(int32 SidePoints, double SpacingCm, const FString& Config);
	void QueueRenderOnlySteps(const TSharedRef<UE::Geometry::FDynamicMesh3>& BaseMesh, const FString& Config);
	void QueueCollisionProbeSteps(const TSharedRef<UE::Geometry::FDynamicMesh3>& BaseMesh, const TSharedRef<TDBench::FTerrainGrid>& Grid, const FString& Config, TDBench::ECollisionMode Mode);
	void QueueEditRecookSteps(const TSharedRef<TDBench::FTerrainGrid>& Grid, const FString& Config, TDBench::ECollisionMode Mode);
	void QueueAsyncBurstSteps(const TSharedRef<UE::Geometry::FDynamicMesh3>& BaseMesh, const TSharedRef<TDBench::FTerrainGrid>& Grid, const FString& Config, int32 ChunkCount);
	void QueuePrecisionSteps(const TSharedRef<TDBench::FTerrainGrid>& Grid, const FString& Config);
	void QueueIsmConfig(bool bHierarchical, int32 InstanceCount, bool bCollision);
	void AddSampleResult(const FString& Name, const FString& Config, const TArray<double>& Samples, const FString& Unit);
	void AddRecord(const TSharedRef<FJsonObject>& Record);
	void EmitTable(const FString& Prefix, const FString& Config, const TDBench::FSampleTable& Table);
	void FinishSuite(const FString& Status);
	ATDBenchChunkActor* SpawnChunkActor(const FVector& Location) const;

	UPROPERTY()
	TObjectPtr<UStaticMesh> CubeMesh;

	TSharedPtr<FStreamableHandle> CubeLoadHandle;
	TSharedPtr<FTDBenchStreamRunner> StreamRunner;
	TArray<FBenchStep> Steps;
	int32 StepIndex = 0;
	TArray<TSharedPtr<FJsonValue>> Results;
	FString SuiteName;
	FString OutputPath;
	int32 Repetitions = 20;
	int32 WarmupRepetitions = 2;
	double SuiteStartSeconds = 0.0;
	double TimeLimitSeconds = 540.0;
	int64 FrameCounter = 0;
	int32 SkipDdcThresholdApplied = 0;
	bool bFinished = false;
};

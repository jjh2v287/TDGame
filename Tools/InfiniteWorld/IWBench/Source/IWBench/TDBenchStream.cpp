#include "TDBenchStream.h"

#include "TDBenchGameMode.h"

#include "AI/NavDataGenerator.h"
#include "AI/NavigationSystemBase.h"
#include "Components/BoxComponent.h"
#include "Components/DynamicMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "NavigationData.h"
#include "NavigationSystem.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "NavMesh/RecastNavMesh.h"
#include "Tasks/Task.h"

DEFINE_LOG_CATEGORY_STATIC(LogTDBenchStream, Log, All);

ATDBenchMoverActor::ATDBenchMoverActor()
{
	PrimaryActorTick.bCanEverTick = false;
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Movable);
	RootComponent = Root;
}

namespace TDBenchStream
{
	constexpr double ChunkSizeCm = 6400.0;
	constexpr int32 ChunkSidePoints = 65;
	constexpr double ChunkSpacingCm = 100.0;
	constexpr int64 SampleUnitsPerChunk = 128;
	constexpr int64 SampleUnitsPerPoint = 2;
	constexpr int32 InstancesPerChunk = 1024;
	constexpr int32 InstanceBatchCount = 8;
	constexpr int32 InstancesPerBatch = InstancesPerChunk / InstanceBatchCount;
	constexpr int32 InstanceGridSide = 32;
	constexpr double ResidentRadiusCm = 9600.0;
	constexpr double ReleaseRadiusCm = 12800.0;
	constexpr double FrameBudgetMs = 2.0;
	constexpr double PrefillTimeoutSeconds = 30.0;
	constexpr double NavWarmTimeoutSeconds = 20.0;
	constexpr double FarOriginCm = 1.0e8;
	constexpr double CaseLaneSpacingCm = 5.0e5;
	constexpr double NavBoundsHalfExtentCm = 1.1e8;
	constexpr double NavBoundsHalfHeightCm = 1.0e5;
	constexpr double LateBoundsHalfExtentCm = 1.0e6;
	constexpr double PathLookAheadCm = 3000.0;
	constexpr double TraceHalfHeightCm = 100000.0;
	constexpr uint64 HeightSeed = 0x5EED1234ABCD0001ull;
	constexpr uint64 PlacementSeed = 0x0BADC0DE12345678ull;

	double NowMs()
	{
		return FPlatformTime::Seconds() * 1000.0;
	}

	uint64 MixBits(uint64 Value)
	{
		Value += 0x9E3779B97F4A7C15ull;
		Value = (Value ^ (Value >> 30)) * 0xBF58476D1CE4E5B9ull;
		Value = (Value ^ (Value >> 27)) * 0x94D049BB133111EBull;
		return Value ^ (Value >> 31);
	}

	double HashToUnit(uint64 Value)
	{
		return double(MixBits(Value) >> 11) * (1.0 / 9007199254740992.0);
	}

	double LatticeValue(int64 CellX, int64 CellY, uint64 Seed)
	{
		const uint64 Key = Seed ^ (uint64(CellX) * 0x9E3779B97F4A7C15ull) ^ (uint64(CellY) * 0xC2B2AE3D27D4EB4Full);
		return HashToUnit(Key) * 2.0 - 1.0;
	}

	int64 FloorDivide(int64 Value, int64 Divisor)
	{
		const int64 Quotient = Value / Divisor;
		const bool bNeedsAdjust = (Value % Divisor != 0) && (Value < 0);
		return bNeedsAdjust ? Quotient - 1 : Quotient;
	}

	double SmoothStep(double T)
	{
		return T * T * (3.0 - 2.0 * T);
	}

	double ValueNoise(int64 SampleX, int64 SampleY, int64 CellUnits, uint64 Seed)
	{
		const int64 CellX = FloorDivide(SampleX, CellUnits);
		const int64 CellY = FloorDivide(SampleY, CellUnits);
		const double FractionX = SmoothStep(double(SampleX - CellX * CellUnits) / double(CellUnits));
		const double FractionY = SmoothStep(double(SampleY - CellY * CellUnits) / double(CellUnits));
		const double V00 = LatticeValue(CellX, CellY, Seed);
		const double V10 = LatticeValue(CellX + 1, CellY, Seed);
		const double V01 = LatticeValue(CellX, CellY + 1, Seed);
		const double V11 = LatticeValue(CellX + 1, CellY + 1, Seed);
		const double Bottom = V00 + (V10 - V00) * FractionX;
		const double Top = V01 + (V11 - V01) * FractionX;
		return Bottom + (Top - Bottom) * FractionY;
	}

	double SampleHeightMeters(int64 SampleX, int64 SampleY)
	{
		double Height = 0.0;
		double Amplitude = 40.0;
		int64 CellUnits = 1024;
		for (int32 Octave = 0; Octave < 6; ++Octave)
		{
			Height += Amplitude * ValueNoise(SampleX, SampleY, CellUnits, HeightSeed + uint64(Octave) * 0x632BE59BD9B4E019ull);
			Amplitude *= 0.5;
			CellUnits = FMath::Max<int64>(CellUnits / 2, 1);
		}
		return Height;
	}

	int64 FloorToChunk(double ValueCm)
	{
		return static_cast<int64>(FMath::FloorToDouble(ValueCm / ChunkSizeCm));
	}

	FVector2D ChunkOrigin(const FInt64Point& Coord)
	{
		return FVector2D(double(Coord.X) * ChunkSizeCm, double(Coord.Y) * ChunkSizeCm);
	}

	double DistanceToChunk(const FVector2D& Point, const FInt64Point& Coord)
	{
		const FVector2D Min = ChunkOrigin(Coord);
		const double DeltaX = FMath::Max3(Min.X - Point.X, 0.0, Point.X - (Min.X + ChunkSizeCm));
		const double DeltaY = FMath::Max3(Min.Y - Point.Y, 0.0, Point.Y - (Min.Y + ChunkSizeCm));
		return FMath::Sqrt(DeltaX * DeltaX + DeltaY * DeltaY);
	}

	double HeightAt(const TArray<double>& HeightsCm, int32 X, int32 Y)
	{
		return HeightsCm[Y * ChunkSidePoints + X];
	}

	FVector3f GridNormal(const TArray<double>& HeightsCm, int32 X, int32 Y)
	{
		const int32 Last = ChunkSidePoints - 1;
		const int32 LeftX = FMath::Max(X - 1, 0);
		const int32 RightX = FMath::Min(X + 1, Last);
		const int32 DownY = FMath::Max(Y - 1, 0);
		const int32 UpY = FMath::Min(Y + 1, Last);
		const double SlopeX = (HeightAt(HeightsCm, RightX, Y) - HeightAt(HeightsCm, LeftX, Y)) / (double(RightX - LeftX) * ChunkSpacingCm);
		const double SlopeY = (HeightAt(HeightsCm, X, UpY) - HeightAt(HeightsCm, X, DownY)) / (double(UpY - DownY) * ChunkSpacingCm);
		return FVector3f(FVector3d(-SlopeX, -SlopeY, 1.0).GetSafeNormal());
	}

	void BuildMesh(const TArray<double>& HeightsCm, UE::Geometry::FDynamicMesh3& Mesh)
	{
		Mesh.EnableAttributes();
		UE::Geometry::FDynamicMeshUVOverlay* UVOverlay = Mesh.Attributes()->PrimaryUV();
		UE::Geometry::FDynamicMeshNormalOverlay* NormalOverlay = Mesh.Attributes()->PrimaryNormals();
		const int32 Side = ChunkSidePoints;
		const double InverseSegments = 1.0 / double(Side - 1);
		for (int32 Y = 0; Y < Side; ++Y)
		{
			for (int32 X = 0; X < Side; ++X)
			{
				Mesh.AppendVertex(FVector3d(X * ChunkSpacingCm, Y * ChunkSpacingCm, HeightAt(HeightsCm, X, Y)));
				UVOverlay->AppendElement(FVector2f(float(X * InverseSegments), float(Y * InverseSegments)));
				NormalOverlay->AppendElement(GridNormal(HeightsCm, X, Y));
			}
		}
		for (int32 Y = 0; Y < Side - 1; ++Y)
		{
			for (int32 X = 0; X < Side - 1; ++X)
			{
				const int32 V00 = Y * Side + X;
				const int32 V10 = V00 + 1;
				const int32 V01 = V00 + Side;
				const int32 V11 = V01 + 1;
				const UE::Geometry::FIndex3i Lower(V00, V11, V10);
				const UE::Geometry::FIndex3i Upper(V00, V01, V11);
				const int32 LowerId = Mesh.AppendTriangle(Lower);
				UVOverlay->SetTriangle(LowerId, Lower);
				NormalOverlay->SetTriangle(LowerId, Lower);
				const int32 UpperId = Mesh.AppendTriangle(Upper);
				UVOverlay->SetTriangle(UpperId, Upper);
				NormalOverlay->SetTriangle(UpperId, Upper);
			}
		}
	}

	void BuildInstances(const FInt64Point& Coord, const TArray<double>& HeightsCm, TArray<FTransform>& OutTransforms)
	{
		OutTransforms.Reserve(InstancesPerChunk);
		const double CellCm = ChunkSizeCm / InstanceGridSide;
		const uint64 ChunkKey = PlacementSeed ^ (uint64(Coord.X) * 0x9E3779B97F4A7C15ull) ^ (uint64(Coord.Y) * 0xC2B2AE3D27D4EB4Full);
		for (int32 CellY = 0; CellY < InstanceGridSide; ++CellY)
		{
			for (int32 CellX = 0; CellX < InstanceGridSide; ++CellX)
			{
				const uint64 Key = ChunkKey + uint64(CellY * InstanceGridSide + CellX) * 3;
				const double LocalX = (CellX + 0.2 + 0.6 * HashToUnit(Key)) * CellCm;
				const double LocalY = (CellY + 0.2 + 0.6 * HashToUnit(Key + 1)) * CellCm;
				const double Yaw = 360.0 * HashToUnit(Key + 2);
				const int32 GridX = FMath::Clamp(FMath::RoundToInt32(LocalX / ChunkSpacingCm), 0, ChunkSidePoints - 1);
				const int32 GridY = FMath::Clamp(FMath::RoundToInt32(LocalY / ChunkSpacingCm), 0, ChunkSidePoints - 1);
				const FVector Location(LocalX, LocalY, HeightAt(HeightsCm, GridX, GridY) + 40.0);
				OutTransforms.Add(FTransform(FRotator(0.0, Yaw, 0.0), Location, FVector(0.4, 0.4, 0.8)));
			}
		}
	}

	struct FChunkBuild
	{
		TArray<double> HeightsCm;
		UE::Geometry::FDynamicMesh3 Mesh;
		TArray<FTransform> InstanceTransforms;
		double WorkerMs = 0.0;
	};

	TSharedPtr<FChunkBuild> BuildChunk(FInt64Point Coord)
	{
		const double Start = NowMs();
		TSharedPtr<FChunkBuild> Build = MakeShared<FChunkBuild>();
		Build->HeightsCm.SetNumUninitialized(ChunkSidePoints * ChunkSidePoints);
		const int64 BaseX = Coord.X * SampleUnitsPerChunk;
		const int64 BaseY = Coord.Y * SampleUnitsPerChunk;
		for (int32 Y = 0; Y < ChunkSidePoints; ++Y)
		{
			for (int32 X = 0; X < ChunkSidePoints; ++X)
			{
				Build->HeightsCm[Y * ChunkSidePoints + X] = SampleHeightMeters(BaseX + X * SampleUnitsPerPoint, BaseY + Y * SampleUnitsPerPoint) * 100.0;
			}
		}
		BuildMesh(Build->HeightsCm, Build->Mesh);
		BuildInstances(Coord, Build->HeightsCm, Build->InstanceTransforms);
		Build->WorkerMs = NowMs() - Start;
		return Build;
	}

	struct FChunkShell
	{
		TWeakObjectPtr<ATDBenchChunkActor> Actor;
		TWeakObjectPtr<UDynamicMeshComponent> Terrain;
		TArray<TWeakObjectPtr<UInstancedStaticMeshComponent>> Batches;
	};

	enum class EChunkPhase : uint8
	{
		Building,
		Ready,
		Spawning,
		Live
	};

	struct FChunkRecord
	{
		FInt64Point Coord;
		EChunkPhase Phase = EChunkPhase::Building;
		UE::Tasks::TTask<TSharedPtr<FChunkBuild>> Task;
		TSharedPtr<FChunkBuild> Build;
		TSharedPtr<FChunkShell> Shell;
		int32 NextBatch = 0;
		double CenterGroundZ = 0.0;
		double RequestMs = 0.0;
		double BuildDoneMs = -1.0;
		double TerrainMs = -1.0;
		double InstancesMs = -1.0;
		double CollisionMs = -1.0;
		double InRangeMs = -1.0;
		double NavMs = -1.0;
		bool bSeenOutsideNavZone = false;
	};

	struct FCaseSpec
	{
		FString Name;
		FVector2D Direction = FVector2D(1.0, 0.0);
		double SpeedCmPerSecond = 600.0;
		FVector2D Start = FVector2D::ZeroVector;
		double InvokerGenerationCm = 4000.0;
		double InvokerRemovalCm = 6000.0;
	};

	struct FCaseStats
	{
		TArray<double> ManagerMs;
		TArray<double> FrameCpuMs;
		TArray<double> FrameDeltaMs;
		TArray<double> TerrainItemMs;
		TArray<double> BatchItemMs;
		TArray<double> ReleaseItemMs;
		TArray<double> NavUpdateMs;
		TArray<double> WorkerMs;
		TArray<double> RequestToBuildMs;
		TArray<double> RequestToCollisionMs;
		TArray<double> RequestToInstancesMs;
		TArray<double> TilesPerSecond;
		TArray<double> ActiveTiles;
		TArray<double> PathQueryMs;
		TArray<double> NavFromRequestMs;
		TArray<double> NavFromReadyInRangeMs;
		TArray<double> OverheadMs;
		int32 MaxGameThreadQueue = 0;
		int32 MaxWorkerInFlight = 0;
		int32 ChunksRequested = 0;
		int32 TerrainSpawned = 0;
		int32 ChunksReleased = 0;
		int32 UnreadyFrames = 0;
		int32 UnreadyEpisodes = 0;
		double UnreadyMsTotal = 0.0;
		bool bWasUnready = false;
		int32 Frames = 0;
		int32 TilesBuilt = 0;
		int32 TilesBuiltThisSecond = 0;
		int32 MaxRemainingNavTasks = 0;
		int32 NavAlreadyAtRangeEntry = 0;
		TArray<double> NavAppearDistanceCm;
		int32 PathAttempts = 0;
		int32 PathComplete = 0;
		int32 PathPartial = 0;
		int32 PathNoGround = 0;
		int32 PathProjectStartFail = 0;
		int32 PathProjectEndFail = 0;
		int32 PathSearchFail = 0;
		double NavMemoryMaxBytes = 0.0;
		double NavMemoryFinalBytes = 0.0;
		double PrefillSeconds = -1.0;
		int32 PrefillChunks = 0;
		double NavWarmSeconds = -1.0;
		int32 NavWarmTiles = 0;
		bool bPrefillTimedOut = false;
		bool bNavWarmTimedOut = false;
	};

	TSharedRef<FJsonObject> MakeDistribution(TArray<double> Values)
	{
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		const int32 Count = Values.Num();
		Object->SetNumberField(TEXT("n"), Count);
		if (Count == 0)
		{
			return Object;
		}
		Values.Sort();
		double Sum = 0.0;
		for (double Value : Values)
		{
			Sum += Value;
		}
		auto Percentile = [&Values, Count](double Fraction)
		{
			const int32 Index = FMath::Clamp(int32(FMath::CeilToDouble(Fraction * Count)) - 1, 0, Count - 1);
			return Values[Index];
		};
		Object->SetNumberField(TEXT("p50"), Percentile(0.50));
		Object->SetNumberField(TEXT("p95"), Percentile(0.95));
		Object->SetNumberField(TEXT("p99"), Percentile(0.99));
		Object->SetNumberField(TEXT("max"), Values.Last());
		Object->SetNumberField(TEXT("mean"), Sum / Count);
		return Object;
	}

	TArray<FCaseSpec> ParseCases(const FString& CasesText, bool bNavigation)
	{
		TArray<FString> Tokens;
		const FString Source = CasesText.IsEmpty() ? (bNavigation ? FString(TEXT("x6i4000,x6i6000,f6i4000")) : FString(TEXT("x6,d6,x15,d15,x50,d50,f6"))) : CasesText;
		Source.ParseIntoArray(Tokens, TEXT(","), true);
		TArray<FCaseSpec> Cases;
		for (int32 Index = 0; Index < Tokens.Num(); ++Index)
		{
			const FString& Token = Tokens[Index];
			if (Token.Len() < 2)
			{
				continue;
			}
			FCaseSpec Spec;
			Spec.Name = Token;
			const TCHAR Kind = Token[0];
			FString Rest = Token.Mid(1);
			FString SpeedText = Rest;
			FString InvokerText;
			Rest.Split(TEXT("i"), &SpeedText, &InvokerText);
			Spec.SpeedCmPerSecond = FCString::Atod(*SpeedText) * 100.0;
			if (!InvokerText.IsEmpty())
			{
				Spec.InvokerGenerationCm = FCString::Atod(*InvokerText);
				Spec.InvokerRemovalCm = Spec.InvokerGenerationCm + 2000.0;
			}
			Spec.Direction = (Kind == TEXT('d')) ? FVector2D(1.0, 1.0).GetSafeNormal() : FVector2D(1.0, 0.0);
			const double LaneY = double(Index + 1) * CaseLaneSpacingCm;
			Spec.Start = (Kind == TEXT('f')) ? FVector2D(FarOriginCm, LaneY) : FVector2D(0.0, LaneY);
			Cases.Add(Spec);
		}
		return Cases;
	}

	bool TraceGround(UWorld* World, const FVector2D& Point, double BaseZ, FHitResult& OutHit)
	{
		const FCollisionQueryParams Params(FName(TEXT("TDBenchStreamTrace")), false);
		const FVector Start(Point.X, Point.Y, BaseZ + TraceHalfHeightCm);
		const FVector End(Point.X, Point.Y, BaseZ - TraceHalfHeightCm);
		return World->LineTraceSingleByChannel(OutHit, Start, End, ECC_Visibility, Params);
	}
}

using namespace TDBenchStream;

class FTDBenchStreamRunner
{
public:
	FTDBenchStreamRunner(UWorld* InWorld, UStaticMesh* InCube, bool bInNavigation, const FString& CasesText, double InCaseSeconds)
		: World(InWorld)
		, CubeMesh(InCube)
		, bNavigation(bInNavigation)
		, CaseSeconds(InCaseSeconds)
	{
		Cases = ParseCases(CasesText, bNavigation);
		FParse::Bool(FCommandLine::Get(), TEXT("TDBenchIsmCollision="), bIsmCollision);
		FParse::Bool(FCommandLine::Get(), TEXT("TDBenchTerrainCollision="), bTerrainCollision);
		FParse::Value(FCommandLine::Get(), TEXT("TDBenchBatches="), ActiveBatchCount);
		ActiveBatchCount = FMath::Clamp(ActiveBatchCount, 0, InstanceBatchCount);
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Mover = World->SpawnActor<ATDBenchMoverActor>(ATDBenchMoverActor::StaticClass(), FTransform::Identity, Params);
		Stage = bNavigation ? EStage::NavSetup : EStage::CaseBegin;
		UE_LOG(LogTDBenchStream, Display, TEXT("TDBENCH stream runner nav=%d cases=%d caseSeconds=%.1f"), bNavigation ? 1 : 0, Cases.Num(), CaseSeconds);
	}

	bool Tick()
	{
		switch (Stage)
		{
		case EStage::NavSetup:
			TickNavSetup();
			return false;
		case EStage::NavWaitData:
			TickNavWaitData();
			return false;
		case EStage::CaseBegin:
			BeginCase();
			return false;
		case EStage::CasePrefill:
			TickPrefill();
			return false;
		case EStage::CaseNavWarm:
			TickNavWarm();
			return false;
		case EStage::CaseMove:
			TickMove();
			return false;
		case EStage::LateBounds:
			TickLateBounds();
			return false;
		case EStage::Done:
			return true;
		}
		return true;
	}

	TArray<TSharedRef<FJsonObject>> Records;
	int32 ConsumedRecords = 0;

private:
	enum class EStage : uint8
	{
		NavSetup,
		NavWaitData,
		CaseBegin,
		CasePrefill,
		CaseNavWarm,
		CaseMove,
		LateBounds,
		Done
	};

	ANavMeshBoundsVolume* SpawnBoundsVolume(const FVector& Center, double HalfExtentCm)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ANavMeshBoundsVolume* Volume = World->SpawnActor<ANavMeshBoundsVolume>(ANavMeshBoundsVolume::StaticClass(), FTransform(Center), Params);
		if (!Volume)
		{
			return nullptr;
		}
		BrushOnlyBox = Volume->GetComponentsBoundingBox(true);
		UBoxComponent* Box = NewObject<UBoxComponent>(Volume);
		Box->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Box->SetCanEverAffectNavigation(false);
		Box->SetBoxExtent(FVector(HalfExtentCm, HalfExtentCm, NavBoundsHalfHeightCm), false);
		Box->SetupAttachment(Volume->GetRootComponent());
		Box->RegisterComponent();
		if (UNavigationSystemV1* System = NavSystem.Get())
		{
			System->OnNavigationBoundsUpdated(Volume);
		}
		return Volume;
	}

	void TickNavSetup()
	{
		NavSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		TSharedRef<FJsonObject> Record = MakeShared<FJsonObject>();
		Record->SetStringField(TEXT("name"), TEXT("nav.setup"));
		Record->SetBoolField(TEXT("nav_system_present"), NavSystem.IsValid());
		if (!NavSystem.IsValid())
		{
			Records.Add(Record);
			Stage = EStage::CaseBegin;
			bNavigation = false;
			return;
		}
		const double Start = NowMs();
		ANavMeshBoundsVolume* Volume = SpawnBoundsVolume(FVector::ZeroVector, NavBoundsHalfExtentCm);
		const double SpawnMs = NowMs() - Start;
		const FBox VolumeBox = Volume ? Volume->GetComponentsBoundingBox(true) : FBox(ForceInit);
		Record->SetNumberField(TEXT("bounds_spawn_ms"), SpawnMs);
		Record->SetStringField(TEXT("volume_components_box"), VolumeBox.ToString());
		Record->SetStringField(TEXT("volume_brush_only_box"), BrushOnlyBox.ToString());
		Record->SetNumberField(TEXT("active_tiles_generation"), NavSystem->IsActiveTilesGenerationEnabled() ? 1 : 0);
		SetupRecord = Record;
		NavSetupStartMs = NowMs();
		Stage = EStage::NavWaitData;
		UE_LOG(LogTDBenchStream, Display, TEXT("TDBENCH nav bounds spawned box=%s spawnMs=%.3f"), *VolumeBox.ToString(), SpawnMs);
	}

	void TickNavWaitData()
	{
		const double FrameCpuMs = (FApp::GetDeltaTime() - FApp::GetIdleTime()) * 1000.0;
		NavSetupMaxFrameMs = FMath::Max(NavSetupMaxFrameMs, FrameCpuMs);
		const double ElapsedSeconds = (NowMs() - NavSetupStartMs) / 1000.0;
		if (!NavMesh.IsValid())
		{
			NavMesh = Cast<ARecastNavMesh>(NavSystem->GetDefaultNavDataInstance(FNavigationSystem::DontCreate));
			if (NavMesh.IsValid())
			{
				NavDataSeconds = ElapsedSeconds;
			}
		}
		const bool bSettled = NavMesh.IsValid() && ElapsedSeconds > NavDataSeconds + 1.5;
		if (!bSettled && ElapsedSeconds < 10.0)
		{
			return;
		}
		TSharedRef<FJsonObject> Record = SetupRecord.ToSharedRef();
		Record->SetBoolField(TEXT("navmesh_present"), NavMesh.IsValid());
		Record->SetNumberField(TEXT("navmesh_spawn_s"), NavDataSeconds);
		Record->SetNumberField(TEXT("setup_max_frame_cpu_ms"), NavSetupMaxFrameMs);
		FBox RegisteredBox(ForceInit);
		for (const FNavigationBounds& Bounds : NavSystem->GetNavigationBounds())
		{
			RegisteredBox += Bounds.AreaBox;
		}
		Record->SetNumberField(TEXT("registered_bounds_count"), NavSystem->GetNavigationBounds().Num());
		Record->SetStringField(TEXT("registered_bounds_box"), RegisteredBox.ToString());
		if (NavMesh.IsValid())
		{
			TileSizeUU = NavMesh->GetTileSizeUU();
			Record->SetNumberField(TEXT("tile_size_uu"), TileSizeUU);
			Record->SetNumberField(TEXT("fixed_tile_pool"), NavMesh->bFixedTilePoolSize ? 1 : 0);
			Record->SetNumberField(TEXT("tile_pool_size"), NavMesh->TilePoolSize);
			Record->SetNumberField(TEXT("runtime_generation"), int32(NavMesh->GetRuntimeGenerationMode()));
			Record->SetNumberField(TEXT("active_tiles_at_setup"), NavMesh->GetNumActiveTiles());
		}
		Records.Add(Record);
		UE_LOG(LogTDBenchStream, Display, TEXT("TDBENCH nav setup navmesh=%d tile=%.0f bounds=%s maxFrame=%.2f"), NavMesh.IsValid() ? 1 : 0, TileSizeUU, *RegisteredBox.ToString(), NavSetupMaxFrameMs);
		if (!NavMesh.IsValid())
		{
			bNavigation = false;
		}
		Stage = EStage::CaseBegin;
	}

	void BeginCase()
	{
		if (CaseIndex >= Cases.Num() && bNavigation && !bLateBoundsDone)
		{
			LateBoundsEnterMs = NowMs();
			Stage = EStage::LateBounds;
			return;
		}
		if (bInvokerRegistered && Mover.IsValid())
		{
			UNavigationSystemV1::UnregisterNavigationInvoker(*Mover);
			bInvokerRegistered = false;
		}
		DrainAll();
		if (CaseIndex >= Cases.Num())
		{
			Stage = EStage::Done;
			return;
		}
		Stats = FCaseStats();
		SeenTiles.Reset();
		const FCaseSpec& Spec = Cases[CaseIndex];
		Mover->SetActorLocation(FVector(Spec.Start.X, Spec.Start.Y, 0.0));
		PhaseStartMs = NowMs();
		Stage = EStage::CasePrefill;
		UE_LOG(LogTDBenchStream, Display, TEXT("TDBENCH case begin %s speed=%.0f start=(%.0f,%.0f)"), *Spec.Name, Spec.SpeedCmPerSecond, Spec.Start.X, Spec.Start.Y);
	}

	bool AreResidentChunksReady(const FVector2D& Point) const
	{
		int32 Count = 0;
		const int64 MinX = FloorToChunk(Point.X - ResidentRadiusCm);
		const int64 MaxX = FloorToChunk(Point.X + ResidentRadiusCm);
		const int64 MinY = FloorToChunk(Point.Y - ResidentRadiusCm);
		const int64 MaxY = FloorToChunk(Point.Y + ResidentRadiusCm);
		for (int64 ChunkY = MinY; ChunkY <= MaxY; ++ChunkY)
		{
			for (int64 ChunkX = MinX; ChunkX <= MaxX; ++ChunkX)
			{
				const FInt64Point Coord(ChunkX, ChunkY);
				if (DistanceToChunk(Point, Coord) > ResidentRadiusCm)
				{
					continue;
				}
				const TSharedPtr<FChunkRecord>* Found = Chunks.Find(Coord);
				if (!Found || (*Found)->Phase != EChunkPhase::Live || (*Found)->CollisionMs < 0.0)
				{
					return false;
				}
				++Count;
			}
		}
		return Count > 0;
	}

	void TickPrefill()
	{
		const FCaseSpec& Spec = Cases[CaseIndex];
		UpdateStreaming(Spec.Start, false);
		const double ElapsedSeconds = (NowMs() - PhaseStartMs) / 1000.0;
		const bool bReady = AreResidentChunksReady(Spec.Start);
		if (!bReady && ElapsedSeconds < PrefillTimeoutSeconds)
		{
			return;
		}
		Stats.PrefillSeconds = ElapsedSeconds;
		Stats.PrefillChunks = Chunks.Num();
		Stats.bPrefillTimedOut = !bReady;
		if (bNavigation)
		{
			UNavigationSystemV1::RegisterNavigationInvoker(*Mover, Spec.InvokerGenerationCm, Spec.InvokerRemovalCm);
			bInvokerRegistered = true;
			PhaseStartMs = NowMs();
			Stage = EStage::CaseNavWarm;
			return;
		}
		StartMove();
	}

	void TickNavWarm()
	{
		const FCaseSpec& Spec = Cases[CaseIndex];
		UpdateStreaming(Spec.Start, false);
		ScanTiles(false);
		const double ElapsedSeconds = (NowMs() - PhaseStartMs) / 1000.0;
		const FNavDataGenerator* Generator = NavMesh.IsValid() ? NavMesh->GetGenerator() : nullptr;
		const int32 Remaining = Generator ? Generator->GetNumRemaningBuildTasks() : 0;
		const int32 Active = NavMesh.IsValid() ? NavMesh->GetNumActiveTiles() : 0;
		const bool bIdle = ElapsedSeconds > 2.0 && Remaining == 0 && Active > 0;
		if (!bIdle && ElapsedSeconds < NavWarmTimeoutSeconds)
		{
			return;
		}
		Stats.NavWarmSeconds = ElapsedSeconds;
		Stats.NavWarmTiles = Active;
		Stats.bNavWarmTimedOut = !bIdle;
		StartMove();
	}

	void StartMove()
	{
		Stats.TilesBuilt = 0;
		Stats.TilesBuiltThisSecond = 0;
		PhaseStartMs = NowMs();
		NextSecondMs = PhaseStartMs + 1000.0;
		NextScanMs = PhaseStartMs;
		NextMemoryMs = PhaseStartMs;
		Stage = EStage::CaseMove;
	}

	FVector2D CurrentMoverPoint() const
	{
		const FCaseSpec& Spec = Cases[CaseIndex];
		const double ElapsedSeconds = (NowMs() - PhaseStartMs) / 1000.0;
		return Spec.Start + Spec.Direction * (Spec.SpeedCmPerSecond * ElapsedSeconds);
	}

	void TickMove()
	{
		const FCaseSpec& Spec = Cases[CaseIndex];
		const double NowValue = NowMs();
		const double ElapsedSeconds = (NowValue - PhaseStartMs) / 1000.0;
		if (ElapsedSeconds >= CaseSeconds)
		{
			FinishCase();
			++CaseIndex;
			Stage = EStage::CaseBegin;
			return;
		}
		const FVector2D Point = CurrentMoverPoint();
		Mover->SetActorLocation(FVector(Point.X, Point.Y, 0.0));
		++Stats.Frames;
		if (Stats.Frames > 2)
		{
			Stats.FrameDeltaMs.Add(FApp::GetDeltaTime() * 1000.0);
			Stats.FrameCpuMs.Add((FApp::GetDeltaTime() - FApp::GetIdleTime()) * 1000.0);
		}
		const double ManagerMs = UpdateStreaming(Point, true);
		Stats.ManagerMs.Add(ManagerMs);

		const double OverheadStart = NowMs();
		FHitResult Hit;
		const bool bGround = TraceGround(World, Point, 0.0, Hit) && Cast<UDynamicMeshComponent>(Hit.GetComponent()) != nullptr;
		if (!bGround)
		{
			++Stats.UnreadyFrames;
			Stats.UnreadyMsTotal += FApp::GetDeltaTime() * 1000.0;
			if (!Stats.bWasUnready)
			{
				++Stats.UnreadyEpisodes;
			}
		}
		Stats.bWasUnready = !bGround;

		if (bNavigation)
		{
			TickNavigationMeasurements(Spec, Point, bGround ? Hit.ImpactPoint.Z : 0.0, bGround);
		}
		Stats.OverheadMs.Add(NowMs() - OverheadStart);
	}

	void ScanTiles(bool bCount)
	{
		if (!NavMesh.IsValid())
		{
			return;
		}
		TArray<FNavTileRef> Tiles;
		NavMesh->GetAllNavMeshTiles(Tiles);
		for (const FNavTileRef& Tile : Tiles)
		{
			bool bAlreadySeen = false;
			SeenTiles.Add(uint64(Tile), &bAlreadySeen);
			if (!bAlreadySeen && bCount)
			{
				++Stats.TilesBuilt;
				++Stats.TilesBuiltThisSecond;
			}
		}
		const FNavDataGenerator* Generator = NavMesh->GetGenerator();
		if (Generator)
		{
			Stats.MaxRemainingNavTasks = FMath::Max(Stats.MaxRemainingNavTasks, Generator->GetNumRemaningBuildTasks());
		}
	}

	void TickNavigationMeasurements(const FCaseSpec& Spec, const FVector2D& Point, double GroundZ, bool bGround)
	{
		const double NowValue = NowMs();
		if (NowValue >= NextScanMs)
		{
			NextScanMs = NowValue + 100.0;
			ScanTiles(true);
			CheckChunkNavigation(Spec, Point);
		}
		if (NowValue >= NextMemoryMs && NavMesh.IsValid())
		{
			NextMemoryMs = NowValue + 5000.0;
			const double Bytes = double(NavMesh->LogMemUsed());
			Stats.NavMemoryMaxBytes = FMath::Max(Stats.NavMemoryMaxBytes, Bytes);
			Stats.NavMemoryFinalBytes = Bytes;
		}
		if (NowValue < NextSecondMs)
		{
			return;
		}
		NextSecondMs += 1000.0;
		Stats.TilesPerSecond.Add(Stats.TilesBuiltThisSecond);
		Stats.TilesBuiltThisSecond = 0;
		if (NavMesh.IsValid())
		{
			Stats.ActiveTiles.Add(NavMesh->GetNumActiveTiles());
		}
		RunPathQuery(Spec, Point, GroundZ, bGround);
	}

	void RunPathQuery(const FCaseSpec& Spec, const FVector2D& Point, double GroundZ, bool bGround)
	{
		++Stats.PathAttempts;
		const double Start = NowMs();
		const FVector2D Target = Point + Spec.Direction * PathLookAheadCm;
		FHitResult TargetHit;
		const bool bTargetGround = TraceGround(World, Target, 0.0, TargetHit);
		if (!bGround || !bTargetGround || !NavMesh.IsValid())
		{
			++Stats.PathNoGround;
			Stats.PathQueryMs.Add(NowMs() - Start);
			return;
		}
		const FVector Extent(150.0, 150.0, 300.0);
		FNavLocation StartLocation;
		FNavLocation EndLocation;
		if (!NavSystem->ProjectPointToNavigation(FVector(Point.X, Point.Y, GroundZ), StartLocation, Extent, NavMesh.Get()))
		{
			++Stats.PathProjectStartFail;
			Stats.PathQueryMs.Add(NowMs() - Start);
			return;
		}
		if (!NavSystem->ProjectPointToNavigation(TargetHit.ImpactPoint, EndLocation, Extent, NavMesh.Get()))
		{
			++Stats.PathProjectEndFail;
			Stats.PathQueryMs.Add(NowMs() - Start);
			return;
		}
		const FPathFindingQuery Query(nullptr, *NavMesh.Get(), StartLocation.Location, EndLocation.Location, NavMesh->GetDefaultQueryFilter());
		const FPathFindingResult Result = NavSystem->FindPathSync(Query);
		Stats.PathQueryMs.Add(NowMs() - Start);
		if (!Result.IsSuccessful())
		{
			++Stats.PathSearchFail;
			return;
		}
		if (Result.IsPartial())
		{
			++Stats.PathPartial;
			return;
		}
		++Stats.PathComplete;
	}

	void CheckChunkNavigation(const FCaseSpec& Spec, const FVector2D& Point)
	{
		const double InRangeRadius = Spec.InvokerGenerationCm - 0.5 * TileSizeUU;
		const double NowValue = NowMs();
		for (const TPair<FInt64Point, TSharedPtr<FChunkRecord>>& Pair : Chunks)
		{
			FChunkRecord& Record = *Pair.Value;
			if (Record.CollisionMs < 0.0 || Record.NavMs >= 0.0)
			{
				continue;
			}
			const FVector2D Center = ChunkOrigin(Record.Coord) + FVector2D(ChunkSizeCm * 0.5, ChunkSizeCm * 0.5);
			const double Distance = FVector2D::Distance(Center, Point);
			if (Distance > Spec.InvokerGenerationCm + TileSizeUU)
			{
				Record.bSeenOutsideNavZone = true;
				continue;
			}
			if (Distance <= InRangeRadius && Record.InRangeMs < 0.0)
			{
				Record.InRangeMs = NowValue;
			}
			FNavLocation Location;
			if (!NavSystem->ProjectPointToNavigation(FVector(Center.X, Center.Y, Record.CenterGroundZ), Location, FVector(300.0, 300.0, 500.0), NavMesh.Get()))
			{
				continue;
			}
			Record.NavMs = NowValue;
			if (Record.bSeenOutsideNavZone)
			{
				Stats.NavAppearDistanceCm.Add(Distance);
			}
			Stats.NavFromRequestMs.Add(Record.NavMs - Record.RequestMs);
			if (Record.InRangeMs < 0.0 || Record.InRangeMs == NowValue)
			{
				++Stats.NavAlreadyAtRangeEntry;
				continue;
			}
			Stats.NavFromReadyInRangeMs.Add(Record.NavMs - FMath::Max(Record.CollisionMs, Record.InRangeMs));
		}
	}

	TSharedPtr<FChunkShell> AcquireShell()
	{
		if (Pool.Num() > 0)
		{
			return Pool.Pop(EAllowShrinking::No);
		}
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		TSharedPtr<FChunkShell> Shell = MakeShared<FChunkShell>();
		Shell->Actor = World->SpawnActor<ATDBenchChunkActor>(ATDBenchChunkActor::StaticClass(), FTransform::Identity, Params);
		Shell->Batches.SetNum(InstanceBatchCount);
		++ShellsSpawned;
		return Shell;
	}

	void SpawnTerrain(FChunkRecord& Record)
	{
		Record.Shell = AcquireShell();
		ATDBenchChunkActor* Actor = Record.Shell->Actor.Get();
		const FVector2D Origin = ChunkOrigin(Record.Coord);
		UDynamicMeshComponent* Component = NewObject<UDynamicMeshComponent>(Actor);
		Component->SetupAttachment(Actor->GetRootComponent());
		Component->SetRelativeLocation(FVector(Origin.X, Origin.Y, 0.0));
		if (bTerrainCollision)
		{
			Component->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
			Component->bUseAsyncCooking = true;
			Component->SetComplexAsSimpleCollisionEnabled(true, false);
		}
		else
		{
			Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Component->SetDeferredCollisionUpdatesEnabled(true, false);
		}
		Component->SetCanEverAffectNavigation(bNavigation);
		Component->RegisterComponent();
		Component->SetMesh(MoveTemp(Record.Build->Mesh));
		Record.Shell->Terrain = Component;
		Record.TerrainMs = NowMs();
		Record.Phase = EChunkPhase::Spawning;
		if (!bTerrainCollision)
		{
			Record.CollisionMs = Record.TerrainMs;
		}
		if (ActiveBatchCount == 0)
		{
			Record.Phase = EChunkPhase::Live;
			Record.InstancesMs = Record.TerrainMs;
			Record.Build.Reset();
		}
	}

	void SpawnBatch(FChunkRecord& Record)
	{
		ATDBenchChunkActor* Actor = Record.Shell->Actor.Get();
		const int32 BatchIndex = Record.NextBatch;
		UInstancedStaticMeshComponent* Component = Record.Shell->Batches[BatchIndex].Get();
		if (!Component)
		{
			Component = NewObject<UInstancedStaticMeshComponent>(Actor);
			Component->SetupAttachment(Actor->GetRootComponent());
			Component->SetMobility(EComponentMobility::Static);
			Component->SetStaticMesh(CubeMesh);
			if (bIsmCollision)
			{
				Component->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
				Component->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
			}
			else
			{
				Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			}
			Component->SetCanEverAffectNavigation(bNavigation);
			Record.Shell->Batches[BatchIndex] = Component;
		}
		const FVector2D Origin = ChunkOrigin(Record.Coord);
		Component->SetRelativeLocation(FVector(Origin.X, Origin.Y, 0.0));
		const TArray<FTransform> Slice(Record.Build->InstanceTransforms.GetData() + BatchIndex * InstancesPerBatch, InstancesPerBatch);
		Component->AddInstances(Slice, false, false, false);
		Component->RegisterComponent();
		++Record.NextBatch;
		if (Record.NextBatch < ActiveBatchCount)
		{
			return;
		}
		Record.Phase = EChunkPhase::Live;
		Record.InstancesMs = NowMs();
		Record.Build.Reset();
	}

	void ReleaseShell(const TSharedPtr<FChunkShell>& Shell)
	{
		for (const TWeakObjectPtr<UInstancedStaticMeshComponent>& Batch : Shell->Batches)
		{
			UInstancedStaticMeshComponent* Component = Batch.Get();
			if (!Component)
			{
				continue;
			}
			if (Component->IsRegistered())
			{
				Component->UnregisterComponent();
			}
			Component->ClearInstances();
		}
		if (UDynamicMeshComponent* Terrain = Shell->Terrain.Get())
		{
			Terrain->DestroyComponent();
		}
		Shell->Terrain.Reset();
		Pool.Add(Shell);
	}

	void DrainAll()
	{
		for (const TPair<FInt64Point, TSharedPtr<FChunkRecord>>& Pair : Chunks)
		{
			if (Pair.Value->Shell.IsValid())
			{
				ReleaseShell(Pair.Value->Shell);
			}
		}
		Chunks.Reset();
		for (const TSharedPtr<FChunkShell>& Shell : ReleaseQueue)
		{
			ReleaseShell(Shell);
		}
		ReleaseQueue.Reset();
	}

	double UpdateStreaming(const FVector2D& Point, bool bMeasure)
	{
		const double FrameStartMs = NowMs();
		const int64 MinX = FloorToChunk(Point.X - ResidentRadiusCm);
		const int64 MaxX = FloorToChunk(Point.X + ResidentRadiusCm);
		const int64 MinY = FloorToChunk(Point.Y - ResidentRadiusCm);
		const int64 MaxY = FloorToChunk(Point.Y + ResidentRadiusCm);
		for (int64 ChunkY = MinY; ChunkY <= MaxY; ++ChunkY)
		{
			for (int64 ChunkX = MinX; ChunkX <= MaxX; ++ChunkX)
			{
				const FInt64Point Coord(ChunkX, ChunkY);
				if (Chunks.Contains(Coord) || DistanceToChunk(Point, Coord) > ResidentRadiusCm)
				{
					continue;
				}
				TSharedPtr<FChunkRecord> Record = MakeShared<FChunkRecord>();
				Record->Coord = Coord;
				Record->RequestMs = FrameStartMs;
				Record->Task = UE::Tasks::Launch(UE_SOURCE_LOCATION, [Coord]() { return BuildChunk(Coord); });
				Chunks.Add(Coord, Record);
				if (bMeasure)
				{
					++Stats.ChunksRequested;
				}
			}
		}

		int32 WorkerInFlight = 0;
		int32 GameThreadQueue = ReleaseQueue.Num();
		TArray<FChunkRecord*> WorkList;
		for (auto It = Chunks.CreateIterator(); It; ++It)
		{
			FChunkRecord& Record = *It.Value();
			if (DistanceToChunk(Point, Record.Coord) > ReleaseRadiusCm)
			{
				if (Record.Shell.IsValid())
				{
					ReleaseQueue.Add(Record.Shell);
				}
				It.RemoveCurrent();
				continue;
			}
			if (Record.Phase == EChunkPhase::Building)
			{
				if (!Record.Task.IsCompleted())
				{
					++WorkerInFlight;
					continue;
				}
				Record.Build = Record.Task.GetResult();
				Record.Task = UE::Tasks::TTask<TSharedPtr<FChunkBuild>>();
				Record.BuildDoneMs = FrameStartMs;
				Record.CenterGroundZ = HeightAt(Record.Build->HeightsCm, ChunkSidePoints / 2, ChunkSidePoints / 2);
				Record.Phase = EChunkPhase::Ready;
				if (bMeasure)
				{
					Stats.WorkerMs.Add(Record.Build->WorkerMs);
					Stats.RequestToBuildMs.Add(Record.BuildDoneMs - Record.RequestMs);
				}
			}
			if (Record.Phase == EChunkPhase::Ready)
			{
				GameThreadQueue += 1 + ActiveBatchCount;
				WorkList.Add(&Record);
			}
			else if (Record.Phase == EChunkPhase::Spawning)
			{
				GameThreadQueue += ActiveBatchCount - Record.NextBatch;
				WorkList.Add(&Record);
			}
		}
		if (bMeasure)
		{
			Stats.MaxGameThreadQueue = FMath::Max(Stats.MaxGameThreadQueue, GameThreadQueue);
			Stats.MaxWorkerInFlight = FMath::Max(Stats.MaxWorkerInFlight, WorkerInFlight);
		}

		WorkList.Sort([&Point](const FChunkRecord& Left, const FChunkRecord& Right)
		{
			return DistanceToChunk(Point, Left.Coord) < DistanceToChunk(Point, Right.Coord);
		});
		int32 ItemsDone = 0;
		auto HasBudget = [&ItemsDone, FrameStartMs]()
		{
			return ItemsDone == 0 || NowMs() - FrameStartMs < FrameBudgetMs;
		};
		for (FChunkRecord* Record : WorkList)
		{
			while (Record->Phase != EChunkPhase::Live && HasBudget())
			{
				const double ItemStart = NowMs();
				if (Record->Phase == EChunkPhase::Ready)
				{
					SpawnTerrain(*Record);
					if (bMeasure)
					{
						Stats.TerrainItemMs.Add(NowMs() - ItemStart);
						++Stats.TerrainSpawned;
					}
				}
				else
				{
					SpawnBatch(*Record);
					if (bMeasure)
					{
						Stats.BatchItemMs.Add(NowMs() - ItemStart);
					}
					if (Record->Phase == EChunkPhase::Live && bMeasure)
					{
						Stats.RequestToInstancesMs.Add(Record->InstancesMs - Record->RequestMs);
					}
				}
				++ItemsDone;
			}
		}
		while (ReleaseQueue.Num() > 0 && HasBudget())
		{
			const double ItemStart = NowMs();
			ReleaseShell(ReleaseQueue.Pop(EAllowShrinking::No));
			if (bMeasure)
			{
				Stats.ReleaseItemMs.Add(NowMs() - ItemStart);
				++Stats.ChunksReleased;
			}
			++ItemsDone;
		}

		for (const TPair<FInt64Point, TSharedPtr<FChunkRecord>>& Pair : Chunks)
		{
			FChunkRecord& Record = *Pair.Value;
			if (Record.TerrainMs < 0.0 || Record.CollisionMs >= 0.0 || !Record.Shell.IsValid())
			{
				continue;
			}
			UDynamicMeshComponent* Terrain = Record.Shell->Terrain.Get();
			const FVector2D Center = ChunkOrigin(Record.Coord) + FVector2D(ChunkSizeCm * 0.5, ChunkSizeCm * 0.5);
			FHitResult Hit;
			if (!TraceGround(World, Center, Record.CenterGroundZ, Hit) || Hit.GetComponent() != Terrain)
			{
				continue;
			}
			Record.CollisionMs = NowMs();
			if (bMeasure)
			{
				Stats.RequestToCollisionMs.Add(Record.CollisionMs - Record.RequestMs);
			}
			if (bNavigation && Terrain)
			{
				const double NavStart = NowMs();
				FNavigationSystem::UpdateComponentData(*Terrain);
				if (bMeasure)
				{
					Stats.NavUpdateMs.Add(NowMs() - NavStart);
				}
			}
		}
		if (ItemsDone > 0)
		{
			World->SendAllEndOfFrameUpdates();
		}
		return NowMs() - FrameStartMs;
	}

	void FinishCase()
	{
		const FCaseSpec& Spec = Cases[CaseIndex];
		TSharedRef<FJsonObject> Record = MakeShared<FJsonObject>();
		Record->SetStringField(TEXT("name"), bNavigation ? TEXT("nav.case") : TEXT("stream.case"));
		Record->SetStringField(TEXT("case"), Spec.Name);
		Record->SetNumberField(TEXT("speed_mps"), Spec.SpeedCmPerSecond / 100.0);
		Record->SetStringField(TEXT("direction"), Spec.Direction.ToString());
		Record->SetNumberField(TEXT("start_x_cm"), Spec.Start.X);
		Record->SetNumberField(TEXT("start_y_cm"), Spec.Start.Y);
		Record->SetNumberField(TEXT("duration_s"), CaseSeconds);
		Record->SetNumberField(TEXT("frames"), Stats.Frames);
		Record->SetBoolField(TEXT("ism_collision"), bIsmCollision);
		Record->SetBoolField(TEXT("terrain_collision"), bTerrainCollision);
		Record->SetNumberField(TEXT("instance_batches"), ActiveBatchCount);
		Record->SetNumberField(TEXT("prefill_s"), Stats.PrefillSeconds);
		Record->SetNumberField(TEXT("prefill_chunks"), Stats.PrefillChunks);
		Record->SetBoolField(TEXT("prefill_timed_out"), Stats.bPrefillTimedOut);
		Record->SetObjectField(TEXT("manager_ms"), MakeDistribution(Stats.ManagerMs));
		Record->SetObjectField(TEXT("frame_cpu_ms"), MakeDistribution(Stats.FrameCpuMs));
		Record->SetObjectField(TEXT("frame_delta_ms"), MakeDistribution(Stats.FrameDeltaMs));
		Record->SetObjectField(TEXT("bench_overhead_ms"), MakeDistribution(Stats.OverheadMs));
		Record->SetObjectField(TEXT("item_terrain_ms"), MakeDistribution(Stats.TerrainItemMs));
		Record->SetObjectField(TEXT("item_batch128_ms"), MakeDistribution(Stats.BatchItemMs));
		Record->SetObjectField(TEXT("item_release_ms"), MakeDistribution(Stats.ReleaseItemMs));
		Record->SetObjectField(TEXT("worker_build_ms"), MakeDistribution(Stats.WorkerMs));
		Record->SetObjectField(TEXT("latency_request_to_build_ms"), MakeDistribution(Stats.RequestToBuildMs));
		Record->SetObjectField(TEXT("latency_request_to_collision_ms"), MakeDistribution(Stats.RequestToCollisionMs));
		Record->SetObjectField(TEXT("latency_request_to_instances_ms"), MakeDistribution(Stats.RequestToInstancesMs));
		Record->SetNumberField(TEXT("max_game_thread_queue_items"), Stats.MaxGameThreadQueue);
		Record->SetNumberField(TEXT("max_worker_in_flight"), Stats.MaxWorkerInFlight);
		Record->SetNumberField(TEXT("chunks_requested"), Stats.ChunksRequested);
		Record->SetNumberField(TEXT("terrain_spawned"), Stats.TerrainSpawned);
		Record->SetNumberField(TEXT("chunks_released"), Stats.ChunksReleased);
		Record->SetNumberField(TEXT("shells_spawned_total"), ShellsSpawned);
		Record->SetNumberField(TEXT("unready_frames"), Stats.UnreadyFrames);
		Record->SetNumberField(TEXT("unready_episodes"), Stats.UnreadyEpisodes);
		Record->SetNumberField(TEXT("unready_ms_total"), Stats.UnreadyMsTotal);
		if (bNavigation)
		{
			Record->SetNumberField(TEXT("tile_size_uu"), TileSizeUU);
			Record->SetNumberField(TEXT("invoker_generation_cm"), Spec.InvokerGenerationCm);
			Record->SetNumberField(TEXT("invoker_removal_cm"), Spec.InvokerRemovalCm);
			Record->SetNumberField(TEXT("nav_warm_s"), Stats.NavWarmSeconds);
			Record->SetNumberField(TEXT("nav_warm_active_tiles"), Stats.NavWarmTiles);
			Record->SetBoolField(TEXT("nav_warm_timed_out"), Stats.bNavWarmTimedOut);
			Record->SetNumberField(TEXT("tiles_built_during_move"), Stats.TilesBuilt);
			Record->SetObjectField(TEXT("tiles_built_per_s"), MakeDistribution(Stats.TilesPerSecond));
			Record->SetObjectField(TEXT("active_tiles"), MakeDistribution(Stats.ActiveTiles));
			Record->SetNumberField(TEXT("max_remaining_nav_tasks"), Stats.MaxRemainingNavTasks);
			Record->SetNumberField(TEXT("path_attempts"), Stats.PathAttempts);
			Record->SetNumberField(TEXT("path_complete"), Stats.PathComplete);
			Record->SetNumberField(TEXT("path_partial"), Stats.PathPartial);
			Record->SetNumberField(TEXT("path_no_ground"), Stats.PathNoGround);
			Record->SetNumberField(TEXT("path_project_start_fail"), Stats.PathProjectStartFail);
			Record->SetNumberField(TEXT("path_project_end_fail"), Stats.PathProjectEndFail);
			Record->SetNumberField(TEXT("path_search_fail"), Stats.PathSearchFail);
			Record->SetObjectField(TEXT("path_query_ms"), MakeDistribution(Stats.PathQueryMs));
			Record->SetObjectField(TEXT("nav_update_component_ms"), MakeDistribution(Stats.NavUpdateMs));
			Record->SetObjectField(TEXT("latency_request_to_nav_ms"), MakeDistribution(Stats.NavFromRequestMs));
			Record->SetObjectField(TEXT("latency_ready_in_range_to_nav_ms"), MakeDistribution(Stats.NavFromReadyInRangeMs));
			Record->SetNumberField(TEXT("nav_already_at_range_entry"), Stats.NavAlreadyAtRangeEntry);
			Record->SetObjectField(TEXT("nav_appear_distance_cm"), MakeDistribution(Stats.NavAppearDistanceCm));
			Record->SetNumberField(TEXT("in_range_radius_cm"), Spec.InvokerGenerationCm - 0.5 * TileSizeUU);
			Record->SetNumberField(TEXT("nav_memory_max_bytes"), Stats.NavMemoryMaxBytes);
			Record->SetNumberField(TEXT("nav_memory_final_bytes"), Stats.NavMemoryFinalBytes);
		}
		Records.Add(Record);
		const TSharedPtr<FJsonObject> Manager = Record->GetObjectField(TEXT("manager_ms"));
		const TSharedPtr<FJsonObject> Frame = Record->GetObjectField(TEXT("frame_cpu_ms"));
		UE_LOG(LogTDBenchStream, Display, TEXT("TDBENCH case done %s frames=%d manager p50=%.3f p99=%.3f max=%.3f frameCpu p50=%.3f p99=%.3f unready=%d tiles=%d path=%d/%d"),
			*Spec.Name, Stats.Frames, Manager->GetNumberField(TEXT("p50")), Manager->GetNumberField(TEXT("p99")), Manager->GetNumberField(TEXT("max")),
			Frame->GetNumberField(TEXT("p50")), Frame->GetNumberField(TEXT("p99")), Stats.UnreadyFrames, Stats.TilesBuilt, Stats.PathComplete, Stats.PathAttempts);
	}

	void TickLateBounds()
	{
		const double FrameCpuMs = (FApp::GetDeltaTime() - FApp::GetIdleTime()) * 1000.0;
		if (LateBoundsStartMs < 0.0)
		{
			LateBoundsBaselineMaxMs = FMath::Max(LateBoundsBaselineMaxMs, FrameCpuMs);
			if (NowMs() - LateBoundsEnterMs < 1500.0)
			{
				return;
			}
			const FVector Center = Mover->GetActorLocation();
			LateBoundsActiveTiles = NavMesh.IsValid() ? NavMesh->GetNumActiveTiles() : -1;
			LateBoundsStartMs = NowMs();
			SpawnBoundsVolume(Center, LateBoundsHalfExtentCm);
			LateBoundsSpawnMs = NowMs() - LateBoundsStartMs;
			return;
		}
		LateBoundsMaxFrameMs = FMath::Max(LateBoundsMaxFrameMs, FrameCpuMs);
		LateBoundsMaxDeltaMs = FMath::Max(LateBoundsMaxDeltaMs, FApp::GetDeltaTime() * 1000.0);
		if (NowMs() - LateBoundsStartMs < 4000.0)
		{
			return;
		}
		TSharedRef<FJsonObject> Record = MakeShared<FJsonObject>();
		Record->SetStringField(TEXT("name"), TEXT("nav.late_bounds"));
		Record->SetNumberField(TEXT("half_extent_cm"), LateBoundsHalfExtentCm);
		Record->SetNumberField(TEXT("tile_size_uu"), TileSizeUU);
		const double TilesPerSide = 2.0 * LateBoundsHalfExtentCm / FMath::Max(TileSizeUU, 1.0);
		Record->SetNumberField(TEXT("tiles_in_box"), TilesPerSide * TilesPerSide);
		Record->SetNumberField(TEXT("spawn_ms"), LateBoundsSpawnMs);
		Record->SetNumberField(TEXT("max_frame_cpu_ms"), LateBoundsMaxFrameMs);
		Record->SetNumberField(TEXT("max_frame_delta_ms"), LateBoundsMaxDeltaMs);
		Record->SetNumberField(TEXT("baseline_max_frame_cpu_ms"), LateBoundsBaselineMaxMs);
		Record->SetNumberField(TEXT("active_tiles_at_spawn"), LateBoundsActiveTiles);
		Records.Add(Record);
		UE_LOG(LogTDBenchStream, Display, TEXT("TDBENCH late bounds tiles=%.0f maxFrameCpu=%.2f maxDelta=%.2f"), TilesPerSide * TilesPerSide, LateBoundsMaxFrameMs, LateBoundsMaxDeltaMs);
		bLateBoundsDone = true;
		if (bInvokerRegistered && Mover.IsValid())
		{
			UNavigationSystemV1::UnregisterNavigationInvoker(*Mover);
			bInvokerRegistered = false;
		}
		DrainAll();
		Stage = EStage::Done;
	}

	UWorld* World = nullptr;
	UStaticMesh* CubeMesh = nullptr;
	bool bNavigation = false;
	bool bIsmCollision = true;
	bool bTerrainCollision = true;
	int32 ActiveBatchCount = InstanceBatchCount;
	double CaseSeconds = 60.0;
	TArray<FCaseSpec> Cases;
	int32 CaseIndex = 0;
	EStage Stage = EStage::CaseBegin;
	TWeakObjectPtr<ATDBenchMoverActor> Mover;
	TMap<FInt64Point, TSharedPtr<FChunkRecord>> Chunks;
	TArray<TSharedPtr<FChunkShell>> Pool;
	TArray<TSharedPtr<FChunkShell>> ReleaseQueue;
	int32 ShellsSpawned = 0;
	FCaseStats Stats;
	double PhaseStartMs = 0.0;
	double NextSecondMs = 0.0;
	double NextScanMs = 0.0;
	double NextMemoryMs = 0.0;
	TWeakObjectPtr<UNavigationSystemV1> NavSystem;
	TWeakObjectPtr<ARecastNavMesh> NavMesh;
	TSharedPtr<FJsonObject> SetupRecord;
	double NavSetupStartMs = 0.0;
	double NavSetupMaxFrameMs = 0.0;
	double NavDataSeconds = -1.0;
	double TileSizeUU = 0.0;
	bool bInvokerRegistered = false;
	TSet<uint64> SeenTiles;
	bool bLateBoundsDone = false;
	double LateBoundsStartMs = -1.0;
	double LateBoundsSpawnMs = 0.0;
	double LateBoundsMaxFrameMs = 0.0;
	double LateBoundsMaxDeltaMs = 0.0;
	double LateBoundsEnterMs = 0.0;
	double LateBoundsBaselineMaxMs = 0.0;
	int32 LateBoundsActiveTiles = 0;
	FBox BrushOnlyBox = FBox(ForceInit);
};

void ATDBenchGameMode::QueueStreamSuite(bool bNavigation)
{
	FString CasesText;
	FParse::Value(FCommandLine::Get(), TEXT("TDBenchCases="), CasesText, false);
	double CaseSeconds = 60.0;
	FParse::Value(FCommandLine::Get(), TEXT("TDBenchCaseSeconds="), CaseSeconds);
	int32 MaxFps = 60;
	FParse::Value(FCommandLine::Get(), TEXT("TDBenchMaxFps="), MaxFps);
	FParse::Value(FCommandLine::Get(), TEXT("TDBenchTimeLimit="), TimeLimitSeconds);
	if (IConsoleVariable* MaxFpsVariable = IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS")))
	{
		MaxFpsVariable->Set(float(MaxFps), ECVF_SetByCode);
	}
	UE_LOG(LogTDBenchStream, Display, TEXT("TDBENCH stream suite nav=%d cases=%s maxFps=%d"), bNavigation ? 1 : 0, *CasesText, MaxFps);
	Steps.Add([this, bNavigation, CasesText, CaseSeconds]() -> bool
	{
		if (!StreamRunner.IsValid())
		{
			StreamRunner = MakeShared<FTDBenchStreamRunner>(GetWorld(), CubeMesh, bNavigation, CasesText, CaseSeconds);
		}
		const bool bDone = StreamRunner->Tick();
		while (StreamRunner->ConsumedRecords < StreamRunner->Records.Num())
		{
			AddRecord(StreamRunner->Records[StreamRunner->ConsumedRecords++]);
		}
		return bDone;
	});
}

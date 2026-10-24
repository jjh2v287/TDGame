#include "TDBenchGameMode.h"

#include "Async/ParallelFor.h"
#include "Async/TaskGraphInterfaces.h"
#include "Chaos/HeightField.h"
#include "Chaos/Particles.h"
#include "Chaos/TriangleMeshImplicitObject.h"
#include "Components/DynamicMeshComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonValue.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Engine/AssetManager.h"
#include "Engine/CollisionProfile.h"
#include "PhysicsEngine/BodySetup.h"
#include "Engine/StaticMesh.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

DEFINE_LOG_CATEGORY_STATIC(LogTDBench, Log, All);

using UE::Geometry::FDynamicMesh3;
using UE::Geometry::FIndex3i;

namespace TDBench
{
	constexpr int64 SampleUnitsPerMeter = 2;
	constexpr int64 ChunkOriginSampleX = 12345 * 128;
	constexpr int64 ChunkOriginSampleY = -6789 * 128;
	constexpr uint64 HeightSeed = 0x5EED1234ABCD0001ull;
	constexpr double TraceTopCm = 100000.0;
	constexpr double TraceBottomCm = -100000.0;

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

	struct FTerrainGrid
	{
		int32 SidePoints = 0;
		double SpacingCm = 0.0;
		TArray<double> HeightsCm;

		double GetHeightCm(int32 X, int32 Y) const
		{
			return HeightsCm[Y * SidePoints + X];
		}

		double GetSizeCm() const
		{
			return double(SidePoints - 1) * SpacingCm;
		}
	};

	FTerrainGrid GenerateGrid(int32 SidePoints, double SpacingCm)
	{
		FTerrainGrid Grid;
		Grid.SidePoints = SidePoints;
		Grid.SpacingCm = SpacingCm;
		Grid.HeightsCm.SetNumUninitialized(SidePoints * SidePoints);
		const int64 StepUnits = FMath::RoundToInt64(SpacingCm / 100.0 * double(SampleUnitsPerMeter));
		for (int32 Y = 0; Y < SidePoints; ++Y)
		{
			for (int32 X = 0; X < SidePoints; ++X)
			{
				const double HeightMeters = SampleHeightMeters(ChunkOriginSampleX + X * StepUnits, ChunkOriginSampleY + Y * StepUnits);
				Grid.HeightsCm[Y * SidePoints + X] = HeightMeters * 100.0;
			}
		}
		return Grid;
	}

	FVector3f ComputeGridNormal(const FTerrainGrid& Grid, int32 X, int32 Y)
	{
		const int32 Last = Grid.SidePoints - 1;
		const int32 LeftX = FMath::Max(X - 1, 0);
		const int32 RightX = FMath::Min(X + 1, Last);
		const int32 DownY = FMath::Max(Y - 1, 0);
		const int32 UpY = FMath::Min(Y + 1, Last);
		const double SlopeX = (Grid.GetHeightCm(RightX, Y) - Grid.GetHeightCm(LeftX, Y)) / (double(RightX - LeftX) * Grid.SpacingCm);
		const double SlopeY = (Grid.GetHeightCm(X, UpY) - Grid.GetHeightCm(X, DownY)) / (double(UpY - DownY) * Grid.SpacingCm);
		return FVector3f(FVector3d(-SlopeX, -SlopeY, 1.0).GetSafeNormal());
	}

	FDynamicMesh3 BuildTerrainMesh(const FTerrainGrid& Grid, const FVector3d& VertexOffset)
	{
		FDynamicMesh3 Mesh;
		Mesh.EnableAttributes();
		UE::Geometry::FDynamicMeshUVOverlay* UVOverlay = Mesh.Attributes()->PrimaryUV();
		UE::Geometry::FDynamicMeshNormalOverlay* NormalOverlay = Mesh.Attributes()->PrimaryNormals();
		const int32 Side = Grid.SidePoints;
		const double InverseSegments = 1.0 / double(Side - 1);
		for (int32 Y = 0; Y < Side; ++Y)
		{
			for (int32 X = 0; X < Side; ++X)
			{
				Mesh.AppendVertex(VertexOffset + FVector3d(X * Grid.SpacingCm, Y * Grid.SpacingCm, Grid.GetHeightCm(X, Y)));
				UVOverlay->AppendElement(FVector2f(float(X * InverseSegments), float(Y * InverseSegments)));
				NormalOverlay->AppendElement(ComputeGridNormal(Grid, X, Y));
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
				const FIndex3i Lower(V00, V11, V10);
				const FIndex3i Upper(V00, V01, V11);
				const int32 LowerId = Mesh.AppendTriangle(Lower);
				UVOverlay->SetTriangle(LowerId, Lower);
				NormalOverlay->SetTriangle(LowerId, Lower);
				const int32 UpperId = Mesh.AppendTriangle(Upper);
				UVOverlay->SetTriangle(UpperId, Upper);
				NormalOverlay->SetTriangle(UpperId, Upper);
			}
		}
		return Mesh;
	}

	double AnalyticHeightCm(const FTerrainGrid& Grid, double LocalX, double LocalY, bool bOtherDiagonal = false, bool bSwapAxes = false)
	{
		const double GridX = (bSwapAxes ? LocalY : LocalX) / Grid.SpacingCm;
		const double GridY = (bSwapAxes ? LocalX : LocalY) / Grid.SpacingCm;
		const int32 CellX = FMath::Clamp(int32(FMath::FloorToDouble(GridX)), 0, Grid.SidePoints - 2);
		const int32 CellY = FMath::Clamp(int32(FMath::FloorToDouble(GridY)), 0, Grid.SidePoints - 2);
		const double FractionX = GridX - CellX;
		const double FractionY = GridY - CellY;
		const double H00 = Grid.GetHeightCm(CellX, CellY);
		const double H10 = Grid.GetHeightCm(CellX + 1, CellY);
		const double H01 = Grid.GetHeightCm(CellX, CellY + 1);
		const double H11 = Grid.GetHeightCm(CellX + 1, CellY + 1);
		if (bOtherDiagonal)
		{
			if (FractionX + FractionY <= 1.0)
			{
				return H00 + FractionX * (H10 - H00) + FractionY * (H01 - H00);
			}
			return H11 + (1.0 - FractionX) * (H01 - H11) + (1.0 - FractionY) * (H10 - H11);
		}
		if (FractionX >= FractionY)
		{
			return H00 + FractionX * (H10 - H00) + FractionY * (H11 - H10);
		}
		return H00 + FractionY * (H01 - H00) + FractionX * (H11 - H01);
	}

	FVector2D TraceSampleLocal(const FTerrainGrid& Grid, int32 SampleIndex)
	{
		const double Size = Grid.GetSizeCm();
		const double U = 0.05 + 0.9 * HashToUnit(uint64(SampleIndex) * 2 + 7001);
		const double V = 0.05 + 0.9 * HashToUnit(uint64(SampleIndex) * 2 + 7002);
		return FVector2D(U * Size, V * Size);
	}

	bool TraceDown(UWorld* World, const UPrimitiveComponent* Target, double WorldX, double WorldY, double BaseZ, FVector& OutImpact)
	{
		FHitResult Hit;
		FCollisionQueryParams Params(FName(TEXT("TDBenchTrace")), true);
		const FVector Start(WorldX, WorldY, BaseZ + TraceTopCm);
		const FVector End(WorldX, WorldY, BaseZ + TraceBottomCm);
		if (!World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
		{
			return false;
		}
		if (Hit.GetComponent() != Target)
		{
			return false;
		}
		OutImpact = Hit.ImpactPoint;
		return true;
	}

	struct FSampleTable
	{
		TArray<FString> Order;
		TMap<FString, TArray<double>> Columns;

		void Add(const FString& Key, double Value)
		{
			if (!Columns.Contains(Key))
			{
				Order.Add(Key);
			}
			Columns.FindOrAdd(Key).Add(Value);
		}
	};

	enum class ECollisionMode : uint8
	{
		None,
		Sync,
		Async
	};

	struct FTerrainSpawnTimes
	{
		double CreateMs = 0.0;
		double SetMeshMs = 0.0;
		double RegisterMs = 0.0;
		double TotalMs = 0.0;
		double EndOfFrameFlushMs = 0.0;
		double EndMs = 0.0;
	};

	UDynamicMeshComponent* CreateTerrainComponent(AActor* Owner, FDynamicMesh3&& Mesh, ECollisionMode Mode, FTerrainSpawnTimes& OutTimes)
	{
		const double Start = NowMs();
		UDynamicMeshComponent* Component = NewObject<UDynamicMeshComponent>(Owner);
		Component->SetupAttachment(Owner->GetRootComponent());
		if (Mode == ECollisionMode::None)
		{
			Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Component->SetDeferredCollisionUpdatesEnabled(true, false);
		}
		else
		{
			Component->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
			Component->bUseAsyncCooking = (Mode == ECollisionMode::Async);
			Component->SetComplexAsSimpleCollisionEnabled(true, false);
		}
		const double Created = NowMs();
		Component->RegisterComponent();
		const double Registered = NowMs();
		Component->SetMesh(MoveTemp(Mesh));
		const double MeshSet = NowMs();
		OutTimes.CreateMs = Created - Start;
		OutTimes.RegisterMs = Registered - Created;
		OutTimes.SetMeshMs = MeshSet - Registered;
		OutTimes.TotalMs = MeshSet - Start;
		Owner->GetWorld()->SendAllEndOfFrameUpdates();
		OutTimes.EndMs = NowMs();
		OutTimes.EndOfFrameFlushMs = OutTimes.EndMs - MeshSet;
		return Component;
	}

	TSharedRef<FJsonObject> MakeStatsObject(TArray<double> Samples)
	{
		TSharedRef<FJsonObject> Stats = MakeShared<FJsonObject>();
		const int32 Count = Samples.Num();
		Stats->SetNumberField(TEXT("n"), Count);
		if (Count == 0)
		{
			return Stats;
		}
		TArray<TSharedPtr<FJsonValue>> RawValues;
		double Sum = 0.0;
		for (double Value : Samples)
		{
			RawValues.Add(MakeShared<FJsonValueNumber>(FMath::RoundToDouble(Value * 10000.0) / 10000.0));
			Sum += Value;
		}
		Samples.Sort();
		const double Median = (Count % 2 == 1) ? Samples[Count / 2] : 0.5 * (Samples[Count / 2 - 1] + Samples[Count / 2]);
		const int32 P95Index = FMath::Clamp(int32(FMath::CeilToDouble(0.95 * Count)) - 1, 0, Count - 1);
		Stats->SetNumberField(TEXT("median"), Median);
		Stats->SetNumberField(TEXT("p95"), Samples[P95Index]);
		Stats->SetNumberField(TEXT("max"), Samples.Last());
		Stats->SetNumberField(TEXT("min"), Samples[0]);
		Stats->SetNumberField(TEXT("mean"), Sum / Count);
		Stats->SetArrayField(TEXT("samples"), RawValues);
		return Stats;
	}

	struct FProbeState
	{
		TWeakObjectPtr<ATDBenchChunkActor> Actor;
		TWeakObjectPtr<UDynamicMeshComponent> Component;
		FTerrainSpawnTimes Times;
		double StartMs = 0.0;
		int64 StartFrame = 0;
		double ReadyMs = -1.0;
		int64 ReadyFrames = -1;
		double NextFrameMs = -1.0;
		bool bStarted = false;
		bool bTimedOut = false;
	};

	struct FEditState
	{
		TWeakObjectPtr<ATDBenchChunkActor> Actor;
		TWeakObjectPtr<UDynamicMeshComponent> Component;
		double StartMs = 0.0;
		double EditMs = 0.0;
		double ExpectedZ = 0.0;
		int64 StartFrame = 0;
		bool bWaiting = false;
		bool bReady = false;
	};

	struct FIsmState
	{
		TWeakObjectPtr<ATDBenchChunkActor> Actor;
		TWeakObjectPtr<UInstancedStaticMeshComponent> Component;
		int32 Phase = 0;
		double CreatedEndMs = 0.0;
		bool bNextFrameRecorded = false;
		double PhaseStartMs = 0.0;
		int64 PhaseStartFrame = 0;
	};
}

ATDBenchChunkActor::ATDBenchChunkActor()
{
	PrimaryActorTick.bCanEverTick = false;
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Static);
	RootComponent = Root;
}

ATDBenchGameMode::ATDBenchGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	bStartPlayersAsSpectators = true;
}

void ATDBenchGameMode::BeginPlay()
{
	Super::BeginPlay();

	FParse::Value(FCommandLine::Get(), TEXT("TDBenchSuite="), SuiteName);
	FParse::Value(FCommandLine::Get(), TEXT("TDBenchOut="), OutputPath);
	FParse::Value(FCommandLine::Get(), TEXT("TDBenchReps="), Repetitions);
	if (SuiteName.IsEmpty())
	{
		UE_LOG(LogTDBench, Display, TEXT("TDBENCH no suite requested"));
		return;
	}
	if (OutputPath.IsEmpty())
	{
		OutputPath = FPaths::ProjectSavedDir() / FString::Printf(TEXT("TDBench_%s.json"), *SuiteName);
	}

	if (IConsoleVariable* SkipDdc = IConsoleManager::Get().FindConsoleVariable(TEXT("p.BodySetupSkipDDCThreshold")))
	{
		SkipDdc->Set(1 << 30, ECVF_SetByCode);
		SkipDdcThresholdApplied = SkipDdc->GetInt();
	}
	if (IConsoleVariable* MaxFps = IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS")))
	{
		MaxFps->Set(0.0f, ECVF_SetByCode);
	}

	SuiteStartSeconds = FPlatformTime::Seconds();
	CubeLoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(FSoftObjectPath(TEXT("/Engine/BasicShapes/Cube.Cube")));
	Steps.Add([this]() -> bool
	{
		if (CubeLoadHandle.IsValid() && !CubeLoadHandle->HasLoadCompleted())
		{
			return false;
		}
		CubeMesh = CubeLoadHandle.IsValid() ? Cast<UStaticMesh>(CubeLoadHandle->GetLoadedAsset()) : nullptr;
		UE_LOG(LogTDBench, Display, TEXT("TDBENCH cube loaded: %s"), CubeMesh ? *CubeMesh->GetPathName() : TEXT("null"));
		return true;
	});

	if (SuiteName == TEXT("terrain"))
	{
		QueueTerrainSuite();
	}
	else if (SuiteName == TEXT("diag"))
	{
		QueueDiagnosticSteps();
	}
	else if (SuiteName == TEXT("ism"))
	{
		QueueIsmSuite();
	}
	else if (SuiteName == TEXT("stream") || SuiteName == TEXT("nav"))
	{
		QueueStreamSuite(SuiteName == TEXT("nav"));
	}
	else
	{
		UE_LOG(LogTDBench, Error, TEXT("TDBENCH unknown suite %s"), *SuiteName);
	}
	UE_LOG(LogTDBench, Display, TEXT("TDBENCH suite=%s steps=%d out=%s"), *SuiteName, Steps.Num(), *OutputPath);
}

void ATDBenchGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (SuiteName.IsEmpty() || bFinished)
	{
		return;
	}
	++FrameCounter;
	if (FPlatformTime::Seconds() - SuiteStartSeconds > TimeLimitSeconds)
	{
		FinishSuite(TEXT("timeout"));
		return;
	}
	if (StepIndex >= Steps.Num())
	{
		FinishSuite(TEXT("complete"));
		return;
	}
	if (Steps[StepIndex]())
	{
		++StepIndex;
	}
}

ATDBenchChunkActor* ATDBenchGameMode::SpawnChunkActor(const FVector& Location) const
{
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	return GetWorld()->SpawnActor<ATDBenchChunkActor>(ATDBenchChunkActor::StaticClass(), FTransform(Location), Params);
}

void ATDBenchGameMode::AddSampleResult(const FString& Name, const FString& Config, const TArray<double>& Samples, const FString& Unit)
{
	TSharedRef<FJsonObject> Record = TDBench::MakeStatsObject(Samples);
	Record->SetStringField(TEXT("name"), Name);
	Record->SetStringField(TEXT("config"), Config);
	Record->SetStringField(TEXT("unit"), Unit);
	UE_LOG(LogTDBench, Display, TEXT("TDBENCH %s [%s] median=%.4f p95=%.4f max=%.4f %s"), *Name, *Config,
		Record->GetNumberField(TEXT("median")), Record->GetNumberField(TEXT("p95")), Record->GetNumberField(TEXT("max")), *Unit);
	AddRecord(Record);
}

void ATDBenchGameMode::AddRecord(const TSharedRef<FJsonObject>& Record)
{
	Results.Add(MakeShared<FJsonValueObject>(Record));
}

void ATDBenchGameMode::EmitTable(const FString& Prefix, const FString& Config, const TDBench::FSampleTable& Table)
{
	for (const FString& Key : Table.Order)
	{
		const FString Unit = Key.EndsWith(TEXT("_ms")) ? TEXT("ms") : (Key.EndsWith(TEXT("_frames")) ? TEXT("frames") : TEXT("count"));
		AddSampleResult(Prefix + TEXT(".") + Key, Config, Table.Columns[Key], Unit);
	}
}

void ATDBenchGameMode::FinishSuite(const FString& Status)
{
	bFinished = true;
	const double TotalSeconds = FPlatformTime::Seconds() - SuiteStartSeconds;
	TSharedRef<FJsonObject> Meta = MakeShared<FJsonObject>();
	Meta->SetStringField(TEXT("engine_version"), FEngineVersion::Current().ToString());
	Meta->SetBoolField(TEXT("can_ever_render"), FApp::CanEverRender());
	Meta->SetBoolField(TEXT("with_editor"), WITH_EDITOR != 0);
	Meta->SetStringField(TEXT("build_configuration"), LexToString(FApp::GetBuildConfiguration()));
	Meta->SetNumberField(TEXT("task_graph_worker_threads"), FTaskGraphInterface::Get().GetNumWorkerThreads());
	Meta->SetNumberField(TEXT("logical_cores"), FPlatformMisc::NumberOfCoresIncludingHyperthreads());
	Meta->SetNumberField(TEXT("skip_ddc_threshold"), SkipDdcThresholdApplied);
	Meta->SetNumberField(TEXT("repetitions"), Repetitions);
	Meta->SetNumberField(TEXT("warmup_repetitions"), WarmupRepetitions);
	Meta->SetNumberField(TEXT("total_frames"), double(FrameCounter));
	Meta->SetNumberField(TEXT("total_seconds"), TotalSeconds);
	Meta->SetNumberField(TEXT("average_frame_ms"), FrameCounter > 0 ? TotalSeconds * 1000.0 / double(FrameCounter) : 0.0);
	Meta->SetNumberField(TEXT("steps_completed"), StepIndex);
	Meta->SetNumberField(TEXT("steps_total"), Steps.Num());
	Meta->SetStringField(TEXT("command_line"), FCommandLine::Get());

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("suite"), SuiteName);
	Root->SetStringField(TEXT("status"), Status);
	Root->SetObjectField(TEXT("meta"), Meta);
	Root->SetArrayField(TEXT("results"), Results);

	FString Json;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
	FJsonSerializer::Serialize(Root, Writer);
	const bool bSaved = FFileHelper::SaveStringToFile(Json, *OutputPath);
	UE_LOG(LogTDBench, Display, TEXT("TDBENCH finished status=%s saved=%d path=%s seconds=%.1f frames=%lld"), *Status, bSaved ? 1 : 0, *OutputPath, TotalSeconds, FrameCounter);
	FPlatformMisc::RequestExit(false, TEXT("TDBench"));
}

void ATDBenchGameMode::QueueDiagnosticSteps()
{
	TSharedRef<TDBench::FTerrainGrid> Grid = MakeShared<TDBench::FTerrainGrid>(TDBench::GenerateGrid(65, 100.0));
	TSharedRef<TDBench::FProbeState> State = MakeShared<TDBench::FProbeState>();
	TSharedRef<int32> FramesLogged = MakeShared<int32>(0);
	Steps.Add([this, Grid, State, FramesLogged]() -> bool
	{
		const FVector Origin(400000.0, 0.0, 0.0);
		if (!State->bStarted)
		{
			ATDBenchChunkActor* Actor = SpawnChunkActor(Origin);
			State->Component = TDBench::CreateTerrainComponent(Actor, TDBench::BuildTerrainMesh(*Grid, FVector3d::ZeroVector), TDBench::ECollisionMode::Sync, State->Times);
			State->Actor = Actor;
			State->bStarted = true;
		}
		UDynamicMeshComponent* Component = State->Component.Get();
		UBodySetup* BodySetup = Component ? Component->GetBodySetup() : nullptr;
		const FBodyInstance* Body = Component ? Component->GetBodyInstance() : nullptr;
		const double X = Origin.X + Grid->GetSizeCm() * 0.5 + 3.0;
		const double Y = Origin.Y + Grid->GetSizeCm() * 0.5 + 7.0;
		const FVector Start(X, Y, 100000.0);
		const FVector End(X, Y, -100000.0);
		FHitResult WorldHit;
		const bool bWorldHit = GetWorld()->LineTraceSingleByChannel(WorldHit, Start, End, ECC_Visibility, FCollisionQueryParams(FName(TEXT("Diag")), true));
		FHitResult ObjectHit;
		const bool bObjectHit = GetWorld()->LineTraceSingleByObjectType(ObjectHit, Start, End, FCollisionObjectQueryParams(FCollisionObjectQueryParams::InitType::AllObjects), FCollisionQueryParams(FName(TEXT("DiagObj")), true));
		FHitResult ComponentHit;
		const bool bComponentHit = Component && Component->LineTraceComponent(ComponentHit, Start, End, FCollisionQueryParams(FName(TEXT("DiagComp")), true));
		UE_LOG(LogTDBench, Display, TEXT("TDBENCH diag frame=%d registered=%d collisionEnabled=%d visResp=%d bodyValid=%d bodySetup=%d trimesh=%d created=%d traceFlag=%d mobility=%d bounds=%s worldHit=%d(%s) objectHit=%d(%s) compHit=%d z=%.2f analytic=%.2f"),
			*FramesLogged,
			Component && Component->IsRegistered() ? 1 : 0,
			Component ? int32(Component->GetCollisionEnabled()) : -1,
			Component ? int32(Component->GetCollisionResponseToChannel(ECC_Visibility)) : -1,
			Body && Body->IsValidBodyInstance() ? 1 : 0,
			BodySetup ? 1 : 0,
			BodySetup ? BodySetup->TriMeshGeometries.Num() : -1,
			BodySetup && BodySetup->bCreatedPhysicsMeshes ? 1 : 0,
			BodySetup ? int32(BodySetup->CollisionTraceFlag) : -1,
			Component ? int32(Component->Mobility) : -1,
			Component ? *Component->Bounds.GetBox().ToString() : TEXT("none"),
			bWorldHit ? 1 : 0, bWorldHit ? *GetNameSafe(WorldHit.GetComponent()) : TEXT("-"),
			bObjectHit ? 1 : 0, bObjectHit ? *GetNameSafe(ObjectHit.GetComponent()) : TEXT("-"),
			bComponentHit ? 1 : 0, bComponentHit ? ComponentHit.ImpactPoint.Z : 0.0,
			TDBench::AnalyticHeightCm(*Grid, X - Origin.X, Y - Origin.Y));
		if (BodySetup && BodySetup->TriMeshGeometries.Num() > 0 && BodySetup->TriMeshGeometries[0].IsValid())
		{
			const Chaos::FTriangleMeshImplicitObject* Trimesh = BodySetup->TriMeshGeometries[0].GetReference();
			const Chaos::FAABB3 LocalBox = Trimesh->BoundingBox();
			UE_LOG(LogTDBench, Display, TEXT("TDBENCH diag trimesh triangles=%d particles=%d boxMin=%s boxMax=%s"), Trimesh->Elements().GetNumTriangles(), int32(Trimesh->Particles().Size()), *LocalBox.Min().ToString(), *LocalBox.Max().ToString());
		}
		const double Analytic = TDBench::AnalyticHeightCm(*Grid, X - Origin.X, Y - Origin.Y);
		FHitResult ShortHit;
		const bool bShort = GetWorld()->LineTraceSingleByChannel(ShortHit, FVector(X, Y, Analytic + 500.0), FVector(X, Y, Analytic - 500.0), ECC_Visibility, FCollisionQueryParams(FName(TEXT("DiagShort")), true));
		FHitResult UpHit;
		const bool bUp = GetWorld()->LineTraceSingleByChannel(UpHit, FVector(X, Y, Analytic - 500.0), FVector(X, Y, Analytic + 500.0), ECC_Visibility, FCollisionQueryParams(FName(TEXT("DiagUp")), true));
		FHitResult SimpleHit;
		const bool bSimple = GetWorld()->LineTraceSingleByChannel(SimpleHit, Start, End, ECC_Visibility, FCollisionQueryParams(FName(TEXT("DiagSimple")), false));
		FHitResult SweepHit;
		const bool bSweep = GetWorld()->SweepSingleByChannel(SweepHit, Start, End, FQuat::Identity, ECC_Visibility, FCollisionShape::MakeSphere(10.0f), FCollisionQueryParams(FName(TEXT("DiagSweep")), true));
		UE_LOG(LogTDBench, Display, TEXT("TDBENCH diag variants short=%d(%.2f) up=%d(%.2f) simpleFlag=%d sweep=%d(%.2f)"), bShort ? 1 : 0, ShortHit.ImpactPoint.Z, bUp ? 1 : 0, UpHit.ImpactPoint.Z, bSimple ? 1 : 0, bSweep ? 1 : 0, SweepHit.ImpactPoint.Z);
		if (*FramesLogged == 0 && CubeMesh)
		{
			ATDBenchChunkActor* CubeActor = SpawnChunkActor(FVector(900000.0, 0.0, 0.0));
			UStaticMeshComponent* CubeComponent = NewObject<UStaticMeshComponent>(CubeActor);
			CubeComponent->SetMobility(EComponentMobility::Static);
			CubeComponent->SetupAttachment(CubeActor->GetRootComponent());
			CubeComponent->SetStaticMesh(CubeMesh);
			CubeComponent->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
			CubeComponent->RegisterComponent();
		}
		FHitResult CubeHit;
		const bool bCube = GetWorld()->LineTraceSingleByChannel(CubeHit, FVector(900000.0, 0.0, 1000.0), FVector(900000.0, 0.0, -1000.0), ECC_Visibility, FCollisionQueryParams(FName(TEXT("DiagCube")), false));
		UE_LOG(LogTDBench, Display, TEXT("TDBENCH diag cube hit=%d z=%.2f comp=%s"), bCube ? 1 : 0, CubeHit.ImpactPoint.Z, *GetNameSafe(CubeHit.GetComponent()));
		++(*FramesLogged);
		return *FramesLogged >= 4;
	});
}

void ATDBenchGameMode::QueueTerrainSuite()
{
	QueueTerrainConfig(33, 100.0, TEXT("32m@1m_33x33"));
	QueueTerrainConfig(65, 100.0, TEXT("64m@1m_65x65"));
	QueueTerrainConfig(129, 50.0, TEXT("64m@0.5m_129x129"));
	QueueTerrainConfig(129, 100.0, TEXT("128m@1m_129x129"));
}

void ATDBenchGameMode::QueueTerrainConfig(int32 SidePoints, double SpacingCm, const FString& Config)
{
	TSharedRef<TDBench::FTerrainGrid> Grid = MakeShared<TDBench::FTerrainGrid>();
	TSharedRef<FDynamicMesh3> BaseMesh = MakeShared<FDynamicMesh3>();

	Steps.Add([this, Grid, BaseMesh, SidePoints, SpacingCm, Config]() -> bool
	{
		TArray<double> HeightSamples;
		TArray<double> MeshSamples;
		for (int32 Rep = 0; Rep < WarmupRepetitions + Repetitions; ++Rep)
		{
			const double Start = TDBench::NowMs();
			TDBench::FTerrainGrid LocalGrid = TDBench::GenerateGrid(SidePoints, SpacingCm);
			const double HeightsDone = TDBench::NowMs();
			FDynamicMesh3 Mesh = TDBench::BuildTerrainMesh(LocalGrid, FVector3d::ZeroVector);
			const double MeshDone = TDBench::NowMs();
			if (Rep >= WarmupRepetitions)
			{
				HeightSamples.Add(HeightsDone - Start);
				MeshSamples.Add(MeshDone - HeightsDone);
			}
			if (Rep == 0)
			{
				*Grid = LocalGrid;
				*BaseMesh = MoveTemp(Mesh);
			}
		}
		AddSampleResult(TEXT("a0_height_generation_ms"), Config, HeightSamples, TEXT("ms"));
		AddSampleResult(TEXT("a_dynamicmesh_build_ms"), Config, MeshSamples, TEXT("ms"));
		TSharedRef<FJsonObject> Info = MakeShared<FJsonObject>();
		Info->SetStringField(TEXT("name"), TEXT("mesh_info"));
		Info->SetStringField(TEXT("config"), Config);
		Info->SetNumberField(TEXT("vertices"), BaseMesh->VertexCount());
		Info->SetNumberField(TEXT("triangles"), BaseMesh->TriangleCount());
		AddRecord(Info);
		return true;
	});

	Steps.Add([this, SidePoints, SpacingCm, Config]() -> bool
	{
		constexpr int32 JobCount = 16;
		TArray<double> Samples;
		for (int32 Rep = 0; Rep < WarmupRepetitions + Repetitions; ++Rep)
		{
			const double Start = TDBench::NowMs();
			ParallelFor(JobCount, [SidePoints, SpacingCm](int32 JobIndex)
			{
				TDBench::FTerrainGrid LocalGrid = TDBench::GenerateGrid(SidePoints, SpacingCm);
				FDynamicMesh3 Mesh = TDBench::BuildTerrainMesh(LocalGrid, FVector3d::ZeroVector);
			});
			if (Rep >= WarmupRepetitions)
			{
				Samples.Add((TDBench::NowMs() - Start) / JobCount);
			}
		}
		AddSampleResult(TEXT("a_parallel16_heights_plus_mesh_per_chunk_ms"), Config, Samples, TEXT("ms"));
		return true;
	});

	QueueRenderOnlySteps(BaseMesh, Config);
	QueueCollisionProbeSteps(BaseMesh, Grid, Config, TDBench::ECollisionMode::Sync);
	QueueCollisionProbeSteps(BaseMesh, Grid, Config, TDBench::ECollisionMode::Async);
	QueueEditRecookSteps(Grid, Config, TDBench::ECollisionMode::Sync);
	QueueEditRecookSteps(Grid, Config, TDBench::ECollisionMode::Async);
	QueueAsyncBurstSteps(BaseMesh, Grid, Config, 8);

	if (SidePoints == 65 || SidePoints == 129)
	{
		Steps.Add([this, Grid, Config]() -> bool
		{
			const int32 Side = Grid->SidePoints;
			TArray<double> Samples;
			TRefCountPtr<Chaos::FHeightField> LastField;
			for (int32 Rep = 0; Rep < WarmupRepetitions + Repetitions; ++Rep)
			{
				TArray<Chaos::FReal> Heights;
				Heights.SetNumUninitialized(Side * Side);
				for (int32 Index = 0; Index < Side * Side; ++Index)
				{
					Heights[Index] = Grid->HeightsCm[Index];
				}
				TArray<uint8> Materials;
				Materials.SetNumZeroed((Side - 1) * (Side - 1));
				const double Start = TDBench::NowMs();
				TRefCountPtr<Chaos::FHeightField> Field(new Chaos::FHeightField(MoveTemp(Heights), MoveTemp(Materials), Side, Side, Chaos::FVec3(Grid->SpacingCm, Grid->SpacingCm, 1.0)));
				Field->BoundingBox();
				const double Built = TDBench::NowMs();
				if (Rep >= WarmupRepetitions)
				{
					Samples.Add(Built - Start);
				}
				LastField = Field;
			}
			AddSampleResult(TEXT("e_chaos_heightfield_ctor_ms"), Config, Samples, TEXT("ms"));

			TRefCountPtr<Chaos::FHeightField> UnitField;
			{
				TArray<Chaos::FReal> Heights;
				Heights.SetNumUninitialized(Side * Side);
				for (int32 Index = 0; Index < Side * Side; ++Index)
				{
					Heights[Index] = Grid->HeightsCm[Index];
				}
				TArray<uint8> Materials;
				Materials.SetNumZeroed((Side - 1) * (Side - 1));
				UnitField = new Chaos::FHeightField(MoveTemp(Heights), MoveTemp(Materials), Side, Side, Chaos::FVec3(1.0, 1.0, 1.0));
				UnitField->BoundingBox();
			}
			int32 Hits = 0;
			int32 FarDownHits = 0;
			int32 NearDownHits = 0;
			int32 UpOnlyHits = 0;
			double MinHeight = TNumericLimits<double>::Max();
			double MaxHeight = TNumericLimits<double>::Lowest();
			for (double HeightCm : Grid->HeightsCm)
			{
				MinHeight = FMath::Min(MinHeight, HeightCm);
				MaxHeight = FMath::Max(MaxHeight, HeightCm);
			}
			int32 UnitHits = 0;
			double UnitMaxErrorDirect = 0.0;
			double UnitMaxErrorSwapped = 0.0;
			double MaxErrorDirect = 0.0;
			double MaxErrorSwapped = 0.0;
			for (int32 SampleIndex = 0; SampleIndex < 200; ++SampleIndex)
			{
				const FVector2D Local = TDBench::TraceSampleLocal(*Grid, SampleIndex);
				Chaos::FReal OutTime = 0.0;
				Chaos::FVec3 OutPosition(0.0);
				Chaos::FVec3 OutNormal(0.0);
				int32 OutFace = INDEX_NONE;
				const bool bFarDownHit = LastField->Raycast(Chaos::FVec3(Local.X, Local.Y, TDBench::TraceTopCm), Chaos::FVec3(0.0, 0.0, -1.0), TDBench::TraceTopCm - TDBench::TraceBottomCm, 0.0, OutTime, OutPosition, OutNormal, OutFace);
				const bool bNearDownHit = bFarDownHit || LastField->Raycast(Chaos::FVec3(Local.X, Local.Y, MaxHeight + 100.0), Chaos::FVec3(0.0, 0.0, -1.0), MaxHeight - MinHeight + 200.0, 0.0, OutTime, OutPosition, OutNormal, OutFace);
				const bool bUpHit = bNearDownHit || LastField->Raycast(Chaos::FVec3(Local.X, Local.Y, MinHeight - 100.0), Chaos::FVec3(0.0, 0.0, 1.0), MaxHeight - MinHeight + 200.0, 0.0, OutTime, OutPosition, OutNormal, OutFace);
				Chaos::FReal UnitTime = 0.0;
				Chaos::FVec3 UnitPosition(0.0);
				Chaos::FVec3 UnitNormal(0.0);
				int32 UnitFace = INDEX_NONE;
				const bool bUnitHit = UnitField->Raycast(Chaos::FVec3(Local.X / Grid->SpacingCm, Local.Y / Grid->SpacingCm, MaxHeight + 100.0), Chaos::FVec3(0.0, 0.0, -1.0), MaxHeight - MinHeight + 200.0, 0.0, UnitTime, UnitPosition, UnitNormal, UnitFace);
				if (bUnitHit)
				{
					++UnitHits;
					const double UnitDirect = FMath::Min(FMath::Abs(UnitPosition.Z - TDBench::AnalyticHeightCm(*Grid, Local.X, Local.Y, false, false)), FMath::Abs(UnitPosition.Z - TDBench::AnalyticHeightCm(*Grid, Local.X, Local.Y, true, false)));
					const double UnitSwapped = FMath::Min(FMath::Abs(UnitPosition.Z - TDBench::AnalyticHeightCm(*Grid, Local.X, Local.Y, false, true)), FMath::Abs(UnitPosition.Z - TDBench::AnalyticHeightCm(*Grid, Local.X, Local.Y, true, true)));
					UnitMaxErrorDirect = FMath::Max(UnitMaxErrorDirect, UnitDirect);
					UnitMaxErrorSwapped = FMath::Max(UnitMaxErrorSwapped, UnitSwapped);
				}
				FarDownHits += bFarDownHit ? 1 : 0;
				NearDownHits += (!bFarDownHit && bNearDownHit) ? 1 : 0;
				UpOnlyHits += (!bNearDownHit && bUpHit) ? 1 : 0;
				if (!bUpHit)
				{
					continue;
				}
				++Hits;
				const double Direct = FMath::Min(FMath::Abs(OutPosition.Z - TDBench::AnalyticHeightCm(*Grid, Local.X, Local.Y, false, false)), FMath::Abs(OutPosition.Z - TDBench::AnalyticHeightCm(*Grid, Local.X, Local.Y, true, false)));
				const double Swapped = FMath::Min(FMath::Abs(OutPosition.Z - TDBench::AnalyticHeightCm(*Grid, Local.X, Local.Y, false, true)), FMath::Abs(OutPosition.Z - TDBench::AnalyticHeightCm(*Grid, Local.X, Local.Y, true, true)));
				MaxErrorDirect = FMath::Max(MaxErrorDirect, Direct);
				MaxErrorSwapped = FMath::Max(MaxErrorSwapped, Swapped);
			}
			TSharedRef<FJsonObject> Check = MakeShared<FJsonObject>();
			Check->SetStringField(TEXT("name"), TEXT("e_chaos_heightfield_raycast_check"));
			Check->SetStringField(TEXT("config"), Config);
			Check->SetNumberField(TEXT("rays"), 200);
			Check->SetNumberField(TEXT("hits"), Hits);
			Check->SetNumberField(TEXT("far_down_hits"), FarDownHits);
			Check->SetNumberField(TEXT("near_down_only_hits"), NearDownHits);
			Check->SetNumberField(TEXT("up_only_hits"), UpOnlyHits);
			Check->SetNumberField(TEXT("unit_scale_hits"), UnitHits);
			Check->SetNumberField(TEXT("unit_scale_max_error_cm_rows_are_y"), UnitMaxErrorDirect);
			Check->SetNumberField(TEXT("unit_scale_max_error_cm_rows_are_x"), UnitMaxErrorSwapped);
			UE_LOG(LogTDBench, Display, TEXT("TDBENCH heightfield unit-scale raycast [%s] hits=%d errRowsY=%.4f errRowsX=%.4f"), *Config, UnitHits, UnitMaxErrorDirect, UnitMaxErrorSwapped);
			Check->SetNumberField(TEXT("max_error_cm_rows_are_y"), MaxErrorDirect);
			Check->SetNumberField(TEXT("max_error_cm_rows_are_x"), MaxErrorSwapped);
			AddRecord(Check);
			UE_LOG(LogTDBench, Display, TEXT("TDBENCH heightfield raycast [%s] hits=%d farDown=%d nearDown=%d upOnly=%d errDirect=%.4f errSwapped=%.4f"), *Config, Hits, FarDownHits, NearDownHits, UpOnlyHits, MaxErrorDirect, MaxErrorSwapped);
			return true;
		});
	}

	if (SidePoints == 65 || (SidePoints == 129 && SpacingCm < 99.0))
	{
		QueuePrecisionSteps(Grid, Config);
	}
}

void ATDBenchGameMode::QueueRenderOnlySteps(const TSharedRef<FDynamicMesh3>& BaseMesh, const FString& Config)
{
	TSharedRef<TDBench::FSampleTable> Table = MakeShared<TDBench::FSampleTable>();
	for (int32 Rep = 0; Rep < WarmupRepetitions + Repetitions; ++Rep)
	{
		TSharedRef<TDBench::FProbeState> State = MakeShared<TDBench::FProbeState>();
		Steps.Add([this, BaseMesh, Table, State, Rep]() -> bool
		{
			if (!State->bStarted)
			{
				FDynamicMesh3 MeshCopy = *BaseMesh;
				ATDBenchChunkActor* Actor = SpawnChunkActor(FVector(200000.0, 0.0, 0.0));
				State->Component = TDBench::CreateTerrainComponent(Actor, MoveTemp(MeshCopy), TDBench::ECollisionMode::None, State->Times);
				State->Actor = Actor;
				State->bStarted = true;
				return false;
			}
			const double NextFrameMs = TDBench::NowMs() - State->Times.EndMs;
			const double DestroyStart = TDBench::NowMs();
			if (State->Actor.IsValid())
			{
				State->Actor->Destroy();
			}
			const double DestroyMs = TDBench::NowMs() - DestroyStart;
			if (Rep >= WarmupRepetitions)
			{
				Table->Add(TEXT("create_ms"), State->Times.CreateMs);
				Table->Add(TEXT("setmesh_ms"), State->Times.SetMeshMs);
				Table->Add(TEXT("register_ms"), State->Times.RegisterMs);
				Table->Add(TEXT("total_ms"), State->Times.TotalMs);
				Table->Add(TEXT("eof_flush_ms"), State->Times.EndOfFrameFlushMs);
				Table->Add(TEXT("next_frame_ms"), NextFrameMs);
				Table->Add(TEXT("destroy_ms"), DestroyMs);
			}
			return true;
		});
	}
	Steps.Add([this, Table, Config]() -> bool
	{
		EmitTable(TEXT("b_component_no_collision"), Config, *Table);
		return true;
	});
}

void ATDBenchGameMode::QueueCollisionProbeSteps(const TSharedRef<FDynamicMesh3>& BaseMesh, const TSharedRef<TDBench::FTerrainGrid>& Grid, const FString& Config, TDBench::ECollisionMode Mode)
{
	const FString Prefix = (Mode == TDBench::ECollisionMode::Sync) ? TEXT("c_sync_complex_collision") : TEXT("d_async_complex_collision");
	TSharedRef<TDBench::FSampleTable> Table = MakeShared<TDBench::FSampleTable>();
	for (int32 Rep = 0; Rep < WarmupRepetitions + Repetitions; ++Rep)
	{
		TSharedRef<TDBench::FProbeState> State = MakeShared<TDBench::FProbeState>();
		Steps.Add([this, BaseMesh, Grid, Table, State, Mode, Rep]() -> bool
		{
			const FVector Origin(400000.0, 0.0, 0.0);
			const double CenterX = Origin.X + Grid->GetSizeCm() * 0.5 + 3.0;
			const double CenterY = Origin.Y + Grid->GetSizeCm() * 0.5 + 7.0;
			if (!State->bStarted)
			{
				FDynamicMesh3 MeshCopy = *BaseMesh;
				ATDBenchChunkActor* Actor = SpawnChunkActor(Origin);
				State->StartMs = TDBench::NowMs();
				State->StartFrame = FrameCounter;
				State->Component = TDBench::CreateTerrainComponent(Actor, MoveTemp(MeshCopy), Mode, State->Times);
				State->Actor = Actor;
				State->bStarted = true;
			}
			else if (State->NextFrameMs < 0.0)
			{
				State->NextFrameMs = TDBench::NowMs() - State->Times.EndMs;
			}
			if (State->ReadyMs < 0.0 && !State->bTimedOut)
			{
				FVector Impact;
				const bool bHit = TDBench::TraceDown(GetWorld(), State->Component.Get(), CenterX, CenterY, Origin.Z, Impact);
				const double ProbeElapsedMs = TDBench::NowMs() - State->StartMs;
				if (bHit)
				{
					State->ReadyMs = ProbeElapsedMs;
					State->ReadyFrames = FrameCounter - State->StartFrame;
				}
				State->bTimedOut = !bHit && ProbeElapsedMs > 2000.0;
			}
			const bool bTimedOut = State->bTimedOut;
			const double ElapsedMs = bTimedOut ? 2000.0 : State->ReadyMs;
			if ((State->ReadyMs < 0.0 && !bTimedOut) || State->NextFrameMs < 0.0)
			{
				return false;
			}
			const double DestroyStart = TDBench::NowMs();
			if (State->Actor.IsValid())
			{
				State->Actor->Destroy();
			}
			const double DestroyMs = TDBench::NowMs() - DestroyStart;
			if (Rep >= WarmupRepetitions)
			{
				Table->Add(TEXT("gt_create_ms"), State->Times.CreateMs);
				Table->Add(TEXT("gt_setmesh_ms"), State->Times.SetMeshMs);
				Table->Add(TEXT("gt_register_ms"), State->Times.RegisterMs);
				Table->Add(TEXT("gt_total_ms"), State->Times.TotalMs);
				Table->Add(TEXT("gt_eof_flush_ms"), State->Times.EndOfFrameFlushMs);
				Table->Add(TEXT("next_frame_ms"), State->NextFrameMs);
				Table->Add(TEXT("trace_ready_wall_ms"), ElapsedMs);
				Table->Add(TEXT("trace_ready_frames"), double(State->ReadyFrames));
				Table->Add(TEXT("timeout_count"), bTimedOut ? 1.0 : 0.0);
				Table->Add(TEXT("destroy_ms"), DestroyMs);
			}
			return true;
		});
	}
	Steps.Add([this, Table, Prefix, Config]() -> bool
	{
		EmitTable(Prefix, Config, *Table);
		return true;
	});
}

void ATDBenchGameMode::QueueEditRecookSteps(const TSharedRef<TDBench::FTerrainGrid>& Grid, const FString& Config, TDBench::ECollisionMode Mode)
{
	const FString Prefix = (Mode == TDBench::ECollisionMode::Sync) ? TEXT("c2_edit_recook_sync") : TEXT("d2_edit_recook_async");
	TSharedRef<TDBench::FSampleTable> Table = MakeShared<TDBench::FSampleTable>();
	TSharedRef<TDBench::FEditState> State = MakeShared<TDBench::FEditState>();
	const FVector Origin(600000.0, 0.0, 0.0);

	Steps.Add([this, Grid, State, Mode, Origin]() -> bool
	{
		if (!State->Actor.IsValid())
		{
			ATDBenchChunkActor* Actor = SpawnChunkActor(Origin);
			TDBench::FTerrainSpawnTimes Times;
			State->Component = TDBench::CreateTerrainComponent(Actor, TDBench::BuildTerrainMesh(*Grid, FVector3d::ZeroVector), Mode, Times);
			State->Actor = Actor;
			State->StartMs = TDBench::NowMs();
		}
		FVector Impact;
		const double ProbeX = Origin.X + Grid->GetSizeCm() * 0.5 + 3.0;
		const double ProbeY = Origin.Y + Grid->GetSizeCm() * 0.5 + 7.0;
		const bool bHit = TDBench::TraceDown(GetWorld(), State->Component.Get(), ProbeX, ProbeY, Origin.Z, Impact);
		return bHit || (TDBench::NowMs() - State->StartMs > 2000.0);
	});

	for (int32 Rep = 0; Rep < WarmupRepetitions + Repetitions; ++Rep)
	{
		Steps.Add([this, Grid, State, Table, Rep, Origin]() -> bool
		{
			UDynamicMeshComponent* Component = State->Component.Get();
			if (!Component)
			{
				return true;
			}
			const int32 Side = Grid->SidePoints;
			const int32 Center = (Side - 1) / 2;
			const double ProbeLocalX = Center * Grid->SpacingCm + 3.0;
			const double ProbeLocalY = Center * Grid->SpacingCm + 7.0;
			if (!State->bWaiting)
			{
				const double Raise = (Rep % 2 == 0) ? 50.0 : 0.0;
				State->ExpectedZ = Origin.Z + TDBench::AnalyticHeightCm(*Grid, ProbeLocalX, ProbeLocalY) + Raise;
				const double Start = TDBench::NowMs();
				Component->EditMesh([Grid, Side, Center, Raise](FDynamicMesh3& Mesh)
				{
					for (int32 Y = Center - 2; Y <= Center + 2; ++Y)
					{
						for (int32 X = Center - 2; X <= Center + 2; ++X)
						{
							const int32 VertexId = Y * Side + X;
							const FVector3d Position = Mesh.GetVertex(VertexId);
							Mesh.SetVertex(VertexId, FVector3d(Position.X, Position.Y, Grid->GetHeightCm(X, Y) + Raise));
						}
					}
				});
				State->EditMs = TDBench::NowMs() - Start;
				State->StartMs = Start;
				State->StartFrame = FrameCounter;
				State->bWaiting = true;
			}
			FVector Impact;
			const bool bHit = TDBench::TraceDown(GetWorld(), Component, Origin.X + ProbeLocalX, Origin.Y + ProbeLocalY, Origin.Z, Impact);
			const bool bUpdated = bHit && FMath::Abs(Impact.Z - State->ExpectedZ) < 1.0;
			const double ElapsedMs = TDBench::NowMs() - State->StartMs;
			const bool bTimedOut = ElapsedMs > 2000.0;
			if (!bUpdated && !bTimedOut)
			{
				return false;
			}
			State->bWaiting = false;
			if (Rep >= WarmupRepetitions)
			{
				Table->Add(TEXT("gt_editmesh_ms"), State->EditMs);
				Table->Add(TEXT("trace_updated_wall_ms"), ElapsedMs);
				Table->Add(TEXT("trace_updated_frames"), double(FrameCounter - State->StartFrame));
				Table->Add(TEXT("timeout_count"), bTimedOut ? 1.0 : 0.0);
			}
			return true;
		});
	}

	Steps.Add([this, State, Table, Prefix, Config]() -> bool
	{
		if (State->Actor.IsValid())
		{
			State->Actor->Destroy();
		}
		EmitTable(Prefix, Config, *Table);
		return true;
	});
}

void ATDBenchGameMode::QueueAsyncBurstSteps(const TSharedRef<FDynamicMesh3>& BaseMesh, const TSharedRef<TDBench::FTerrainGrid>& Grid, const FString& Config, int32 ChunkCount)
{
	struct FBurstState
	{
		TArray<TWeakObjectPtr<ATDBenchChunkActor>> Actors;
		TArray<TWeakObjectPtr<UDynamicMeshComponent>> Components;
		double StartMs = 0.0;
		int64 StartFrame = 0;
		bool bStarted = false;
	};
	TSharedRef<TDBench::FSampleTable> Table = MakeShared<TDBench::FSampleTable>();
	for (int32 Rep = 0; Rep < WarmupRepetitions + Repetitions; ++Rep)
	{
		TSharedRef<FBurstState> State = MakeShared<FBurstState>();
		Steps.Add([this, BaseMesh, Grid, Table, State, ChunkCount, Rep]() -> bool
		{
			const double Spacing = Grid->GetSizeCm() + 1000.0;
			if (!State->bStarted)
			{
				TArray<FDynamicMesh3> Copies;
				Copies.Reserve(ChunkCount);
				for (int32 Index = 0; Index < ChunkCount; ++Index)
				{
					Copies.Add(*BaseMesh);
				}
				State->StartMs = TDBench::NowMs();
				State->StartFrame = FrameCounter;
				for (int32 Index = 0; Index < ChunkCount; ++Index)
				{
					ATDBenchChunkActor* Actor = SpawnChunkActor(FVector(1000000.0 + Index * Spacing, 0.0, 0.0));
					TDBench::FTerrainSpawnTimes Times;
					State->Components.Add(TDBench::CreateTerrainComponent(Actor, MoveTemp(Copies[Index]), TDBench::ECollisionMode::Async, Times));
					State->Actors.Add(Actor);
				}
				State->bStarted = true;
				if (Rep >= WarmupRepetitions)
				{
					Table->Add(TEXT("gt_spawn_all_ms"), TDBench::NowMs() - State->StartMs);
				}
			}
			int32 ReadyCount = 0;
			for (int32 Index = 0; Index < State->Components.Num(); ++Index)
			{
				FVector Impact;
				const double ProbeX = 1000000.0 + Index * Spacing + Grid->GetSizeCm() * 0.5 + 3.0;
				const double ProbeY = Grid->GetSizeCm() * 0.5 + 7.0;
				ReadyCount += TDBench::TraceDown(GetWorld(), State->Components[Index].Get(), ProbeX, ProbeY, 0.0, Impact) ? 1 : 0;
			}
			const double ElapsedMs = TDBench::NowMs() - State->StartMs;
			const bool bTimedOut = ElapsedMs > 3000.0;
			if (ReadyCount < ChunkCount && !bTimedOut)
			{
				return false;
			}
			const double DestroyStart = TDBench::NowMs();
			for (const TWeakObjectPtr<ATDBenchChunkActor>& Actor : State->Actors)
			{
				if (Actor.IsValid())
				{
					Actor->Destroy();
				}
			}
			if (Rep >= WarmupRepetitions)
			{
				Table->Add(TEXT("all_ready_wall_ms"), ElapsedMs);
				Table->Add(TEXT("all_ready_frames"), double(FrameCounter - State->StartFrame));
				Table->Add(TEXT("timeout_count"), bTimedOut ? 1.0 : 0.0);
				Table->Add(TEXT("destroy_all_ms"), TDBench::NowMs() - DestroyStart);
			}
			return true;
		});
	}
	Steps.Add([this, Table, Config, ChunkCount]() -> bool
	{
		EmitTable(FString::Printf(TEXT("d3_async_burst_%d_chunks"), ChunkCount), Config, *Table);
		return true;
	});
}

void ATDBenchGameMode::QueuePrecisionSteps(const TSharedRef<TDBench::FTerrainGrid>& Grid, const FString& Config)
{
	const double Offsets[] = {0.0, 1.0e6, 1.0e7, 1.0e8, 1.0e9};
	for (double Offset : Offsets)
	{
		for (int32 LayoutIndex = 0; LayoutIndex < 2; ++LayoutIndex)
		{
			const bool bBakedIntoVertices = (LayoutIndex == 1);
			if (bBakedIntoVertices && Offset == 0.0)
			{
				continue;
			}
			Steps.Add([this, Grid, Config, Offset, bBakedIntoVertices]() -> bool
			{
				const FVector ActorLocation = bBakedIntoVertices ? FVector::ZeroVector : FVector(Offset, Offset, 0.0);
				const FVector3d VertexOffset = bBakedIntoVertices ? FVector3d(Offset, Offset, 0.0) : FVector3d::ZeroVector;
				ATDBenchChunkActor* Actor = SpawnChunkActor(ActorLocation);
				TDBench::FTerrainSpawnTimes Times;
				UDynamicMeshComponent* Component = TDBench::CreateTerrainComponent(Actor, TDBench::BuildTerrainMesh(*Grid, VertexOffset), TDBench::ECollisionMode::Sync, Times);
				int32 Hits = 0;
				double MaxError = 0.0;
				double SumError = 0.0;
				double MaxDrift = 0.0;
				for (int32 SampleIndex = 0; SampleIndex < 200; ++SampleIndex)
				{
					const FVector2D Local = TDBench::TraceSampleLocal(*Grid, SampleIndex);
					const double WorldX = Offset + Local.X;
					const double WorldY = Offset + Local.Y;
					FVector Impact;
					if (!TDBench::TraceDown(GetWorld(), Component, WorldX, WorldY, 0.0, Impact))
					{
						continue;
					}
					++Hits;
					const double Error = FMath::Abs(Impact.Z - TDBench::AnalyticHeightCm(*Grid, Local.X, Local.Y));
					MaxError = FMath::Max(MaxError, Error);
					SumError += Error;
					MaxDrift = FMath::Max(MaxDrift, FMath::Max(FMath::Abs(Impact.X - WorldX), FMath::Abs(Impact.Y - WorldY)));
				}
				Actor->Destroy();
				TSharedRef<FJsonObject> Record = MakeShared<FJsonObject>();
				Record->SetStringField(TEXT("name"), TEXT("f_precision_trace"));
				Record->SetStringField(TEXT("config"), Config);
				Record->SetStringField(TEXT("layout"), bBakedIntoVertices ? TEXT("offset_baked_into_vertices") : TEXT("offset_in_actor_transform"));
				Record->SetNumberField(TEXT("offset_cm"), Offset);
				Record->SetNumberField(TEXT("rays"), 200);
				Record->SetNumberField(TEXT("hits"), Hits);
				Record->SetNumberField(TEXT("max_abs_error_cm"), MaxError);
				Record->SetNumberField(TEXT("mean_abs_error_cm"), Hits > 0 ? SumError / Hits : -1.0);
				Record->SetNumberField(TEXT("max_xy_drift_cm"), MaxDrift);
				Record->SetNumberField(TEXT("setup_gt_total_ms"), Times.TotalMs);
				AddRecord(Record);
				UE_LOG(LogTDBench, Display, TEXT("TDBENCH precision [%s] offset=%.0f baked=%d hits=%d maxErr=%.5f meanErr=%.5f drift=%.5f"), *Config, Offset, bBakedIntoVertices ? 1 : 0, Hits, MaxError, Hits > 0 ? SumError / Hits : -1.0, MaxDrift);
				return true;
			});
		}
	}
}

void ATDBenchGameMode::QueueIsmSuite()
{
	const int32 Counts[] = {256, 1024, 4096};
	for (int32 TypeIndex = 0; TypeIndex < 2; ++TypeIndex)
	{
		for (int32 Count : Counts)
		{
			QueueIsmConfig(TypeIndex == 1, Count, false);
			QueueIsmConfig(TypeIndex == 1, Count, true);
		}
	}
}

void ATDBenchGameMode::QueueIsmConfig(bool bHierarchical, int32 InstanceCount, bool bCollision)
{
	const FString Config = FString::Printf(TEXT("%s_n%d_collision_%s"), bHierarchical ? TEXT("hism") : TEXT("ism"), InstanceCount, bCollision ? TEXT("on") : TEXT("off"));
	TSharedRef<TArray<FTransform>> Transforms = MakeShared<TArray<FTransform>>();
	for (int32 Index = 0; Index < InstanceCount; ++Index)
	{
		const double X = TDBench::HashToUnit(uint64(Index) * 3 + 101) * 6400.0;
		const double Y = TDBench::HashToUnit(uint64(Index) * 3 + 102) * 6400.0;
		const double Yaw = TDBench::HashToUnit(uint64(Index) * 3 + 103) * 360.0;
		Transforms->Add(FTransform(FRotator(0.0, Yaw, 0.0), FVector(X, Y, 25.0), FVector(0.5)));
	}
	TSharedRef<TArray<int32>> RemovalIndices = MakeShared<TArray<int32>>();
	for (int32 Index = InstanceCount - 1; Index >= 0; --Index)
	{
		if (Index % 10 == 0)
		{
			RemovalIndices->Add(Index);
		}
	}
	TSharedRef<TDBench::FSampleTable> Table = MakeShared<TDBench::FSampleTable>();
	TSharedRef<bool> bTreeWaitBroken = MakeShared<bool>(false);

	for (int32 Rep = 0; Rep < WarmupRepetitions + Repetitions; ++Rep)
	{
		TSharedRef<TDBench::FIsmState> State = MakeShared<TDBench::FIsmState>();
		Steps.Add([this, Transforms, RemovalIndices, Table, State, bTreeWaitBroken, bHierarchical, bCollision, Rep]() -> bool
		{
			const bool bRecord = Rep >= WarmupRepetitions;
			UInstancedStaticMeshComponent* Component = State->Component.Get();
			UHierarchicalInstancedStaticMeshComponent* Hierarchical = Cast<UHierarchicalInstancedStaticMeshComponent>(Component);

			if (State->Phase != 0 && !State->bNextFrameRecorded)
			{
				State->bNextFrameRecorded = true;
				if (bRecord)
				{
					Table->Add(TEXT("next_frame_ms"), TDBench::NowMs() - State->CreatedEndMs);
				}
			}

			auto IsTreeReady = [Hierarchical]() -> bool
			{
				return !Hierarchical || (Hierarchical->IsTreeFullyBuilt() && !Hierarchical->IsAsyncBuilding());
			};
			auto WaitTree = [&](const TCHAR* Key, int32 NextPhase) -> bool
			{
				const double ElapsedMs = TDBench::NowMs() - State->PhaseStartMs;
				const bool bReady = IsTreeReady();
				if (!bReady && !*bTreeWaitBroken && ElapsedMs < 3000.0)
				{
					return false;
				}
				if (!bReady)
				{
					*bTreeWaitBroken = true;
				}
				if (bRecord)
				{
					Table->Add(FString(Key) + TEXT("_wall_ms"), bReady ? ElapsedMs : -1.0);
					Table->Add(FString(Key) + TEXT("_frames"), bReady ? double(FrameCounter - State->PhaseStartFrame) : -1.0);
				}
				State->Phase = NextPhase;
				return false;
			};

			switch (State->Phase)
			{
			case 0:
			{
				ATDBenchChunkActor* Actor = SpawnChunkActor(FVector(800000.0, 0.0, 0.0));
				const double Start = TDBench::NowMs();
				UInstancedStaticMeshComponent* NewComponent = bHierarchical
					? NewObject<UHierarchicalInstancedStaticMeshComponent>(Actor)
					: NewObject<UInstancedStaticMeshComponent>(Actor);
				NewComponent->SetMobility(EComponentMobility::Static);
				NewComponent->SetupAttachment(Actor->GetRootComponent());
				NewComponent->SetStaticMesh(CubeMesh);
				NewComponent->SetCollisionEnabled(bCollision ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
				NewComponent->SetRemoveSwap();
				const double Created = TDBench::NowMs();
				NewComponent->AddInstances(*Transforms, false, false, false);
				const double Added = TDBench::NowMs();
				NewComponent->RegisterComponent();
				const double Registered = TDBench::NowMs();
				GetWorld()->SendAllEndOfFrameUpdates();
				const double Flushed = TDBench::NowMs();
				State->CreatedEndMs = Flushed;
				State->Actor = Actor;
				State->Component = NewComponent;
				if (bRecord)
				{
					Table->Add(TEXT("create_ms"), Created - Start);
					Table->Add(TEXT("add_instances_unregistered_ms"), Added - Created);
					Table->Add(TEXT("register_ms"), Registered - Added);
					Table->Add(TEXT("total_ms"), Registered - Start);
					Table->Add(TEXT("eof_flush_ms"), Flushed - Registered);
				}
				UHierarchicalInstancedStaticMeshComponent* NewHierarchical = Cast<UHierarchicalInstancedStaticMeshComponent>(NewComponent);
				if (NewHierarchical && !NewHierarchical->IsAsyncBuilding() && !NewHierarchical->IsTreeFullyBuilt())
				{
					NewHierarchical->BuildTreeIfOutdated(true, false);
				}
				State->PhaseStartMs = Start;
				State->PhaseStartFrame = FrameCounter;
				State->Phase = bHierarchical ? 1 : 2;
				return false;
			}
			case 1:
				return WaitTree(TEXT("tree_after_add"), 2);
			case 2:
			{
				if (!Component)
				{
					return true;
				}
				const double Start = TDBench::NowMs();
				Component->RemoveInstances(*RemovalIndices, true);
				const double Removed = TDBench::NowMs();
				if (bRecord)
				{
					Table->Add(TEXT("remove_10pct_ms"), Removed - Start);
				}
				State->PhaseStartMs = Start;
				State->PhaseStartFrame = FrameCounter;
				State->Phase = bHierarchical ? 3 : 4;
				return false;
			}
			case 3:
				return WaitTree(TEXT("tree_after_remove"), 4);
			case 4:
			{
				if (!Component)
				{
					return true;
				}
				const double Start = TDBench::NowMs();
				const int32 NewIndex = Component->AddInstance(FTransform(FVector(3200.0, 3200.0, 25.0)), false);
				const double Added = TDBench::NowMs();
				Component->RemoveInstance(NewIndex);
				const double Removed = TDBench::NowMs();
				if (bRecord)
				{
					Table->Add(TEXT("add_one_registered_ms"), Added - Start);
					Table->Add(TEXT("remove_one_registered_ms"), Removed - Added);
				}
				State->PhaseStartMs = Start;
				State->PhaseStartFrame = FrameCounter;
				State->Phase = bHierarchical ? 5 : 6;
				return false;
			}
			case 5:
				return WaitTree(TEXT("tree_after_single_edit"), 6);
			default:
			{
				const double Start = TDBench::NowMs();
				if (State->Actor.IsValid())
				{
					State->Actor->Destroy();
				}
				if (bRecord)
				{
					Table->Add(TEXT("destroy_ms"), TDBench::NowMs() - Start);
				}
				return true;
			}
			}
		});
	}
	Steps.Add([this, Table, Config]() -> bool
	{
		EmitTable(TEXT("ism"), Config, *Table);
		return true;
	});
}

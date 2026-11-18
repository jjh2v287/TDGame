#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FluidFX/TDFog3DLayer.h"
#include "FluidFX/TDFog3DTypes.h"
#include "TDFog3DZoneActor.generated.h"

class APawn;
class UMaterialInstanceDynamic;
class UExponentialHeightFogComponent;
class UMaterialInterface;
class USceneCaptureComponent2D;
class USceneComponent;
class USkeletalMeshComponent;
class UStaticMeshComponent;
class UTextureRenderTarget2D;
class UTextureRenderTargetVolume;
struct FStreamableHandle;
struct FTDFog3DZoneActorGroundTestAccess;
struct FTDFog3DZoneActorTestAccess;

enum class ETDFog3DZoneTickOutcome : uint8
{
	Hidden,
	Inactive,
	Simulate
};

struct FTDFog3DZoneTickGates
{
	bool bIsRepresentative = false;
	bool bHasValidSettings = false;
	bool bIsEnabled = false;
	bool bIsMaterialReady = false;
	bool bHasPawn = false;
	bool bIsPawnInsideZone = false;
	int32 ConsoleVolumetricFog = 1;
	bool bHasHeightFog = false;
	bool bHeightFogEnablesVolumetricFog = false;
	bool bHeightFogRenders = false;
	int32 Mode = 0;
};

struct FTDFog3DHeightFogState
{
	bool bIsRegistered = false;
	bool bRenders = false;
	bool bEnablesVolumetricFog = false;
	float FogDensity = 0.0f;
	float SecondFogDensity = 0.0f;
	float FogMaxOpacity = 0.0f;
	float VolumetricFogDistance = 0.0f;
};

struct FTDFog3DInputPose
{
	TOptional<FTransform> BladeSocketWorld;
	TOptional<FVector> HandWorld;
	TOptional<FVector> LeftFootWorld;
	TOptional<FVector> RightFootWorld;
	TOptional<FVector> PelvisWorld;
	TOptional<FVector> PawnWorld;
};

struct FTDFog3DMaterialParameters
{
	FVector4f Window = FVector4f(0.0f, 0.0f, 0.0f, 0.0f);
	FVector4f Grid = FVector4f(0.0f, 0.0f, 0.0f, 0.0f);
	FVector4f BaseShape = FVector4f(0.0f, 0.0f, 0.0f, 0.0f);
	FVector4f Zone = FVector4f(0.0f, 0.0f, 0.0f, 0.0f);
	FVector4f FineNoise = FVector4f(0.0f, 0.0f, 0.0f, 0.0f);
	FVector4f StainNoise = FVector4f(0.0f, 0.0f, 0.0f, 0.0f);
	FVector4f Ground = FVector4f(0.0f, 0.0f, 0.0f, 0.0f);
	float Active = 0.0f;
};

UCLASS()
class TDGAME_API ATDFog3DZoneActor : public AActor
{
	GENERATED_BODY()

public:
	ATDFog3DZoneActor();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaSeconds) override;
	void RequestGroundRecapture();
	void RequestGroundReadback(const FVector2D& StartWorld, const FVector2D& EndWorld, int32 SampleCount);

	static constexpr int32 HandSlotId = 100;
	static constexpr int32 LeftFootSlotId = 101;
	static constexpr int32 RightFootSlotId = 102;
	static constexpr int32 PelvisSlotId = 103;

	static bool IsPointInsideZone(const FVector& PointWorld, const FVector& ZoneCenterWorld, const FVector& ZoneHalfExtentCm, double VerticalMarginCm);
	static bool IsVolumetricFogDrawn(const FTDFog3DZoneTickGates& Gates);
	static bool IsHeightFogDrawingVolumetricFog(const FTDFog3DHeightFogState& State);
	static ETDFog3DZoneTickOutcome DecideTickOutcome(const FTDFog3DZoneTickGates& Gates);
	static FVector ComputeZoneVolumeScale(const FVector& ZoneHalfExtentCm, const FVector& MeshBoxExtentCm);
	static void AppendBladeSamples(
		const FTransform& SocketWorld,
		const FVector& BaseLocalCm,
		const FVector& TipLocalCm,
		float RadiusCm,
		float WindStrength,
		float WindGain,
		float ErosionPerSec,
		TArray<FTDFog3DInputSample>& InOutSamples);
	static void BuildInputSamples(const FTDFog3DInputPose& Pose, const FTDFog3DSettings& Settings, TArray<FTDFog3DInputSample>& OutSamples);
	static FString MakeSettingsLogText(const FTDFog3DSettings& Settings, int32 QualityLevel, int32 Resolution, float StepsPerSecond);
	static void ComputeConstantMaterialParameters(const FTDFog3DSettings& Settings, const FVector& ZoneHalfExtentCm, int32 WindowCells, FTDFog3DMaterialParameters& InOutParameters);
	static TSoftObjectPtr<UMaterialInterface> ChooseVolumeMaterial(const FTDFog3DSettings& Settings, const TSoftObjectPtr<UMaterialInterface>& ActorVolumeMaterial);
	static bool HaveGroundSettingsChanged(const FTDFog3DSettings& PreviousSettings, const FTDFog3DSettings& CurrentSettings);
	static bool IsGroundCaptureDrawn(bool bIsDrawnByMainRenderer, uint32 SceneFrameNumber, uint32 LastSeenSceneFrameNumber);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Fog3D")
	TObjectPtr<USceneComponent> ZoneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Fog3D")
	TObjectPtr<UStaticMeshComponent> ZoneVolume;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fog3D")
	FVector ZoneHalfExtentCm = FVector(3000.0, 3000.0, 150.0);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fog3D")
	TSoftObjectPtr<UMaterialInterface> VolumeMaterial;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	friend struct FTDFog3DZoneActorTestAccess;
	friend struct FTDFog3DZoneActorGroundTestAccess;

	void ClaimRepresentative();
	void UpdateZoneVolumeScale();
	bool RefreshSettings();
	void ConfigureLayer(bool bForceConfigure);
	void EnsureStateTarget();
	void RequestVolumeMaterial();
	void HandleVolumeMaterialLoaded();
	void ApplyVolumeMaterial(UMaterialInterface& Material);
	void SetZoneVisible(bool bIsVisible);
	void SetActiveParameter(bool bIsActive);
	void WriteMaterialParameters();
	FTDFog3DZoneTickGates GatherTickGates(const APawn* Pawn);
	void TrackResetCauses(const FTDFog3DZoneTickGates& Gates, ETDFog3DZoneTickOutcome Outcome);
	void FindHeightFog(float DeltaSeconds);
	void ForgetInputHistoryWhenPawnChanges(APawn* Pawn);
	void LogSummary();
	APawn* FindPlayerPawn() const;
	USkeletalMeshComponent* FindPawnMesh(const APawn& Pawn) const;
	void ReadInputPose(const APawn& Pawn, FTDFog3DInputPose& OutPose);
	void ReadBoneLocation(const USkeletalMeshComponent& Mesh, FName BoneName, uint8 MissingInputBit, TOptional<FVector>& OutLocation);
	void WarnMissingInput(uint8 MissingInputBit, FName InputName);
	void SimulateStep(const APawn& Pawn, float DeltaSeconds);
	void WarnAfterSubmit();
	FVector GetZoneReferenceWorld() const;
	double GetZoneFloorZ() const;
	void CancelVolumeMaterialLoad();
	bool ShouldFollowGround() const;
	bool HasGroundResources() const;
	void UpdateGroundResources();
	void EnsureGroundCapture();
	void EnsureGroundTargets(int32 CaptureSize);
	void ReleaseGroundResources();
	void ApplyGroundTextureParameter();
	void RestartGroundAfterRejection();
	void RestartGroundWhenSettingsChange(const FTDFog3DSettings& PreviousSettings);
	bool ReadGroundSceneDrawSignal();
	FTDFog3DGroundDecision TrackGround(const FVector& PlayerWorld);
	bool ConsumeForcedGroundRecapture();
	const TCHAR* TakeGroundRecaptureReason(ETDFog3DGroundTrackerState PreviousState);
	void LogGroundRecapture(const TCHAR* Reason, const FIntPoint& NextWindowMinCell) const;
	void CountDeferredGroundStep(const FVector& PlayerWorld);
	const TCHAR* ChooseGroundDeferralReason(const FVector& PlayerWorld) const;
	void LogFinishedGroundReadback();
	void RequestGroundCapture(const FIntPoint& NextWindowMinCell);
	void ApplyGroundCaptureList(UWorld& World);
	FTDFog3DGroundStep MakeGroundStep(const FTDFog3DGroundDecision& Decision) const;
	void ConfirmGroundStep(const FTDFog3DGroundDecision& Decision);

	UPROPERTY(Transient, VisibleInstanceOnly, Category = "Fog3D")
	TObjectPtr<UTextureRenderTargetVolume> StateTarget;

	UPROPERTY(Transient, VisibleInstanceOnly, Category = "Fog3D")
	TObjectPtr<UMaterialInstanceDynamic> VolumeMID;

	UPROPERTY(Transient, VisibleInstanceOnly, Category = "Fog3D")
	TObjectPtr<USceneCaptureComponent2D> GroundCapture;

	UPROPERTY(Transient, VisibleInstanceOnly, Category = "Fog3D")
	TObjectPtr<UTextureRenderTarget2D> GroundCaptureTarget;

	UPROPERTY(Transient, VisibleInstanceOnly, Category = "Fog3D")
	TObjectPtr<UTextureRenderTarget2D> GroundMaterialTarget;

	FTDFog3DLayer Layer;
	FTDFog3DGroundTracker GroundTracker;
	TSoftObjectPtr<UMaterialInterface> RequestedVolumeMaterial;
	float GroundCaptureDecodeOffsetCm = 0.0f;
	int32 GroundSeenRejectedStepCount = 0;
	int32 DeferredGroundStepTotal = 0;
	TArray<FVector2D> GroundReadbackWorldPoints;
	bool bIsGroundRecaptureRequested = false;
	bool bIsGroundRestartedBySettings = false;
	uint32 LastGroundSceneFrameNumber = 0;
	bool bHasRequestedVolumeMaterial = false;
	FTDFog3DSettings Settings;
	FTDFog3DMaterialParameters DesiredMaterialParameters;
	FTDFog3DMaterialParameters WrittenMaterialParameters;
	TSharedPtr<FStreamableHandle> VolumeMaterialLoadHandle;
	TWeakObjectPtr<UExponentialHeightFogComponent> HeightFogComponent;
	TWeakObjectPtr<APawn> LastTickPawn;
	int32 SettingsGeneration = INDEX_NONE;
	int32 AppliedMode = INDEX_NONE;
	int32 AppliedQualityLevel = INDEX_NONE;
	int32 ReportedRejectedStepCount = 0;
	uint32 SubmittedStepTotal = 0;
	int32 RejectedStepTotal = 0;
	float SecondsSinceHeightFogSearch = 0.0f;
	uint8 WarnedMissingInputMask = 0;
	bool bIsRepresentative = false;
	bool bHasReadSettings = false;
	bool bHasValidSettings = false;
	bool bHasWrittenMaterialParameters = false;
	bool bWasOutsideZone = false;
	bool bWasVolumetricFogOff = false;
	bool bHasWarnedDuplicate = false;
	bool bHasWarnedMissingMaterial = false;
	bool bHasWarnedMaterialDomain = false;
	bool bHasWarnedMissingMesh = false;
	bool bHasWarnedDroppedSpheres = false;
	bool bHasWarnedSkippedObstacles = false;
	bool bHasWarnedSubmitRefused = false;
};

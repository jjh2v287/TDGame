#pragma once

#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "FluidFX/TDFog3DGround.h"
#include "FluidFX/TDFog3DLayer.h"
#include "FluidFX/TDFog3DZoneActor.h"
#include "GameFramework/Pawn.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/Build.h"

#if WITH_DEV_AUTOMATION_TESTS

struct FTDFog3DZoneActorGroundTestAccess
{
	static FTDFog3DSettings& GetSettings(ATDFog3DZoneActor& Actor)
	{
		return Actor.Settings;
	}

	static void UpdateGroundResources(ATDFog3DZoneActor& Actor)
	{
		Actor.UpdateGroundResources();
	}

	static USceneCaptureComponent2D* GetGroundCapture(const ATDFog3DZoneActor& Actor)
	{
		return Actor.GroundCapture;
	}

	static UTextureRenderTarget2D* GetGroundCaptureTarget(const ATDFog3DZoneActor& Actor)
	{
		return Actor.GroundCaptureTarget;
	}

	static UTextureRenderTarget2D* GetGroundMaterialTarget(const ATDFog3DZoneActor& Actor)
	{
		return Actor.GroundMaterialTarget;
	}

	static void AttachMaterialInstance(ATDFog3DZoneActor& Actor, UMaterialInstanceDynamic* MaterialInstance)
	{
		Actor.VolumeMID = MaterialInstance;
		Actor.ApplyGroundTextureParameter();
	}

	static FTDFog3DMaterialParameters& GetDesiredMaterialParameters(ATDFog3DZoneActor& Actor)
	{
		return Actor.DesiredMaterialParameters;
	}

	static void WriteMaterialParameters(ATDFog3DZoneActor& Actor)
	{
		Actor.WriteMaterialParameters();
	}

	static FTDFog3DGroundStep MakeGroundStep(const ATDFog3DZoneActor& Actor, const FTDFog3DGroundDecision& Decision)
	{
		return Actor.MakeGroundStep(Decision);
	}

	static void SetGroundCaptureDecodeOffset(ATDFog3DZoneActor& Actor, float DecodeOffsetCm)
	{
		Actor.GroundCaptureDecodeOffsetCm = DecodeOffsetCm;
	}

	static int32 GetLayerResolution(const ATDFog3DZoneActor& Actor)
	{
		return Actor.Layer.GetResolution();
	}

	static FTDFog3DGroundTracker& GetGroundTracker(ATDFog3DZoneActor& Actor)
	{
		return Actor.GroundTracker;
	}

	static void RestartGroundWhenSettingsChange(ATDFog3DZoneActor& Actor, const FTDFog3DSettings& PreviousSettings)
	{
		Actor.RestartGroundWhenSettingsChange(PreviousSettings);
	}

	static void RestartGroundAfterRejection(ATDFog3DZoneActor& Actor)
	{
		Actor.RestartGroundAfterRejection();
	}

	static void SetGroundSeenRejectedStepCount(ATDFog3DZoneActor& Actor, int32 RejectedStepCount)
	{
		Actor.GroundSeenRejectedStepCount = RejectedStepCount;
	}

	static void ConfigureLayer(ATDFog3DZoneActor& Actor)
	{
		Actor.ConfigureLayer(true);
	}

	static FTDFog3DGroundDecision TrackGround(ATDFog3DZoneActor& Actor, const FVector& PlayerWorld)
	{
		return Actor.TrackGround(PlayerWorld);
	}

	static void SimulateStep(ATDFog3DZoneActor& Actor, const APawn& Pawn, float DeltaSeconds)
	{
		Actor.SimulateStep(Pawn, DeltaSeconds);
	}

	static const FTDFog3DLayer& GetLayer(const ATDFog3DZoneActor& Actor)
	{
		return Actor.Layer;
	}

	static float GetGroundCaptureDecodeOffset(const ATDFog3DZoneActor& Actor)
	{
		return Actor.GroundCaptureDecodeOffsetCm;
	}

	static double GetZoneFloorZ(const ATDFog3DZoneActor& Actor)
	{
		return Actor.GetZoneFloorZ();
	}

	static FVector GetZoneReferenceWorld(const ATDFog3DZoneActor& Actor)
	{
		return Actor.GetZoneReferenceWorld();
	}
};

#endif

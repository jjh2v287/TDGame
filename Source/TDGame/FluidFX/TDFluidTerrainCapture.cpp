#include "FluidFX/TDFluidTerrainCapture.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "LandscapeComponent.h"
#include "LandscapeNaniteComponent.h"
#include "LandscapeProxy.h"

const FName FTDFluidTerrainCapture::TerrainActorTag(TEXT("TDFluidTerrain"));

bool FTDFluidTerrainCapture::IsLandscapeSurfaceComponent(const UPrimitiveComponent& Component)
{
	return Component.IsA<ULandscapeComponent>() || Component.IsA<ULandscapeNaniteComponent>();
}

bool FTDFluidTerrainCapture::ShouldRenderInMainRenderer(ETDFluidHeightCaptureContent Content, bool bHasGameViewport)
{
	return Content == ETDFluidHeightCaptureContent::TerrainOnly && bHasGameViewport;
}

void FTDFluidTerrainCapture::ApplyTerrainOnlyList(USceneCaptureComponent2D& Capture, UWorld& World)
{
	Capture.PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
	Capture.bRenderInMainRenderer = ShouldRenderInMainRenderer(ETDFluidHeightCaptureContent::TerrainOnly, World.GetGameViewport() != nullptr);
	Capture.ShowOnlyActors.Reset();
	Capture.ClearShowOnlyComponents();
	for (TActorIterator<AActor> ActorIt(&World); ActorIt; ++ActorIt)
	{
		AActor* Actor = *ActorIt;
		if (Actor->IsA<ALandscapeProxy>())
		{
			Actor->ForEachComponent<UPrimitiveComponent>(false, [&Capture](UPrimitiveComponent* Component)
			{
				if (IsLandscapeSurfaceComponent(*Component))
				{
					Capture.ShowOnlyComponents.Add(Component);
				}
			});
			continue;
		}
		if (Actor->ActorHasTag(TerrainActorTag))
		{
			Capture.ShowOnlyActors.Add(Actor);
		}
	}
}

void FTDFluidTerrainCapture::HideOwnerAndPawns(USceneCaptureComponent2D& Capture, const AActor* Owner, bool bIgnoresPawns, UWorld& World)
{
	Capture.HiddenActors.Reset();
	if (Owner)
	{
		Capture.HiddenActors.Add(const_cast<AActor*>(Owner));
	}
	if (!bIgnoresPawns)
	{
		return;
	}
	for (TActorIterator<APawn> PawnIt(&World); PawnIt; ++PawnIt)
	{
		Capture.HiddenActors.Add(*PawnIt);
	}
}

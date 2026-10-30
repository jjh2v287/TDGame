#include "FluidFX/TDFluidLiveActor.h"

#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "FluidFX/TDFluidLiveComponent.h"
#include "FluidFX/TDFluidSimStateCacheAsset.h"
#include "FluidFX/TDFluidSimStateCacheTypes.h"
#include "HAL/IConsoleManager.h"

namespace TDFluidLiveActorSimStateCachePrivate
{
	TAutoConsoleVariable<int32> CVarTDFluidSimStateCache(
		TEXT("TD.FluidFX.SimStateCache"),
		1,
		TEXT("0 ignores simulation state caches assigned to fluid live actors."),
		ECVF_Default);
}

bool TDFluidSimStateCache::IsCacheConsoleEnabled()
{
	return TDFluidLiveActorSimStateCachePrivate::CVarTDFluidSimStateCache.GetValueOnGameThread() != 0;
}

void ATDFluidLiveActor::RequestSimStateCacheLoad()
{
	CancelSimStateCacheLoad();
	if (SimStateCache.IsNull() || !LiveComponent || !TDFluidSimStateCache::IsCacheConsoleEnabled())
	{
		return;
	}

	bIsAwaitingSimStateCache = true;
	SimStateCacheWaitSeconds = 0.0f;
	if (SimStateCache.Get())
	{
		HandleSimStateCacheLoaded();
		return;
	}

	SimStateCacheLoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
		SimStateCache.ToSoftObjectPath(),
		FStreamableDelegate::CreateUObject(this, &ATDFluidLiveActor::HandleSimStateCacheLoaded));
}

void ATDFluidLiveActor::HandleSimStateCacheLoaded()
{
	const bool bWasAwaiting = bIsAwaitingSimStateCache;
	bIsAwaitingSimStateCache = false;
	SimStateCacheLoadHandle.Reset();
	if (!bWasAwaiting || !LiveComponent)
	{
		return;
	}

	const UTDFluidSimStateCacheAsset* LoadedCache = SimStateCache.Get();
	if (!TDFluidSimStateCache::IsCacheConsoleEnabled() || !LoadedCache || !LoadedCache->IsUsable())
	{
		UE_LOG(LogTemp, Warning, TEXT("%s: simulation state cache %s is disabled, missing or unusable"), *GetName(), *SimStateCache.ToString());
		return;
	}

	LiveComponent->QueueSimStateSeed(LoadedCache->MakeSnapshot());
}

void ATDFluidLiveActor::CancelSimStateCacheLoad()
{
	bIsAwaitingSimStateCache = false;
	if (!SimStateCacheLoadHandle.IsValid())
	{
		return;
	}

	SimStateCacheLoadHandle->CancelHandle();
	SimStateCacheLoadHandle.Reset();
}

bool ATDFluidLiveActor::ShouldWaitForSimStateCache(float DeltaTime)
{
	if (!bIsAwaitingSimStateCache)
	{
		return false;
	}

	SimStateCacheWaitSeconds += DeltaTime;
	const float MaxWaitSeconds = LiveComponent ? LiveComponent->GetRuntimeOpsSettings().SimStateCacheMaxWaitSeconds : 0.0f;
	if (SimStateCacheWaitSeconds < MaxWaitSeconds)
	{
		return true;
	}

	UE_LOG(LogTemp, Warning, TEXT("%s: simulation state cache %s did not load within %.2f seconds, starting without it"), *GetName(), *SimStateCache.ToString(), MaxWaitSeconds);
	CancelSimStateCacheLoad();
	return false;
}

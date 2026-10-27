#include "FluidFX/TDFluidHeightCacheAsset.h"
#include "CollisionQueryParams.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FluidFX/TDFluidLiveActor.h"
#include "GameFramework/Pawn.h"
#include "Misc/Crc.h"

namespace
{
	constexpr float MaxEncodedStepCount = 65534.0f;
	const FName HeightCacheTraceIgnoreTag(TEXT("TDFluidTraceIgnore"));
	const FName HeightCacheWaterSurfaceTag(TEXT("TDFluidWaterSurface"));
}

uint16 UTDFluidHeightCacheAsset::EncodeHeight(float HeightCm, float MinZCm, float RangeCm)
{
	if (RangeCm <= 0.0f)
	{
		return 1;
	}

	const float Normalized = FMath::Clamp((HeightCm - MinZCm) / RangeCm, 0.0f, 1.0f);
	return static_cast<uint16>(1 + FMath::RoundToInt(Normalized * MaxEncodedStepCount));
}

float UTDFluidHeightCacheAsset::DecodeHeight(uint16 EncodedHeight, float MinZCm, float RangeCm)
{
	const float Step = static_cast<float>(FMath::Max<int32>(EncodedHeight, 1) - 1);
	return MinZCm + Step * RangeCm / MaxEncodedStepCount;
}

float UTDFluidHeightCacheAsset::GetQuantizationErrorCm(float RangeCm)
{
	return FMath::Max(RangeCm, 0.0f) / (2.0f * MaxEncodedStepCount);
}

void UTDFluidHeightCacheAsset::AddBakeIgnoredActors(UWorld& World, FCollisionQueryParams& InOutQueryParams)
{
	for (TActorIterator<AActor> ActorIt(&World); ActorIt; ++ActorIt)
	{
		const AActor* Candidate = *ActorIt;
		const bool bIsIgnoredByClass = Candidate->IsA<APawn>() || Candidate->IsA<ATDFluidLiveActor>();
		const bool bIsIgnoredByTag = Candidate->ActorHasTag(HeightCacheTraceIgnoreTag) || Candidate->ActorHasTag(HeightCacheWaterSurfaceTag);
		if (bIsIgnoredByClass || bIsIgnoredByTag)
		{
			InOutQueryParams.AddIgnoredActor(Candidate);
		}
	}
}

bool UTDFluidHeightCacheAsset::IsUsable() const
{
	return Resolution > 0 && TexelCm > 0.0f && EncodedHeights.Num() == Resolution * Resolution;
}

bool UTDFluidHeightCacheAsset::SampleHeightCm(const FVector2D& WorldXY, double& OutHeightCm) const
{
	if (!IsUsable())
	{
		return false;
	}

	const double LocalX = (WorldXY.X - OriginCm.X) / TexelCm;
	const double LocalY = (WorldXY.Y - OriginCm.Y) / TexelCm;
	if (LocalX < 0.0 || LocalY < 0.0 || LocalX > Resolution || LocalY > Resolution)
	{
		return false;
	}

	const double LastIndex = static_cast<double>(Resolution - 1);
	const double SampleX = FMath::Clamp(LocalX - 0.5, 0.0, LastIndex);
	const double SampleY = FMath::Clamp(LocalY - 0.5, 0.0, LastIndex);
	const int32 X0 = FMath::FloorToInt32(SampleX);
	const int32 Y0 = FMath::FloorToInt32(SampleY);
	const int32 X1 = FMath::Min(X0 + 1, Resolution - 1);
	const int32 Y1 = FMath::Min(Y0 + 1, Resolution - 1);
	const uint16 Taps[4] = {
		EncodedHeights[Y0 * Resolution + X0],
		EncodedHeights[Y0 * Resolution + X1],
		EncodedHeights[Y1 * Resolution + X0],
		EncodedHeights[Y1 * Resolution + X1]
	};
	for (const uint16 Tap : Taps)
	{
		if (Tap == MissEncodedHeight)
		{
			return false;
		}
	}

	const double FractionX = SampleX - X0;
	const double FractionY = SampleY - Y0;
	const double Bottom = FMath::Lerp(static_cast<double>(DecodeHeight(Taps[0], EncodingMinZCm, EncodingRangeCm)), static_cast<double>(DecodeHeight(Taps[1], EncodingMinZCm, EncodingRangeCm)), FractionX);
	const double Top = FMath::Lerp(static_cast<double>(DecodeHeight(Taps[2], EncodingMinZCm, EncodingRangeCm)), static_cast<double>(DecodeHeight(Taps[3], EncodingMinZCm, EncodingRangeCm)), FractionX);
	OutHeightCm = FMath::Lerp(Bottom, Top, FractionY);
	return true;
}

FVector4f UTDFluidHeightCacheAsset::GetHeightWindow() const
{
	const double SpanCm = static_cast<double>(Resolution) * TexelCm;
	const FVector2D CenterCm = OriginCm + FVector2D(0.5 * SpanCm);
	return FVector4f(static_cast<float>(CenterCm.X), static_cast<float>(CenterCm.Y), static_cast<float>(SpanCm), static_cast<float>(SpanCm));
}

UTexture2D* UTDFluidHeightCacheAsset::CreateTransientHeightTexture() const
{
	if (!IsUsable())
	{
		return nullptr;
	}

	TArray<float> Pixels;
	Pixels.SetNumUninitialized(EncodedHeights.Num());
	for (int32 Index = 0; Index < EncodedHeights.Num(); ++Index)
	{
		const uint16 Encoded = EncodedHeights[Index];
		Pixels[Index] = Encoded == MissEncodedHeight ? MissTextureHeightCm : DecodeHeight(Encoded, EncodingMinZCm, EncodingRangeCm);
	}

	const TConstArrayView64<uint8> ImageBytes(reinterpret_cast<const uint8*>(Pixels.GetData()), static_cast<int64>(Pixels.Num()) * sizeof(float));
	UTexture2D* HeightTexture = UTexture2D::CreateTransient(Resolution, Resolution, PF_R32_FLOAT, NAME_None, ImageBytes);
	if (!HeightTexture)
	{
		return nullptr;
	}

	HeightTexture->Filter = TF_Bilinear;
	HeightTexture->AddressX = TA_Clamp;
	HeightTexture->AddressY = TA_Clamp;
	HeightTexture->SRGB = false;
	HeightTexture->UpdateResource();
	return HeightTexture;
}

void UTDFluidHeightCacheAsset::SetEncodedFromHeights(const FVector2D& InOriginCm, int32 InResolution, float InTexelCm, TConstArrayView<float> HeightsCm, TConstArrayView<bool> HitMask)
{
	const int32 CellCount = InResolution * InResolution;
	if (InResolution <= 0 || HeightsCm.Num() != CellCount || HitMask.Num() != CellCount)
	{
		return;
	}

	float MinZCm = TNumericLimits<float>::Max();
	float MaxZCm = TNumericLimits<float>::Lowest();
	for (int32 Index = 0; Index < CellCount; ++Index)
	{
		if (!HitMask[Index])
		{
			continue;
		}
		MinZCm = FMath::Min(MinZCm, HeightsCm[Index]);
		MaxZCm = FMath::Max(MaxZCm, HeightsCm[Index]);
	}

	const bool bHasAnyHit = MinZCm <= MaxZCm;
	OriginCm = InOriginCm;
	Resolution = InResolution;
	TexelCm = InTexelCm;
	EncodingMinZCm = bHasAnyHit ? MinZCm : 0.0f;
	EncodingRangeCm = bHasAnyHit ? MaxZCm - MinZCm : 0.0f;
	EncodedHeights.SetNumUninitialized(CellCount);
	for (int32 Index = 0; Index < CellCount; ++Index)
	{
		EncodedHeights[Index] = HitMask[Index] ? EncodeHeight(HeightsCm[Index], EncodingMinZCm, EncodingRangeCm) : MissEncodedHeight;
	}
	SourceChecksum = static_cast<int64>(FCrc::MemCrc32(EncodedHeights.GetData(), EncodedHeights.Num() * sizeof(uint16)));
}

bool UTDFluidHeightCacheAsset::BakeFromLineTraces(UObject* WorldContextObject, FVector2D CenterCm, FVector2D ExtentCm, float InTexelCm, float TraceTopZCm, float TraceBottomZCm)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (!World || InTexelCm < 1.0f || TraceTopZCm <= TraceBottomZCm)
	{
		return false;
	}

	const double SpanCm = FMath::Max(ExtentCm.X, ExtentCm.Y);
	if (SpanCm < InTexelCm)
	{
		return false;
	}

	const int32 BakeResolution = FMath::Min(FMath::CeilToInt32(SpanCm / InTexelCm), MaxBakeResolution);
	const FVector2D BakeOriginCm = CenterCm - FVector2D(0.5 * BakeResolution * InTexelCm);
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(TDFluidHeightCacheBake), false);
	AddBakeIgnoredActors(*World, QueryParams);
	const FCollisionObjectQueryParams ObjectQueryParams(ECC_WorldStatic);

	const int32 CellCount = BakeResolution * BakeResolution;
	TArray<float> HeightsCm;
	HeightsCm.SetNumZeroed(CellCount);
	TArray<bool> HitMask;
	HitMask.SetNumZeroed(CellCount);
	for (int32 CellY = 0; CellY < BakeResolution; ++CellY)
	{
		for (int32 CellX = 0; CellX < BakeResolution; ++CellX)
		{
			const FVector2D TexelCenter = BakeOriginCm + FVector2D((CellX + 0.5) * InTexelCm, (CellY + 0.5) * InTexelCm);
			FHitResult Hit;
			const int32 Index = CellY * BakeResolution + CellX;
			HitMask[Index] = World->LineTraceSingleByObjectType(Hit, FVector(TexelCenter, TraceTopZCm), FVector(TexelCenter, TraceBottomZCm), ObjectQueryParams, QueryParams);
			HeightsCm[Index] = HitMask[Index] ? static_cast<float>(Hit.ImpactPoint.Z) : 0.0f;
		}
	}

	SetEncodedFromHeights(BakeOriginCm, BakeResolution, InTexelCm, HeightsCm, HitMask);
	SourceLevel = World->GetMapName();
	MarkPackageDirty();
	return true;
}

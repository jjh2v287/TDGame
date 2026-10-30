float2 extentCm = max(SimExtentsCm.xy, float2(1.0, 1.0));
float2 windowMinimum = SimCenterWorld.xy - 0.5 * extentCm;
float2 planeCoordinate = float2(dot(PixelWorldPosition, PlaneAxisU), dot(PixelWorldPosition, PlaneAxisV)) + PlaneCoordinateOffsetCm.xy;
float2 windowUv = (planeCoordinate - windowMinimum) / extentCm;
float windowMask = any(abs(windowUv - 0.5) > 0.5005) ? 0.0 : 1.0;

float4 densitySample = Texture2DSampleLevel(DensityPressureTexture, DensityPressureTextureSampler, windowUv, 0);
float4 velocitySample = Texture2DSampleLevel(VelocityTexture, VelocityTextureSampler, windowUv, 0);
float2 flowCmPerSec = (velocitySample.rg * 2.0 - 1.0) * VelocitySpeedReferenceCmPerSec;

float cycleSeconds = max(SheetFlowCycleSeconds, 0.05);
float phaseFirst = frac(ElapsedSeconds / cycleSeconds);
float phaseSecond = frac(phaseFirst + 0.5);
float weightFirst = 1.0 - abs(1.0 - 2.0 * phaseFirst);

float2 streakScale = float2(1.0, 1.0 / max(SheetStreakStretch, 1.0)) / max(SheetStreakScaleCm, 1.0);
float2 foamCoordinateFirst = (planeCoordinate - flowCmPerSec * (phaseFirst * cycleSeconds)) * streakScale;
float2 foamCoordinateSecond = (planeCoordinate - flowCmPerSec * (phaseSecond * cycleSeconds)) * streakScale;
float foamNoiseFirst = Texture2DSample(FoamNoiseTexture, FoamNoiseTextureSampler, foamCoordinateFirst).r;
float foamNoiseSecond = Texture2DSample(FoamNoiseTexture, FoamNoiseTextureSampler, foamCoordinateSecond).r;
float foamNoise = lerp(foamNoiseSecond, foamNoiseFirst, weightFirst);

float densityValue = saturate(densitySample.r);
float foamAmount = saturate(foamNoise * densityValue * SheetDensityContrast);
float sheetOpacity = saturate(saturate(densityValue * SheetOpacityScale) * (SheetNoiseOpacityBase + SheetNoiseOpacityGain * foamNoise));
float3 sheetColor = lerp(SheetTint, SheetFoamColor, foamAmount);

return float4(sheetColor, sheetOpacity) * windowMask;

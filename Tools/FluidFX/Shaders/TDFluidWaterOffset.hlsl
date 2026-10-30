float swashOffsetCm = 0.0;

if (waterLayerBlend > 0.0 && SwashHeightCm > 0.0 && simField[0].z >= 0.0)
{
    float coastDepthCm = simField[0].z * WaterDepthReferenceCm;
    float coastMaximumDepthCm = max(CoastWaveMaxDepthCm, 1.0);
    float swashPhase = TD_WATER_TWO_PI * (coastDepthCm / max(CoastWaveDepthSpacingCm, 1.0) + CoastWavePhase);
    float swashShoal = 1.0 - smoothstep(0.6 * coastMaximumDepthCm, coastMaximumDepthCm, coastDepthCm);
    swashOffsetCm = SwashHeightCm * max(0.0, sin(swashPhase)) * swashShoal * simFade * waterLayerBlend;
}

float denseActive = saturate(DenseWaterActive);
float denseDepthCm = simField[0].z * WaterDepthReferenceCm;
float denseWetWeight = step(max(DenseWaterWetThresholdCm, 0.001), denseDepthCm) * windowDepthKnown;
float denseTerrainKnown = 1.0 - step(waterTerrainUnknownLimit - 0.01, simTerrainNormalized);
float denseTerrainZCm = TerrainReferenceZCm + simTerrainNormalized * waterTerrainScaleCm;
float denseTargetZCm = denseTerrainZCm + lerp(-DenseDrySinkCm, denseDepthCm, denseWetWeight);
float denseOffsetZCm = (denseTargetZCm - WaterWorldPosition.z) * denseActive * windowDepthKnown * denseTerrainKnown;

return float3(waveDisplacementCm.xy + lodMorphDeltaXY, waveDisplacementCm.z + rippleHeightCm + swashOffsetCm + tileMapHeightCm + denseOffsetZCm);

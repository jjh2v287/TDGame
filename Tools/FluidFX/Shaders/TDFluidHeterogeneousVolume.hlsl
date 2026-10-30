float tdVolIsDriven = step(0.5, SimGridResolution);
float tdVolIsSymmetric = step(0.5, ExtrudeSymmetric) * tdVolIsDriven;
float3 tdVolPlanePosition = TD_VOL_TO_PLANE(VolWorldPosition, PlaneAxisU, PlaneAxisV, PlaneAxisN) + float3(PlaneCoordinateOffsetCm.xy, 0.0) - float3(VolumeOffsetCm.xy, tdVolIsSymmetric * VolumeOffsetCm.z);
float3 tdVolQ = lerp(VolWorldPosition, tdVolPlanePosition, tdVolIsDriven);
float3 tdVolCenter = tdVolIsDriven > 0.5 ? SimCenterWorld : float3(VolObjectPosition.xy, VolObjectPosition.z - VolObjectBoundsCm.z);
float2 tdVolSize = tdVolIsDriven > 0.5 ? SimExtentsCm.xy : 2.0 * VolObjectBoundsCm.xy;
float2 tdVolUv = TD_VOL_WINDOW_UV(tdVolQ.xy, tdVolCenter.xy, tdVolSize);
float tdVolEdgeCm = TD_VOL_EDGE_DISTANCE_CM(tdVolUv, tdVolSize);

VolAlbedoOut = VolAlbedo;
VolEmissiveOut = float3(0.0, 0.0, 0.0);
VolAmbientOcclusionOut = 1.0;
if (tdVolEdgeCm <= 0.0)
{
    return float4(0.0, 0.0, 0.0, 0.0);
}

float tdVolEdgeWeight = TD_VOL_EDGE_WEIGHT(tdVolEdgeCm, VolEdgeFadeCm);
float3 tdVolWind = ElapsedSeconds * VolNoiseWindCmPerSec;
float tdVolSource = 0.0;
if (tdVolIsDriven > 0.5)
{
    tdVolSource = TD_VOL_SIM_DENSITY(Texture2DSampleLevel(DensityPressureTexture, DensityPressureTextureSampler, saturate(tdVolUv), 0));
}
else
{
    float tdVolCoverage = saturate(VolPassiveCoverage);
    tdVolSource = smoothstep(1.0 - tdVolCoverage - 0.15, 1.0 - tdVolCoverage + 0.15, TD_VOL_NOISE(VolWorldPosition + tdVolWind, VolPassiveNoiseScaleCm));
}

float4 tdVolColorInput = float4(VolAlbedo, 1.0);
if (VolColorInputBlend + VolColorInputHeightFromAlpha > 0.0001)
{
    tdVolColorInput = Texture2DSampleLevel(VolColorInputTexture, VolColorInputTextureSampler, saturate(tdVolUv), 0);
}
tdVolSource *= lerp(1.0, saturate(tdVolColorInput.a), saturate(VolColorInputHeightFromAlpha));

float tdVolFloorCm = tdVolCenter.z;
if (TerrainHeightDecode.z > 0.5 && VolTerrainAlignBlend > 0.0 && tdVolIsSymmetric < 0.5)
{
    float2 tdVolTerrainUv = (VolWorldPosition.xy - TerrainHeightWindow.xy) / max(TerrainHeightWindow.zw, float2(1.0, 1.0)) + 0.5;
    float tdVolTerrainInside = step(0.0, min(min(tdVolTerrainUv.x, tdVolTerrainUv.y), min(1.0 - tdVolTerrainUv.x, 1.0 - tdVolTerrainUv.y)));
    float tdVolTerrainCm = TD_VOL_TERRAIN_HEIGHT(Texture2DSampleLevel(TerrainHeightTexture, TerrainHeightTextureSampler, saturate(tdVolTerrainUv), 0).r, TerrainHeightDecode);
    float tdVolTerrainValid = tdVolTerrainInside * step(tdVolFloorCm - max(VolTerrainMaxDropCm, 0.0), tdVolTerrainCm);
    tdVolFloorCm = lerp(tdVolFloorCm, tdVolTerrainCm, saturate(VolTerrainAlignBlend) * tdVolTerrainValid);
}

float tdVolHeightCm = tdVolIsSymmetric > 0.5 ? abs(tdVolQ.z - tdVolCenter.z) : max(tdVolQ.z - tdVolFloorCm, 0.0);
float tdVolExtrudeCm = TD_VOL_EXTRUDE_TOP_CM(tdVolSource, VolExtrudeHeightCm, VolExtrudeExponent);
float tdVolTopCm = tdVolIsSymmetric > 0.5 ? 0.5 * tdVolExtrudeCm : tdVolExtrudeCm;
float tdVolShape = tdVolSource * TD_VOL_LAYER_PROFILE(tdVolTopCm, VolTopFadeCm, tdVolHeightCm);
float tdVolOcclusionTopCm = tdVolTopCm;

float2 tdVolAxisOffset = tdVolQ.xy - (tdVolCenter.xy + VolCrossSectionAxisOffsetCm.xy);
float tdVolAxisDistanceCm = length(tdVolAxisOffset);
float tdVolSectionMode = round(VolCrossSectionMode);
if (tdVolSectionMode > 0.5)
{
    float tdVolSectionV = saturate(tdVolHeightCm / max(VolCrossSectionHeightCm, 1.0));
    float tdVolRadiusLimitCm = 0.5 * min(tdVolSize.x, tdVolSize.y);
    float tdVolSectionU = tdVolSectionMode < 1.5 ? TD_VOL_RADIAL_U(tdVolAxisDistanceCm, tdVolRadiusLimitCm, VolCrossSectionRadialExponent) : (tdVolSectionMode < 2.5 ? saturate(tdVolUv.x) : saturate(tdVolSource));
    float tdVolSection = saturate(Texture2DSampleLevel(CrossSectionTexture, CrossSectionTextureSampler, float2(tdVolSectionU, tdVolSectionV), 0).r);
    tdVolShape = tdVolSectionMode < 2.5 ? tdVolSource * tdVolSection : tdVolSection;
    tdVolOcclusionTopCm = VolCrossSectionHeightCm;
}

float tdVolNoise = 0.0;
if (VolNoiseSubtract > 0.0)
{
    float3 tdVolNoisePosition = VolNoiseInWorldSpace > 0.5 ? VolWorldPosition : tdVolQ;
    if (VolPolarNoise > 0.5 && tdVolAxisDistanceCm > 1.0)
    {
        tdVolNoisePosition = TD_VOL_POLAR(tdVolAxisOffset, tdVolNoisePosition, VolPolarAngularScaleCm);
    }
    float3 tdVolNoiseBase = tdVolNoisePosition + tdVolWind;
    if (VolFlowStrength > 0.0 && tdVolIsDriven > 0.5)
    {
        float tdVolCycleSeconds = max(VolFlowCycleSeconds, 0.05);
        float tdVolPhaseFirst = frac(ElapsedSeconds / tdVolCycleSeconds);
        float tdVolPhaseSecond = frac(tdVolPhaseFirst + 0.5);
        float2 tdVolFlowCmPerSec = (Texture2DSampleLevel(VelocityTexture, VelocityTextureSampler, saturate(tdVolUv), 0).rg * 2.0 - 1.0) * (VolFlowVelocityReferenceCmPerSec * VolFlowStrength);
        float tdVolFlowWeight = TD_VOL_FLOW_WEIGHT(tdVolPhaseFirst);
        float tdVolFlowFirst = TD_VOL_NOISE(tdVolNoiseBase - float3(tdVolFlowCmPerSec * (tdVolPhaseFirst * tdVolCycleSeconds), 0.0), VolNoiseScaleCm);
        float tdVolFlowSecond = TD_VOL_NOISE(tdVolNoiseBase - float3(tdVolFlowCmPerSec * (tdVolPhaseSecond * tdVolCycleSeconds), 0.0), VolNoiseScaleCm);
        tdVolNoise = tdVolFlowWeight * tdVolFlowFirst + (1.0 - tdVolFlowWeight) * tdVolFlowSecond;
    }
    else
    {
        tdVolNoise = TD_VOL_NOISE(tdVolNoiseBase, VolNoiseScaleCm);
    }
    if (VolNoiseMix > 0.0)
    {
        tdVolNoise = lerp(tdVolNoise, TD_VOL_NOISE(tdVolNoisePosition + 0.5 * tdVolWind, VolNoiseSecondScaleCm), saturate(VolNoiseMix));
    }
}
float tdVolBase = saturate(tdVolShape - VolNoiseSubtract * tdVolNoise);

float tdVolHeightFade = TD_VOL_HEIGHT_FADE(VolWorldPosition.z, VolHeightFadeStartCm, VolHeightFadeLengthCm);
float tdVolCameraFade = TD_VOL_CAMERA_FADE(length(VolWorldPosition - VolCameraPosition), VolCameraFadeStartCm, VolCameraFadeLengthCm, VolCameraFadeInvert);
float tdVolDensity = tdVolBase * tdVolEdgeWeight * tdVolHeightFade * tdVolCameraFade;

VolAlbedoOut = lerp(VolAlbedo, tdVolColorInput.rgb, saturate(VolColorInputBlend));
VolAmbientOcclusionOut = TD_VOL_OCCLUSION(tdVolHeightCm, tdVolOcclusionTopCm, saturate(VolAmbientFloor));
if (AuxHeatActive > 0.5 && VolBlackbodyIntensity > 0.0 && tdVolIsDriven > 0.5)
{
    float tdVolHeat = saturate(Texture2DSampleLevel(AuxTexture, AuxTextureSampler, saturate(tdVolUv), 0).r);
    VolEmissiveOut = TD_VOL_BLACKBODY(tdVolHeat * VolBlackbodyTemperatureK) * (VolBlackbodyIntensity * pow(tdVolHeat, max(VolBlackbodyExponent, 0.01)) * tdVolDensity);
}
float tdVolExtinction = VolExtinctionPerM * tdVolDensity;
return float4(tdVolExtinction, tdVolExtinction, tdVolExtinction, tdVolDensity);

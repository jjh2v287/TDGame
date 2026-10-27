#define TD_FOG_NOISE(P, SCALE_CM) Texture3DSampleLevel(NoiseVolumeTexture, NoiseVolumeTextureSampler, (P) / max((SCALE_CM), 1.0), 0).r
#define TDFogFloorHeightCm(IS_DRIVEN) lerp(FogObjectPosition.z - FogObjectBoundsCm.z, SimCenterWorld.z, (IS_DRIVEN))

float fogIsDriven = step(0.5, SimGridResolution);
float fogAreaWrapped = fogIsDriven * step(0.5, SimAreaWrapped);
float3 fogWindowCenter = lerp(FogWindowCenterWorld, SimCenterWorld, fogIsDriven);
float3 fogWindowExtent = lerp(FogWindowExtentsCm, SimExtentsCm, fogIsDriven);
float2 fogWindowSize = max(fogWindowExtent.xy, float2(1.0, 1.0));
float2 fogWindowUv = (FogWorldPosition.xy - fogWindowCenter.xy) / fogWindowSize + 0.5;
float2 fogEdgeCm = (0.5 - abs(fogWindowUv - 0.5)) * fogWindowSize;
float fogEdgeDistanceCm = min(fogEdgeCm.x, fogEdgeCm.y);
float fogHasWindow = step(0.5, fogWindowExtent.x) * step(0.5, fogWindowExtent.y);
float fogBlendWidthCm = max(FogWindowBlendRatio * min(fogWindowSize.x, fogWindowSize.y), 1.0);
float fogWindowWeight = fogHasWindow * smoothstep(0.0, fogBlendWidthCm, fogEdgeDistanceCm);
fogWindowWeight = lerp(fogWindowWeight, 1.0, fogAreaWrapped);

float fogFloorCm = TDFogFloorHeightCm(fogIsDriven);
float fogHeightCm = FogWorldPosition.z - fogFloorCm;
float3 fogWindOffsetCm = ElapsedSeconds * FogNoiseWindCmPerSec;
float fogShapeSoftCm = max(FogShapeSoftCm, 1.0);

float fogSimDensity = 0.0;
if (fogIsDriven > 0.5 && (fogEdgeDistanceCm >= 0.0 || fogAreaWrapped > 0.5))
{
    float4 fogSimTap = Texture2DSampleLevel(DensityPressureTexture, DensityPressureTextureSampler, fogAreaWrapped > 0.5 ? fogWindowUv : saturate(fogWindowUv), 0);
    fogSimDensity = saturate(fogSimTap.r) * saturate(fogSimTap.a);
}

float fogPassiveDensity = 0.0;
if (fogIsDriven < 0.5)
{
    float fogCoverage = saturate(FogPassiveCoverage);
    float fogPassiveNoise = TD_FOG_NOISE(FogWorldPosition + fogWindOffsetCm, FogPassiveNoiseScaleCm);
    float fogPattern = smoothstep(1.0 - fogCoverage - 0.15, 1.0 - fogCoverage + 0.15, fogPassiveNoise);
    float3 fogLocalCm = FogWorldPosition - FogObjectPosition;
    float3 fogHalfCm = max(FogObjectBoundsCm, float3(1.0, 1.0, 1.0));
    float2 fogInsideCm = fogHalfCm.xy - abs(fogLocalCm.xy);
    float fogSlabMask = smoothstep(0.0, fogShapeSoftCm, min(fogInsideCm.x, fogInsideCm.y));
    float fogCylinderRadiusCm = min(fogHalfCm.x, fogHalfCm.y);
    float fogCylinderMask = 1.0 - smoothstep(fogCylinderRadiusCm - fogShapeSoftCm, fogCylinderRadiusCm, length(fogLocalCm.xy));
    float fogSphereRadiusCm = min(fogCylinderRadiusCm, fogHalfCm.z);
    float fogShellInnerCm = fogSphereRadiusCm - max(FogShellThicknessCm, 1.0);
    float fogSphereDistanceCm = length(fogLocalCm);
    float fogShellMask = smoothstep(fogShellInnerCm - fogShapeSoftCm, fogShellInnerCm, fogSphereDistanceCm) * (1.0 - smoothstep(fogSphereRadiusCm - fogShapeSoftCm, fogSphereRadiusCm, fogSphereDistanceCm));
    float fogShapeMask = FogShape < 0.5 ? fogSlabMask : (FogShape < 1.5 ? fogCylinderMask : fogShellMask);
    fogPassiveDensity = fogPattern * fogShapeMask;
}

float fogSource = saturate(lerp(fogPassiveDensity, fogSimDensity, fogIsDriven));
float fogTopCm = FogExtrudeHeightCm * pow(fogSource, max(FogExtrudeExponent, 0.01));
float fogLayerProfile = fogTopCm > 0.0 ? 1.0 - smoothstep(fogTopCm - max(FogTopFadeCm, 1.0), fogTopCm, fogHeightCm) : 0.0;
float fogUsesLayerProfile = (fogIsDriven > 0.5 || FogShape < 1.5) ? 1.0 : 0.0;
float fogProfile = lerp(1.0, fogLayerProfile, fogUsesLayerProfile);

float fogErosionNoise = TD_FOG_NOISE(FogWorldPosition + fogWindOffsetCm, FogNoiseScaleCm);
float fogBase = saturate(fogSource * fogProfile - FogNoiseSubtract * fogErosionNoise);

float fogRoleWeight = fogIsDriven > 0.5 ? fogWindowWeight : (FogUsesWindowMask > 0.5 ? 1.0 - fogWindowWeight : 1.0);
float fogCameraDistanceCm = length(FogWorldPosition - FogCameraPosition);
float fogCameraRamp = smoothstep(FogCameraFadeStartCm, FogCameraFadeStartCm + max(FogCameraFadeLengthCm, 0.001), fogCameraDistanceCm);
float fogCameraFade = FogCameraFadeLengthCm > 0.0 ? (FogCameraFadeInvert > 0.5 ? 1.0 - fogCameraRamp : fogCameraRamp) : 1.0;
float fogDensity = fogBase * fogRoleWeight * fogCameraFade;

return float4(FogExtinctionPerM * FogExtinctionTint * fogDensity, fogDensity);

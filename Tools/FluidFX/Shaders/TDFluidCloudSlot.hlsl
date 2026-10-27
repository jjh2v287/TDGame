SlotScatterAlbedo = float3(0.0, 0.0, 0.0);
SlotEmissive = float3(0.0, 0.0, 0.0);
if (SlotSource < 0.5)
{
    return float4(0.0, 0.0, 0.0, 0.0);
}

float3 cloudP = CloudWorldPosition;
float cloudShape = round(SlotShape);
float cloudPattern = round(SlotPattern);
float2 cloudCenterCm = SlotFootprintCenterKm.xy * TD_CLOUD_KM;
float2 cloudSizeCm = max(SlotFootprintSizeKm.xy, float2(0.0, 0.0)) * TD_CLOUD_KM;
float cloudPatternScaleCm = max(SlotPatternScaleKm * TD_CLOUD_KM, 1.0);
float cloudHasFootprint = step(1e-6, SlotFootprintSizeKm.x);
float2 cloudOffsetCm = cloudP.xy - cloudCenterCm;
float2 cloudUv = cloudHasFootprint > 0.5 ? cloudOffsetCm / max(cloudSizeCm, float2(1.0, 1.0)) + 0.5 : frac(cloudP.xy / cloudPatternScaleCm);
float cloudYawRadians = radians(SlotSectionYawDegrees);
float2 cloudLinearDir = float2(cos(cloudYawRadians), sin(cloudYawRadians));
float cloudLinearU = dot(cloudOffsetCm, cloudLinearDir) / max(cloudSizeCm.x, 1.0) + 0.5;
float cloudIsLinear = (cloudShape > 1.5 && cloudShape < 2.5) ? 1.0 : 0.0;
float cloudEdge = lerp(TD_CLOUD_EDGE(cloudUv), min(cloudLinearU, 1.0 - cloudLinearU), cloudIsLinear);
float cloudEdgeRamp = TD_CLOUD_SAFE_POW(saturate(cloudEdge / max(0.5 * SlotEdgeMaskWidthRatio, 1e-4)), SlotEdgeMaskPower);
float cloudEdgeMask = cloudHasFootprint > 0.5 ? (cloudEdge <= 0.0 ? 0.0 : cloudEdgeRamp) : 1.0;
float3 cloudFlowCm = float3(SlotWindKmPerSec.xy, 0.0) * (TD_CLOUD_KM * ElapsedSeconds);

float2 cloudLobeCell = frac(cloudP.xy / cloudPatternScaleCm) - 0.5;
float cloudLobe = sqrt(saturate(1.0 - 4.0 * dot(cloudLobeCell, cloudLobeCell)));
float cloudCoverageRaw = 1.0;
if (SlotSource < 1.5)
{
    float4 cloudCoverageTap = TD_CLOUD_COVERAGE(cloudHasFootprint > 0.5 ? saturate(cloudUv) : cloudUv);
    cloudCoverageRaw = max(cloudCoverageTap.r, cloudCoverageTap.b);
}
else if (SlotSource < 2.5)
{
    float2 cloudPatternQ = (cloudP.xy - cloudFlowCm.xy) / cloudPatternScaleCm;
    float cloudN2 = TD_CLOUD_PATTERN_NOISE(cloudPatternQ);
    float cloudKnob = saturate(SlotPatternCoverage);
    float cloudStratus = smoothstep(1.0 - cloudKnob - 0.15, 1.0 - cloudKnob + 0.15, cloudN2);
    float cloudNimbus = saturate(cloudKnob + 0.35 * (cloudN2 - 0.5));
    float cloudCumulus = smoothstep(1.0 - cloudKnob - 0.08, 1.0 - cloudKnob + 0.08, cloudN2) * (0.6 + 0.4 * TD_CLOUD_PATTERN_NOISE(cloudPatternQ * 4.0));
    cloudCoverageRaw = cloudPattern < 0.5 ? cloudStratus : (cloudPattern < 1.5 ? cloudNimbus : (cloudPattern < 2.5 ? cloudStratus : cloudCumulus));
}
float cloudThreshold = SlotCoverageThreshold;
float cloudCoverage = saturate((cloudCoverageRaw * SlotCoverageScale - cloudThreshold) / max(1.0 - cloudThreshold, 1e-3));

float cloudTerrainCm = SlotTerrainFollow > 0.0 ? TD_CLOUD_TERRAIN_HEIGHT_CM(cloudP.xy, CloudTerrainFootprintKm, CloudTerrainHeightRangeKm) : 0.0;
float cloudThicknessCm = max(SlotThicknessKm, 0.0) * TD_CLOUD_KM;
float cloudIsMammatus = (cloudPattern > 1.5 && cloudPattern < 2.5) ? 1.0 : 0.0;
float cloudBottomCm = SlotBottomKm * TD_CLOUD_KM + SlotTerrainFollow * cloudTerrainCm - cloudIsMammatus * SlotLobeDepthRatio * cloudThicknessCm * cloudLobe;
float cloudHeightCm = CloudAltitudeCm - cloudBottomCm;
float cloudV = cloudHeightCm / max(cloudThicknessCm, 1.0);
float cloudInBand = step(0.0, cloudHeightCm) * step(cloudHeightCm, cloudThicknessCm);

float cloudDensity = 0.0;
if (cloudShape < 0.5)
{
    float cloudTopCm = cloudThicknessCm * TD_CLOUD_SAFE_POW(cloudCoverage, SlotTopExponent);
    float cloudRise = smoothstep(0.0, max(SlotBottomFadeRatio * cloudThicknessCm, 1.0), cloudHeightCm);
    float cloudCap = 1.0 - smoothstep(cloudTopCm - max(SlotTopFadeRatio * cloudThicknessCm, 1.0), cloudTopCm, cloudHeightCm);
    cloudDensity = cloudTopCm > 0.0 ? cloudCoverage * cloudRise * cloudCap * step(0.0, cloudHeightCm) : 0.0;
}
else if (cloudShape < 1.5)
{
    float cloudRadialU = TD_CLOUD_SAFE_POW(saturate(length(cloudOffsetCm) / max(0.5 * cloudSizeCm.x, 1.0)), SlotRadialUExponent);
    cloudDensity = cloudCoverage * TD_CLOUD_SECTION(float2(cloudRadialU, cloudV)) * cloudInBand;
}
else if (cloudShape < 2.5)
{
    float cloudAcrossCm = abs(dot(cloudOffsetCm, float2(-cloudLinearDir.y, cloudLinearDir.x)));
    float cloudDepthFade = 1.0 - smoothstep(0.0, max(0.5 * SlotSectionDepthKm * TD_CLOUD_KM, 1.0), cloudAcrossCm);
    float cloudInU = step(0.0, cloudLinearU) * step(cloudLinearU, 1.0);
    cloudDensity = cloudCoverage * TD_CLOUD_SECTION(float2(cloudLinearU, cloudV)) * cloudDepthFade * cloudInBand * cloudInU;
}
else
{
    cloudDensity = TD_CLOUD_SECTION(float2(cloudCoverageRaw, cloudV)) * cloudInBand * (cloudCoverageRaw > cloudThreshold ? 1.0 : 0.0);
}

float cloudEroded = 0.0;
if (cloudDensity > 0.0)
{
    float3 cloudDetailX = (cloudP - cloudFlowCm) / max(SlotDetailNoiseScaleKm * TD_CLOUD_KM, 1.0);
    float cloudFirst = saturate(cloudDensity - SlotDetailErosion * (1.0 - TD_CLOUD_DETAIL_NOISE(cloudDetailX)) * (1.0 - cloudDensity));
    cloudEroded = saturate(cloudFirst - SlotEdgeErosion * (1.0 - TD_CLOUD_NOISE(cloudDetailX * 4.1)) * (1.0 - smoothstep(0.0, 0.35, cloudFirst)));
}

float cloudFadeStartCm = CloudCameraFadeStartKm * TD_CLOUD_KM;
float cloudFadeEndCm = CloudCameraFadeEndKm * TD_CLOUD_KM;
float cloudCameraFade = cloudFadeEndCm > cloudFadeStartCm ? smoothstep(cloudFadeStartCm, cloudFadeEndCm, length(cloudP - CloudCameraPosition)) : 1.0;
float cloudWeight = cloudEdgeMask * cloudCameraFade;
float cloudSigma = max(SlotExtinctionPerM, 0.0) * cloudEroded * cloudWeight;
SlotScatterAlbedo = cloudSigma * saturate(SlotAlbedo);
SlotEmissive = max(SlotEmissiveColor, float3(0.0, 0.0, 0.0)) * max(SlotEmissiveIntensity, 0.0) * TD_CLOUD_SAFE_POW(cloudEroded, SlotEmissiveExponent) * cloudWeight;
float cloudConservative = (cloudShape > 2.5 ? (cloudCoverageRaw > cloudThreshold ? 1.0 : 0.0) : cloudCoverage) * cloudEdgeMask * cloudInBand;
return float4(cloudSigma, cloudConservative, cloudEroded * cloudWeight, 0.0);

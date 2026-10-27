#define TD_TRACK_HEIGHT_AT(XY, OUT_HEIGHT, OUT_DEPTH) { float4 trackMacroTap = TD_TRACK_FETCH(((XY) - trackWindowCenter) / trackWindowSize + 0.5); OUT_DEPTH = TD_TRACK_DEPTH_FROM(trackMacroTap); float trackMacroHeightCm = lerp(trackOutsideCm, TD_TRACK_INSIDE(trackMacroTap, OUT_DEPTH), trackEdgeBlend); OUT_HEIGHT = trackDeformActive > 0.5 ? max(trackDeformBlend * trackMacroHeightCm, TrackDeformFloorBiasCm) : trackMacroHeightCm; }

float3 trackViewVector = TrackPixelPosition - CameraWorldPosition;
float trackViewLengthCm = max(length(trackViewVector), 0.001);
float3 trackViewDirection = trackViewVector / trackViewLengthCm;
float trackRasterHeightCm = TrackPixelPosition.z - TrackRestPosition.z;
float trackHitOffsetCm = 0.0;
float trackPomActive = (trackIsDriven > 0.5 && TrackPomMaxDepthCm > 0.0 && trackViewLengthCm < TrackPomFadeEndCm && trackViewDirection.z < -0.05 && trackEdgeDistanceCm >= TrackFloorClipInsetCm && trackDeformEdgeCm >= TrackFloorClipInsetCm) ? 1.0 : 0.0;
if (trackPomActive > 0.5)
{
    float trackPomSteps = clamp(round(lerp(TrackPomMinSteps, TrackPomMaxSteps, 1.0 - abs(trackViewDirection.z))), 1.0, 64.0);
    float trackPomHalfSpanCm = TrackPomMaxDepthCm / max(abs(trackViewDirection.z), 0.2);
    float trackPomStepCm = 2.0 * trackPomHalfSpanCm / trackPomSteps;
    float trackPreviousOffsetCm = -trackPomHalfSpanCm;
    float trackPreviousHeightCm = 0.0;
    float trackPreviousDepth = 0.0;
    TD_TRACK_HEIGHT_AT(TrackPixelPosition.xy + trackViewDirection.xy * trackPreviousOffsetCm, trackPreviousHeightCm, trackPreviousDepth);
    float trackPreviousGapCm = trackRasterHeightCm + trackViewDirection.z * trackPreviousOffsetCm - trackPreviousHeightCm;
    trackHitOffsetCm = trackPreviousGapCm <= 0.0 ? trackPreviousOffsetCm : 0.0;
    [loop]
    for (int trackPomIndex = 1; trackPomIndex <= 64; ++trackPomIndex)
    {
        if (trackPreviousGapCm <= 0.0 || float(trackPomIndex) > trackPomSteps)
        {
            break;
        }
        float trackStepOffsetCm = -trackPomHalfSpanCm + trackPomStepCm * float(trackPomIndex);
        float trackStepHeightCm = 0.0;
        float trackStepDepth = 0.0;
        TD_TRACK_HEIGHT_AT(TrackPixelPosition.xy + trackViewDirection.xy * trackStepOffsetCm, trackStepHeightCm, trackStepDepth);
        float trackStepGapCm = trackRasterHeightCm + trackViewDirection.z * trackStepOffsetCm - trackStepHeightCm;
        if (trackStepGapCm <= 0.0)
        {
            trackHitOffsetCm = trackPreviousOffsetCm + trackPomStepCm * trackPreviousGapCm / max(trackPreviousGapCm - trackStepGapCm, 0.0001);
        }
        trackPreviousOffsetCm = trackStepOffsetCm;
        trackPreviousGapCm = trackStepGapCm;
    }
}

float2 trackHitXY = TrackPixelPosition.xy + trackViewDirection.xy * trackHitOffsetCm;
float trackHitHeightCm = trackRasterHeightCm + trackViewDirection.z * trackHitOffsetCm;
float trackHitDepth = trackDepth;
if (trackIsDriven > 0.5)
{
    float trackHitSurfaceCm = 0.0;
    TD_TRACK_HEIGHT_AT(trackHitXY, trackHitSurfaceCm, trackHitDepth);
}

float trackShadow = 1.0;
if (trackPomActive > 0.5 && TrackPomShadowStrength > 0.0 && TrackPomShadowSteps >= 1.0)
{
    float3 trackTowardLight = length(TrackSunDirectionWS) > 0.5 ? normalize(TrackSunDirectionWS) : -normalize(TrackShadowLightDirection);
    if (trackTowardLight.z > 0.02)
    {
        float trackShadowSpanCm = TrackPomMaxDepthCm / max(trackTowardLight.z, 0.2);
        float trackOcclusion = 0.0;
        [loop]
        for (int trackShadowIndex = 1; trackShadowIndex <= 16; ++trackShadowIndex)
        {
            if (float(trackShadowIndex) > TrackPomShadowSteps)
            {
                break;
            }
            float trackShadowOffsetCm = trackShadowSpanCm * float(trackShadowIndex) / TrackPomShadowSteps;
            float trackShadowSurfaceCm = 0.0;
            float trackShadowDepth = 0.0;
            TD_TRACK_HEIGHT_AT(trackHitXY + trackTowardLight.xy * trackShadowOffsetCm, trackShadowSurfaceCm, trackShadowDepth);
            trackOcclusion = max(trackOcclusion, saturate((trackShadowSurfaceCm - (trackHitHeightCm + trackTowardLight.z * trackShadowOffsetCm)) * 0.5));
        }
        trackShadow = 1.0 - saturate(TrackPomShadowStrength) * trackOcclusion;
    }
}

float2 trackSlope = float2(0.0, 0.0);
if (trackIsDriven > 0.5)
{
    float trackCellCm = trackWindowSize.x / max(SimGridResolution, 2.0);
    float trackEastCm = 0.0;
    float trackWestCm = 0.0;
    float trackNorthCm = 0.0;
    float trackSouthCm = 0.0;
    float trackUnusedDepth = 0.0;
    TD_TRACK_HEIGHT_AT(trackHitXY + float2(trackCellCm, 0.0), trackEastCm, trackUnusedDepth);
    TD_TRACK_HEIGHT_AT(trackHitXY - float2(trackCellCm, 0.0), trackWestCm, trackUnusedDepth);
    TD_TRACK_HEIGHT_AT(trackHitXY + float2(0.0, trackCellCm), trackNorthCm, trackUnusedDepth);
    TD_TRACK_HEIGHT_AT(trackHitXY - float2(0.0, trackCellCm), trackSouthCm, trackUnusedDepth);
    trackSlope = float2(trackEastCm - trackWestCm, trackNorthCm - trackSouthCm) / (2.0 * trackCellCm) * TrackNormalStrength;
}
if (TileMapNormalStrength > 0.0 && TileMapHeightCm > 0.0)
{
    float trackTileCameraFade = 1.0 - smoothstep(TileMapCameraFadeCm, TileMapCameraFadeCm + max(TileMapCameraFadeLengthCm, 0.001), trackCameraDistanceCm);
    float2 trackTileTexel = float2(1.0 / 512.0, 1.0 / 512.0);
    float trackTileCenter = TD_TRACK_TILE(trackTileUvPrimary);
    float trackTileAlongU = TD_TRACK_TILE(trackTileUvPrimary + float2(trackTileTexel.x, 0.0));
    float trackTileAlongV = TD_TRACK_TILE(trackTileUvPrimary + float2(0.0, trackTileTexel.y));
    float2 trackTileGradient = float2(trackTileAlongU - trackTileCenter, trackTileAlongV - trackTileCenter) / (trackTileTexel * trackTilePeriodCm);
    float2 trackTileSlope = trackTileAxisU * trackTileGradient.x + trackTileAxisV * trackTileGradient.y;
    trackSlope += 0.75 * TileMapHeightCm * trackTileSlope * TileMapNormalStrength * trackTileCameraFade * trackTileFade;
}
float4 trackDetailTap = Texture2DSample(TrackDetailNormalTexture, TrackDetailNormalTextureSampler, trackHitXY / max(TrackDetailTileCm, 1.0));
float2 trackDetailXY = (trackDetailTap.rg * 2.0 - 1.0) * TrackDetailNormalStrength;
TrackNormalWS = normalize(float3(-trackSlope + trackDetailXY, 1.0));

float trackSparkleHash = frac(sin(dot(floor(trackHitXY / 0.8), float2(12.9898, 78.233))) * 43758.5453);
float trackSparkle = step(0.985, trackSparkleHash) * TrackSparkleIntensity * (1.0 - saturate(trackHitDepth));
float trackGrooveDepth = saturate(trackHitDepth);
float3 trackColor = lerp(TrackSurfaceColor, TrackGrooveColor, trackGrooveDepth) * trackShadow + trackSparkle;
float trackWet = 0.0;
if (WetSourceActive > 0.5)
{
    float2 trackWetUv = (trackHitXY - WetSourceWindow.xy) / max(WetSourceWindow.zw, float2(1.0, 1.0)) + 0.5;
    float trackWetInside = step(0.0, trackWetUv.x) * step(trackWetUv.x, 1.0) * step(0.0, trackWetUv.y) * step(trackWetUv.y, 1.0);
    float4 trackWetTap = Texture2DSampleLevel(WetSourceTexture, WetSourceTextureSampler, saturate(trackWetUv), 0);
    trackWet = trackWetInside * saturate(trackWetTap.g) * (1.0 - smoothstep(0.0, 2.0 / max(WetSourceDepthReferenceCm, 1.0), trackWetTap.r));
}
TrackRoughnessOut = TrackRoughness;
if (trackWet > 0.0)
{
    trackColor *= (1.0 - TrackWetDarkening * trackWet) * (1.0 - TrackWetGrooveDarkening * trackWet * trackGrooveDepth);
    TrackRoughnessOut = lerp(TrackRoughness, TrackWetGrooveRoughness, trackWet * trackGrooveDepth);
}
TrackPixelDepthOffsetCm = max(trackHitOffsetCm, 0.0);
return float4(trackColor, 1.0 - trackFloorClip);

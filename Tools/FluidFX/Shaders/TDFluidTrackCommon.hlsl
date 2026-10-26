#define TD_TRACK_TWO_PI 6.28318530718
#if TD_TRACK_VERTEX
#define TD_TRACK_BANK_TAP_COUNT 4
#else
#define TD_TRACK_BANK_TAP_COUNT 8
#endif
#define TD_TRACK_FETCH(UV) Texture2DSampleLevel(DensityPressureTexture, DensityPressureTextureSampler, saturate(UV), 0)
#define TD_TRACK_DEPTH_FROM(TAP) (saturate((TAP).r) * saturate((TAP).a))
#define TD_TRACK_TILE(UV) Texture2DSampleLevel(TrackDuneHeightTexture, TrackDuneHeightTextureSampler, (UV), 0).r

float trackIsDriven = step(0.5, SimGridResolution);
float2 trackWindowCenter = lerp(TrackWindowCenterWorld.xy, SimCenterWorld.xy, trackIsDriven);
float2 trackWindowExtent = lerp(TrackWindowExtentsCm.xy, SimExtentsCm.xy, trackIsDriven);
float2 trackWindowSize = max(trackWindowExtent, float2(1.0, 1.0));
float trackHasWindow = step(0.5, trackWindowExtent.x) * step(0.5, trackWindowExtent.y);
float2 trackWindowUv = (TrackRestPosition.xy - trackWindowCenter) / trackWindowSize + 0.5;
float2 trackEdgeCm = (0.5 - abs(trackWindowUv - 0.5)) * trackWindowSize;
float trackEdgeDistanceCm = trackHasWindow > 0.5 ? min(trackEdgeCm.x, trackEdgeCm.y) : -1000000.0;
float trackEdgeBlend = smoothstep(TrackFloorClipInsetCm, TrackFloorClipInsetCm + max(TrackEdgeFadeCm, 0.001), trackEdgeDistanceCm);
float trackCameraDistanceCm = length(TrackRestPosition - CameraWorldPosition);

float trackTileAngle = radians(TileMapWindDegrees);
float2 trackTileAxisU = float2(cos(trackTileAngle), sin(trackTileAngle));
float2 trackTileAxisV = float2(-trackTileAxisU.y, trackTileAxisU.x);
float2 trackTileAxisSecondU = float2(cos(trackTileAngle + 0.6), sin(trackTileAngle + 0.6));
float2 trackTileAxisSecondV = float2(-trackTileAxisSecondU.y, trackTileAxisSecondU.x);
float2 trackTilePeriodCm = max(TileMapPeriodCm.xy, float2(10.0, 10.0));
float2 trackTileUvPrimary = float2(dot(TrackRestPosition.xy, trackTileAxisU), dot(TrackRestPosition.xy, trackTileAxisV)) / trackTilePeriodCm;
float2 trackTileUvSecondary = float2(dot(TrackRestPosition.xy, trackTileAxisSecondU), dot(TrackRestPosition.xy, trackTileAxisSecondV)) / (trackTilePeriodCm * 0.37);
float trackTileFade = TileMapWpoFadeEndCm > 0.0 ? 1.0 - smoothstep(0.75 * TileMapWpoFadeEndCm, TileMapWpoFadeEndCm, trackCameraDistanceCm) : 1.0;
float trackTileHeightCm = 0.0;
if (TileMapHeightCm > 0.0)
{
    float trackTilePrimary = TD_TRACK_TILE(trackTileUvPrimary);
    float trackTileSecondary = TD_TRACK_TILE(trackTileUvSecondary);
    trackTileHeightCm = TileMapHeightCm * (0.75 * (trackTilePrimary - 0.5) + 0.25 * (trackTileSecondary - 0.5)) * trackTileFade;
}

float trackLayerCm = max(TrackLayerThicknessCm + trackTileHeightCm, 0.0);
float trackOutsideCm = trackLayerCm * saturate(TrackOutsideLayerRatio);
float2 trackBankRadiusUv = max(TrackBankRadiusCm, 1.0) / trackWindowSize;
float trackDepth = 0.0;
float trackBankBlur = 0.0;
if (trackIsDriven > 0.5)
{
    float4 trackCenterTap = TD_TRACK_FETCH(trackWindowUv);
    trackDepth = TD_TRACK_DEPTH_FROM(trackCenterTap);
    [unroll]
    for (int trackBankTap = 0; trackBankTap < TD_TRACK_BANK_TAP_COUNT; ++trackBankTap)
    {
        float trackBankAngle = TD_TRACK_TWO_PI * (float(trackBankTap) / float(TD_TRACK_BANK_TAP_COUNT));
        float4 trackBankSample = TD_TRACK_FETCH(trackWindowUv + float2(cos(trackBankAngle), sin(trackBankAngle)) * trackBankRadiusUv);
        trackBankBlur += TD_TRACK_DEPTH_FROM(trackBankSample);
    }
    trackBankBlur /= float(TD_TRACK_BANK_TAP_COUNT);
}

float trackInsideCm = trackLayerCm * (1.0 - saturate(TrackDepthScale * trackDepth)) + TrackBankHeightCm * max(trackBankBlur - trackDepth, 0.0);
float trackHeightCm = trackOutsideCm;
float trackFloorClip = 0.0;
if (trackIsDriven > 0.5)
{
    trackHeightCm = trackEdgeDistanceCm < TrackFloorClipInsetCm ? trackOutsideCm - TrackEdgeSinkCm : lerp(trackOutsideCm, trackInsideCm, trackEdgeBlend);
}
else
{
    trackFloorClip = (TrackUsesWindowMask > 0.5 && trackEdgeDistanceCm >= TrackFloorClipInsetCm) ? 1.0 : 0.0;
}

float trackSampleCellCm = trackWindowSize.x / max(SimGridResolution, 2.0);
float2 trackSampleSlope = float2(0.0, 0.0);
if (trackIsDriven > 0.5)
{
    float4 trackSampleEast = TD_TRACK_FETCH((TrackRestPosition.xy + float2(trackSampleCellCm, 0.0) - trackWindowCenter) / trackWindowSize + 0.5);
    float4 trackSampleWest = TD_TRACK_FETCH((TrackRestPosition.xy - float2(trackSampleCellCm, 0.0) - trackWindowCenter) / trackWindowSize + 0.5);
    float4 trackSampleNorth = TD_TRACK_FETCH((TrackRestPosition.xy + float2(0.0, trackSampleCellCm) - trackWindowCenter) / trackWindowSize + 0.5);
    float4 trackSampleSouth = TD_TRACK_FETCH((TrackRestPosition.xy - float2(0.0, trackSampleCellCm) - trackWindowCenter) / trackWindowSize + 0.5);
    float2 trackSampleDepthGradient = float2(TD_TRACK_DEPTH_FROM(trackSampleEast) - TD_TRACK_DEPTH_FROM(trackSampleWest), TD_TRACK_DEPTH_FROM(trackSampleNorth) - TD_TRACK_DEPTH_FROM(trackSampleSouth)) / (2.0 * trackSampleCellCm);
    trackSampleSlope = -trackLayerCm * saturate(TrackDepthScale) * trackSampleDepthGradient * trackEdgeBlend * TrackNormalStrength;
}
TrackSampleNormalWS = normalize(float3(-trackSampleSlope, 1.0));
return float4(trackEdgeBlend * trackHasWindow, trackDepth, trackHeightCm, 0.0);

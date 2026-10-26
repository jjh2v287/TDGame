float2 lodMorphDeltaXY = float2(0.0, 0.0);
float lodWaveFilterSpacingCm = 0.0;
if (LodInstanceSpacingCm > 0.0)
{
    float lodRangeCm = LodBaseRangeCm * LodInstanceSpacingCm / max(LodBaseQuadSizeCm, 0.01);
    float lodMorphAmount = saturate((length(WaterWorldPosition - LodObserverWorld) - LodMorphStartRatio * lodRangeCm) / max((1.0 - LodMorphStartRatio) * lodRangeCm, 0.01));
    float2 lodVertexIndex = round(WaterWorldPosition.xy / LodInstanceSpacingCm);
    float2 lodOddIndex = lodVertexIndex - 2.0 * floor(lodVertexIndex * 0.5);
    lodMorphDeltaXY = -lodOddIndex * LodInstanceSpacingCm * lodMorphAmount;
    lodWaveFilterSpacingCm = LodVertexWaveFilterRatio * LodInstanceSpacingCm * (1.0 + lodMorphAmount);
}

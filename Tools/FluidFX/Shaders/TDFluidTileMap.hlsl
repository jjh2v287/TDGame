float tileMapHeightCm = 0.0;
float2 tileMapSlope = float2(0.0, 0.0);
float tileMapFoam = 0.0;
if (TileMap1PeriodCm.x > 0.0 && TileMap1PeriodCm.y > 0.0)
{
    float tileMapFade = 1.0 - saturate((length(tileSamplePositionCm - tileCameraPositionCm) - TileMap1FadeStartCm) / max(TileMap1FadeLengthCm, 1.0));
    float2 tileMapUv = (tileSamplePositionCm.xy - TileMap1OriginCm.xy) / TileMap1PeriodCm.xy;
    float4 tileMapCenter = Texture2DSampleLevel(TileMap1Texture, TileMap1TextureSampler, tileMapUv, 0);
    float tileMapCenterHeightCm = (tileMapCenter.g * 2.0 - 1.0) * TileMap1HeightCm + tileMapCenter.r * TileMap1DensityHeightCm;
    tileMapHeightCm = tileMapCenterHeightCm * tileMapFade;
#if !TD_TILEMAP_HEIGHT_ONLY
    float tileMapTexel = 1.0 / max(TileMap1PeriodCm.z, 1.0);
    float4 tileMapRight = Texture2DSampleLevel(TileMap1Texture, TileMap1TextureSampler, tileMapUv + float2(tileMapTexel, 0.0), 0);
    float4 tileMapUp = Texture2DSampleLevel(TileMap1Texture, TileMap1TextureSampler, tileMapUv + float2(0.0, tileMapTexel), 0);
    float2 tileMapCellCm = max(TileMap1PeriodCm.xy * tileMapTexel, float2(1.0, 1.0));
    float tileMapRightHeightCm = (tileMapRight.g * 2.0 - 1.0) * TileMap1HeightCm + tileMapRight.r * TileMap1DensityHeightCm;
    float tileMapUpHeightCm = (tileMapUp.g * 2.0 - 1.0) * TileMap1HeightCm + tileMapUp.r * TileMap1DensityHeightCm;
    tileMapSlope = float2(tileMapRightHeightCm - tileMapCenterHeightCm, tileMapUpHeightCm - tileMapCenterHeightCm) / tileMapCellCm * (TileMap1NormalStrength * tileMapFade);
    tileMapFoam = tileMapCenter.r * TileMap1FoamStrength * tileMapFade;
#endif
}

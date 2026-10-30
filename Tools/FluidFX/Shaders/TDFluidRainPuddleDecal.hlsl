float2 rainWindowUv = (WaterWorldPosition.xy - SimCenterWorld.xy) / max(SimExtentsCm.xy, float2(1.0, 1.0)) + 0.5;
float2 rainEdgeDistance = min(rainWindowUv, 1.0 - rainWindowUv);
float rainWindowFade = saturate(min(rainEdgeDistance.x, rainEdgeDistance.y) / max(SimExtentsCm.z, 0.0001)) * step(15.5, SimGridResolution);
float4 rainSurface = Texture2DSampleLevel(WaterSurfaceTexture, WaterSurfaceTextureSampler, saturate(rainWindowUv), 0);
float rainTerrainKnown = 1.0 - step(7.99, rainSurface.a);
float rainDryLand = (rainSurface.r * WaterDepthReferenceCm > 0.5) ? 0.0 : 1.0;
float rainCenterZ = TerrainReferenceZCm + rainSurface.a * 1000.0;
float2 rainTapStep = RainPuddleCavityRadiusCm / max(SimExtentsCm.xy, float2(1.0, 1.0));
float rainRingSum = 0.0;
[unroll]
for (int rainTap = 0; rainTap < 8; ++rainTap)
{
    float rainTapAngle = 0.78539816 * rainTap;
    float2 rainTapUv = saturate(rainWindowUv + float2(cos(rainTapAngle), sin(rainTapAngle)) * rainTapStep);
    float4 rainTapSurface = Texture2DSampleLevel(WaterSurfaceTexture, WaterSurfaceTextureSampler, rainTapUv, 0);
    float rainTapKnown = 1.0 - step(7.99, rainTapSurface.a);
    rainRingSum += lerp(rainCenterZ, TerrainReferenceZCm + rainTapSurface.a * 1000.0, rainTapKnown);
}
float rainCavityCm = rainRingSum * 0.125 - rainCenterZ;
float rainLevelCm = max(RainPuddleLevelCm, 0.0);
float rainFilledCm = clamp(rainCavityCm - (RainPuddleCavityReferenceCm - rainLevelCm), 0.0, rainLevelCm);
float rainHeightGate = step(WaterWorldPosition.z - rainCenterZ, PuddleHeightToleranceCm);
float rainMask = saturate(rainFilledCm / max(PuddleEdgeSoftnessCm, 0.01)) * rainWindowFade * rainHeightGate * rainTerrainKnown * rainDryLand;
float rainCellSize = max(PuddleRippleCellCm, 1.0);
float rainRingWidth = max(PuddleRippleWidthCm, 0.01);
float rainCellDropsPerSec = RainDropsPerSquareMeterPerSec * RainIntensity * rainCellSize * rainCellSize * 0.0001;
float rainPeriod = 1.0 / max(rainCellDropsPerSec, 0.01);
float rainRingLife = max(min(rainPeriod, (rainCellSize - rainRingWidth) / max(PuddleRippleSpeedCmPerSec, 0.001)), 0.001);
float rainRippleActive = step(0.0001, RainIntensity * RainDropsPerSquareMeterPerSec);
int2 rainBaseCell = int2(floor(WaterWorldPosition.xy / rainCellSize));
float2 rainNormal = float2(0.0, 0.0);
[unroll]
for (int rainCellY = -1; rainCellY <= 1; ++rainCellY)
{
    [unroll]
    for (int rainCellX = -1; rainCellX <= 1; ++rainCellX)
    {
        int2 rainCell = rainBaseCell + int2(rainCellX, rainCellY);
        uint rainHashY = asuint(rainCell.y) * 747796405u + 2891336453u;
        rainHashY = ((rainHashY >> ((rainHashY >> 28u) + 4u)) ^ rainHashY) * 277803737u;
        rainHashY = (rainHashY >> 22u) ^ rainHashY;
        uint rainHash = (asuint(rainCell.x) ^ rainHashY) * 747796405u + 2891336453u;
        rainHash = ((rainHash >> ((rainHash >> 28u) + 4u)) ^ rainHash) * 277803737u;
        rainHash = (rainHash >> 22u) ^ rainHash;
        uint rainDrawX = (rainHash ^ 0x68E31DA4u) * 747796405u + 2891336453u;
        rainDrawX = ((rainDrawX >> ((rainDrawX >> 28u) + 4u)) ^ rainDrawX) * 277803737u;
        rainDrawX = (rainDrawX >> 22u) ^ rainDrawX;
        uint rainDrawY = (rainHash ^ 0xB5297A4Du) * 747796405u + 2891336453u;
        rainDrawY = ((rainDrawY >> ((rainDrawY >> 28u) + 4u)) ^ rainDrawY) * 277803737u;
        rainDrawY = (rainDrawY >> 22u) ^ rainDrawY;
        float rainPhase = float(rainHash >> 8u) * 5.9604645e-8;
        float2 rainCenter = (float2(rainCell) + float2(float(rainDrawX >> 8u), float(rainDrawY >> 8u)) * 5.9604645e-8) * rainCellSize;
        float rainAge = frac(RainTimeSeconds / rainPeriod + rainPhase) * rainPeriod;
        float2 rainOffset = WaterWorldPosition.xy - rainCenter;
        float rainDistance = length(rainOffset);
        float rainBand = rainDistance - rainAge * PuddleRippleSpeedCmPerSec;
        float rainInBand = 1.0 - step(rainRingWidth, abs(rainBand));
        float rainWave = sin(6.2831853 * rainBand / rainRingWidth) * exp(-PuddleRippleDecayPerSec * rainAge) * rainInBand;
        float rainLifeFade = 1.0 - smoothstep(0.5 * rainRingLife, rainRingLife, rainAge);
        rainNormal += rainOffset / max(rainDistance, 0.001) * (rainWave * rainLifeFade);
    }
}
rainNormal = rainNormal * PuddleRippleNormalStrength * rainRippleActive;
return float4(rainMask, rainNormal.x, rainNormal.y, 0.0);

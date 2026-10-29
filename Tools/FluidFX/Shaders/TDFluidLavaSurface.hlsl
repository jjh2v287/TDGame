float3 tileSamplePositionCm = LavaPosition;
float3 tileCameraPositionCm = CameraWorldPosition;
TD_LAVA_TILE_MAP_INSERT
float lavaIsDriven = step(0.5, SimGridResolution);
float2 lavaWindowSize = max(SimExtentsCm.xy, float2(1.0, 1.0));
float2 lavaWindowUv = (LavaPosition.xy - SimCenterWorld.xy) / lavaWindowSize + 0.5;
float lavaInside = step(0.0, lavaWindowUv.x) * step(lavaWindowUv.x, 1.0) * step(0.0, lavaWindowUv.y) * step(lavaWindowUv.y, 1.0) * lavaIsDriven;
float4 lavaLocalTap = Texture2DSampleLevel(DensityPressureTexture, DensityPressureTextureSampler, saturate(lavaWindowUv), 0);
float lavaLocal = saturate(lavaLocalTap.r) * saturate(lavaLocalTap.a) * lavaInside;
float lavaCrustEdge = max(LavaCrustSoftness, 0.0001);
float lavaCrustTile = TileMap1PeriodCm.x > 0.0 ? smoothstep(LavaCrustThreshold - lavaCrustEdge, LavaCrustThreshold + lavaCrustEdge, tileMapFoam) : 1.0;
float lavaCrust = saturate(lavaCrustTile * (1.0 - LavaLocalRevealStrength * lavaLocal));
float lavaMolten = pow(saturate(1.0 - lavaCrust), max(LavaEmissiveExponent, 0.01));
LavaEmissiveOut = LavaEmissiveColor * LavaEmissiveIntensity * lavaMolten;
LavaNormalWS = normalize(float3(-tileMapSlope, 1.0));
return float4(lerp(LavaEmissiveColor * LavaMoltenColorScale, LavaCrustColor, lavaCrust), lerp(LavaMoltenRoughness, LavaCrustRoughness, lavaCrust));

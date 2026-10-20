float2 wetWindowUv = (WaterWorldPosition.xy - SimCenterWorld.xy) / max(SimExtentsCm.xy, float2(1.0, 1.0)) + 0.5;
float2 wetEdgeDistance = min(wetWindowUv, 1.0 - wetWindowUv);
float wetWindowFade = saturate(min(wetEdgeDistance.x, wetEdgeDistance.y) / max(SimExtentsCm.z, 0.0001)) * step(15.5, SimGridResolution);
float4 wetSurface = Texture2DSampleLevel(WaterSurfaceTexture, WaterSurfaceTextureSampler, saturate(wetWindowUv), 0);
float wetDryLand = 1.0 - smoothstep(0.0, max(WetSubmergedFadeCm, 0.01) / max(WaterDepthReferenceCm, 1.0), wetSurface.r);
return saturate(wetSurface.g) * wetDryLand * wetWindowFade * WetOpacity;

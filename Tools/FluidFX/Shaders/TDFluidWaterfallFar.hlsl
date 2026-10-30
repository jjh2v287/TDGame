float3 cardOffset = PixelWorldPosition - ObjectCenterWorld;
float horizontalCm = dot(cardOffset, normalize(CardRightWorld));
float heightCm = cardOffset.z;

float noiseScaleCm = max(FarNoiseScaleCm, 1.0);
float streakStretch = max(FarStreakStretch, 1.0);
float cycleSeconds = max(FarFlowCycleSeconds, 0.05);
float2 baseCoordinate = float2(horizontalCm, heightCm / streakStretch) / noiseScaleCm;
float phaseOffset = Texture2DSample(FlowNoiseTexture, FlowNoiseTextureSampler, baseCoordinate).r;

float phaseFirst = frac(ElapsedSeconds / cycleSeconds + phaseOffset);
float phaseSecond = frac(phaseFirst + 0.5);
float weightFirst = 1.0 - abs(1.0 - 2.0 * phaseFirst);
float2 coordinateFirst = float2(horizontalCm, (heightCm + FarFlowSpeedCmPerSec * phaseFirst * cycleSeconds) / streakStretch) / noiseScaleCm;
float2 coordinateSecond = float2(horizontalCm, (heightCm + FarFlowSpeedCmPerSec * phaseSecond * cycleSeconds) / streakStretch) / noiseScaleCm;
float foamNoiseFirst = Texture2DSample(FoamNoiseTexture, FoamNoiseTextureSampler, coordinateFirst).r;
float foamNoiseSecond = Texture2DSample(FoamNoiseTexture, FoamNoiseTextureSampler, coordinateSecond).r;
float foamNoise = lerp(foamNoiseSecond, foamNoiseFirst, weightFirst);
float foamAmount = saturate((foamNoise - (1.0 - FarFoamCoverage)) * FarFoamSharpness);

float cardHeightCm = max(ObjectBoundsCm.z * 2.0, 1.0);
float heightRatio = saturate((heightCm + 0.5 * cardHeightCm) / cardHeightCm);
float mistAmount = 1.0 - saturate(heightRatio / max(FarMistHeightRatio, 1e-3));

float edgeFadeRatio = max(FarEdgeFadeRatio, 1e-4);
float edgeWeight = smoothstep(0.0, edgeFadeRatio, min(EdgeUv.x, 1.0 - EdgeUv.x)) * smoothstep(0.0, 0.5 * edgeFadeRatio, min(EdgeUv.y, 1.0 - EdgeUv.y));

float farOpacity = saturate(FarOpacity * edgeWeight * saturate(FarBaseOpacityBias + FarFoamOpacityGain * foamAmount + mistAmount));
float3 farColor = lerp(FarTint, FarFoamColor, saturate(foamAmount + mistAmount));

return float4(farColor, farOpacity);

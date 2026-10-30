float3 sphereOffset = PixelWorldPosition - ObjectCenterWorld;
float3 sphereDirection = sphereOffset / max(length(sphereOffset), 1e-4);
float longitudeTurns = atan2(sphereDirection.y, sphereDirection.x) / 6.28318530718;
float2 sphereUv = float2(frac(longitudeTurns * SphereLongitudeRepeat + 0.5), acos(clamp(sphereDirection.z, -1.0, 1.0)) / 3.14159265359);
float4 simulationTap = Texture2DSampleLevel(DensityPressureTexture, DensityPressureTextureSampler, sphereUv, 0);

float poleDistance = min(sphereUv.y, 1.0 - sphereUv.y);
float poleBlend = saturate(poleDistance / max(SpherePoleFadeRatio, 1e-3));
float densityValue = lerp(SpherePoleDensity, saturate(simulationTap.r), poleBlend);
float selectiveValue = saturate(simulationTap.b);
float densityMix = saturate(densityValue * SphereDensityContrast);

float fresnelBase = 1.0 - saturate(dot(sphereDirection, CameraVectorWorld));
float fresnelValue = pow(fresnelBase, max(SphereFresnelPower, 1e-3)) * SphereFresnelStrength;

float3 liquidColor = lerp(lerp(SphereBaseColor, SphereDenseColor, densityMix), SphereSelectiveColor, selectiveValue);
float liquidOpacity = saturate(lerp(SphereOpacityMin, SphereOpacityMax, densityMix) + fresnelValue);

return float4(liquidColor, liquidOpacity);

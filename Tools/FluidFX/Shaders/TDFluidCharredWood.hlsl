float3 crackCoordinate = PixelWorldPosition * (max(WoodCrackScale, 0.0001) * 0.25);
float4 crackSample = Texture3DSampleLevel(NoiseVolumeTexture, NoiseVolumeTextureSampler, crackCoordinate, 0);
float crackWidth = clamp(WoodCrackWidth, 0.02, 0.95);
float primaryCrack = pow(saturate(1.0 - abs(2.0 * crackSample.b - 1.0)), 6.0);
float secondaryCrack = 0.8 * pow(saturate(1.0 - abs(2.0 * crackSample.a - 1.0)), 8.0);
float crackMask = saturate((max(primaryCrack, secondaryCrack) - (1.0 - crackWidth)) / crackWidth);

float3 flickerCoordinate = float3(PixelWorldPosition.xy * 0.004, ElapsedSeconds * max(WoodFlickerRate, 0.0) * 0.25);
float flicker = 0.55 + 0.9 * Texture3DSampleLevel(NoiseVolumeTexture, NoiseVolumeTextureSampler, flickerCoordinate, 0).r;
float heat = FireRadiusCm > 0.5 ? saturate(1.0 - distance(PixelWorldPosition, FireCenterWorld) / FireRadiusCm) : 1.0;
float glow = crackMask * crackMask * heat * flicker;
float charLevel = (0.6 + 0.4 * crackSample.r) * (1.0 - 0.7 * crackMask);

return float4(WoodGlowColor * (WoodGlowIntensity * glow), charLevel);

float3 pixelOffset = PixelWorldPosition - CameraWorldPosition;
float pathCm = length(pixelOffset);
float3 viewDirection = pixelOffset / max(pathCm, 1e-3);

if (CameraWorldPosition.z < UnderwaterSurfaceZCm && viewDirection.z > 1e-4)
{
    pathCm = min(pathCm, (UnderwaterSurfaceZCm - CameraWorldPosition.z) / viewDirection.z);
}
pathCm = min(pathCm, UnderwaterMaxDistanceCm);

float belowSurfaceCm = UnderwaterSurfaceZCm - PixelWorldPosition.z;
float3 faceNormal = normalize(cross(ddx(PixelWorldPosition), ddy(PixelWorldPosition)));
float upFacing = saturate(0.25 + 0.75 * abs(faceNormal.z));
float causticWindow = smoothstep(0.0, max(UnderwaterCausticsOnsetCm, 0.01), belowSurfaceCm) * (1.0 - smoothstep(0.5 * UnderwaterCausticsMaxDepthCm, max(UnderwaterCausticsMaxDepthCm, 1.0), belowSurfaceCm));
float2 causticDrift = float2(0.94, 0.34) * (UnderwaterCausticsDriftCmPerSec * ElapsedSeconds);
float2 rotatedPosition = float2(PixelWorldPosition.x * 0.7648 - PixelWorldPosition.y * 0.6442, PixelWorldPosition.x * 0.6442 + PixelWorldPosition.y * 0.7648);
float causticTilingCm = max(UnderwaterCausticsTilingCm, 10.0);
const float causticStoreScale = 4.0;
float3 causticFirst = Texture2DSample(UnderwaterCausticsTexture, UnderwaterCausticsTextureSampler, (PixelWorldPosition.xy + causticDrift) / causticTilingCm).rgb;
float3 causticSecond = Texture2DSample(UnderwaterCausticsTexture, UnderwaterCausticsTextureSampler, (rotatedPosition + float2(causticDrift.y, -causticDrift.x) * 0.8) / (causticTilingCm * 1.37)).rgb;
float3 causticScale = max(1.0 + UnderwaterCausticsStrength * (min(causticFirst, causticSecond) * causticStoreScale - UnderwaterCausticsMeanLevel) * causticWindow * upFacing, 0.2);
float3 fogColorLocal = UnderwaterFogColor.rgb * (1.0 + UnderwaterUpwardBrightness * saturate(viewDirection.z));

float3 absorbedColor = SceneColor * causticScale * exp(-UnderwaterAbsorptionPerCm * pathCm);
float fogAmount = 1.0 - exp(-UnderwaterFogDensityPerCm * pathCm);

return lerp(absorbedColor, fogColorLocal, fogAmount);

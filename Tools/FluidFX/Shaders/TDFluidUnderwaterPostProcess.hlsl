float3 pixelOffset = PixelWorldPosition - CameraWorldPosition;
float pathCm = length(pixelOffset);
float3 viewDirection = pixelOffset / max(pathCm, 1e-3);

if (CameraWorldPosition.z < UnderwaterSurfaceZCm && viewDirection.z > 1e-4)
{
    pathCm = min(pathCm, (UnderwaterSurfaceZCm - CameraWorldPosition.z) / viewDirection.z);
}
pathCm = min(pathCm, UnderwaterMaxDistanceCm);

float3 absorbedColor = SceneColor * exp(-UnderwaterAbsorptionPerCm * pathCm);
float fogAmount = 1.0 - exp(-UnderwaterFogDensityPerCm * pathCm);

return lerp(absorbedColor, UnderwaterFogColor, fogAmount);

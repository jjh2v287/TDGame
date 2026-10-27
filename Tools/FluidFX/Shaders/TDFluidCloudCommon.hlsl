#define TD_CLOUD_KM 100000.0
#define TD_CLOUD_SAFE_POW(X, E) pow(max((X), 0.0), max((E), 0.01))
#define TD_CLOUD_NOISE(UVW) Texture3DSampleLevel(NoiseVolumeTexture, NoiseVolumeTextureSampler, (UVW), 0).r
#define TD_CLOUD_COVERAGE(UV) Texture2DSampleLevel(SlotCoverageTexture, SlotCoverageTextureSampler, (UV), 0)
#define TD_CLOUD_SECTION(UV) Texture2DSampleLevel(SlotSectionTexture, SlotSectionTextureSampler, saturate(UV), 0).r
#define TD_CLOUD_EDGE(UV) min(0.5 - abs((UV).x - 0.5), 0.5 - abs((UV).y - 0.5))
#define TD_CLOUD_INSIDE01(UV) (step(0.0, (UV).x) * step((UV).x, 1.0) * step(0.0, (UV).y) * step((UV).y, 1.0))
#define TD_CLOUD_TERRAIN_UV(PXY, FP) (((PXY) - (FP).xy * TD_CLOUD_KM) / max((FP).zw * TD_CLOUD_KM, float2(1.0, 1.0)))
#define TD_CLOUD_TERRAIN_HEIGHT_CM(PXY, FP, HR) (step(1e-6, (FP).z) * TD_CLOUD_KM * lerp((HR).x, (HR).y, Texture2DSampleLevel(TerrainHeightTexture, TerrainHeightTextureSampler, saturate(TD_CLOUD_TERRAIN_UV(PXY, FP)), 0).r * TD_CLOUD_INSIDE01(TD_CLOUD_TERRAIN_UV(PXY, FP))))
#define TD_CLOUD_PATTERN_NOISE(Q) ((TD_CLOUD_NOISE(float3((Q), 0.37)) + 0.5 * TD_CLOUD_NOISE(float3((Q) * 2.03, 0.37))) / 1.5)
#define TD_CLOUD_DETAIL_NOISE(X) (0.65 * TD_CLOUD_NOISE(X) + 0.35 * TD_CLOUD_NOISE((X) * 2.03))

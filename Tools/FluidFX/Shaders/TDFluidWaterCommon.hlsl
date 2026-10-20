#define TD_WATER_TWO_PI 6.28318530718
#if TD_WATER_VERTEX
#define TD_WATER_SIM_TAP_COUNT 1
#define TD_WATER_WAVE_PASS_COUNT 1
#else
#define TD_WATER_SIM_TAP_COUNT 5
#define TD_WATER_WAVE_PASS_COUNT 2
#endif
#define TD_WATER_CATMULL_FETCH(TEX, SMP) (catmullWeight0.y * (catmullWeight0.x * Texture2DSampleLevel(TEX, SMP, float2(catmullTapLow.x, catmullTapLow.y), 0) + catmullWeightMiddle.x * Texture2DSampleLevel(TEX, SMP, float2(catmullTapMiddle.x, catmullTapLow.y), 0) + catmullWeight3.x * Texture2DSampleLevel(TEX, SMP, float2(catmullTapHigh.x, catmullTapLow.y), 0)) + catmullWeightMiddle.y * (catmullWeight0.x * Texture2DSampleLevel(TEX, SMP, float2(catmullTapLow.x, catmullTapMiddle.y), 0) + catmullWeightMiddle.x * Texture2DSampleLevel(TEX, SMP, float2(catmullTapMiddle.x, catmullTapMiddle.y), 0) + catmullWeight3.x * Texture2DSampleLevel(TEX, SMP, float2(catmullTapHigh.x, catmullTapMiddle.y), 0)) + catmullWeight3.y * (catmullWeight0.x * Texture2DSampleLevel(TEX, SMP, float2(catmullTapLow.x, catmullTapHigh.y), 0) + catmullWeightMiddle.x * Texture2DSampleLevel(TEX, SMP, float2(catmullTapMiddle.x, catmullTapHigh.y), 0) + catmullWeight3.x * Texture2DSampleLevel(TEX, SMP, float2(catmullTapHigh.x, catmullTapHigh.y), 0)))

float waterGridResolution = max(SimGridResolution, 2.0);
float2 waterExtentCm = max(SimExtentsCm.xy, float2(1.0, 1.0));
float2 windowUv = (WaterWorldPosition.xy - SimCenterWorld.xy) / waterExtentCm + 0.5;
float2 windowEdgeDistance = min(windowUv, 1.0 - windowUv);
float windowFade = saturate(min(windowEdgeDistance.x, windowEdgeDistance.y) / max(SimExtentsCm.z, 0.0001));
float simIsDriven = step(15.5, SimGridResolution);
float waterLayerBlend = saturate(FluidWaterLayerActive);
float simFade = windowFade * simIsDriven;

const float waveLengthCm[8] = { 640.0, 410.0, 270.0, 180.0, 120.0, 80.0, 52.0, 34.0 };
const float waveAmplitudeShare[8] = { 0.30, 0.24, 0.18, 0.12, 0.08, 0.05, 0.03, 0.02 };
const float waveAngleOffsetDegrees[8] = { -31.0, 18.0, -9.0, 33.0, 4.0, -24.0, 26.0, -16.0 };
const float wavePhaseOffset[8] = { 0.7, 2.9, 4.4, 1.3, 5.6, 3.1, 0.2, 4.9 };
const float waveSteepness = 0.6;
const float waveTimeScale = 0.45;
const float gravityCmPerSecondSquared = 980.0;

float waveAmplitudeCm = PassiveWaveAmplitudeCm * (1.0 - 0.5 * simFade);
float waveBaseAngle = radians(PassiveWaveDirectionDegrees);
float2 waveRestXY = WaterWorldPosition.xy;
float3 waveDisplacementCm = float3(0.0, 0.0, 0.0);
float2 waveSlope = float2(0.0, 0.0);
float waveFoldDeterminant = 1.0;

[unroll]
for (int wavePass = 0; wavePass < TD_WATER_WAVE_PASS_COUNT; ++wavePass)
{
    float3 passDisplacement = float3(0.0, 0.0, 0.0);
    float2 passSlope = float2(0.0, 0.0);
    float3 passFold = float3(0.0, 0.0, 0.0);

    [unroll]
    for (int waveIndex = 0; waveIndex < 8; ++waveIndex)
    {
        float waveNumber = TD_WATER_TWO_PI / waveLengthCm[waveIndex];
        float waveAngle = waveBaseAngle + radians(waveAngleOffsetDegrees[waveIndex]);
        float2 waveDirection = float2(cos(waveAngle), sin(waveAngle));
        float waveAmplitudeLocal = waveAmplitudeCm * waveAmplitudeShare[waveIndex];
        float wavePhase = waveNumber * dot(waveDirection, waveRestXY) - sqrt(gravityCmPerSecondSquared * waveNumber) * waveTimeScale * ElapsedSeconds + wavePhaseOffset[waveIndex];
        float waveSine = 0.0;
        float waveCosine = 0.0;
        sincos(wavePhase, waveSine, waveCosine);
        passDisplacement += float3(waveDirection * (waveSteepness * waveAmplitudeLocal * waveCosine), waveAmplitudeLocal * waveSine);
        passSlope += waveDirection * (waveNumber * waveAmplitudeLocal * waveCosine);
        float foldTerm = waveSteepness * waveNumber * waveAmplitudeLocal * waveSine;
        passFold += float3(waveDirection.x * waveDirection.x, waveDirection.y * waveDirection.y, waveDirection.x * waveDirection.y) * foldTerm;
    }

    waveDisplacementCm = passDisplacement;
    waveSlope = passSlope;
    waveFoldDeterminant = (1.0 - passFold.x) * (1.0 - passFold.y) - passFold.z * passFold.z;
    waveRestXY = WaterWorldPosition.xy - passDisplacement.xy;
}

const float2 simTapOffset[5] = { float2(0.0, 0.0), float2(1.0, 0.0), float2(-1.0, 0.0), float2(0.0, 1.0), float2(0.0, -1.0) };
float4 simField[5];

[unroll]
for (int simFieldIndex = 0; simFieldIndex < 5; ++simFieldIndex)
{
    simField[simFieldIndex] = float4(0.0, 0.0, -1.0, 0.0);
}

float simTexelHalfUv = 0.5 / waterGridResolution;
float simSelectiveDensity = 0.0;

[unroll]
for (int simTapIndex = 0; simTapIndex < TD_WATER_SIM_TAP_COUNT; ++simTapIndex)
{
    float2 tapCenterUv = clamp(windowUv + simTapOffset[simTapIndex] * simTexelHalfUv, 0.0, 1.0);
    float2 catmullPosition = tapCenterUv * waterGridResolution;
    float2 catmullCenter = floor(catmullPosition - 0.5) + 0.5;
    float2 catmullFraction = catmullPosition - catmullCenter;
    float2 catmullSquared = catmullFraction * catmullFraction;
    float2 catmullCubed = catmullSquared * catmullFraction;
    float2 catmullWeight0 = -0.5 * catmullCubed + catmullSquared - 0.5 * catmullFraction;
    float2 catmullWeight1 = 1.5 * catmullCubed - 2.5 * catmullSquared + 1.0;
    float2 catmullWeight2 = -1.5 * catmullCubed + 2.0 * catmullSquared + 0.5 * catmullFraction;
    float2 catmullWeight3 = 0.5 * catmullCubed - 0.5 * catmullSquared;
    float2 catmullWeightMiddle = catmullWeight1 + catmullWeight2;
    float2 catmullTapLow = clamp((catmullCenter - 1.0) / waterGridResolution, simTexelHalfUv, 1.0 - simTexelHalfUv);
    float2 catmullTapMiddle = clamp((catmullCenter + catmullWeight2 / catmullWeightMiddle) / waterGridResolution, simTexelHalfUv, 1.0 - simTexelHalfUv);
    float2 catmullTapHigh = clamp((catmullCenter + 2.0) / waterGridResolution, simTexelHalfUv, 1.0 - simTexelHalfUv);

    float4 densityTap = TD_WATER_CATMULL_FETCH(DensityPressureTexture, DensityPressureTextureSampler);
    float densityRipple = clamp(densityTap.g * 2.0 - 1.0, -1.0, 1.0);
    float rippleDecoded = densityRipple;
    float depthNormalized = -1.0;

    if (waterLayerBlend > 0.0)
    {
        float4 surfaceTap = TD_WATER_CATMULL_FETCH(WaterSurfaceTexture, WaterSurfaceTextureSampler);
        float layerRipple = clamp(surfaceTap.b * 2.0 - 1.0, -1.0, 1.0) + PressureNormalStrength * densityRipple;
        rippleDecoded = lerp(densityRipple, layerRipple, waterLayerBlend);
        depthNormalized = saturate(surfaceTap.r);
    }

    simField[simTapIndex] = float4(saturate(densityTap.r), rippleDecoded, depthNormalized, saturate(densityTap.a));
    if (simTapIndex == 0)
    {
        simSelectiveDensity = saturate(densityTap.b);
    }
}

float simMask = simIsDriven * min(windowFade, simField[0].w);
float foamDensity = simField[0].x * simMask;
float rippleHeightCm = simField[0].y * RippleHeightReferenceCm * simMask;
float2 rippleSlope = float2(simField[1].y - simField[2].y, simField[3].y - simField[4].y) * (RippleHeightReferenceCm * waterGridResolution) / waterExtentCm * simMask;

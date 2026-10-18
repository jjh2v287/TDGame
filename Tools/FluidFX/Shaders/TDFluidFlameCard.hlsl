float gridResolution = max(SimGridResolution, 2.0);
float2 extentCm = max(SimExtentsCm.xy, float2(1.0, 1.0));
float2 windowMinimum = SimCenterWorld.xy - 0.5 * extentCm;
float2 planeCoordinate = float2(dot(PixelWorldPosition, PlaneAxisU), dot(PixelWorldPosition, PlaneAxisV));
float2 baseUv = (planeCoordinate - windowMinimum) / extentCm;

if (any(abs(baseUv - 0.5) > 0.5005))
{
    return float4(0.0, 0.0, 0.0, 0.0);
}

float flowCycleSeconds = 1.0;
float boilRate = 1.6;
float4 flowSample = Texture2DSampleLevel(VelocityTexture, VelocityTextureSampler, baseUv, 0);
float speedRatio = flowSample.b;
float2 flowCmPerSec = (flowSample.rg * 2.0 - 1.0) * (600.0 * FlameFlowSpeed);

float phaseFirst = frac(ElapsedSeconds / flowCycleSeconds);
float phaseSecond = frac(phaseFirst + 0.5);
float weightFirst = 1.0 - abs(1.0 - 2.0 * phaseFirst);
float boilTime = ElapsedSeconds * boilRate;

#define TD_FLAME_STRETCH(C) float3((C).x, 0.5 * (C).y, (C).z)
#define TD_FLAME_NOISE(C) (0.65 * GradientNoise3D_ALU(TD_FLAME_STRETCH(C), false, 1.0) + 0.35 * GradientNoise3D_ALU(TD_FLAME_STRETCH(C) * 2.17 + 11.3, false, 1.0))

float3 coordinateFirst = float3((planeCoordinate - flowCmPerSec * (phaseFirst * flowCycleSeconds)) * FlameNoiseScale, boilTime);
float3 coordinateSecond = float3((planeCoordinate - flowCmPerSec * (phaseSecond * flowCycleSeconds)) * FlameNoiseScale, boilTime);
float3 verticalNoiseOffset = float3(23.7, 41.3, 7.1);
float fieldHorizontal = lerp(TD_FLAME_NOISE(coordinateSecond), TD_FLAME_NOISE(coordinateFirst), weightFirst);
float fieldVertical = lerp(TD_FLAME_NOISE(coordinateSecond + verticalNoiseOffset), TD_FLAME_NOISE(coordinateFirst + verticalNoiseOffset), weightFirst);

bool usesPrimaryDensity = FlameDensitySource > 0.5;
float4 previewDensity = Texture2DSampleLevel(DensityPressureTexture, DensityPressureTextureSampler, baseUv, 0);
float previewValue = usesPrimaryDensity ? previewDensity.r : previewDensity.b;
float distortionCm = (0.3 + 0.7 * (1.0 - saturate(previewValue))) * FlameNoiseStrength;
float2 warpedPlaneCoordinate = planeCoordinate + float2(fieldHorizontal, 0.6 * fieldVertical) * distortionCm;
float2 warpedUv = saturate((warpedPlaneCoordinate - windowMinimum) / extentCm);

float2 texelPosition = warpedUv * gridResolution - 0.5;
float2 texelFloor = floor(texelPosition);
float2 texelFraction = texelPosition - texelFloor;
float2 fractionSquared = texelFraction * texelFraction;
float2 fractionCubed = fractionSquared * texelFraction;
float2 weightA = (1.0 - 3.0 * texelFraction + 3.0 * fractionSquared - fractionCubed) / 6.0;
float2 weightB = (4.0 - 6.0 * fractionSquared + 3.0 * fractionCubed) / 6.0;
float2 weightC = (1.0 + 3.0 * texelFraction + 3.0 * fractionSquared - 3.0 * fractionCubed) / 6.0;
float2 weightD = fractionCubed / 6.0;
float2 pairLow = weightA + weightB;
float2 pairHigh = weightC + weightD;
float2 tapLow = (texelFloor - 0.5 + weightB / pairLow) / gridResolution;
float2 tapHigh = (texelFloor + 1.5 + weightD / pairHigh) / gridResolution;

float4 cubicDensity = pairLow.y * (pairLow.x * Texture2DSampleLevel(DensityPressureTexture, DensityPressureTextureSampler, float2(tapLow.x, tapLow.y), 0)
    + pairHigh.x * Texture2DSampleLevel(DensityPressureTexture, DensityPressureTextureSampler, float2(tapHigh.x, tapLow.y), 0))
    + pairHigh.y * (pairLow.x * Texture2DSampleLevel(DensityPressureTexture, DensityPressureTextureSampler, float2(tapLow.x, tapHigh.y), 0)
    + pairHigh.x * Texture2DSampleLevel(DensityPressureTexture, DensityPressureTextureSampler, float2(tapHigh.x, tapHigh.y), 0));

float flameDensity = usesPrimaryDensity ? cubicDensity.r : cubicDensity.b;
float borderCoverage = saturate(cubicDensity.a);

float erosionNoise = saturate(0.5 + 0.5 * fieldHorizontal);
float erosionAmount = clamp(FlameErosion, 0.0, 0.95);
float erodedDensity = saturate((flameDensity - erosionAmount * (1.0 - erosionNoise)) / (1.0 - erosionAmount));

float temperatureBlend = saturate(pow(erodedDensity, 0.6) * (0.8 + 0.2 * speedRatio) + 0.12 * (erosionNoise - 0.5));
float temperatureKelvin = lerp(FlameTemperatureMinKelvin, FlameTemperatureMaxKelvin, temperatureBlend);

float3 wavelengthMicrometers = float3(0.610, 0.550, 0.465);
float3 radiationExponent = 14388.0 / (wavelengthMicrometers * max(temperatureKelvin, 300.0));
float3 radiationLevel = 1.0 / (pow(wavelengthMicrometers, 5.0) * (exp(radiationExponent) - 1.0));
float3 normalizedColor = radiationLevel / max(dot(radiationLevel, float3(0.2126, 0.7152, 0.0722)), 1e-30);
float edgeDimming = lerp(0.45, 1.0, temperatureBlend);
float3 flameEmission = normalizedColor * edgeDimming * FlameIntensity * erodedDensity;

float depthFade = saturate((SceneDepthCm - PixelDepthCm) / max(FlameDepthFadeCm, 1.0));
float flameCoverage = saturate(erodedDensity * FlameOpacityScale) * borderCoverage * depthFade;

return float4(flameEmission, flameCoverage);

#undef TD_FLAME_NOISE
#undef TD_FLAME_STRETCH

float gridResolution = max(SimGridResolution, 2.0);
float2 extentCm = max(SimExtentsCm.xy, float2(1.0, 1.0));
float2 windowMinimum = SimCenterWorld.xy - 0.5 * extentCm;
float2 planeCoordinate = float2(dot(PixelWorldPosition, PlaneAxisU), dot(PixelWorldPosition, PlaneAxisV)) + PlaneCoordinateOffsetCm.xy;
float2 baseUv = (planeCoordinate - windowMinimum) / extentCm;
float2 noisePlaneCoordinate = planeCoordinate + CardLayerIndex * float2(17.3, 9.1);

if (any(abs(baseUv - 0.5) > 0.5005))
{
    return float4(0.0, 0.0, 0.0, 0.0);
}

float flowCycleSeconds = max(FlameFlowCycleSeconds, 0.05);
float boilRate = 1.6;
float noiseTileCells = 4.0;
float detailScaleRatio = 2.9;
float ridgeSharpness = max(FlameRidgeSharpness, 0.5);
float billowExponent = 1.5 / ridgeSharpness;
float noiseBillow = saturate(FlameNoiseBillow);
float4 flowSample = Texture2DSampleLevel(VelocityTexture, VelocityTextureSampler, baseUv, 0);
float speedRatio = flowSample.b;
float2 flowCmPerSec = (flowSample.rg * 2.0 - 1.0) * (600.0 * FlameFlowSpeed);

float phaseFirst = frac(ElapsedSeconds / flowCycleSeconds);
float phaseSecond = frac(phaseFirst + 0.5);
float weightFirst = 1.0 - abs(1.0 - 2.0 * phaseFirst);
float boilTime = (ElapsedSeconds + CardLayerIndex * 0.37) * boilRate;
float3x3 detailRotation = float3x3(0.00, 0.80, 0.60, -0.80, 0.36, -0.48, -0.60, -0.48, 0.64);

#define TD_FLAME_COORDINATE(PHASE) (float3((noisePlaneCoordinate - flowCmPerSec * ((PHASE) * flowCycleSeconds)) * FlameNoiseScale * float2(1.0, 0.5), boilTime) / noiseTileCells)
#define TD_FLAME_DETAIL_COORDINATE(C) (mul(detailRotation, (C)) * detailScaleRatio + float3(0.31, 0.57, 0.13))
#define TD_FLAME_NOISE(C) Texture3DSampleLevel(NoiseVolumeTexture, NoiseVolumeTextureSampler, (C), 0)
#define TD_RIDGE(V) pow(saturate(1.0 - abs(2.0 * (V) - 1.0)), ridgeSharpness)
#define TD_RIDGE_SUM(BASE, DETAIL) (0.35 * TD_RIDGE((BASE).b) + 0.3 * TD_RIDGE((BASE).a) + 0.2 * TD_RIDGE((DETAIL).b) + 0.15 * TD_RIDGE((DETAIL).a))
#define TD_BILLOW(V) pow(saturate(abs(2.0 * (V) - 1.0)), billowExponent)
#define TD_BILLOW_SUM(BASE, DETAIL) (0.35 * TD_BILLOW((BASE).b) + 0.3 * TD_BILLOW((BASE).a) + 0.2 * TD_BILLOW((DETAIL).b) + 0.15 * TD_BILLOW((DETAIL).a))
#define TD_WARP(BASE, DETAIL) (0.7 * ((BASE).rg * 2.0 - 1.0) + 0.3 * ((DETAIL).rg * 2.0 - 1.0))

float3 coordinateFirst = TD_FLAME_COORDINATE(phaseFirst);
float3 coordinateSecond = TD_FLAME_COORDINATE(phaseSecond);
float4 baseFirst = TD_FLAME_NOISE(coordinateFirst);
float4 baseSecond = TD_FLAME_NOISE(coordinateSecond);
float4 detailFirst = TD_FLAME_NOISE(TD_FLAME_DETAIL_COORDINATE(coordinateFirst));
float4 detailSecond = TD_FLAME_NOISE(TD_FLAME_DETAIL_COORDINATE(coordinateSecond));
float2 warpField = lerp(TD_WARP(baseSecond, detailSecond), TD_WARP(baseFirst, detailFirst), weightFirst);
float ridgeValue = lerp(TD_RIDGE_SUM(baseSecond, detailSecond), TD_RIDGE_SUM(baseFirst, detailFirst), weightFirst);
if (noiseBillow > 0.0)
{
    float billowValue = lerp(TD_BILLOW_SUM(baseSecond, detailSecond), TD_BILLOW_SUM(baseFirst, detailFirst), weightFirst);
    ridgeValue = lerp(ridgeValue, billowValue, noiseBillow);
}

bool usesPrimaryDensity = FlameDensitySource > 0.5;
float4 previewDensity = Texture2DSampleLevel(DensityPressureTexture, DensityPressureTextureSampler, baseUv, 0);
float previewValue = usesPrimaryDensity ? previewDensity.r : previewDensity.b;
float distortionCm = (0.3 + 0.7 * (1.0 - saturate(previewValue))) * FlameNoiseStrength;
float2 warpedPlaneCoordinate = planeCoordinate + float2(warpField.x, 0.6 * warpField.y) * distortionCm;
float2 warpedUv = saturate((warpedPlaneCoordinate - windowMinimum) / extentCm);

#define TD_DENSITY_TAP(U, V) Texture2DSampleLevel(DensityPressureTexture, DensityPressureTextureSampler, float2((U), (V)), 0)

float2 texelPosition = warpedUv * gridResolution;
float2 centerTexel = floor(texelPosition - 0.5) + 0.5;
float2 texelFraction = texelPosition - centerTexel;
float2 weightLow = texelFraction * (-0.5 + texelFraction * (1.0 - 0.5 * texelFraction));
float2 weightNear = 1.0 + texelFraction * texelFraction * (-2.5 + 1.5 * texelFraction);
float2 weightFar = texelFraction * (0.5 + texelFraction * (2.0 - 1.5 * texelFraction));
float2 weightHigh = texelFraction * texelFraction * (-0.5 + 0.5 * texelFraction);
float2 weightMiddle = weightNear + weightFar;
float2 uvMiddle = (centerTexel + weightFar / weightMiddle) / gridResolution;
float2 uvLow = (centerTexel - 1.0) / gridResolution;
float2 uvHigh = (centerTexel + 2.0) / gridResolution;
float tapWeightSum = weightMiddle.x * weightLow.y + weightLow.x * weightMiddle.y + weightMiddle.x * weightMiddle.y + weightHigh.x * weightMiddle.y + weightMiddle.x * weightHigh.y;
float4 cubicDensity = (TD_DENSITY_TAP(uvMiddle.x, uvLow.y) * (weightMiddle.x * weightLow.y)
    + TD_DENSITY_TAP(uvLow.x, uvMiddle.y) * (weightLow.x * weightMiddle.y)
    + TD_DENSITY_TAP(uvMiddle.x, uvMiddle.y) * (weightMiddle.x * weightMiddle.y)
    + TD_DENSITY_TAP(uvHigh.x, uvMiddle.y) * (weightHigh.x * weightMiddle.y)
    + TD_DENSITY_TAP(uvMiddle.x, uvHigh.y) * (weightMiddle.x * weightHigh.y)) / tapWeightSum;

float flameDensity = saturate(usesPrimaryDensity ? cubicDensity.r : cubicDensity.b);
float borderCoverage = saturate(cubicDensity.a);

float erosionAmount = clamp(FlameErosion, 0.0, 0.95);
float erodedDensity = saturate((flameDensity - erosionAmount * (1.0 - ridgeValue)) / (1.0 - erosionAmount));

bool usesHeat = AuxHeatActive > 0.5;
float heatSample = Texture2DSampleLevel(AuxTexture, AuxTextureSampler, warpedUv, 0).r * max(FlameHeatScale, 0.0);
float erodedHeat = saturate((heatSample - erosionAmount * (1.0 - ridgeValue)) / (1.0 - erosionAmount));
float baseSpeedHeatFactor = 0.8 + 0.2 * speedRatio;
float speedHeatFactor = baseSpeedHeatFactor + (saturate(FlameSpeedHeatFloor) - 0.8) * (1.0 - speedRatio);
float densityTemperatureBlend = saturate(pow(erodedDensity, max(FlameTemperatureCurve, 0.1)) * speedHeatFactor + 0.12 * (ridgeValue - 0.5));
float heatTemperatureBlend = saturate(erodedHeat * speedHeatFactor + 0.12 * (ridgeValue - 0.5));
float temperatureBlend = usesHeat ? heatTemperatureBlend : densityTemperatureBlend;
float temperatureKelvin = lerp(FlameTemperatureMinKelvin, FlameTemperatureMaxKelvin, temperatureBlend);

float3 wavelengthMicrometers = float3(0.610, 0.550, 0.465);
float3 radiationExponent = 14388.0 / (wavelengthMicrometers * max(temperatureKelvin, 300.0));
float3 radiationLevel = 1.0 / (pow(wavelengthMicrometers, 5.0) * (exp(radiationExponent) - 1.0));
float3 chromaColor = radiationLevel / max(dot(radiationLevel, float3(0.2126, 0.7152, 0.0722)), 1e-30);
float temperatureBrightness = pow(saturate(temperatureKelvin / max(FlameTemperatureMaxKelvin, 1.0)), max(FlameBrightnessExponent, 0.0));
float heatDensityExponent = clamp(FlameHeatDensityExponent, 0.05, 2.0);
float emissionCoverage = usesHeat ? pow(max(erodedDensity - 1e-4, 0.0), heatDensityExponent) : erodedDensity;
float heatGateRamp = smoothstep(0.0, 1.0, saturate((erodedHeat - FlameHeatGateLow) / max(FlameHeatGateHigh - FlameHeatGateLow, 0.001)));
float coldRidgeWeight = usesHeat ? saturate(FlameColdRidgeContrast) * (1.0 - heatGateRamp) : 0.0;
float coldRidgeTexture = lerp(1.0, saturate(2.0 * ridgeValue), coldRidgeWeight);
float heatGate = (usesHeat ? lerp(1.0, heatGateRamp, saturate(FlameHeatGate)) : 1.0) * coldRidgeTexture;
if (FlameColorRampMode > 0.5)
{
    float3 rampLuminanceWeights = float3(0.2126, 0.7152, 0.0722);
    float rampIndex = saturate(temperatureBlend * pow(max(emissionCoverage, 1e-6), max(FlameRampCoverageExponent, 0.0)));
    float rampWarmPosition = clamp(FlameRampWarmPosition, 0.001, 0.998);
    float rampHotPosition = clamp(FlameRampHotPosition, rampWarmPosition + 0.001, 0.999);
    float3 rampColdChroma = FlameRampColdColor / max(dot(FlameRampColdColor, rampLuminanceWeights), 1e-4);
    float3 rampWarmChroma = FlameRampWarmColor / max(dot(FlameRampWarmColor, rampLuminanceWeights), 1e-4);
    float3 rampHotChroma = FlameRampHotColor / max(dot(FlameRampHotColor, rampLuminanceWeights), 1e-4);
    float3 rampCoreChroma = FlameRampCoreColor / max(dot(FlameRampCoreColor, rampLuminanceWeights), 1e-4);
    float3 rampChroma = lerp(rampColdChroma, rampWarmChroma, saturate(rampIndex / rampWarmPosition));
    rampChroma = lerp(rampChroma, rampHotChroma, saturate((rampIndex - rampWarmPosition) / (rampHotPosition - rampWarmPosition)));
    rampChroma = lerp(rampChroma, rampCoreChroma, saturate((rampIndex - rampHotPosition) / (1.0 - rampHotPosition)));
    rampChroma = max(rampChroma, 0.0);
    chromaColor = rampChroma / max(dot(rampChroma, rampLuminanceWeights), 1e-4);
}
float3 flameEmission = chromaColor * (temperatureBrightness * FlameIntensity * emissionCoverage * heatGate);

float depthFade = saturate((SceneDepthCm - PixelDepthCm) / max(FlameDepthFadeCm, 1.0));
float edgeFade = borderCoverage * depthFade;
float smokeCoverage = saturate(flameDensity * FlameOpacityScale) * saturate(FlameSmokeOpacity);
float3 smokeRadiance = FlameSmokeColor * smokeCoverage;

return float4((flameEmission + smokeRadiance) * (edgeFade * CardIntensityScale), smokeCoverage * edgeFade);

#undef TD_DENSITY_TAP
#undef TD_WARP
#undef TD_BILLOW_SUM
#undef TD_BILLOW
#undef TD_RIDGE_SUM
#undef TD_RIDGE
#undef TD_FLAME_NOISE
#undef TD_FLAME_DETAIL_COORDINATE
#undef TD_FLAME_COORDINATE

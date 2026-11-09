#define TD_WATER_DECODE_SLOPE(PACKED) ((PACKED) / sqrt(max(1.0 - dot((PACKED), (PACKED)), 0.09)))
#define TD_WATER_CUBIC_FETCH(TEX, SMP) (pairLow.y * (pairLow.x * Texture2DSampleLevel(TEX, SMP, float2(tapLow.x, tapLow.y), 0) + pairHigh.x * Texture2DSampleLevel(TEX, SMP, float2(tapHigh.x, tapLow.y), 0)) + pairHigh.y * (pairLow.x * Texture2DSampleLevel(TEX, SMP, float2(tapLow.x, tapHigh.y), 0) + pairHigh.x * Texture2DSampleLevel(TEX, SMP, float2(tapHigh.x, tapHigh.y), 0)))
#define TD_WATER_RIBBON_SCALE(TILE_CM) (float2(max(round(ribbonStretchedSegmentCm / (TILE_CM)), 1.0), ribbonWidthSafeCm / (TILE_CM)))
#define TD_WATER_RIBBON_UV(SCALE, ALONG_OFFSET_CM) (float2(RibbonUv.x - (ALONG_OFFSET_CM) * ribbonInverseSegmentCm, RibbonUv.y) * (SCALE))

const float lakeDriftCmPerSecond = 5.0;
const float phaseNoiseTilingCm = 900.0;
const float secondOctaveWeight = 0.7;
const float causticStoreScale = 4.0;
const float3 foamChannelWeights = float3(0.6, 0.25, 0.15);
const float froudeMinimumDepthCm = 2.0;
const float farFoamOpacity = 0.6;
const float secondCausticDriftRatio = 0.8;

float viewDepthDeltaCm = max(SceneDepthWithoutWaterCm - PixelDepthCm, 0.0);
float verticalViewRatio = abs(WaterWorldPosition.z - CameraWorldPosition.z) / max(PixelDepthCm, 1.0);
float sceneThicknessCm = viewDepthDeltaCm * verticalViewRatio;
float windowThicknessCm = lerp(LiteFallbackThicknessCm, simField[0].z * WaterDepthReferenceCm, windowDepthKnown * simFade * waterLayerBlend);
float verticalThicknessCm = lerp(sceneThicknessCm, windowThicknessCm, saturate(LiteDepthMode));

float2 waterCellSizeCm = waterExtentCm / waterGridResolution;
float2 velocityTexelPosition = clamp(windowUv, 0.0, 1.0) * waterGridResolution - 0.5;
float2 velocityTexelFloor = floor(velocityTexelPosition);
float2 texelFraction = velocityTexelPosition - velocityTexelFloor;
float2 fractionSquared = texelFraction * texelFraction;
float2 fractionCubed = fractionSquared * texelFraction;
float2 weightA = (1.0 - 3.0 * texelFraction + 3.0 * fractionSquared - fractionCubed) / 6.0;
float2 weightB = (4.0 - 6.0 * fractionSquared + 3.0 * fractionCubed) / 6.0;
float2 weightC = (1.0 + 3.0 * texelFraction + 3.0 * fractionSquared - 3.0 * fractionCubed) / 6.0;
float2 weightD = fractionCubed / 6.0;
float2 pairLow = weightA + weightB;
float2 pairHigh = weightC + weightD;
float2 tapLow = (velocityTexelFloor - 0.5 + weightB / pairLow) / waterGridResolution;
float2 tapHigh = (velocityTexelFloor + 1.5 + weightD / pairHigh) / waterGridResolution;
float4 velocityCubic = TD_WATER_CUBIC_FETCH(VelocityTexture, VelocityTextureSampler);
float2 simVelocityCm = (velocityCubic.rg * 2.0 - 1.0) * VelocitySpeedReferenceCmPerSec * simMask;
float simSpeedRatio = saturate(velocityCubic.b) * simMask;

float gradientStepUv = 1.0 / waterGridResolution;
float2 velocityPlusX = Texture2DSampleLevel(VelocityTexture, VelocityTextureSampler, windowUv + float2(gradientStepUv, 0.0), 0).rg;
float2 velocityMinusX = Texture2DSampleLevel(VelocityTexture, VelocityTextureSampler, windowUv - float2(gradientStepUv, 0.0), 0).rg;
float2 velocityPlusY = Texture2DSampleLevel(VelocityTexture, VelocityTextureSampler, windowUv + float2(0.0, gradientStepUv), 0).rg;
float2 velocityMinusY = Texture2DSampleLevel(VelocityTexture, VelocityTextureSampler, windowUv - float2(0.0, gradientStepUv), 0).rg;
float velocityGradientPerSecond = VelocitySpeedReferenceCmPerSec * length(float4((velocityPlusX - velocityMinusX) / waterCellSizeCm.x, (velocityPlusY - velocityMinusY) / waterCellSizeCm.y)) * simMask;

float2 passiveDirection = float2(cos(waveBaseAngle), sin(waveBaseAngle));
float ribbonActive = step(0.001, abs(RibbonFlowCmPerSec));
float2 ribbonDirection = RibbonTangentWS.xy / max(length(RibbonTangentWS.xy), 0.0001);
float2 ribbonAcross = float2(-ribbonDirection.y, ribbonDirection.x);
float ribbonSimBlend = simMask * waterLayerBlend;
float2 ribbonFlowCm = ribbonDirection * RibbonFlowCmPerSec * (1.0 - ribbonSimBlend);
float2 flowCm = simVelocityCm * lerp(1.0, waterLayerBlend, ribbonActive) + ribbonFlowCm * ribbonActive + passiveDirection * (lakeDriftCmPerSecond * (1.0 - ribbonActive));

float flowPeriodSeconds = max(FlowPhasePeriodSeconds, 0.1);
float flowStretchLimit = min(1.0, MaxFlowStretch / max(velocityGradientPerSecond * flowPeriodSeconds, 0.0001));
float flowPhaseOffset = Texture2DSample(FlowPhaseNoiseTexture, FlowPhaseNoiseTextureSampler, WaterWorldPosition.xy / phaseNoiseTilingCm).r;
float flowPhaseFirst = frac(ElapsedSeconds / flowPeriodSeconds + flowPhaseOffset);
float flowPhaseSecond = frac(flowPhaseFirst + 0.5);
float flowWeightFirst = 1.0 - abs(1.0 - 2.0 * flowPhaseFirst);
float flowWeightSecond = 1.0 - flowWeightFirst;
float2 flowOffsetFirst = flowCm * flowStretchLimit * ((flowPhaseFirst - 0.5) * flowPeriodSeconds);
float2 flowOffsetSecond = flowCm * flowStretchLimit * ((flowPhaseSecond - 0.5) * flowPeriodSeconds);
float phaseVarianceBoost = rsqrt(flowWeightFirst * flowWeightFirst + flowWeightSecond * flowWeightSecond);

float ribbonSegmentSafeCm = max(RibbonSegmentLengthCm, 1.0);
float ribbonInverseSegmentCm = 1.0 / ribbonSegmentSafeCm;
float ribbonWidthSafeCm = max(RibbonWidthCm, 1.0);
float ribbonStretch = max(RibbonStretchAlong, 1.0);
float ribbonStretchedSegmentCm = ribbonSegmentSafeCm / ribbonStretch;
float alongOffsetFirstCm = dot(flowOffsetFirst, ribbonDirection);
float alongOffsetSecondCm = dot(flowOffsetSecond, ribbonDirection);
float localDetailWeight = saturate(bRibbonLocalFlowDetail);
float localCausticWeight = saturate(bRibbonLocalCaustics);
float localTilingWeight = saturate(bRibbonLocalTiling);

float2 detailTilingCm = max(DetailNormalTilingCm.xy, float2(10.0, 10.0));
float2 detailRibbonScale = TD_WATER_RIBBON_SCALE(detailTilingCm.x);
float2 detailScrollFirstCm = float2(cos(waveBaseAngle + 0.35), sin(waveBaseAngle + 0.35)) * (7.0 * ElapsedSeconds);
float2 detailScrollSecondCm = float2(cos(waveBaseAngle - 1.1), sin(waveBaseAngle - 1.1)) * (4.5 * ElapsedSeconds);

float2 detailUvFirstA = lerp((WaterWorldPosition.xy - flowOffsetFirst + detailScrollFirstCm) / detailTilingCm.x, TD_WATER_RIBBON_UV(detailRibbonScale, alongOffsetFirstCm), localDetailWeight);
float2 detailUvFirstB = lerp((WaterWorldPosition.xy - flowOffsetSecond + detailScrollFirstCm) / detailTilingCm.x, TD_WATER_RIBBON_UV(detailRibbonScale, alongOffsetSecondCm), localDetailWeight);
float4 detailFirstPhaseA = Texture2DSample(WaterDetailNormalTexture, WaterDetailNormalTextureSampler, detailUvFirstA);
float4 detailFirstPhaseB = Texture2DSample(WaterDetailNormalTexture, WaterDetailNormalTextureSampler, detailUvFirstB);
float2 detailPackedFirst = (flowWeightFirst * (detailFirstPhaseA.rg * 2.0 - 1.0) + flowWeightSecond * (detailFirstPhaseB.rg * 2.0 - 1.0)) * phaseVarianceBoost;
float2 detailSlopeTexture = TD_WATER_DECODE_SLOPE(detailPackedFirst);
float2 detailSlopeRibbon = ribbonDirection * (detailSlopeTexture.x / ribbonStretch) + ribbonAcross * detailSlopeTexture.y;
float2 detailSlope = lerp(detailSlopeTexture, detailSlopeRibbon, localDetailWeight);

if (bUseSecondDetailOctave > 0.5)
{
    float4 detailSecondPhaseA = Texture2DSample(WaterDetailNormalTexture, WaterDetailNormalTextureSampler, (WaterWorldPosition.xy - flowOffsetFirst + detailScrollSecondCm) / detailTilingCm.y);
    float4 detailSecondPhaseB = Texture2DSample(WaterDetailNormalTexture, WaterDetailNormalTextureSampler, (WaterWorldPosition.xy - flowOffsetSecond + detailScrollSecondCm) / detailTilingCm.y);
    float2 detailPackedSecond = (flowWeightFirst * (detailSecondPhaseA.rg * 2.0 - 1.0) + flowWeightSecond * (detailSecondPhaseB.rg * 2.0 - 1.0)) * phaseVarianceBoost;
    detailSlope += TD_WATER_DECODE_SLOPE(detailPackedSecond) * secondOctaveWeight;
}

float2 microSlope = float2(0.0, 0.0);
if (MicroNormalStrength > 0.0)
{
    float2 microScrollCm = float2(cos(waveBaseAngle + 2.1), sin(waveBaseAngle + 2.1)) * (11.0 * ElapsedSeconds);
    float4 microSample = Texture2DSample(WaterDetailNormalTexture, WaterDetailNormalTextureSampler, (float2(WaterWorldPosition.y, -WaterWorldPosition.x) + microScrollCm) / max(MicroNormalTilingCm, 5.0));
    microSlope = TD_WATER_DECODE_SLOPE(microSample.rg * 2.0 - 1.0) * MicroNormalStrength;
}

float nearWindowExtentCm = max(NearRippleWindow.z, 1.0);
float2 nearWindowOffsetCm = abs(WaterWorldPosition.xy - NearRippleWindow.xy);
float nearWindowInside = step(nearWindowOffsetCm.x, 0.5 * nearWindowExtentCm) * step(nearWindowOffsetCm.y, 0.5 * nearWindowExtentCm);
float2 nearSlope = float2(0.0, 0.0);
if (NearRippleActive * nearWindowInside > 0.5)
{
    float4 nearSample = Texture2DSampleLevel(NearRippleTexture, NearRippleTextureSampler, frac(WaterWorldPosition.xy / nearWindowExtentCm), 0);
    nearSlope = nearSample.gb * NearRippleNormalStrength;
}

float2 ringSlope = float2(0.0, 0.0);
float wakeFoam = 0.0;
if (AuxObstacleDistanceActive > 0.5)
{
    float auxRangeCm = max(ObstacleDistanceRangeCm, 1.0);
    float obstacleDistanceCm = Texture2DSampleLevel(AuxTexture, AuxTextureSampler, windowUv, 0).g * auxRangeCm;
    float obstacleDistancePlusX = Texture2DSampleLevel(AuxTexture, AuxTextureSampler, windowUv + float2(gradientStepUv, 0.0), 0).g * auxRangeCm;
    float obstacleDistancePlusY = Texture2DSampleLevel(AuxTexture, AuxTextureSampler, windowUv + float2(0.0, gradientStepUv), 0).g * auxRangeCm;
    float2 obstacleGradient = float2(obstacleDistancePlusX, obstacleDistancePlusY) - obstacleDistanceCm;
    float2 obstacleNormal = obstacleGradient / max(length(obstacleGradient), 0.0001);
    float obstacleFlowSpeedCm = length(flowCm);
    float2 obstacleFlowDirection = flowCm / max(obstacleFlowSpeedCm, 0.0001);
    float ringFlowRatio = saturate(obstacleFlowSpeedCm / max(ObstacleRingFlowSpeedCmPerSec, 1.0));
    float ringWavelengthCm = max(ObstacleRingWavelengthCm, 1.0);
    float ringCount = max(ObstacleRingCount, 1.0);
    float outsideDistanceCm = max(obstacleDistanceCm, 0.0);
    float ringCompression = 1.0 + ObstacleRingFlowSkew * ringFlowRatio * (-dot(obstacleNormal, obstacleFlowDirection));
    float ringDistanceCm = outsideDistanceCm * ringCompression;
    float ringPhase = TD_WATER_TWO_PI * (ringDistanceCm / ringWavelengthCm - frac(ObstacleRingSpeedCmPerSec * ElapsedSeconds / ringWavelengthCm));
    float ringEnvelope = smoothstep(0.0, 0.5 * ringWavelengthCm, obstacleDistanceCm) * exp(-outsideDistanceCm / max(ObstacleRingFalloffCm, 1.0)) * (1.0 - smoothstep((ringCount - 0.5) * ringWavelengthCm, (ringCount + 0.5) * ringWavelengthCm, ringDistanceCm));
    ringSlope = ObstacleRingNormalStrength * ringEnvelope * cos(ringPhase) * obstacleNormal * simMask;
    wakeFoam = ObstacleWakeFoamStrength * pow(saturate(dot(obstacleNormal, obstacleFlowDirection)), max(ObstacleWakeConeSharpness, 1.0)) * exp(-outsideDistanceCm / max(ObstacleWakeLengthCm, 1.0)) * ringFlowRatio * step(0.0, obstacleDistanceCm) * simMask;
}

float2 totalSlope = waveSlope + rippleSlope * RippleNormalStrength + detailSlope * DetailNormalStrength + microSlope + tileMapSlope + nearSlope + ringSlope;
float3 surfaceNormal = normalize(float3(-totalSlope, 1.0));

float foamTilingSafeCm = max(FoamTilingCm, 10.0);
float2 foamRibbonScale = TD_WATER_RIBBON_SCALE(foamTilingSafeCm);
float2 foamUvFirst = lerp((WaterWorldPosition.xy - flowOffsetFirst) / foamTilingSafeCm, TD_WATER_RIBBON_UV(foamRibbonScale, alongOffsetFirstCm), localTilingWeight);
float2 foamUvSecond = lerp((WaterWorldPosition.xy - flowOffsetSecond) / foamTilingSafeCm, TD_WATER_RIBBON_UV(foamRibbonScale, alongOffsetSecondCm), localTilingWeight);
float3 foamSampleFirst = Texture2DSample(FoamNoiseTexture, FoamNoiseTextureSampler, foamUvFirst).rgb;
float3 foamSampleSecond = Texture2DSample(FoamNoiseTexture, FoamNoiseTextureSampler, foamUvSecond).rgb;
if (FoamAnisotropyMax > 1.0001)
{
    float streakFlowSpeedCm = length(flowCm);
    float2 streakDirection = streakFlowSpeedCm < 1.0 ? passiveDirection : flowCm / streakFlowSpeedCm;
    float streakAnisotropy = lerp(1.0, FoamAnisotropyMax, saturate(streakFlowSpeedCm / max(FoamAnisotropySpeedCmPerSec, 1.0)));
    float2 streakOffsetUv = lerp(streakDirection, float2(1.0, 0.0), localTilingWeight) * (0.5 * (streakAnisotropy - 1.0) * max(FoamAnisotropyFeatureTiles, 0.001));
    foamSampleFirst = (foamSampleFirst + Texture2DSample(FoamNoiseTexture, FoamNoiseTextureSampler, foamUvFirst + streakOffsetUv).rgb + Texture2DSample(FoamNoiseTexture, FoamNoiseTextureSampler, foamUvFirst - streakOffsetUv).rgb) / 3.0;
    foamSampleSecond = (foamSampleSecond + Texture2DSample(FoamNoiseTexture, FoamNoiseTextureSampler, foamUvSecond + streakOffsetUv).rgb + Texture2DSample(FoamNoiseTexture, FoamNoiseTextureSampler, foamUvSecond - streakOffsetUv).rgb) / 3.0;
}
float foamNoise = saturate(0.5 + (dot(lerp(foamSampleSecond, foamSampleFirst, flowWeightFirst), foamChannelWeights) - 0.5) * 1.5 * phaseVarianceBoost);

float foamDepthSimCm = simField[0].z * WaterDepthReferenceCm;
float foamDepthCm = lerp(verticalThicknessCm, foamDepthSimCm, saturate(ShoreFoamSimDepthWeight) * windowDepthKnown * simFade * waterLayerBlend);
float shoreFoam = (1.0 - smoothstep(0.15 * ShoreFoamDepthCm, max(ShoreFoamDepthCm, 0.01), foamDepthCm)) * ShoreFoamAmount;

float coastFoam = 0.0;
if (CoastWaveMaxDepthCm > 0.0 && CoastFoamStrength > 0.0)
{
    float coastPhase = TD_WATER_TWO_PI * (foamDepthCm / max(CoastWaveDepthSpacingCm, 1.0) + CoastWavePhase);
    float coastCrest = smoothstep(CoastFoamThreshold, 1.0, max(0.0, sin(coastPhase)));
    float coastBreakup = GradientNoise3D_ALU(float3(WaterWorldPosition.xy / max(CoastBreakupScaleCm, 10.0), ElapsedSeconds * 0.2), false, 1.0) * 0.5 + 0.5;
    float coastShoal = 1.0 - smoothstep(0.6 * CoastWaveMaxDepthCm, CoastWaveMaxDepthCm, foamDepthCm);
    coastFoam = coastCrest * (1.0 - CoastBreakupAmount * (1.0 - coastBreakup)) * coastShoal * CoastFoamStrength;
}

float whitecapFoam = saturate((WhitecapJacobianThreshold - waveFoldDeterminant) / 0.2);
foamDensity = saturate(foamDensity + tileMapFoam);
float densityFoam = saturate((foamDensity - FoamDensityThreshold) / max(1.0 - FoamDensityThreshold, 0.01)) * FoamFromDensity;
float speedFoam = saturate(simSpeedRatio - FoamSpeedThreshold) * FoamFromSpeed;
float froudeNumber = length(flowCm) * rsqrt(gravityCmPerSecondSquared * max(verticalThicknessCm, froudeMinimumDepthCm));
float froudeFoam = saturate(froudeNumber - FoamFroudeThreshold) * FoamFromFroude;
float foamAmount = min(saturate(densityFoam + speedFoam + froudeFoam + shoreFoam + coastFoam + whitecapFoam + wakeFoam), FoamCoverageMax);
float foamThickness = 1.0;
float foamLaceMask = 0.0;
if (FoamLayerMode > 0.5)
{
    float3 foamLayers = saturate(0.5 + (lerp(foamSampleSecond, foamSampleFirst, flowWeightFirst) - 0.5) * phaseVarianceBoost);
    float foamCoverShare = saturate(foamAmount * (1.0 + (foamLayers.g - 0.5) * 2.0 * FoamPatchVariation));
    float laceSoftness = max(FoamLaceSoftness, 0.01);
    float laceThreshold = 1.0 - foamCoverShare;
    float laceMask = smoothstep(laceThreshold - laceSoftness, laceThreshold + laceSoftness, foamLayers.r) * step(0.0001, foamCoverShare);
    float coreWeight = smoothstep(FoamCoreStart, 1.0, foamAmount);
    float coreMask = coreWeight * smoothstep(laceThreshold - 0.15, laceThreshold + 0.15, foamLayers.g);
    float bubbleMask = smoothstep(1.0 - FoamBubbleShare, 1.0 - FoamBubbleShare + laceSoftness, foamLayers.b) * saturate(4.0 * foamAmount) * (1.0 - coreWeight);
    foamThickness = saturate(max(coreMask, (foamLayers.r - laceThreshold) / max(foamCoverShare, 0.05)));
    foamLaceMask = max(max(laceMask, bubbleMask), coreMask) * lerp(FoamThinOpacity, 1.0, foamThickness);
}
else
{
    foamLaceMask = smoothstep(1.0 - foamAmount, 1.0 - foamAmount + max(FoamEdgeSoftness, 0.02), foamNoise);
}
float foamFarBlend = saturate((PixelDepthCm - FoamFadeStartCm) / max(FoamFadeLengthCm, 1.0));
float foamMask = lerp(foamLaceMask, foamAmount * farFoamOpacity, foamFarBlend);

float dyeOpacity = saturate(simSelectiveDensity * DyeOpacity) * simMask;
float topOpacity = max(foamMask, dyeOpacity);
float foamShare = topOpacity > foamMask ? foamMask / topOpacity : 1.0;
float3 foamBaseColor = FoamColor;
if (FoamLayerMode > 0.5)
{
    foamBaseColor = lerp(FoamThinColor, FoamColor, foamThickness);
}
float3 topColor = lerp(DyeColor, foamBaseColor, foamShare);

float3 causticLight = float3(1.0, 1.0, 1.0);
float causticWindow = 0.0;
if (bUseCaustics > 0.5)
{
    float bedRayScale = clamp(SceneDepthWithoutWaterCm / max(PixelDepthCm, 1.0), 1.0, 64.0);
    float2 bedPosition = CameraWorldPosition.xy + (WaterWorldPosition.xy - CameraWorldPosition.xy) * bedRayScale;
    float2 causticDrift = passiveDirection * (CausticsDriftCmPerSecond * ElapsedSeconds);
    float causticTilingSafeCm = max(CausticsTilingCm, 10.0);
    float2 rotatedBed = float2(bedPosition.x * 0.7648 - bedPosition.y * 0.6442, bedPosition.x * 0.6442 + bedPosition.y * 0.7648);
    float2 causticRibbonScale = TD_WATER_RIBBON_SCALE(causticTilingSafeCm);
    float bedAlongShiftCm = dot(bedPosition - WaterWorldPosition.xy, ribbonDirection);
    float causticDriftAlongCm = CausticsDriftCmPerSecond * ElapsedSeconds;
    float2 causticLocalFirst = TD_WATER_RIBBON_UV(causticRibbonScale, causticDriftAlongCm - bedAlongShiftCm);
    float causticLocalSecondAlong = causticLocalFirst.x + (1.0 - secondCausticDriftRatio) * causticDriftAlongCm * ribbonInverseSegmentCm * causticRibbonScale.x;
    float2 causticUvFirst = lerp((bedPosition + causticDrift) / causticTilingSafeCm, causticLocalFirst, localCausticWeight);
    float2 causticUvSecond = lerp((rotatedBed + float2(causticDrift.y, -causticDrift.x) * secondCausticDriftRatio) / (causticTilingSafeCm * 1.37), float2(causticLocalFirst.y, -causticLocalSecondAlong) + 0.37, localCausticWeight);
    float3 causticFirst = Texture2DSample(CausticsTexture, CausticsTextureSampler, causticUvFirst).rgb;
    float3 causticSecond = Texture2DSample(CausticsTexture, CausticsTextureSampler, causticUvSecond).rgb;
    causticLight = min(causticFirst, causticSecond) * causticStoreScale;
    causticWindow = smoothstep(0.0, max(CausticsOnsetDepthCm, 0.01), verticalThicknessCm) * (1.0 - smoothstep(0.5 * CausticsMaxDepthCm, max(CausticsMaxDepthCm, 1.0), verticalThicknessCm));
}
float3 causticScale = max(1.0 + CausticsStrength * (causticLight - CausticsMeanLevel) * causticWindow, 0.2);

float edgeSoftness = smoothstep(0.0, max(EdgeSoftnessCm, 0.01), verticalThicknessCm);
float3 viewDirection = normalize(CameraWorldPosition - WaterWorldPosition);
float3 sunHalfVector = normalize(SunDirectionWS / max(length(SunDirectionWS), 0.0001) + viewDirection);
float3 swellNormal = normalize(float3(-waveSlope, 1.0));
float glintHotspot = pow(saturate(dot(swellNormal, sunHalfVector)), max(GlintHotspotSharpness, 1.0));
float glintLobe = pow(saturate(dot(surfaceNormal, sunHalfVector)), max(GlintSharpness, 1.0));
float3 glintEmission = SunIlluminance * (GlintIntensity * glintLobe * glintHotspot * (1.0 - foamMask) * edgeSoftness);
float detailRoughnessBoost = 0.3 * saturate(length(detailSlope * DetailNormalStrength));
float waterRoughnessValue = max(WaterRoughness + detailRoughnessBoost, WaterRoughnessMin);
float liteDepthBlend = 1.0 - exp(-dot(WaterExtinctionPerCm, float3(0.3333, 0.3333, 0.3333)) * 3.0 * verticalThicknessCm);
float denseActive = saturate(DenseWaterActive);
float denseDepthCm = simField[0].z * WaterDepthReferenceCm;
float denseCoverBlend = denseActive * windowDepthKnown;
float denseCover = lerp(1.0, smoothstep(DenseWaterWetThresholdCm, DenseWaterWetThresholdCm + max(DenseEdgeSoftnessCm, 0.01), denseDepthCm), denseCoverBlend);

OutNormalWS = normalize(lerp(surfaceNormal, float3(0.0, 0.0, 1.0), FoamNormalFlatten * foamMask));
OutBaseColor = topColor;
OutRoughness = lerp(waterRoughnessValue, FoamRoughness, foamMask);
OutSpecular = SpecularScale * edgeSoftness;
OutTopOpacity = topOpacity * denseCover;
OutColorScaleBehindWater = causticScale;
OutEmissive = glintEmission;
OutLiteColor = lerp(lerp(LiteBedColor, WaterScatteringAlbedo, liteDepthBlend), DyeColor, dyeOpacity);
OutTranslucentOpacity = saturate(max(topOpacity, liteDepthBlend * edgeSoftness)) * denseCover;
OutTranslucentColor = lerp(WaterScatteringAlbedo, topColor, topOpacity);

if (FoamDebugView > 0.5)
{
    const float foamDebugEmissiveScale = 4.8;
    float3 foamDebugColor = FoamDebugView < 1.5 ? float3(densityFoam + speedFoam + froudeFoam + wakeFoam, shoreFoam + coastFoam, whitecapFoam) : (FoamDebugView < 2.5 ? foamAmount.xxx : (FoamDebugView < 3.5 ? foamMask.xxx : float3(tileMapFoam, length(tileMapSlope), 0.0)));
    foamDebugColor = saturate(foamDebugColor);
    OutBaseColor = float3(0.0, 0.0, 0.0);
    OutEmissive = foamDebugColor * foamDebugEmissiveScale;
    OutTopOpacity = 1.0;
    OutRoughness = 1.0;
    OutSpecular = 0.0;
    OutLiteColor = foamDebugColor;
    OutTranslucentOpacity = 1.0;
    OutTranslucentColor = foamDebugColor;
}

return float4(foamMask, verticalThicknessCm, simMask, causticLight.g * causticWindow);

float2 extentCm = max(SimExtentsCm.xy, float2(1.0, 1.0));
float2 windowMinimum = SimCenterWorld.xy - 0.5 * extentCm;
float3 pixelPlane = float3(dot(PixelWorldPosition, PlaneAxisU), dot(PixelWorldPosition, PlaneAxisV), dot(PixelWorldPosition, PlaneAxisN)) + float3(PlaneCoordinateOffsetCm.xy, 0.0);
float3 cameraPlane = float3(dot(CameraWorldPosition, PlaneAxisU), dot(CameraWorldPosition, PlaneAxisV), dot(CameraWorldPosition, PlaneAxisN)) + float3(PlaneCoordinateOffsetCm.xy, 0.0);
float2 baseUv = (pixelPlane.xy - windowMinimum) / extentCm;

if (any(abs(baseUv - 0.5) > 0.5005))
{
    return float4(0.0, 0.0, 0.0, 0.0);
}

float flowCycleSeconds = 1.2;
float phaseFirst = frac(ElapsedSeconds / flowCycleSeconds);
float phaseSecond = frac(phaseFirst + 0.5);
float weightFirst = 1.0 - abs(1.0 - 2.0 * phaseFirst);
float4 flowSample = Texture2DSampleLevel(VelocityTexture, VelocityTextureSampler, baseUv, 0);
float2 flowCmPerSec = (flowSample.rg * 2.0 - 1.0) * (600.0 * NoiseFlowSpeed);

#define TD_FAKE_NOISE(PHASE) Texture3DSampleLevel(NoiseVolumeTexture, NoiseVolumeTextureSampler, float3((pixelPlane.xy - flowCmPerSec * ((PHASE) * flowCycleSeconds)) * NoiseScale, 0.05 * ElapsedSeconds) * 0.25, 0)
#define TD_FAKE_DETAIL(S) (0.55 * (S).r + 0.3 * (S).b + 0.15 * (S).a)
#define TD_FAKE_HG(C, G) ((1.0 - (G) * (G)) / pow(max(1.0 + (G) * (G) - 2.0 * (G) * (C), 0.0001), 1.5))

float noiseDetail = lerp(TD_FAKE_DETAIL(TD_FAKE_NOISE(phaseSecond)), TD_FAKE_DETAIL(TD_FAKE_NOISE(phaseFirst)), weightFirst);
bool usesPrimaryDensity = FakeDensitySource > 0.5;
float4 centerSample = Texture2DSampleLevel(DensityPressureTexture, DensityPressureTextureSampler, baseUv, 0);
float rawDensity = (usesPrimaryDensity ? centerSample.r : centerSample.b) * saturate(centerSample.a);
float noiseAmount = saturate(NoiseStrength);
float fakeDensity = saturate(rawDensity * (1.0 - noiseAmount + 2.0 * noiseAmount * noiseDetail));
float thicknessCm = max(FakeThicknessCm, 1.0);
float columnHeightCm = fakeDensity * thicknessCm;
float extinctionPerCm = max(DensityScale, 0.0);
float3 extinctionTint = max(ExtinctionTint, float3(0.0, 0.0, 0.0));

float3 viewVector = pixelPlane - cameraPlane;
float3 rayDirection = viewVector / max(length(viewVector), 0.001);
float viewCosine = max(abs(rayDirection.z), 0.2);
float viewOpticalDepth = extinctionPerCm * columnHeightCm / viewCosine;

float lightModeValue = floor(VolumeLightMode + 0.5);
bool hasLight = lightModeValue > 0.5;
bool isPointLight = hasLight && lightModeValue < 1.5;
float3 lightPlane = float3(dot(VolumeLightPosition, PlaneAxisU), dot(VolumeLightPosition, PlaneAxisV), dot(VolumeLightPosition, PlaneAxisN)) + float3(PlaneCoordinateOffsetCm.xy, 0.0);
float3 lightDirectionPlane = float3(dot(VolumeLightDirection, PlaneAxisU), dot(VolumeLightDirection, PlaneAxisV), dot(VolumeLightDirection, PlaneAxisN));
float3 towardLight = isPointLight ? lightPlane - pixelPlane : -lightDirectionPlane;
float towardLightLength = max(length(towardLight), 0.001);
float3 lightVector = towardLight / towardLightLength;
float horizontalLength = max(length(lightVector.xy), 0.0001);
float2 marchDirectionUv = (lightVector.xy / horizontalLength) / extentCm;
float riseSlope = abs(lightVector.z) / horizontalLength;
float lightHorizontalCm = isPointLight ? towardLightLength * horizontalLength : 100000.0;
float shadowStepCount = clamp(floor(ShadowSteps + 0.5), 1.0, 16.0);
int shadowStepTotal = (int)shadowStepCount;
float shadowStepCm = min(max(FakeShadowLengthCm, 1.0), lightHorizontalCm) / shadowStepCount;
float shadowSoftnessCm = 0.1 * thicknessCm;
float ditherFrame = floor(fmod(ElapsedSeconds * 60.0, 8.0));
float2 ditherCoord = Parameters.SvPosition.xy + 5.588238 * ditherFrame;
float shadowJitter = frac(52.9829189 * frac(dot(ditherCoord, float2(0.06711056, 0.00583715))));

float3 litRadiance = float3(0.0, 0.0, 0.0);
if (hasLight)
{
    float lightOpticalDepth = extinctionPerCm * 0.5 * columnHeightCm / max(abs(lightVector.z), 0.2);
    [loop]
    for (int shadowIndex = 0; shadowIndex < shadowStepTotal; shadowIndex++)
    {
        float marchCm = shadowStepCm * (float(shadowIndex) + 0.5 + shadowJitter);
        float2 marchUv = baseUv + marchDirectionUv * marchCm;
        if (any(abs(marchUv - 0.5) > 0.5))
        {
            break;
        }
        float4 marchSample = Texture2DSampleLevel(DensityPressureTexture, DensityPressureTextureSampler, marchUv, 0);
        float occluderHeightCm = 0.5 * (usesPrimaryDensity ? marchSample.r : marchSample.b) * saturate(marchSample.a) * thicknessCm;
        float rayHeightCm = marchCm * riseSlope;
        lightOpticalDepth += extinctionPerCm * shadowStepCm * smoothstep(-shadowSoftnessCm, shadowSoftnessCm, occluderHeightCm - rayHeightCm);
    }
    bool isLitFromBehind = (lightVector.z > 0.0) != (cameraPlane.z - pixelPlane.z > 0.0);
    lightOpticalDepth += isLitFromBehind ? extinctionPerCm * 0.5 * columnHeightCm / max(abs(lightVector.z), 0.2) : 0.0;
    float distanceRatio = isPointLight ? towardLightLength / max(VolumeLightRadius, 1.0) : 0.0;
    float rangeWindow = saturate(1.0 - distanceRatio * distanceRatio * distanceRatio * distanceRatio);
    float lightAttenuation = rangeWindow * rangeWindow / (1.0 + 6.0 * distanceRatio * distanceRatio);
    float scatterCosine = dot(rayDirection, lightVector);
    float phaseValue = lerp(TD_FAKE_HG(scatterCosine, clamp(PhaseBackwardG, -0.95, 0.95)), TD_FAKE_HG(scatterCosine, clamp(PhaseForwardG, -0.95, 0.95)), saturate(PhaseForwardWeight));
    litRadiance = VolumeLightColor * (lightAttenuation * phaseValue) * exp(-lightOpticalDepth * extinctionTint);
}

float pulse = max(1.0 + PulseDepth * sin(ElapsedSeconds * PulseRate - fakeDensity * 6.2831853), 0.0);
float3 emission = EmissionColor * (pow(fakeDensity, max(EmissionPower, 0.1)) * pulse);
float depthFade = saturate((SceneDepthCm - PixelDepthCm) / max(DepthFadeCm, 1.0));
float opacity = (1.0 - exp(-viewOpticalDepth)) * depthFade;
float3 radiance = SmokeColor * (AmbientColor + litRadiance) + ShadowColor;

return float4(radiance * opacity + emission * depthFade, opacity);

#undef TD_FAKE_HG
#undef TD_FAKE_DETAIL
#undef TD_FAKE_NOISE

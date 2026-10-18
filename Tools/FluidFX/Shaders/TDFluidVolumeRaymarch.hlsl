#define TD_TO_PLANE(P) float3(dot((P), PlaneAxisU), dot((P), PlaneAxisV), dot((P), PlaneAxisN))

float3 tdCameraPosition = TD_TO_PLANE(CameraWorldPosition);
float3 tdPixelPosition = TD_TO_PLANE(PixelWorldPosition);
float3 tdLightPosition = TD_TO_PLANE(VolumeLightPosition);
float3 tdLightDirection = TD_TO_PLANE(VolumeLightDirection);
float3 tdWorldUp = TD_TO_PLANE(float3(0.0, 0.0, 1.0));

bool isSymmetric = ExtrudeSymmetric > 0.5;
float boxFloorZ = isSymmetric ? SimCenterWorld.z - 0.5 * VolumeHeightCm : SimCenterWorld.z;
float3 boxMin = float3(SimCenterWorld.xy - 0.5 * SimExtentsCm.xy, boxFloorZ);
float3 boxMax = float3(SimCenterWorld.xy + 0.5 * SimExtentsCm.xy, boxFloorZ + VolumeHeightCm);
float2 uvScale = 1.0 / max(SimExtentsCm.xy, 1.0);

float3 toPixel = tdPixelPosition - tdCameraPosition;
float pixelDistance = max(length(toPixel), 0.001);
float3 rayDirection = toPixel / pixelDistance;
float3 rayDirectionSafe = rayDirection + 1e-5 * (step(0.0, rayDirection) * 2.0 - 1.0);
float3 slabFirst = (boxMin - tdCameraPosition) / rayDirectionSafe;
float3 slabSecond = (boxMax - tdCameraPosition) / rayDirectionSafe;
float rawEnter = max(max(min(slabFirst.x, slabSecond.x), min(slabFirst.y, slabSecond.y)), min(slabFirst.z, slabSecond.z));
float rayExit = min(min(max(slabFirst.x, slabSecond.x), max(slabFirst.y, slabSecond.y)), max(slabFirst.z, slabSecond.z));

if (rayExit <= 0.0 || pixelDistance < 0.5 * (rawEnter + rayExit))
{
    return float4(0.0, 0.0, 0.0, 0.0);
}

float rayStart = max(rawEnter, 0.0);
float sceneDistance = SceneDepthCm * pixelDistance / max(PixelDepthCm, 1.0);
float rayEnd = min(rayExit, sceneDistance);
if (rayEnd <= rayStart)
{
    return float4(0.0, 0.0, 0.0, 0.0);
}

float ditherFrame = floor(fmod(ElapsedSeconds * 60.0, 8.0));
float2 ditherCoord = Parameters.SvPosition.xy + 5.588238 * ditherFrame;
float rayJitter = frac(52.9829189 * frac(dot(ditherCoord, float2(0.06711056, 0.00583715))));
float shadowJitter = frac(rayJitter * 7.13 + 0.37);

float velocityReferenceCmPerSec = 600.0;
float flowCycleSeconds = 1.2;
float noiseDriftCmPerSec = 6.0;
float phaseFirst = frac(ElapsedSeconds / flowCycleSeconds);
float phaseSecond = frac(phaseFirst + 0.5);
float weightFirst = 1.0 - abs(1.0 - 2.0 * phaseFirst);
float3 noiseDrift = -(ElapsedSeconds * noiseDriftCmPerSec) * tdWorldUp;
float noiseAmplitudeCm = NoiseStrength * VolumeHeightCm * 0.3;
float baseHeightCm = VolumeHeightCm * 0.68;
float heightScaleCm = isSymmetric ? 0.5 * baseHeightCm : baseHeightCm;
float shadowShiftCm = 0.0;
float edgeFadeFraction = 0.24;

float surfaceSoftness = max(SurfaceSoftnessCm, 0.5);
float softFalloff = max(SoftFalloffCm, 1.0);
bool isHardMode = RenderMode < 0.5;
bool isVolumeMode = RenderMode > 1.5;
float volumeCoreDepthCm = 0.6 * noiseAmplitudeCm;
float noiseSkipDepth = isHardMode ? -noiseAmplitudeCm : -noiseAmplitudeCm - 4.0 * softFalloff;
float distanceFade = saturate((4200.0 - rayStart) / 1600.0);

float lightModeValue = floor(VolumeLightMode + 0.5);
bool hasLight = lightModeValue > 0.5;
bool isPointLight = lightModeValue > 0.5 && lightModeValue < 1.5;
float shadowStepCount = clamp(floor(ShadowSteps + 0.5), 1.0, 16.0);
int shadowStepTotal = (int)shadowStepCount;

#define TD_UV(P) saturate(((P).xy - boxMin.xy) * uvScale)
#define TD_DENSITY(P) Texture2DSampleLevel(DensityPressureTexture, DensityPressureTextureSampler, TD_UV(P), 0)
#define TD_FLOW(P) (float3(Texture2DSampleLevel(VelocityTexture, VelocityTextureSampler, TD_UV(P), 0).rg * 2.0 - 1.0, 0.0) * (velocityReferenceCmPerSec * NoiseFlowSpeed))
#define TD_BILLOW(P, OFFSET, SCALE) (2.0 * abs(GradientNoise3D_ALU((((P) - (OFFSET)) + noiseDrift) * (NoiseScale * (SCALE)), false, 1.0)) - 0.6)
#define TD_NOISE_SHIFT(P) (noiseAmplitudeCm * (0.78 * lerp(TD_BILLOW(P, TD_FLOW(P) * (phaseSecond * flowCycleSeconds), 1.0), TD_BILLOW(P, TD_FLOW(P) * (phaseFirst * flowCycleSeconds), 1.0), weightFirst) + 0.22 * TD_BILLOW(P, TD_FLOW(P) * (phaseFirst * flowCycleSeconds), 2.31)))
#define TD_EDGE(P) smoothstep(0.0, edgeFadeFraction, min(min(TD_UV(P).x, 1.0 - TD_UV(P).x), min(TD_UV(P).y, 1.0 - TD_UV(P).y)) + edgeFadeFraction * (0.9 * GradientNoise3D_ALU(float3((P).xy * (NoiseScale * 0.55), 0.03 * ElapsedSeconds), false, 1.0) + 0.45 * GradientNoise3D_ALU(float3((P).xy * (NoiseScale * 1.6), 0.05 * ElapsedSeconds + 7.0), false, 1.0)))
#define TD_COVERAGE(P, D) saturate((D).r * (D).a * TD_EDGE(P) * 2.5)
#define TD_HEIGHT_OFFSET(P) (isSymmetric ? abs((P).z - SimCenterWorld.z) : ((P).z - boxMin.z))
#define TD_TOP_MARGIN(P) (isSymmetric ? 0.5 * VolumeHeightCm - abs((P).z - SimCenterWorld.z) : boxMax.z - (P).z)
#define TD_RAW_DEPTH(P, D) ((D).r * (D).a * TD_EDGE(P) * heightScaleCm - TD_HEIGHT_OFFSET(P))

float hardStepCount = isHardMode ? clamp(floor(StepCount * 0.5 + 0.5), 8.0, 96.0) : clamp(floor(StepCount + 0.5), 8.0, 160.0);
float haloTransmittance = 1.0;
float3 haloColor = float3(0.0, 0.0, 0.0);
int hardStepTotal = (int)hardStepCount;
float hardStepLength = (rayEnd - rayStart) / hardStepCount;
float lowT = rayStart;
float highT = rayStart;
bool hasEntered = false;

[loop]
for (int marchIndex = 0; marchIndex < hardStepTotal; marchIndex++)
{
    float marchT = rayStart + hardStepLength * (float(marchIndex) + rayJitter);
    float3 marchPoint = tdCameraPosition + rayDirection * marchT;
    float4 marchDensity = TD_DENSITY(marchPoint);
    float marchDepth = TD_RAW_DEPTH(marchPoint, marchDensity);
    if (marchDepth > noiseSkipDepth && !(isVolumeMode && marchDepth > volumeCoreDepthCm))
    {
        marchDepth += TD_COVERAGE(marchPoint, marchDensity) * TD_NOISE_SHIFT(marchPoint);
    }
    if (!isVolumeMode && marchDepth > 0.0)
    {
        highT = marchT;
        hasEntered = true;
        break;
    }
    lowT = marchT;
    if (!isHardMode)
    {
        float haloCoverage = TD_COVERAGE(marchPoint, marchDensity);
        float haloTopFade = saturate(TD_TOP_MARGIN(marchPoint) / (0.15 * VolumeHeightCm));
        float haloSigma = DensityScale * haloCoverage * haloTopFade * exp(min(marchDepth, 0.0) / softFalloff);
        if (haloSigma > 0.0005)
        {
            float haloAlpha = 1.0 - exp(-haloSigma * hardStepLength);
            float3 haloLit = float3(0.0, 0.0, 0.0);
            if (hasLight)
            {
                float3 haloToLight = isPointLight ? (tdLightPosition - marchPoint) : (-tdLightDirection);
                float haloLightLength = max(length(haloToLight), 0.0001);
                float3 haloLightVector = haloToLight / haloLightLength;
                float haloRatio = isPointLight ? haloLightLength / max(VolumeLightRadius, 1.0) : 0.0;
                float haloWindow = saturate(1.0 - haloRatio * haloRatio * haloRatio * haloRatio);
                float haloAttenuation = haloWindow * haloWindow / (1.0 + 6.0 * haloRatio * haloRatio);
                haloLit = VolumeLightColor * (haloAttenuation * (0.6 + 1.6 * pow(saturate(dot(rayDirection, haloLightVector)), 4.0)));
            }
            float3 haloRadiance = SmokeColor * (AmbientColor * 0.9 + haloLit) + ShadowColor;
            haloColor += haloRadiance * (haloTransmittance * haloAlpha);
            haloTransmittance *= 1.0 - haloAlpha;
            if (isVolumeMode && haloTransmittance < 0.01)
            {
                break;
            }
        }
    }
}

if (!hasEntered)
{
    float haloOnlyAlpha = 1.0 - haloTransmittance;
    return float4(haloColor / max(haloOnlyAlpha, 0.0001), haloOnlyAlpha * distanceFade);
}

[loop]
for (int refineIndex = 0; refineIndex < 6; refineIndex++)
{
    float refineT = 0.5 * (lowT + highT);
    float3 refinePoint = tdCameraPosition + rayDirection * refineT;
    float4 refineDensity = TD_DENSITY(refinePoint);
    float refineDepth = TD_RAW_DEPTH(refinePoint, refineDensity);
    if (refineDepth > noiseSkipDepth)
    {
        refineDepth += TD_COVERAGE(refinePoint, refineDensity) * TD_NOISE_SHIFT(refinePoint);
    }
    if (refineDepth > 0.0)
    {
        highT = refineT;
    }
    else
    {
        lowT = refineT;
    }
}

float3 surfacePoint = tdCameraPosition + rayDirection * highT;

float probeDistance = surfaceSoftness / max(abs(rayDirection.z), 0.3);
float3 probePoint = surfacePoint + rayDirection * probeDistance;
float4 probeDensity = TD_DENSITY(probePoint);
float probeDepth = TD_RAW_DEPTH(probePoint, probeDensity);
if (probeDepth > noiseSkipDepth)
{
    probeDepth += TD_COVERAGE(probePoint, probeDensity) * TD_NOISE_SHIFT(probePoint);
}
float surfaceAlpha = saturate(probeDepth / surfaceSoftness);

float normalEpsilon = max(surfaceSoftness, 2.0);
float3 depthGradient = float3(0.0, 0.0, 0.0);
[loop]
for (int axisIndex = 0; axisIndex < 3; axisIndex++)
{
    float3 axisUnit = float3(axisIndex == 0 ? 1.0 : 0.0, axisIndex == 1 ? 1.0 : 0.0, axisIndex == 2 ? 1.0 : 0.0);
    float3 tapPoint = surfacePoint + axisUnit * normalEpsilon;
    float4 tapDensity = TD_DENSITY(tapPoint);
    float tapDepth = TD_RAW_DEPTH(tapPoint, tapDensity) + TD_COVERAGE(tapPoint, tapDensity) * TD_NOISE_SHIFT(tapPoint);
    depthGradient += axisUnit * (tapDepth / normalEpsilon);
}
float3 surfaceNormal = normalize(-depthGradient + float3(0.0, 0.0, 0.0001));
float surfaceBillow = TD_NOISE_SHIFT(surfacePoint) / max(noiseAmplitudeCm, 0.001);
float crevice = saturate(0.62 + 0.38 * surfaceBillow);

float3 hardLit = float3(0.0, 0.0, 0.0);
if (hasLight)
{
    float3 shadowOrigin = surfacePoint + surfaceNormal * (surfaceSoftness + 0.6 * noiseAmplitudeCm + 1.0);
    float3 towardLight = isPointLight ? (tdLightPosition - shadowOrigin) : (-tdLightDirection);
    float towardLightLength = max(length(towardLight), 0.0001);
    float3 lightVector = towardLight / towardLightLength;
    float lightDistance = isPointLight ? towardLightLength : 100000.0;
    float distanceRatio = isPointLight ? lightDistance / max(VolumeLightRadius, 1.0) : 0.0;
    float rangeWindow = saturate(1.0 - distanceRatio * distanceRatio * distanceRatio * distanceRatio);
    float lightAttenuation = rangeWindow * rangeWindow / (1.0 + 6.0 * distanceRatio * distanceRatio);

    if (lightAttenuation > 0.001)
    {
        float3 lightVectorSafe = lightVector + 1e-5 * (step(0.0, lightVector) * 2.0 - 1.0);
        float3 lightSlabFirst = (boxMin - shadowOrigin) / lightVectorSafe;
        float3 lightSlabSecond = (boxMax - shadowOrigin) / lightVectorSafe;
        float lightBoxExit = min(min(max(lightSlabFirst.x, lightSlabSecond.x), max(lightSlabFirst.y, lightSlabSecond.y)), max(lightSlabFirst.z, lightSlabSecond.z));
        float shadowLength = max(min(lightBoxExit, lightDistance), 0.0);
        float shadowStepLength = shadowLength / shadowStepCount;
        float hardOpticalDepth = 0.0;

        [loop]
        for (int shadowIndex = 0; shadowIndex < shadowStepTotal; shadowIndex++)
        {
            float3 shadowPoint = shadowOrigin + lightVector * (shadowStepLength * (float(shadowIndex) + shadowJitter));
            float4 shadowDensity = TD_DENSITY(shadowPoint);
            float shadowDepth = TD_RAW_DEPTH(shadowPoint, shadowDensity) - shadowShiftCm;
            hardOpticalDepth += DensityScale * 3.0 * TD_COVERAGE(shadowPoint, shadowDensity) * smoothstep(-surfaceSoftness, surfaceSoftness, shadowDepth) * shadowStepLength;
        }

        float hardTransmittance = exp(-hardOpticalDepth);
        float diffuseTerm = saturate((dot(surfaceNormal, lightVector) + 0.35) / 1.35);
        float forwardGlow = pow(saturate(dot(rayDirection, lightVector)), 5.0) * (1.0 - surfaceAlpha * 0.5);
        hardLit = VolumeLightColor * (lightAttenuation * (diffuseTerm * hardTransmittance + 0.8 * forwardGlow));
    }
}

float ambientFactor = (0.35 + 0.65 * saturate(dot(surfaceNormal, tdWorldUp))) * crevice;
float3 hardColor = SmokeColor * (AmbientColor * ambientFactor + hardLit * (0.55 + 0.45 * crevice)) + ShadowColor;
float coreAlpha = surfaceAlpha;
float combinedAlpha = 1.0 - haloTransmittance * (1.0 - coreAlpha);
float3 combinedColor = haloColor + haloTransmittance * hardColor * coreAlpha;
return float4(combinedColor / max(combinedAlpha, 0.0001), combinedAlpha * distanceFade);


#undef TD_RAW_DEPTH
#undef TD_TOP_MARGIN
#undef TD_HEIGHT_OFFSET
#undef TD_COVERAGE
#undef TD_EDGE
#undef TD_NOISE_SHIFT
#undef TD_BILLOW
#undef TD_FLOW
#undef TD_DENSITY
#undef TD_UV
#undef TD_TO_PLANE

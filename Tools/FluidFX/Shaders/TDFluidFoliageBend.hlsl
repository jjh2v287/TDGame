float3 tdBendArm = BendWorldPosition - BendRootWorld;
float tdBendBoundsHeight = max(BendLocalBoundsMax.z - BendLocalBoundsMin.z, 0.001);
float tdBendHeight = FoliageBladeHeightCm > 0.0 ? saturate(length(tdBendArm) / FoliageBladeHeightCm) : saturate((BendLocalPosition.z - BendLocalBoundsMin.z) / tdBendBoundsHeight);
float2 tdBendExtent = FoliageWindowExtents.xy;
float2 tdBendUv = (BendRootWorld.xy - FoliageWindowCenter.xy) / max(tdBendExtent, 1.0) + 0.5;
float2 tdBendEdge = 0.5 - abs(tdBendUv - 0.5);
float tdBendMinExtent = min(tdBendExtent.x, tdBendExtent.y);
float tdBendEdgeCm = min(tdBendEdge.x, tdBendEdge.y) * tdBendMinExtent;
float tdBendActive = step(0.5, FoliageFlowState.x) * step(0.001, tdBendExtent.x);
float tdBendWeight = tdBendActive * smoothstep(0.0, max(FoliageWindowEdgeFadeRatio * tdBendMinExtent, 0.001), tdBendEdgeCm);
float2 tdBendSim = (2.0 * Texture2DSampleLevel(FoliageFlowTexture, FoliageFlowTextureSampler, saturate(tdBendUv), 0.0).rg - 1.0) * 90.0;
float2 tdWindDirection = FoliageWindDirection.xy / max(length(FoliageWindDirection.xy), 0.0001);
float2 tdWindSide = float2(-tdWindDirection.y, tdWindDirection.x);
float tdWindGustScale = max(FoliageWindGustScaleCm, 1.0);
float tdWindAlong = dot(BendRootWorld.xy, tdWindDirection) / tdWindGustScale;
float tdWindAcross = dot(BendRootWorld.xy, tdWindSide) / tdWindGustScale;
float tdWindPhase = FoliageWindSpeed * ElapsedSeconds;
float tdWindGust = 0.5 + 0.25 * sin(6.28318531 * (tdWindAlong - 0.37 * tdWindPhase)) + 0.25 * sin(6.28318531 * (1.7 * tdWindAcross + 0.23 * tdWindPhase));
float tdWindAmount = FoliageWindAmplitude * (0.5 + 0.5 * sin(6.28318531 * tdWindPhase + 0.013 * BendRootWorld.x + 0.021 * BendRootWorld.y)) * lerp(1.0, tdWindGust, FoliageWindGustStrength);
float2 tdWindBend = tdWindDirection * FoliageWindMaxDegrees * tdWindAmount;
float2 tdBendVector = tdWindBend * (1.0 - tdBendWeight * FoliageSimReplacesWind) + tdBendSim * tdBendWeight * FoliageBendScale;
float tdBendLength = length(tdBendVector);
float tdBendDegrees = min(tdBendLength, 85.0);
float2 tdBendDirection = tdBendVector / max(tdBendLength, 0.0001);
float tdBendAngle = radians(tdBendDegrees) * pow(tdBendHeight, max(FoliageBendHeightExponent, 0.01));
BendAmount = tdBendDegrees * tdBendWeight / 90.0;
if (tdBendAngle < 0.000001)
{
return float3(0.0, 0.0, 0.0);
}
float3 tdBendAxisRaw = cross(float3(0.0, 0.0, 1.0), float3(tdBendDirection, 0.0));
float3 tdBendAxis = tdBendAxisRaw * rsqrt(max(dot(tdBendAxisRaw, tdBendAxisRaw), 1e-12));
float tdBendCos = cos(tdBendAngle);
float tdBendSin = sin(tdBendAngle);
float3 tdBendRotated = tdBendArm * tdBendCos + cross(tdBendAxis, tdBendArm) * tdBendSin + tdBendAxis * dot(tdBendAxis, tdBendArm) * (1.0 - tdBendCos);
return tdBendRotated - tdBendArm;

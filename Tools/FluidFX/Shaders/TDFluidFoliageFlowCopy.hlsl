float4 tdFlowPacked = Texture2DSampleLevel(VelocityTexture, VelocityTextureSampler, FlowUv, 0.0);
float2 tdFlowPlane = (2.0 * tdFlowPacked.rg - 1.0) * VelocitySpeedReferenceCmPerSec;
float2 tdFlowWorld = tdFlowPlane.x * PlaneAxisU.xy + tdFlowPlane.y * PlaneAxisV.xy;
float tdFlowSpeed = length(tdFlowWorld);
float tdFlowMagnitude = pow(saturate(tdFlowSpeed / max(FoliageBendSpeedReferenceCmPerSec, 1.0)), max(FoliageBendResponse, 0.01));
float tdFlowDegrees = clamp(FoliageBendMaxDegrees, 0.0, 85.0) * tdFlowMagnitude;
float2 tdFlowDirection = tdFlowWorld / max(tdFlowSpeed, 0.001);
return float4(0.5 + 0.5 * tdFlowDirection * tdFlowDegrees / 90.0, tdFlowDegrees / 90.0, 1.0);

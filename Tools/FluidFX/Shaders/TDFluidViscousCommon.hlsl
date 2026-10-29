#define TD_VISCOUS_FETCH(UV) Texture2DSampleLevel(DensityPressureTexture, DensityPressureTextureSampler, saturate(UV), 0)
#define TD_VISCOUS_INSIDE(UV) (step(0.0, (UV).x) * step((UV).x, 1.0) * step(0.0, (UV).y) * step((UV).y, 1.0))
#define TD_VISCOUS_COVERAGE(TAP) smoothstep(ViscousMaskThreshold - viscousEdge, ViscousMaskThreshold + viscousEdge, (TAP).r)
#define TD_VISCOUS_HEIGHT(TAP, COVERAGE) ((ViscousHeightCm * pow(saturate(((TAP).r - ViscousMaskThreshold) / max(1.0 - ViscousMaskThreshold, 0.001)), viscousExponent) + ViscousPressureHeightCm * (2.0 * (TAP).g - 1.0) * (COVERAGE)) * saturate((TAP).a))
#define TD_VISCOUS_HEIGHT_AT(XY, OUT_HEIGHT) { float2 viscousNeighborUv = ((XY) - viscousCenter) / viscousSize + 0.5; float4 viscousNeighborTap = TD_VISCOUS_FETCH(viscousNeighborUv); OUT_HEIGHT = TD_VISCOUS_HEIGHT(viscousNeighborTap, TD_VISCOUS_COVERAGE(viscousNeighborTap)) * TD_VISCOUS_INSIDE(viscousNeighborUv) * viscousIsDriven; }

float viscousIsDriven = step(0.5, SimGridResolution);
float2 viscousCenter = SimCenterWorld.xy;
float2 viscousSize = max(SimExtentsCm.xy, float2(1.0, 1.0));
float2 viscousUv = (ViscousPosition.xy - viscousCenter) / viscousSize + 0.5;
float viscousEdge = max(ViscousEdgeSoftness, 0.0001);
float viscousExponent = clamp(ViscousHeightExponent, 0.1, 4.0);
float viscousInside = TD_VISCOUS_INSIDE(viscousUv) * viscousIsDriven;
float4 viscousTap = TD_VISCOUS_FETCH(viscousUv);
float viscousCoverage = TD_VISCOUS_COVERAGE(viscousTap);
float viscousHeightCm = TD_VISCOUS_HEIGHT(viscousTap, viscousCoverage) * viscousInside;

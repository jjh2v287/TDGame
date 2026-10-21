float emberHeatValue = saturate(EmberHeat);
float emberTemperatureKelvin = lerp(EmberTemperatureMinKelvin, EmberTemperatureMaxKelvin, emberHeatValue);

float3 wavelengthMicrometers = float3(0.610, 0.550, 0.465);
float3 radiationExponent = 14388.0 / (wavelengthMicrometers * max(emberTemperatureKelvin, 300.0));
float3 radiationLevel = 1.0 / (pow(wavelengthMicrometers, 5.0) * (exp(radiationExponent) - 1.0));
float3 normalizedColor = radiationLevel / max(dot(radiationLevel, float3(0.2126, 0.7152, 0.0722)), 1e-30);
float3 emberTintColor = float3(1.0, max(EmberTintGreen, 0.0), max(EmberTintBlue, 0.0));
float3 emberTintChroma = emberTintColor / max(dot(emberTintColor, float3(0.2126, 0.7152, 0.0722)), 1e-4);
normalizedColor = lerp(normalizedColor, emberTintChroma, saturate(EmberTintBlend));

float facing = saturate(dot(normalize(SurfaceNormal), normalize(ViewVector)));
float emberBrightness = EmberIntensity * saturate(EmberFade) * pow(emberHeatValue, 1.5) * facing * facing;
return normalizedColor * emberBrightness;

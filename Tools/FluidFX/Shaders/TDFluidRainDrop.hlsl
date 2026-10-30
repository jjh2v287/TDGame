float tdRainAcross = abs(2.0 * TexCoord.x - 1.0);
float tdRainStreakAcross = 1.0 - smoothstep(0.0, 1.0, tdRainAcross / max(RainStreakWidthRatio, 0.01));
float tdRainStreakAlong = smoothstep(0.0, 1.0, TexCoord.y) * (1.0 - smoothstep(0.9, 1.0, TexCoord.y));
float tdRainStreak = tdRainStreakAcross * tdRainStreakAlong;
float tdRainRadius = length(2.0 * TexCoord - 1.0);
float tdRainRingRadius = lerp(0.45, 0.9, saturate(ParticleColor.g));
float tdRainRing = 1.0 - smoothstep(0.0, max(RainSplashRingWidth, 0.01), abs(tdRainRadius - tdRainRingRadius));
return saturate(ParticleColor.a * lerp(tdRainStreak, tdRainRing, saturate(ParticleColor.r)));

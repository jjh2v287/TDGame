float combineSigma = Slot0Main.x + Slot1Main.x + Slot2Main.x + Slot3Main.x;
float combineConservative = max(max(Slot0Main.y, Slot1Main.y), max(Slot2Main.y, Slot3Main.y));
float3 combineScatter = Slot0ScatterAlbedo + Slot1ScatterAlbedo + Slot2ScatterAlbedo + Slot3ScatterAlbedo;
CloudAlbedo = combineSigma > 1e-8 ? combineScatter / combineSigma : float3(1.0, 1.0, 1.0);
CloudEmissive = Slot0Emissive + Slot1Emissive + Slot2Emissive + Slot3Emissive;
return float4(combineSigma, combineSigma, combineSigma, combineConservative);

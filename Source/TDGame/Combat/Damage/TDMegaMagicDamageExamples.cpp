#include "Combat/Damage/TDDamageExamples.h"
#include "Combat/Damage/TDDamageDefinition.h"
#include "NiagaraSystem.h"

namespace
{
	void SetVisual(UTDDamageDefinition* Definition, const TCHAR* RelativePath, float Scale, float Tail = 0.75f)
	{
		const FString PackagePath = FString(TEXT("/Game/MegaMagicVFXBundle/VFX/")) + RelativePath;
		Definition->VisualEffect = LoadObject<UNiagaraSystem>(nullptr, *PackagePath);
		Definition->VisualScale = FVector(Scale);
		Definition->VisualTailSeconds = Tail;
		Definition->bDrawDebug = false;
		Definition->bProjectToGround = Definition->Mode != ETDDamageEntityMode::Projectile;
	}

	UTDDamageDefinition* MakeDefinition(UObject* Outer, const TCHAR* Name, ETDDamageEntityMode Mode,
		const TCHAR* RelativePath, float Scale)
	{
		UTDDamageDefinition* Definition = NewObject<UTDDamageDefinition>(Outer, Name);
		Definition->Mode = Mode;
		Definition->bDestroyOnHit = Mode == ETDDamageEntityMode::Projectile;
		SetVisual(Definition, RelativePath, Scale);
		return Definition;
	}

	void AddDamage(UTDDamageDefinition* Definition, float Amount, float SpellRatio, ETDDamageElement Element)
	{
		FTDDamageRule& Rule = Definition->Rules.AddDefaulted_GetRef();
		Rule.Event = ETDDamageEvent::Hit;
		FTDDamageAction& Action = Rule.Actions.AddDefaulted_GetRef();
		Action.Type = ETDDamageActionType::Damage;
		Action.Magnitude.Base = Amount;
		Action.Magnitude.SpellRatio = SpellRatio;
		Action.Element = Element;
	}

	FTDDamageAction& AddSpawn(UTDDamageDefinition* Definition, ETDDamageEvent Event, UTDDamageDefinition* Child)
	{
		FTDDamageRule& Rule = Definition->Rules.AddDefaulted_GetRef();
		Rule.Event = Event;
		FTDDamageAction& Action = Rule.Actions.AddDefaulted_GetRef();
		Action.Type = ETDDamageActionType::SpawnEntity;
		Action.Entity = Child;
		return Action;
	}
}

void TDDamageExamples::CreateMegaMagicExamples(UObject* Outer, TArray<UTDDamageDefinition*>& OutSpells)
{
	CreateExamples(Outer, OutSpells);
	if (OutSpells.Num() != 6)
	{
		return;
	}

	const TCHAR* FlameShield = TEXT("MagicShieldsVFX/VFX/DefaultVersions/FlameShield/Systems/N_FlameShield");
	const TCHAR* AquaShield = TEXT("MagicShieldsVFX/VFX/DefaultVersions/AquaShield/Systems/N_AquaShield");
	const TCHAR* ArcaneShield = TEXT("MagicShieldsVFX/VFX/DefaultVersions/ArcaneShield/Systems/N_ArcaneShield");
	const TCHAR* PoisonShield = TEXT("MagicShieldsVFX/VFX/DefaultVersions/PoisonShield/Systems/N_PoisonShield");
	const TCHAR* FireRing = TEXT("MagicAuraVFX/VFX/RingOfFlames/Systems/N_RingOfFlames");
	const TCHAR* GalacticField = TEXT("MagicAuraVFX/VFX/GalacticField/Systems/N_GalacticField");
	const TCHAR* LightningField = TEXT("MagicAuraVFX/VFX/LightningField/Systems/N_LightningField");
	const TCHAR* FlameBlast = TEXT("MagicalExplosionsVFX/VFX/FlameBlast/Systems/N_FlameBlast");
	const TCHAR* LightningBlast = TEXT("MagicalExplosionsVFX/VFX/LightningExplosion/Systems/N_LightningBlast");
	const TCHAR* EnergyBlast = TEXT("MagicalExplosionsVFX/VFX/EnergyBlast/Systems/N_EnergyBlast");

	UTDDamageDefinition* FlameField = FindObjectFast<UTDDamageDefinition>(Outer, TEXT("DA_TDFlameField"));
	UTDDamageDefinition* IceShard = FindObjectFast<UTDDamageDefinition>(Outer, TEXT("DA_TDIceShard"));
	UTDDamageDefinition* MineExplosion = FindObjectFast<UTDDamageDefinition>(Outer, TEXT("DA_TDMineExplosion"));
	UTDDamageDefinition* FallingMeteor = FindObjectFast<UTDDamageDefinition>(Outer, TEXT("DA_TDFallingMeteor"));
	UTDDamageDefinition* MeteorExplosion = FindObjectFast<UTDDamageDefinition>(Outer, TEXT("DA_TDMeteorExplosion"));
	SetVisual(OutSpells[0], FlameShield, 0.16f);
	SetVisual(FlameField, FireRing, 2.4f);
	SetVisual(OutSpells[1], GalacticField, 2.8f);
	SetVisual(IceShard, AquaShield, 0.19f, 0.35f);
	SetVisual(OutSpells[2], FireRing, 1.3f);
	OutSpells[2]->bShowDuringActivationDelay = true;
	SetVisual(MineExplosion, FlameBlast, 1.3f, 2.f);
	SetVisual(OutSpells[3], GalacticField, 1.2f, 0.35f);
	OutSpells[3]->bScaleVisualWithRadius = true;
	SetVisual(OutSpells[4], FireRing, 3.8f, 0.35f);
	SetVisual(FallingMeteor, FlameShield, 0.4f);
	SetVisual(MeteorExplosion, FlameBlast, 1.7f, 2.f);
	SetVisual(OutSpells[5], ArcaneShield, 0.16f);

	UTDDamageDefinition* ThunderBurst = MakeDefinition(Outer, TEXT("DA_TDThunderBurst"),
		ETDDamageEntityMode::Area, LightningBlast, 1.5f);
	ThunderBurst->Radius = 280.f;
	ThunderBurst->Lifetime = 0.2f;
	ThunderBurst->VisualTailSeconds = 2.f;
	AddDamage(ThunderBurst, 30.f, 0.7f, ETDDamageElement::Arcane);
	UTDDamageDefinition* ThunderCage = MakeDefinition(Outer, TEXT("DA_TDThunderCage"),
		ETDDamageEntityMode::Area, LightningField, 3.4f);
	ThunderCage->Cooldown = 6.f;
	ThunderCage->Radius = 280.f;
	ThunderCage->Lifetime = 3.5f;
	ThunderCage->ActivationDelay = 0.45f;
	ThunderCage->bShowDuringActivationDelay = true;
	ThunderCage->MaxHitsPerTarget = 0;
	ThunderCage->HitInterval = 0.6f;
	ThunderCage->PulseInterval = 0.6f;
	AddDamage(ThunderCage, 9.f, 0.2f, ETDDamageElement::Arcane);
	AddSpawn(ThunderCage, ETDDamageEvent::Expire, ThunderBurst);
	OutSpells.Add(ThunderCage);

	UTDDamageDefinition* VenomPool = MakeDefinition(Outer, TEXT("DA_TDVenomPool"),
		ETDDamageEntityMode::Area, PoisonShield, 1.73f);
	VenomPool->Radius = 260.f;
	VenomPool->Lifetime = 4.f;
	VenomPool->VisualScale.Z = 0.35f;
	VenomPool->MaxHitsPerTarget = 0;
	VenomPool->HitInterval = 0.5f;
	VenomPool->PulseInterval = 0.5f;
	AddDamage(VenomPool, 7.f, 0.18f, ETDDamageElement::Arcane);
	UTDDamageDefinition* VenomBloom = MakeDefinition(Outer, TEXT("DA_TDVenomBloom"),
		ETDDamageEntityMode::Mine, PoisonShield, 0.7f);
	VenomBloom->Cooldown = 4.f;
	VenomBloom->Radius = 140.f;
	VenomBloom->Lifetime = 12.f;
	VenomBloom->ActivationDelay = 0.7f;
	VenomBloom->bShowDuringActivationDelay = true;
	VenomBloom->PulseInterval = 0.1f;
	AddSpawn(VenomBloom, ETDDamageEvent::Trigger, VenomPool);
	OutSpells.Add(VenomBloom);

	UTDDamageDefinition* AstralImpact = MakeDefinition(Outer, TEXT("DA_TDAstralImpact"),
		ETDDamageEntityMode::Area, EnergyBlast, 0.65f);
	AstralImpact->Radius = 150.f;
	AstralImpact->Lifetime = 0.15f;
	AstralImpact->VisualTailSeconds = 1.5f;
	AddDamage(AstralImpact, 8.f, 0.2f, ETDDamageElement::Arcane);
	UTDDamageDefinition* AstralLance = MakeDefinition(Outer, TEXT("DA_TDAstralLance"),
		ETDDamageEntityMode::Projectile, ArcaneShield, 0.16f);
	AstralLance->VisualScale.X = 0.4f;
	AstralLance->ProjectileSpeed = 720.f;
	AstralLance->ProjectileRadius = 24.f;
	AstralLance->Lifetime = 2.5f;
	AddDamage(AstralLance, 15.f, 0.4f, ETDDamageElement::Arcane);
	AddSpawn(AstralLance, ETDDamageEvent::End, AstralImpact);
	FTDDamageRule& HomingRule = AstralLance->Rules.AddDefaulted_GetRef();
	HomingRule.Event = ETDDamageEvent::Spawn;
	FTDDamageAction& Homing = HomingRule.Actions.AddDefaulted_GetRef();
	Homing.Type = ETDDamageActionType::ApplyHoming;
	Homing.DelaySeconds = 0.12f;
	Homing.Homing.SearchRadius = 900.f;
	Homing.Homing.TurnRateDegreesPerSecond = 360.f;
	UTDDamageDefinition* AstralLances = MakeDefinition(Outer, TEXT("DA_TDAstralLances"),
		ETDDamageEntityMode::Area, GalacticField, 1.8f);
	AstralLances->Cooldown = 3.f;
	AstralLances->Radius = 160.f;
	AstralLances->Lifetime = 0.9f;
	for (int32 Index = 0; Index < 3; ++Index)
	{
		FTDDamageAction& Spawn = AddSpawn(AstralLances, ETDDamageEvent::Spawn, AstralLance);
		Spawn.DelaySeconds = 0.15f + Index * 0.2f;
		Spawn.SpawnAnchor = ETDDamageSpawnAnchor::CastTarget;
		Spawn.SpawnOffset = FVector(-180.f, (Index - 1) * 140.f, 300.f);
		Spawn.SpawnDirection = ETDDamageDirection::TowardCastTarget;
	}
	OutSpells.Add(AstralLances);

	UTDDamageDefinition* PhoenixWake = MakeDefinition(Outer, TEXT("DA_TDPhoenixWake"),
		ETDDamageEntityMode::Shockwave, FireRing, 1.3f);
	PhoenixWake->Radius = 110.f;
	PhoenixWake->InnerRadius = 30.f;
	PhoenixWake->ExpansionSpeed = 350.f;
	PhoenixWake->Lifetime = 1.1f;
	PhoenixWake->PulseInterval = 0.02f;
	PhoenixWake->bScaleVisualWithRadius = true;
	PhoenixWake->VisualTailSeconds = 0.3f;
	AddDamage(PhoenixWake, 22.f, 0.55f, ETDDamageElement::Fire);
	UTDDamageDefinition* PhoenixCore = MakeDefinition(Outer, TEXT("DA_TDPhoenixCore"),
		ETDDamageEntityMode::Projectile, FlameShield, 0.37f);
	PhoenixCore->ProjectileSpeed = 1150.f;
	PhoenixCore->ProjectileRadius = 55.f;
	PhoenixCore->Lifetime = 1.8f;
	AddDamage(PhoenixCore, 25.f, 0.65f, ETDDamageElement::Fire);
	AddSpawn(PhoenixCore, ETDDamageEvent::End, PhoenixWake);
	AddSpawn(PhoenixCore, ETDDamageEvent::End, MeteorExplosion);
	AddSpawn(PhoenixCore, ETDDamageEvent::End, FlameField);
	UTDDamageDefinition* PhoenixDive = MakeDefinition(Outer, TEXT("DA_TDPhoenixDive"),
		ETDDamageEntityMode::Area, FireRing, 3.5f);
	PhoenixDive->Cooldown = 8.f;
	PhoenixDive->Radius = 300.f;
	PhoenixDive->Lifetime = 1.35f;
	FTDDamageAction& Dive = AddSpawn(PhoenixDive, ETDDamageEvent::Spawn, PhoenixCore);
	Dive.DelaySeconds = 0.45f;
	Dive.SpawnAnchor = ETDDamageSpawnAnchor::CastTarget;
	Dive.SpawnOffset.Z = 850.f;
	Dive.SpawnDirection = ETDDamageDirection::Down;
	OutSpells.Add(PhoenixDive);
}

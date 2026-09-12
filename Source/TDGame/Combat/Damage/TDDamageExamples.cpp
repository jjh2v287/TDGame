#include "Combat/Damage/TDDamageExamples.h"
#include "Combat/Damage/TDDamageDefinition.h"
#include "Combat/Damage/TDStatusDefinition.h"

namespace
{
	UTDDamageDefinition* CreateDefinition(UObject* Outer, const TCHAR* Name, ETDDamageEntityMode Mode, const FLinearColor& Color)
	{
		UTDDamageDefinition* Definition = NewObject<UTDDamageDefinition>(Outer, Name);
		Definition->Mode = Mode;
		Definition->DebugColor = Color;
		Definition->bDrawDebug = true;
		Definition->bDestroyOnHit = Mode == ETDDamageEntityMode::Projectile;
		return Definition;
	}

	FTDDamageAction MakeDamage(float Base, float SpellRatio, ETDDamageElement Element, float PerLevel = 0.f)
	{
		FTDDamageAction Action;
		Action.Magnitude.Base = Base;
		Action.Magnitude.SpellRatio = SpellRatio;
		Action.Magnitude.PerLevel = PerLevel;
		Action.Element = Element;
		return Action;
	}

	FTDDamageAction MakeSpawn(UTDDamageDefinition* Definition)
	{
		FTDDamageAction Action;
		Action.Type = ETDDamageActionType::SpawnEntity;
		Action.Entity = Definition;
		return Action;
	}

	void AddRule(UTDDamageDefinition* Definition, ETDDamageEvent Event, const FTDDamageAction& Action)
	{
		FTDDamageRule& Rule = Definition->Rules.AddDefaulted_GetRef();
		Rule.Event = Event;
		Rule.Actions.Add(Action);
	}
}

void TDDamageExamples::CreateExamples(UObject* Outer, TArray<UTDDamageDefinition*>& OutSpells)
{
	OutSpells.Reset();
	if (!IsValid(Outer))
	{
		return;
	}

	UTDDamageDefinition* FlameField = CreateDefinition(Outer, TEXT("DA_TDFlameField"), ETDDamageEntityMode::Area, FLinearColor(1.f, 0.25f, 0.f));
	FlameField->Radius = 180.f;
	FlameField->Lifetime = 4.f;
	FlameField->MaxHitsPerTarget = 0;
	FlameField->HitInterval = 0.5f;
	FlameField->PulseInterval = 0.5f;
	AddRule(FlameField, ETDDamageEvent::Hit, MakeDamage(4.f, 0.15f, ETDDamageElement::Fire, 0.5f));

	UTDDamageDefinition* Fireball = CreateDefinition(Outer, TEXT("DA_TDFireball"), ETDDamageEntityMode::Projectile, FLinearColor::Red);
	Fireball->Cooldown = 0.6f;
	Fireball->Lifetime = 2.f;
	Fireball->ProjectileSpeed = 1100.f;
	Fireball->ProjectileRadius = 20.f;
	FTDDamageAction FireballDamage = MakeDamage(24.f, 0.8f, ETDDamageElement::Fire, 2.f);
	FireballDamage.bCanCrit = true;
	AddRule(Fireball, ETDDamageEvent::Hit, FireballDamage);
	AddRule(Fireball, ETDDamageEvent::End, MakeSpawn(FlameField));
	OutSpells.Add(Fireball);

	UTDStatusDefinition* Freeze = NewObject<UTDStatusDefinition>(Outer, TEXT("DA_TDFrostFreeze"));
	Freeze->Duration = 2.f;
	Freeze->bFreezesTarget = true;
	Freeze->DamageThreshold = 30.f;
	Freeze->BuildupResetDelay = 3.f;
	FTDDamageRule& ShatterRule = Freeze->Rules.AddDefaulted_GetRef();
	ShatterRule.Event = ETDDamageEvent::Expire;
	ShatterRule.Actions.Add(MakeDamage(20.f, 0.5f, ETDDamageElement::Frost, 1.f));

	UTDDamageDefinition* IceShard = CreateDefinition(Outer, TEXT("DA_TDIceShard"), ETDDamageEntityMode::Projectile, FLinearColor(0.1f, 0.7f, 1.f));
	IceShard->Lifetime = 1.5f;
	IceShard->ProjectileSpeed = 850.f;
	IceShard->ProjectileRadius = 28.f;
	FTDDamageAction FrostDamage = MakeDamage(10.f, 0.25f, ETDDamageElement::Frost, 0.5f);
	FrostDamage.Status = Freeze;
	AddRule(IceShard, ETDDamageEvent::Hit, FrostDamage);

	UTDDamageDefinition* Blizzard = CreateDefinition(Outer, TEXT("DA_TDBlizzard"), ETDDamageEntityMode::Area, FLinearColor(0.3f, 0.65f, 1.f));
	Blizzard->Cooldown = 5.f;
	Blizzard->Radius = 220.f;
	Blizzard->Lifetime = 5.f;
	Blizzard->PulseInterval = 0.25f;
	FTDDamageAction SpawnIce = MakeSpawn(IceShard);
	SpawnIce.SpawnAnchor = ETDDamageSpawnAnchor::CastTarget;
	SpawnIce.SpawnDirection = ETDDamageDirection::Down;
	SpawnIce.SpawnOffset.Z = 650.f;
	SpawnIce.SpawnCount = 4;
	SpawnIce.ScatterRadius = 190.f;
	AddRule(Blizzard, ETDDamageEvent::Pulse, SpawnIce);
	OutSpells.Add(Blizzard);

	UTDDamageDefinition* MineExplosion = CreateDefinition(Outer, TEXT("DA_TDMineExplosion"), ETDDamageEntityMode::Area, FLinearColor(1.f, 0.75f, 0.1f));
	MineExplosion->Radius = 260.f;
	MineExplosion->Lifetime = 0.25f;
	AddRule(MineExplosion, ETDDamageEvent::Hit, MakeDamage(40.f, 1.f, ETDDamageElement::Fire, 2.f));

	UTDDamageDefinition* Mine = CreateDefinition(Outer, TEXT("DA_TDMine"), ETDDamageEntityMode::Mine, FLinearColor::Yellow);
	Mine->Cooldown = 2.f;
	Mine->Lifetime = 20.f;
	Mine->ActivationDelay = 0.8f;
	Mine->PulseInterval = 0.1f;
	Mine->Radius = 110.f;
	AddRule(Mine, ETDDamageEvent::Trigger, MakeSpawn(MineExplosion));
	OutSpells.Add(Mine);

	UTDDamageDefinition* Shockwave = CreateDefinition(Outer, TEXT("DA_TDShockwave"), ETDDamageEntityMode::Shockwave, FLinearColor(0.7f, 0.2f, 1.f));
	Shockwave->Cooldown = 2.f;
	Shockwave->Radius = 100.f;
	Shockwave->InnerRadius = 45.f;
	Shockwave->ExpansionSpeed = 420.f;
	Shockwave->Lifetime = 1.5f;
	Shockwave->PulseInterval = 0.02f;
	AddRule(Shockwave, ETDDamageEvent::Hit, MakeDamage(22.f, 0.65f, ETDDamageElement::Arcane, 1.f));
	OutSpells.Add(Shockwave);

	UTDDamageDefinition* MeteorExplosion = CreateDefinition(Outer, TEXT("DA_TDMeteorExplosion"), ETDDamageEntityMode::Area, FLinearColor(1.f, 0.1f, 0.02f));
	MeteorExplosion->Radius = 330.f;
	MeteorExplosion->Lifetime = 0.35f;
	AddRule(MeteorExplosion, ETDDamageEvent::Hit, MakeDamage(60.f, 1.5f, ETDDamageElement::Fire, 3.f));

	UTDDamageDefinition* FallingMeteor = CreateDefinition(Outer, TEXT("DA_TDFallingMeteor"), ETDDamageEntityMode::Projectile, FLinearColor(1.f, 0.4f, 0.05f));
	FallingMeteor->ProjectileRadius = 60.f;
	FallingMeteor->ProjectileSpeed = 900.f;
	FallingMeteor->Lifetime = 2.f;
	AddRule(FallingMeteor, ETDDamageEvent::Hit, MakeDamage(15.f, 0.5f, ETDDamageElement::Fire, 1.f));
	AddRule(FallingMeteor, ETDDamageEvent::End, MakeSpawn(MeteorExplosion));

	UTDDamageDefinition* Meteor = CreateDefinition(Outer, TEXT("DA_TDMeteor"), ETDDamageEntityMode::Area, FLinearColor(1.f, 0.4f, 0.05f));
	Meteor->Cooldown = 4.f;
	Meteor->Radius = 330.f;
	Meteor->Lifetime = 1.2f;
	FTDDamageAction SpawnMeteor = MakeSpawn(FallingMeteor);
	SpawnMeteor.SpawnAnchor = ETDDamageSpawnAnchor::CastTarget;
	SpawnMeteor.SpawnDirection = ETDDamageDirection::Down;
	SpawnMeteor.SpawnOffset.Z = 1000.f;
	AddRule(Meteor, ETDDamageEvent::Spawn, SpawnMeteor);
	OutSpells.Add(Meteor);

	UTDDamageDefinition* DelayedHoming = CreateDefinition(Outer, TEXT("DA_TDDelayedHoming"), ETDDamageEntityMode::Projectile, FLinearColor(0.2f, 1.f, 0.5f));
	DelayedHoming->Cooldown = 1.f;
	DelayedHoming->Lifetime = 5.f;
	DelayedHoming->ProjectileSpeed = 350.f;
	DelayedHoming->ProjectileRadius = 20.f;
	FTDDamageAction StartHoming;
	StartHoming.Type = ETDDamageActionType::ApplyHoming;
	StartHoming.DelaySeconds = 1.f;
	StartHoming.Homing.TargetSelection = ETDHomingTargetSelection::NearestEnemy;
	StartHoming.Homing.SearchRadius = 1200.f;
	StartHoming.Homing.TurnRateDegreesPerSecond = 180.f;
	StartHoming.Homing.RetargetInterval = 0.2f;
	StartHoming.Homing.TargetLossPolicy = ETDHomingTargetLossPolicy::Reacquire;
	StartHoming.Homing.bRetargetOnApply = true;
	AddRule(DelayedHoming, ETDDamageEvent::Spawn, StartHoming);
	FTDDamageAction StopHoming;
	StopHoming.Type = ETDDamageActionType::StopHoming;
	StopHoming.DelaySeconds = 2.f;
	AddRule(DelayedHoming, ETDDamageEvent::Spawn, StopHoming);
	StartHoming.DelaySeconds = 3.f;
	StartHoming.Homing.TurnRateDegreesPerSecond = 360.f;
	AddRule(DelayedHoming, ETDDamageEvent::Spawn, StartHoming);
	AddRule(DelayedHoming, ETDDamageEvent::Hit, FireballDamage);
	AddRule(DelayedHoming, ETDDamageEvent::End, MakeSpawn(FlameField));
	OutSpells.Add(DelayedHoming);
}

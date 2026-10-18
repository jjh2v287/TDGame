#include "MonsterAI/TDMonsterAttackCatalog.h"
#include "Combat/Damage/TDDamageDefinition.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"
#include "NiagaraSystem.h"
#include "TDGame.h"
#include "UObject/SoftObjectPath.h"

namespace
{
	enum class ETDTelegraphForm : uint8
	{
		Disk,
		Cone,
		Band
	};

	struct FTDTelegraphMaterialEntry
	{
		ETDTelegraphForm Form;
		ETDTelegraphStyle Style;
		float HalfAngleDegrees;
		const TCHAR* Path;
	};

	struct FTDAreaFootprint
	{
		ETDDamageAreaShape Shape = ETDDamageAreaShape::Circle;
		float Radius = 0.f;
		float ConeHalfAngleDegrees = 45.f;
		float LineHalfWidth = 100.f;
		float InnerRadius = 0.f;
	};

	const TCHAR* TelegraphMeshPath = TEXT("/Engine/BasicShapes/Plane.Plane");
	const TCHAR* BoltEffectPath = TEXT("/Game/MegaMagicVFXBundle/VFX/MagicShieldsVFX/VFX/DefaultVersions/PoisonShield/Systems/N_PoisonShield.N_PoisonShield");
	const TCHAR* ArrowEffectPath = TEXT("/Game/SlashTrail_SoftTofu/Niagara/Basic/NS_SlashTrail_Basic_Loop.NS_SlashTrail_Basic_Loop");
	const TCHAR* FlameShieldEffectPath = TEXT("/Game/MegaMagicVFXBundle/VFX/MagicShieldsVFX/VFX/DefaultVersions/FlameShield/Systems/N_FlameShield.N_FlameShield");
	const TCHAR* RingOfFlamesEffectPath = TEXT("/Game/MegaMagicVFXBundle/VFX/MagicAuraVFX/VFX/RingOfFlames/Systems/N_RingOfFlames.N_RingOfFlames");
	const TCHAR* FlameBlastEffectPath = TEXT("/Game/MegaMagicVFXBundle/VFX/MagicalExplosionsVFX/VFX/FlameBlast/Systems/N_FlameBlast.N_FlameBlast");
	constexpr float TelegraphMeshHalfExtent = 50.f;
	constexpr float TelegraphMeshExtent = TelegraphMeshHalfExtent * 2.f;
	constexpr float TelegraphHeight = 3.f;
	constexpr float MaximumWindupReach = 1500.f;

	FTDDamageAction MakeAttackDamage(const float AttackRatio, const ETDDamageElement Element)
	{
		FTDDamageAction Action;
		Action.Magnitude.Base = 0.f;
		Action.Magnitude.AttackRatio = AttackRatio;
		Action.Element = Element;
		return Action;
	}

	void AddHitDamage(UTDDamageDefinition* Definition, const float AttackRatio, const ETDDamageElement Element)
	{
		FTDDamageRule& Rule = Definition->Rules.AddDefaulted_GetRef();
		Rule.Event = ETDDamageEvent::Hit;
		Rule.Actions.Add(MakeAttackDamage(AttackRatio, Element));
	}

	bool DoesAssetPackageExist(const TCHAR* AssetPath)
	{
		return FPackageName::DoesPackageExist(FSoftObjectPath(AssetPath).GetLongPackageName());
	}

	const TCHAR* FindTelegraphMaterialPath(const ETDTelegraphForm Form, const ETDTelegraphStyle Style, const float HalfAngleDegrees)
	{
		static const FTDTelegraphMaterialEntry Entries[] =
		{
			{ ETDTelegraphForm::Disk, ETDTelegraphStyle::Danger, 0.f, TEXT("/Game/MonsterAI/Materials/MI_TD_Telegraph_Danger.MI_TD_Telegraph_Danger") },
			{ ETDTelegraphForm::Disk, ETDTelegraphStyle::Unblockable, 0.f, TEXT("/Game/MonsterAI/Materials/MI_TD_Telegraph_Unblockable.MI_TD_Telegraph_Unblockable") },
			{ ETDTelegraphForm::Disk, ETDTelegraphStyle::Safe, 0.f, TEXT("/Game/MonsterAI/Materials/MI_TD_Telegraph_Safe.MI_TD_Telegraph_Safe") },
			{ ETDTelegraphForm::Band, ETDTelegraphStyle::Danger, 0.f, TEXT("/Game/MonsterAI/Materials/MI_TD_TelegraphBand_Danger.MI_TD_TelegraphBand_Danger") },
			{ ETDTelegraphForm::Band, ETDTelegraphStyle::Unblockable, 0.f, TEXT("/Game/MonsterAI/Materials/MI_TD_TelegraphBand_Unblockable.MI_TD_TelegraphBand_Unblockable") },
			{ ETDTelegraphForm::Cone, ETDTelegraphStyle::Danger, 20.f, TEXT("/Game/MonsterAI/Materials/MI_TD_TelegraphCone_Danger_20.MI_TD_TelegraphCone_Danger_20") },
			{ ETDTelegraphForm::Cone, ETDTelegraphStyle::Danger, 30.f, TEXT("/Game/MonsterAI/Materials/MI_TD_TelegraphCone_Danger_30.MI_TD_TelegraphCone_Danger_30") },
			{ ETDTelegraphForm::Cone, ETDTelegraphStyle::Danger, 35.f, TEXT("/Game/MonsterAI/Materials/MI_TD_TelegraphCone_Danger_35.MI_TD_TelegraphCone_Danger_35") },
			{ ETDTelegraphForm::Cone, ETDTelegraphStyle::Danger, 45.f, TEXT("/Game/MonsterAI/Materials/MI_TD_TelegraphCone_Danger_45.MI_TD_TelegraphCone_Danger_45") },
			{ ETDTelegraphForm::Cone, ETDTelegraphStyle::Danger, 60.f, TEXT("/Game/MonsterAI/Materials/MI_TD_TelegraphCone_Danger_60.MI_TD_TelegraphCone_Danger_60") },
			{ ETDTelegraphForm::Cone, ETDTelegraphStyle::Danger, 90.f, TEXT("/Game/MonsterAI/Materials/MI_TD_TelegraphCone_Danger_90.MI_TD_TelegraphCone_Danger_90") },
			{ ETDTelegraphForm::Cone, ETDTelegraphStyle::Unblockable, 20.f, TEXT("/Game/MonsterAI/Materials/MI_TD_TelegraphCone_Unblockable_20.MI_TD_TelegraphCone_Unblockable_20") },
			{ ETDTelegraphForm::Cone, ETDTelegraphStyle::Unblockable, 30.f, TEXT("/Game/MonsterAI/Materials/MI_TD_TelegraphCone_Unblockable_30.MI_TD_TelegraphCone_Unblockable_30") },
			{ ETDTelegraphForm::Cone, ETDTelegraphStyle::Unblockable, 35.f, TEXT("/Game/MonsterAI/Materials/MI_TD_TelegraphCone_Unblockable_35.MI_TD_TelegraphCone_Unblockable_35") },
			{ ETDTelegraphForm::Cone, ETDTelegraphStyle::Unblockable, 45.f, TEXT("/Game/MonsterAI/Materials/MI_TD_TelegraphCone_Unblockable_45.MI_TD_TelegraphCone_Unblockable_45") },
			{ ETDTelegraphForm::Cone, ETDTelegraphStyle::Unblockable, 60.f, TEXT("/Game/MonsterAI/Materials/MI_TD_TelegraphCone_Unblockable_60.MI_TD_TelegraphCone_Unblockable_60") },
			{ ETDTelegraphForm::Cone, ETDTelegraphStyle::Unblockable, 90.f, TEXT("/Game/MonsterAI/Materials/MI_TD_TelegraphCone_Unblockable_90.MI_TD_TelegraphCone_Unblockable_90") }
		};
		for (const FTDTelegraphMaterialEntry& Entry : Entries)
		{
			if (Entry.Form == Form && Entry.Style == Style && FMath::IsNearlyEqual(Entry.HalfAngleDegrees, HalfAngleDegrees, 0.01f))
			{
				return Entry.Path;
			}
		}
		return nullptr;
	}

	ETDTelegraphForm GetTelegraphForm(const UTDDamageDefinition& Definition)
	{
		if (Definition.AreaShape == ETDDamageAreaShape::Cone)
		{
			return ETDTelegraphForm::Cone;
		}
		if (Definition.AreaShape == ETDDamageAreaShape::Line)
		{
			return ETDTelegraphForm::Band;
		}
		return ETDTelegraphForm::Disk;
	}

	FVector GetTelegraphScale(const UTDDamageDefinition& Definition)
	{
		if (Definition.AreaShape == ETDDamageAreaShape::Line)
		{
			return FVector(Definition.Radius / TelegraphMeshExtent, Definition.LineHalfWidth * 2.f / TelegraphMeshExtent, 1.f);
		}
		const float PlaneScale = Definition.Radius / TelegraphMeshHalfExtent;
		return FVector(PlaneScale, PlaneScale, 1.f);
	}

	FVector GetTelegraphOffset(const UTDDamageDefinition& Definition)
	{
		if (Definition.AreaShape == ETDDamageAreaShape::Line)
		{
			return FVector(Definition.Radius * 0.5f, 0.f, TelegraphHeight);
		}
		return FVector(0.f, 0.f, TelegraphHeight);
	}

	FLinearColor GetTelegraphColor(const ETDTelegraphStyle Style)
	{
		if (Style == ETDTelegraphStyle::Unblockable)
		{
			return FLinearColor(0.62f, 0.18f, 1.f);
		}
		if (Style == ETDTelegraphStyle::Safe)
		{
			return FLinearColor(0.1f, 0.9f, 0.85f);
		}
		return FLinearColor(1.f, 0.15f, 0.05f);
	}

	UTDDamageDefinition* CreateGroundAttack(UObject* Outer, const TCHAR* Name, const float Radius, const float WindupSeconds, const float AttackRatio, const ETDDamageElement Element, const FLinearColor& Color)
	{
		UTDDamageDefinition* Definition = NewObject<UTDDamageDefinition>(Outer, Name);
		Definition->Mode = ETDDamageEntityMode::Area;
		Definition->TargetPolicy = ETDDamageTargetPolicy::Enemies;
		Definition->Cooldown = 0.f;
		Definition->CastRange = 5000.f;
		Definition->Radius = Radius;
		Definition->InnerRadius = 0.f;
		Definition->HalfHeight = 160.f;
		Definition->bProjectToGround = true;
		Definition->ActivationDelay = WindupSeconds;
		Definition->Lifetime = WindupSeconds + 0.1f;
		Definition->PulseInterval = 1.f;
		Definition->MaxHitsPerTarget = 1;
		Definition->bDestroyOnHit = false;
		Definition->DebugColor = Color;
		AddHitDamage(Definition, AttackRatio, Element);
		TDMonsterAttackCatalog::ApplyTelegraph(*Definition, ETDTelegraphStyle::Danger);
		return Definition;
	}

	UTDDamageDefinition* CreateBolt(UObject* Outer, const TCHAR* Name, const float WindupSeconds, const float AttackRatio)
	{
		UTDDamageDefinition* Definition = NewObject<UTDDamageDefinition>(Outer, Name);
		Definition->Mode = ETDDamageEntityMode::Projectile;
		Definition->TargetPolicy = ETDDamageTargetPolicy::Enemies;
		Definition->Cooldown = 0.f;
		Definition->CastRange = 5000.f;
		Definition->ActivationDelay = WindupSeconds;
		Definition->Lifetime = WindupSeconds + 1.8f;
		Definition->ProjectileSpeed = 950.f;
		Definition->ProjectileRadius = 24.f;
		Definition->HalfHeight = 120.f;
		Definition->MaxHitsPerTarget = 1;
		Definition->bDestroyOnHit = true;
		Definition->bShowDuringActivationDelay = true;
		Definition->DebugColor = FLinearColor(0.4f, 1.f, 0.2f);
		Definition->VisualEffect = TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(BoltEffectPath));
		Definition->VisualScale = FVector(0.16f);
		Definition->VisualTailSeconds = 0.3f;
		Definition->bDrawDebug = !DoesAssetPackageExist(BoltEffectPath);
		AddHitDamage(Definition, AttackRatio, ETDDamageElement::Arcane);
		return Definition;
	}

	UTDDamageDefinition* CreateArrow(UObject* Outer, const TCHAR* Name, const float WindupSeconds, const float AttackRatio)
	{
		UTDDamageDefinition* Definition = CreateBolt(Outer, Name, WindupSeconds, AttackRatio);
		Definition->Rules.Reset();
		AddHitDamage(Definition, AttackRatio, ETDDamageElement::Physical);
		Definition->Lifetime = WindupSeconds + 1.f;
		Definition->ProjectileSpeed = 1700.f;
		Definition->ProjectileRadius = 18.f;
		Definition->DebugColor = FLinearColor(0.9f, 0.8f, 0.5f);
		Definition->VisualEffect = TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(ArrowEffectPath));
		Definition->VisualScale = FVector(0.5f);
		Definition->bDrawDebug = !DoesAssetPackageExist(ArrowEffectPath);
		return Definition;
	}

	void Register(TMap<FName, UTDDamageDefinition*>& OutAttacks, UTDDamageDefinition* Definition)
	{
		OutAttacks.Add(Definition->GetFName(), Definition);
	}

	void AddSpawnRule(UTDDamageDefinition* Definition, const ETDDamageEvent Event, UTDDamageDefinition* Child, const FVector& Offset = FVector::ZeroVector, const float DelaySeconds = 0.f)
	{
		FTDDamageRule& Rule = Definition->Rules.AddDefaulted_GetRef();
		Rule.Event = Event;
		FTDDamageAction& Action = Rule.Actions.AddDefaulted_GetRef();
		Action.Type = ETDDamageActionType::SpawnEntity;
		Action.Entity = Child;
		Action.SpawnAnchor = ETDDamageSpawnAnchor::EventLocation;
		Action.SpawnDirection = ETDDamageDirection::Inherit;
		Action.SpawnOffset = Offset;
		Action.SpawnCount = 1;
		Action.DelaySeconds = DelaySeconds;
	}

	void ApplyWindupFootprint(UTDDamageDefinition& Wrapper, const UTDDamageDefinition& Spell)
	{
		const float RemainingSeconds = Spell.Lifetime - Spell.ActivationDelay;
		if (Spell.Mode == ETDDamageEntityMode::Area)
		{
			Wrapper.AreaShape = Spell.AreaShape;
			Wrapper.Radius = Spell.Radius;
			Wrapper.InnerRadius = Spell.InnerRadius;
			Wrapper.ConeHalfAngleDegrees = Spell.ConeHalfAngleDegrees;
			Wrapper.LineHalfWidth = Spell.LineHalfWidth;
			return;
		}
		if (Spell.Mode == ETDDamageEntityMode::Projectile)
		{
			Wrapper.AreaShape = ETDDamageAreaShape::Line;
			Wrapper.Radius = FMath::Clamp(Spell.ProjectileSpeed * RemainingSeconds, 1.f, MaximumWindupReach);
			Wrapper.LineHalfWidth = Spell.ProjectileRadius + 30.f;
			return;
		}
		Wrapper.AreaShape = ETDDamageAreaShape::Circle;
		Wrapper.Radius = Spell.Radius;
		if (Spell.Mode == ETDDamageEntityMode::Shockwave)
		{
			Wrapper.Radius = FMath::Min(Spell.Radius + Spell.ExpansionSpeed * RemainingSeconds, MaximumWindupReach);
		}
	}

	FTDAreaFootprint MakeCircleFootprint(const float Radius)
	{
		FTDAreaFootprint Footprint;
		Footprint.Radius = Radius;
		return Footprint;
	}

	FTDAreaFootprint MakeConeFootprint(const float Radius, const float HalfAngleDegrees)
	{
		FTDAreaFootprint Footprint;
		Footprint.Shape = ETDDamageAreaShape::Cone;
		Footprint.Radius = Radius;
		Footprint.ConeHalfAngleDegrees = HalfAngleDegrees;
		return Footprint;
	}

	FTDAreaFootprint MakeLineFootprint(const float Length, const float HalfWidth)
	{
		FTDAreaFootprint Footprint;
		Footprint.Shape = ETDDamageAreaShape::Line;
		Footprint.Radius = Length;
		Footprint.LineHalfWidth = HalfWidth;
		return Footprint;
	}

	FTDAreaFootprint MakeRingFootprint(const float Radius, const float InnerRadius)
	{
		FTDAreaFootprint Footprint;
		Footprint.Shape = ETDDamageAreaShape::Ring;
		Footprint.Radius = Radius;
		Footprint.InnerRadius = InnerRadius;
		return Footprint;
	}

	UTDDamageDefinition* CreateAreaDefinition(UObject* Outer, const TCHAR* Name, const FTDAreaFootprint& Footprint, const float WindupSeconds, const float LifetimeSeconds)
	{
		UTDDamageDefinition* Definition = NewObject<UTDDamageDefinition>(Outer, Name);
		Definition->Mode = ETDDamageEntityMode::Area;
		Definition->AreaShape = Footprint.Shape;
		Definition->Radius = Footprint.Radius;
		Definition->ConeHalfAngleDegrees = Footprint.ConeHalfAngleDegrees;
		Definition->LineHalfWidth = Footprint.LineHalfWidth;
		Definition->InnerRadius = Footprint.InnerRadius;
		Definition->TargetPolicy = ETDDamageTargetPolicy::Enemies;
		Definition->Cooldown = 0.f;
		Definition->CastRange = 5000.f;
		Definition->HalfHeight = 200.f;
		Definition->bProjectToGround = true;
		Definition->ActivationDelay = WindupSeconds;
		Definition->Lifetime = LifetimeSeconds;
		Definition->PulseInterval = 1.f;
		Definition->MaxHitsPerTarget = 1;
		Definition->bDestroyOnHit = false;
		return Definition;
	}

	UTDDamageDefinition* CreateShapedAttack(UObject* Outer, const TCHAR* Name, const FTDAreaFootprint& Footprint, const float WindupSeconds, const float AttackRatio, const ETDDamageElement Element, const ETDTelegraphStyle Style)
	{
		UTDDamageDefinition* Definition = CreateAreaDefinition(Outer, Name, Footprint, WindupSeconds, WindupSeconds + 0.1f);
		Definition->DebugColor = GetTelegraphColor(Style);
		AddHitDamage(Definition, AttackRatio, Element);
		if (WindupSeconds > 0.f)
		{
			TDMonsterAttackCatalog::ApplyTelegraph(*Definition, Style);
		}
		return Definition;
	}

	void ApplyEffect(UTDDamageDefinition* Definition, const TCHAR* EffectPath, const FVector& Scale, const FVector& Offset)
	{
		if (!DoesAssetPackageExist(EffectPath))
		{
			return;
		}
		Definition->VisualEffect = TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(EffectPath));
		Definition->VisualScale = Scale;
		Definition->VisualOffset = Offset;
		Definition->VisualTailSeconds = 0.35f;
	}

	UTDDamageDefinition* CreateEffectOnly(UObject* Outer, const TCHAR* Name, const FTDAreaFootprint& Footprint, const float LifetimeSeconds, const TCHAR* EffectPath, const FVector& Scale, const FVector& Offset)
	{
		UTDDamageDefinition* Definition = CreateAreaDefinition(Outer, Name, Footprint, 0.f, LifetimeSeconds);
		ApplyEffect(Definition, EffectPath, Scale, Offset);
		return Definition;
	}

	void SetRepeatingHits(UTDDamageDefinition* Definition, const float LifetimeSeconds, const float IntervalSeconds)
	{
		Definition->Lifetime = LifetimeSeconds;
		Definition->PulseInterval = IntervalSeconds;
		Definition->HitInterval = IntervalSeconds;
		Definition->MaxHitsPerTarget = 0;
	}

	void CreateDragonAttacks(UObject* Outer, TMap<FName, UTDDamageDefinition*>& OutAttacks)
	{
		UTDDamageDefinition* Bite = CreateShapedAttack(Outer, TEXT("DA_TDDragonBite"), MakeConeFootprint(420.f, 35.f), 0.55f, 1.5f, ETDDamageElement::Physical, ETDTelegraphStyle::Danger);
		UTDDamageDefinition* Claw = CreateShapedAttack(Outer, TEXT("DA_TDDragonClaw"), MakeConeFootprint(380.f, 60.f), 0.7f, 1.2f, ETDDamageElement::Physical, ETDTelegraphStyle::Danger);
		UTDDamageDefinition* ClawCombo = CreateShapedAttack(Outer, TEXT("DA_TDDragonClawCombo"), MakeLineFootprint(520.f, 170.f), 0.8f, 1.2f, ETDDamageElement::Physical, ETDTelegraphStyle::Danger);
		SetRepeatingHits(ClawCombo, 0.8f + 1.15f, 1.05f);
		ClawCombo->MaxHitsPerTarget = 2;
		UTDDamageDefinition* GrabThrow = CreateShapedAttack(Outer, TEXT("DA_TDDragonGrabThrow"), MakeConeFootprint(480.f, 20.f), 1.3f, 3.f, ETDDamageElement::Physical, ETDTelegraphStyle::Unblockable);

		UTDDamageDefinition* Embers = CreateShapedAttack(Outer, TEXT("DA_TDDragonEmbers"), MakeCircleFootprint(200.f), 0.f, 0.15f, ETDDamageElement::Fire, ETDTelegraphStyle::Danger);
		SetRepeatingHits(Embers, 3.f, 0.5f);
		ApplyEffect(Embers, RingOfFlamesEffectPath, FVector(2.6f), FVector::ZeroVector);
		UTDDamageDefinition* Fireball = CreateBolt(Outer, TEXT("DA_TDDragonFireball"), 0.8f, 1.5f);
		Fireball->Rules.Reset();
		AddHitDamage(Fireball, 1.5f, ETDDamageElement::Fire);
		Fireball->Lifetime = 0.8f + 1.6f;
		Fireball->ProjectileSpeed = 1300.f;
		Fireball->ProjectileRadius = 60.f;
		Fireball->DebugColor = FLinearColor(1.f, 0.45f, 0.05f);
		Fireball->VisualEffect.Reset();
		Fireball->bDrawDebug = false;
		ApplyEffect(Fireball, FlameShieldEffectPath, FVector(0.5f), FVector::ZeroVector);
		AddSpawnRule(Fireball, ETDDamageEvent::End, Embers);

		UTDDamageDefinition* BreathFlames = CreateEffectOnly(Outer, TEXT("DA_TDDragonBreathFlames"), MakeConeFootprint(950.f, 35.f), 2.f, RingOfFlamesEffectPath, FVector(6.f, 3.f, 2.f), FVector(475.f, 0.f, 20.f));
		UTDDamageDefinition* FlameBreath = CreateShapedAttack(Outer, TEXT("DA_TDDragonFlameBreath"), MakeConeFootprint(950.f, 35.f), 1.2f, 0.5f, ETDDamageElement::Fire, ETDTelegraphStyle::Unblockable);
		SetRepeatingHits(FlameBreath, 1.2f + 2.f, 0.25f);
		AddSpawnRule(FlameBreath, ETDDamageEvent::Activate, BreathFlames);

		UTDDamageDefinition* Blast = CreateEffectOnly(Outer, TEXT("DA_TDDragonBlast"), MakeCircleFootprint(240.f), 1.f, FlameBlastEffectPath, FVector(1.3f), FVector::ZeroVector);
		Blast->VisualTailSeconds = 2.f;
		UTDDamageDefinition* AirFireball = CreateShapedAttack(Outer, TEXT("DA_TDDragonAirFireball"), MakeCircleFootprint(240.f), 1.1f, 1.5f, ETDDamageElement::Fire, ETDTelegraphStyle::Danger);
		AddSpawnRule(AirFireball, ETDDamageEvent::Activate, Blast);
		UTDDamageDefinition* LandingShock = CreateShapedAttack(Outer, TEXT("DA_TDDragonLandingShock"), MakeCircleFootprint(480.f), 0.7f, 1.8f, ETDDamageElement::Physical, ETDTelegraphStyle::Danger);
		AddSpawnRule(LandingShock, ETDDamageEvent::Activate, Blast);

		UTDDamageDefinition* FireRainDrop = CreateShapedAttack(Outer, TEXT("DA_TDDragonFireRainDrop"), MakeCircleFootprint(220.f), 1.3f, 2.f, ETDDamageElement::Fire, ETDTelegraphStyle::Unblockable);
		AddSpawnRule(FireRainDrop, ETDDamageEvent::Activate, Blast);
		UTDDamageDefinition* FireRain = CreateAreaDefinition(Outer, TEXT("DA_TDDragonFireRain"), MakeCircleFootprint(650.f), 0.f, 2.8f);
		const FVector FireRainOffsets[] = { FVector(0.f, 0.f, 0.f), FVector(380.f, 0.f, 0.f), FVector(0.f, 380.f, 0.f), FVector(-380.f, 0.f, 0.f), FVector(0.f, -380.f, 0.f) };
		int32 DropIndex = 0;
		for (const FVector& DropOffset : FireRainOffsets)
		{
			AddSpawnRule(FireRain, ETDDamageEvent::Spawn, FireRainDrop, DropOffset, DropIndex * 0.3f);
			++DropIndex;
		}

		UTDDamageDefinition* DiveFlames = CreateEffectOnly(Outer, TEXT("DA_TDDragonDiveFlames"), MakeLineFootprint(1300.f, 220.f), 1.f, RingOfFlamesEffectPath, FVector(8.f, 3.f, 2.f), FVector(650.f, 0.f, 20.f));
		UTDDamageDefinition* DiveBreath = CreateShapedAttack(Outer, TEXT("DA_TDDragonDiveBreath"), MakeLineFootprint(1300.f, 220.f), 1.5f, 0.5f, ETDDamageElement::Fire, ETDTelegraphStyle::Unblockable);
		SetRepeatingHits(DiveBreath, 1.5f + 1.f, 0.25f);
		AddSpawnRule(DiveBreath, ETDDamageEvent::Activate, DiveFlames);

		UTDDamageDefinition* Roar = NewObject<UTDDamageDefinition>(Outer, TEXT("DA_TDDragonRoar"));
		Roar->Mode = ETDDamageEntityMode::Shockwave;
		Roar->TargetPolicy = ETDDamageTargetPolicy::Enemies;
		Roar->Cooldown = 0.f;
		Roar->CastRange = 5000.f;
		Roar->Radius = 120.f;
		Roar->InnerRadius = 40.f;
		Roar->ExpansionSpeed = 700.f;
		Roar->HalfHeight = 200.f;
		Roar->ActivationDelay = 0.6f;
		Roar->Lifetime = 0.6f + 1.f;
		Roar->MaxHitsPerTarget = 1;
		Roar->bDestroyOnHit = false;
		Roar->DebugColor = FLinearColor(1.f, 0.45f, 0.05f);
		AddHitDamage(Roar, 0.3f, ETDDamageElement::Physical);

		UTDDamageDefinition* InfernoFlames = CreateEffectOnly(Outer, TEXT("DA_TDDragonInfernoFlames"), MakeCircleFootprint(2400.f), 1.5f, RingOfFlamesEffectPath, FVector(30.f, 30.f, 4.f), FVector::ZeroVector);
		UTDDamageDefinition* SafeZone = CreateAreaDefinition(Outer, TEXT("DA_TDDragonSafeZone"), MakeCircleFootprint(380.f), 4.f, 4.05f);
		SafeZone->DebugColor = GetTelegraphColor(ETDTelegraphStyle::Safe);
		TDMonsterAttackCatalog::ApplyTelegraph(*SafeZone, ETDTelegraphStyle::Safe);
		SafeZone->VisualOffset.Z = 6.f;
		UTDDamageDefinition* Inferno = CreateShapedAttack(Outer, TEXT("DA_TDDragonInferno"), MakeRingFootprint(2400.f, 380.f), 4.f, 6.f, ETDDamageElement::Fire, ETDTelegraphStyle::Danger);
		Inferno->Lifetime = 4.f + 0.3f;
		AddSpawnRule(Inferno, ETDDamageEvent::Spawn, SafeZone);
		AddSpawnRule(Inferno, ETDDamageEvent::Activate, InfernoFlames);

		for (UTDDamageDefinition* Definition : { Bite, Claw, ClawCombo, Fireball, Embers, FlameBreath, BreathFlames, GrabThrow, AirFireball, Blast, FireRain, FireRainDrop, DiveBreath, DiveFlames, LandingShock, Roar, Inferno, SafeZone, InfernoFlames })
		{
			Register(OutAttacks, Definition);
		}
	}
}

void TDMonsterAttackCatalog::CreateAttacks(UObject* Outer, TMap<FName, UTDDamageDefinition*>& OutAttacks)
{
	OutAttacks.Reset();
	if (!IsValid(Outer))
	{
		return;
	}
	const FLinearColor DangerColor(1.f, 0.15f, 0.05f);
	Register(OutAttacks, CreateGroundAttack(Outer, TEXT("DA_TDGoblinSlash"), 110.f, 0.45f, 1.f, ETDDamageElement::Physical, DangerColor));
	Register(OutAttacks, CreateGroundAttack(Outer, TEXT("DA_TDHyenaBite"), 95.f, 0.35f, 1.f, ETDDamageElement::Physical, DangerColor));
	Register(OutAttacks, CreateGroundAttack(Outer, TEXT("DA_TDBruteSmash"), 170.f, 0.75f, 1.4f, ETDDamageElement::Physical, DangerColor));
	Register(OutAttacks, CreateGroundAttack(Outer, TEXT("DA_TDOgreSmash"), 230.f, 0.95f, 1.6f, ETDDamageElement::Physical, DangerColor));
	Register(OutAttacks, CreateGroundAttack(Outer, TEXT("DA_TDGroundBurst"), 150.f, 1.1f, 1.3f, ETDDamageElement::Fire, FLinearColor(1.f, 0.45f, 0.05f)));
	Register(OutAttacks, CreateGroundAttack(Outer, TEXT("DA_TDGolemStomp"), 320.f, 1.2f, 1.2f, ETDDamageElement::Physical, DangerColor));
	Register(OutAttacks, CreateBolt(Outer, TEXT("DA_TDMonsterBolt"), 0.55f, 1.f));
	Register(OutAttacks, CreateGroundAttack(Outer, TEXT("DA_TDBarghestBite"), 110.f, 0.32f, 1.f, ETDDamageElement::Physical, DangerColor));
	Register(OutAttacks, CreateGroundAttack(Outer, TEXT("DA_TDBarghestPounce"), 140.f, 0.5f, 1.4f, ETDDamageElement::Physical, DangerColor));
	Register(OutAttacks, CreateGroundAttack(Outer, TEXT("DA_TDGriffonBite"), 150.f, 0.5f, 1.2f, ETDDamageElement::Physical, DangerColor));
	Register(OutAttacks, CreateGroundAttack(Outer, TEXT("DA_TDGriffonClaws"), 200.f, 0.55f, 1.3f, ETDDamageElement::Physical, DangerColor));
	Register(OutAttacks, CreateGroundAttack(Outer, TEXT("DA_TDGriffonCombo"), 220.f, 1.1f, 1.9f, ETDDamageElement::Physical, DangerColor));
	Register(OutAttacks, CreateGroundAttack(Outer, TEXT("DA_TDCentaurKick"), 170.f, 0.7f, 1.3f, ETDDamageElement::Physical, DangerColor));
	Register(OutAttacks, CreateArrow(Outer, TEXT("DA_TDCentaurArrow"), 0.45f, 1.1f));
	Register(OutAttacks, CreateGroundAttack(Outer, TEXT("DA_TDQuickSlash"), 150.f, 0.32f, 1.15f, ETDDamageElement::Physical, DangerColor));
	Register(OutAttacks, CreateGroundAttack(Outer, TEXT("DA_TDWideCleave"), 190.f, 0.65f, 1.4f, ETDDamageElement::Physical, DangerColor));
	Register(OutAttacks, CreateGroundAttack(Outer, TEXT("DA_TDHeavySlam"), 280.f, 1.f, 2.f, ETDDamageElement::Physical, DangerColor));
	Register(OutAttacks, CreateGroundAttack(Outer, TEXT("DA_TDStaffSpin"), 230.f, 0.6f, 1.5f, ETDDamageElement::Physical, DangerColor));
	CreateDragonAttacks(Outer, OutAttacks);
}

void TDMonsterAttackCatalog::ApplyTelegraph(UTDDamageDefinition& Definition, const ETDTelegraphStyle Style)
{
	const ETDTelegraphForm Form = GetTelegraphForm(Definition);
	const float HalfAngleDegrees = Form == ETDTelegraphForm::Cone ? Definition.ConeHalfAngleDegrees : 0.f;
	const TCHAR* MaterialPath = FindTelegraphMaterialPath(Form, Style, HalfAngleDegrees);
	if (!MaterialPath)
	{
		UE_LOG(LogTDGame, Warning, TEXT("No telegraph material for %s (shape %d, style %d, half angle %g); drawing the debug shape instead."),
			*Definition.GetName(), static_cast<int32>(Definition.AreaShape), static_cast<int32>(Style), HalfAngleDegrees);
		Definition.bDrawDebug = true;
		return;
	}
	if (!DoesAssetPackageExist(TelegraphMeshPath) || !DoesAssetPackageExist(MaterialPath))
	{
		Definition.bDrawDebug = true;
		return;
	}
	Definition.Mesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TelegraphMeshPath));
	Definition.Material = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(MaterialPath));
	Definition.VisualScale = GetTelegraphScale(Definition);
	Definition.VisualOffset = GetTelegraphOffset(Definition);
	Definition.bShowDuringActivationDelay = true;
}

UTDDamageDefinition* TDMonsterAttackCatalog::CreateWindupSpell(UObject* Outer, const FName WrapperName, UTDDamageDefinition* Spell, const float WindupSeconds, const ETDTelegraphStyle Style)
{
	if (!IsValid(Outer) || !IsValid(Spell) || !FMath::IsFinite(WindupSeconds) || WindupSeconds <= 0.f)
	{
		return nullptr;
	}
	UTDDamageDefinition* Wrapper = NewObject<UTDDamageDefinition>(Outer, WrapperName);
	Wrapper->Mode = ETDDamageEntityMode::Area;
	ApplyWindupFootprint(*Wrapper, *Spell);
	Wrapper->ActivationDelay = FMath::Max(WindupSeconds - Spell->ActivationDelay, 0.05f);
	Wrapper->Lifetime = Wrapper->ActivationDelay + 0.05f;
	Wrapper->PulseInterval = 10.f;
	Wrapper->MaxHitsPerTarget = 1;
	Wrapper->Cooldown = 0.f;
	Wrapper->CastRange = 5000.f;
	Wrapper->bDestroyOnHit = false;
	Wrapper->InnerRadius = Wrapper->AreaShape == ETDDamageAreaShape::Ring ? Wrapper->InnerRadius : 0.f;
	Wrapper->HalfHeight = Spell->HalfHeight;
	Wrapper->bProjectToGround = Spell->Mode != ETDDamageEntityMode::Projectile;
	Wrapper->DebugColor = GetTelegraphColor(Style);
	AddSpawnRule(Wrapper, ETDDamageEvent::Activate, Spell);
	ApplyTelegraph(*Wrapper, Style);
	return Wrapper;
}

#include "MonsterAI/TDMonsterAttackCatalog.h"
#include "Combat/Damage/TDDamageDefinition.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "NiagaraSystem.h"

namespace
{
	const TCHAR* TelegraphMeshPath = TEXT("/Engine/BasicShapes/Plane.Plane");
	const TCHAR* TelegraphMaterialPath = TEXT("/Game/MonsterAI/Materials/MI_TD_Telegraph_Danger.MI_TD_Telegraph_Danger");
	const TCHAR* BoltEffectPath = TEXT("/Game/MegaMagicVFXBundle/VFX/MagicShieldsVFX/VFX/DefaultVersions/PoisonShield/Systems/N_PoisonShield.N_PoisonShield");
	constexpr float TelegraphMeshHalfExtent = 50.f;

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

	void ApplyGroundTelegraph(UTDDamageDefinition* Definition)
	{
		UStaticMesh* TelegraphMesh = LoadObject<UStaticMesh>(nullptr, TelegraphMeshPath);
		UMaterialInterface* TelegraphMaterial = LoadObject<UMaterialInterface>(nullptr, TelegraphMaterialPath);
		if (!TelegraphMesh || !TelegraphMaterial)
		{
			Definition->bDrawDebug = true;
			return;
		}
		const float PlaneScale = Definition->Radius / TelegraphMeshHalfExtent;
		Definition->Mesh = TelegraphMesh;
		Definition->Material = TelegraphMaterial;
		Definition->VisualScale = FVector(PlaneScale, PlaneScale, 1.f);
		Definition->VisualOffset = FVector(0.f, 0.f, 3.f);
		Definition->bShowDuringActivationDelay = true;
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
		ApplyGroundTelegraph(Definition);
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
		Definition->VisualEffect = LoadObject<UNiagaraSystem>(nullptr, BoltEffectPath);
		Definition->VisualScale = FVector(0.16f);
		Definition->VisualTailSeconds = 0.3f;
		Definition->bDrawDebug = Definition->VisualEffect == nullptr;
		AddHitDamage(Definition, AttackRatio, ETDDamageElement::Arcane);
		return Definition;
	}

	void Register(TMap<FName, UTDDamageDefinition*>& OutAttacks, UTDDamageDefinition* Definition)
	{
		OutAttacks.Add(Definition->GetFName(), Definition);
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
}

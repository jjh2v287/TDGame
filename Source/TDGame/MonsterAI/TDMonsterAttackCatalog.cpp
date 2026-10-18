#include "MonsterAI/TDMonsterAttackCatalog.h"
#include "Combat/Damage/TDDamageDefinition.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "NiagaraSystem.h"
#include "UObject/SoftObjectPath.h"

namespace
{
	const TCHAR* TelegraphMeshPath = TEXT("/Engine/BasicShapes/Plane.Plane");
	const TCHAR* TelegraphMaterialPath = TEXT("/Game/MonsterAI/Materials/MI_TD_Telegraph_Danger.MI_TD_Telegraph_Danger");
	const TCHAR* BoltEffectPath = TEXT("/Game/MegaMagicVFXBundle/VFX/MagicShieldsVFX/VFX/DefaultVersions/PoisonShield/Systems/N_PoisonShield.N_PoisonShield");
	const TCHAR* ArrowEffectPath = TEXT("/Game/SlashTrail_SoftTofu/Niagara/Basic/NS_SlashTrail_Basic_Loop.NS_SlashTrail_Basic_Loop");
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

	void ApplyGroundTelegraph(UTDDamageDefinition& Definition, UStaticMesh* TelegraphMesh, UMaterialInterface* TelegraphMaterial)
	{
		if (!TelegraphMesh || !TelegraphMaterial)
		{
			Definition.bDrawDebug = true;
			return;
		}
		const float PlaneScale = Definition.Radius / TelegraphMeshHalfExtent;
		Definition.Mesh = TelegraphMesh;
		Definition.Material = TelegraphMaterial;
		Definition.VisualScale = FVector(PlaneScale, PlaneScale, 1.f);
		Definition.VisualOffset = FVector(0.f, 0.f, 3.f);
		Definition.bShowDuringActivationDelay = true;
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
		Definition->VisualScale = FVector(0.16f);
		Definition->VisualTailSeconds = 0.3f;
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
}

TArray<FSoftObjectPath> TDMonsterAttackCatalog::GetPresentationAssetPaths()
{
	return { FSoftObjectPath(TelegraphMeshPath), FSoftObjectPath(TelegraphMaterialPath), FSoftObjectPath(BoltEffectPath) };
}

void TDMonsterAttackCatalog::ApplyPresentationAssets(const TMap<FName, UTDDamageDefinition*>& CatalogAttacks)
{
	UStaticMesh* TelegraphMesh = Cast<UStaticMesh>(FSoftObjectPath(TelegraphMeshPath).ResolveObject());
	UMaterialInterface* TelegraphMaterial = Cast<UMaterialInterface>(FSoftObjectPath(TelegraphMaterialPath).ResolveObject());
	UNiagaraSystem* BoltEffect = Cast<UNiagaraSystem>(FSoftObjectPath(BoltEffectPath).ResolveObject());
	for (const TPair<FName, UTDDamageDefinition*>& Entry : CatalogAttacks)
	{
		UTDDamageDefinition* Definition = Entry.Value;
		if (!Definition)
		{
			continue;
		}
		if (Definition->Mode == ETDDamageEntityMode::Projectile)
		{
			Definition->VisualEffect = BoltEffect;
			Definition->bDrawDebug = BoltEffect == nullptr;
			continue;
		}
		ApplyGroundTelegraph(*Definition, TelegraphMesh, TelegraphMaterial);
	}
}

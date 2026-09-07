#include "Combat/TDDamageExamplesCommandlet.h"
#include "Combat/TDDamageDefinition.h"
#include "Combat/TDDamageExamples.h"
#include "Combat/TDStatusDefinition.h"
#include "TDGame.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectHash.h"

namespace
{
	bool ValidateAsset(UObject* Asset)
	{
		FString Error;
		bool bIsValid = false;
		if (const UTDDamageDefinition* Damage = Cast<UTDDamageDefinition>(Asset))
		{
			bIsValid = Damage->ValidateDefinition(Error);
		}
		if (const UTDStatusDefinition* Status = Cast<UTDStatusDefinition>(Asset))
		{
			bIsValid = Status->ValidateDefinition(Error);
		}
		if (!bIsValid)
		{
			UE_LOG(LogTDGame, Error, TEXT("Invalid damage example %s: %s"), *GetPathNameSafe(Asset), *Error);
		}
		return bIsValid;
	}
}

UTDDamageExamplesCommandlet::UTDDamageExamplesCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 UTDDamageExamplesCommandlet::Main(const FString& Params)
{
#if WITH_EDITOR
	TStrongObjectPtr<UPackage> ExamplesOuter(CreatePackage(TEXT("/Temp/TDDamageExampleGeneration")));
	TArray<UTDDamageDefinition*> Spells;
	TDDamageExamples::CreateExamples(ExamplesOuter.Get(), Spells);
	TArray<UObject*> GraphObjects;
	GetObjectsWithOuter(ExamplesOuter.Get(), GraphObjects, EGetObjectsFlags::None);
	GraphObjects.RemoveAll([](const UObject* Object) { return !Object->IsA<UDataAsset>(); });
	GraphObjects.Sort([](const UObject& Left, const UObject& Right) { return Left.GetName() < Right.GetName(); });
	if (GraphObjects.IsEmpty())
	{
		UE_LOG(LogTDGame, Error, TEXT("No damage examples were generated."));
		return 1;
	}

	const bool bValidateOnly = FParse::Param(*Params, TEXT("ValidateOnly"));
	TArray<FString> PackageNames;
	TArray<FString> Filenames;
	bool bHasConflict = false;
	for (UObject* Asset : GraphObjects)
	{
		const FString PackageName = TEXT("/Game/Combat/Examples/") + Asset->GetName();
		const FString Filename = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
		PackageNames.Add(PackageName);
		Filenames.Add(Filename);
		if (bValidateOnly)
		{
			const FString AssetPath = PackageName + TEXT(".") + Asset->GetName();
			UObject* ExistingAsset = LoadObject<UDataAsset>(nullptr, *AssetPath);
			if (!ExistingAsset || !ValidateAsset(ExistingAsset))
			{
				UE_LOG(LogTDGame, Error, TEXT("Could not validate saved example: %s"), *AssetPath);
				return 1;
			}
			continue;
		}
		if (!ValidateAsset(Asset))
		{
			return 1;
		}
		if (IFileManager::Get().FileExists(*Filename))
		{
			UE_LOG(LogTDGame, Warning, TEXT("Preserving existing example: %s"), *PackageName);
			bHasConflict = true;
		}
	}

	if (bValidateOnly)
	{
		UE_LOG(LogTDGame, Display, TEXT("Validated %d saved damage example assets and %d spell roots."), GraphObjects.Num(), Spells.Num());
		return 0;
	}
	if (bHasConflict)
	{
		UE_LOG(LogTDGame, Error, TEXT("Example generation stopped before writing because at least one destination already exists. Use -ValidateOnly to validate saved examples."));
		return 1;
	}

	for (int32 Index = 0; Index < GraphObjects.Num(); ++Index)
	{
		UObject* Asset = GraphObjects[Index];
		UPackage* Package = CreatePackage(*PackageNames[Index]);
		if (!Package || !Asset->Rename(nullptr, Package, REN_DontCreateRedirectors | REN_NonTransactional))
		{
			UE_LOG(LogTDGame, Error, TEXT("Could not create example package: %s"), *PackageNames[Index]);
			return 1;
		}
		Asset->ClearFlags(RF_Transient);
		Asset->SetFlags(RF_Public | RF_Standalone);
		Package->MarkPackageDirty();
	}

	for (int32 Index = 0; Index < GraphObjects.Num(); ++Index)
	{
		UObject* Asset = GraphObjects[Index];
		if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filenames[Index]), true))
		{
			UE_LOG(LogTDGame, Error, TEXT("Could not create directory for %s"), *Filenames[Index]);
			return 1;
		}
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;
		if (!UPackage::SavePackage(Asset->GetPackage(), Asset, *Filenames[Index], SaveArgs))
		{
			UE_LOG(LogTDGame, Error, TEXT("Could not save damage example: %s"), *Filenames[Index]);
			return 1;
		}
		UE_LOG(LogTDGame, Display, TEXT("Created damage example: %s"), *PackageNames[Index]);
	}
	UE_LOG(LogTDGame, Display, TEXT("Created %d editable damage example assets and %d spell roots."), GraphObjects.Num(), Spells.Num());
	return 0;
#else
	UE_LOG(LogTDGame, Error, TEXT("TDDamageExamples requires an editor build."));
	return 1;
#endif
}

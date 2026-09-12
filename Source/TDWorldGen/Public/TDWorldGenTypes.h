#pragma once

#include "CoreMinimal.h"
#include "Math/RandomStream.h"
#include "TDWorldGenTypes.generated.h"

constexpr int32 TD_WORLDGEN_VERSION = 1;

USTRUCT(BlueprintType)
struct TDWORLDGEN_API FTDSeedContext
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 MasterSeed = 0;

	FTDSeedContext() = default;
	explicit FTDSeedContext(int32 InMasterSeed) : MasterSeed(InMasterSeed) {}

	FRandomStream Derive(FStringView Domain, int32 Index = 0) const;
	int32 DeriveSeed(FStringView Domain, int32 Index = 0) const;
	FGuid DeriveGuid(FStringView Domain, int32 Index = 0) const;
};

UENUM(BlueprintType)
enum class ETDValidationSeverity : uint8
{
	Info,
	Warning,
	Error
};

USTRUCT(BlueprintType)
struct TDWORLDGEN_API FTDValidationItem
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	ETDValidationSeverity Severity = ETDValidationSeverity::Info;

	UPROPERTY(BlueprintReadOnly)
	FName Code;

	UPROPERTY(BlueprintReadOnly)
	FString Message;

	UPROPERTY(BlueprintReadOnly)
	FVector WorldLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly)
	FName RelatedId;
};

USTRUCT(BlueprintType)
struct TDWORLDGEN_API FTDValidationReport
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	TArray<FTDValidationItem> Items;

	UPROPERTY(BlueprintReadOnly)
	float Score = 0.0f;

	UPROPERTY(BlueprintReadOnly)
	bool bPassed = false;

	void Add(ETDValidationSeverity Severity, FName Code, const FString& Message, const FVector& WorldLocation = FVector::ZeroVector, FName RelatedId = NAME_None);
	bool HasErrors() const;
	int32 CountBySeverity(ETDValidationSeverity Severity) const;
	FString ToMarkdown(const FString& Title) const;
};

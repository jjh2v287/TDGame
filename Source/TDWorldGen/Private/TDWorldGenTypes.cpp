#include "TDWorldGenTypes.h"

FRandomStream FTDSeedContext::Derive(FStringView Domain, int32 Index) const
{
	return FRandomStream(DeriveSeed(Domain, Index));
}

int32 FTDSeedContext::DeriveSeed(FStringView Domain, int32 Index) const
{
	uint32 Hash = FCrc::StrCrc32(Domain.GetData());
	Hash = HashCombine(Hash, static_cast<uint32>(MasterSeed));
	Hash = HashCombine(Hash, static_cast<uint32>(Index));
	return static_cast<int32>(Hash & 0x7fffffff);
}

FGuid FTDSeedContext::DeriveGuid(FStringView Domain, int32 Index) const
{
	const uint32 A = static_cast<uint32>(MasterSeed);
	const uint32 B = FCrc::StrCrc32(Domain.GetData());
	const uint32 C = static_cast<uint32>(Index);
	const uint32 D = HashCombine(HashCombine(A, B), C);
	return FGuid(A, B, C, D);
}

void FTDValidationReport::Add(ETDValidationSeverity Severity, FName Code, const FString& Message, const FVector& WorldLocation, FName RelatedId)
{
	FTDValidationItem& Item = Items.AddDefaulted_GetRef();
	Item.Severity = Severity;
	Item.Code = Code;
	Item.Message = Message;
	Item.WorldLocation = WorldLocation;
	Item.RelatedId = RelatedId;
}

bool FTDValidationReport::HasErrors() const
{
	return CountBySeverity(ETDValidationSeverity::Error) > 0;
}

int32 FTDValidationReport::CountBySeverity(ETDValidationSeverity Severity) const
{
	int32 Count = 0;
	for (const FTDValidationItem& Item : Items)
	{
		if (Item.Severity == Severity)
		{
			++Count;
		}
	}
	return Count;
}

FString FTDValidationReport::ToMarkdown(const FString& Title) const
{
	FString Out = FString::Printf(TEXT("# %s\n\n- 결과: %s, 점수 %.1f / 100\n- 오류 %d, 경고 %d, 정보 %d\n\n| 심각도 | 코드 | 메시지 | 위치(cm) | 관련 |\n|---|---|---|---|---|\n"),
		*Title, bPassed ? TEXT("PASS") : TEXT("FAIL"), Score,
		CountBySeverity(ETDValidationSeverity::Error), CountBySeverity(ETDValidationSeverity::Warning), CountBySeverity(ETDValidationSeverity::Info));
	for (const FTDValidationItem& Item : Items)
	{
		const TCHAR* Severity = Item.Severity == ETDValidationSeverity::Error ? TEXT("Error") : Item.Severity == ETDValidationSeverity::Warning ? TEXT("Warning") : TEXT("Info");
		Out += FString::Printf(TEXT("| %s | %s | %s | (%.0f, %.0f, %.0f) | %s |\n"), Severity, *Item.Code.ToString(), *Item.Message, Item.WorldLocation.X, Item.WorldLocation.Y, Item.WorldLocation.Z, *Item.RelatedId.ToString());
	}
	return Out;
}

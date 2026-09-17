#pragma once

#include "CoreMinimal.h"
#include "UObject/Package.h"

// 애니메이션 저작 도구 공용 로그. 도구가 반환하는 오류 메시지는 "에디터 로그를 확인하라"고
// 안내하므로, 모든 도구가 요청·성공·실패를 이 카테고리에 남긴다.
DECLARE_LOG_CATEGORY_EXTERN(LogTDAnimAuthoring, Log, All);

// 실패 로그에 도구 이름을 붙이기 위한 현재 실행 중인 도구 이름. 저작 도구는 전부
// 에디터 게임 스레드에서만 동작하므로 단일 값으로 충분하다.
inline const TCHAR*& TDActiveAuthoringTool()
{
	static const TCHAR* ToolName = TEXT("TDAnimationAuthoring");
	return ToolName;
}

// 도구 진입 시 이름을 등록하고 요청을 기록한다.
struct FTDAuthoringToolScope
{
	explicit FTDAuthoringToolScope(const TCHAR* InToolName, const FString& Request)
		: PreviousToolName(TDActiveAuthoringTool())
	{
		TDActiveAuthoringTool() = InToolName;
		UE_LOG(LogTDAnimAuthoring, Log, TEXT("%s: request (%d chars): %s"), InToolName, Request.Len(), *Request.Left(1024));
	}

	~FTDAuthoringToolScope()
	{
		TDActiveAuthoringTool() = PreviousToolName;
	}

	FTDAuthoringToolScope(const FTDAuthoringToolScope&) = delete;
	FTDAuthoringToolScope& operator=(const FTDAuthoringToolScope&) = delete;

private:
	const TCHAR* PreviousToolName = nullptr;
};

// 목적지 패키지를 만들고, Commit 없이 스코프를 벗어나면 패키지와 그 안의 에셋을 폐기한다.
// 이 정리가 없으면 실패한 요청이 빈 패키지를 메모리에 남기고, 목적지 검사가 메모리 내
// 패키지도 거부하므로 같은 asset_path로는 에디터를 재시작하기 전까지 재시도할 수 없다.
struct FTDAuthoringPackageScope
{
	explicit FTDAuthoringPackageScope(const FString& PackagePath)
		: Package(CreatePackage(*PackagePath))
	{
	}

	~FTDAuthoringPackageScope()
	{
		if (bIsCommitted || !Package)
		{
			return;
		}
		// MarkAsGarbage만으로는 다음 가비지 컬렉션 전까지 이름이 오브젝트 해시에 남는다.
		// 목적지 검사가 메모리 내 패키지·오브젝트도 거부하므로, 이름을 비워야 같은
		// asset_path로 재시도할 수 있다.
		if (Asset)
		{
			const FName DiscardedAssetName = MakeUniqueObjectName(GetTransientPackage(), Asset->GetClass(), *FString::Printf(TEXT("%s_TDDiscarded"), *Asset->GetName()));
			Asset->ClearFlags(RF_Public | RF_Standalone);
			Asset->Rename(*DiscardedAssetName.ToString(), GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional | REN_DoNotDirty);
			Asset->MarkAsGarbage();
		}
		const FName DiscardedPackageName = MakeUniqueObjectName(nullptr, UPackage::StaticClass(), *FString::Printf(TEXT("%s_TDDiscarded"), *Package->GetName()));
		Package->SetDirtyFlag(false);
		Package->Rename(*DiscardedPackageName.ToString(), nullptr, REN_DontCreateRedirectors | REN_NonTransactional | REN_DoNotDirty);
		Package->MarkAsGarbage();
		UE_LOG(LogTDAnimAuthoring, Log, TEXT("%s: discarded the unused destination package so the same asset_path can be retried."), TDActiveAuthoringTool());
	}

	FTDAuthoringPackageScope(const FTDAuthoringPackageScope&) = delete;
	FTDAuthoringPackageScope& operator=(const FTDAuthoringPackageScope&) = delete;

	UPackage* Get() const
	{
		return Package;
	}

	// 성공 시 패키지와 함께 남을 에셋. 실패하면 이 에셋도 함께 폐기된다.
	void Track(UObject* InAsset)
	{
		Asset = InAsset;
	}

	void Commit()
	{
		bIsCommitted = true;
	}

private:
	UPackage* Package = nullptr;
	UObject* Asset = nullptr;
	bool bIsCommitted = false;
};

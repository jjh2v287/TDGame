#pragma once

#include "CoreMinimal.h"
#include "Editor.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"

class FTDPieWaitUntilCommand : public IAutomationLatentCommand
{
public:
	FTDPieWaitUntilCommand(FAutomationTestBase& InTest, const FString& InDescription, TFunction<bool()> InCondition, double InTimeoutSeconds, TFunction<void()> InOnTimeout)
		: Test(InTest)
		, Description(InDescription)
		, Condition(MoveTemp(InCondition))
		, TimeoutSeconds(InTimeoutSeconds)
		, OnTimeout(MoveTemp(InOnTimeout))
	{
	}

	virtual bool Update() override
	{
		if (Condition())
		{
			Test.AddInfo(FString::Printf(TEXT("%s: done after %.2fs."), *Description, GetCurrentRunTime()));
			return true;
		}
		if (GetCurrentRunTime() < TimeoutSeconds)
		{
			return false;
		}
		Test.AddError(FString::Printf(TEXT("%s: timed out after %.0fs."), *Description, TimeoutSeconds));
		if (OnTimeout)
		{
			OnTimeout();
		}
		return true;
	}

private:
	FAutomationTestBase& Test;
	FString Description;
	TFunction<bool()> Condition;
	double TimeoutSeconds;
	TFunction<void()> OnTimeout;
};

namespace TDPieTestUtils
{
	inline const TCHAR* OpenWorldMapPackagePath = TEXT("/Game/Level/LV_DarkFantasy_OpenWorld");

	inline UWorld* GetPieWorld()
	{
		const FWorldContext* PieContext = GEditor ? GEditor->GetPIEWorldContext() : nullptr;
		return PieContext ? PieContext->World() : nullptr;
	}

	inline APawn* FindPlayerPawn(UWorld* World)
	{
		APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
		return PlayerController ? PlayerController->GetPawn() : nullptr;
	}

	inline bool OpenMapAndStartPie(FAutomationTestBase& Test, const FString& MapPackagePath)
	{
		if (AutomationOpenMap(MapPackagePath))
		{
			return true;
		}
		Test.AddError(FString::Printf(TEXT("Failed to open map '%s' for PIE."), *MapPackagePath));
		return false;
	}

	inline void EnqueueStep(TFunction<bool()> Step)
	{
		FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShareable(new FFunctionLatentCommand(MoveTemp(Step))));
	}

	inline void EnqueueWaitSeconds(float Seconds)
	{
		FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShareable(new FWaitLatentCommand(Seconds)));
	}

	inline void EnqueueWaitUntil(FAutomationTestBase& Test, const FString& Description, TFunction<bool()> Condition, double TimeoutSeconds, TFunction<void()> OnTimeout)
	{
		FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShareable(new FTDPieWaitUntilCommand(Test, Description, MoveTemp(Condition), TimeoutSeconds, MoveTemp(OnTimeout))));
	}

	inline void EnqueueEndPie()
	{
		FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShareable(new FEndPlayMapCommand()));
	}
}

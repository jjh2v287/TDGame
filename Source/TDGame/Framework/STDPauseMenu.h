#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class TDGAME_API STDPauseMenu : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(STDPauseMenu)
	{
	}
	SLATE_EVENT(FSimpleDelegate, OnContinueClicked)
	SLATE_EVENT(FSimpleDelegate, OnSaveAndQuitClicked)
	SLATE_EVENT(FSimpleDelegate, OnNewGameClicked)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual bool SupportsKeyboardFocus() const override { return true; }

private:
	TSharedRef<SWidget> MakeMenuButton(const FText& Label, const FSimpleDelegate& OnClicked) const;
};

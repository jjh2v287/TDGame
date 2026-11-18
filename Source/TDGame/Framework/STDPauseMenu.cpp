#include "Framework/STDPauseMenu.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	constexpr float MenuTitleFontSize = 36.f;
	constexpr float MenuButtonFontSize = 22.f;
	constexpr float MenuButtonWidth = 320.f;
	constexpr float MenuButtonSpacing = 12.f;
	constexpr float MenuBackdropAlpha = 0.6f;
}

void STDPauseMenu::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FLinearColor(0.f, 0.f, 0.f, MenuBackdropAlpha))
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SBox)
			.WidthOverride(MenuButtonWidth)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.f, 0.f, 0.f, MenuButtonSpacing * 2.f)
				.HAlign(HAlign_Center)
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("일시정지")))
					.Font(FCoreStyle::GetDefaultFontStyle(FName(TEXT("Bold")), MenuTitleFontSize))
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.f, 0.f, 0.f, MenuButtonSpacing)
				[
					MakeMenuButton(FText::FromString(TEXT("계속")), InArgs._OnContinueClicked)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.f, 0.f, 0.f, MenuButtonSpacing)
				[
					MakeMenuButton(FText::FromString(TEXT("저장 후 종료")), InArgs._OnSaveAndQuitClicked)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					MakeMenuButton(FText::FromString(TEXT("새로 시작")), InArgs._OnNewGameClicked)
				]
			]
		]
	];
}

TSharedRef<SWidget> STDPauseMenu::MakeMenuButton(const FText& Label, const FSimpleDelegate& OnClicked) const
{
	return SNew(SButton)
		.HAlign(HAlign_Center)
		.OnClicked_Lambda([OnClicked]()
		{
			OnClicked.ExecuteIfBound();
			return FReply::Handled();
		})
		[
			SNew(STextBlock)
			.Text(Label)
			.Font(FCoreStyle::GetDefaultFontStyle(FName(TEXT("Bold")), MenuButtonFontSize))
		];
}

// Copyright ImSlate, Inc. All Rights Reserved.
#pragma once

#include "SlateCore.h"
#include "Components/Widget.h"
#include "Widgets/Input/SEditableText.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SCompoundWidget.h"
#include "ImSlateFactory.h"

#include "ImSearchBox.generated.h"

using FImSearchBoxSuggestionProvider = TFunction<void(const FString&, TArray<FString>&)>;

class IMSLATE_API SImSearchBox : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SImSearchBox)
		: _bUseInlineSuggestions(false)
		, _bShowKeyboardButton(false)
	{}
		SLATE_ARGUMENT(bool, bUseInlineSuggestions)
		SLATE_ARGUMENT(bool, bShowKeyboardButton)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	void SetText(const FText& InText);
	FText GetText() const;

	void SetSuggestions(const TArray<FString>& InItems);
	void SetSuggestions(const TArray<FString>& InItems, int32 HistoryCount, TFunction<void(const FString&)> OnDeleteHistory);
	void ClearSuggestions();

	bool HasPendingCommit() const { return bCommitted; }
	bool IsMenuOpen() const { return bSuggestionsVisible; }
	bool ConsumeNeedsSuggestionRefresh() { bool b = bNeedsSuggestionRefresh; bNeedsSuggestionRefresh = false; return b; }
	FString ConsumeCommit();

	TSharedPtr<SEditableText> GetEditableText() const { return EditText; }
	void SetUseInlineSuggestions(bool bInline) { bUseInlineSuggestions = bInline; }
	void SetKeyboardSuggestionProvider(FImSearchBoxSuggestionProvider InProvider);

protected:
	virtual FReply OnPreviewKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

private:
	TSharedPtr<SEditableText> EditText;
	TSharedPtr<SWidget> KeyboardButtonWidget;
	TSharedPtr<SVerticalBox> SuggestionList;
	TSharedPtr<SBox> SuggestionContainer;
	TSharedPtr<SWindow> SuggestionWindow;
	FDelegateHandle ClickOutsideHandle;
	TArray<TSharedPtr<SButton>> SuggestionButtons;
	bool bUseInlineSuggestions = false;
	bool bShowKeyboardButton = false;
	bool bCommitted = false;
	bool bSuggestionsVisible = false;
	bool bNeedsSuggestionRefresh = false;
	FString CommittedText;
	int32 HighlightIndex = -1;
	TArray<FString> CurrentSuggestions;
	FString TextBeforeNavigate;
	FImSearchBoxSuggestionProvider KeyboardSuggestionProvider;

	void OnTextChanged(const FText& NewText);
	void OnTextCommitted(const FText& Text, ETextCommit::Type Type);
	void OnSuggestionSelected(const FString& Item);
	void UpdateHighlight(int32 NewIndex);
	void ShowSuggestions(bool bShow);
	void ShowInline(bool bShow);
	void ShowFloating(bool bShow);
};

UCLASS(BlueprintType)
class IMSLATE_API UImSearchBox
	: public UWidget
	, public TImFactory<SImSearchBox>
{
	GENERATED_BODY()
public:
	TSharedRef<SImSearchBox> ConstructImWidget() const;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

	TSharedPtr<SImSearchBox> MyWidget;

protected:
	IM_SLATE_PALETTECATEGORY()
};

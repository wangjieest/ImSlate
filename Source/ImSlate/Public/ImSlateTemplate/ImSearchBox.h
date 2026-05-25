// Copyright ImSlate, Inc. All Rights Reserved.
#pragma once

#include "SlateCore.h"
#include "Components/Widget.h"
#include "Widgets/Input/SEditableText.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SMenuAnchor.h"
#include "ImSlateFactory.h"

#include "ImSearchBox.generated.h"

class IMSLATE_API SImSearchBox : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SImSearchBox) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	void SetText(const FText& InText);
	FText GetText() const;

	void SetSuggestions(const TArray<FString>& InItems);
	void ClearSuggestions();

	bool HasPendingCommit() const { return bCommitted; }
	FString ConsumeCommit();

	TSharedPtr<SEditableText> GetEditableText() const { return EditText; }

private:
	TSharedPtr<SEditableText> EditText;
	TSharedPtr<SMenuAnchor> Anchor;
	TSharedPtr<SVerticalBox> SuggestionList;
	bool bCommitted = false;
	FString CommittedText;

	void OnTextChanged(const FText& NewText);
	void OnTextCommitted(const FText& Text, ETextCommit::Type Type);
	void OnSuggestionSelected(const FString& Item);
	TSharedRef<SWidget> MakeMenuContent();
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

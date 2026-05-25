// Copyright ImSlate, Inc. All Rights Reserved.
#include "ImSlateTemplate/ImSearchBox.h"
#include "ImSlateTemplate/ImEditableText.h"
#include "ProtectFieldAccessor.h"

#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"

void SImSearchBox::Construct(const FArguments& InArgs)
{
	SuggestionList = SNew(SVerticalBox);

	auto MyEditText = SNew(SImEditableText)
		.Font(ImSlate::GetImSlateDefaultFont());

	GS_ACCESS_PROTECT(MyEditText, SEditableText, OnTextChangedCallback)->OnTextChangedCallback =
		FOnTextChanged::CreateSP(this, &SImSearchBox::OnTextChanged);
	GS_ACCESS_PROTECT(MyEditText, SEditableText, OnTextCommittedCallback)->OnTextCommittedCallback =
		FOnTextCommitted::CreateSP(this, &SImSearchBox::OnTextCommitted);

	// Background brush for edit box
	static FSlateBrush EditBgBrush;
	static bool bInitBrush = false;
	if (!bInitBrush)
	{
		EditBgBrush.DrawAs = ESlateBrushDrawType::RoundedBox;
		EditBgBrush.TintColor = FLinearColor(0.05f, 0.05f, 0.05f, 0.8f);
		EditBgBrush.OutlineSettings.Color = FLinearColor(0.3f, 0.3f, 0.3f, 1.f);
		EditBgBrush.OutlineSettings.Width = 1.f;
		EditBgBrush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
		EditBgBrush.OutlineSettings.CornerRadii = FVector4(4, 4, 4, 4);
		bInitBrush = true;
	}
	MyEditText->SetBorderImage(&EditBgBrush);

	EditText = MyEditText;

	ChildSlot
	[
		SAssignNew(Anchor, SMenuAnchor)
		.Placement(MenuPlacement_BelowAnchor)
		.OnGetMenuContent(FOnGetContent::CreateSP(this, &SImSearchBox::MakeMenuContent))
		[
			MyEditText
		]
	];
}

void SImSearchBox::SetText(const FText& InText)
{
	if (EditText) EditText->SetText(InText);
}

FText SImSearchBox::GetText() const
{
	return EditText ? EditText->GetText() : FText::GetEmpty();
}

void SImSearchBox::SetSuggestions(const TArray<FString>& InItems)
{
	if (!SuggestionList) return;
	SuggestionList->ClearChildren();
	for (const FString& Item : InItems)
	{
		FString ItemCopy = Item;
		SuggestionList->AddSlot()
		.AutoHeight()
		[
			SNew(SButton)
			.OnClicked_Lambda([this, ItemCopy]() {
				OnSuggestionSelected(ItemCopy);
				return FReply::Handled();
			})
			[
				SNew(STextBlock)
				.Text(FText::FromString(ItemCopy))
				.Font(ImSlate::GetImSlateDefaultFont())
			]
		];
	}
	if (Anchor)
		Anchor->SetIsOpen(InItems.Num() > 0);
}

void SImSearchBox::ClearSuggestions()
{
	if (SuggestionList) SuggestionList->ClearChildren();
	if (Anchor) Anchor->SetIsOpen(false);
}

FString SImSearchBox::ConsumeCommit()
{
	bCommitted = false;
	return MoveTemp(CommittedText);
}

void SImSearchBox::OnTextChanged(const FText& NewText)
{
}

void SImSearchBox::OnTextCommitted(const FText& Text, ETextCommit::Type Type)
{
	if (Type != ETextCommit::OnCleared)
	{
		CommittedText = Text.ToString();
		bCommitted = true;
		ClearSuggestions();
	}
}

void SImSearchBox::OnSuggestionSelected(const FString& Item)
{
	CommittedText = Item;
	bCommitted = true;
	if (EditText) EditText->SetText(FText::FromString(Item));
	ClearSuggestions();
}

TSharedRef<SWidget> SImSearchBox::MakeMenuContent()
{
	return SNew(SBox)
		.MaxDesiredHeight(250.f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				SuggestionList.ToSharedRef()
			]
		];
}

// UImSearchBox
TSharedRef<SImSearchBox> UImSearchBox::ConstructImWidget() const
{
	return SNew(SImSearchBox);
}

TSharedRef<SWidget> UImSearchBox::RebuildWidget()
{
	MyWidget = SNew(SImSearchBox);
	return MyWidget.ToSharedRef();
}

void UImSearchBox::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	MyWidget.Reset();
}

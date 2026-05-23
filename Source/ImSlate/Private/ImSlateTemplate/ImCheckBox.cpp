// Copyright ImSlate, Inc. All Rights Reserved.
#include "ImSlateTemplate/ImCheckBox.h"

SImCheckBox::SImCheckBox()
{
}

void SImCheckBox::Construct(const FArguments& InArgs)
{
	check(InArgs._Style != nullptr);
	Style = InArgs._Style;

	UncheckedImage = InArgs._UncheckedImage;
	UncheckedHoveredImage = InArgs._UncheckedHoveredImage;
	UncheckedPressedImage = InArgs._UncheckedPressedImage;

	CheckedImage = InArgs._CheckedImage;
	CheckedHoveredImage = InArgs._CheckedHoveredImage;
	CheckedPressedImage = InArgs._CheckedPressedImage;

	UndeterminedImage = InArgs._UndeterminedImage;
	UndeterminedHoveredImage = InArgs._UndeterminedHoveredImage;
	UndeterminedPressedImage = InArgs._UndeterminedPressedImage;

	PaddingOverride = InArgs._Padding;
	ForegroundColorOverride = InArgs._ForegroundColor;
	BorderBackgroundColorOverride = InArgs._BorderBackgroundColor;
	CheckBoxTypeOverride = InArgs._Type;

	HorizontalAlignment = InArgs._HAlign;
	bCheckBoxContentUsesAutoWidth = InArgs._CheckBoxContentUsesAutoWidth;

	bIsPressed = false;
	bIsFocusable = InArgs._IsFocusable;

	BuildCheckBox(InArgs._Content.Widget);

	IsCheckboxChecked = InArgs._IsChecked;
	OnCheckStateChanged = InArgs._OnCheckStateChanged;

	ClickMethod = InArgs._ClickMethod;
	TouchMethod = InArgs._TouchMethod;
	PressMethod = InArgs._PressMethod;

	OnGetMenuContent = InArgs._OnGetMenuContent;

	HoveredSound = InArgs._HoveredSoundOverride.Get(InArgs._Style->HoveredSlateSound);
	CheckedSound = InArgs._CheckedSoundOverride.Get(InArgs._Style->CheckedSlateSound);
	UncheckedSound = InArgs._UncheckedSoundOverride.Get(InArgs._Style->UncheckedSlateSound);
}

void SImCheckBox::SetStyle(const FCheckBoxStyle* InStyle, const FImCheckBoxExtraStyle* InExtraStyle)
{
	Style = InStyle;

	if (Style == nullptr)
	{
		FArguments Defaults;
		Style = Defaults._Style;
	}

	check(Style);

	ExtraStyle = InExtraStyle;
	check(ExtraStyle);

	BuildCheckBox(ContentContainer->GetContent());
}

void SImCheckBox::BuildCheckBox(TSharedRef<SWidget> InContent)
{
	if (ContentContainer.IsValid())
	{
		ContentContainer->SetContent(SNullWidget::NullWidget);
	}

	ESlateCheckBoxType::Type CheckBoxType = OnGetCheckBoxType();

	if (CheckBoxType == ESlateCheckBoxType::CheckBox)
	{
		// Check boxes use a separate check button to the side of the user's content (often, a text label or icon.)
		SHorizontalBox::FSlot* ContentSlot;
		this->ChildSlot
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Center)
			[
				SNew(SImage)
				.Image(this, &SImCheckBox::OnGetCheckImage)
#if UE_5_00_OR_LATER
				.ColorAndOpacity(this, &SImCheckBox::GetForegroundColor)
#else
				.ColorAndOpacity(this, &SImCheckBox::OnGetForegroundColor)
#endif
			]
			+ SHorizontalBox::Slot()
			.Padding(TAttribute<FMargin>(this, &SImCheckBox::OnGetPadding))
			.VAlign(VAlign_Center)
			.Expose(ContentSlot)
			[
				SAssignNew(ContentContainer, SBorder)
				.BorderImage(FStyleDefaults::GetNoBrush())
				.Padding(0.0f)
				[
					InContent
				]
			]
		];
		if (bCheckBoxContentUsesAutoWidth)
		{
#if UE_5_00_OR_LATER
			ContentSlot->SetAutoWidth();
#else
			ContentSlot->AutoWidth();
#endif
		}
	}
	else if (ensure(CheckBoxType == ESlateCheckBoxType::ToggleButton))
	{
		// Toggle buttons have a visual appearance that is similar to a Slate button
		this->ChildSlot
		[
			SAssignNew(ContentContainer, SBorder)
			.BorderImage(this, &SImCheckBox::OnGetCheckImage)
			.Padding(this, &SImCheckBox::OnGetPadding)
#if ENGINE_MAJOR_VERSION >= 5
			.ColorAndOpacity(this, &SImCheckBox::GetColorAndOpacity)
#else
			.ForegroundColor(this, &SImCheckBox::OnGetForegroundColor)
#endif
			.BorderBackgroundColor(this, &SImCheckBox::OnGetBorderBackgroundColor)
			.ShowEffectWhenDisabled(false)
			.HAlign(HorizontalAlignment)
			[
				InContent
			]
		];
	}
}

const FSlateBrush* SImCheckBox::OnGetCheckImage() const
{
	ECheckBoxState State = IsCheckboxChecked.Get();

	const FSlateBrush* ImageToUse = nullptr;
	switch (State)
	{
		case ECheckBoxState::Unchecked:
			if (IsEnabled())
			{
				if (IsPressed())
				{
					ImageToUse = GetUncheckedPressedImage();
				}
				else if (IsHovered())
				{
					ImageToUse = GetUncheckedHoveredImage();
				}
				else if (IsFocused())
				{
					ImageToUse = &ExtraStyle->FocusedUncheckedImage;
				}
				else
				{
					ImageToUse = GetUncheckedImage();
				}
			}
			else
			{
				ImageToUse = &ExtraStyle->DisabledUncheckedImage;
			}
			break;

		case ECheckBoxState::Checked:
			if (IsEnabled())
			{
				if (IsPressed())
				{
					ImageToUse = GetCheckedPressedImage();
				}
				else if (IsHovered())
				{
					ImageToUse = GetCheckedHoveredImage();
				}
				else if (IsFocused())
				{
					ImageToUse = &ExtraStyle->FocusedCheckedImage;
				}
				else
				{
					ImageToUse = GetCheckedImage();
				}
			}
			else
			{
				ImageToUse = &ExtraStyle->DisabledCheckedImage;
			}
			break;
		case ECheckBoxState::Undetermined:
			if (IsEnabled())
			{
				if (IsPressed())
				{
					ImageToUse = GetUndeterminedPressedImage();
				}
				else if (IsHovered())
				{
					ImageToUse = GetUndeterminedHoveredImage();
				}
				else if (IsFocused())
				{
					ImageToUse = &ExtraStyle->FocusedUndeterminedImage;
				}
				else
				{
					ImageToUse = GetUndeterminedImage();
				}
			}
			else
			{
				ImageToUse = &ExtraStyle->DisabledUndeterminedImage;
			}
		default:
			break;
	}

	return ImageToUse;
}

void SImCheckBox::SetOnCheckStateChanged(FOnCheckStateChanged InDelegate)
{
	OnCheckStateChanged = InDelegate;
}

TSharedRef<SImCheckBox> UImCheckBox::ConstructImWidget() const
{
	auto MyStealCheckBox = SNew(SImCheckBox)
							//.OnCheckStateChanged(this, &UImCheckBox::SlateOnCheckStateChangedCallback);
							//.OnCheckStateChanged(BIND_UOBJECT_DELEGATE(FOnCheckStateChanged, SlateOnCheckStateChangedCallback))
							.Style(&GetWidgetStyle())
							.HAlign(HorizontalAlignment)
							.ClickMethod(GetClickMethod())
							.TouchMethod(GetTouchMethod())
							.PressMethod(GetPressMethod())
							.IsFocusable(GetIsFocusable());

	// UWidget

	// #if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	// 	bRoutedSynchronizeProperties = true;
	// #endif

#if WIDGET_INCLUDE_RELFECTION_METADATA
	// We only need to do this once, when the slate widget is created.
	MyStealCheckBox->AddMetadata<FReflectionMetaData>(MakeShared<FReflectionMetaData>(GetFName(), GetClass(), (UObject*)this, GetSourceAssetOrClass()));
#endif

	{
		if (bOverride_Cursor /*|| CursorDelegate.IsBound()*/)
		{
			MyStealCheckBox->SetCursor(GetCursor());  // PROPERTY_BINDING(EMouseCursor::Type, Cursor));
		}

		//MyStealCheckBox->SetEnabled(BITFIELD_PROPERTY_BINDING(bIsEnabled));
		MyStealCheckBox->SetVisibility(ConvertVisibility(GetVisibility()));
	}

	MyStealCheckBox->SetClipping(GetClipping());

	MyStealCheckBox->SetFlowDirectionPreference(GetFlowDirectionPreference());
	MyStealCheckBox->ForceVolatile(bIsVolatile);
	MyStealCheckBox->SetRenderOpacity(GetRenderOpacity());

	if (GetRenderTransform().IsIdentity())
	{
		MyStealCheckBox->SetRenderTransform(TOptional<FSlateRenderTransform>());
	}
	else
	{
		MyStealCheckBox->SetRenderTransform(GetRenderTransform().ToSlateRenderTransform());
	}

	MyStealCheckBox->SetRenderTransformPivot(GetRenderTransformPivot());

	// 	if (ToolTipWidgetDelegate.IsBound() && !IsDesignTime())
	// 	{
	// 		TSharedRef<ImSlate::FDelegateToolTip> ToolTip = MakeShared<ImSlate::FDelegateToolTip>();
	// 		ToolTip->ToolTipWidgetDelegate = ToolTipWidgetDelegate;
	// 		MyStealEditableText->SetToolTip(GetToolTip());
	// 	}
	// 	else if (ToolTipWidget != nullptr)
	// 	{
	// 		TSharedRef<SToolTip> ToolTip = SNew(SToolTip)
	// 										.TextMargin(FMargin(0))
	// 										.BorderImage(nullptr)
	// 										[
	// 											ToolTipWidget->TakeWidget()
	// 										];
	//
	// 		MyStealEditableText->SetToolTip(GetToolTip());
	// 	}
	// 	else if (!ToolTipText.IsEmpty() || ToolTipTextDelegate.IsBound())
	// 	{
	// 		MyStealEditableText->SetToolTipText(PROPERTY_BINDING(FText, ToolTipText));
	// 	}

	// UWidget End

	MyStealCheckBox->SetStyle(&GetWidgetStyle(), &ExtraStyle);
	MyStealCheckBox->SetIsChecked(GetCheckedState());


	return MyStealCheckBox;
}

void UImCheckBox::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	MyImCheckbox.Reset();
}

TSharedRef<SWidget> UImCheckBox::RebuildWidget()
{
	MyImCheckbox = SNew(SImCheckBox)
					.Style(&GetWidgetStyle())
					.HAlign(HorizontalAlignment)
					.ClickMethod(GetClickMethod())
					.TouchMethod(GetTouchMethod())
					.PressMethod(GetPressMethod())
					.IsFocusable(GetIsFocusable());

	if (GetChildrenCount() > 0)
	{
		MyImCheckbox->SetContent(GetContentSlot()->Content ? GetContentSlot()->Content->TakeWidget() : SNullWidget::NullWidget);
	}
	MyImCheckbox->SetOnCheckStateChanged(BIND_UOBJECT_DELEGATE(FOnCheckStateChanged, SlateOnCheckStateChangedCallback));

	MyCheckbox = MyImCheckbox;

	return MyImCheckbox.ToSharedRef();
}

void UImCheckBox::SynchronizeProperties()
{
	Super::SynchronizeProperties();
	MyImCheckbox->SetStyle(&GetWidgetStyle(), &ExtraStyle);
}

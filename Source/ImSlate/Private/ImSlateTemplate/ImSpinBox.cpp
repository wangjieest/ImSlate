// Copyright ImSlate, Inc. All Rights Reserved.
#include "ImSlateTemplate/ImSpinBox.h"

#include "PrivateFieldAccessor.h"
#include "Widgets/Text/SlateEditableTextLayout.h"

//
namespace ImSlateSpinBox
{
GS_PRIVATEACCESS_MEMBER(FSlateEditableTextLayout, Margin, TAttribute<FMargin>);
using ImSpinBox = SSpinBox<float>;
GS_PRIVATEACCESS_MEMBER(ImSpinBox, EditableText, TSharedPtr<SEditableText>);
GS_PRIVATEACCESS_MEMBER(ImSpinBox, TextBlock, TSharedPtr<STextBlock>);

FMargin GetLayoutMargin(FSlateEditableTextLayout* InLayout)
{
	return PrivateAccess::Margin(*InLayout).Get();
}
void ApplyMargin(TSharedRef<SSpinBox<float>>& MyStealSpinBox)
{
	auto Margin = GS_ACCESS_PROTECT(PrivateAccess::EditableText(MyStealSpinBox.Get()), SEditableText, EditableTextLayout)->EditableTextLayout.Get();
	PrivateAccess::TextBlock(MyStealSpinBox.Get())->SetMargin(GetLayoutMargin(Margin));
}
}  // namespace ImSlateSpinBox

TSharedRef<SSpinBox<float>> UImSpinBox::ConstructImWidget() const
{
	auto MyStealSpinBox = SNew(SSpinBox<float>)
							.Style(&GetWidgetStyle())
							.Font(GetFont())
							.ClearKeyboardFocusOnCommit(GetClearKeyboardFocusOnCommit())
							.SelectAllTextOnCommit(GetSelectAllTextOnCommit())
							.Justification(GetJustification())
	//.OnValueChanged(BIND_UOBJECT_DELEGATE(FOnFloatValueChanged, HandleOnValueChanged))
	//.OnValueCommitted(BIND_UOBJECT_DELEGATE(FOnFloatValueCommitted, HandleOnValueCommitted))
	//.OnBeginSliderMovement(BIND_UOBJECT_DELEGATE(FSimpleDelegate, HandleOnBeginSliderMovement))
	//.OnEndSliderMovement(BIND_UOBJECT_DELEGATE(FOnFloatValueChanged, HandleOnEndSliderMovement));
		;
#if 0
	ImSlateSpinBox::ApplyMargin(MyStealSpinBox);
#endif
	// UWidget

	// #if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	// 	bRoutedSynchronizeProperties = true;
	// #endif

#if WIDGET_INCLUDE_RELFECTION_METADATA
	// We only need to do this once, when the slate widget is created.
	MyStealSpinBox->AddMetadata<FReflectionMetaData>(MakeShared<FReflectionMetaData>(GetFName(), GetClass(), (UObject*)this, GetSourceAssetOrClass()));
#endif

	{
		if (bOverride_Cursor /*|| CursorDelegate.IsBound()*/)
		{
			MyStealSpinBox->SetCursor(GetCursor());  // PROPERTY_BINDING(EMouseCursor::Type, Cursor));
		}

		//MyStealSpinBox->SetEnabled(BITFIELD_PROPERTY_BINDING(bIsEnabled));
		MyStealSpinBox->SetVisibility(ConvertVisibility(GetVisibility()));
	}

	MyStealSpinBox->SetClipping(GetClipping());

	MyStealSpinBox->SetFlowDirectionPreference(GetFlowDirectionPreference());
	MyStealSpinBox->ForceVolatile(bIsVolatile);
	MyStealSpinBox->SetRenderOpacity(GetRenderOpacity());

	if (GetRenderTransform().IsIdentity())
	{
		MyStealSpinBox->SetRenderTransform(TOptional<FSlateRenderTransform>());
	}
	else
	{
		MyStealSpinBox->SetRenderTransform(GetRenderTransform().ToSlateRenderTransform());
	}

	MyStealSpinBox->SetRenderTransformPivot(GetRenderTransformPivot());

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

	MyStealSpinBox->SetDelta(GetDelta());
	MyStealSpinBox->SetSliderExponent(GetSliderExponent());
	MyStealSpinBox->SetMinDesiredWidth(GetMinDesiredWidth());

	MyStealSpinBox->SetForegroundColor(GetForegroundColor());

	MyStealSpinBox->SetMinFractionalDigits(GetMinFractionalDigits());
	MyStealSpinBox->SetMaxFractionalDigits(GetMaxFractionalDigits());
	MyStealSpinBox->SetAlwaysUsesDeltaSnap(GetAlwaysUsesDeltaSnap());

	// Set optional values
#if 0
	bOverride_MinValue ? SetMinValue(GetMinValue()) : ClearMinValue();
	bOverride_MaxValue ? SetMaxValue(GetMaxValue()) : ClearMaxValue();
	bOverride_MinSliderValue ? SetMinSliderValue(GetMinSliderValue()) : ClearMinSliderValue();
	bOverride_MaxSliderValue ? SetMaxSliderValue(GetMaxSliderValue()) : ClearMaxSliderValue();
#endif

	// Always set the value last so that the max/min values are taken into account.
	// TAttribute<float> ValueBinding = PROPERTY_BINDING(float, Value);
	MyStealSpinBox->SetValue(GetValue());

	return MyStealSpinBox;
}

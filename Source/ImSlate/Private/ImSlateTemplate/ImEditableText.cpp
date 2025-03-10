// Copyright ImSlate, Inc. All Rights Reserved.
#include "ImSlateTemplate/ImEditableText.h"

// SImEditableText
int32 SImEditableText::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled)
	const
{
	const FSlateBrush* BrushResource = BorderImage.Get();
	if (BrushResource && BrushResource->DrawAs != ESlateBrushDrawType::NoDrawType)
	{
		FSlateDrawElement::MakeBox(OutDrawElements,
								   LayerId,
								   AllottedGeometry.ToPaintGeometry(),
								   BrushResource,
								   ESlateDrawEffect::None,
								   BrushResource->GetTint(InWidgetStyle) * InWidgetStyle.GetColorAndOpacityTint() /* * BorderBackgroundColor.Get().GetColor(InWidgetStyle)*/);
	}

	return SEditableText::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
}

void SImEditableText::SetBorderImage(const TAttribute<const FSlateBrush*>& InBrushAttribute)
{
	BorderImage.SetImage(*this, InBrushAttribute);
}

namespace ImSlate
{
/**
* Interface for tool tips.
*/
class FDelegateToolTip : public IToolTip
{
public:
	/**
	* Gets the widget that this tool tip represents.
	*
	* @return The tool tip widget.
	*/
	virtual TSharedRef<class SWidget> AsWidget() override { return GetContentWidget(); }

	/**
	* Gets the tool tip's content widget.
	*
	* @return The content widget.
	*/
	virtual TSharedRef<SWidget> GetContentWidget() override
	{
		if (CachedToolTip.IsValid())
		{
			return CachedToolTip.ToSharedRef();
		}

		UWidget* Widget = ToolTipWidgetDelegate.Execute();
		if (Widget)
		{
			CachedToolTip = Widget->TakeWidget();
			return CachedToolTip.ToSharedRef();
		}

		return SNullWidget::NullWidget;
	}

	/**
	* Sets the tool tip's content widget.
	*
	* @param InContentWidget The new content widget to set.
	*/
	virtual void SetContentWidget(const TSharedRef<SWidget>& InContentWidget) override { CachedToolTip = InContentWidget; }

	/**
	* Checks whether this tool tip has no content to display right now.
	*
	* @return true if the tool tip has no content to display, false otherwise.
	*/
	virtual bool IsEmpty() const override { return !ToolTipWidgetDelegate.IsBound(); }

	/**
	* Checks whether this tool tip can be made interactive by the user (by holding Ctrl).
	*
	* @return true if it is an interactive tool tip, false otherwise.
	*/
	virtual bool IsInteractive() const override { return false; }

	virtual void OnClosed() override
	{
		//TODO Notify interface implementing widget of closure

		CachedToolTip.Reset();
	}

	virtual void OnOpening() override
	{
		//TODO Notify interface implementing widget of opening
	}

public:
	UWidget::FGetWidget ToolTipWidgetDelegate;

private:
	TSharedPtr<SWidget> CachedToolTip;
};
}  // namespace ImSlate

TSharedRef<SEditableText> UImEditableText::ConstructImWidget() const
{
	auto MyStealEditableText = SNew(SImEditableText)
								.Style(&WidgetStyle)
								.MinDesiredWidth(GetMinimumDesiredWidth())
								.IsCaretMovedWhenGainFocus(GetIsCaretMovedWhenGainFocus())
								.SelectAllTextWhenFocused(GetSelectAllTextWhenFocused())
								.RevertTextOnEscape(GetRevertTextOnEscape())
								.ClearKeyboardFocusOnCommit(GetClearKeyboardFocusOnCommit())
								.SelectAllTextOnCommit(GetSelectAllTextOnCommit())
								//.OnTextChanged(BIND_UOBJECT_DELEGATE(FOnTextChanged, HandleOnTextChanged))
								//.OnTextCommitted(BIND_UOBJECT_DELEGATE(FOnTextCommitted, HandleOnTextCommitted))
								.VirtualKeyboardType(EVirtualKeyboardType::AsKeyboardType(KeyboardType.GetValue()))
								.VirtualKeyboardOptions(VirtualKeyboardOptions)
								.VirtualKeyboardTrigger(VirtualKeyboardTrigger)
								.VirtualKeyboardDismissAction(VirtualKeyboardDismissAction)
								.Justification(GetJustification());

	MyStealEditableText->SetBorderImage(&BackgroundImage);

	// #if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	// 	bRoutedSynchronizeProperties = true;
	// #endif

#if WIDGET_INCLUDE_RELFECTION_METADATA
	// We only need to do this once, when the slate widget is created.
	MyStealEditableText->AddMetadata<FReflectionMetaData>(MakeShared<FReflectionMetaData>(GetFName(), GetClass(), (UObject*)this, GetSourceAssetOrClass()));
#endif

	{
		if (bOverride_Cursor /*|| CursorDelegate.IsBound()*/)
		{
			MyStealEditableText->SetCursor(GetCursor());  // PROPERTY_BINDING(EMouseCursor::Type, Cursor));
		}

		//MyStealEditableText->SetEnabled(BITFIELD_PROPERTY_BINDING(bIsEnabled));
		MyStealEditableText->SetVisibility(ConvertVisibility(GetVisibility()));
	}

	MyStealEditableText->SetClipping(GetClipping());

	MyStealEditableText->SetFlowDirectionPreference(GetFlowDirectionPreference());
	MyStealEditableText->ForceVolatile(bIsVolatile);
	MyStealEditableText->SetRenderOpacity(GetRenderOpacity());

	if (GetRenderTransform().IsIdentity())
	{
		MyStealEditableText->SetRenderTransform(TOptional<FSlateRenderTransform>());
	}
	else
	{
		MyStealEditableText->SetRenderTransform(GetRenderTransform().ToSlateRenderTransform());
	}

	MyStealEditableText->SetRenderTransformPivot(GetRenderTransformPivot());

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

	//TAttribute<FText> TextBinding = PROPERTY_BINDING(FText, Text);
	//TAttribute<FText> HintTextBinding = PROPERTY_BINDING(FText, HintText);

	MyStealEditableText->SetText(GetText());
	MyStealEditableText->SetHintText(GetHintText());
	MyStealEditableText->SetIsReadOnly(GetIsReadOnly());
	MyStealEditableText->SetIsPassword(GetIsPassword());
	MyStealEditableText->SetAllowContextMenu(AllowContextMenu);
	MyStealEditableText->SetVirtualKeyboardDismissAction(VirtualKeyboardDismissAction);
	MyStealEditableText->SetJustification(GetJustification());
	// TODO UMG Complete making all properties settable on SEditableText

	((FShapedTextOptions&)ShapedTextOptions).SynchronizeShapedTextProperties(*MyStealEditableText);

	return MyStealEditableText;
}

TSharedRef<SWidget> UImEditableText::RebuildWidget()
{
	MyEditableText = SNew(SImEditableText)
						.Style(&WidgetStyle)
						.MinDesiredWidth(GetMinimumDesiredWidth())
						.IsCaretMovedWhenGainFocus(GetIsCaretMovedWhenGainFocus())
						.SelectAllTextWhenFocused(GetSelectAllTextWhenFocused())
						.RevertTextOnEscape(GetRevertTextOnEscape())
						.ClearKeyboardFocusOnCommit(GetClearKeyboardFocusOnCommit())
						.SelectAllTextOnCommit(GetSelectAllTextOnCommit())
						.OnTextChanged(BIND_UOBJECT_DELEGATE(FOnTextChanged, HandleOnTextChanged))
						.OnTextCommitted(BIND_UOBJECT_DELEGATE(FOnTextCommitted, HandleOnTextCommitted))
						.VirtualKeyboardType(EVirtualKeyboardType::AsKeyboardType(KeyboardType.GetValue()))
						.VirtualKeyboardOptions(VirtualKeyboardOptions)
						.VirtualKeyboardTrigger(VirtualKeyboardTrigger)
						.VirtualKeyboardDismissAction(VirtualKeyboardDismissAction)
						.Justification(GetJustification());

#if WIDGET_INCLUDE_RELFECTION_METADATA
	// We only need to do this once, when the slate widget is created.
	MyEditableText->AddMetadata<FReflectionMetaData>(MakeShared<FReflectionMetaData>(GetFName(), GetClass(), (UObject*)this, GetSourceAssetOrClass()));
#endif

	StaticCastSharedPtr<SImEditableText>(MyEditableText)->SetBorderImage(&BackgroundImage);
	return MyEditableText.ToSharedRef();
}

// Copyright ImSlate, Inc. All Rights Reserved.
#include "ImSlateTemplate/ImEditableText.h"
#include "ImSlateTemplate/ImVirtualKeyboard.h"
#include "SImSlateViewport.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Text/TextLayout.h"
#include "Framework/Text/SlateTextLayout.h"
#include "Widgets/Text/SlateEditableTextLayout.h"
#include "PrivateFieldAccessor.h"

// The caret's pixel position comes from FTextLayout::GetLocationAt (public), but the
// FTextLayout instance lives in FSlateEditableTextLayout::TextLayout which is private.
// GenericStorages' PrivateFieldAccessor exposes it legally (friend injection idiom).
GS_PRIVATEACCESS_MEMBER(FSlateEditableTextLayout, TextLayout, TSharedPtr<FSlateTextLayout>)

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

	int32 MaxLayerId = SEditableText::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);

	// Preview display mode: the engine only draws the native caret when focused
	// (SlateEditableTextLayout.cpp: `if (bHasKeyboardFocus && !bIsReadOnly)`), and we
	// deliberately keep this widget unfocused. So self-draw the caret using the engine's
	// own coordinate API, mirroring FCursorLineHighlighter::OnPaint exactly.
	if (bPreviewDisplayMode && bPreviewCaretVisible && EditableTextLayout)
	{
		const TSharedPtr<FSlateTextLayout>& TextLayout = PrivateAccess::TextLayout(*EditableTextLayout);
		if (TextLayout.IsValid())
		{
			const FTextLocation CaretLoc = EditableTextLayout->GetCursorLocation();
			// GetLocationAt returns a pre-scaled location already including -ScrollOffset.
			const FVector2D Location = TextLayout->GetLocationAt(CaretLoc, /*bPerformInclusiveBoundsCheck*/ false);

			// Horizontal: take the engine-computed X (already includes justify offset & scroll),
			// converted to local space. Vertical: GetLocationAt's Y can land outside the widget
			// geometry, so place the caret vertically centered within the widget's own height
			// (single-line SEditableText renders its text vertically centered too).
			const float Scale = TextLayout->GetScale();
			const float LocalSizeY = (float)AllottedGeometry.GetLocalSize().Y;

			const FSlateFontInfo FontInfo = Font.Get();
			const TSharedRef<FSlateFontMeasure> FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
			const float FontMaxCharHeight = (float)FontMeasure->GetMaxCharacterHeight(FontInfo);
			const float CaretWidth = FMath::Clamp(0.08f * FontMaxCharHeight, 1.f, 2.f);
			const float CaretHeight = FMath::Min(FontMaxCharHeight, LocalSizeY);

			const float CaretX = Location.X / Scale;                          // local X
			const float CaretY = FMath::Max(0.f, (LocalSizeY - CaretHeight) * 0.5f);  // vertically centered
			const FVector2D CaretLocalPos(CaretX, CaretY);

			static FSlateBrush CaretBrush;
			CaretBrush.DrawAs = ESlateBrushDrawType::Image;
			CaretBrush.TintColor = FLinearColor::White;

			FSlateDrawElement::MakeBox(
				OutDrawElements,
				MaxLayerId + 1,
				AllottedGeometry.ToPaintGeometry(FVector2D(CaretWidth, CaretHeight),
					FSlateLayoutTransform(1.f, UE::Slate::CastToVector2f(CaretLocalPos))),
				&CaretBrush,
				ESlateDrawEffect::None,
				FLinearColor::White);

			return MaxLayerId + 1;
		}
	}

	return MaxLayerId;
}

void SImEditableText::SetBorderImage(const TAttribute<const FSlateBrush*>& InBrushAttribute)
{
	BorderImage.SetImage(*this, InBrushAttribute);
}

FReply SImEditableText::OnKeyChar(const FGeometry& MyGeometry, const FCharacterEvent& InCharacterEvent)
{
	// Preview display mode: text is driven by the virtual keyboard's keys, not by this
	// widget. Swallow char input so it never edits the displayed text directly.
	if (bPreviewDisplayMode)
		return FReply::Handled();

	FReply Reply = SEditableText::OnKeyChar(MyGeometry, InCharacterEvent);
	if (auto Keyboard = ImSlate::SImSlateVirtualKeyboard::Get())
	{
		if (Keyboard->IsShowing())
			Keyboard->SyncFromEditor(GetText().ToString());
	}
	return Reply;
}

FReply SImEditableText::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	// Preview display mode: forward Enter/Escape to the keyboard, swallow the rest.
	// Never call SEditableText::OnKeyDown (would edit text / move caret independently).
	if (bPreviewDisplayMode)
	{
		if (auto Keyboard = ImSlate::SImSlateVirtualKeyboard::Get())
		{
			if (Keyboard->IsShowing())
			{
				const FKey Key = InKeyEvent.GetKey();
				if (Key == EKeys::Enter)  { Keyboard->Hide(true);  return FReply::Handled(); }
				if (Key == EKeys::Escape) { Keyboard->Hide(false); return FReply::Handled(); }
			}
		}
		return FReply::Handled();
	}

	if (auto Keyboard = ImSlate::SImSlateVirtualKeyboard::Get())
	{
		if (Keyboard->IsShowing())
		{
			const FKey Key = InKeyEvent.GetKey();
			if (Key == EKeys::Enter)  { Keyboard->Hide(true);  return FReply::Handled(); }
			if (Key == EKeys::Escape) { Keyboard->Hide(false); return FReply::Handled(); }
		}
	}
	FReply Reply = SEditableText::OnKeyDown(MyGeometry, InKeyEvent);
	if (auto Keyboard = ImSlate::SImSlateVirtualKeyboard::Get())
	{
		if (Keyboard->IsShowing())
			Keyboard->SyncFromEditor(GetText().ToString());
	}
	return Reply;
}

SImEditableText::~SImEditableText()
{
	// Keyboard follows this editable's lifecycle: when the bound editable is
	// destroyed, dismiss the keyboard (no-op if it was rebound to another one).
	if (auto Keyboard = ImSlate::SImSlateVirtualKeyboard::Get())
		Keyboard->NotifyOwnerDestroyed(this);
}

FReply SImEditableText::OnFocusReceived(const FGeometry& MyGeometry, const FFocusEvent& InFocusEvent)
{
	// Preview display mode: never grab focus / pop the keyboard / activate IME.
	// (We don't call super, so FSlateEditableTextLayout::HandleFocusReceived — which
	// would EnableTextInputMethodContext / ShowVirtualKeyboard — is never reached.)
	if (bPreviewDisplayMode)
		return FReply::Handled();

	if (ImSlate::SImSlateVirtualKeyboard::ShouldUseVirtualKeyboard())
	{
		if (auto Keyboard = ImSlate::SImSlateVirtualKeyboard::Get())
		{
			auto WeakSelf = TWeakPtr<SImEditableText>(StaticCastSharedRef<SImEditableText>(AsShared()));
			ImSlate::FVirtualKeyboardShowParams Params;
			Params.InitialText = GetText().ToString();
			Params.OnTextChanged = [WeakSelf](const FString& Text) {
				if (auto This = WeakSelf.Pin())
					This->SetText(FText::FromString(Text));
			};
			Params.CommitCallback = [WeakSelf](const FString& Text, ETextCommit::Type Type) {
				if (auto This = WeakSelf.Pin())
				{
					// Always apply Text: on OnEnter it's the edited text; on OnCleared (Esc) it's
					// the original text captured at open time, so this RESTORES the field (the live
					// OnTextChanged sync had been updating it during editing).
					This->SetText(FText::FromString(Text));
					if (This->VKCommitCallback)
						This->VKCommitCallback(Text, Type);
				}
			};
			Params.SuggestionProvider = VKSuggestionProvider;
			Params.Owner = AsShared();
			Keyboard->Show(Params);
			return FReply::Handled();
		}
	}
	return SEditableText::OnFocusReceived(MyGeometry, InFocusEvent);
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

	MyStealEditableText->SetFont(ImSlate::GetImSlateDefaultFont());

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

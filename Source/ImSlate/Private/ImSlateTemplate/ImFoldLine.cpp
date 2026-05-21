// Copyright ImSlate, Inc. All Rights Reserved.
#include "ImSlateTemplate/ImFoldLine.h"

//
#include "SlateCore.h"

#include "Widgets/SInvalidationPanel.h"

void SImFoldLine::SetTextWithFoldIndicator(const FText& InText)
{
	OriginalText = InText;
	FString Arrow = bIsFolded ? TEXT("\x25B6 ") : TEXT("\x25BC ");
	SetText(FText::FromString(Arrow + InText.ToString()));
}

FReply SImFoldLine::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bIsFolded = !bIsFolded;
		SetTextWithFoldIndicator(OriginalText);
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FCursorReply SImFoldLine::OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const
{
	return FCursorReply::Cursor(EMouseCursor::Hand);
}

int32 SImFoldLine::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled)
	const
{
	// Draw header background — subtle bar like ImGui's CollapsingHeader
	static FSlateBrush HeaderBrush;
	HeaderBrush.DrawAs = ESlateBrushDrawType::RoundedBox;
	HeaderBrush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
	HeaderBrush.OutlineSettings.CornerRadii = FVector4(3.f, 3.f, 3.f, 3.f);

	FLinearColor BgColor = bIsFolded
		? FLinearColor(0.188f, 0.188f, 0.188f, 0.6f)   // folded: darker
		: FLinearColor(0.25f, 0.25f, 0.25f, 0.6f);      // expanded: slightly lighter

	FSlateDrawElement::MakeBox(
		OutDrawElements,
		LayerId,
		AllottedGeometry.ToPaintGeometry(),
		&HeaderBrush,
		ESlateDrawEffect::None,
		BgColor);

	return STextBlock::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId + 1, InWidgetStyle, bParentEnabled);
}

TSharedRef<SImFoldLine> UImFoldLine::ConstructImWidget() const
{
	auto MyStealTextBlock = SNew(SImFoldLine)
							.SimpleTextMode(bSimpleTextMode);

	// UWidget

	// #if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	// 	bRoutedSynchronizeProperties = true;
	// #endif

#if WIDGET_INCLUDE_RELFECTION_METADATA
	// We only need to do this once, when the slate widget is created.
	MyStealTextBlock->AddMetadata<FReflectionMetaData>(MakeShared<FReflectionMetaData>(GetFName(), GetClass(), (UObject*)this, GetSourceAssetOrClass()));
#endif

	{
		if (bOverride_Cursor /*|| CursorDelegate.IsBound()*/)
		{
			MyStealTextBlock->SetCursor(GetCursor());  // PROPERTY_BINDING(EMouseCursor::Type, Cursor));
		}

		//MyStealEditableText->SetEnabled(BITFIELD_PROPERTY_BINDING(bIsEnabled));
		MyStealTextBlock->SetVisibility(ConvertVisibility(GetVisibility()));
	}

	MyStealTextBlock->SetClipping(GetClipping());

	MyStealTextBlock->SetFlowDirectionPreference(GetFlowDirectionPreference());
	MyStealTextBlock->ForceVolatile(bIsVolatile);
	MyStealTextBlock->SetRenderOpacity(GetRenderOpacity());

	if (GetRenderTransform().IsIdentity())
	{
		MyStealTextBlock->SetRenderTransform(TOptional<FSlateRenderTransform>());
	}
	else
	{
		MyStealTextBlock->SetRenderTransform(GetRenderTransform().ToSlateRenderTransform());
	}

	MyStealTextBlock->SetRenderTransformPivot(GetRenderTransformPivot());

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

	// UWidget

	// MyStealTextBlock->SetText(TextBinding);
	MyStealTextBlock->SetFont(GetFont());
	MyStealTextBlock->SetStrikeBrush(&GetStrikeBrush());
	MyStealTextBlock->SetColorAndOpacity(GetColorAndOpacity());
	MyStealTextBlock->SetShadowOffset(GetShadowOffset());
	MyStealTextBlock->SetShadowColorAndOpacity(GetShadowColorAndOpacity());
	MyStealTextBlock->SetMinDesiredWidth(GetMinDesiredWidth());
	MyStealTextBlock->SetTransformPolicy(GetTextTransformPolicy());
	MyStealTextBlock->BorderImage.SetImage(MyStealTextBlock.Get(), &Background);

	const_cast<UImFoldLine*>(this)->Super::SynchronizeTextLayoutProperties(*MyStealTextBlock);

	// Ensure hit-testable for click-to-fold interaction
	MyStealTextBlock->SetVisibility(EVisibility::Visible);

	return MyStealTextBlock;
}

TSharedRef<SWidget> UImFoldLine::RebuildWidget()
{
	auto WidgetRef = StaticCastSharedRef<SImFoldLine>(Super::RebuildWidget());
#if WIDGET_INCLUDE_RELFECTION_METADATA
	// We only need to do this once, when the slate widget is created.
	WidgetRef->AddMetadata<FReflectionMetaData>(MakeShared<FReflectionMetaData>(GetFName(), GetClass(), (UObject*)this, GetSourceAssetOrClass()));
#endif

	WidgetRef->BorderImage.SetImage(WidgetRef.Get(), &Background);
	return WidgetRef;
}

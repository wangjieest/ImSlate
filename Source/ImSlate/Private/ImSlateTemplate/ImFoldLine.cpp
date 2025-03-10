// Copyright ImSlate, Inc. All Rights Reserved.
#include "ImSlateTemplate/ImFoldLine.h"

//
#include "SlateCore.h"

#include "Widgets/SInvalidationPanel.h"

int32 SImFoldLine::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled)
	const
{
#if 0
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
#endif
	return STextBlock::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
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

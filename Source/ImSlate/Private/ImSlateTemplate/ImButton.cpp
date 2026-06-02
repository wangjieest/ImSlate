// Copyright ImSlate, Inc. All Rights Reserved.
#include "ImSlateTemplate/ImButton.h"

//
#include "Components/ButtonSlot.h"
#include "Widgets/Input/SButton.h"
#include "Blueprint/DragDropOperation.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Framework/Application/SlateApplication.h"
#include "ImSlateTemplate/ImMultiStateButton.h"
#include "Rendering/DrawElements.h"
#include "Slate/UMGDragDropOp.h"
#include "Widgets/Layout/SDPIScaler.h"
#include "Widgets/Text/STextBlock.h"
#include "PrivateFieldAccessor.h"

#if ENGINE_MAJOR_VERSION >= 5
namespace XButtonNS
{
GS_PRIVATEACCESS_MEMBER(SButton, Style, const FButtonStyle*);
}
#endif

static FName SButtonTypeName("SImButton");

SImButton::SImButton()
{
}

/**
 * Construct this widget
 *
 * @param	InArgs	The declaration data for this widget
 */
void SImButton::Construct(const FArguments& InArgs, UImMultiStateButton* InButtonObject)
{
	SButton::Construct(InArgs);

	// Only do this if we're exactly an SXButton
	if (GetType() == SButtonTypeName)
	{
		SetCanTick(false);
	}

	ButtonObject = InButtonObject;

	// InButtonObject may be null for lightweight uses (e.g. a fold header that only needs the
	// base button behaviour + SetReleaseCaptureOnDragScroll). Only pull the extra style when present.
	if (InButtonObject)
		SetButtonExtraStyle(&InButtonObject->ExtraStyle);
}

void SImButton::SetButtonExtraStyle(const FImButtonExtraStyle* ButtonStyle)
{
	FocusedImage = &ButtonStyle->Focused;
	DraggedImage = &ButtonStyle->Dragged;

	Invalidate(EInvalidateWidget::Layout);
}

bool SImButton::IsFocused() const
{
	return HasKeyboardFocus();
}

bool SImButton::IsDragging() const
{
	return bInDragDrop;
}

void SImButton::EnterCustomState(FImCustomWidgetState* State)
{
	if (!State)
	{
		return;
	}
	CustomState = *State;
	bUseCustomState = true;
}

void SImButton::QuitCustomState()
{
	bUseCustomState = false;
}


FReply SImButton::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	bPressActive = false;
	if (IsDragging())
	{
		Drop();
	}
	return SButton::OnMouseButtonUp(MyGeometry, MouseEvent);
}

void SImButton::OnDropOperation()
{
	if (IsDragging())
	{
		Drop();
	}
}

void SImButton::Drop()
{
	bInDragDrop = false;
	if (ButtonObject.IsValid())
		ButtonObject->OnDragEnd.Broadcast();
}

FReply SImButton::OnDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	// No backing UObject (e.g. fold header) → no UMG-style drag-drop; nothing to do.
	if (!ButtonObject.IsValid())
		return FReply::Unhandled();
	ButtonObject->OnDragQuery.Broadcast(MyGeometry, MouseEvent);
	if (IsValid(ButtonObject->DragDropOp))
	{
		bInDragDrop = true;
		ButtonObject->OnDragBegin.Broadcast();
		FVector2D ScreenCursorPos = MouseEvent.GetScreenSpacePosition();
		FVector2D ScreenDrageePosition = (FVector2D)MyGeometry.AbsolutePosition;
		float DPIScale = UWidgetLayoutLibrary::GetViewportScale(ButtonObject.Get());
		FOnButtonDropOperation OnDrop;
		OnDrop.BindSP(SharedThis(this), &SImButton::OnDropOperation);
		TSharedRef<FImButtonDragDropOp> Op = FImButtonDragDropOp::New(ButtonObject->DragDropOp, MouseEvent.GetPointerIndex(), ScreenCursorPos, ScreenDrageePosition, DPIScale, ButtonObject->GetWorld(), OnDrop);
		return FReply::Handled().BeginDragDrop(Op);
	}
	return FReply::Unhandled();
}

FReply SImButton::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (bReleaseCaptureOnDragScroll)
	{
		bPressActive = true;
		PressScreenPos = MouseEvent.GetScreenSpacePosition();
	}
	return SButton::OnMouseButtonDown(MyGeometry, MouseEvent);
}

FReply SImButton::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	// List-row buttons (fold headers): once a press becomes a vertical drag, release mouse capture
	// and stop consuming the event so the parent panel's scroll/pan takes over (drag-to-scroll).
	if (bReleaseCaptureOnDragScroll && bPressActive && HasMouseCapture())
	{
		const float Moved = (MouseEvent.GetScreenSpacePosition() - PressScreenPos).Size();
		const float Threshold = 6.f * ImSlate::GetImSlateEffectiveScale();
		if (Moved > Threshold)
		{
			bPressActive = false;
			// Releasing capture + returning Unhandled lets the engine route the ongoing drag to the
			// panel as a scroll/pan gesture. The click is cancelled (no toggle), which is what we
			// want for a drag.
			return FReply::Handled().ReleaseMouseCapture();
		}
	}

	auto Reply = SButton::OnMouseMove(MyGeometry, MouseEvent);
	// pressed & moving
	if (IsPressed())
	{
		if (!IsDragging())
		{
			return OnDragDetected(MyGeometry, MouseEvent);
		}
	}
	return Reply;
}

void SImButton::OnFocusLost(const FFocusEvent& InFocusEvent)
{
	if (ButtonObject.IsValid())
		ButtonObject->OnUnFocused.Broadcast();
	SButton::OnFocusLost(InFocusEvent);
}

FReply SImButton::OnFocusReceived(const FGeometry& MyGeometry, const FFocusEvent& InFocusEvent)
{
	if (ButtonObject.IsValid())
		ButtonObject->OnFocused.Broadcast();
	return SWidget::OnFocusReceived(MyGeometry, InFocusEvent);
}

/** @return An image that represents this button's border*/
const FSlateBrush* SImButton::GetBorder() const
{
	if (bUseCustomState)
	{
		return &CustomState.ActivatedBrush;
	}
#if ENGINE_MAJOR_VERSION >= 5
	auto CurrentStyle = XButtonNS::PrivateAccess::Style(*this);
	if (!GetShowDisabledEffect() && !IsEnabled())
	{
		return &CurrentStyle->Disabled;
	}
	else if (IsDragging() && DraggedImage)
	{
		return DraggedImage;
	}
	else if (IsPressed())
	{
		return &CurrentStyle->Pressed;
	}
	else if (IsHovered())
	{
		return &CurrentStyle->Hovered;
	}
	else if (IsFocused() && FocusedImage)
	{
		return FocusedImage;
	}
	else
	{
		return &CurrentStyle->Normal;
	}
#else
	if (!GetShowDisabledEffect() && !IsEnabled())
	{
		return DisabledImage;
	}
	else if (IsDragging())
	{
		return DraggedImage;
	}
	else if (IsPressed())
	{
		return PressedImage;
	}
	else if (IsHovered())
	{
		return HoverImage;
	}
	else if (IsFocused())
	{
		return FocusedImage;
	}
	else
	{
		return NormalImage;
	}
#endif
}

TSharedRef<SButton> UImButton::ConstructImWidget() const
{
	TSharedPtr<SButton> MyStealButton = SNew(SButton)
										//.OnClicked(BIND_UOBJECT_DELEGATE(FOnClicked, SlateHandleClicked))
										//.OnPressed(BIND_UOBJECT_DELEGATE(FSimpleDelegate, SlateHandlePressed))
										//.OnReleased(BIND_UOBJECT_DELEGATE(FSimpleDelegate, SlateHandleReleased))
										//.OnHovered_UObject(this, &ThisClass::SlateHandleHovered)
										//.OnUnhovered_UObject(this, &ThisClass::SlateHandleUnhovered)
										.ButtonStyle(&GetStyle())
										.ClickMethod(GetClickMethod())
										.TouchMethod(GetTouchMethod())
										.PressMethod(GetPressMethod())
										.IsFocusable(GetIsFocusable());

	if (GetChildrenCount() > 0)
	{
		Cast<UButtonSlot>(GetContentSlot())->BuildSlot(MyStealButton.ToSharedRef());
	}

	// UWidget

	// #if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	// 	bRoutedSynchronizeProperties = true;
	// #endif

#if WIDGET_INCLUDE_RELFECTION_METADATA
	// We only need to do this once, when the slate widget is created.
	MyStealButton->AddMetadata<FReflectionMetaData>(MakeShared<FReflectionMetaData>(GetFName(), GetClass(), (UObject*)this, GetSourceAssetOrClass()));
#endif

	{
		if (bOverride_Cursor /*|| CursorDelegate.IsBound()*/)
		{
			MyStealButton->SetCursor(GetCursor());  // PROPERTY_BINDING(EMouseCursor::Type, Cursor));
		}

		//MyStealButton->SetEnabled(BITFIELD_PROPERTY_BINDING(bIsEnabled));
		MyStealButton->SetVisibility(ConvertVisibility(GetVisibility()));
	}

	MyStealButton->SetClipping(GetClipping());

	MyStealButton->SetFlowDirectionPreference(GetFlowDirectionPreference());
	MyStealButton->ForceVolatile(bIsVolatile);
	MyStealButton->SetRenderOpacity(GetRenderOpacity());

	if (GetRenderTransform().IsIdentity())
	{
		MyStealButton->SetRenderTransform(TOptional<FSlateRenderTransform>());
	}
	else
	{
		MyStealButton->SetRenderTransform(GetRenderTransform().ToSlateRenderTransform());
	}

	MyStealButton->SetRenderTransformPivot(GetRenderTransformPivot());

	// 	if (ToolTipWidgetDelegate.IsBound() && !IsDesignTime())
	// 	{
	// 		TSharedRef<ImSlate::FDelegateToolTip> ToolTip = MakeShared<ImSlate::FDelegateToolTip>();
	// 		ToolTip->ToolTipWidgetDelegate = ToolTipWidgetDelegate;
	// 		MyStealEditableText->SetToolTip(ToolTip);
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
	// 		MyStealEditableText->SetToolTip(ToolTip);
	// 	}
	// 	else if (!ToolTipText.IsEmpty() || ToolTipTextDelegate.IsBound())
	// 	{
	// 		MyStealEditableText->SetToolTipText(PROPERTY_BINDING(FText, ToolTipText));
	// 	}

	// UWidget End

	MyStealButton->SetColorAndOpacity(GetColorAndOpacity());
	MyStealButton->SetBorderBackgroundColor(GetBackgroundColor());

	return MyStealButton.ToSharedRef();
}

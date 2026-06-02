// Copyright ImSlate, Inc. All Rights Reserved.
#pragma once

#include "Components/Button.h"
#include "ImSlateFactory.h"
#include "ImMultiStateButton.h"

//
#include "ImButton.generated.h"


/**
 * Slate's Buttons are clickable Widgets that can contain arbitrary widgets as its Content().
 */
class SImButton : public SButton
{
public:
	using FArguments = SButton::FArguments;

	SImButton();

	/** @return An image that represents this button's border*/
	// GetBorder() no longer virtual in UE 5.7, remove override
	virtual const FSlateBrush* GetBorder() const;

	/**
	 * Construct this widget
	 *
	 * @param	InArgs	The declaration data for this widget
	 */
	void Construct(const FArguments& InArgs, UImMultiStateButton* InButtonObject);

	void SetButtonExtraStyle(const FImButtonExtraStyle* ButtonStyle);

	virtual bool IsFocused() const;

	virtual bool IsDragging() const;

	void EnterCustomState(FImCustomWidgetState* State);

	void QuitCustomState();

	// When enabled, a press that turns into a vertical drag releases mouse capture and lets the
	// event fall through to the parent panel's scroll/pan, instead of the button swallowing it.
	// Used by list-row style buttons (e.g. fold headers) so dragging up/down over them scrolls the
	// list. Off by default (normal buttons keep capturing).
	void SetReleaseCaptureOnDragScroll(bool bEnable) { bReleaseCaptureOnDragScroll = bEnable; }

public:
	FReply OnFocusReceived(const FGeometry& MyGeometry, const FFocusEvent& InFocusEvent) override;
	void OnFocusLost(const FFocusEvent& InFocusEvent) override;
	FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	FReply OnDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	void OnDropOperation();

protected:
	
	const FSlateBrush* FocusedImage = nullptr;

	const FSlateBrush* DraggedImage = nullptr;

	bool bUseCustomState = false;
	FImCustomWidgetState CustomState;

	bool bInDragDrop = false;
	virtual void Drop();

	bool bReleaseCaptureOnDragScroll = false;  // see SetReleaseCaptureOnDragScroll
	FVector2D PressScreenPos = FVector2D::ZeroVector;
	bool bPressActive = false;

protected:

	TWeakObjectPtr<UImMultiStateButton> ButtonObject;
};

UCLASS(BlueprintType)
class IMSLATE_API UImButton
	: public UButton
	, public TImFactory<SButton>
{
	GENERATED_BODY()
public:
	TSharedRef<SButton> ConstructImWidget() const;

protected:
	IM_SLATE_PALETTECATEGORY()
};

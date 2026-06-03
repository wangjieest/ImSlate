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

	// Drag-to-scroll: when set, a vertical drag over this button (while it holds capture) is forwarded
	// to this handler as a per-move Y delta — instead of releasing capture. Used by fold headers to pan
	// the content panel. The handler typically calls SImSlateWindow::ScrollContentBy.
	// Handler returns the FReply to propagate: Handled() to keep scrolling (button keeps capture), or
	// Handled().BeginDragDrop(...) to hand the gesture to a window drag-drop (move-window). Unhandled to decline.
	void SetDragScrollHandler(TFunction<FReply(FVector2D, FVector2D)> InHandler) { DragScrollHandler = MoveTemp(InHandler); }
	void SetDragScrollEndHandler(TFunction<void(FVector2D)> InHandler) { DragScrollEndHandler = MoveTemp(InHandler); }
	// Down-detect for move-window: on press, if content can't scroll, arm a DetectDrag on this target
	// (the owning window) so a drag becomes a stable down-detected window move (= host switch), like the
	// titlebar — instead of an unstable move-time DetectDrag. CanScroll decides scroll-vs-move at press.
	void SetDragDetectTarget(TSharedRef<SWidget> InTarget) { DragDetectTarget = InTarget; }
	void SetCanWindowScroll(TFunction<bool()> InFn) { CanWindowScroll = MoveTemp(InFn); }

public:
	FReply OnFocusReceived(const FGeometry& MyGeometry, const FFocusEvent& InFocusEvent) override;
	void OnFocusLost(const FFocusEvent& InFocusEvent) override;
	FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	FReply OnDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	FReply OnTouchStarted(const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent) override;
	FReply OnTouchMoved(const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent) override;
	FReply OnTouchEnded(const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent) override;
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

	TFunction<FReply(FVector2D, FVector2D)> DragScrollHandler;  // forwarded drag: (press pos, current pos) → FReply
	TFunction<void(FVector2D)> DragScrollEndHandler;           // drag released at (current pos)
	TWeakPtr<SWidget> DragDetectTarget;                        // window to DetectDrag for move-window
	TFunction<bool()> CanWindowScroll;                        // press-time scroll-vs-move decision
	bool bDragScrolling = false;                    // this press has crossed the drag threshold
	FVector2D LastDragPos = FVector2D::ZeroVector;  // last pointer pos, for per-move delta

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

// Copyright ImSlate, Inc. All Rights Reserved.
#pragma once

#include "Components/Button.h"
#include "ImSlateFactory.h"
#include "ImMultiStateButton.h"

//
#include "ImButton.generated.h"


// A pluggable press/drag behaviour for SImButton. The button itself knows nothing about scrolling,
// windows or folds — it only detects "this press became a drag" and forwards it here. Whoever installs
// the behaviour (e.g. a fold header) decides what a drag means (scroll the panel, move the window, ...).
//
// Press routing:
//   - ShouldBubbleOnPress() returning true makes OnMouseButtonDown return Unhandled (DON'T capture), so
//     the press bubbles to an ancestor (e.g. the window does DetectDrag(self) → move). A tap is then
//     turned back into a click in OnMouseButtonUp. Returning false keeps the normal SButton capture.
// Drag:
//   - OnDragMove(press, cur) is called once the pointer crosses the drag threshold (per move). Return
//     Handled() to keep owning the drag (button keeps capture & keeps forwarding), or Unhandled() to
//     decline (the press falls back to bubbling / the click is left to fire).
//   - OnDragEnd(cur) is called when a drag releases.
struct FImMousePressBehavior
{
	TFunction<bool()> ShouldBubbleOnPress;                       // true → don't capture on press, bubble up
	TFunction<FReply(FVector2D Press, FVector2D Cur)> OnDragMove;  // a past-threshold drag move
	TFunction<void(FVector2D Cur)> OnDragEnd;                    // drag released

	bool IsSet() const { return (bool)OnDragMove; }
};

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

	// Install a press/drag behaviour (see FImMousePressBehavior). When set, this button forwards drags to
	// it (used by fold headers to scroll the panel / move the window). When unset, the button is a plain
	// SButton. Off by default (normal buttons just click).
	void SetMousePressBehavior(FImMousePressBehavior InBehavior) { PressBehavior = MoveTemp(InBehavior); }

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
	// Capture-loss safety net for drag-scroll: when the pointer is dragged outside the panel/window and
	// released there, the up may never reach this button, so OnMouseButtonUp/OnTouchEnded don't run and
	// the drag-scroll never ends. The engine ALWAYS fires OnMouseCaptureLost when capture goes away for
	// any reason → finish the pan here (OnDragEnd + clear state) so nothing leaks.
	void OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override;
	void OnDropOperation();

protected:
	
	const FSlateBrush* FocusedImage = nullptr;

	const FSlateBrush* DraggedImage = nullptr;

	bool bUseCustomState = false;
	FImCustomWidgetState CustomState;

	bool bInDragDrop = false;
	virtual void Drop();

	FImMousePressBehavior PressBehavior;            // pluggable drag behaviour; see SetMousePressBehavior
	FVector2D PressScreenPos = FVector2D::ZeroVector;
	bool bPressActive = false;
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

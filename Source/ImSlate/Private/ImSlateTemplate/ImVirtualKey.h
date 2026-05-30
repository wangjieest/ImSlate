// Copyright ImSlate, Inc. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SCompoundWidget.h"
#include "ImSlateFactory.h"
#include "ImSlateTemplate/ImVirtualKeyboard.h"

namespace ImSlate
{

DECLARE_DELEGATE_TwoParams(FOnVirtualKeyPressed, const FVirtualKeyDef& /*KeyDef*/, const FString& /*InputValue*/);
DECLARE_DELEGATE_OneParam(FOnVirtualKeyAction, EVirtualKeyAction /*Action*/);
DECLARE_DELEGATE_TwoParams(FOnVirtualKeyLongPress, const FVirtualKeyDef& /*KeyDef*/, const FGeometry& /*KeyGeometry*/);
DECLARE_DELEGATE_OneParam(FOnVirtualKeyLongPressMove, int32 /*HighlightIndex*/);
DECLARE_DELEGATE_OneParam(FOnVirtualKeyLongPressEnd, int32 /*SelectedIndex*/);
DECLARE_DELEGATE_TwoParams(FOnVirtualKeyPressVisual, const FVirtualKeyDef& /*KeyDef*/, const FGeometry& /*KeyGeometry*/);
DECLARE_DELEGATE_TwoParams(FOnVirtualKeyMoveVisual, const FVector2D& /*Delta from press*/, bool /*bSwipeReady*/);
DECLARE_DELEGATE(FOnVirtualKeyReleaseVisual);

// -1 = auto-scroll left, 0 = stop, 1 = auto-scroll right
DECLARE_DELEGATE_OneParam(FOnSpaceCursorZone, int32 /*Direction*/);

class SImSlateKey : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SImSlateKey)
		: _KeyDef()
		, _bShiftActive(false)
		, _bShiftSingleShot(false)
	{}
		SLATE_ARGUMENT(const FVirtualKeyDef*, KeyDef)
		SLATE_ATTRIBUTE(bool, bShiftActive)
		SLATE_ATTRIBUTE(bool, bShiftSingleShot)
		SLATE_EVENT(FOnVirtualKeyPressed, OnKeyInput)
		SLATE_EVENT(FOnVirtualKeyAction, OnKeyAction)
		SLATE_EVENT(FOnVirtualKeyLongPress, OnLongPress)
		SLATE_EVENT(FOnVirtualKeyLongPressMove, OnLongPressMove)
		SLATE_EVENT(FOnVirtualKeyLongPressEnd, OnLongPressEnd)
		SLATE_EVENT(FOnVirtualKeyPressVisual, OnPressVisual)
		SLATE_EVENT(FOnVirtualKeyMoveVisual, OnMoveVisual)
		SLATE_EVENT(FOnVirtualKeyReleaseVisual, OnReleaseVisual)
		SLATE_EVENT(FOnSpaceCursorZone, OnSpaceCursorZone)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	void UpdateKeyDef(const FVirtualKeyDef* InKeyDef);
	void SetLongPressPopupInfo(float PopupCenterAbsX, float CellWidth, int32 CharCount);
	void SetKeyboardWidthGetter(TAttribute<float> InGetter) { KeyboardWidthGetter = MoveTemp(InGetter); }

	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

	virtual FReply OnTouchStarted(const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent) override;
	virtual FReply OnTouchMoved(const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent) override;
	virtual FReply OnTouchEnded(const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent) override;

private:
	const FVirtualKeyDef* KeyDef = nullptr;
	TAttribute<bool> bShiftActive;
	TAttribute<bool> bShiftSingleShot;
	FOnVirtualKeyPressed OnKeyInput;
	FOnVirtualKeyAction OnKeyAction;
	FOnVirtualKeyLongPress OnLongPress;
	FOnVirtualKeyLongPressMove OnLongPressMove;
	FOnVirtualKeyLongPressEnd OnLongPressEnd;
	FOnVirtualKeyPressVisual OnPressVisual;
	FOnVirtualKeyMoveVisual OnMoveVisual;
	FOnVirtualKeyReleaseVisual OnReleaseVisual;
	FOnSpaceCursorZone OnSpaceCursorZone;
	int32 LastCursorZone = 0;
	bool bWasInOuterZone = false;
	TAttribute<float> KeyboardWidthGetter;

	FVector2D LongPressAnchorPos = FVector2D::ZeroVector;
	float LongPressCellWidth = 0.f;
	int32 LongPressCharCount = 0;

	enum class ESwipeDirection { None, Up, Down, Left, Right };

	bool bIsPressed = false;
	bool bSwipeDetected = false;
	bool bSwipeActive = false;
	bool bLongPressHandled = false;
	bool bSwipeVisualShown = false;
	ESwipeDirection ActiveSwipeDir = ESwipeDirection::None;
	FVector2D PressStartPos = FVector2D::ZeroVector;
	double PressStartTime = 0.0;

	static constexpr float LongPressThreshold = 0.7f;

	void HandlePress(const FGeometry& MyGeometry, const FVector2D& ScreenPos);
	void HandleRelease(const FGeometry& MyGeometry, const FVector2D& ScreenPos);
	void HandleMove(const FGeometry& MyGeometry, const FVector2D& ScreenPos);

	void FireInput(const FString& InputValue);
	void FireAction(EVirtualKeyAction Action);
	ESwipeDirection DetectSwipe(const FVector2D& Delta) const;
};

class SImSlateKeyPopup : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SImSlateKeyPopup) {}
		SLATE_ARGUMENT(TArray<FString>, Chars)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	void SetHighlightIndex(int32 Index);
	int32 GetHighlightIndex() const { return HighlightIndex; }
	int32 GetCharCount() const { return Chars.Num(); }
	FString GetCharAt(int32 Index) const { return Chars.IsValidIndex(Index) ? Chars[Index] : FString(); }
	float GetCellWidth() const { return CellWidth; }

private:
	TArray<FString> Chars;
	int32 HighlightIndex = -1;
	float CellWidth = 0.f;
	TArray<TSharedPtr<SBorder>> CellBorders;
};

// Draggable cursor slider — hold and drag left/right to move text cursor
class SImSlateCursorSlider : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SImSlateCursorSlider) {}
		SLATE_EVENT(FOnVirtualKeyAction, OnCursorMove)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual FVector2D ComputeDesiredSize(float) const override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnTouchStarted(const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent) override;
	virtual FReply OnTouchMoved(const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent) override;
	virtual FReply OnTouchEnded(const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent) override;

private:
	FOnVirtualKeyAction OnCursorMove;
	bool bDragging = false;
	float DragAccumulator = 0.f;
	FVector2D LastDragPos = FVector2D::ZeroVector;
	float StepThreshold = 0.f;
};

}  // namespace ImSlate

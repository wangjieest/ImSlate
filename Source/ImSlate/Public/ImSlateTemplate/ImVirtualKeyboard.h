// Copyright ImSlate, Inc. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

#include "ImVirtualKeyboard.generated.h"

namespace ImSlate
{

enum class EVirtualKeyAction : uint8
{
	Char,
	Backspace,
	Shift,
	CapsLock,
	Enter,
	Cancel,
	ToggleLayer,
	Space,
	Left,
	Right,
	UndoBackspace,
	ToggleType,
};

struct FVirtualKeySwipeEntry
{
	FString Label;
	FSimpleDelegate Callback;

	bool IsSet() const { return !Label.IsEmpty(); }
};

struct FVirtualKeySwipe
{
	FVirtualKeySwipeEntry Up;
	FVirtualKeySwipeEntry Down;
	FVirtualKeySwipeEntry Left;
	FVirtualKeySwipeEntry Right;

	bool HasAny() const { return Up.IsSet() || Down.IsSet() || Left.IsSet() || Right.IsSet(); }
};

struct FVirtualKeyDef
{
	FString Label;
	FString ShiftLabel;
	FString Value;
	FString ShiftValue;
	FVirtualKeySwipe Swipe;
	TArray<FString> LongPressChars;
	EVirtualKeyAction Action = EVirtualKeyAction::Char;
	float WidthMultiplier = 1.f;

	FString GetDisplayLabel(bool bShift) const
	{
		if (bShift && !ShiftLabel.IsEmpty()) return ShiftLabel;
		return Label;
	}
	FString GetInputValue(bool bShift) const
	{
		if (bShift && !ShiftValue.IsEmpty()) return ShiftValue;
		return Value;
	}
};

using FImSlateSuggestionProvider = TFunction<void(const FString& Input, TArray<FString>& OutSuggestions)>;

struct FVirtualKeyboardShowParams
{
	FString InitialText;
	int32 CursorPosition = INDEX_NONE;
	TFunction<void(const FString&, ETextCommit::Type)> CommitCallback;
	TFunction<void(const FString&)> OnTextChanged;  // real-time sync on every keystroke
	FImSlateSuggestionProvider SuggestionProvider;
};

class IMSLATE_API SImSlateVirtualKeyboard : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SImSlateVirtualKeyboard) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override { return FReply::Handled(); }
	virtual FReply OnTouchStarted(const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent) override;
	virtual FReply OnTouchMoved(const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent) override;
	virtual FReply OnTouchEnded(const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent) override;

	void Show(const FVirtualKeyboardShowParams& Params);
	void Hide(bool bCommit);
	bool IsShowing() const { return bVisible; }
	void UpdateSuggestionsAsync(TArray<FString> InResults);

	static TSharedPtr<SImSlateVirtualKeyboard> Get();
	static bool ShouldUseVirtualKeyboard();

private:
	enum class EKeyboardType { QWERTY, T9 };
	enum class EKeyboardLayoutMode { FullScreen, Split };
	enum class EShiftState : uint8 { Default, SingleShot, Locked };

	bool bVisible = false;
	EShiftState ShiftState = EShiftState::Default;
	int32 CurrentLayer = 0;
	EKeyboardType KeyboardType = EKeyboardType::QWERTY;
	FString CurrentText;
	int32 CursorPosition = 0;
	FString OriginalText;
	FString BackspaceUndoBuffer;

	TFunction<void(const FString&, ETextCommit::Type)> CommitCallback;
	TFunction<void(const FString&)> OnTextChanged;
	FImSlateSuggestionProvider SuggestionProvider;

	TSharedPtr<class STextBlock> PreviewText;
	TSharedPtr<class SBorder> PreviewBorder;
	TSharedPtr<FActiveTimerHandle> CursorBlinkTimer;
	bool bCursorVisible = true;
	bool bPreviewDragging = false;
	FVector2D PreviewDragLastPos = FVector2D::ZeroVector;
	float PreviewDragAccum = 0.f;
	TSharedPtr<class SWrapBox> SuggestionBar;
	TSharedPtr<class SVerticalBox> KeyboardGrid;
	TSharedPtr<class SOverlay> RootOverlay;
	TSharedPtr<class SImSlateKeyPopup> ActivePopup;
	TSharedPtr<class SWidget> SwipeVisual;
	TMap<FString, TSharedPtr<class STextBlock>> SwipeDirectionTexts;
	TWeakPtr<class SImSlateKey> ActiveLongPressKey;

	struct FKeyWidgetEntry
	{
		TSharedPtr<class SImSlateKey> Widget;
		const FVirtualKeyDef* Def = nullptr;
	};
	TArray<FKeyWidgetEntry> KeyWidgets;
	TArray<FVirtualKeyDef> PersistentKeyDefs;

	EKeyboardLayoutMode GetLayoutMode() const;

	void BuildKeyboard();
	void BuildFullScreenLayout();
	void BuildSplitLayout();
	void BuildKeyRow(TSharedRef<class SHorizontalBox> Row, const TArray<FVirtualKeyDef>& Keys);
	void UpdatePreview();
	void UpdateSuggestions();
	void PopulateSuggestionBar(const TArray<FString>& Suggestions);

	void OnKeyInput(const FVirtualKeyDef& KeyDef, const FString& InputValue);
	void OnKeyAction(EVirtualKeyAction Action);
	void OnKeyLongPress(const FVirtualKeyDef& KeyDef, const FGeometry& KeyGeometry);
	void OnKeyLongPressMove(int32 HighlightIndex);
	void OnKeyLongPressEnd(int32 SelectedIndex);
	void OnKeyPressVisual(const FVirtualKeyDef& KeyDef, const FGeometry& KeyGeometry);
	void OnKeyMoveVisual(const FVector2D& Delta, bool bSwipeReady);
	void OnKeyReleaseVisual();
	void OnSpaceCursorZone(int32 Direction);
	float SwipeVisualBaseY = 0.f;
	int32 AutoScrollDirection = 0;
	TSharedPtr<FActiveTimerHandle> AutoScrollTimer;
	TQueue<TArray<FString>> AsyncSuggestionQueue;
	void DismissPopup();
	void OnSuggestionClicked(const FString& Value);

	bool IsUpperCase() const;
	bool IsInPreviewArea(const FVector2D& AbsPos) const;
	void HandlePreviewDrag(const FVector2D& ScreenPos);
	void InsertText(const FString& Text);
	void DeleteBackward();
	void MoveCursor(int32 Delta);
	void ToggleShift();

	FVirtualKeyDef MakeBackspaceKey(float Width);
	TArray<TArray<FVirtualKeyDef>> GetQWERTYLayout();
	TArray<TArray<FVirtualKeyDef>> GetSymbolLayout();
	TArray<TArray<FVirtualKeyDef>> GetNumberRow();
	TArray<FVirtualKeyDef> GetBottomRow();
	TArray<TArray<FVirtualKeyDef>> GetT9Layout();
	TArray<FVirtualKeyDef> GetT9BottomRow();

};

}  // namespace ImSlate

UCLASS()
class IMSLATE_API UImVirtualKeyboard : public UObject
{
	GENERATED_BODY()
public:
	static TSharedPtr<ImSlate::SImSlateVirtualKeyboard> GetVirtualKeyboard() { return ImSlate::SImSlateVirtualKeyboard::Get(); }
};

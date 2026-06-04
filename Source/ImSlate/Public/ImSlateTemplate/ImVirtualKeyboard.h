// Copyright ImSlate, Inc. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

#include "ImVirtualKeyboard.generated.h"

// Global forward declaration so `class SConstraintCanvas` inside the ImSlate namespace below
// refers to the engine's ::SConstraintCanvas, not a new (incomplete) ImSlate::SConstraintCanvas.
class SConstraintCanvas;

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
	StepUp,    // numeric keypad: value += step (clamped to [Min,Max])
	StepDown,  // numeric keypad: value -= step
};

// Swipe direction. Public (was SImSlateKey's private nested enum) so the continuous step callback
// below can take a direction parameter.
enum class EImSwipeDir : uint8 { None, Up, Down, Left, Right };

struct FVirtualKeySwipeEntry
{
	FString Label;
	FSimpleDelegate Callback;   // single-shot: fired once on release (swipe-and-let-go)
	bool bStep = false;         // continuous: when true, this direction fires the key-level OnStep
	                            // repeatedly during the drag (one call per StepW of travel).

	bool IsSet() const { return !Label.IsEmpty(); }
};

struct FVirtualKeySwipe
{
	FVirtualKeySwipeEntry Up;
	FVirtualKeySwipeEntry Down;
	FVirtualKeySwipeEntry Left;
	FVirtualKeySwipeEntry Right;

	// Continuous (step-drag) channel: ONE key-level callback shared by all four directions; the
	// direction is passed as the argument. Only directions with bStep=true invoke it. Orthogonal to
	// the per-direction single-shot Callback above — a direction may use either or both.
	TFunction<void(EImSwipeDir)> OnStep;

	bool HasAny() const { return Up.IsSet() || Down.IsSet() || Left.IsSet() || Right.IsSet(); }
	bool HasStep() const { return Up.bStep || Down.bStep || Left.bStep || Right.bStep; }
	const FVirtualKeySwipeEntry& Entry(EImSwipeDir D) const
	{
		switch (D)
		{
		case EImSwipeDir::Up:    return Up;
		case EImSwipeDir::Down:  return Down;
		case EImSwipeDir::Left:  return Left;
		case EImSwipeDir::Right: return Right;
		default:                 return Up;  // None: arbitrary; callers guard on direction
		}
	}
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
	bool bDisabled = false;  // grayed-out, non-interactive (e.g. 8/9 while in OCT base)

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

// On-screen keyboard layout family. Namespace-level (not the private nested enum) so Show params and
// callers can request a layout. QWERTY = letters (+symbol layer), T9 = 9-key, Number = numeric-only
// (0-9, dot, minus) for SpinBox / numeric inputs.
enum class EImKeyboardType : uint8 { QWERTY, T9, Number };

// Type-aware config for the numeric keypad (used when InitialKeyboardType == Number). Lets callers
// shape the keypad to the bound value's type (int hides the dot, unsigned hides minus, hex adds A-F)
// and, SpinBox-style, drive +/- step keys with optional clamp range.
struct FImNumericKeyboardParams
{
	bool bAllowDecimal = true;    // false (integer types) → hide the '.' key
	bool bAllowNegative = true;   // false (unsigned types) → hide the '-' key
	bool bHex = false;            // true → add A-F keys (hexadecimal entry)
	TOptional<double> Min;        // clamp lower bound for step keys (and typed entry)
	TOptional<double> Max;        // clamp upper bound
	TOptional<double> Step;       // when set, the keypad shows +/- step keys (SpinBox-like)
};

struct FVirtualKeyboardShowParams
{
	FString InitialText;
	int32 CursorPosition = INDEX_NONE;
	TFunction<void(const FString&, ETextCommit::Type)> CommitCallback;
	TFunction<void(const FString&)> OnTextChanged;  // real-time sync on every keystroke
	FImSlateSuggestionProvider SuggestionProvider;
	TWeakPtr<class SWidget> Owner;  // bound editable widget; keyboard follows its lifecycle
	bool bBlockBackground = true;   // true: full-screen modal, taps don't pass through to game
	// true: clicking a suggestion replaces the text AND commits+closes the keyboard (Done).
	// false: clicking a suggestion only replaces the text; the keyboard stays open for more edits.
	bool bSuggestionCommitsAndCloses = true;
	// Initial on-screen layout. Number → pops a numeric-only keypad (no letters, ToggleType hidden).
	EImKeyboardType InitialKeyboardType = EImKeyboardType::QWERTY;
	// Numeric keypad shaping (only used when InitialKeyboardType == Number).
	FImNumericKeyboardParams Numeric;

	// Input history. When HistoryKey is non-empty, committed entries are persisted per key (Saved
	// ini) and shown in the suggestion bar: empty input → recent N entries; typed input → matching
	// history first, then provider results (provider results overlapping history are hidden). History
	// rows get an 'X' delete button. HistoryFilter (optional): only entries passing it are recorded.
	FString HistoryKey;
	TFunction<bool(const FString&)> HistoryFilter;
	// Max history entries kept/shown for this key. Default 10. Caps both the stored list and the
	// suggestion rows. (Global ceiling imslate.InputHistoryMax still applies on top.)
	int32 MaxHistory = 10;
};

class IMSLATE_API SImSlateVirtualKeyboard : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SImSlateVirtualKeyboard) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	// Force the keyboard widget to fill the whole viewport so its top-left origin is fixed.
	// Confirmed by logs: without this the widget sized to its content (keys+suggestions), so
	// its kbAbsPos.Y drifted as the suggestion row count changed (338->412->450 high, top
	// moving 728->582->504), which dragged the popups (anchored in this widget's space) along.
	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override { return FReply::Handled(); }
	virtual FReply OnTouchStarted(const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent) override;
	virtual FReply OnTouchMoved(const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent) override;
	virtual FReply OnTouchEnded(const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent) override;

	// Physical keyboard support: the keyboard ROOT takes Slate user focus while shown (set in
	// Tick on the first frame after Show, so the widget is in the tree). Physical key events
	// are routed here and reuse the exact same OnKeyInput/OnKeyAction logic as the on-screen
	// keys, so the preview + bound editable + suggestions stay in sync. The root is a
	// SCompoundWidget (not SEditableText), so holding focus never pops the OS keyboard on mobile.
	virtual bool SupportsKeyboardFocus() const override { return bVisible; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnKeyUp(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnKeyChar(const FGeometry& MyGeometry, const FCharacterEvent& InCharacterEvent) override;
	void Show(const FVirtualKeyboardShowParams& Params);
	void Hide(bool bCommit);
	bool IsShowing() const { return bVisible; }
	void UpdateSuggestionsAsync(TArray<FString> InResults);
	// Sync text AND caret from the bound editable (physical-keyboard input). CaretPos < 0 means
	// "unknown" → keep clamped old position; otherwise move the preview caret to it.
	void SyncFromEditor(const FString& Text, int32 CaretPos = -1);

	// Lifecycle binding: keyboard belongs to the widget that called Show().
	// When that widget is destroyed, the keyboard auto-hides.
	bool IsOwnedBy(const class SWidget* Widget) const;
	void NotifyOwnerDestroyed(const class SWidget* Widget);

	static TSharedPtr<SImSlateVirtualKeyboard> Get();
	// PIE-safe variant: resolves the game viewport keyboard for WorldContext's world instead of the
	// process-global context. Required under "Play As Client", where multiple worlds exist and the
	// global context may point at the server world (which has no game viewport). Falls back to Get().
	static TSharedPtr<SImSlateVirtualKeyboard> Get(const class UObject* WorldContext);
	// Non-creating hide for WorldContext's world: hides an already-open keyboard without lazily
	// creating/attaching one. Safe to call during teardown (EndPlayMap / Deinitialize), where Get()
	// would re-create + attach into an already-invalid viewport overlay and trip an ensure.
	static void HideForWorld(const class UObject* WorldContext);
	// Returns true only when the focused edit lives inside the game viewport (overlay, no OS window/IME).
	// Floated-out windows (host viewport = real SWindow) use the OS keyboard instead.
	static bool ShouldUseVirtualKeyboard(const class SWidget* InWidget = nullptr);

	// Hide any currently-open virtual keyboard WITHOUT creating one (non-creating, unlike Get()).
	// bCommit: true → commit the in-progress text (Hide(true)); false → discard (Hide(false)).
	// Safe to call any time; no-op if none is open. Returns true if a keyboard was open and hidden.
	static bool HideOpenKeyboard(bool bCommit);

	// App deactivate / background: discard + close (a keyboard surviving into resume is a known
	// suspend/resume crash). Thin wrapper over HideOpenKeyboard(false).
	static void HideOpenKeyboardForAppLifecycle() { HideOpenKeyboard(false); }

private:
	// Alias to the namespace-level enum (now includes Number) so existing cpp references to
	// EKeyboardType::QWERTY / ::T9 keep compiling unchanged.
	using EKeyboardType = EImKeyboardType;
	enum class EKeyboardLayoutMode { FullScreen, Split };
	enum class EShiftState : uint8 { Default, SingleShot, Locked };

	bool bVisible = false;
	bool bPendingFocus = false;  // Show() set this; Tick() grabs user focus on the first frame in-tree
	bool bSuggestionCommitsAndCloses = true;  // see FVirtualKeyboardShowParams
	TWeakPtr<class SWidget> BoundOwner;
	EShiftState ShiftState = EShiftState::Default;
	int32 CurrentLayer = 0;
	EKeyboardType KeyboardType = EKeyboardType::QWERTY;
	FImNumericKeyboardParams NumericParams;  // shapes the numeric keypad (set from Show params)
	int32 NumericRadix = 10;                 // integer entry base: 10 / 16 / 2 (radix-toggle key cycles)
	FString HistoryKey;                      // non-empty → input history enabled (set from Show params)
	TFunction<bool(const FString&)> HistoryFilter;  // only entries passing this are recorded
	int32 HistoryMax = 10;                   // per-key cap (from Show params); default 10
	// Layout mode is derived purely from the Tick-provided geometry width (not from
	// toggle history), so the same screen always yields the same mode. We track the
	// width the keyboard was last *built* against to know when a rebuild is needed.
	float BuiltWidth = -1.f;
	EKeyboardLayoutMode BuiltLayoutMode = EKeyboardLayoutMode::FullScreen;
	FString CurrentText;
	int32 CursorPosition = 0;
	FString OriginalText;
	FString BackspaceUndoBuffer;

	TFunction<void(const FString&, ETextCommit::Type)> CommitCallback;
	TFunction<void(const FString&)> OnTextChanged;
	FImSlateSuggestionProvider SuggestionProvider;

	TSharedPtr<class SImEditableText> PreviewEdit;  // preview text + self-drawn caret (engine layout)
	TSharedPtr<class SBorder> PreviewBorder;
	TSharedPtr<class SWidget> BackgroundBlocker;  // full-screen modal hit-blocker
	// Toggle-type + Done keys flanking the preview row (persist across BuildKeyboard).
	TSharedPtr<class SImSlateKey> ToggleTypeKey;
	TSharedPtr<class SImSlateKey> DoneKey;
	TSharedPtr<FVirtualKeyDef> ToggleTypeKeyDef;
	TSharedPtr<FVirtualKeyDef> DoneKeyDef;
	TSharedPtr<FActiveTimerHandle> CursorBlinkTimer;
	bool bCursorVisible = true;
	bool bPreviewDragging = false;
	FVector2D PreviewDragLastPos = FVector2D::ZeroVector;
	float PreviewDragAccum = 0.f;
	TSharedPtr<class SWrapBox> SuggestionBar;
	// Current candidate list + keyboard-navigated selection. SelectedSuggestionIndex == -1 means no
	// selection (Up/Down haven't been used, or input changed). Up/Down move it; Enter on a selected
	// candidate commits THAT word instead of the typed text.
	TArray<FString> CurrentSuggestions;
	int32 SelectedSuggestionIndex = -1;
	int32 SuggestionHistoryCount = 0;  // leading CurrentSuggestions that are history rows (deletable via Ctrl+Backspace)
	void MoveSuggestionSelection(int32 Delta);  // Down=+1 / Up=-1, clamped to [-1, Num-1], no wrap
	TSharedPtr<class SVerticalBox> KeyboardGrid;
	TSharedPtr<class SVerticalBox> PreviewKeysRoot;  // preview + keys unit (suggestions float above it)
	TSharedPtr<SConstraintCanvas> KeyboardCanvas;  // main anchor-based layout (engine global type)
	TSharedPtr<class SOverlay> RootOverlay;  // top-most layer for transient popups
	TSharedPtr<class SImSlateKeyPopup> ActivePopup;
	TSharedPtr<class SWidget> SwipeVisual;
	TSharedPtr<class SImStepRuler> StepRulerH;  // horizontal ticks (value axis, vertical drag)
	TSharedPtr<class SImStepRuler> StepRulerV;  // vertical ticks (cursor axis, horizontal drag)
	TSharedPtr<class SWidget> StepDragVisual;
	TSharedPtr<class STextBlock> StepDragLeftText;
	TSharedPtr<class STextBlock> StepDragRightText;
	const FVirtualKeyDef* ActiveKeyDef = nullptr;
	TMap<FString, TSharedPtr<class STextBlock>> SwipeDirectionTexts;
	TWeakPtr<class SImSlateKey> ActiveLongPressKey;

	struct FKeyWidgetEntry
	{
		TSharedPtr<class SImSlateKey> Widget;
		const FVirtualKeyDef* Def = nullptr;
	};
	TArray<FKeyWidgetEntry> KeyWidgets;
	TArray<FVirtualKeyDef> PersistentKeyDefs;

	EKeyboardLayoutMode ComputeLayoutMode(float Width) const;  // pure: mode from width
	EKeyboardLayoutMode GetLayoutMode() const { return BuiltLayoutMode; }  // last built mode

	void BuildKeyboard();
	void BuildFullScreenLayout();
	void BuildSplitLayout();
	void BuildKeyRow(TSharedRef<class SHorizontalBox> Row, const TArray<FVirtualKeyDef>& Keys);
	TSharedRef<class SImSlateKey> MakeBoundKey(const FVirtualKeyDef* DefPtr);
	void UpdateToggleTypeLabel();
	void UpdateToggleTypeKeyVisibility();  // numeric pad hides the type-toggle key
	void UpdatePreview();
	void UpdateSuggestions();
	// First HistoryCount items render with an 'X' delete button (recent input history); the rest are
	// plain provider suggestions. HistoryCount 0 = the legacy plain bar.
	void PopulateSuggestionBar(const TArray<FString>& Suggestions, int32 HistoryCount = 0);

	void OnKeyInput(const FVirtualKeyDef& KeyDef, const FString& InputValue);
	void OnKeyAction(EVirtualKeyAction Action);
	void OnKeyLongPress(const FVirtualKeyDef& KeyDef, const FGeometry& KeyGeometry);
	void OnKeyLongPressMove(int32 HighlightIndex);
	void OnKeyLongPressEnd(int32 SelectedIndex);
	void OnKeyPressVisual(const FVirtualKeyDef& KeyDef, const FGeometry& KeyGeometry, bool bForceStepDrag);
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
	FVirtualKeyDef MakeSpaceKey(float Width);
	TArray<TArray<FVirtualKeyDef>> GetQWERTYLayout();
	TArray<TArray<FVirtualKeyDef>> GetSymbolLayout();
	TArray<FVirtualKeyDef> GetBottomRow();
	TArray<TArray<FVirtualKeyDef>> GetT9Layout();
	TArray<FVirtualKeyDef> GetT9BottomRow();
	TArray<TArray<FVirtualKeyDef>> GetNumberLayout();  // numeric-only keypad (0-9, dot, minus, del, cursor)
	void ApplyNumericStep(int32 Direction);            // +1/-1: value ± step(or 1), clamped
	void AdjustDigitAtCursor(int32 Direction);         // up/down on cursor key: ±1 the digit at caret, with carry, clamped
	void ClearNumericValue();                          // Del swipe-up Clr in Number mode: set to 0, or the clamp bound nearest 0
	void CycleNumericRadix();                          // integer keypad: DEC→HEX→OCT→DEC, converts current value
	static FString FormatIntInRadix(int64 Value, int32 Radix);  // integer → string in base 10/16/2

};

}  // namespace ImSlate

UCLASS()
class IMSLATE_API UImVirtualKeyboard : public UObject
{
	GENERATED_BODY()
public:
	static TSharedPtr<ImSlate::SImSlateVirtualKeyboard> GetVirtualKeyboard() { return ImSlate::SImSlateVirtualKeyboard::Get(); }
};

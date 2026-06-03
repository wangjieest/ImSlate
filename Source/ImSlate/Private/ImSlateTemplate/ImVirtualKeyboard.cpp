// Copyright ImSlate, Inc. All Rights Reserved.
#include "ImSlateTemplate/ImVirtualKeyboard.h"
#include "ImSlateTemplate/ImVirtualKey.h"

#include "ImSlatePrivate.h"
#include "SImViewportGame.h"
#include "SImSlateWindow.h"
#include "ImSlateTemplate/ImEditableText.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "UnrealClient.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformApplicationMisc.h"  // FPlatformApplicationMisc::ClipboardPaste
#include "Framework/Text/TextLayout.h"
#include "Widgets/Input/SEditableText.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SOverlay.h"
#include "Styling/CoreStyle.h"          // FCoreStyle::Get / FButtonStyle
#include "Brushes/SlateColorBrush.h"    // FSlateColorBrush (selected-suggestion fill)

namespace ImSlate
{

bool GForceVirtualKeyboard = false;
static FAutoConsoleVariableRef CVar_ForceVirtualKeyboard(
	TEXT("imslate.ForceVirtualKeyboard"),
	GForceVirtualKeyboard,
	TEXT("Force virtual keyboard on desktop for testing."));

// The global GetImSlateEffectiveScale() is tuned for panels; on high-PPI phones the engine
// divides the physical density by ContentScaleFactor (r.MobileContentScaleFactor, e.g. 2.68 on
// iPhone 17), so the effective scale lands ~1.8 and the on-screen keys feel too small. Rather
// than inflate ALL panels globally, the keyboard gets its own extra multiplier on top.
//
// Default is MOBILE-ONLY: on desktop the keys are already plenty large (effective scale already
// carries the monitor DPI, which on a high-PPI editor monitor can be 2.0~2.7), so the extra ×1.5
// just makes the keyboard balloon to near full-screen. Desktop defaults to 1.0; mobile keeps the
// 1.5 compensation. The CVar still overrides on either platform.
float GImSlateKeyboardScale = (PLATFORM_ANDROID || PLATFORM_IOS) ? 1.5f : 1.0f;
static FAutoConsoleVariableRef CVar_ImSlateKeyboardScale(
	TEXT("imslate.KeyboardScale"),
	GImSlateKeyboardScale,
	TEXT("Extra size multiplier for the virtual keyboard only (on top of imslate.LayoutScale). 1.0 = same as panels. Default: mobile 1.5, desktop 1.0."));

// ---- Plan A: keyboard height cap ----
// The keyboard fills the whole viewport, so its cached local height == viewport height. We cache
// it (updated each Tick) plus the keyboard's per-unit-scale logical height (= measured content
// height / the scale it was measured at). GetKbScale() then clamps the scale down so the keyboard
// never exceeds GKeyboardMaxHeightFraction of the viewport. This SHRINKS the keys to fit (no
// clipping), unlike an SBox MaxDesiredHeight which would just crop the bottom rows off.
static float GCachedViewportHeight = 0.f;          // logical px (== keyboard's cached local Y)
static float GCachedKeyboardUnitHeight = 0.f;      // content height at scale=1 (content / scale)

// Hard cap on the WHOLE keyboard (preview + keys) as a fraction of the available viewport height.
// On a large/high-DPI screen (e.g. editor at full size) the DPI-driven scale can balloon the
// keyboard to nearly full-screen; this clamps it so it never eats more than this fraction.
float GKeyboardMaxHeightFraction = 0.5f;
static FAutoConsoleVariableRef CVar_KeyboardMaxHeightFraction(
	TEXT("imslate.KeyboardMaxHeightFraction"),
	GKeyboardMaxHeightFraction,
	TEXT("Max keyboard height as a fraction of the viewport height (0.5 = half-screen). 0 disables the cap."));

// Keyboard-local scale: panel effective scale × keyboard multiplier. ALL keyboard sizing
// (keys, spacing, popups, height caps) AND the key widgets' ComputeDesiredSize / gesture
// thresholds (ImVirtualKey.cpp) use this so the whole keyboard scales uniformly. Exported via
// ImSlateFactory.h as GetImSlateKeyboardScale; GetKbScale is the file-local short alias.
static float GetKbScale()
{
	float Scale = GetImSlateEffectiveScale() * FMath::Max(GImSlateKeyboardScale, 0.1f);

	// Cap the keyboard to a fraction of the viewport height by clamping the scale. We need both the
	// viewport height and the keyboard's natural per-unit-scale height; both are measured at runtime
	// (see Tick). Until measured, no cap is applied.
	if (GKeyboardMaxHeightFraction > 0.f && GCachedViewportHeight > 0.f && GCachedKeyboardUnitHeight > 0.f)
	{
		const float MaxH = GCachedViewportHeight * GKeyboardMaxHeightFraction;
		const float MaxScale = MaxH / GCachedKeyboardUnitHeight;
		Scale = FMath::Min(Scale, MaxScale);
	}
	return FMath::Max(Scale, 0.1f);
}

// Exported alias (declared in ImSlateFactory.h) so the key widgets in ImVirtualKey.cpp can use the
// SAME scale as the keyboard layout — otherwise keys sized by GetImSlateEffectiveScale while the
// layout/spacing used GetKbScale, making keys too small relative to their slots.
IMSLATE_API float GetImSlateKeyboardScale()
{
	return GetKbScale();
}

// Minimum edge margin (logical px, ×KbScale) applied on top of the OS safe area. Default 0:
// on desktop the OS safe inset is 0, so the keyboard sits flush to the edge (no extra gap). On
// mobile the OS safe inset already provides the gap. Raise this if you want a guaranteed margin
// on all platforms. NOTE: it's a MAX with the safe inset, NOT added — so a large mobile safe
// inset is used as-is, and this only kicks in where the safe inset is smaller (e.g. desktop).
float GImSlateKeyboardEdgeMargin = 0.f;
static FAutoConsoleVariableRef CVar_ImSlateKeyboardEdgeMargin(
	TEXT("imslate.KeyboardEdgeMargin"),
	GImSlateKeyboardEdgeMargin,
	TEXT("Min keyboard edge margin in logical px (×KeyboardScale), max'd with the OS safe area. 0 = flush on desktop."));

// Extra LEFT/RIGHT margin on mobile when the screen is in LANDSCAPE, as a fraction of screen
// width. Phones in landscape put cutouts/rounded corners on the left & right; this pulls the
// keyboard edges further in so keys stay reachable and unobscured. Applied via max() like the
// other margins (so it only widens, never shrinks). 0 disables.
float GImSlateKeyboardLandscapeSideFrac = 0.04f;
static FAutoConsoleVariableRef CVar_ImSlateKeyboardLandscapeSideFrac(
	TEXT("imslate.KeyboardLandscapeSideFrac"),
	GImSlateKeyboardLandscapeSideFrac,
	TEXT("Mobile landscape extra left/right keyboard margin as a fraction of screen width (0 = off)."));

// Keyboard edge margin as an FMargin: max(OS safe-area inset, EdgeMargin[, landscape side]) per
// side. Used by BOTH the keyboard body and the sibling suggestion overlay so they line up. Top is
// not padded (the keyboard is bottom-anchored). Returns local Slate units.
static FMargin GetKbSafeMargin()
{
	FMargin Safe(0.f);
	if (FSlateApplication::IsInitialized())
		FSlateApplicationBase::Get().GetSafeZoneSize(Safe, FVector2f::ZeroVector);
	const float E = FMath::Max(GImSlateKeyboardEdgeMargin, 0.f) * GetKbScale();

	// Mobile + landscape: add an extra left/right inset (fraction of screen width).
	float SideExtra = 0.f;
#if PLATFORM_IOS || PLATFORM_ANDROID
	if (GImSlateKeyboardLandscapeSideFrac > 0.f && FSlateApplication::IsInitialized())
	{
		const FVector2D Screen = FSlateApplicationBase::Get().GetPreferredWorkArea().GetSize();
		if (Screen.X > Screen.Y)  // landscape
			SideExtra = Screen.X * GImSlateKeyboardLandscapeSideFrac;
	}
#endif

	// max (not add): mobile safe inset used as-is; E / SideExtra only fill in where it's smaller.
	const float Left  = FMath::Max3(Safe.Left,  E, SideExtra);
	const float Right = FMath::Max3(Safe.Right, E, SideExtra);
	return FMargin(Left, 0.f, Right, FMath::Max(Safe.Bottom, E));
}

TSharedPtr<SImSlateVirtualKeyboard> SImSlateVirtualKeyboard::Get()
{
	if (!GImSlate) return nullptr;
	for (auto& Viewport : GImSlate->Viewports)
	{
		if (Viewport->IsGameViewport())
		{
			auto& GameVP = static_cast<SImViewportGame&>(Viewport.Get());
			auto Kb = GameVP.GetOrCreateVirtualKeyboard();
			if (Kb.IsValid())
				GameVP.EnsureKeyboardInViewport();
			return Kb;
		}
	}
	return nullptr;
}

bool SImSlateVirtualKeyboard::ShouldUseVirtualKeyboard(const SWidget* InWidget)
{
	// Desktop testing override
	if (GForceVirtualKeyboard)
		return Get().IsValid();

	if (!Get().IsValid())
		return false;

	// Walk up the parent chain to find the owning ImSlate window.
	// In-viewport windows (game viewport overlay) have no OS window/IME → use virtual keyboard.
	// Floated-out windows (host viewport = real SWindow) have OS keyboard → don't.
	if (InWidget)
	{
		TSharedPtr<SWidget> Cur = const_cast<SWidget*>(InWidget)->AsShared();
		while (Cur.IsValid())
		{
			if (Cur->GetType() == TEXT("SImSlateWindow"))
			{
				auto* Window = static_cast<SImSlateWindow*>(Cur.Get());
				return Window->IsViewportGame();
			}
			Cur = Cur->GetParentWidget();
		}
	}

	// No widget context: fall back to platform check
	return FPlatformApplicationMisc::RequiresVirtualKeyboard();
}

// ==================== Layout Data ====================

static FVirtualKeyDef MakeCharKey(const FString& Lower, const FString& Upper, const FString& SwipeUp = FString())
{
	FVirtualKeyDef Def;
	Def.Label = Lower;
	Def.ShiftLabel = Upper;
	Def.Value = Lower;
	Def.ShiftValue = Upper;
	Def.Swipe.Up.Label = SwipeUp;
	Def.Action = EVirtualKeyAction::Char;
	return Def;
}

FVirtualKeyDef SImSlateVirtualKeyboard::MakeBackspaceKey(float Width)
{
	FVirtualKeyDef Def;
	Def.Label = TEXT("Del");
	Def.Action = EVirtualKeyAction::Backspace;
	Def.WidthMultiplier = Width;
	// Swipe up = clear all
	Def.Swipe.Up.Label = TEXT("Clr");
	Def.Swipe.Up.Callback.BindLambda([this]() {
		BackspaceUndoBuffer = CurrentText.Left(CursorPosition);
		CurrentText.RemoveAt(0, CursorPosition);
		CursorPosition = 0;
		UpdatePreview(); UpdateSuggestions();
	});
	Def.Swipe.Down.Label = TEXT("Hide");
	Def.Swipe.Down.Callback.BindLambda([this]() { Hide(false); });
	return Def;
}

static FVirtualKeyDef MakeActionKey(EVirtualKeyAction Action, const FString& Label, float Width = 1.f)
{
	FVirtualKeyDef Def;
	Def.Label = Label;
	Def.Action = Action;
	Def.WidthMultiplier = Width;
	return Def;
}

TArray<TArray<FVirtualKeyDef>> SImSlateVirtualKeyboard::GetQWERTYLayout()
{
	const TCHAR* Row1Chars[] = {TEXT("q"),TEXT("w"),TEXT("e"),TEXT("r"),TEXT("t"),TEXT("y"),TEXT("u"),TEXT("i"),TEXT("o"),TEXT("p")};
	const TCHAR* SwipeDigits[] = {TEXT("1"),TEXT("2"),TEXT("3"),TEXT("4"),TEXT("5"),TEXT("6"),TEXT("7"),TEXT("8"),TEXT("9"),TEXT("0")};

	TArray<TArray<FVirtualKeyDef>> Rows;

	// Row 1: qwertyuiop with swipe-up digits (no separate number row).
	{
		TArray<FVirtualKeyDef> Row;
		for (int32 i = 0; i < 10; ++i)
		{
			FString Lower = Row1Chars[i];
			Row.Add(MakeCharKey(Lower, Lower.ToUpper(), SwipeDigits[i]));
		}
		Rows.Add(MoveTemp(Row));
	}

	// Row 2: asdfghjkl with swipe-up symbols !@#$%&*()
	{
		const TCHAR* Chars[] = {TEXT("a"),TEXT("s"),TEXT("d"),TEXT("f"),TEXT("g"),TEXT("h"),TEXT("j"),TEXT("k"),TEXT("l")};
		const TCHAR* Syms[]  = {TEXT("!"),TEXT("@"),TEXT("#"),TEXT("$"),TEXT("%"),TEXT("&"),TEXT("*"),TEXT("("),TEXT(")")};
		TArray<FVirtualKeyDef> Row;
		for (int32 i = 0; i < 9; ++i)
		{
			FString Lower = Chars[i];
			Row.Add(MakeCharKey(Lower, Lower.ToUpper(), Syms[i]));
		}
		Rows.Add(MoveTemp(Row));
	}

	// Row 3: [Shift] zxcvbnm [Backspace] with swipe-up symbols ~:'-/.?
	{
		const TCHAR* Chars[] = {TEXT("z"),TEXT("x"),TEXT("c"),TEXT("v"),TEXT("b"),TEXT("n"),TEXT("m")};
		const TCHAR* Syms[]  = {TEXT("~"),TEXT(":"),TEXT("'"),TEXT("_"),TEXT("/"),TEXT("."),TEXT("?")};
		TArray<FVirtualKeyDef> Row;
		Row.Add(MakeActionKey(EVirtualKeyAction::Shift, TEXT("\x21E7"), 1.3f));
		for (int32 i = 0; i < 7; ++i)
		{
			FString Lower = Chars[i];
			auto Key = MakeCharKey(Lower, Lower.ToUpper(), Syms[i]);
			Row.Add(MoveTemp(Key));
		}
		Row.Add(MakeBackspaceKey(1.3f));
		Rows.Add(MoveTemp(Row));
	}

	return Rows;
}

TArray<FVirtualKeyDef> SImSlateVirtualKeyboard::GetBottomRow()
{
	// ToggleType + Done moved to the preview row.
	TArray<FVirtualKeyDef> Row;

	// Full-screen row spans 10 columns; keep the same . : space = 1 : 2 ratio.
	auto DotKey = MakeCharKey(TEXT("."), TEXT("."), TEXT(","));
	DotKey.LongPressChars = {TEXT(";"),TEXT("-"),TEXT("\""),TEXT("+"),TEXT("="),TEXT("\\"),TEXT("["),TEXT("]"),TEXT("<"),TEXT(">")};
	DotKey.WidthMultiplier = 3.34f;
	Row.Add(MoveTemp(DotKey));

	Row.Add(MakeActionKey(EVirtualKeyAction::Space, TEXT("\x25C0  space  \x25B6"), 6.66f));
	return Row;
}

static FVirtualKeyDef MakeT9Key(const FString& Label, const FString& Num, const TArray<FString>& Chars)
{
	FVirtualKeyDef Def;
	Def.Label = Label.ToLower();
	Def.ShiftLabel = Label.ToUpper();
	Def.Value = Num;
	Def.Action = EVirtualKeyAction::Char;
	Def.WidthMultiplier = 2.f;

	if (Chars.Num() >= 1) Def.Swipe.Up.Label = Chars[0];
	if (Chars.Num() >= 2) Def.Swipe.Left.Label = Chars[1];
	if (Chars.Num() >= 3) Def.Swipe.Right.Label = Chars[2];
	if (Chars.Num() >= 4) Def.Swipe.Down.Label = Chars[3];

	for (const FString& Ch : Chars)
		Def.LongPressChars.Add(Ch);
	Def.LongPressChars.Add(Num);
	for (const FString& Ch : Chars)
		Def.LongPressChars.Add(Ch.ToUpper());

	return Def;
}

TArray<TArray<FVirtualKeyDef>> SImSlateVirtualKeyboard::GetT9Layout()
{
	TArray<TArray<FVirtualKeyDef>> Rows;

	// Row 1: .,?  ABC  DEF  [⌫]
	{
		TArray<FVirtualKeyDef> Row;

		FVirtualKeyDef PuncKey;
		PuncKey.Label = TEXT(".,?");
		PuncKey.Action = EVirtualKeyAction::Char;
		PuncKey.Value = TEXT("1");
		PuncKey.WidthMultiplier = 2.f;
		PuncKey.Swipe.Up.Label = TEXT(".");
		PuncKey.Swipe.Left.Label = TEXT(",");
		PuncKey.Swipe.Right.Label = TEXT("?");
		PuncKey.Swipe.Down.Label = TEXT("!");
		PuncKey.LongPressChars = {TEXT(";"),TEXT(":"),TEXT("'"),TEXT("1"),TEXT("\""),TEXT("-"),TEXT("/")};
		Row.Add(MoveTemp(PuncKey));

		Row.Add(MakeT9Key(TEXT("ABC"), TEXT("2"), {TEXT("a"),TEXT("b"),TEXT("c")}));
		Row.Add(MakeT9Key(TEXT("DEF"), TEXT("3"), {TEXT("d"),TEXT("e"),TEXT("f")}));
		Row.Add(MakeBackspaceKey(2.f));
		Rows.Add(MoveTemp(Row));
	}

	// Row 2: GHI  JKL  MNO  [⇧]
	{
		TArray<FVirtualKeyDef> Row;
		Row.Add(MakeT9Key(TEXT("GHI"), TEXT("4"), {TEXT("g"),TEXT("h"),TEXT("i")}));
		Row.Add(MakeT9Key(TEXT("JKL"), TEXT("5"), {TEXT("j"),TEXT("k"),TEXT("l")}));
		Row.Add(MakeT9Key(TEXT("MNO"), TEXT("6"), {TEXT("m"),TEXT("n"),TEXT("o")}));
		Row.Add(MakeActionKey(EVirtualKeyAction::Shift, TEXT("\x21E7"), 2.f));
		Rows.Add(MoveTemp(Row));
	}

	// Row 3: PQRS  TUV  WXYZ  [◀▶]
	{
		TArray<FVirtualKeyDef> Row;
		Row.Add(MakeT9Key(TEXT("PQRS"), TEXT("7"), {TEXT("p"),TEXT("q"),TEXT("r"),TEXT("s")}));
		{
			auto TuvKey = MakeT9Key(TEXT("TUV"), TEXT("8"), {TEXT("t"),TEXT("u"),TEXT("v")});
			TuvKey.Swipe.Down.Label = TEXT("0");
			Row.Add(MoveTemp(TuvKey));
		}
		Row.Add(MakeT9Key(TEXT("WXYZ"), TEXT("9"), {TEXT("w"),TEXT("x"),TEXT("y"),TEXT("z")}));

		{
			auto CursorKey = MakeActionKey(EVirtualKeyAction::Left, TEXT("\x25C0 \x25B6"), 2.f);
			CursorKey.Swipe.Left.Label = TEXT("\x25C0");
			CursorKey.Swipe.Left.Callback.BindLambda([this]() { MoveCursor(-1); UpdatePreview(); });
			CursorKey.Swipe.Right.Label = TEXT("\x25B6");
			CursorKey.Swipe.Right.Callback.BindLambda([this]() { MoveCursor(1); UpdatePreview(); });
			Row.Add(MoveTemp(CursorKey));
		}
		Rows.Add(MoveTemp(Row));
	}

	return Rows;
}

TArray<FVirtualKeyDef> SImSlateVirtualKeyboard::GetT9BottomRow()
{
	// ToggleType + Done moved to the preview row.
	TArray<FVirtualKeyDef> Row;
	Row.Add(MakeCharKey(TEXT("0"), TEXT("0")));
	Row[0].WidthMultiplier = 2.f;
	Row.Add(MakeActionKey(EVirtualKeyAction::Space, TEXT("\x25C0 space \x25B6"), 4.f));
	return Row;
}

TArray<TArray<FVirtualKeyDef>> SImSlateVirtualKeyboard::GetSymbolLayout()
{
	TArray<TArray<FVirtualKeyDef>> Rows;

	// Row 1: common symbols
	{
		const TCHAR* Syms[] = {TEXT("!"),TEXT("@"),TEXT("#"),TEXT("$"),TEXT("%"),TEXT("^"),TEXT("&"),TEXT("*"),TEXT("("),TEXT(")")};
		TArray<FVirtualKeyDef> Row;
		for (auto S : Syms) Row.Add(MakeCharKey(S, S));
		Row[3].LongPressChars = {TEXT("€"),TEXT("£"),TEXT("¥"),TEXT("₩"),TEXT("¢")};
		Rows.Add(MoveTemp(Row));
	}

	// Row 2: more symbols
	{
		const TCHAR* Syms[] = {TEXT("-"),TEXT("_"),TEXT("="),TEXT("+"),TEXT("["),TEXT("]"),TEXT("{"),TEXT("}"),TEXT("\\")};
		TArray<FVirtualKeyDef> Row;
		for (auto S : Syms) Row.Add(MakeCharKey(S, S));
		Row[0].LongPressChars = {TEXT("–"),TEXT("—"),TEXT("•")};
		Row[2].LongPressChars = {TEXT("≈"),TEXT("≠")};
		Rows.Add(MoveTemp(Row));
	}

	// Row 3: [ABC] remaining symbols + backspace
	{
		TArray<FVirtualKeyDef> Row;
		Row.Add(MakeActionKey(EVirtualKeyAction::ToggleLayer, TEXT("ABC"), 1.3f));
		const TCHAR* Syms[] = {TEXT(";"),TEXT(":"),TEXT("'"),TEXT("\""),TEXT(","),TEXT("<"),TEXT(">")};
		for (auto S : Syms) Row.Add(MakeCharKey(S, S));
		Row[3].LongPressChars = {TEXT("«"),TEXT("»"),TEXT("„"),TEXT("\x201C"),TEXT("\x201D")};
		Row.Add(MakeBackspaceKey(1.3f));
		Rows.Add(MoveTemp(Row));
	}

	return Rows;
}

// Keyboard sizing caps in LOGICAL pixels (multiplied by the keyboard scale at use sites).
static constexpr float GMaxKeyboardHeight = 300.f;

// ==================== Construct ====================

void SImSlateVirtualKeyboard::Construct(const FArguments& InArgs)
{
	SetVisibility(EVisibility::Collapsed);

	float Scale = GetKbScale();

	// Preview-row keys: persistent KeyDefs (not rebuilt by BuildKeyboard).
	ToggleTypeKeyDef = MakeShared<FVirtualKeyDef>();
	ToggleTypeKeyDef->Action = EVirtualKeyAction::ToggleType;
	ToggleTypeKeyDef->Label = TEXT("T9");
	ToggleTypeKey = MakeBoundKey(ToggleTypeKeyDef.Get());

	DoneKeyDef = MakeShared<FVirtualKeyDef>();
	DoneKeyDef->Action = EVirtualKeyAction::Enter;
	DoneKeyDef->Label = TEXT("Done");
	// Up swipe = clear all text (keyboard stays open).
	DoneKeyDef->Swipe.Up.Label = TEXT("Clr");
	DoneKeyDef->Swipe.Up.Callback.BindLambda([this]() {
		CurrentText.Reset();
		CursorPosition = 0;
		UpdatePreview();
		UpdateSuggestions();
	});
	// Down swipe = Esc: cancel, restoring the text to what it was when the keyboard opened.
	DoneKeyDef->Swipe.Down.Label = TEXT("Esc");
	DoneKeyDef->Swipe.Down.Callback.BindLambda([this]() { Hide(false); });
	DoneKey = MakeBoundKey(DoneKeyDef.Get());

	ChildSlot
	[
		SAssignNew(RootOverlay, SOverlay)

		// Full-screen modal hit-blocker (bottom layer). Visibility toggled in Show()
		// per FVirtualKeyboardShowParams::bBlockBackground.
		+ SOverlay::Slot()
		[
			SAssignNew(BackgroundBlocker, SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
			.BorderBackgroundColor(FLinearColor(0.f, 0.f, 0.f, 0.f))  // transparent but hit-testable
			.Visibility(EVisibility::Visible)
		]

		// Suggestions: SEPARATE overlay layer (not in the keyboard's vertical stack, so candidate
		// count never changes the keyboard height / origin). VAlign_Bottom + bottom Offset = the
		// preview+keys height pins the band DIRECTLY ABOVE the preview row. The band is only as
		// tall as its content (AutoHeight via VAlign_Bottom), capped at 120; few candidates sit
		// right above the preview, more candidates extend UPWARD from there. This keeps the
		// candidates near the keys (visible) instead of starting from the top of the screen.
		+ SOverlay::Slot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Bottom)
		.Padding(TAttribute<FMargin>::CreateLambda([this]() -> FMargin {
			const float UnitH = PreviewKeysRoot.IsValid() ? PreviewKeysRoot->GetCachedGeometry().GetLocalSize().Y : 0.f;
			// Left/right: keep off the device cutouts (same inset the keyboard body uses).
			// Bottom: sit directly above the preview row, which is itself lifted by the safe-area
			// bottom inset + extra — so add that here too, otherwise the band would overlap it.
			const FMargin Safe = GetKbSafeMargin();
			return FMargin(Safe.Left, 0.f, Safe.Right, UnitH + Safe.Bottom);
		}))
		[
			SNew(SBox)
			.MaxDesiredHeight(120.f)  // cap; beyond this the scroll box scrolls
			[
				SNew(SScrollBox)
				.Orientation(Orient_Vertical)
				+ SScrollBox::Slot()
				[
					SAssignNew(SuggestionBar, SWrapBox)
					.UseAllottedSize(true)
				]
			]
		]

		// Preview row + keys: the keyboard proper. Bottom-anchored via a top spacer. Its height
		// depends ONLY on preview + keys (suggestions are no longer here), so it is constant
		// regardless of candidate count → stable origin.
		+ SOverlay::Slot()
		[
			SNew(SVerticalBox)

			// Top spacer: pushes preview+keys to the bottom of the (full-screen) overlay.
			+ SVerticalBox::Slot()
			.FillHeight(1.f)

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				// Edge margin: keep the keyboard off the device's left/right cutouts and bottom
				// home-indicator. = max(OS safe inset, imslate.KeyboardEdgeMargin) per side; on
				// desktop the inset is 0 and the default EdgeMargin is 0, so the keyboard sits
				// flush (no left/right gap). Top is NOT padded — keyboard is bottom-anchored and
				// the spacer above handles the top. (We use a plain SBox + GetKbSafeMargin instead
				// of SSafeZone so the same max(safe, min) rule applies and isn't double-added.)
				SNew(SBox)
				.HAlign(HAlign_Fill)
				.VAlign(VAlign_Fill)
				.Padding(TAttribute<FMargin>::CreateLambda([]() -> FMargin { return GetKbSafeMargin(); }))
				[
				SAssignNew(PreviewKeysRoot, SVerticalBox)

				// Preview row: [ToggleType] [preview text] [Done]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SAssignNew(PreviewBorder, SBorder)
					.BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
					.BorderBackgroundColor(FLinearColor(0.06f, 0.06f, 0.06f, 0.95f))
					.Padding(FMargin(2.f, 2.f))
					.Visibility(EVisibility::Visible)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SNew(SBox)
							.WidthOverride(TAttribute<FOptionalSize>::CreateLambda([]() { return FOptionalSize(54.f * GetKbScale()); }))
							.HeightOverride(TAttribute<FOptionalSize>::CreateLambda([]() { return FOptionalSize(32.f * GetKbScale()); }))  // match keyboard key height (SImSlateKey: 32×scale)
							[
								ToggleTypeKey.ToSharedRef()
							]
						]
						+ SHorizontalBox::Slot()
						.FillWidth(1.f)
						.VAlign(VAlign_Center)
						.Padding(FMargin(6.f, 0.f))
						[
							SAssignNew(PreviewEdit, SImEditableText)
							.Font(GetImSlateDefaultFont(11))
							.ColorAndOpacity(FLinearColor::White)
							.Justification(ETextJustify::Center)
							.IsReadOnly(true)  // caret is self-drawn; native caret/edit disabled
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SNew(SBox)
							.WidthOverride(TAttribute<FOptionalSize>::CreateLambda([]() { return FOptionalSize(64.f * GetKbScale()); }))
							.HeightOverride(TAttribute<FOptionalSize>::CreateLambda([]() { return FOptionalSize(32.f * GetKbScale()); }))  // match keyboard key height (SImSlateKey: 32×scale)
							[
								DoneKey.ToSharedRef()
							]
						]
					]
				]

				// Keyboard: auto height, max 300 logical px (scaled by DPI).
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SBox)
					.MaxDesiredHeight(GMaxKeyboardHeight * GetKbScale())
					[
						SNew(SBorder)
						.BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
						.BorderBackgroundColor(FLinearColor(0.08f, 0.08f, 0.08f, 0.95f))
						.Padding(2.f)
						.Visibility(EVisibility::Visible)
						[
							SAssignNew(KeyboardGrid, SVerticalBox)
						]
					]
				]
				]  // end edge-margin SBox (around PreviewKeysRoot)
			]
		]
	];

	if (PreviewEdit.IsValid())
	{
		PreviewEdit->SetPreviewDisplayMode(true);
		// Display-only: don't let the editable widget eat mouse events, so preview-area
		// drag (move cursor) still reaches the keyboard. It only shows text + self-drawn caret.
		PreviewEdit->SetVisibility(EVisibility::HitTestInvisible);
	}

	BuildKeyboard();
}

FVector2D SImSlateVirtualKeyboard::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
	// Fill the whole viewport so this widget's origin is fixed (independent of content height).
	// This keeps popups (anchored in this widget's local space) from drifting when the
	// suggestion row count changes the content height during a Del step-drag.
	if (GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport)
	{
		const FIntPoint VP = GEngine->GameViewport->Viewport->GetSizeXY();
		if (VP.X > 0 && VP.Y > 0)
			return FVector2D(VP.X, VP.Y);
	}
	// Fallback before the viewport is known: defer to content size.
	return SCompoundWidget::ComputeDesiredSize(LayoutScaleMultiplier);
}

SImSlateVirtualKeyboard::EKeyboardLayoutMode SImSlateVirtualKeyboard::ComputeLayoutMode(float Width) const
{
	// Pure function of width: same screen → same mode, independent of toggle history.
	if (Width <= 0.f)
		return EKeyboardLayoutMode::FullScreen;  // unknown yet — start collapsed

	float Scale = GetKbScale();
	// Base key height is 32 logical px; Scale already includes the keyboard multiplier (GetKbScale).
	// (The old FMath::Min(32, GMaxKeyHeight=48) was dead — 32 is always smaller, never reached 48.
	// Size is now driven by imslate.KeyboardScale.)
	float KeyH = 32.f * Scale;
	float ComfortKeyW = KeyH * 1.3f;
	float SideWidth = 5.f * ComfortKeyW + 5.f * 4.f * Scale;
	float MinGap = 60.f * Scale;
	bool bCanSplit = (SideWidth * 2.f + MinGap) <= Width;

	return bCanSplit ? EKeyboardLayoutMode::Split : EKeyboardLayoutMode::FullScreen;
}

void SImSlateVirtualKeyboard::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	if (!bVisible) return;

	// Grab Slate user focus onto the keyboard root once it's in the tree (Tick implies parented).
	// This is the deferred follow-up to Show()'s bPendingFocus — see Show() for why it can't be
	// done there. SetKeyboardFocus mirrors the pattern used in SImSearchBox.
	if (bPendingFocus)
	{
		if (FSlateApplication::IsInitialized()
			&& FSlateApplication::Get().SetKeyboardFocus(SharedThis(this), EFocusCause::SetDirectly))
		{
			bPendingFocus = false;
		}
	}

	// AllottedGeometry here is authoritative (unlike GetCachedGeometry timing during
	// Construct/Show). Rebuild only when the resulting layout mode actually changes.
	float Width = AllottedGeometry.GetLocalSize().X;
	if (Width <= 0.f) return;

	EKeyboardLayoutMode DesiredMode = ComputeLayoutMode(Width);
	if (DesiredMode != BuiltLayoutMode || BuiltWidth < 0.f)
	{
		BuiltLayoutMode = DesiredMode;
		BuiltWidth = Width;
		BuildKeyboard();
	}

	// Plan A height-cap inputs (see GetKbScale): the keyboard fills the viewport, so AllottedGeometry
	// Y == viewport height. The per-unit-scale content height = current content desired height /
	// current keyboard scale; dividing out the scale makes it scale-independent, so feeding it back
	// into GetKbScale's clamp converges (the keys are linear in scale).
	GCachedViewportHeight = AllottedGeometry.GetLocalSize().Y;
	if (PreviewKeysRoot.IsValid())
	{
		const float ContentH = PreviewKeysRoot->GetDesiredSize().Y;
		const float CurScale = FMath::Max(GetImSlateKeyboardScale(), 0.1f);
		if (ContentH > 0.f)
			GCachedKeyboardUnitHeight = ContentH / CurScale;
	}
}

void SImSlateVirtualKeyboard::BuildKeyboard()
{
	KeyboardGrid->ClearChildren();
	KeyWidgets.Reset();
	PersistentKeyDefs.Reset();
	PersistentKeyDefs.Reserve(128);

	if (GetLayoutMode() == EKeyboardLayoutMode::Split)
		BuildSplitLayout();
	else
		BuildFullScreenLayout();
}

void SImSlateVirtualKeyboard::BuildFullScreenLayout()
{
	float Scale = GetKbScale();
	float KeySpacing = 4.f * Scale;

	auto AddRow = [&](const TArray<FVirtualKeyDef>& Keys) {
		TSharedRef<SHorizontalBox> RowBox = SNew(SHorizontalBox);
		BuildKeyRow(RowBox, Keys);
		KeyboardGrid->AddSlot()
		.AutoHeight()
		.Padding(FMargin(0.f, KeySpacing * 0.5f))
		[
			RowBox
		];
	};

	if (KeyboardType == EKeyboardType::T9)
	{
		for (const auto& Row : GetT9Layout())
			AddRow(Row);
		AddRow(GetT9BottomRow());
	}
	else
	{
		if (CurrentLayer == 0)
		{
			for (const auto& Row : GetQWERTYLayout())
				AddRow(Row);
		}
		else
		{
			for (const auto& Row : GetSymbolLayout())
				AddRow(Row);
		}

		AddRow(GetBottomRow());
	}
}

void SImSlateVirtualKeyboard::BuildSplitLayout()
{
	float Scale = GetKbScale();
	float KeySpacing = 4.f * Scale;

	auto AddRowsToBox = [&](TSharedRef<SVerticalBox> Box, const TArray<TArray<FVirtualKeyDef>>& Rows) {
		for (const auto& Row : Rows)
		{
			TSharedRef<SHorizontalBox> RowBox = SNew(SHorizontalBox);
			BuildKeyRow(RowBox, Row);
			Box->AddSlot().AutoHeight().Padding(FMargin(0.f, KeySpacing * 0.5f))[RowBox];
		}
	};

	auto BuildQWERTYHalf = [&](TSharedRef<SVerticalBox> Half, bool bRight) {
		auto AllRows = (CurrentLayer == 0) ? GetQWERTYLayout() : GetSymbolLayout();

		auto SplitRows = [&](const TArray<TArray<FVirtualKeyDef>>& Src) {
			TArray<TArray<FVirtualKeyDef>> Result;
			for (const auto& Row : Src)
			{
				int32 Mid = Row.Num() / 2;
				TArray<FVirtualKeyDef> HalfRow;
				int32 Start = bRight ? Mid : 0;
				int32 End = bRight ? Row.Num() : Mid;
				for (int32 i = Start; i < End; ++i) HalfRow.Add(Row[i]);
				Result.Add(MoveTemp(HalfRow));
			}
			return Result;
		};

		AddRowsToBox(Half, SplitRows(AllRows));

		// Each half gets its own [. | space] so both sides have matching proportions.
		TArray<FVirtualKeyDef> HalfBottom;
		auto DotKey = MakeCharKey(TEXT("."), TEXT("."), TEXT(","));
		DotKey.LongPressChars = {TEXT(";"),TEXT("-"),TEXT("\""),TEXT("+"),TEXT("="),TEXT("\\"),TEXT("["),TEXT("]"),TEXT("<"),TEXT(">")};
		DotKey.WidthMultiplier = 2.f;
		HalfBottom.Add(MoveTemp(DotKey));
		HalfBottom.Add(MakeActionKey(EVirtualKeyAction::Space, TEXT("\x25C0 space \x25B6"), 4.f));
		TArray<TArray<FVirtualKeyDef>> BottomRows = {MoveTemp(HalfBottom)};
		AddRowsToBox(Half, BottomRows);
	};

	TSharedRef<SVerticalBox> LeftHalf = SNew(SVerticalBox);
	TSharedRef<SVerticalBox> RightHalf = SNew(SVerticalBox);

	if (KeyboardType == EKeyboardType::T9)
	{
		auto T9Rows = GetT9Layout();
		TArray<TArray<FVirtualKeyDef>> BottomRows = {GetT9BottomRow()};
		AddRowsToBox(LeftHalf, T9Rows);
		AddRowsToBox(LeftHalf, BottomRows);
		AddRowsToBox(RightHalf, T9Rows);
		AddRowsToBox(RightHalf, BottomRows);
	}
	else
	{
		BuildQWERTYHalf(LeftHalf, false);
		BuildQWERTYHalf(RightHalf, true);
	}

	// Both T9 and T26 use the SAME side width — a thumb's reach is a physical span,
	// not a column count. Each half fills this fixed width regardless of key count.
	float SplitScale = GetKbScale();
	float SplitKeyH = 32.f * SplitScale;  // base 32 logical px (× KeyboardScale); old Min(32,48) was dead
	float ComfortKeyW = SplitKeyH * 1.3f;
	float SideWidth = 5.f * ComfortKeyW + 5.f * 4.f * SplitScale;

	KeyboardGrid->AddSlot()
	.FillHeight(1.f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Bottom)
		[
			SNew(SBox)
			.WidthOverride(SideWidth)
			[ LeftHalf ]
		]
		+ SHorizontalBox::Slot()
		.FillWidth(1.f)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Bottom)
		[
			SNew(SBox)
			.WidthOverride(SideWidth)
			[ RightHalf ]
		]
	];
}

TSharedRef<SImSlateKey> SImSlateVirtualKeyboard::MakeBoundKey(const FVirtualKeyDef* DefPtr)
{
	TSharedRef<SImSlateKey> KeyWidget = SNew(SImSlateKey)
		.KeyDef(DefPtr)
		.bShiftActive_Lambda([this]() { return IsUpperCase(); })
		.bShiftSingleShot_Lambda([this]() { return ShiftState == EShiftState::SingleShot; })
		.OnKeyInput_Lambda([this](const FVirtualKeyDef& Def, const FString& Val) { DismissPopup(); OnKeyInput(Def, Val); })
		.OnKeyAction_Lambda([this](EVirtualKeyAction Action) { DismissPopup(); OnKeyAction(Action); })
		.OnLongPress_Lambda([this](const FVirtualKeyDef& Def, const FGeometry& Geo) { OnKeyLongPress(Def, Geo); })
		.OnLongPressMove_Lambda([this](int32 Idx) { OnKeyLongPressMove(Idx); })
		.OnLongPressEnd_Lambda([this](int32 Idx) { OnKeyLongPressEnd(Idx); })
		.OnPressVisual_Lambda([this](const FVirtualKeyDef& Def, const FGeometry& Geo, bool bForceStepDrag) { OnKeyPressVisual(Def, Geo, bForceStepDrag); })
		.OnMoveVisual_Lambda([this](const FVector2D& Delta, bool bSwipeReady) { OnKeyMoveVisual(Delta, bSwipeReady); })
		.OnReleaseVisual_Lambda([this]() { OnKeyReleaseVisual(); })
		.OnSpaceCursorZone_Lambda([this](int32 Dir) { OnSpaceCursorZone(Dir); });
	return KeyWidget;
}

void SImSlateVirtualKeyboard::BuildKeyRow(TSharedRef<SHorizontalBox> RowBox, const TArray<FVirtualKeyDef>& Keys)
{
	float Scale = GetKbScale();

	int32 BaseIndex = PersistentKeyDefs.Num();
	PersistentKeyDefs.Append(Keys);

	for (int32 i = 0; i < Keys.Num(); ++i)
	{
		const FVirtualKeyDef* DefPtr = &PersistentKeyDefs[BaseIndex + i];

		TSharedRef<SImSlateKey> KeyWidget = MakeBoundKey(DefPtr);
		RowBox->AddSlot()
		.FillWidth(DefPtr->WidthMultiplier)
		.Padding(FMargin(2.f * Scale, 0.f))
		[
			KeyWidget
		];

		if (DefPtr->Action == EVirtualKeyAction::Space)
		{
			KeyWidget->SetKeyboardWidthGetter(TAttribute<float>::CreateLambda([this]() {
				float W = GetCachedGeometry().GetLocalSize().X;
				return (GetLayoutMode() == EKeyboardLayoutMode::Split) ? W * 0.33f : W;
			}));
		}

		FKeyWidgetEntry Entry;
		Entry.Widget = KeyWidget;
		Entry.Def = DefPtr;
		KeyWidgets.Add(Entry);
	}
}

void SImSlateVirtualKeyboard::UpdateToggleTypeLabel()
{
	if (ToggleTypeKeyDef.IsValid())
		ToggleTypeKeyDef->Label = (KeyboardType == EKeyboardType::QWERTY) ? TEXT("T9") : TEXT("T26");
}

// ==================== Show / Hide ====================

void SImSlateVirtualKeyboard::Show(const FVirtualKeyboardShowParams& Params)
{
	CurrentText = Params.InitialText;
	OriginalText = Params.InitialText;
	CursorPosition = (Params.CursorPosition == INDEX_NONE) ? CurrentText.Len() : Params.CursorPosition;
	CommitCallback = Params.CommitCallback;
	OnTextChanged = Params.OnTextChanged;
	SuggestionProvider = Params.SuggestionProvider;
	BoundOwner = Params.Owner;  // rebind to the new editable widget's lifecycle
	bSuggestionCommitsAndCloses = Params.bSuggestionCommitsAndCloses;

	// Modal background: block taps from reaching the game when requested.
	if (BackgroundBlocker.IsValid())
		BackgroundBlocker->SetVisibility(Params.bBlockBackground ? EVisibility::Visible : EVisibility::Collapsed);

	ShiftState = EShiftState::Default;
	CurrentLayer = 0;
	BackspaceUndoBuffer.Reset();
	UpdateToggleTypeLabel();

	bVisible = true;
	SetVisibility(EVisibility::Visible);

	// Build once now (may be collapsed if geometry isn't ready). Tick() will rebuild
	// with the correct split/full mode as soon as it has authoritative geometry.
	BuiltWidth = -1.f;
	BuildKeyboard();

	// Start cursor blink
	if (!CursorBlinkTimer.IsValid())
	{
		CursorBlinkTimer = RegisterActiveTimer(0.5f, FWidgetActiveTimerDelegate::CreateLambda(
			[this](double, float) -> EActiveTimerReturnType {
				if (!bVisible) { CursorBlinkTimer.Reset(); return EActiveTimerReturnType::Stop; }
				// Owner gone (panel closed / widget destroyed) → hide keyboard
				if (!BoundOwner.IsValid())
				{
					CursorBlinkTimer.Reset();
					Hide(false);
					return EActiveTimerReturnType::Stop;
				}
				bCursorVisible = !bCursorVisible;
				if (PreviewEdit.IsValid())
					PreviewEdit->SetPreviewCaretVisible(bCursorVisible);  // caret is drawn by PreviewEdit
				Invalidate(EInvalidateWidgetReason::Paint);  // repaint subtree (incl. PreviewEdit)
				return EActiveTimerReturnType::Continue;
			}));
	}

	UpdatePreview();
	UpdateSuggestions();

	// Take Slate user focus onto the keyboard ROOT so physical keys route here (not to the
	// editor viewport's global shortcuts). Deferred to Tick: right now the widget may have just
	// been made Visible and isn't in the widget tree yet, so SetKeyboardFocus/FindPathToWidget
	// could fail. Tick runs once the keyboard is parented & has authoritative geometry.
	bPendingFocus = true;
}

void SImSlateVirtualKeyboard::Hide(bool bCommit)
{
	if (!bVisible) return;

	bVisible = false;
	bPendingFocus = false;
	SetVisibility(EVisibility::Collapsed);

	// Release focus back to the game/editor viewport so its shortcuts work again once the
	// keyboard is gone (we no longer SupportsKeyboardFocus while hidden).
	if (FSlateApplication::IsInitialized() && HasAnyUserFocusOrFocusedDescendants())
		FSlateApplication::Get().SetAllUserFocusToGameViewport();

	if (bCommit)
	{
		if (CommitCallback)
			CommitCallback(CurrentText, ETextCommit::OnEnter);
	}
	else
	{
		if (CommitCallback)
			CommitCallback(OriginalText, ETextCommit::OnCleared);
	}

	CommitCallback = nullptr;
	OnTextChanged = nullptr;
	SuggestionProvider = nullptr;
	BoundOwner.Reset();
	CurrentSuggestions.Reset();
	SelectedSuggestionIndex = -1;
}

bool SImSlateVirtualKeyboard::IsOwnedBy(const SWidget* Widget) const
{
	return Widget != nullptr && BoundOwner.Pin().Get() == Widget;
}

void SImSlateVirtualKeyboard::NotifyOwnerDestroyed(const SWidget* Widget)
{
	// Auto-hide only when the editable we're bound to is the one being destroyed.
	// If the keyboard was rebound to another editable, this is a no-op.
	if (IsOwnedBy(Widget))
		Hide(false);
}

// ==================== Text Editing ====================

void SImSlateVirtualKeyboard::InsertText(const FString& Text)
{
	if (CursorPosition >= CurrentText.Len())
		CurrentText += Text;
	else
		CurrentText.InsertAt(CursorPosition, Text);
	CursorPosition += Text.Len();
}

void SImSlateVirtualKeyboard::DeleteBackward()
{
	if (CursorPosition > 0 && CurrentText.Len() > 0)
	{
		CurrentText.RemoveAt(CursorPosition - 1, 1);
		CursorPosition--;
	}
}

void SImSlateVirtualKeyboard::MoveCursor(int32 Delta)
{
	CursorPosition = FMath::Clamp(CursorPosition + Delta, 0, CurrentText.Len());
}

bool SImSlateVirtualKeyboard::IsUpperCase() const
{
	bool bDefaultUpper = (KeyboardType == EKeyboardType::T9);
	switch (ShiftState)
	{
	case EShiftState::SingleShot: return !bDefaultUpper;
	case EShiftState::Locked:     return !bDefaultUpper;
	default:                      return bDefaultUpper;
	}
}

void SImSlateVirtualKeyboard::ToggleShift()
{
	switch (ShiftState)
	{
	case EShiftState::Default:    ShiftState = EShiftState::SingleShot; break;
	case EShiftState::SingleShot: ShiftState = EShiftState::Locked; break;
	case EShiftState::Locked:     ShiftState = EShiftState::Default; break;
	}
}

void SImSlateVirtualKeyboard::UpdatePreview()
{
	bCursorVisible = true;
	if (PreviewEdit.IsValid())
	{
		PreviewEdit->SetText(FText::FromString(CurrentText));
		PreviewEdit->SetPreviewCaretVisible(true);
		PreviewEdit->GoTo(FTextLocation(0, CursorPosition));  // after SetText: caret to current index
	}
	if (OnTextChanged)
		OnTextChanged(CurrentText);
}

int32 SImSlateVirtualKeyboard::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	// Caret is now self-drawn by PreviewEdit (SImEditableText preview mode) using the
	// engine's text layout, so the keyboard no longer paints it here.
	return SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
}

// ==================== Editor Sync ====================

void SImSlateVirtualKeyboard::SyncFromEditor(const FString& Text, int32 CaretPos)
{
	if (!bVisible) return;

	// Follow the editor's real caret when provided (physical typing/arrows move it); else keep
	// the old position clamped. Compute the target caret first so we can detect caret-only
	// changes (e.g. a bare arrow key moves the caret without changing the text).
	const int32 NewCaret = (CaretPos >= 0)
		? FMath::Clamp(CaretPos, 0, Text.Len())
		: FMath::Clamp(CursorPosition, 0, Text.Len());

	// Bail only when NOTHING changed — both text and caret. Previously bailing on text-equality
	// alone meant arrow-key caret moves (text unchanged) never updated the preview caret.
	if (CurrentText == Text && CursorPosition == NewCaret) return;

	CurrentText = Text;
	CursorPosition = NewCaret;
	bCursorVisible = true;
	if (PreviewEdit.IsValid())
	{
		PreviewEdit->SetText(FText::FromString(CurrentText));
		PreviewEdit->SetPreviewCaretVisible(true);
		PreviewEdit->GoTo(FTextLocation(0, CursorPosition));
	}
	UpdateSuggestions();
}

// ==================== Preview Drag + Cursor Blink ====================

bool SImSlateVirtualKeyboard::IsInPreviewArea(const FVector2D& AbsPos) const
{
	if (PreviewBorder.IsValid())
		return PreviewBorder->GetCachedGeometry().IsUnderLocation(AbsPos);
	return false;
}

void SImSlateVirtualKeyboard::HandlePreviewDrag(const FVector2D& ScreenPos)
{
	float DeltaX = ScreenPos.X - PreviewDragLastPos.X;
	PreviewDragLastPos = ScreenPos;
	PreviewDragAccum += DeltaX;
	float Step = 12.f * GetKbScale();
	while (PreviewDragAccum > Step)  { MoveCursor(1);  PreviewDragAccum -= Step; UpdatePreview(); }
	while (PreviewDragAccum < -Step) { MoveCursor(-1); PreviewDragAccum += Step; UpdatePreview(); }
}

FReply SImSlateVirtualKeyboard::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (IsInPreviewArea(MouseEvent.GetScreenSpacePosition()))
	{
		bPreviewDragging = true;
		PreviewDragLastPos = MouseEvent.GetScreenSpacePosition();
		PreviewDragAccum = 0.f;
	}
	return FReply::Handled();
}

FReply SImSlateVirtualKeyboard::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	bPreviewDragging = false;
	return FReply::Handled();
}

FReply SImSlateVirtualKeyboard::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (bPreviewDragging)
		HandlePreviewDrag(MouseEvent.GetScreenSpacePosition());
	return FReply::Handled();
}

FReply SImSlateVirtualKeyboard::OnTouchStarted(const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent)
{
	if (IsInPreviewArea(InTouchEvent.GetScreenSpacePosition()))
	{
		bPreviewDragging = true;
		PreviewDragLastPos = InTouchEvent.GetScreenSpacePosition();
		PreviewDragAccum = 0.f;
	}
	return FReply::Handled();
}

FReply SImSlateVirtualKeyboard::OnTouchMoved(const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent)
{
	if (bPreviewDragging)
		HandlePreviewDrag(InTouchEvent.GetScreenSpacePosition());
	return FReply::Handled();
}

FReply SImSlateVirtualKeyboard::OnTouchEnded(const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent)
{
	bPreviewDragging = false;
	return FReply::Handled();
}

void SImSlateVirtualKeyboard::UpdateSuggestions()
{
	if (!SuggestionBar.IsValid()) return;

	// Input changed → the candidate list is being refreshed, so the old keyboard selection is no
	// longer meaningful. Reset it. (Up/Down navigation rebuilds the bar via PopulateSuggestionBar
	// directly, NOT through here, so this never clears a fresh selection.)
	SelectedSuggestionIndex = -1;

	// Consume async results first
	TArray<FString> AsyncResults;
	if (AsyncSuggestionQueue.Dequeue(AsyncResults))
	{
		PopulateSuggestionBar(AsyncResults);
		return;
	}

	if (!SuggestionProvider) { SuggestionBar->ClearChildren(); return; }

	TArray<FString> Suggestions;
	SuggestionProvider(CurrentText, Suggestions);
	PopulateSuggestionBar(Suggestions);
}

void SImSlateVirtualKeyboard::UpdateSuggestionsAsync(TArray<FString> InResults)
{
	AsyncSuggestionQueue.Enqueue(MoveTemp(InResults));
	RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateLambda(
		[this](double, float) -> EActiveTimerReturnType {
			if (bVisible) UpdateSuggestions();
			return EActiveTimerReturnType::Stop;
		}));
}

void SImSlateVirtualKeyboard::PopulateSuggestionBar(const TArray<FString>& Suggestions)
{
	if (!SuggestionBar.IsValid()) return;
	SuggestionBar->ClearChildren();

	float Scale = GetKbScale();
	int32 MaxItems = 12;
	const int32 Count = FMath::Min(Suggestions.Num(), MaxItems);

	// Cache the visible candidates so Up/Down can navigate them and Enter can commit the selection.
	CurrentSuggestions.Reset();
	for (int32 i = 0; i < Count; ++i)
		CurrentSuggestions.Add(Suggestions[i]);
	// Keep selection in range (a shorter new list may invalidate the old index).
	if (SelectedSuggestionIndex >= Count)
		SelectedSuggestionIndex = -1;

	// Selected vs unselected candidate buttons share the SAME flat brush type and the SAME paddings,
	// so they are identical in size/shape and the label sits at the same place — only the fill color
	// differs. (The earlier approach reused the default "Button" style for unselected and a flat box
	// for selected; their differing brush margins/paddings made the selected one a different shape
	// and pushed its text lower.) Built once and reused.
	auto MakeFlatStyle = [](const FLinearColor& FillColor) -> FButtonStyle {
		FButtonStyle S = FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("Button");
		FSlateColorBrush Fill(FillColor);
		S.SetNormal(Fill);
		S.SetHovered(Fill);
		S.SetPressed(Fill);
		S.SetNormalPadding(FMargin(0.f));   // identical padding for both states → identical text pos
		S.SetPressedPadding(FMargin(0.f));
		return S;
	};
	static const FButtonStyle UnselectedStyle = MakeFlatStyle(FLinearColor(0.18f, 0.18f, 0.18f, 1.f));  // dark grey
	static const FButtonStyle SelectedStyle   = MakeFlatStyle(FLinearColor(0.10f, 0.45f, 0.90f, 1.f));  // blue

	const FSlateFontInfo NormalFont = GetImSlateDefaultFont(8);
	const FSlateFontInfo BoldFont   = FCoreStyle::GetDefaultFontStyle("Bold", NormalFont.Size);

	for (int32 i = 0; i < Count; ++i)
	{
		FString SugCopy = Suggestions[i];
		const bool bSelected = (i == SelectedSuggestionIndex);
		SuggestionBar->AddSlot()
		.Padding(2.f * Scale)
		[
			SNew(SButton)
			.ContentPadding(FMargin(6.f * Scale, 2.f * Scale))  // same content padding both states
			.ButtonStyle(bSelected ? &SelectedStyle : &UnselectedStyle)
			// Fire on press, not click: the keyboard window (ActivationPolicy::Never, in the game viewport /
				// touch) never delivers a full OnClicked — same reason the keys trigger on down-press.
				.ClickMethod(EButtonClickMethod::MouseDown)
				.TouchMethod(EButtonTouchMethod::Down)
				.OnClicked_Lambda([this, SugCopy]() { OnSuggestionClicked(SugCopy); return FReply::Handled(); })
			[
				SNew(STextBlock)
				.Text(FText::FromString(SugCopy))
				// Selected → bold; both white. Bold makes the selection pop without an odd text color.
				.Font(bSelected ? BoldFont : NormalFont)
				.ColorAndOpacity(FSlateColor(FLinearColor::White))
			]
		];
	}

	Invalidate(EInvalidateWidgetReason::Layout);
}

void SImSlateVirtualKeyboard::MoveSuggestionSelection(int32 Delta)
{
	if (CurrentSuggestions.Num() == 0)
	{
		SelectedSuggestionIndex = -1;
		return;
	}
	const int32 Last = CurrentSuggestions.Num() - 1;
	int32 NewIdx;
	if (SelectedSuggestionIndex < 0 && Delta < 0)
	{
		// Nothing selected yet + Up → wrap up into the LAST item (so the first Up press selects
		// something instead of staying unselected). Down from nothing still goes to the first item.
		NewIdx = Last;
	}
	else
	{
		// Range is [-1, Last]: -1 = nothing selected. Clamp (no wrap) at the ends.
		NewIdx = FMath::Clamp(SelectedSuggestionIndex + Delta, -1, Last);
	}
	if (NewIdx != SelectedSuggestionIndex)
	{
		SelectedSuggestionIndex = NewIdx;
		// Rebuild the bar to repaint the highlight (cheap — at most MaxItems buttons). Pass a COPY:
		// PopulateSuggestionBar resets CurrentSuggestions before refilling from its argument, so
		// passing the member directly would clear it mid-read.
		const TArray<FString> Copy = CurrentSuggestions;
		PopulateSuggestionBar(Copy);
	}
}

// ==================== Input Handling ====================

void SImSlateVirtualKeyboard::OnKeyInput(const FVirtualKeyDef& KeyDef, const FString& InputValue)
{
	FString Text = InputValue;
	if (IsUpperCase() && Text.Len() == 1 && FChar::IsAlpha(Text[0]))
		Text = Text.ToUpper();
	InsertText(Text);

	if (ShiftState == EShiftState::SingleShot)
		ShiftState = EShiftState::Default;

	UpdatePreview();
	UpdateSuggestions();
}

void SImSlateVirtualKeyboard::OnKeyAction(EVirtualKeyAction Action)
{
	switch (Action)
	{
	case EVirtualKeyAction::Backspace:
		if (CursorPosition > 0)
		{
			BackspaceUndoBuffer += CurrentText[CursorPosition - 1];
			DeleteBackward();
			UpdatePreview();
			UpdateSuggestions();
		}
		break;
	case EVirtualKeyAction::Shift:
		ToggleShift();
		break;
	case EVirtualKeyAction::CapsLock:
		ShiftState = (ShiftState == EShiftState::Locked) ? EShiftState::Default : EShiftState::Locked;
		break;
	case EVirtualKeyAction::Enter:
		Hide(true);
		break;
	case EVirtualKeyAction::Cancel:
		Hide(false);
		break;
	case EVirtualKeyAction::Space:
		InsertText(TEXT(" "));
		UpdatePreview();
		UpdateSuggestions();
		break;
	case EVirtualKeyAction::Left:
		MoveCursor(-1);
		UpdatePreview();
		break;
	case EVirtualKeyAction::Right:
		MoveCursor(1);
		UpdatePreview();
		break;
	case EVirtualKeyAction::UndoBackspace:
		if (BackspaceUndoBuffer.Len() > 0)
		{
			FString Ch = BackspaceUndoBuffer.Right(1);
			BackspaceUndoBuffer.RemoveAt(BackspaceUndoBuffer.Len() - 1);
			InsertText(Ch);
			UpdatePreview();
			UpdateSuggestions();
		}
		break;
	case EVirtualKeyAction::ToggleLayer:
		CurrentLayer = (CurrentLayer + 1) % 2;
		BuildKeyboard();
		break;
	case EVirtualKeyAction::ToggleType:
		KeyboardType = (KeyboardType == EKeyboardType::QWERTY) ? EKeyboardType::T9 : EKeyboardType::QWERTY;
		CurrentLayer = 0;
		UpdateToggleTypeLabel();
		BuildKeyboard();
		break;
	default:
		break;
	}
}

FReply SImSlateVirtualKeyboard::OnKeyChar(const FGeometry& MyGeometry, const FCharacterEvent& InCharacterEvent)
{
	if (!bVisible)
		return FReply::Unhandled();

	const TCHAR Ch = InCharacterEvent.GetCharacter();
	// Only printable characters get inserted. Control chars (Backspace/Enter/Esc/Tab/etc.) and Space
	// are handled in OnKeyDown so we don't insert them twice. The OS already applied Shift/CapsLock
	// to produce the correct case, so insert the character verbatim — do NOT route through OnKeyInput
	// (its IsUpperCase()/ToUpper() would re-case it against the virtual keyboard's ShiftState).
	// Non-printables are still SWALLOWED (return Handled): the keyboard is modal while open, so no
	// char event may leak to the layer underneath.
	if (Ch < 32 || Ch == 127 || Ch == TEXT(' '))
		return FReply::Handled();

	InsertText(FString::Chr(Ch));
	UpdatePreview();
	UpdateSuggestions();
	return FReply::Handled();
}

FReply SImSlateVirtualKeyboard::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (!bVisible)
		return FReply::Unhandled();

	const FKey Key = InKeyEvent.GetKey();

	// Ctrl+V paste: no EVirtualKeyAction for paste, so read the clipboard and reuse the insert path.
	if (Key == EKeys::V && InKeyEvent.IsControlDown())
	{
		FString Clip;
		FPlatformApplicationMisc::ClipboardPaste(Clip);
		// Single-line: strip newlines so a multi-line clipboard doesn't break the preview.
		Clip.ReplaceInline(TEXT("\r\n"), TEXT(" "));
		Clip.ReplaceInline(TEXT("\r"), TEXT(" "));
		Clip.ReplaceInline(TEXT("\n"), TEXT(" "));
		if (!Clip.IsEmpty())
		{
			InsertText(Clip);
			UpdatePreview();
			UpdateSuggestions();
		}
		return FReply::Handled();
	}

	// Up/Down navigate the candidate (suggestion) list when there is one.
	if (Key == EKeys::Down)             { MoveSuggestionSelection(+1); return FReply::Handled(); }
	if (Key == EKeys::Up)               { MoveSuggestionSelection(-1); return FReply::Handled(); }

	// Enter: if a candidate is highlighted (Up/Down picked one), commit THAT word; otherwise the
	// usual Done (commit the typed text).
	if (Key == EKeys::Enter)
	{
		if (CurrentSuggestions.IsValidIndex(SelectedSuggestionIndex))
			OnSuggestionClicked(CurrentSuggestions[SelectedSuggestionIndex]);
		else
			OnKeyAction(EVirtualKeyAction::Enter);
		return FReply::Handled();
	}

	// Map physical keys onto the SAME actions as the on-screen keys (OnKeyAction → UpdatePreview
	// → bound editable + suggestions stay in sync).
	if (Key == EKeys::Escape)           { OnKeyAction(EVirtualKeyAction::Cancel);    return FReply::Handled(); }
	if (Key == EKeys::BackSpace)        { OnKeyAction(EVirtualKeyAction::Backspace); return FReply::Handled(); }
	if (Key == EKeys::Left)             { OnKeyAction(EVirtualKeyAction::Left);      return FReply::Handled(); }
	if (Key == EKeys::Right)            { OnKeyAction(EVirtualKeyAction::Right);     return FReply::Handled(); }
	if (Key == EKeys::SpaceBar)         { OnKeyAction(EVirtualKeyAction::Space);     return FReply::Handled(); }

	// While the keyboard is open it is the focused, modal input target: SWALLOW every remaining key
	// (F-keys, letters' raw KeyDown, etc.) so nothing leaks to the game / editor viewport shortcuts
	// underneath. (Printable chars are produced via OnKeyChar; this only governs KeyDown routing.)
	return FReply::Handled();
}

FReply SImSlateVirtualKeyboard::OnKeyUp(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	// Modal while open: swallow ALL key-up events too, so the paired up of any key (whose down we
	// swallowed) can't trigger a viewport shortcut underneath that fires on release.
	if (!bVisible)
		return FReply::Unhandled();
	return FReply::Handled();
}

void SImSlateVirtualKeyboard::OnKeyLongPress(const FVirtualKeyDef& KeyDef, const FGeometry& KeyGeometry)
{
	if (KeyDef.LongPressChars.Num() == 0 || !RootOverlay.IsValid()) return;
	DismissPopup();

	ActivePopup = SNew(SImSlateKeyPopup)
		.Chars(KeyDef.LongPressChars);

	// Position popup above the key, clamped to keyboard bounds (local coords)
	FVector2D KeyLocalPos = GetCachedGeometry().AbsoluteToLocal(KeyGeometry.GetAbsolutePosition());
	FVector2D KeyLocalSize = KeyGeometry.GetLocalSize();
	FVector2D MySize = GetCachedGeometry().GetLocalSize();
	float PopupPadding = 8.f;
	float CellW = ActivePopup->GetCellWidth() + 2.f;
	float PopupWidth = KeyDef.LongPressChars.Num() * CellW + PopupPadding * 2.f;
	float KeyCenterX = KeyLocalPos.X + KeyLocalSize.X * 0.5f;
	float PopupX = KeyCenterX - PopupWidth * 0.5f;
	PopupX = FMath::Clamp(PopupX, 0.f, FMath::Max(0.f, MySize.X - PopupWidth));
	// Place the popup fully ABOVE the key: offset by the popup's own height + a gap,
	// not a fixed 50 (which left the popup overlapping the key row).
	float PopupHeight = ActivePopup->GetDesiredSize().Y;
	if (PopupHeight <= 0.f)
		PopupHeight = 48.f * GetKbScale();  // fallback before first layout
	float PopupY = KeyLocalPos.Y - PopupHeight - 6.f;
	PopupY = FMath::Max(0.f, PopupY);

	RootOverlay->AddSlot()
	.HAlign(HAlign_Left)
	.VAlign(VAlign_Top)
	.Padding(FMargin(PopupX, PopupY, 0.f, 0.f))
	[
		ActivePopup.ToSharedRef()
	];

	// Default selection is the MIDDLE entry — matches the finger-at-center index that
	// HandleRelease computes (round(0/CellW)+HalfCount = Count/2) when released without moving.
	ActivePopup->SetHighlightIndex(KeyDef.LongPressChars.Num() / 2);

	// Tell the key where the popup center is so it can compute correct indices.
	// IMPORTANT: the key compares against ScreenPos (ABSOLUTE screen space), so BOTH the
	// anchor X and the cell width must be in absolute space. PopupX/CellW are local (unscaled),
	// so multiply by DPIScale. Previously CellW was passed unscaled → XOffset(abs)/CellW(local)
	// mismatched by the DPI factor → the selection jumped multiple cells per cell of movement.
	FVector2D MyAbsPos = GetCachedGeometry().GetAbsolutePosition();
	float DPIScale = GetCachedGeometry().GetAccumulatedLayoutTransform().GetScale();
	float PopupCenterAbsX = MyAbsPos.X + (PopupX + PopupWidth * 0.5f) * DPIScale;
	float CellWAbs = CellW * DPIScale;
	for (auto& Entry : KeyWidgets)
	{
		if (Entry.Def == &KeyDef && Entry.Widget.IsValid())
		{
			Entry.Widget->SetLongPressPopupInfo(PopupCenterAbsX, CellWAbs, KeyDef.LongPressChars.Num());
			ActiveLongPressKey = Entry.Widget;
			break;
		}
	}
}

void SImSlateVirtualKeyboard::OnKeyLongPressMove(int32 HighlightIndex)
{
	if (ActivePopup.IsValid())
		ActivePopup->SetHighlightIndex(HighlightIndex);
}

void SImSlateVirtualKeyboard::OnKeyLongPressEnd(int32 SelectedIndex)
{
	if (ActivePopup.IsValid())
	{
		FString SelectedChar = ActivePopup->GetCharAt(SelectedIndex);
		if (!SelectedChar.IsEmpty())
		{
			InsertText(SelectedChar);
			UpdatePreview();
			UpdateSuggestions();
		}
	}
	DismissPopup();
}

void SImSlateVirtualKeyboard::OnKeyPressVisual(const FVirtualKeyDef& KeyDef, const FGeometry& KeyGeometry, bool bForceStepDrag)
{
	if (!RootOverlay.IsValid()) return;
	OnKeyReleaseVisual();

	// Step-drag hint when: Space/Del (always), OR the caller forced it (Done's horizontal drag).
	// Done's VERTICAL swipe passes bForceStepDrag=false → falls through to the four-way popup.
	bool bIsStepDrag = bForceStepDrag
		|| KeyDef.Action == EVirtualKeyAction::Space
		|| KeyDef.Action == EVirtualKeyAction::Backspace;

	if (bIsStepDrag)
	{
		float Scale = GetKbScale();
		FSlateFontInfo Font = GetImSlateDefaultFont(7);
		FLinearColor Dim(0.5f, 0.5f, 0.5f, 1.f);

		bool bIsSpace = (KeyDef.Action == EVirtualKeyAction::Space);
		FString LeftLabel = bIsSpace ? TEXT("\x25C0 Cursor") : TEXT("\x25C0 Del");
		FString RightLabel = bIsSpace ? TEXT("Cursor \x25B6") : TEXT("Undo \x25B6");

		// Position the popup in RootOverlay's OWN coordinate space (it is the popup's parent).
		// Using this widget's geometry instead caused drift: RootOverlay's origin moves as the
		// suggestion row count changes the layout, so converting via the popup's actual parent
		// keeps the popup pinned to the key's absolute position regardless of that drift.
		const FGeometry& OverlayGeo = RootOverlay->GetCachedGeometry();
		FVector2D KeyLocalPos = OverlayGeo.AbsoluteToLocal(KeyGeometry.GetAbsolutePosition());
		FVector2D KeyLocalSize = KeyGeometry.GetLocalSize();
		FVector2D MySize = OverlayGeo.GetLocalSize();
		// Width must fit the actual text (left + separator + right + paddings), not just scale
		// off the key width — a narrow key (e.g. Del) was clipping "◀ Del | Undo ▶".
		TSharedRef<FSlateFontMeasure> FM = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
		float TextW = (float)FM->Measure(LeftLabel, Font).X + (float)FM->Measure(RightLabel, Font).X;
		float SepAndPad = (float)FM->Measure(TEXT("\x2502"), Font).X + 16.f * Scale /*sep slot padding*/ + 20.f * Scale /*border padding*/;
		float PopupWidth = FMath::Max(KeyLocalSize.X * 1.2f, TextW + SepAndPad);
		float PopupX = KeyLocalPos.X + KeyLocalSize.X * 0.5f - PopupWidth * 0.5f;
		PopupX = FMath::Clamp(PopupX, 0.f, FMath::Max(0.f, MySize.X - PopupWidth));
		float PopupY = KeyLocalPos.Y - 32.f;
		PopupY = FMath::Max(0.f, PopupY);

		StepDragVisual = SNew(SBox)
			.WidthOverride(PopupWidth)
			[
				SNew(SBox)
				.HeightOverride(28.f * GetKbScale())
				[
					SNew(SBorder)
					.BorderImage(&GetPopupBgBrush())
					.Padding(FMargin(10.f, 4.f))
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1.f).HAlign(HAlign_Left).VAlign(VAlign_Center)
						[ SAssignNew(StepDragLeftText, STextBlock).Text(FText::FromString(LeftLabel)).Font(Font).ColorAndOpacity(Dim) ]
						+ SHorizontalBox::Slot().AutoWidth().HAlign(HAlign_Center).VAlign(VAlign_Center).Padding(FMargin(8.f, 0.f))
						[ SNew(STextBlock).Text(FText::FromString(TEXT("\x2502"))).Font(Font).ColorAndOpacity(FLinearColor(0.4f, 0.4f, 0.4f)) ]
						+ SHorizontalBox::Slot().FillWidth(1.f).HAlign(HAlign_Right).VAlign(VAlign_Center)
						[ SAssignNew(StepDragRightText, STextBlock).Text(FText::FromString(RightLabel)).Font(Font).ColorAndOpacity(Dim) ]
					]
				]
			];

		RootOverlay->AddSlot()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Top)
		.Padding(FMargin(PopupX, PopupY, 0.f, 0.f))
		[ StepDragVisual.ToSharedRef() ];
		return;
	}

	if (!KeyDef.Swipe.HasAny()) return;
	ActiveKeyDef = &KeyDef;

	float Scale = GetKbScale();
	FSlateFontInfo Font = GetImSlateDefaultFont(12);  // slightly smaller than before (was 14) to fit cells snugly
	FLinearColor HintColor(0.9f, 0.9f, 0.9f, 1.f);

	auto ApplyCase = [this](const FString& S) -> FString {
		if (S.Len() == 1 && FChar::IsAlpha(S[0]))
			return IsUpperCase() ? S.ToUpper() : S.ToLower();
		return S;
	};

	const FString UpText    = ApplyCase(KeyDef.Swipe.Up.Label);
	const FString DownText  = ApplyCase(KeyDef.Swipe.Down.Label);
	const FString LeftText  = ApplyCase(KeyDef.Swipe.Left.Label);
	const FString RightText = ApplyCase(KeyDef.Swipe.Right.Label);
	const FString CenterText = KeyDef.Value.IsEmpty() ? KeyDef.GetDisplayLabel(IsUpperCase()) : KeyDef.Value;

	// UNIFIED RULE: build the popup as rows × columns, omitting any empty row/column so the
	// popup hugs exactly the cells that exist. CellSize is a font-based square constant
	// (independent of key geometry). Each cell has a uniform minimum size, widened only if
	// its own text is longer (so e.g. "Done"/"Esc" are never clipped). No empty thirds, no
	// empty rows: up-only-and-center → 2 rows; full four-way → 3 rows × 3 cols.
	TSharedRef<FSlateFontMeasure> FM = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const float GlyphH = (float)FM->GetMaxCharacterHeight(Font);
	const float CellSize = GlyphH + 10.f * Scale;     // uniform square minimum
	const float CellPadX = 12.f * Scale;              // horizontal breathing room for wide labels

	const bool bHasUp    = !UpText.IsEmpty();
	const bool bHasDown  = !DownText.IsEmpty();
	const bool bHasLeft  = !LeftText.IsEmpty();
	const bool bHasRight = !RightText.IsEmpty();

	SwipeDirectionTexts.Reset();

	// A single cell: fixed height, width = max(CellSize, text + padding) so it never clips.
	auto MakeCell = [&](const FString& Text, const FString& DirKey) -> TSharedRef<SWidget> {
		float W = FMath::Max(CellSize, (float)FM->Measure(Text, Font).X + CellPadX);
		TSharedPtr<STextBlock> Tb;
		TSharedRef<SBox> Box = SNew(SBox)
			.WidthOverride(W)
			.HeightOverride(CellSize)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SAssignNew(Tb, STextBlock)
				.Text(FText::FromString(Text))
				.Font(Font)
				.ColorAndOpacity(HintColor)
				.Justification(ETextJustify::Center)
			];
		if (!DirKey.IsEmpty())
			SwipeDirectionTexts.Add(DirKey, Tb);
		return Box;
	};
	// An empty placeholder cell (keeps a column aligned when only one side exists).
	auto MakeSpacer = [&]() -> TSharedRef<SWidget> {
		return SNew(SBox).WidthOverride(CellSize).HeightOverride(CellSize);
	};

	TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);

	// Up row (only if Up exists) — centered over the middle column.
	if (bHasUp)
	{
		Rows->AddSlot().AutoHeight().HAlign(HAlign_Center)
		[ MakeCell(UpText, TEXT("U")) ];
	}

	// Middle row: [Left] Center [Right]. Spacers keep Center centered when one side is empty.
	{
		TSharedRef<SHorizontalBox> Mid = SNew(SHorizontalBox);
		if (bHasLeft || bHasRight)
			Mid->AddSlot().AutoWidth()[ bHasLeft ? MakeCell(LeftText, TEXT("L")) : MakeSpacer() ];
		Mid->AddSlot().AutoWidth()[ MakeCell(CenterText, TEXT("C")) ];
		if (bHasLeft || bHasRight)
			Mid->AddSlot().AutoWidth()[ bHasRight ? MakeCell(RightText, TEXT("R")) : MakeSpacer() ];
		Rows->AddSlot().AutoHeight().HAlign(HAlign_Center)[ Mid ];
	}

	// Down row (only if Down exists).
	if (bHasDown)
	{
		Rows->AddSlot().AutoHeight().HAlign(HAlign_Center)
		[ MakeCell(DownText, TEXT("D")) ];
	}

	TSharedRef<SWidget> Grid = Rows;
	// Popup auto-sizes to its content (SBorder wraps the rows); compute extents for placement.
	const int32 NumRows = 1 + (bHasUp ? 1 : 0) + (bHasDown ? 1 : 0);
	const int32 NumCols = (bHasLeft || bHasRight) ? 3 : 1;
	const float GridH = CellSize * NumRows;
	// Popup auto-sizes to its rows (no forced override), so a wide Center label (e.g. "Done")
	// is fully contained. Border padding adds to the size.
	const float BorderPad = 2.f * Scale;
	SwipeVisual = SNew(SBorder)
		.BorderImage(&GetPopupBgBrush())
		.Padding(BorderPad)
		[ Grid ];

	// Estimate popup extents for placement (rows/cols * cell + border). Center column may be
	// wider than CellSize for long labels, so use the measured center width as a floor.
	float CenterW = FMath::Max(CellSize, (float)FM->Measure(CenterText, Font).X + CellPadX);
	float ContentW = (NumCols == 3) ? (CellSize * 2.f + CenterW) : CenterW;
	float PopupW = ContentW + BorderPad * 2.f;
	float PopupH = GridH + BorderPad * 2.f;

	FVector2D KeyLocalPos = GetCachedGeometry().AbsoluteToLocal(KeyGeometry.GetAbsolutePosition());
	FVector2D KeyLocalSize = KeyGeometry.GetLocalSize();
	FVector2D MySize = GetCachedGeometry().GetLocalSize();
	float PopupX = KeyLocalPos.X + KeyLocalSize.X * 0.5f - PopupW * 0.5f;
	PopupX = FMath::Clamp(PopupX, 0.f, FMath::Max(0.f, MySize.X - PopupW));
	float PopupY = KeyLocalPos.Y - PopupH - 4.f;
	PopupY = FMath::Max(0.f, PopupY);

	RootOverlay->AddSlot()
	.HAlign(HAlign_Left)
	.VAlign(VAlign_Top)
	.Padding(FMargin(PopupX, PopupY, 0.f, 0.f))
	[ SwipeVisual.ToSharedRef() ];
}

void SImSlateVirtualKeyboard::OnKeyMoveVisual(const FVector2D& Delta, bool bSwipeReady)
{
	if (StepDragVisual.IsValid())
	{
		FLinearColor Dim(0.5f, 0.5f, 0.5f, 1.f);
		FLinearColor Active(0.3f, 0.8f, 1.f, 1.f);
		bool bLeft = Delta.X < -4.f;
		bool bRight = Delta.X > 4.f;
		if (StepDragLeftText.IsValid())  StepDragLeftText->SetColorAndOpacity(bLeft ? Active : Dim);
		if (StepDragRightText.IsValid()) StepDragRightText->SetColorAndOpacity(bRight ? Active : Dim);
		return;
	}

	if (SwipeVisual.IsValid())
	{
		float DPIScale = GetCachedGeometry().GetAccumulatedLayoutTransform().GetScale();
		float ScaledY = (DPIScale > 0.f) ? (float)Delta.Y / DPIScale : (float)Delta.Y;
		float ClampedY = FMath::Min(ScaledY, 0.f);
		SwipeVisual->SetRenderTransform(FSlateRenderTransform(FVector2f(0.f, ClampedY)));
	}

	FString ActiveDir = TEXT("C");
	if (bSwipeReady && ActiveKeyDef)
	{
		const FVirtualKeySwipeEntry* TestEntry = nullptr;
		if (FMath::Abs(Delta.X) > FMath::Abs(Delta.Y))
		{
			ActiveDir = Delta.X > 0 ? TEXT("R") : TEXT("L");
			TestEntry = Delta.X > 0 ? &ActiveKeyDef->Swipe.Right : &ActiveKeyDef->Swipe.Left;
		}
		else
		{
			ActiveDir = Delta.Y < 0 ? TEXT("U") : TEXT("D");
			TestEntry = Delta.Y < 0 ? &ActiveKeyDef->Swipe.Up : &ActiveKeyDef->Swipe.Down;
		}
		if (!TestEntry || !TestEntry->IsSet())
			ActiveDir = TEXT("C");
	}

	FLinearColor Normal(0.7f, 0.7f, 0.7f, 1.f);
	FLinearColor Highlight(0.3f, 0.8f, 1.f, 1.f);
	for (auto& [Dir, Text] : SwipeDirectionTexts)
	{
		if (Text.IsValid())
			Text->SetColorAndOpacity(Dir == ActiveDir ? Highlight : Normal);
	}
}

void SImSlateVirtualKeyboard::OnKeyReleaseVisual()
{
	if (StepDragVisual.IsValid() && RootOverlay.IsValid())
	{
		RootOverlay->RemoveSlot(StepDragVisual.ToSharedRef());
		StepDragVisual.Reset();
		StepDragLeftText.Reset();
		StepDragRightText.Reset();
	}
	if (SwipeVisual.IsValid() && RootOverlay.IsValid())
	{
		RootOverlay->RemoveSlot(SwipeVisual.ToSharedRef());
		SwipeVisual.Reset();
	}
	SwipeDirectionTexts.Reset();
	ActiveKeyDef = nullptr;
}

void SImSlateVirtualKeyboard::OnSpaceCursorZone(int32 Direction)
{
	AutoScrollDirection = Direction;

	if (Direction != 0 && !AutoScrollTimer.IsValid())
	{
		AutoScrollTimer = RegisterActiveTimer(0.08f, FWidgetActiveTimerDelegate::CreateLambda(
			[this](double, float) -> EActiveTimerReturnType {
				if (AutoScrollDirection != 0)
				{
					MoveCursor(AutoScrollDirection);
					UpdatePreview();
					return EActiveTimerReturnType::Continue;
				}
				AutoScrollTimer.Reset();
				return EActiveTimerReturnType::Stop;
			}));
	}
	else if (Direction == 0 && AutoScrollTimer.IsValid())
	{
		auto Timer = AutoScrollTimer;
		AutoScrollTimer.Reset();
		UnRegisterActiveTimer(Timer.ToSharedRef());
	}
}

void SImSlateVirtualKeyboard::DismissPopup()
{
	if (ActivePopup.IsValid() && RootOverlay.IsValid())
	{
		RootOverlay->RemoveSlot(ActivePopup.ToSharedRef());
		ActivePopup.Reset();
	}
}

void SImSlateVirtualKeyboard::OnSuggestionClicked(const FString& Value)
{
	CurrentText = Value;
	CursorPosition = CurrentText.Len();
	if (bSuggestionCommitsAndCloses)
	{
		Hide(true);  // replace + commit + close
	}
	else
	{
		// Replace only; keep the keyboard open for further editing.
		UpdatePreview();
		UpdateSuggestions();
	}
}

}  // namespace ImSlate

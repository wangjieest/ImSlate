// Copyright ImSlate, Inc. All Rights Reserved.
#include "ImSlateTemplate/ImVirtualKeyboard.h"
#include "ImSlateTemplate/ImVirtualKey.h"

#include "ImSlatePrivate.h"
#include "SImViewportGame.h"
#include "ImSlateTemplate/ImEditableText.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Text/TextLayout.h"
#include "Widgets/Input/SEditableText.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SOverlay.h"

namespace ImSlate
{

bool GForceVirtualKeyboard = false;
static FAutoConsoleVariableRef CVar_ForceVirtualKeyboard(
	TEXT("imslate.ForceVirtualKeyboard"),
	GForceVirtualKeyboard,
	TEXT("Force virtual keyboard on desktop for testing."));

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

bool SImSlateVirtualKeyboard::ShouldUseVirtualKeyboard()
{
	// Always use our virtual keyboard when an instance is available
	return Get().IsValid() || GForceVirtualKeyboard;
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

// ==================== Construct ====================

void SImSlateVirtualKeyboard::Construct(const FArguments& InArgs)
{
	SetVisibility(EVisibility::Collapsed);

	float Scale = GetImSlateEffectiveScale();

	// Preview-row keys: persistent KeyDefs (not rebuilt by BuildKeyboard).
	ToggleTypeKeyDef = MakeShared<FVirtualKeyDef>();
	ToggleTypeKeyDef->Action = EVirtualKeyAction::ToggleType;
	ToggleTypeKeyDef->Label = TEXT("T9");
	ToggleTypeKey = MakeBoundKey(ToggleTypeKeyDef.Get());

	DoneKeyDef = MakeShared<FVirtualKeyDef>();
	DoneKeyDef->Action = EVirtualKeyAction::Enter;
	DoneKeyDef->Label = TEXT("Done");
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

		+ SOverlay::Slot()
		[
			SNew(SVerticalBox)

			// Top spacer: absorbs remaining space
			+ SVerticalBox::Slot()
			.FillHeight(1.f)

			// Suggestions: scrollable, max 100px
			+ SVerticalBox::Slot()
			.AutoHeight()
			.MaxHeight(100.f)
			[
				SNew(SScrollBox)
				.Orientation(Orient_Vertical)
				+ SScrollBox::Slot()
				[
					SAssignNew(SuggestionBar, SWrapBox)
					.UseAllottedSize(true)
				]
			]

			// Preview row: [ToggleType] [preview text] [Done] (fixed above keyboard)
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
						.WidthOverride(54.f)
						.HeightOverride(30.f)
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
						.WidthOverride(64.f)
						.HeightOverride(30.f)
						[
							DoneKey.ToSharedRef()
						]
					]
				]
			]

			// Keyboard: auto height, max 300px, always at bottom
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SBox)
				.MaxDesiredHeight(300.f)
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
		]
	];

	if (PreviewEdit.IsValid())
		PreviewEdit->SetPreviewDisplayMode(true);

	BuildKeyboard();
}

static constexpr float GMaxKeyboardHeight = 300.f;
static constexpr float GMaxKeyHeight = 48.f;
static constexpr float GMinKeyWidth = 32.f;

SImSlateVirtualKeyboard::EKeyboardLayoutMode SImSlateVirtualKeyboard::ComputeLayoutMode(float Width) const
{
	// Pure function of width: same screen → same mode, independent of toggle history.
	if (Width <= 0.f)
		return EKeyboardLayoutMode::FullScreen;  // unknown yet — start collapsed

	float Scale = GetImSlateEffectiveScale();
	float KeyH = FMath::Min(32.f * Scale, GMaxKeyHeight);
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
	float Scale = GetImSlateEffectiveScale();
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
	float Scale = GetImSlateEffectiveScale();
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
	float SplitScale = GetImSlateEffectiveScale();
	float SplitKeyH = FMath::Min(32.f * SplitScale, GMaxKeyHeight);
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
		.OnPressVisual_Lambda([this](const FVirtualKeyDef& Def, const FGeometry& Geo) { OnKeyPressVisual(Def, Geo); })
		.OnMoveVisual_Lambda([this](const FVector2D& Delta, bool bSwipeReady) { OnKeyMoveVisual(Delta, bSwipeReady); })
		.OnReleaseVisual_Lambda([this]() { OnKeyReleaseVisual(); })
		.OnSpaceCursorZone_Lambda([this](int32 Dir) { OnSpaceCursorZone(Dir); });
	return KeyWidget;
}

void SImSlateVirtualKeyboard::BuildKeyRow(TSharedRef<SHorizontalBox> RowBox, const TArray<FVirtualKeyDef>& Keys)
{
	float Scale = GetImSlateEffectiveScale();

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
				bCursorVisible = !bCursorVisible;
				if (PreviewEdit.IsValid())
					PreviewEdit->SetPreviewCaretVisible(bCursorVisible);  // caret is drawn by PreviewEdit
				Invalidate(EInvalidateWidgetReason::Paint);  // repaint subtree (incl. PreviewEdit)
				return EActiveTimerReturnType::Continue;
			}));
	}

	UpdatePreview();
	UpdateSuggestions();
}

void SImSlateVirtualKeyboard::Hide(bool bCommit)
{
	if (!bVisible) return;

	bVisible = false;
	SetVisibility(EVisibility::Collapsed);

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

void SImSlateVirtualKeyboard::SyncFromEditor(const FString& Text)
{
	if (!bVisible || CurrentText == Text) return;
	CurrentText = Text;
	CursorPosition = FMath::Clamp(CursorPosition, 0, CurrentText.Len());
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
	float Step = 12.f * GetImSlateEffectiveScale();
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

	float Scale = GetImSlateEffectiveScale();
	int32 MaxItems = 12;
	for (int32 i = 0; i < FMath::Min(Suggestions.Num(), MaxItems); ++i)
	{
		FString SugCopy = Suggestions[i];
		SuggestionBar->AddSlot()
		.Padding(2.f * Scale)
		[
			SNew(SButton)
			.OnClicked_Lambda([this, SugCopy]() { OnSuggestionClicked(SugCopy); return FReply::Handled(); })
			[
				SNew(STextBlock)
				.Text(FText::FromString(SugCopy))
				.Font(GetImSlateDefaultFont(8))
			]
		];
	}

	Invalidate(EInvalidateWidgetReason::Layout);
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
	float PopupY = KeyLocalPos.Y - 50.f;
	PopupY = FMath::Max(0.f, PopupY);

	RootOverlay->AddSlot()
	.HAlign(HAlign_Left)
	.VAlign(VAlign_Top)
	.Padding(FMargin(PopupX, PopupY, 0.f, 0.f))
	[
		ActivePopup.ToSharedRef()
	];

	int32 DefaultIdx = KeyDef.LongPressChars.Find(KeyDef.Value);
	ActivePopup->SetHighlightIndex(DefaultIdx >= 0 ? DefaultIdx : KeyDef.LongPressChars.Num() / 2);

	// Tell the key where the popup center is so it can compute correct indices
	FVector2D MyAbsPos = GetCachedGeometry().GetAbsolutePosition();
	float DPIScale = GetCachedGeometry().GetAccumulatedLayoutTransform().GetScale();
	float PopupCenterAbsX = MyAbsPos.X + (PopupX + PopupWidth * 0.5f) * DPIScale;
	for (auto& Entry : KeyWidgets)
	{
		if (Entry.Def == &KeyDef && Entry.Widget.IsValid())
		{
			Entry.Widget->SetLongPressPopupInfo(PopupCenterAbsX, CellW, KeyDef.LongPressChars.Num());
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

void SImSlateVirtualKeyboard::OnKeyPressVisual(const FVirtualKeyDef& KeyDef, const FGeometry& KeyGeometry)
{
	if (!RootOverlay.IsValid()) return;
	OnKeyReleaseVisual();

	bool bIsStepDrag = (KeyDef.Action == EVirtualKeyAction::Space || KeyDef.Action == EVirtualKeyAction::Backspace);

	if (bIsStepDrag)
	{
		float Scale = GetImSlateEffectiveScale();
		FSlateFontInfo Font = GetImSlateDefaultFont(7);
		FLinearColor Dim(0.5f, 0.5f, 0.5f, 1.f);

		bool bIsSpace = (KeyDef.Action == EVirtualKeyAction::Space);
		FString LeftLabel = bIsSpace ? TEXT("\x25C0 Cursor") : TEXT("\x25C0 Del");
		FString RightLabel = bIsSpace ? TEXT("Cursor \x25B6") : TEXT("Undo \x25B6");

		FVector2D KeyLocalPos = GetCachedGeometry().AbsoluteToLocal(KeyGeometry.GetAbsolutePosition());
		FVector2D KeyLocalSize = KeyGeometry.GetLocalSize();
		FVector2D MySize = GetCachedGeometry().GetLocalSize();
		float PopupWidth = FMath::Min(KeyLocalSize.X * 1.2f, 200.f);
		float PopupX = KeyLocalPos.X + KeyLocalSize.X * 0.5f - PopupWidth * 0.5f;
		PopupX = FMath::Clamp(PopupX, 0.f, FMath::Max(0.f, MySize.X - PopupWidth));
		float PopupY = KeyLocalPos.Y - 32.f;
		PopupY = FMath::Max(0.f, PopupY);

		StepDragVisual = SNew(SBox)
			.WidthOverride(PopupWidth)
			[
				SNew(SBox)
				.HeightOverride(28.f)
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

	float Scale = GetImSlateEffectiveScale();
	float CellSize = FMath::Min(KeyGeometry.GetLocalSize().X, KeyGeometry.GetLocalSize().Y);
	FSlateFontInfo Font = GetImSlateDefaultFont(14);
	FLinearColor HintColor(0.9f, 0.9f, 0.9f, 1.f);

	TSharedRef<SOverlay> Grid = SNew(SOverlay);
	SwipeDirectionTexts.Reset();

	auto AddCell = [&](const FString& Text, EHorizontalAlignment HAlign, EVerticalAlignment VAlign, const FString& DirKey = FString()) {
		if (Text.IsEmpty()) return;
		TSharedPtr<STextBlock> Tb;
		Grid->AddSlot()
		.HAlign(HAlign)
		.VAlign(VAlign)
		.Padding(FMargin(4.f * Scale))
		[
			SAssignNew(Tb, STextBlock)
			.Text(FText::FromString(Text))
			.Font(Font)
			.ColorAndOpacity(HintColor)
		];
		if (!DirKey.IsEmpty())
			SwipeDirectionTexts.Add(DirKey, Tb);
	};

	auto ApplyCase = [this](const FString& S) -> FString {
		if (S.Len() == 1 && FChar::IsAlpha(S[0]))
			return IsUpperCase() ? S.ToUpper() : S.ToLower();
		return S;
	};
	AddCell(ApplyCase(KeyDef.Swipe.Up.Label), HAlign_Center, VAlign_Top, TEXT("U"));
	AddCell(ApplyCase(KeyDef.Swipe.Down.Label), HAlign_Center, VAlign_Bottom, TEXT("D"));
	AddCell(ApplyCase(KeyDef.Swipe.Left.Label), HAlign_Left, VAlign_Center, TEXT("L"));
	AddCell(ApplyCase(KeyDef.Swipe.Right.Label), HAlign_Right, VAlign_Center, TEXT("R"));
	AddCell(KeyDef.Value.IsEmpty() ? KeyDef.GetDisplayLabel(IsUpperCase()) : KeyDef.Value, HAlign_Center, VAlign_Center, TEXT("C"));

	float GridSize = CellSize * 3.f;
	SwipeVisual = SNew(SBox)
		.WidthOverride(GridSize)
		.HeightOverride(GridSize)
		[
			SNew(SBorder)
			.BorderImage(&GetPopupBgBrush())
			.Padding(2.f * Scale)
			[ Grid ]
		];

	FVector2D KeyLocalPos = GetCachedGeometry().AbsoluteToLocal(KeyGeometry.GetAbsolutePosition());
	FVector2D KeyLocalSize = KeyGeometry.GetLocalSize();
	FVector2D MySize = GetCachedGeometry().GetLocalSize();
	float PopupX = KeyLocalPos.X + KeyLocalSize.X * 0.5f - GridSize * 0.5f;
	PopupX = FMath::Clamp(PopupX, 0.f, FMath::Max(0.f, MySize.X - GridSize));
	float PopupY = KeyLocalPos.Y - GridSize - 4.f;
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
	Hide(true);
}

}  // namespace ImSlate

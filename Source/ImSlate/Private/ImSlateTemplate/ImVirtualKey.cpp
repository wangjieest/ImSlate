// Copyright ImSlate, Inc. All Rights Reserved.
#include "ImSlateTemplate/ImVirtualKey.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"

namespace ImSlate
{

static FSlateBrush& GetKeyBrush(bool bPressed)
{
	static FSlateBrush NormalBrush;
	static FSlateBrush PressedBrush;
	static bool bInit = false;
	if (!bInit)
	{
		auto InitBrush = [](FSlateBrush& Brush, const FLinearColor& Color) {
			Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
			Brush.TintColor = Color;
			Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
			Brush.OutlineSettings.CornerRadii = FVector4(6, 6, 6, 6);
		};
		InitBrush(NormalBrush, FLinearColor(0.2f, 0.2f, 0.2f, 0.95f));
		InitBrush(PressedBrush, FLinearColor(0.4f, 0.4f, 0.4f, 1.f));
		bInit = true;
	}
	return bPressed ? PressedBrush : NormalBrush;
}

// ==================== SImSlateKey ====================

void SImSlateKey::Construct(const FArguments& InArgs)
{
	KeyDef = InArgs._KeyDef;
	bShiftActive = InArgs._bShiftActive;
	bShiftSingleShot = InArgs._bShiftSingleShot;
	OnKeyInput = InArgs._OnKeyInput;
	OnKeyAction = InArgs._OnKeyAction;
	OnLongPress = InArgs._OnLongPress;
	OnLongPressMove = InArgs._OnLongPressMove;
	OnLongPressEnd = InArgs._OnLongPressEnd;
	OnPressVisual = InArgs._OnPressVisual;
	OnMoveVisual = InArgs._OnMoveVisual;
	OnReleaseVisual = InArgs._OnReleaseVisual;
	OnSpaceCursorZone = InArgs._OnSpaceCursorZone;
}

void SImSlateKey::UpdateKeyDef(const FVirtualKeyDef* InKeyDef)
{
	KeyDef = InKeyDef;
}

void SImSlateKey::SetLongPressPopupInfo(float PopupCenterAbsX, float InCellWidth, int32 InCharCount)
{
	LongPressAnchorPos.X = PopupCenterAbsX;
	LongPressCellWidth = InCellWidth;
	LongPressCharCount = InCharCount;
}

FVector2D SImSlateKey::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
	float Scale = GetImSlateEffectiveScale();
	float H = FMath::Min(32.f * Scale, 48.f); // capped at GMaxKeyHeight
	float W = H * 0.85f;
	if (KeyDef)
		W *= KeyDef->WidthMultiplier;
	return FVector2D(W, H);
}

int32 SImSlateKey::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	if (!KeyDef) return LayerId;

	const FVector2D LocalSize = AllottedGeometry.GetLocalSize();
	const bool bShift = bShiftActive.Get(false);

	// Background
	FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
		AllottedGeometry.ToPaintGeometry(),
		&GetKeyBrush(bIsPressed),
		ESlateDrawEffect::None,
		GetKeyBrush(bIsPressed).TintColor.GetSpecifiedColor());

	TSharedRef<FSlateFontMeasure> FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	FVector2D Center = LocalSize * 0.5f;
	float IconSize = FMath::Min(LocalSize.X, LocalSize.Y) * 0.35f;
	FLinearColor IconColor = FLinearColor::White;

	bool bDrawnIcon = false;
	if (KeyDef->Action == EVirtualKeyAction::Backspace)
	{
		// ← with X shape
		TArray<FVector2D> Points;
		float W = IconSize * 1.2f, H = IconSize * 0.8f;
		Points.Add(Center + FVector2D(-W, 0));
		Points.Add(Center + FVector2D(-W * 0.3f, -H));
		Points.Add(Center + FVector2D(W, -H));
		Points.Add(Center + FVector2D(W, H));
		Points.Add(Center + FVector2D(-W * 0.3f, H));
		Points.Add(Center + FVector2D(-W, 0));
		FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 1, AllottedGeometry.ToPaintGeometry(), Points, ESlateDrawEffect::None, IconColor, true, 1.5f);
		bDrawnIcon = true;
	}
	else if (KeyDef->Action == EVirtualKeyAction::Shift)
	{
		float S = IconSize;
		bool bSingleShot = bShiftSingleShot.Get(false);
		FVector2D ArrowCenter = bSingleShot ? FVector2D(Center.X, Center.Y - S * 0.15f) : Center;

		TArray<FVector2D> Points;
		Points.Add(ArrowCenter + FVector2D(0, -S));
		Points.Add(ArrowCenter + FVector2D(S * 0.8f, S * 0.2f));
		Points.Add(ArrowCenter + FVector2D(S * 0.3f, S * 0.2f));
		Points.Add(ArrowCenter + FVector2D(S * 0.3f, S * 0.7f));
		Points.Add(ArrowCenter + FVector2D(-S * 0.3f, S * 0.7f));
		Points.Add(ArrowCenter + FVector2D(-S * 0.3f, S * 0.2f));
		Points.Add(ArrowCenter + FVector2D(-S * 0.8f, S * 0.2f));
		Points.Add(ArrowCenter + FVector2D(0, -S));

		if (bShift)
			FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 1, AllottedGeometry.ToPaintGeometry(), Points, ESlateDrawEffect::None, IconColor, true, 3.f);
		else
			FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 1, AllottedGeometry.ToPaintGeometry(), Points, ESlateDrawEffect::None, FLinearColor(0.6f, 0.6f, 0.6f), true, 1.5f);

		if (bSingleShot)
		{
			float UnderY = ArrowCenter.Y + S * 0.9f;
			TArray<FVector2D> Line;
			Line.Add(FVector2D(Center.X - S * 0.6f, UnderY));
			Line.Add(FVector2D(Center.X + S * 0.6f, UnderY));
			FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 1, AllottedGeometry.ToPaintGeometry(), Line, ESlateDrawEffect::None, IconColor, false, 2.f);
		}
		bDrawnIcon = true;
	}
	else if (KeyDef->Action == EVirtualKeyAction::Left)
	{
		// ◀ ▶ two triangles
		float S = IconSize * 0.5f, Gap = IconSize * 0.6f;
		TArray<FVector2D> L;
		L.Add(Center + FVector2D(-Gap - S, 0));
		L.Add(Center + FVector2D(-Gap + S, -S));
		L.Add(Center + FVector2D(-Gap + S, S));
		L.Add(Center + FVector2D(-Gap - S, 0));
		FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 1, AllottedGeometry.ToPaintGeometry(), L, ESlateDrawEffect::None, IconColor, true, 1.5f);
		TArray<FVector2D> R;
		R.Add(Center + FVector2D(Gap + S, 0));
		R.Add(Center + FVector2D(Gap - S, -S));
		R.Add(Center + FVector2D(Gap - S, S));
		R.Add(Center + FVector2D(Gap + S, 0));
		FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 1, AllottedGeometry.ToPaintGeometry(), R, ESlateDrawEffect::None, IconColor, true, 1.5f);
		bDrawnIcon = true;
	}

	if (!bDrawnIcon)
	{
		// Main label text
		FSlateFontInfo MainFont = GetImSlateDefaultFont(10);
		FString DisplayLabel = KeyDef->GetDisplayLabel(bShift);
		FVector2D TextSize = (FVector2D)FontMeasure->Measure(DisplayLabel, MainFont);
		FVector2D TextPos = (LocalSize - TextSize) * 0.5f;
		FSlateDrawElement::MakeText(OutDrawElements, LayerId + 1,
			AllottedGeometry.ToPaintGeometry(TextSize, FSlateLayoutTransform(1.f, UE::Slate::CastToVector2f(TextPos))),
			DisplayLabel, MainFont, ESlateDrawEffect::None, FLinearColor::White);
	}

	// Top-right hint: T9 keys show number (Value), T26 keys show swipe-up char
	{
		FString HintText;
		if (KeyDef->WidthMultiplier >= 2.f)
			HintText = KeyDef->Value;  // T9: show number
		else
			HintText = KeyDef->Swipe.Up.Label;  // T26: show swipe-up symbol

		if (!HintText.IsEmpty())
		{
			FSlateFontInfo HintFont = GetImSlateDefaultFont(5);
			FLinearColor HintColor(0.55f, 0.55f, 0.55f, 0.9f);
			FVector2D HintSize = (FVector2D)FontMeasure->Measure(HintText, HintFont);
			float Margin = 2.f;
			FVector2D HintPos(LocalSize.X - HintSize.X - Margin, Margin);
			FSlateDrawElement::MakeText(OutDrawElements, LayerId + 1,
				AllottedGeometry.ToPaintGeometry(HintSize, FSlateLayoutTransform(1.f, UE::Slate::CastToVector2f(HintPos))),
				HintText, HintFont, ESlateDrawEffect::None, HintColor);
		}
	}

	return LayerId + 1;
}

// ==================== Input Handling ====================

FReply SImSlateKey::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		HandlePress(MyGeometry, MouseEvent.GetScreenSpacePosition());
		return FReply::Handled().CaptureMouse(SharedThis(this));
	}
	return FReply::Unhandled();
}

FReply SImSlateKey::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && HasMouseCapture())
	{
		HandleRelease(MyGeometry, MouseEvent.GetScreenSpacePosition());
		return FReply::Handled().ReleaseMouseCapture();
	}
	return FReply::Unhandled();
}

FReply SImSlateKey::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (bIsPressed)
	{
		HandleMove(MyGeometry, MouseEvent.GetScreenSpacePosition());
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FReply SImSlateKey::OnTouchStarted(const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent)
{
	HandlePress(MyGeometry, InTouchEvent.GetScreenSpacePosition());
	return FReply::Handled().CaptureMouse(SharedThis(this));
}

FReply SImSlateKey::OnTouchMoved(const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent)
{
	if (bIsPressed)
		HandleMove(MyGeometry, InTouchEvent.GetScreenSpacePosition());
	return FReply::Handled();
}

FReply SImSlateKey::OnTouchEnded(const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent)
{
	HandleRelease(MyGeometry, InTouchEvent.GetScreenSpacePosition());
	return FReply::Handled().ReleaseMouseCapture();
}

// ==================== Gesture Logic ====================

void SImSlateKey::HandlePress(const FGeometry& MyGeometry, const FVector2D& ScreenPos)
{
	bIsPressed = true;
	bSwipeDetected = false;
	bSwipeActive = false;
	bLongPressHandled = false;
	bWasInOuterZone = false;
	bSwipeVisualShown = false;
	ActiveSwipeDir = ESwipeDirection::None;
	LastCursorZone = 0;
	PressStartPos = ScreenPos;
	PressStartTime = FPlatformTime::Seconds();
}

void SImSlateKey::HandleRelease(const FGeometry& MyGeometry, const FVector2D& ScreenPos)
{
	if (!bIsPressed || !KeyDef)
	{
		bIsPressed = false;
		return;
	}

	double PressDuration = FPlatformTime::Seconds() - PressStartTime;
	float SwipeThreshold = 8.f * GetImSlateEffectiveScale();
	bIsPressed = false;
	OnReleaseVisual.ExecuteIfBound();
	if (LastCursorZone != 0)
		OnSpaceCursorZone.ExecuteIfBound(0);

	if (bSwipeActive)
	{
		FVector2D FinalDelta = ScreenPos - PressStartPos;

		// If finger returned to center, treat as tap (input Value/default)
		if (FinalDelta.Size() < SwipeThreshold * 1.5f)
		{
			if (KeyDef->Action != EVirtualKeyAction::Char)
				FireAction(KeyDef->Action);
			else
				FireInput(KeyDef->GetInputValue(bShiftActive.Get(false)));
			return;
		}

		ESwipeDirection FinalDir = DetectSwipe(FinalDelta);
		const FVirtualKeySwipeEntry* Entry = nullptr;
		switch (FinalDir)
		{
		case ESwipeDirection::Up:    Entry = &KeyDef->Swipe.Up; break;
		case ESwipeDirection::Down:  Entry = &KeyDef->Swipe.Down; break;
		case ESwipeDirection::Left:  Entry = &KeyDef->Swipe.Left; break;
		case ESwipeDirection::Right: Entry = &KeyDef->Swipe.Right; break;
		default: break;
		}

		if (Entry && Entry->IsSet())
		{
			if (Entry->Callback.IsBound())
				Entry->Callback.Execute();
			else
				FireInput(Entry->Label);
		}
		else
		{
			// Fallback: no swipe configured for this direction → input default Value
			if (KeyDef->Action != EVirtualKeyAction::Char)
				FireAction(KeyDef->Action);
			else
				FireInput(KeyDef->GetInputValue(bShiftActive.Get(false)));
		}
		return;
	}

	if (bSwipeDetected)
		return;

	if (bLongPressHandled)
	{
		if (LongPressCharCount > 0)
		{
			float XOffset = ScreenPos.X - LongPressAnchorPos.X;
			float CellW = LongPressCellWidth > 0.f ? LongPressCellWidth : 44.f * GetImSlateEffectiveScale();
			int32 HalfCount = LongPressCharCount / 2;
			int32 Index = FMath::Clamp(FMath::RoundToInt(XOffset / CellW) + HalfCount, 0, LongPressCharCount - 1);
			OnLongPressEnd.ExecuteIfBound(Index);
		}
		return;
	}

	if (KeyDef->Action != EVirtualKeyAction::Char)
	{
		FireAction(KeyDef->Action);
	}
	else
	{
		FireInput(KeyDef->GetInputValue(bShiftActive.Get(false)));
	}
}

void SImSlateKey::HandleMove(const FGeometry& MyGeometry, const FVector2D& ScreenPos)
{
	if (bSwipeDetected || !KeyDef) return;

	FVector2D Delta = ScreenPos - PressStartPos;
	float SwipeThreshold = 8.f * GetImSlateEffectiveScale();
	bool bIsSpaceOrDel = (KeyDef->Action == EVirtualKeyAction::Space || KeyDef->Action == EVirtualKeyAction::Backspace);

	// Space/Del: immediate step-drag when finger crosses threshold (no long-press wait)
	if (!bLongPressHandled && !bSwipeActive && bIsSpaceOrDel && Delta.Size() >= SwipeThreshold)
	{
		bLongPressHandled = true;
		LongPressAnchorPos = PressStartPos;
	}

	// Other keys: long-press check while finger stays near start
	bool bCanLongPress = KeyDef->LongPressChars.Num() > 0;
	if (!bLongPressHandled && !bSwipeActive && Delta.Size() < SwipeThreshold)
	{
		if (bCanLongPress && FPlatformTime::Seconds() - PressStartTime >= LongPressThreshold)
		{
			bLongPressHandled = true;
			LongPressAnchorPos = ScreenPos;
			OnLongPress.ExecuteIfBound(*KeyDef, MyGeometry);
		}
		if (!bSwipeVisualShown && Delta.Size() >= SwipeThreshold * 0.5f && KeyDef->Swipe.HasAny() && !bIsSpaceOrDel)
		{
			bSwipeVisualShown = true;
			OnPressVisual.ExecuteIfBound(*KeyDef, MyGeometry);
		}
		if (bSwipeVisualShown)
			OnMoveVisual.ExecuteIfBound(Delta, Delta.Size() >= SwipeThreshold);
		return;
	}

	// Drag active: Space = cursor step, Del = delete/undo step, LongPress = popup drag
	if (bLongPressHandled)
	{
		float XOffset = ScreenPos.X - LongPressAnchorPos.X;
		float StepW = 12.f * GetImSlateEffectiveScale();

		if (KeyDef->Action == EVirtualKeyAction::Space)
		{
			while (XOffset > StepW)  { OnKeyAction.ExecuteIfBound(EVirtualKeyAction::Right); LongPressAnchorPos.X += StepW; XOffset -= StepW; }
			while (XOffset < -StepW) { OnKeyAction.ExecuteIfBound(EVirtualKeyAction::Left);  LongPressAnchorPos.X -= StepW; XOffset += StepW; }
		}
		else if (KeyDef->Action == EVirtualKeyAction::Backspace)
		{
			while (XOffset < -StepW) { OnKeyAction.ExecuteIfBound(EVirtualKeyAction::Backspace);     LongPressAnchorPos.X -= StepW; XOffset += StepW; }
			while (XOffset > StepW)  { OnKeyAction.ExecuteIfBound(EVirtualKeyAction::UndoBackspace); LongPressAnchorPos.X += StepW; XOffset -= StepW; }
		}
		else if (LongPressCharCount > 0)
		{
			float CellW = LongPressCellWidth > 0.f ? LongPressCellWidth : 44.f * GetImSlateEffectiveScale();
			int32 HalfCount = LongPressCharCount / 2;
			int32 Index = FMath::Clamp(FMath::RoundToInt(XOffset / CellW) + HalfCount, 0, LongPressCharCount - 1);
			OnLongPressMove.ExecuteIfBound(Index);
		}
		return;
	}

	// Swipe visual for regular keys
	if (!bSwipeVisualShown && KeyDef->Swipe.HasAny() && !bIsSpaceOrDel)
	{
		bSwipeVisualShown = true;
		OnPressVisual.ExecuteIfBound(*KeyDef, MyGeometry);
	}
	if (bSwipeVisualShown)
		OnMoveVisual.ExecuteIfBound(Delta, Delta.Size() >= SwipeThreshold);

	if (Delta.Size() < SwipeThreshold)
		return;

	ESwipeDirection Dir = DetectSwipe(Delta);
	if (Dir == ESwipeDirection::None) return;

	ActiveSwipeDir = Dir;
	bSwipeActive = true;
}

SImSlateKey::ESwipeDirection SImSlateKey::DetectSwipe(const FVector2D& Delta) const
{
	if (FMath::Abs(Delta.X) > FMath::Abs(Delta.Y))
		return Delta.X > 0 ? ESwipeDirection::Right : ESwipeDirection::Left;
	else
		return Delta.Y < 0 ? ESwipeDirection::Up : ESwipeDirection::Down;
}

void SImSlateKey::FireInput(const FString& InputValue)
{
	if (!InputValue.IsEmpty() && KeyDef)
		OnKeyInput.ExecuteIfBound(*KeyDef, InputValue);
}

void SImSlateKey::FireAction(EVirtualKeyAction Action)
{
	OnKeyAction.ExecuteIfBound(Action);
}

// ==================== SImSlateKeyPopup ====================

static FSlateBrush& GetPopupBgBrush()
{
	static FSlateBrush Brush;
	static bool bInit = false;
	if (!bInit)
	{
		Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
		Brush.TintColor = FLinearColor(0.12f, 0.12f, 0.12f, 0.98f);
		Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
		Brush.OutlineSettings.CornerRadii = FVector4(8, 8, 8, 8);
		Brush.OutlineSettings.Color = FLinearColor(0.3f, 0.3f, 0.3f, 1.f);
		Brush.OutlineSettings.Width = 1.f;
		bInit = true;
	}
	return Brush;
}

void SImSlateKeyPopup::Construct(const FArguments& InArgs)
{
	Chars = InArgs._Chars;
	HighlightIndex = -1;
	float Scale = GetImSlateEffectiveScale();
	CellWidth = 44.f * Scale;

	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
	for (int32 i = 0; i < Chars.Num(); ++i)
	{
		TSharedPtr<SBorder> CellBorder;
		Row->AddSlot()
		.AutoWidth()
		.Padding(1.f)
		[
			SAssignNew(CellBorder, SBorder)
			.BorderImage(&GetKeyBrush(false))
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.Padding(FMargin(4.f * Scale, 2.f * Scale))
			[
				SNew(STextBlock)
				.Text(FText::FromString(Chars[i]))
				.Font(GetImSlateDefaultFont(16))
				.ColorAndOpacity(FLinearColor::White)
			]
		];
		CellBorders.Add(CellBorder);
	}

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(&GetPopupBgBrush())
		.Padding(4.f * Scale)
		[
			Row
		]
	];
}

void SImSlateKeyPopup::SetHighlightIndex(int32 Index)
{
	if (HighlightIndex == Index) return;
	if (CellBorders.IsValidIndex(HighlightIndex))
		CellBorders[HighlightIndex]->SetBorderImage(&GetKeyBrush(false));
	HighlightIndex = Index;
	if (CellBorders.IsValidIndex(HighlightIndex))
		CellBorders[HighlightIndex]->SetBorderImage(&GetKeyBrush(true));
}

// ==================== SImSlateCursorSlider ====================

void SImSlateCursorSlider::Construct(const FArguments& InArgs)
{
	OnCursorMove = InArgs._OnCursorMove;
	StepThreshold = 20.f * GetImSlateEffectiveScale();
}

FVector2D SImSlateCursorSlider::ComputeDesiredSize(float) const
{
	float Scale = GetImSlateEffectiveScale();
	return FVector2D(0.f, 32.f * Scale);
}

int32 SImSlateCursorSlider::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	FVector2D Size = AllottedGeometry.GetLocalSize();
	float Scale = GetImSlateEffectiveScale();

	static FSlateBrush TrackBrush;
	static bool bInit = false;
	if (!bInit)
	{
		TrackBrush.DrawAs = ESlateBrushDrawType::RoundedBox;
		TrackBrush.TintColor = FLinearColor(0.15f, 0.15f, 0.15f, 0.9f);
		TrackBrush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
		TrackBrush.OutlineSettings.CornerRadii = FVector4(4, 4, 4, 4);
		bInit = true;
	}

	FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), &TrackBrush);

	// Draw arrows ◀ ▶ and center grip ═══
	FSlateFontInfo Font = GetImSlateDefaultFont(12);
	TSharedRef<FSlateFontMeasure> FM = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	FString Label = TEXT("\x25C0 \x2550\x2550\x2550 \x25B6");
	FVector2D TextSize = (FVector2D)FM->Measure(Label, Font);
	FVector2D TextPos = (Size - TextSize) * 0.5f;
	FLinearColor Color = bDragging ? FLinearColor(0.8f, 0.8f, 0.8f, 1.f) : FLinearColor(0.5f, 0.5f, 0.5f, 0.8f);

	FSlateDrawElement::MakeText(OutDrawElements, LayerId + 1,
		AllottedGeometry.ToPaintGeometry(TextSize, FSlateLayoutTransform(1.f, UE::Slate::CastToVector2f(TextPos))),
		Label, Font, ESlateDrawEffect::None, Color);

	return LayerId + 1;
}

FReply SImSlateCursorSlider::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bDragging = true;
		DragAccumulator = 0.f;
		LastDragPos = MouseEvent.GetScreenSpacePosition();
		return FReply::Handled().CaptureMouse(SharedThis(this));
	}
	return FReply::Unhandled();
}

FReply SImSlateCursorSlider::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	bDragging = false;
	return FReply::Handled().ReleaseMouseCapture();
}

FReply SImSlateCursorSlider::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (bDragging)
	{
		float DeltaX = MouseEvent.GetScreenSpacePosition().X - LastDragPos.X;
		LastDragPos = MouseEvent.GetScreenSpacePosition();
		DragAccumulator += DeltaX;
		while (DragAccumulator > StepThreshold)  { OnCursorMove.ExecuteIfBound(EVirtualKeyAction::Right); DragAccumulator -= StepThreshold; }
		while (DragAccumulator < -StepThreshold) { OnCursorMove.ExecuteIfBound(EVirtualKeyAction::Left);  DragAccumulator += StepThreshold; }
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FReply SImSlateCursorSlider::OnTouchStarted(const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent)
{
	bDragging = true;
	DragAccumulator = 0.f;
	LastDragPos = InTouchEvent.GetScreenSpacePosition();
	return FReply::Handled().CaptureMouse(SharedThis(this));
}

FReply SImSlateCursorSlider::OnTouchMoved(const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent)
{
	if (bDragging)
	{
		float DeltaX = InTouchEvent.GetScreenSpacePosition().X - LastDragPos.X;
		LastDragPos = InTouchEvent.GetScreenSpacePosition();
		DragAccumulator += DeltaX;
		while (DragAccumulator > StepThreshold)  { OnCursorMove.ExecuteIfBound(EVirtualKeyAction::Right); DragAccumulator -= StepThreshold; }
		while (DragAccumulator < -StepThreshold) { OnCursorMove.ExecuteIfBound(EVirtualKeyAction::Left);  DragAccumulator += StepThreshold; }
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FReply SImSlateCursorSlider::OnTouchEnded(const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent)
{
	bDragging = false;
	return FReply::Handled().ReleaseMouseCapture();
}

}  // namespace ImSlate

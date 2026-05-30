// Copyright ImSlate, Inc. All Rights Reserved.
#include "ImSlateTemplate/ImVirtualKey.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
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
	// NOTE: do NOT override LongPressAnchorPos here. Step-drag selection uses the finger's
	// own trigger position (set in TryTriggerLongPress) as the anchor, like Space/Del. Using
	// the popup center instead broke right-side keys: the popup gets clamped left of the
	// finger, so XOffset started positive and left-drag jumped rightward.
	LongPressCellWidth = InCellWidth;
	LongPressCharCount = InCharCount;
}

FVector2D SImSlateKey::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
	float Scale = GetImSlateEffectiveScale();
	// The cap must scale with DPI too. A fixed 48px cap was in *physical* pixels, so on a
	// high-DPI screen (e.g. iOS retina) it clamped keys to a tiny size. Cap in logical px (*Scale).
	float H = FMath::Min(32.f * Scale, 48.f * Scale); // = 32*Scale; logical cap of 48
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
		TArray<FVector2D> Points;
		float W = IconSize * 0.8f, H = IconSize * 0.55f;
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

// On rapid repeated taps Slate routes the 2nd press as a double-click instead of a
// regular down. Without this override that press would fall through to the keyboard
// (which swallows double-clicks) and its paired up would arrive with bIsPressed=false
// and be dropped — losing every other tap. Treat a double-click as a normal press.
FReply SImSlateKey::OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	return OnMouseButtonDown(MyGeometry, MouseEvent);
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

	// Auto long-press: fire the popup after LongPressThreshold even if the finger never moves
	// (HandleMove only runs on movement, so a still hold would otherwise never trigger).
	if (KeyDef && KeyDef->LongPressChars.Num() > 0)
	{
		LongPressTimer = RegisterActiveTimer(LongPressThreshold,
			FWidgetActiveTimerDelegate::CreateLambda([this](double, float) -> EActiveTimerReturnType {
				LongPressTimer.Reset();
				if (bIsPressed)
					TryTriggerLongPress(GetCachedGeometry(), PressStartPos);  // finger still at start
				return EActiveTimerReturnType::Stop;
			}));
	}
}

bool SImSlateKey::TryTriggerLongPress(const FGeometry& Geometry, const FVector2D& ScreenPos)
{
	if (bLongPressHandled || bSwipeVisualShown || !KeyDef || KeyDef->LongPressChars.Num() == 0)
		return false;
	bLongPressHandled = true;
	LongPressAnchorPos = ScreenPos;
	LongPressSelIndex = KeyDef->LongPressChars.Num() / 2;  // start at the middle entry
	OnLongPress.ExecuteIfBound(*KeyDef, Geometry);
	return true;
}

void SImSlateKey::HandleRelease(const FGeometry& MyGeometry, const FVector2D& ScreenPos)
{
	if (LongPressTimer.IsValid()) { UnRegisterActiveTimer(LongPressTimer.ToSharedRef()); LongPressTimer.Reset(); }

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
			// Use the step-drag accumulated selection (matches the highlight shown during move).
			OnLongPressEnd.ExecuteIfBound(LongPressSelIndex);
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

	// Space/Del: immediate step-drag when finger crosses threshold
	if (!bLongPressHandled && bIsSpaceOrDel && Delta.Size() >= SwipeThreshold)
	{
		bLongPressHandled = true;
		LongPressAnchorPos = PressStartPos;
		OnPressVisual.ExecuteIfBound(*KeyDef, MyGeometry);
	}

	// Long-press check (finger near start, for keys with LongPressChars).
	// The auto timer started in HandlePress is the primary trigger; this is the fallback when
	// the finger is moving slightly. Skip if a swipe popup is already active (sliding back must
	// not flip swipe → long-press).
	if (!bLongPressHandled && !bSwipeVisualShown && KeyDef->LongPressChars.Num() > 0 && Delta.Size() < SwipeThreshold
		&& FPlatformTime::Seconds() - PressStartTime >= LongPressThreshold)
	{
		TryTriggerLongPress(MyGeometry, ScreenPos);
	}

	// Swipe popup trigger: finger moved past the swipe threshold → show four-way visual.
	// Use distance from the press point (not "left the key bounds"), so edge keys — which
	// can't be dragged outside toward the screen edge — still pop their four-way popup.
	if (!bLongPressHandled && !bSwipeVisualShown && KeyDef->Swipe.HasAny())
	{
		if (Delta.Size() >= SwipeThreshold)
		{
			bSwipeVisualShown = true;
			OnPressVisual.ExecuteIfBound(*KeyDef, MyGeometry);
		}
	}

	// Step-drag active: Space = cursor, Del = delete/undo, LongPress = popup
	if (bLongPressHandled)
	{
		float XOffset = ScreenPos.X - LongPressAnchorPos.X;
		float StepW = 12.f * GetImSlateEffectiveScale();

		if (KeyDef->Action == EVirtualKeyAction::Space)
		{
			OnMoveVisual.ExecuteIfBound(FVector2D(XOffset, 0.f), true);
			while (XOffset > StepW)  { OnKeyAction.ExecuteIfBound(EVirtualKeyAction::Right); LongPressAnchorPos.X += StepW; XOffset -= StepW; }
			while (XOffset < -StepW) { OnKeyAction.ExecuteIfBound(EVirtualKeyAction::Left);  LongPressAnchorPos.X -= StepW; XOffset += StepW; }
		}
		else if (KeyDef->Action == EVirtualKeyAction::Backspace)
		{
			OnMoveVisual.ExecuteIfBound(FVector2D(XOffset, 0.f), true);
			while (XOffset < -StepW) { OnKeyAction.ExecuteIfBound(EVirtualKeyAction::Backspace);     LongPressAnchorPos.X -= StepW; XOffset += StepW; }
			while (XOffset > StepW)  { OnKeyAction.ExecuteIfBound(EVirtualKeyAction::UndoBackspace); LongPressAnchorPos.X += StepW; XOffset -= StepW; }
		}
		else if (LongPressCharCount > 0)
		{
			// Step-drag selection (like Space/Del): every StepW of movement advances the
			// selection by one, accumulating — so a small finger motion scans many entries.
			// NOT an absolute finger-position mapping.
			int32 OldIndex = LongPressSelIndex;
			while (XOffset > StepW)  { LongPressSelIndex = FMath::Min(LongPressSelIndex + 1, LongPressCharCount - 1); LongPressAnchorPos.X += StepW; XOffset -= StepW; }
			while (XOffset < -StepW) { LongPressSelIndex = FMath::Max(LongPressSelIndex - 1, 0);                      LongPressAnchorPos.X -= StepW; XOffset += StepW; }
			if (LongPressSelIndex != OldIndex)
				OnLongPressMove.ExecuteIfBound(LongPressSelIndex);
		}
		return;
	}

	// Swipe visual: a direction activates when the finger crosses, in that direction, the
	// smaller of (a) the standard "leave the key bounds" distance and (b) the space actually
	// available before the finger hits the screen edge. So a normal key keeps the standard
	// bounds behavior, while an EDGE key — which can't be dragged past its border toward the
	// screen edge — gets a reduced threshold on its squeezed side so that direction is still
	// reachable. Only the squeezed side is relaxed; the others stay standard.
	if (bSwipeVisualShown)
	{
		const FVector2D KeySize = MyGeometry.GetLocalSize();
		const float GeoScale = MyGeometry.GetAccumulatedLayoutTransform().GetScale();
		// Standard distance to "leave" the key from its center, in absolute screen px.
		const float StdX = (KeySize.X * 0.5f) * GeoScale;
		const float StdY = (KeySize.Y * 0.5f) * GeoScale;
		const float Margin = 6.f * GeoScale;  // keep a little room from the very edge

		// Absolute key rect and the window (screen) rect the finger can move within.
		const FVector2D AbsTL = MyGeometry.LocalToAbsolute(FVector2D::ZeroVector);
		const FVector2D AbsBR = MyGeometry.LocalToAbsolute(KeySize);
		FVector2D ScreenMin(0.f, 0.f), ScreenMax(FLT_MAX, FLT_MAX);
		if (TSharedPtr<SWindow> Win = FSlateApplication::Get().FindWidgetWindow(SharedThis(this)))
		{
			const FGeometry& WG = Win->GetTickSpaceGeometry();
			ScreenMin = WG.LocalToAbsolute(FVector2D::ZeroVector);
			ScreenMax = WG.LocalToAbsolute(WG.GetLocalSize());
		}
		// Reachable travel before hitting the screen edge, per direction (absolute px).
		const float ReachL = FMath::Max(0.f, (AbsTL.X - ScreenMin.X) - Margin);
		const float ReachR = FMath::Max(0.f, (ScreenMax.X - AbsBR.X) - Margin);
		const float ReachU = FMath::Max(0.f, (AbsTL.Y - ScreenMin.Y) - Margin);
		const float ReachD = FMath::Max(0.f, (ScreenMax.Y - AbsBR.Y) - Margin);

		// Per-direction threshold: standard, but capped to what's reachable on a squeezed side.
		const float MinThresh = 8.f * GetImSlateEffectiveScale();
		const float ThreshL = FMath::Max(MinThresh, FMath::Min(StdX, ReachL));
		const float ThreshR = FMath::Max(MinThresh, FMath::Min(StdX, ReachR));
		const float ThreshU = FMath::Max(MinThresh, FMath::Min(StdY, ReachU));
		const float ThreshD = FMath::Max(MinThresh, FMath::Min(StdY, ReachD));

		bool bDirActive = false;
		if (FMath::Abs(Delta.X) > FMath::Abs(Delta.Y))
			bDirActive = (Delta.X > 0) ? (Delta.X >= ThreshR) : (-Delta.X >= ThreshL);
		else
			bDirActive = (Delta.Y < 0) ? (-Delta.Y >= ThreshU) : (Delta.Y >= ThreshD);

		OnMoveVisual.ExecuteIfBound(Delta, bDirActive);

		if (bDirActive)
		{
			ActiveSwipeDir = DetectSwipe(Delta);
			bSwipeActive = true;
		}
		else
		{
			bSwipeActive = false;
			ActiveSwipeDir = ESwipeDirection::None;
		}
	}
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
		// Each cell is forced to a fixed width (CellWidth) so the on-screen layout matches
		// the index-mapping math in SImSlateKey::HandleMove (which steps by CellWidth).
		// Previously cells were AutoWidth → actual width ≠ CellWidth → highlight drifted
		// from the finger position.
		Row->AddSlot()
		.AutoWidth()
		.Padding(1.f)
		[
			SNew(SBox)
			.WidthOverride(CellWidth)
			[
				SAssignNew(CellBorder, SBorder)
				.BorderImage(&GetKeyBrush(false))
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.Padding(FMargin(0.f, 2.f * Scale))
				[
					SNew(STextBlock)
					.Text(FText::FromString(Chars[i]))
					.Font(GetImSlateDefaultFont(16))
					.ColorAndOpacity(FLinearColor::White)
				]
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

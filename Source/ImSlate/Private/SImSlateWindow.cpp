// Copyright ImSlate, Inc. All Rights Reserved.
#include "SImSlateWindow.h"

#include "SImSlatePanel.h"
#include "Widgets/Layout/SDPIScaler.h"



#include "SImSlateViewport.h"
#include "SImViewportGame.h"
#include "SImViewportHost.h"
#include "UObject/StrongObjectPtr.h"

//
#include "AttributeCompatibility.h"
#include "Brushes/SlateColorBrush.h"
#include "Engine/Texture2D.h"
#include "GenericPlatform/GenericApplicationMessageHandler.h"
#include "ImSlate.h"
#include "ImSlateStyleSetting.h"
#include "Misc/ScopeExit.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBar.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Text/STextBlock.h"
#include "Application/ThrottleManager.h"
#include "XConsoleManager.h"
#if ENGINE_MAJOR_VERSION >= 5
#include "Layout/ChildrenBase.h"
#endif

namespace ImSlate
{
extern SImViewportGame* GetGameViewportImpl();

// Multi-window (ImViewportHost = separate native SWindow) is DESKTOP-ONLY. iOS allows only
// one UI window — dragging an ImSlate window out of the game viewport used to switch to the
// Host path and create a second SWindow, crashing with "Only one UI window may be created on
// iOS". On mobile, windows must always stay inline in the game viewport.
bool bSupportImViewportHost = PLATFORM_DESKTOP;
#if PLATFORM_DESKTOP
FXConsoleVariableRef CVar_SupportImViewportHost(TEXT("imslate.multiview"), bSupportImViewportHost, TEXT(""));
#endif

class FViewportPopupHolder
{
public:
	FViewportPopupHolder(SImViewportHost* In, SImSlateWindow* InWindow, bool bActive)
		: Host(In)
	{
		if (ensure(Host && InWindow))
		{
			Host->MakePopupWindow(InWindow, bActive);
		}
	}
	~FViewportPopupHolder()
	{
		if (Host)
		{
			Host->DestroyPopupWindow();
		}
	}

protected:
	SImViewportHost* Host;
};

static SImViewportHost* SelectViewportHost(SImSlateWindow* InWindow, bool bActive)
{
	auto& g = *GImSlate;
	TSharedPtr<SImViewportHost> Host;
	for (auto& Viewport : g.Viewports)
	{
		if (!Viewport->Window && !Viewport->IsGameViewport())
		{
			Host = StaticCastSharedRef<SImViewportHost>(Viewport);
			ensure(Host->GetChildren()->Num() == 0);
			break;
		}
	}

	if (!Host)
	{
		auto Index = g.Viewports.Add(SAssignNew(Host, SImViewportHost));
		Host->ID = Index;
	}
	SImViewportHost* Ptr = Host.Get();
	g.HostById.Emplace(Host.ToSharedRef(), MakeShared<FViewportPopupHolder>(Ptr, InWindow, bActive));
	return Ptr;
}
static void ReleaseViewportHost(const TSharedRef<SImSlateWindow>& InWindow, SImSlateViewport* InViewport)
{
	check(InWindow->Viewport == InViewport);
	auto Shared = StaticCastSharedRef<SImSlateViewport>(InViewport->AsShared());
	InViewport->RemoveWindow(InWindow);
	GImSlate->Viewports.Remove(Shared);
}

class FImSlateDragOperation : public FDragDropOperation
{
public:
	DRAG_DROP_OPERATOR_TYPE(FImSlateDragOperation, FDragDropOperation)

	static TSharedRef<FImSlateDragOperation> New(const TSharedRef<SImSlateWindow>& InWindowToBeDragged, const FVector2D& InGrabOffset)
	{
		const TSharedRef<FImSlateDragOperation> Operation = MakeShareable(new FImSlateDragOperation(InWindowToBeDragged, InGrabOffset));
		return Operation;
	}

protected:
	TSharedRef<SImSlateWindow> ImSlateWindow;
	FVector2D AbsGrabOffset;
	int32 FrameKeepInGameView = 0;
	FImSlateDragOperation(const TSharedRef<SImSlateWindow>& InWindowToBeDragged, const FVector2D& InGrabOffset)
		: ImSlateWindow(InWindowToBeDragged)
		, AbsGrabOffset(InGrabOffset)
	{
	}

	virtual void OnDragged(const FDragDropEvent& DragDropEvent) override
	{
		bool bIsHostGameViewport = ImSlateWindow->IsViewportGame();

		if (bSupportImViewportHost && bIsHostGameViewport && !ImSlateWindow->IsAreaInGameViewport())
		{
			auto Viewport = ImSlateWindow->SelectViewport();
			// gameview to hostview
			ImSlateWindow->MoveToViewport(Viewport);
			bIsHostGameViewport = false;
			FrameKeepInGameView = 6;
		}

		FVector2D AbsPos = DragDropEvent.GetScreenSpacePosition() - AbsGrabOffset;
		if (bIsHostGameViewport)
		{
			ImSlateWindow->Viewport->SetWindowPos(&ImSlateWindow.Get(), ImSlateWindow->Viewport->AbsoluteToLocal(AbsPos));
		}
		else
		{
			ImSlateWindow->Viewport->SetWindowPos(&ImSlateWindow.Get(), AbsPos);
			if (FrameKeepInGameView > 0)
			{
				FrameKeepInGameView--;
				auto GameViewport = ImSlateWindow->GetViewportGame();
				GameViewport->AddWindow(ImSlateWindow);
				GameViewport->SetWindowPos(&ImSlateWindow.Get(), GameViewport->AbsoluteToLocal(AbsPos));
			}
		}
#if 0
		if (auto GameViewportClient = ImSlateWindow->GetViewportGame()->GameViewportClient.Get())
			GameViewportClient->RedrawRequested(GameViewportClient->Viewport);
#endif
	}

	virtual void OnDrop(bool bDropWasHandled, const FPointerEvent& MouseEvent) override
	{
		if (ImSlateWindow->IsAreaInGameViewport())
		{
			// hostview to gameview
			auto GameViewport = ImSlateWindow->GetViewportGame();
			ImSlateWindow->MoveToViewport(GameViewport);
			FVector2D TargetPosition = MouseEvent.GetScreenSpacePosition() - AbsGrabOffset;
			TargetPosition = ImSlateWindow->Viewport->AbsoluteToLocal(TargetPosition);
			ImSlateWindow->Viewport->SetWindowPos(&ImSlateWindow.Get(), TargetPosition);
			ImSlateWindow->BringWindowToFront();
		}
		else if (bSupportImViewportHost)
		{
			ImSlateWindow->BringWindowToFront();
		}
	}
};

static int32 DisableThrottleCnt = 0;
template<typename Base = SCompoundWidget>
class SImSlateDragingBase : public Base
{
public:
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (!DragParameters.IsSet() &&  MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
		{
			DragParameters = FDragParameters(Target->GetCachedGeometry().GetAbsolutePosition(), MouseEvent.GetScreenSpacePosition(), MouseEvent.GetPointerIndex());
			return FReply::Handled().CaptureMouse(this->AsShared());
		}
		return FReply::Handled();
	}
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (DragParameters.IsSet() && DragParameters->PointerIndex == MouseEvent.GetPointerIndex() && this->HasMouseCapture())
		{
			auto ScreenSpaceDelta = MouseEvent.GetScreenSpacePosition() - DragParameters->AbsDragStart;
			FVector2D AbsPos = DragParameters->AbsWindowPos + ScreenSpaceDelta;
			if (Target && AbsPos.RoundToVector() != DragParameters->AbsWindowPos.RoundToVector())
			{
				OnDragingVal(Target, DragParameters.GetValue(), MouseEvent.GetScreenSpacePosition());
			}
		}
		return FReply::Handled();
	}
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if(DragParameters->PointerIndex == MouseEvent.GetPointerIndex() && MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
		{
			DragParameters.Reset();
			return FReply::Handled().ReleaseMouseCapture();
		}
		return FReply::Handled();
	}

	virtual FReply OnTouchStarted(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (!DragParameters.IsSet())
		{
			DragParameters = FDragParameters(Target->GetCachedGeometry().GetAbsolutePosition(), MouseEvent.GetScreenSpacePosition(), MouseEvent.GetPointerIndex());
			if (DisableThrottleCnt++ == 0)
			{
				FSlateThrottleManager::Get().DisableThrottle(true);
			}
			return FReply::Handled();
		}
		return FReply::Handled();
	}
	virtual FReply OnTouchMoved(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (DragParameters.IsSet() && DragParameters->PointerIndex == MouseEvent.GetPointerIndex())
		{
			auto ScreenSpaceDelta = MouseEvent.GetScreenSpacePosition() - DragParameters->AbsDragStart;
			FVector2D AbsPos = DragParameters->AbsWindowPos + ScreenSpaceDelta;
			if (Target && AbsPos.RoundToVector() != DragParameters->AbsWindowPos.RoundToVector())
			{
				OnDragingVal(Target, DragParameters.GetValue(), MouseEvent.GetScreenSpacePosition());
			}
		}
		return FReply::Handled();
	}
	virtual FReply OnTouchEnded(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (DragParameters->PointerIndex == MouseEvent.GetPointerIndex())
		{
			DragParameters.Reset();
			if (--DisableThrottleCnt == 0)
			{
				FSlateThrottleManager::Get().DisableThrottle(false);
			}
			return FReply::Handled();
		}
		return FReply::Handled();
	}
	virtual void CacheDesiredSize(float LayoutScaleMultiplier) override { SWidget::CacheDesiredSize(LayoutScaleMultiplier); }

protected:
	struct FDragParameters
	{
		FDragParameters(FVector2D InAbsOriginal, FVector2D InAbsDragStart, uint32 InPointIdx = -1)
			: AbsWindowPos(InAbsOriginal)
			, AbsDragStart(InAbsDragStart)
			, PointerIndex(InPointIdx)
		{
		}

		FVector2D AbsWindowPos;
		FVector2D AbsDragStart;
		uint32 PointerIndex;
	};
	virtual void OnDragingVal(SImSlateWindow* InTarget, const FDragParameters& DragParam, FVector2D ScreenSpacePos) {}
	TOptional<FDragParameters> DragParameters;
	SImSlateWindow* Target = nullptr;
};

class SResizeArea final : public SImSlateDragingBase<>
{
public:
	SLATE_BEGIN_ARGS(SResizeArea) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, SImSlateWindow* InTarget)
	{
		SetClipping(InTarget->GetClipping());
		Target = InTarget;
		ChildSlot
		[
			SNew(SBox)
			.HeightOverride(5.f)
		];
	}
	FVector2D OrignalSize = FVector2D::ZeroVector;
	virtual void OnDragingVal(SImSlateWindow* InTarget, const FDragParameters& DragParam, FVector2D ScreenSpacePos) override
	{
		FVector2D Delta = ScreenSpacePos - DragParam.AbsDragStart;
		InTarget->DragingWindowSize(OrignalSize + Delta);
	}
	virtual FCursorReply OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const
	{
		if (IsInArea(MyGeometry, CursorEvent))
			return FCursorReply::Cursor(EMouseCursor::ResizeSouthEast);
		else
			return SWidget::OnCursorQuery(MyGeometry, CursorEvent);
	}
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (IsInArea(MyGeometry, MouseEvent))
		{
			OrignalSize = Target->IsViewportGame() ? Target->GetCachedGeometry().GetLocalSize() : Target->GetCachedGeometry().GetAbsoluteSize();
			return SImSlateDragingBase<>::OnMouseButtonDown(MyGeometry, MouseEvent);
		}
		else
		{
			return SWidget::OnMouseButtonDown(MyGeometry, MouseEvent);
		}
	}
	virtual FReply OnTouchStarted(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (IsInArea(MyGeometry, MouseEvent))
		{
			OrignalSize = Target->IsViewportGame() ? Target->GetCachedGeometry().GetLocalSize() : Target->GetCachedGeometry().GetAbsoluteSize();
			return SImSlateDragingBase<>::OnTouchStarted(MyGeometry, MouseEvent);
		}
		else
		{
			return SWidget::OnTouchStarted(MyGeometry, MouseEvent);
		}
	}
	bool IsInArea(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) const
	{
		auto LocalPos = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
		return (LocalPos.X + LocalPos.Y >= MyGeometry.GetLocalSize().X);
	}

	virtual int32 OnPaint(const FPaintArgs& Args,
						  const FGeometry& AllottedGeometry,
						  const FSlateRect& MyCullingRect,  //
						  FSlateWindowElementList& OutDrawElements,
						  int32 LayerId,
						  const FWidgetStyle& InWidgetStyle,
						  bool bParentEnabled) const override
	{
		int32 ContentLayerId = LayerId + 1;
		int32 MaxLayerId = ContentLayerId;

		FSlateColorBrush ColorBrush(FLinearColor::White);
		const auto Width = AllottedGeometry.GetLocalSize().X;
		TArray<FVector2D> Pair{{0.f, Width}, {Width, Width}, {Width, 0}};
		FSlateDrawElement::MakeLines(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), Pair, ESlateDrawEffect::None, FLinearColor::Gray);

		Pair.SetNum(2);
		const int32 TotalCnt = 4;
		for (int32 Idx = TotalCnt; Idx >= 0; --Idx)
		{
			auto Side = Width * Idx / TotalCnt;
			Pair[0] = FVector2D{Width, Side};
			Pair[1] = FVector2D{Side, Width};
			FSlateDrawElement::MakeLines(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), Pair, ESlateDrawEffect::None, FLinearColor::Gray);
		}

		return MaxLayerId;
	}
};

class SImHeaderArea final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SImHeaderArea) {}
	SLATE_ATTRIBUTE(FVector2D, MinSize)
	SLATE_END_ARGS()
	SImSlateWindow* Target = nullptr;

	void Construct(const FArguments& InArgs, SImSlateWindow* InWindow, const FText& InTitle = FText::GetEmpty())
	{
		SetClipping(InWindow->GetClipping());
		Target = InWindow;
		Title = InTitle;
		MinSizeAttr = InArgs._MinSize;

		TitleBrush.TintColor = GetDefault<UXImSlateStyleSetting>()->WindowHeaderColor;

		ChildSlot
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.VAlign(VAlign_Top)
			.HAlign(HAlign_Fill)
			.AutoHeight()
			[
				SNew(SBox)
				.HeightOverride(ComputeDesiredSize(0.f).Y)
				[
					SNew(SBorder)
					// UE 5.7: Use TAttribute::CreateSP for Slate widgets (non-UObject types)
					//.BorderImage(TAttribute<const FSlateBrush*>::CreateSP(this, &SImHeaderArea::HeaderBrush))
					.Visibility(EVisibility::SelfHitTestInvisible)
					.Content()
					[
						SNew(SHorizontalBox)
						//加上simage
						+ SHorizontalBox::Slot()
						.HAlign(HAlign_Left)
						.VAlign(VAlign_Center)
						.AutoWidth()
						.Padding(8.0f, 0.0f, 8.0f, 0.0f)
						[
							SNew(SImage)
							.Image(HeaderIconBrush())
						]
						+ SHorizontalBox::Slot()
						.HAlign(HAlign_Left)
						.VAlign(VAlign_Center)
						.AutoWidth()
						[
							SNew(STextBlock)
							.Text(TAttribute<FText>::CreateSP(Target, &SImSlateWindow::GetTitleText))
							.Font(ImSlate::GetImSlateDefaultFont(12))
							.ColorAndOpacity(FLinearColor::White)
						]
						+ SHorizontalBox::Slot()
						.FillWidth(1.f)
						.HAlign(HAlign_Fill)
						.VAlign(VAlign_Fill)
						[
							SNew(SSpacer)
						]
						+ SHorizontalBox::Slot()
						.HAlign(HAlign_Right)
						.VAlign(VAlign_Center)
						.AutoWidth()
						[
							SNew(SButton)
							.Text(NSLOCTEXT("ImSlate", "CloseIcon", "X"))
							// UE 5.7: Use TAttribute::CreateLambda with this pointer capture to access member variable
							.Visibility(TAttribute<EVisibility>::CreateLambda([this]() {
								return this->Target->ShouldShowCloseButton() ? EVisibility::Visible : EVisibility::Collapsed;
							}))
							.OnClicked(this, &SImHeaderArea::OnCloseBtnClick)
						]
					]
				]
			]
			+ SVerticalBox::Slot()
			.VAlign(VAlign_Fill)
			.HAlign(HAlign_Fill)
			[
				SNew(SSpacer)
			]
		];
	}

	FVector2D GetMinSize() const { return MinSizeAttr.Get(FVector2D{200.f, 40.f}); }

public:
	FText Title;
	FSlateBrush TitleBrush;

	FSlateBrush IconBrush;
	TStrongObjectPtr<UTexture2D> IconTexture;

	TAttribute<FVector2D> MinSizeAttr;
	TOptional<float> TitleHeight;
	float GetTitleHeight() const
	{
		return TitleHeight.Get(24.f) * ImSlate::GetImSlateEffectiveScale();
	}

public:
	const FSlateBrush* HeaderBrush() { return &TitleBrush; }

	const FSlateBrush* HeaderIconBrush()
	{
		if (!IconTexture.Get())
		{
			//IconTexture.Reset(LoadObject<UTexture2D>(nullptr, TEXT("Logo")));
			//IconBrush = FSlateImageBrush((UObject*)(IconTexture.Get()), FVector2D(16.f, 16.f));
		}
		return &IconBrush;
	}

	void SetHeaderBrushBgAlpha(float InAlpha)
	{
		auto DefaultColor = GetDefault<UXImSlateStyleSetting>()->WindowHeaderColor;
		DefaultColor.A = InAlpha;
		TitleBrush.TintColor = DefaultColor;
	}

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (!(Target->Flags & ImSlateWindowFlags_NoMove))
			return SCompoundWidget::OnMouseButtonDown(MyGeometry, MouseEvent);
		return FReply::Handled();
	}
	virtual FReply OnTouchStarted(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (!(Target->Flags & ImSlateWindowFlags_NoMove))
			return SCompoundWidget::OnTouchStarted(MyGeometry, MouseEvent);
		return FReply::Handled();
	}
	void SetTitle(const FText& InTitle) { Title = InTitle; }
	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override
	{
		if (Target && Target->Viewport)
			return {Target->GetWindowSize().X, GetTitleHeight()};
		return {100.f, GetTitleHeight()};
	}

	FReply OnCloseBtnClick()
	{
		if (Target)
		{
			Target->Viewport->OnClose(Target);
		}
		return FReply::Handled();
	}
	virtual void CacheDesiredSize(float LayoutScaleMultiplier) override { SWidget::CacheDesiredSize(LayoutScaleMultiplier); }
};

//////////////////////////////////////////////////////////////////////////
SImSlateWindow::SImSlateWindow()
	: Children(this)
{
	bHasCustomPrepass = true;
	LastFrameActive = -1;
	LastFrameJustFocused = -1;
	LastTimeActive = -1.0f;
	Clipping = EWidgetClipping::ClipToBounds;
	HAlignment = EHorizontalAlignment::HAlign_Fill;
	VAlignment = EVerticalAlignment::VAlign_Fill;
	ViewportGame = GetGameViewportImpl();
}

void SImSlateWindow::Construct(const FArguments& InArgs, uint32 ImSlateId)
{
	UE_LOG(LogTemp, Log, TEXT("SImSlateWindow Construct: %p, id: %d"), this, ImSlateId);

	WindowId = ImSlateId;
	Flags = InArgs._Flags;

	ImSlateCond InitCond = ImSlateCond_Always | ImSlateCond_Once | ImSlateCond_Appearing;
	SetWindowByAlphaAllowFlags = InitCond;
	SetWindowPosAllowFlags = InitCond;
	SetWindowSizeAllowFlags = InitCond;
	SetWindowCollapsedAllowFlags = InitCond;
	SetWindowDockAllowFlags = InitCond;

	bShowResizeHandle = !(Flags & (ImSlateWindowFlags_NoResize));

	ContentBackgroundBrush.TintColor = GetDefault<UXImSlateStyleSetting>()->WindowContentColor;

	Children.Add(SAssignNew(ResizeArea, SResizeArea, this));
	Children.Add(SAssignNew(HeaderHandle, SImHeaderArea, this, InArgs._Title));
	Children.Add(SAssignNew(ChildPanel, SImSlatePanel, this));
}

void SImSlateWindow::SetContentCollapsed(bool bInCollapsed)
{
	ChildPanel->SetVisibility(bInCollapsed ? EVisibility::Collapsed : EVisibility::SelfHitTestInvisible);
}

bool SImSlateWindow::IsContentCollapsed() const
{
	return ChildPanel->GetVisibility() != EVisibility::Collapsed;
}

void SImSlateWindow::SetShowTitleBar(bool bInShowTitleBar)
{
	HeaderHandle->SetVisibility(bInShowTitleBar ? EVisibility::Visible : EVisibility::Collapsed);
}

bool SImSlateWindow::IsShowTitleBar() const
{
	return HeaderHandle->GetVisibility() != EVisibility::Collapsed;
}

void SImSlateWindow::SetScrollTarget(FVector2D In)
{
	ChildPanel->SetScrollTarget(In);
}

void SImSlateWindow::CheckCloseButton(bool* bIn)
{
	if (bIn)
	{
		*bIn = !Hidden;
	}
}

void SImSlateWindow::SetTitleText(FText InTitle)
{
	HeaderHandle->Title = InTitle;
}

FText SImSlateWindow::GetTitleText() const
{
	return HeaderHandle->Title;
}

bool SImSlateWindow::CustomPrepass(float LayoutScaleMultiplier)
{
	return true;
}

FReply SImSlateWindow::OnFocusReceived(const FGeometry& MyGeometry, const FFocusEvent& InFocusEvent)
{
	auto Reply = SImSlateWindowBase::OnFocusReceived(MyGeometry, InFocusEvent);
	if (Reply.IsEventHandled() && Viewport)
	{
		Viewport->BringWindowToFront(ToSharedRef());
	}
	return Reply;
}

SImViewportGame* SImSlateWindow::IsAreaInGameViewport() const
{
	do
	{
		if (!Viewport)
			break;
#if WITH_EDITOR
		if (GIsEditor)
		{
			auto& g = *GImSlate;
			for (auto Gameviewport : g.Viewports)
			{
				if (!Gameviewport->Contains(GetCachedGeometry()))
					continue;
				if (!Gameviewport->IsGameViewport())
					continue;
				return (SImViewportGame*)(&Gameviewport.Get());
			}
		}
		else
#endif
		{
			auto Gameviewport = GetViewportGame();
			if (!Gameviewport)
				break;
			if (!Gameviewport->Contains(GetCachedGeometry()))
				continue;
			if (!Gameviewport->IsGameViewport())
				continue;
			return (SImViewportGame*)Gameviewport;
		}
	} while (false);
	return nullptr;
}

bool SImSlateWindow::IsViewportGame() const
{
	return Viewport == GetViewportGame();
}

SImSlateViewport* SImSlateWindow::SelectViewport()
{
	auto GameViewport = IsAreaInGameViewport();
	if (ViewportOwned && GameViewport)
	{
		return GameViewport;
	}

	if (!GameViewport && !ViewportOwned && bSupportImViewportHost)
	{
		return SelectViewportHost(this, false);
	}

	return Viewport;
}

void SImSlateWindow::UpdateViewport()
{
	if (!ensure(Viewport))
	{
		Viewport = GetViewportGame();
		ViewportOwned = false;
	}
	Viewport->AddWindow(ToSharedRef());
}

void SImSlateWindow::MoveToViewport(SImSlateViewport* InViewport)
{
	InViewport = InViewport ? InViewport : GetViewportGame();
	check(InViewport);
	if (Viewport != InViewport)
	{
		const bool bTargetGameview = InViewport->IsGameViewport();
		if (bTargetGameview)
		{
			// hostview to gameview
			if ((ViewportOwned && ensure(Viewport)))
			{
				// ReleaseViewportHost(ToSharedRef(), Viewport);
			}
		}
		else
		{
			// gameview to hostview
		}

		Viewport = InViewport;
		ViewportOwned = !bTargetGameview;
		// Prevent Appearing re-trigger on viewport switch (keep window "active")
		LastFrameActive = GImSlate->FrameCount;
	}
}

FVector2D SImSlateWindow::CalcSize(FVector2D InSize) const
{
	InSize = FVector2D::Max(MinSize, InSize);
	InSize = FVector2D::Min(MaxSize.Get(InSize), InSize);
	return InSize;
}

void SImSlateWindow::SetContentScale(const TAttribute<FVector2D>& InContentScale)
{
	if (SetAttribute(ContentScale, InContentScale, EInvalidateWidgetReason::Layout))
	{
		Invalidate(EInvalidateWidgetReason::Layout);
#if UE_5_00_OR_LATER
		MarkPrepassAsDirty();
#else
		// Invalidate(EInvalidateWidgetReason::Prepass);
		InvalidatePrepass();
#endif
	}
}

bool SImSlateWindow::ShouldShowCloseButton() const
{
	return true;
}

void SImSlateWindow::SetBgAlpha(float InAlpha)
{
	BackgroudAlpha = InAlpha;
	// SetRenderOpacity(BackgroudAlpha);
}

void SImSlateWindow::SetBgColor(const FLinearColor& InColor)
{
	ContentBackgroundBrush.TintColor = InColor;
}

void SImSlateWindow::SetWindowSize(FVector2D InSize)
{
	RequiredSize = InSize;
	if (ensure(Viewport))
	{
		Viewport->SetWindowSize(this, RequiredSize);
	}
}

void SImSlateWindow::SetWindowContentSize(FVector2D InSize)
{
	if (ensure(Viewport))
	{
		RequiredSize = InSize + FVector2D(PanelMargin.GetTotalSpaceAlong<EOrientation::Orient_Horizontal>(), PanelMargin.GetTotalSpaceAlong<EOrientation::Orient_Vertical>()) + FVector2D(0.f, GetTitleHeight());
		Viewport->SetWindowSize(this, RequiredSize);
	}
}

void SImSlateWindow::SetWindowTitleHeight(float InHeight)
{
	if (TitleHeight != InHeight)
	{
		auto Delta = InHeight - TitleHeight;
		ActualSize.Y += Delta;
		TitleHeight = InHeight;
		Invalidate(EInvalidateWidgetReason::Layout);
	}
}

FVector2D SImSlateWindow::GetContentSize() const
{
	return GetCachedGeometry().GetLocalSize() - FVector2D(0.f, GetTitleHeight());
}

void SImSlateWindow::DragingWindowSize(FVector2D InSize)
{
	InvokeResizeCallback(InSize);
}

void SImSlateWindow::InvokeResizeCallback(FVector2D InSize)
{
	if (ensure(Viewport))
	{
		if (ResizeCallback)
		{
			ImSlateSizeCallbackData Data;
			Data.Window = this;
			Data.Pos = GetWindowPos();
			Data.ContentSize = InSize - FVector2D(0.f, GetTitleHeight());
			Data.DesiredSize = Data.ContentSize;
			ResizeCallback(Data);
			if (Data.ContentSize != Data.DesiredSize)
			{
				SetWindowContentSize(FVector2D::Max(Data.DesiredSize, HeaderHandle->GetMinSize() * FVector2D(1.f, 2.f)));
			}
		}
		else
		{
			Viewport->SetWindowSize(this, FVector2D::Max(InSize, HeaderHandle->GetMinSize() * FVector2D(1.f, 2.f)));
		}
	}
}

void SImSlateWindow::DragingWindowPos(FVector2D InPos, FVector2D InAbsolutePos)
{
	if (ensure(Viewport))
	{
		Viewport->SetWindowPos(this, IsViewportGame() ? InPos : InAbsolutePos);
	}
}

FVector2D SImSlateWindow::GetWindowSize() const
{
	if (ensure(Viewport))
	{
		return Viewport->GetWindowSize(this);
	}
	return FVector2D::ZeroVector;
}

FVector2D SImSlateWindow::GetWindowPos() const
{
	if (ensure(Viewport))
	{
		return Viewport->GetWindowPos(this);
	}
	return FVector2D::ZeroVector;
}

void SImSlateWindow::BringWindowToFront()
{
	if (ensure(Viewport))
	{
		Viewport->BringWindowToFront(ToSharedRef());
	}
}

void SImSlateWindow::BeginItemFrame()
{
	if (!(Flags & ImSlateWindowFlags_EventDrived) && ChildPanel)
	{
		ChildPanel->BeginItemFrame();
	}
}

void SImSlateWindow::CommitItemFrame()
{
	if (ChildPanel)
	{
		ChildPanel->CommitItemFrame();
	}
}

void SImSlateWindow::SetItemParent(ImSlateId ItemId)
{
	if (ChildPanel) ChildPanel->SetItemParent(ItemId);
}

void SImSlateWindow::PushFoldContext(ImSlateId FoldId)
{
	if (ChildPanel) ChildPanel->PushFoldContext(FoldId);
}

void SImSlateWindow::PopFoldContext()
{
	if (ChildPanel) ChildPanel->PopFoldContext();
}

void SImSlateWindow::SetFoldCollapsed(ImSlateId FoldId, bool bCollapsed)
{
	if (ChildPanel) ChildPanel->SetFoldCollapsed(FoldId, bCollapsed);
}

void SImSlateWindow::MarkPanelLayoutDirty()
{
	if (ChildPanel)
	{
		ChildPanel->MarkLayoutDirty();
	}
}

FItemSlotPod* SImSlateWindow::FindItem(ImSlateId InId, SWidget** OutWidget, int32* OutChildIndex)
{
	auto FindIdx = ChildPanel->CachedItems.FindIndexById(InId);
	if (FindIdx != INDEX_NONE)
	{
		if (!ChildPanel->Children.IsValidIndex(FindIdx))
		{
			ChildPanel->CachedItems.DeleteByID(InId);
			return nullptr;
		}

		// During frame build, only find items from previous frame (not same-frame additions)
		if (ChildPanel->bFrameStarted && FindIdx >= ChildPanel->GetFrameBaseChildCount())
		{
			return nullptr;
		}

		auto Slot = &ChildPanel->Children[FindIdx];
		if (OutWidget)
			*OutWidget = &Slot->GetWidget().Get();
		if (OutChildIndex)
			*OutChildIndex = FindIdx;
		return Slot;
	}
	return nullptr;
}

FItemSlotPod& SImSlateWindow::AddItem(ImSlateId InId, const TSharedRef<SWidget>& Item)
{
	if (!(Flags & ImSlateWindowFlags_EventDrived))
	{
		int32 NewIndex = ChildPanel->Children.Num();
		ChildPanel->CachedItems.Add(InId, NewIndex);
	}
	return !!(Flags & ImSlateWindowFlags_EventDrived) ? ChildPanel->AddCurrentSlot(InId)[Item] : ChildPanel->AddSlot(InId)[Item];
}

bool SImSlateWindow::ReuseItem(ImSlateId InId, int32 ExistingChildIndex)
{
	if (!(Flags & ImSlateWindowFlags_EventDrived))
	{
		return ChildPanel->TouchSlot(ExistingChildIndex);
	}
	return true;
}

void SImSlateWindow::AddExistItem(ImSlateId InId, FItemSlotPod* InExistSlot)
{
	if (!(Flags & ImSlateWindowFlags_EventDrived))
	{
		int32 NewIndex = ChildPanel->Children.Num();
		ChildPanel->CachedItems.Add(InId, NewIndex);
	}
	!!(Flags & ImSlateWindowFlags_EventDrived) ? ChildPanel->AddCurrentSlot(static_cast<SImSlatePanel::FSlot*>(InExistSlot)) : ChildPanel->AddSlot(static_cast<SImSlatePanel::FSlot*>(InExistSlot));
}

FVector2D SImSlateWindow::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
	return (FVector2D)TotalDesiredSizes;
}

void SImSlateWindow::CacheDesiredSize(float LayoutScaleMultiplier)
{
	auto TmpTotalDesiredSizes = FVector2D::ZeroVector;
	if (RequiredSize.IsSet())
	{
		// always compute desired size
		if (RequiredSize.GetValue().IsZero())
		{
			if (bool bShowHeader = !(Flags & ImSlateWindowFlags_NoTitleBar))
			{
				HeaderHandle->CacheDesiredSize(LayoutScaleMultiplier);
				TmpTotalDesiredSizes.X = FMath::Max(TmpTotalDesiredSizes.X, HeaderHandle->GetDesiredSize().X);
				TmpTotalDesiredSizes.Y += TitleHeight;
			}
			if (bool bShowContent = (!Collapsed && EVisibility::Collapsed != ChildPanel->GetVisibility()))
			{
				ChildPanel->CacheDesiredSize(LayoutScaleMultiplier);
				TmpTotalDesiredSizes.X = FMath::Max(TmpTotalDesiredSizes.X, ChildPanel->GetDesiredWidth() + PanelMargin.GetTotalSpaceAlong<EOrientation::Orient_Horizontal>());
				TmpTotalDesiredSizes.Y += (ChildPanel->GetDesiredHeight() + PanelMargin.GetTotalSpaceAlong<EOrientation::Orient_Vertical>());
			}
		}
		else
		{
			TmpTotalDesiredSizes = RequiredSize.GetValue();
		}
		RequiredSize.Reset();

		TotalDesiredSizes = CalcSize(TmpTotalDesiredSizes);
		ActualSize = TotalDesiredSizes;
	}
	else if (Flags & (ImSlateWindowFlags_AlwaysAutoResize) || (SetWindowSizeAllowFlags & ImSlateCond_FirstClearMask))
	{
		if (bool bShowHeader = !(Flags & ImSlateWindowFlags_NoTitleBar))
		{
			HeaderHandle->CacheDesiredSize(LayoutScaleMultiplier);
			TmpTotalDesiredSizes.X = FMath::Max(TmpTotalDesiredSizes.X, HeaderHandle->GetDesiredSize().X);
			TmpTotalDesiredSizes.Y += TitleHeight;
		}
		if (bool bShowContent = (!Collapsed && EVisibility::Collapsed != ChildPanel->GetVisibility()))
		{
			ChildPanel->CacheDesiredSize(LayoutScaleMultiplier);
			TmpTotalDesiredSizes.X = FMath::Max(TmpTotalDesiredSizes.X, ChildPanel->GetDesiredWidth() + PanelMargin.GetTotalSpaceAlong<EOrientation::Orient_Horizontal>());
			TmpTotalDesiredSizes.Y += (ChildPanel->GetDesiredHeight() + PanelMargin.GetTotalSpaceAlong<EOrientation::Orient_Vertical>());
		}
		TotalDesiredSizes = CalcSize(TmpTotalDesiredSizes);
		ActualSize = TotalDesiredSizes;
	}

	ensure(ActualSize.HasValidSize() && TotalDesiredSizes.HasValidSize());
	if (!ViewportOwned && SetWindowPosPivot.IsSet())
	{
		ActualPos -= SetWindowPosPivot.GetValue() * ActualSize;
		SetWindowPosPivot.Reset();
	}

	if (Viewport && !ViewportOwned)
	{
		FVector2D ViewportSize = Viewport->GetCachedGeometry().GetLocalSize();
		if (ViewportSize.X > 0 && ViewportSize.Y > 0)
		{
			if (SetWindowSizeAllowFlags & ImSlateCond_FirstClearMask)
			{
				ActualSize.X = FMath::Min(ActualSize.X, ViewportSize.X);
				ActualSize.Y = FMath::Min(ActualSize.Y, ViewportSize.Y);
			}
			if (!(Flags & ImSlateWindowFlags_NoTitleBar))
			{
				constexpr float MinGrabWidth = 50.f;
				float TitleH = GetTitleHeight();
				ActualPos.X = FMath::Clamp(ActualPos.X, -(ActualSize.X - MinGrabWidth), ViewportSize.X - MinGrabWidth);
				ActualPos.Y = FMath::Clamp(ActualPos.Y, 0.f, FMath::Max(0.f, ViewportSize.Y - TitleH));
			}
		}
	}

#if 0
	if (Flags & ImSlateWindowFlags_AlwaysAutoResize)
		Flags |= ImSlateWindowFlags_NoResize;
#endif

	SetWindowSizeAllowFlags &= ~ImSlateCond_FirstClearMask;
	SImSlateWindowBase::CacheDesiredSize(LayoutScaleMultiplier);
}

FReply SImSlateWindow::OnPreviewMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if ((Flags & ImSlateWindowFlags_NoMouseInputs) == 0)
	{
		BringWindowToFront();
	}
	return SImSlateWindowBase::OnPreviewMouseButtonDown(MyGeometry, MouseEvent);
}

FReply SImSlateWindow::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	// Slate routes events leaf→root. If we reach here, no child widget handled the click.
	// Safe to start drag detection for window movement.
	if (MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton) && !(Flags & ImSlateWindowFlags_NoMove))
	{
		return FReply::Handled().DetectDrag(this->AsShared(), EKeys::LeftMouseButton).PreventThrottling();
	}
	return FReply::Unhandled();
}

FReply SImSlateWindow::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	return ChildPanel->OnMouseWheel(ChildPanel->GetCachedGeometry(), MouseEvent);
}

float SImSlateWindow::GetTitleHeight() const
{
	if (Flags & ImSlateWindowFlags_NoTitleBar)
		return 0.f;
	return TitleHeight * ImSlate::GetImSlateEffectiveScale();
}

FReply SImSlateWindow::OnDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if ((Flags & ImSlateWindowFlags_NoMove) == 0)
	{
		FVector2D AbsGrabOffset = MouseEvent.GetScreenSpacePosition() - MyGeometry.GetAbsolutePosition();
		TSharedRef<FImSlateDragOperation> DragDropOperation = FImSlateDragOperation::New(ToSharedRef(), AbsGrabOffset);
		return FReply::Handled().BeginDragDrop(DragDropOperation).PreventThrottling();
	}
	return SImSlateWindowBase::OnDragDetected(MyGeometry, MouseEvent);
}

void SImSlateWindow::OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const
{
	float OffsetHeight = 0;
	auto HeaderHandleVis = HeaderHandle->GetVisibility();
	if (bool bShowHeaderHandle = !(Flags & ImSlateWindowFlags_NoTitleBar) && ArrangedChildren.Accepts(HeaderHandleVis))
	{
		FVector2D HeaderSize = AllottedGeometry.GetLocalSize();
		OffsetHeight = GetTitleHeight();
		HeaderSize.Y = OffsetHeight;
		ArrangedChildren.AddWidget(HeaderHandleVis, AllottedGeometry.MakeChild(HeaderHandle.ToSharedRef(), FVector2D::ZeroVector, HeaderSize));
	}

	auto ChildPanelVis = ChildPanel->GetVisibility();
	if (bool bShowContent = !Collapsed && ArrangedChildren.Accepts(ChildPanelVis))
	{
		const FVector2D PanelSize{AllottedGeometry.GetLocalSize().X, AllottedGeometry.GetLocalSize().Y - OffsetHeight};
		ArrangedChildren.AddWidget(ChildPanelVis, AllottedGeometry.MakeChild(ChildPanel.ToSharedRef(), FVector2D(0.f, OffsetHeight) + PanelMargin.GetTopLeft(), PanelSize - PanelMargin.GetDesiredSize()));
	}

	if (bShowResizeHandle)
	{
		ArrangedChildren.AddWidget(EVisibility::Visible, AllottedGeometry.MakeChild(ResizeArea.ToSharedRef(), AllottedGeometry.GetLocalSize() - FVector2D{ResizeHandleWidth}, FVector2D{ResizeHandleWidth}));
	}
}

/*
	╭-------------------------------------------------╮
	|                                                 | <-- HeaderHandle
	├-------------------------------------------------┤
	|                                                 │
	|                                                 │
	|  ChildPanel                                     | 
	|                                                 |
	|                                                 │
	|                                               ┌─┤
	╰-----------------------------------------------┴-╯ <-- ResizeHandle
*/
int32 SImSlateWindow::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	FArrangedChildren ArrangedChildren(EVisibility::Visible);
	this->ArrangeChildren(AllottedGeometry, ArrangedChildren);

	int32 ContentLayerId = LayerId + 1;
	int32 MaxLayerId = ContentLayerId;
	if (ArrangedChildren.Num() > 0)
	{
		FSlateRect MyNewCullingRect(AllottedGeometry.GetRenderBoundingRect(CullingBoundsExtension));
		FSlateClippingZone ClippingZone(MyCullingRect.IntersectionWith(MyNewCullingRect));
		OutDrawElements.PushClip(ClippingZone);
		ON_SCOPE_EXIT { OutDrawElements.PopClip(); };

		// Because we paint multiple children, we must track the maximum layer id that they produced in case one of our parents wants to an overlay for all of its contents.
		const FPaintArgs NewArgs = Args.WithNewParent(this);
		const bool bShouldBeEnabled = ShouldBeEnabled(bParentEnabled);

		for (int32 ChildIndex = 0; ChildIndex < ArrangedChildren.Num(); ++ChildIndex)
		{
			const FArrangedWidget& CurWidget = ArrangedChildren[ChildIndex];

			if (!IsChildWidgetCulled(MyNewCullingRect, CurWidget))
			{
				ContentLayerId = CurWidget.Widget->Paint(NewArgs, CurWidget.Geometry, MyNewCullingRect, OutDrawElements, ContentLayerId, InWidgetStyle, bShouldBeEnabled);
				MaxLayerId = FMath::Max(MaxLayerId, ContentLayerId);
			}
			else
			{
				//SlateGI - RemoveContent
			}
		}

		auto HeaderHandleVis = HeaderHandle->GetVisibility();
		const bool bShowHeaderHandle = !(Flags & ImSlateWindowFlags_NoTitleBar) && ArrangedChildren.Accepts(HeaderHandleVis);
		const float HeaderHeight = bShowHeaderHandle ? GetTitleHeight() : 0.f;

		auto ChildPanelVis = ChildPanel->GetVisibility();
		const bool bShowContent = ArrangedChildren.Accepts(ChildPanelVis);
		if (bShowContent)
		{
			TArray<SlateIndex> Triangles;
			Triangles.SetNumZeroed(6);
			Triangles[0] = 0;
			Triangles[1] = 1;
			Triangles[2] = 3;

			Triangles[3] = 1;
			Triangles[4] = 3;
			Triangles[5] = 2;

			TArray<FVector2D> Points;
			TArray<FSlateVertex> Vertices;
			Vertices.SetNumZeroed(4);
			Points.SetNumZeroed(4);

			Points[0] = FVector2D(0.f, HeaderHeight);
			Points[1] = FVector2D(AllottedGeometry.GetLocalSize().X, HeaderHeight);
			Points[2] = FVector2D(AllottedGeometry.GetLocalSize().X, AllottedGeometry.GetLocalSize().Y);
			Points[3] = FVector2D(0.f, AllottedGeometry.GetLocalSize().Y);

			auto LinearColor = ContentBackgroundBrush.TintColor.GetSpecifiedColor();
			LinearColor.A = BackgroudAlpha;
			const FColor Color = LinearColor.ToFColor(true);

			Vertices[0].Position = (FVector2f)Points[0];
			Vertices[0].MaterialTexCoords = Vertices[0].Position;
			Vertices[0].Color = Color;
			Vertices[0].Position = AllottedGeometry.ToPaintGeometry().GetAccumulatedRenderTransform().TransformPoint(Vertices[0].Position);

			Vertices[1].Position = (FVector2f)Points[1];
			Vertices[1].MaterialTexCoords = Vertices[1].Position;
			Vertices[1].Color = Color;
			Vertices[1].Position = AllottedGeometry.ToPaintGeometry().GetAccumulatedRenderTransform().TransformPoint(Vertices[1].Position);

			Vertices[2].Position = (FVector2f)Points[2];
			Vertices[2].MaterialTexCoords = Vertices[2].Position;
			Vertices[2].Color = Color;
			Vertices[2].Position = AllottedGeometry.ToPaintGeometry().GetAccumulatedRenderTransform().TransformPoint(Vertices[2].Position);

			Vertices[3].Position = (FVector2f)Points[3];
			Vertices[3].MaterialTexCoords = Vertices[3].Position;
			Vertices[3].Color = Color;
			Vertices[3].Position = AllottedGeometry.ToPaintGeometry().GetAccumulatedRenderTransform().TransformPoint(Vertices[3].Position);

			FSlateDrawElement::MakeCustomVerts(OutDrawElements, LayerId, ContentBackgroundBrush.GetRenderingResource(), Vertices, Triangles, nullptr, 0, 0);
			//FSlateDrawElement::MakeCustomVerts(OutDrawElements, LayerId, Handle, VertexBuffer, IndexBuffer, nullptr, 0, 0);
		}

		//DockingCross
		if (bShowDockingCross)
		{
		}
	}
	return MaxLayerId;
}

void SImSlateWindow::OnClippingChanged()
{
	ChildPanel->SetClipping(GetClipping());
}

}  // namespace ImSlate

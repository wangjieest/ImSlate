// Copyright ImSlate, Inc. All Rights Reserved.
#include "SImSlatePanel.h"

#include "Layout/LayoutUtils.h"
#include "Layout/Margin.h"
#include "SImSlateWindow.h"
#include "Types/SlateEnums.h"

namespace ImSlate
{
extern void PrepassInternal(const TSharedRef<SWidget>& InWidget, float LayoutScaleMultiplier);

//////////////////////////////////////////////////////////////////////////

int32 SImSlatePanel::RemoveSlot(const TSharedRef<SWidget>& SlotWidget)
{
	for (int32 SlotIdx = 0; SlotIdx < Children.Num(); ++SlotIdx)
	{
		if (SlotWidget == Children[SlotIdx].GetWidget())
		{
			Children.RemoveAt(SlotIdx);
			if (SlotWidget->GetVisibility() != EVisibility::Collapsed)
			{
				Invalidate(EInvalidateWidget::LayoutAndVolatility);
				MarkPrepassAsDirty();
			}
			return SlotIdx;
		}
	}

	return -1;
}

int32 SImSlatePanel::RemoveSlot(uint32 InKey)
{
	for (int32 SlotIdx = 0; SlotIdx < Children.Num(); ++SlotIdx)
	{
		if (InKey == Children[SlotIdx].Hash)
		{
			auto SlotWidget = Children[SlotIdx].GetWidget();
			Children.RemoveAt(SlotIdx);
			if (SlotWidget->GetVisibility() != EVisibility::Collapsed)
			{
				Invalidate(EInvalidateWidget::LayoutAndVolatility);
				MarkPrepassAsDirty();
			}
			return SlotIdx;
		}
	}

	return -1;
}

void SImSlatePanel::ClearChildren()
{
	Children.Empty();
	NewChildren.Empty();

	Invalidate(EInvalidateWidget::LayoutAndVolatility);
	MarkPrepassAsDirty();
}

 SImSlatePanel::SImSlatePanel()
	: Children(this)
	, NewChildren(this)
{
	SetCanTick(false);
	bCanSupportFocus = false;
}

SImSlatePanel::~SImSlatePanel()
{
	Children.ResetSlots();
	NewChildren.ResetSlots(true);
}

void SImSlatePanel::Construct(const FArguments& InArgs, SImSlateWindow* InParent)
{
#if UE_5_00_OR_LATER
	const int32 NumSlots = InArgs._Slots.Num();
	for (int32 SlotIndex = 0; SlotIndex < NumSlots; ++SlotIndex)
	{
		Children.Add(InArgs._Slots[SlotIndex].GetSlot());
	}
#else
	const int32 NumSlots = InArgs.Slots.Num();
	for (int32 SlotIndex = 0; SlotIndex < NumSlots; ++SlotIndex)
	{
		Children.Add(InArgs.Slots[SlotIndex]);
	}
#endif
	Window = InParent;

	SetVisibility(EVisibility::SelfHitTestInvisible);
	bHideScrollBar = InArgs._HideScollBar || (Window->Flags & ImSlateWindowFlags_NoScrollbar);

	SetClipping(InParent->GetClipping());

	SAssignNew(ScrollHandle, SScrollBar)
	.AlwaysShowScrollbar(false)
	.Padding(FMargin(1.f, 0.f))
	.HideWhenNotInUse(true)
	.Orientation(EOrientation::Orient_Vertical)
	.OnUserScrolled(this, &SImSlatePanel::OnScrollFraction);
}

void SImSlatePanel::OnScrollFraction(float InFraction)
{
	OnScrollOffset(InFraction * TotalActualSize.Y);
}

bool SImSlatePanel::OnScrollOffset(float NewOffset)
{
	if (!FMath::IsNearlyEqual(ScrollOffset, NewOffset))
	{
		NewOffset = FMath::Clamp(NewOffset, 0.f, TotalActualSize.Y - GetCachedGeometry().GetLocalSize().Y);
		ScrollOffset = NewOffset;
		Invalidate(EInvalidateWidget::Layout);
		MarkPrepassAsDirty();
		return true;
	}
	return false;
}

void SImSlatePanel::OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const
{
	if (Children.Num() <= 0)
		return;

	const auto PanelSize = AllottedGeometry.GetLocalSize();
	const auto PanelHeight = PanelSize.Y;

	ScrollHandle->SetState(ScrollOffset / TotalActualSize.Y, PanelHeight / TotalActualSize.Y);

	const bool bShowScrollBar = !bHideScrollBar && ScrollHandle->IsNeeded();
	const auto PanelWidth = PanelSize.X - (bShowScrollBar ? ScrollBarWidth : 0.f);
	ON_SCOPE_EXIT
	{
		ScrollHandle->SetState(ScrollOffset / TotalActualSize.Y, PanelHeight / TotalActualSize.Y);
		if (bShowScrollBar)
		{
			ArrangedChildren.AddWidget(EVisibility::Visible, AllottedGeometry.MakeChild(ScrollHandle.ToSharedRef(), FVector2D(PanelWidth, 0.f), FVector2D(ScrollBarWidth, PanelHeight)));
		}
	};

	float ItemArrangingOffset = 0.f;
	struct FStretchInfo
	{
		float Factor = 0.f;
		float Occupied = 0.f;

		float ColLeft = 0.f;
		float ColRight = 0.f;
		TArray<const FSlot*> Slots;
	};

	struct FRowInfo
	{
		TArray<FStretchInfo, TInlineAllocator<4>> ColumnInfo = {{}};
		float CurColLeft = 0.f;
		float CurColRight = 0.f;
		float RowTop = 0.f;
		float RowMaxHeight = 0.f;
		bool bHasAspectRatio = false;
		FStretchInfo& AddColumn(float InColRight)
		{
			if (ColumnInfo.Num() > 0)
			{
				Column().ColLeft = CurColLeft;
				Column().ColRight = CurColRight;
				CurColLeft = CurColRight;
				CurColRight = InColRight;
			}
			else
			{
				CurColLeft = 0.f;
				CurColRight = InColRight;
			}

			FStretchInfo& Ref = ColumnInfo.Emplace_GetRef(FStretchInfo());
			Ref.ColLeft = CurColLeft;
			Ref.ColRight = CurColRight;
			return Ref;
		}
		FStretchInfo& Column() { return ColumnInfo.Last(); }
	};

	FRowInfo CurRowInfo;
	int32 RowIdx = 0;
	auto ArrangeCurrentRow = [&] /* return should continue next */ {
		const auto DesiredRowHeight = RowHeights[RowIdx++];
#if WITH_EDITOR && 0
		struct FRowHeightScope
		{
			const float& Desired;
			const float& Calculated;
			FRowHeightScope(const float& InDesired, const float& InCalculated)
				: Desired(InDesired)
				, Calculated(InCalculated)
			{
				EnsureEqual();
			}
			~FRowHeightScope() { EnsureEqual(); }
			void EnsureEqual() const { ensure(Desired == Calculated); }
		} RoheightScope(DesiredRowHeight, CurRowInfo.RowMaxHeight);
#endif

		ON_SCOPE_EXIT
		{
			ItemArrangingOffset += CurRowInfo.RowMaxHeight;
			CurRowInfo.bHasAspectRatio = false;
		};
		if (ItemArrangingOffset + CurRowInfo.RowMaxHeight < ScrollOffset)
			return true;

		if (ItemArrangingOffset >= ScrollOffset + PanelHeight)
			return false;

		if (CurRowInfo.bHasAspectRatio)
			CurRowInfo.RowMaxHeight = 0.f;
		TArray<FSlateRect> ItemLayouts;
		for (auto i = 0; i < CurRowInfo.ColumnInfo.Num(); ++i)
		{
			const auto& ColInfo = CurRowInfo.ColumnInfo[i];
			float OffsetInCol = 0.f;
			for (auto ChildSlot : ColInfo.Slots)
			{
				const auto& CurChild = *ChildSlot;
				const FVector2D ChildDesiredSize = CurChild.GetWidget()->GetDesiredSize();
				FVector2D ChildSize(0.f, 0.f);
				ChildSize.Y = ChildDesiredSize.Y;
				if (CurChild.bFillWidth)
				{
					if (ColInfo.Factor > 0.f)
					{
						// Stretch widgets get a fraction of the space remaining after all the fixed-space requirements are met
						ChildSize.X = (PanelWidth - ColInfo.Occupied) * CurChild.StretchValue / ColInfo.Factor;
					}
				}
				else
				{
					// Auto-sized widgets get their desired-size value
					ChildSize.X = ChildDesiredSize.X;
				}

				ChildSize = CurChild.GetSize(ChildSize);
				const FMargin SlotPadding(CurChild.SlotPadding);
				FVector2D SlotSize = FVector2D(ChildSize.X + SlotPadding.GetTotalSpaceAlong<Orient_Horizontal>(), ChildSize.Y);

				const EVisibility ChildVisibility = CurChild.GetWidget()->GetVisibility();
				if (ChildVisibility != EVisibility::Collapsed)
				{
					// Figure out the size and local position of the child within the slot
					AlignmentArrangeResult XAlignmentResult = AlignChild<Orient_Horizontal>(SlotSize.X, CurChild, SlotPadding);
					AlignmentArrangeResult YAlignmentResult = AlignChild<Orient_Vertical>(SlotSize.Y, CurChild, SlotPadding);

					// remove paddings
					FVector2D LocalPosition = FVector2D(ColInfo.ColLeft + OffsetInCol + XAlignmentResult.Offset, CurRowInfo.RowTop + YAlignmentResult.Offset - ScrollOffset);
					FVector2D LocalSize = FVector2D(XAlignmentResult.Size, YAlignmentResult.Size);

					if (CurRowInfo.bHasAspectRatio && CurChild.AspectRatio > 0.f)
					{
						auto ActualRatio = LocalSize.X / LocalSize.Y;
						if (!FMath::IsNearlyEqual(ActualRatio, CurChild.AspectRatio))
						{
							LocalSize.Y = LocalSize.X / CurChild.AspectRatio;
						}
						CurRowInfo.RowMaxHeight = FMath::Max(CurRowInfo.RowMaxHeight, LocalSize.Y);
					}
					else
					{
						// CurRowInfo.RowMaxHeight = FMath::Max(CurRowInfo.RowMaxHeight, CurChild.GetHeight(ChildDesiredSize.Y) + SlotPadding.GetTotalSpaceAlong<Orient_Vertical>());
					}
					// Add the information about this child to the output list
					ItemLayouts.Emplace(LocalPosition, LocalPosition + LocalSize);
				}

				OffsetInCol += SlotSize.X;
			}
		}

		int32 ItemIndex = 0;
		for (auto i = 0; i < CurRowInfo.ColumnInfo.Num(); ++i)
		{
			const auto& ColInfo = CurRowInfo.ColumnInfo[i];
			for (auto ChildSlot : ColInfo.Slots)
			{
				const auto& CurChild = *ChildSlot;
				const EVisibility ChildVisibility = CurChild.GetWidget()->GetVisibility();
				if (ChildVisibility != EVisibility::Collapsed)
				{
					FVector2D LocalPosition = ItemLayouts[ItemIndex].GetTopLeft();
					FVector2D LocalSize = ItemLayouts[ItemIndex].GetSize();
					++ItemIndex;
					if (CurRowInfo.RowMaxHeight > LocalSize.Y)
					{
						switch (CurChild.VAlignment)
						{
							case VAlign_Center:
								LocalPosition.Y += (CurRowInfo.RowMaxHeight - LocalSize.Y) / 2;
								break;
							case VAlign_Fill:
								LocalSize.Y = CurRowInfo.RowMaxHeight;
								break;
							case VAlign_Bottom:
								LocalPosition.Y += (CurRowInfo.RowMaxHeight - LocalSize.Y);
								break;
							case VAlign_Top:
								break;
							default:
								break;
						}
					}
					ArrangedChildren.AddWidget(ChildVisibility, AllottedGeometry.MakeChild(CurChild.GetWidget(), LocalPosition, LocalSize));
				}
			}
		}
		return true;
	};

	const float RightBoundry = PanelWidth;
	int32 LastChildVisibleIdx = -1;
	bool bPrevBreak = false;
	int32 ColumnIdx = 0;
	for (int32 ChildIndex = 0; ChildIndex < Children.Num(); ++ChildIndex)
	{
		const auto& CurChild = Children[ChildIndex];
		const bool bArrangeOnNewRow = bPrevBreak || CurChild.bNewRow;
		if (bArrangeOnNewRow)
			ColumnIdx = 0;

		// if (CurChild.GetWidget()->GetVisibility() != EVisibility::Collapsed)
		{
			auto& SlotPadding = CurChild.SlotPadding;
			FVector2D ChildDesiredSize = CurChild.GetWidget()->GetDesiredSize();

			if (bArrangeOnNewRow)
			{
				if (LastChildVisibleIdx >= 0)
				{
					if (!ArrangeCurrentRow())
						return;
				}

				CurRowInfo.RowTop = CurRowInfo.RowTop + CurRowInfo.RowMaxHeight;
				CurRowInfo.RowMaxHeight = 0.f;
				CurRowInfo.CurColLeft = 0.f;
				CurRowInfo.CurColRight = RightBoundry;
				CurRowInfo.ColumnInfo.Empty();
				CurRowInfo.AddColumn(RightBoundry);
			}

			CurRowInfo.Column().Slots.Add(&CurChild);
			CurRowInfo.Column().Occupied += SlotPadding.GetTotalSpaceAlong<Orient_Horizontal>();
			CurRowInfo.RowMaxHeight = FMath::Max(CurRowInfo.RowMaxHeight, CurChild.GetHeight(ChildDesiredSize.Y) + SlotPadding.GetTotalSpaceAlong<Orient_Vertical>());
			CurRowInfo.bHasAspectRatio |= CurChild.AspectRatio > 0.f;

			if (CurChild.bFillWidth)
			{
				CurRowInfo.Column().Factor += CurChild.StretchValue;
			}
			else
			{
				CurRowInfo.Column().Occupied += CurChild.GetWidth(ChildDesiredSize.X);
			}

			if (CurChild.bAlignCol && ColumnAlignOff.IsValidIndex(CurChild.StretchToCol))
			{
				CurRowInfo.CurColRight = ColumnAlignOff[CurChild.StretchToCol];
				CurRowInfo.AddColumn(RightBoundry);
			}

			if (LastChildVisibleIdx < 0)
				LastChildVisibleIdx = ChildIndex;
		}

		++ColumnIdx;
		bPrevBreak = CurChild.bBreakLine;
	}

	if (LastChildVisibleIdx >= 0)
		ArrangeCurrentRow();
}

FChildren* SImSlatePanel::GetChildren()
{
	return &Children;
}

FVector2D SImSlatePanel::ComputeDesiredSize(float Scale) const
{
	return TotalActualSize;
}

void SImSlatePanel::CacheDesiredSize(float LayoutScaleMultiplier)
{
	TotalActualSize = FVector2D::ZeroVector;
	RowHeights.Empty();
	FVector2D AccDesiredSize = FVector2D::ZeroVector;

	auto UpdateDesiredSize = [&] {
		TotalActualSize.X = FMath::Max(AccDesiredSize.X, TotalActualSize.X);
		TotalActualSize.Y += AccDesiredSize.Y;
		RowHeights.Add(AccDesiredSize.Y);
	};

	bool bPrevBreak = false;
	int32 ColumnIdx = 0;
	for (int32 ChildIndex = 0; ChildIndex < Children.Num(); ++ChildIndex)
	{
		const auto& CurChild = Children[ChildIndex];
		const bool bArrangeOnNewRow = bPrevBreak || CurChild.bNewRow;
		if (bArrangeOnNewRow)
			ColumnIdx = 0;
		if (CurChild.GetWidget()->GetVisibility() != EVisibility::Collapsed)
		{
			FVector2D CurChildDesiredSize = CurChild.GetWidget()->GetDesiredSize();
			const auto ActualDesiredSize = CurChild.GetSize(CurChildDesiredSize);

			float ChildRowDesiredWidth = ActualDesiredSize.X + CurChild.SlotPadding.GetTotalSpaceAlong<Orient_Horizontal>();

			if (CurChild.bAlignCol && ColumnAlignOff.IsValidIndex(CurChild.StretchToCol))
			{
				ChildRowDesiredWidth = FMath::Max(0.f, ColumnAlignOff[CurChild.StretchToCol] - AccDesiredSize.X);
			}

			float FinalChildDesiredHeight = ActualDesiredSize.Y + CurChild.SlotPadding.GetTotalSpaceAlong<Orient_Vertical>();

			if (bArrangeOnNewRow)
			{
				UpdateDesiredSize();
				AccDesiredSize.X = 0.f;
				AccDesiredSize.Y = 0.f;
			}
			AccDesiredSize.X += ChildRowDesiredWidth;
			AccDesiredSize.Y = FMath::Max(AccDesiredSize.Y, FinalChildDesiredHeight);
			++ColumnIdx;
		}
		bPrevBreak = CurChild.bBreakLine;
	}
	UpdateDesiredSize();

	RowHeights.Add(TotalActualSize.Y);
	if ((Window->Flags & ImSlateWindowFlags_NoScrollbar) == 0 && ScrollHandle->IsNeeded())
		PrepassInternal(ScrollHandle.ToSharedRef(), LayoutScaleMultiplier);

	SPanel::CacheDesiredSize(LayoutScaleMultiplier);
}

FReply SImSlatePanel::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if ((Window->Flags & ImSlateWindowFlags_NoScrollWithMouse) == 0 && ScrollHandle->IsNeeded())
	{
		auto NewScrollOffset = ScrollOffset;
		NewScrollOffset -= MouseEvent.GetWheelDelta() * GetGlobalScrollAmount();
		if (OnScrollOffset(NewScrollOffset))
		{
			return FReply::Handled();
		}
	}

	return FReply::Unhandled();
}

int32 SImSlatePanel::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	FArrangedChildren ArrangedChildren(EVisibility::Visible);
	ArrangeChildren(AllottedGeometry, ArrangedChildren);

	FSlateRect MyNewCullingRect(AllottedGeometry.GetRenderBoundingRect(CullingBoundsExtension));
	FSlateClippingZone ClippingZone(MyCullingRect.IntersectionWith(MyNewCullingRect));
	OutDrawElements.PushClip(ClippingZone);
	ON_SCOPE_EXIT { OutDrawElements.PopClip(); };

	return PaintArrangedChildren(Args, ArrangedChildren, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
}

float SImSlatePanel::GetDesiredWidth() const
{
	return GetDesiredSize().X;
}

float SImSlatePanel::GetDesiredHeight() const
{
	return GetDesiredSize().Y;
}

}  // namespace ImSlate

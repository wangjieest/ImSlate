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
	FrameOrder.Reset();

	Invalidate(EInvalidateWidget::LayoutAndVolatility);
	MarkPrepassAsDirty();
}

 SImSlatePanel::SImSlatePanel()
	: Children(this)
{
	SetCanTick(false);
	bCanSupportFocus = false;
}

SImSlatePanel::~SImSlatePanel()
{
	Children.ResetSlots();
}

bool SImSlatePanel::CommitItemFrame()
{
	if (!bFrameStarted)
		return false;
	bFrameStarted = false;

	const int32 NumFrame = FrameOrder.Num();
	const int32 NumChildren = Children.Num();

	const bool bHasNewItems = (NumChildren > FrameBaseChildCount);

	// Count collapsed items in Children
	int32 NumCollapsed = 0;
	for (int32 i = 0; i < NumChildren; ++i)
	{
		if (IsInCollapsedSubtree(Children[i].Hash))
			++NumCollapsed;
	}

	// Fast path: active items + collapsed items = total children, same order, no new items
	if ((NumFrame + NumCollapsed) == NumChildren && !bHasNewItems && bFrameOrderIsSequential)
	{
		if (bLayoutDirty)
		{
			Invalidate(EInvalidateWidget::Layout);
			MarkPrepassAsDirty();
		}
		// else: zero cost — nothing changed, Slate uses cached geometry
		return false;
	}

	// Determine what actually changed
	TSet<int32> UsedSet;
	UsedSet.Reserve(NumFrame);
	for (int32 Idx : FrameOrder)
		UsedSet.Add(Idx);

	// Collapsed fold children count as "used" — don't remove them
	const int32 TotalUsed = UsedSet.Num() + NumCollapsed;
	const bool bNeedsRemove = (TotalUsed < NumChildren);
	const bool bNeedsReorder = !bFrameOrderIsSequential;

	if (bNeedsReorder || bNeedsRemove)
	{
		// Full rebuild: steal pointers, reorder, keep collapsed, delete truly unused
		TArray<FSlot*> OldPtrs;
		OldPtrs.SetNumUninitialized(NumChildren);
		FSlot** RawData = Children.Slots.GetData();
		for (int32 i = 0; i < NumChildren; ++i)
		{
			OldPtrs[i] = RawData[i];
			RawData[i] = nullptr;
		}
		Children.Empty();

		// Re-add active items in frame order
		for (int32 Idx : FrameOrder)
		{
			Children.Add(OldPtrs[Idx]);
			OldPtrs[Idx] = nullptr;
		}

		// Re-add items in collapsed subtrees (keep them alive)
		for (int32 i = 0; i < OldPtrs.Num(); ++i)
		{
			if (OldPtrs[i] && IsInCollapsedSubtree(OldPtrs[i]->Hash))
			{
				Children.Add(OldPtrs[i]);
				OldPtrs[i] = nullptr;
			}
		}

		// Delete truly unused
		for (FSlot* Ptr : OldPtrs)
		{
			if (Ptr)
			{
				ResetSlotBase(Ptr);
				delete Ptr;
			}
		}
	}
	// else: items were just appended in order — Children is already correct, no Empty+Add needed

	// Rebuild cache (always, since indices may have changed or new items added)
	CachedItems = FCachedItem();
	for (int32 i = 0; i < Children.Num(); ++i)
	{
		CachedItems.Add(Children[i].Hash, i);
	}

	Invalidate(EInvalidateWidgetReason::ChildOrder | EInvalidateWidgetReason::Paint);
	return true;
}

bool SImSlatePanel::IsInCollapsedSubtree(ImSlateId ItemId) const
{
	const ImSlateId* ParentPtr = ItemParentMap.Find(ItemId);
	if (!ParentPtr || *ParentPtr == 0)
		return false;  // root level

	ImSlateId ParentId = *ParentPtr;
	if (CollapsedFoldIds.Contains(ParentId))
		return true;  // direct parent is collapsed

	return IsInCollapsedSubtree(ParentId);  // check ancestors
}

void SImSlatePanel::SetItemParent(ImSlateId ItemId)
{
	ImSlateId ParentId = FoldContextStack.Num() > 0 ? FoldContextStack.Last() : 0;
	ItemParentMap.FindOrAdd(ItemId) = ParentId;
}

void SImSlatePanel::PushFoldContext(ImSlateId FoldId)
{
	// Opening a fold — remove from collapsed set, un-collapse children
	if (CollapsedFoldIds.Remove(FoldId) > 0)
	{
		for (int32 i = 0; i < Children.Num(); ++i)
		{
			const TSharedRef<SWidget>& Widget = Children[i].GetWidget();
			if (Widget == SNullWidget::NullWidget)
				continue;
			ImSlateId ChildHash = Children[i].Hash;
			const ImSlateId* ParentPtr = ItemParentMap.Find(ChildHash);
			if (ParentPtr && *ParentPtr == FoldId && !IsInCollapsedSubtree(ChildHash))
			{
				Widget->SetVisibility(EVisibility::Visible);
			}
		}
		bLayoutDirty = true;
	}
	FoldContextStack.Push(FoldId);
}

void SImSlatePanel::PopFoldContext()
{
	if (FoldContextStack.Num() > 0)
		FoldContextStack.Pop();
}

void SImSlatePanel::SetFoldCollapsed(ImSlateId FoldId, bool bCollapsed)
{
	if (bCollapsed)
	{
		bool bAlreadyCollapsed = false;
		CollapsedFoldIds.Add(FoldId, &bAlreadyCollapsed);
		if (!bAlreadyCollapsed)
		{
			for (int32 i = 0; i < Children.Num(); ++i)
			{
				const TSharedRef<SWidget>& Widget = Children[i].GetWidget();
				if (Widget != SNullWidget::NullWidget && IsInCollapsedSubtree(Children[i].Hash))
				{
					Widget->SetVisibility(EVisibility::Collapsed);
				}
			}
			bLayoutDirty = true;
		}
	}
	else
	{
		PushFoldContext(FoldId);  // reuse un-collapse logic
		PopFoldContext();         // but don't keep it on the stack
	}
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

	const float SafeTotalHeight = FMath::Max(TotalActualSize.Y, 1.f);
	ScrollHandle->SetState(ScrollOffset / SafeTotalHeight, PanelHeight / SafeTotalHeight);

	const bool bShowScrollBar = !bHideScrollBar && ScrollHandle->IsNeeded();
	const auto PanelWidth = PanelSize.X - (bShowScrollBar ? ScrollBarWidth : 0.f);
	ON_SCOPE_EXIT
	{
		ScrollHandle->SetState(ScrollOffset / SafeTotalHeight, PanelHeight / SafeTotalHeight);
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
	auto ArrangeCurrentRow = [&] /* return should continue next */ {
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

		if (CurChild.GetWidget()->GetVisibility() != EVisibility::Collapsed)
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

	// Clamp scroll offset when content size changes (e.g. fold/unfold)
	{
		float PanelHeight = GetCachedGeometry().GetLocalSize().Y;
		float MaxScroll = FMath::Max(0.f, TotalActualSize.Y - PanelHeight);
		ScrollOffset = FMath::Clamp(ScrollOffset, 0.f, MaxScroll);
	}

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

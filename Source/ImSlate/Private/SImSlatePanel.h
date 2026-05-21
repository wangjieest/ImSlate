// Copyright ImSlate, Inc. All Rights Reserved.
#pragma once
#include "ImSlateFwd.h"
#include "SImSlateLayout.h"
#include "SlotBase.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Layout/SScrollBar.h"
#include "Widgets/SPanel.h"

namespace ImSlate
{
class SImSlateWindow;
class SImSlatePanel final : public SPanel
{
public:
	class FSlot
		: public FItemSlotPod
		, public TSlotBase<FSlot>
	{
	public:
		FSlot(uint32 InKey)
			: FItemSlotPod(InKey)
			, TSlotBase<FSlot>()

		{
		}

		FSlot& Padding(FMargin InPadding)
		{
			SlotPadding = InPadding;
			return *this;
		}

		FSlot& Padding(float Left, float Top, float Right, float Bottom)
		{
			SlotPadding = FMargin(Left, Top, Right, Bottom);
			return *this;
		}

		FSlot& Padding(float Horizontal, float Vertical)
		{
			SlotPadding = FMargin(Horizontal, Vertical);
			return *this;
		}

		FSlot& Padding(float Uniform)
		{
			SlotPadding = FMargin(Uniform);
			return *this;
		}

		FSlot& AutoWidth()
		{
			bFillWidth = 0;
			return *this;
		}

		FSlot& FillWidth(float StretchCoefficient)
		{
			bFillWidth = 1;
			StretchValue = StretchCoefficient;
			return *this;
		}

		FSlot& StretchToColumn(int32 InStretchToCol)
		{
			bAlignCol = 1;
			StretchToCol = InStretchToCol;
			return *this;
		}

		FSlot& NewRow()
		{
			bNewRow = 1;
			return *this;
		}

		FSlot& BreakLine()
		{
			bBreakLine = 1;
			return *this;
		}

		FSlot& Apply(const FItemSlotPod& Other)
		{
			static_cast<FItemSlotPod&>(*this) = Other;
			Invalidate(EInvalidateWidgetReason::Layout);
			return *this;
		}

		FSlot& operator=(const FItemSlotPod& Other) { return Apply(Other); }

		friend bool operator==(const FSlot& Lhs, const FSlot& Rhs) { return Lhs.Hash == Rhs.Hash && Lhs.GetWidget() == Rhs.GetWidget() && Lhs.FlagBits == Rhs.FlagBits && Lhs.StretchToCol == Rhs.StretchToCol; }

		EHorizontalAlignment GetHorizontalAlignment() const { return (EHorizontalAlignment)HAlignment; }

		EVerticalAlignment GetVerticalAlignment() const { return (EVerticalAlignment)VAlignment; }
	};

	static FSlot& Slot(uint32 InKey) { return *(new FSlot(InKey)); }

	FSlot& AddSlot(uint32 InKey)
	{
		FSlot& NewSlot = Slot(InKey);
		int32 Index = this->Children.Num();
		this->Children.Add(&NewSlot);
		if (bFrameStarted)
			FrameOrder.Add(Index);
		// Always Invalidate — Slate must know about new children (like SlateIM's Slot.SetContent)
		Invalidate(EInvalidateWidget::Layout | EInvalidateWidgetReason::ChildOrder);
		return NewSlot;
	}

	void AddSlot(FSlot* InSlot)
	{
		int32 Index = this->Children.Num();
		this->Children.Add(InSlot);
		if (bFrameStarted)
			FrameOrder.Add(Index);
		Invalidate(EInvalidateWidget::Layout | EInvalidateWidgetReason::ChildOrder);
	}

	// EventDrived mode: add directly without frame tracking
	FSlot& AddCurrentSlot(uint32 InKey)
	{
		FSlot& NewSlot = Slot(InKey);
		this->Children.Add(&NewSlot);
		Invalidate(EInvalidateWidget::Layout | EInvalidateWidgetReason::ChildOrder);
		return NewSlot;
	}

	void AddCurrentSlot(FSlot* InSlot)
	{
		this->Children.Add(InSlot);
		Invalidate(EInvalidateWidget::Layout | EInvalidateWidgetReason::ChildOrder);
	}

	// Frame lifecycle: non-EventDrived mode
	void BeginItemFrame()
	{
		FrameOrder.Reset();
		TouchedSlots.Reset();
		TouchedSlots.Init(false, Children.Num());
		bFrameStarted = true;
		bLayoutDirty = false;
		bFrameOrderIsSequential = true;
		NextExpectedTouchIndex = 0;
		FrameBaseChildCount = Children.Num();
	}

	void MarkLayoutDirty() { bLayoutDirty = true; }

	// Returns true if the slot was successfully touched (first time this frame).
	// Returns false if already touched (duplicate ID) — caller should create a new slot instead.
	bool TouchSlot(int32 ChildIndex)
	{
		if (TouchedSlots.IsValidIndex(ChildIndex) && TouchedSlots[ChildIndex])
			return false;  // already touched this frame — duplicate ID
		if (TouchedSlots.IsValidIndex(ChildIndex))
			TouchedSlots[ChildIndex] = true;
		FrameOrder.Add(ChildIndex);
		// Track if touched indices match the expected non-collapsed sequence
		if (bFrameOrderIsSequential)
		{
			// Skip over collapsed indices to find next expected active index
			while (NextExpectedTouchIndex < Children.Num()
				&& IsInCollapsedSubtree(Children[NextExpectedTouchIndex].Hash))
			{
				++NextExpectedTouchIndex;
			}
			if (ChildIndex != NextExpectedTouchIndex)
				bFrameOrderIsSequential = false;
			++NextExpectedTouchIndex;
		}
		return true;
	}

	// Items added before this count are from previous frame (visible to FindItem)
	// Items at or after this count were added during current frame (invisible to FindItem)
	int32 GetFrameBaseChildCount() const { return FrameBaseChildCount; }

	bool CommitItemFrame();

	FSlot& InsertSlot(uint32 InKey, int32 Index = INDEX_NONE)
	{
		if (Index == INDEX_NONE)
		{
			return AddSlot(InKey);
		}

		FSlot& NewSlot = Slot(InKey);
		this->Children.Insert(&NewSlot, Index);

		Invalidate(EInvalidateWidget::Layout);

		return NewSlot;
	}

	int32 NumSlots() const { return this->Children.Num(); }

	int32 RemoveSlot(const TSharedRef<SWidget>& SlotWidget);
	int32 RemoveSlot(uint32 InKey);

	void ClearChildren();

	SImSlatePanel();
	~SImSlatePanel();

	SLATE_BEGIN_ARGS(SImSlatePanel) {}
#if UE_5_00_OR_LATER
	SLATE_SLOT_ARGUMENT(FSlot, Slots)
#else
	SLATE_SUPPORTS_SLOT(FSlot)
#endif
	SLATE_ARGUMENT_DEFAULT(bool, HideScollBar) = false;
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, SImSlateWindow* InParent);
	auto ToSharedRef() { return SharedThis<SImSlatePanel>(this); }

protected:
	virtual void OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const override;
	virtual FChildren* GetChildren() override;

	mutable TChildrenLayout<FSlot> Children;
	TArray<int32, TInlineAllocator<64>> FrameOrder;
	TBitArray<> TouchedSlots;
	bool bFrameStarted = false;
	bool bLayoutDirty = false;
	bool bFrameOrderIsSequential = true;
	int32 NextExpectedTouchIndex = 0;
	int32 FrameBaseChildCount = 0;

	void OnScrollFraction(float InFraction);
	bool OnScrollOffset(float InOffset);
	void SetScrollTarget(FVector2D In) { OnScrollOffset(In.Y); }

protected:
#if !UE_5_00_OR_LATER
	void MarkPrepassAsDirty() { InvalidatePrepass(); }
#endif

	struct FCachedItem
	{
		TMap<uint32, int32> CachedID2Indexes;
		TMap<int32, uint32> CachedIndex2IDs;

		void Add(uint32 InId, int32 InIndex)
		{
			DeleteByIndex(InIndex);
			CachedID2Indexes.FindOrAdd(InId) = InIndex;
			CachedIndex2IDs.FindOrAdd(InIndex) = InId;
		}

		void DeleteByID(uint32 InId)
		{
			if (auto* Find = CachedID2Indexes.Find(InId))
			{
				CachedIndex2IDs.Remove(*Find);
				CachedID2Indexes.Remove(InId);
			}
		}

		void DeleteByIndex(int32 InIndex)
		{
			if (auto* Find = CachedIndex2IDs.Find(InIndex))
			{
				CachedID2Indexes.Remove(*Find);
				CachedIndex2IDs.Remove(InIndex);
			}
		}

		int32 FindIndexById(uint32 InId)
		{
			if (auto* Find = CachedID2Indexes.Find(InId))
				return *Find;
			return INDEX_NONE;
		}
	};

	FCachedItem CachedItems;

	// Tree structure: each item knows its parent fold via ImSlateId
	TMap<ImSlateId, ImSlateId> ItemParentMap;           // ItemId → ParentFoldId (0 = root)
	TSet<ImSlateId> CollapsedFoldIds;                   // folds that are currently collapsed
	TArray<ImSlateId, TInlineAllocator<4>> FoldContextStack;  // current fold nesting during frame build

	bool IsInCollapsedSubtree(ImSlateId ItemId) const;

public:
	void SetItemParent(ImSlateId ItemId);               // assign current fold context as parent
	void PushFoldContext(ImSlateId FoldId);
	void PopFoldContext();
	void SetFoldCollapsed(ImSlateId FoldId, bool bCollapsed);

protected:
	ImSlateWindow* Window = nullptr;
	bool bHideScrollBar = false;
	friend class SImSlateWindow;
	virtual float GetDesiredWidth() const;
	virtual float GetDesiredHeight() const;
	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;
	virtual void CacheDesiredSize(float LayoutScaleMultiplier) override;
	virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled)
		const override;
	ImVec2 TotalActualSize;

	TArray<float, TInlineAllocator<4>> ColumnAlignOff;

	TSharedPtr<SScrollBar> ScrollHandle;
	float ScrollOffset = 0.f;
	float ScrollBarWidth = 12.f;
	mutable TArray<float, TInlineAllocator<4>> RowHeights;
};
}  // namespace ImSlate

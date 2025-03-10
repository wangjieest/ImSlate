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
		this->NewChildren.Add(&NewSlot);

		Invalidate(EInvalidateWidget::Layout | EInvalidateWidgetReason::ChildOrder);
		return NewSlot;
	}

	FSlot& AddCurrentSlot(uint32 InKey)
	{
		FSlot& NewSlot = Slot(InKey);
		this->Children.Add(&NewSlot);

		Invalidate(EInvalidateWidget::Layout | EInvalidateWidgetReason::ChildOrder);
		return NewSlot;
	}

	void AddSlot(FSlot* InSlot)
	{
		this->NewChildren.Add(InSlot);
		Invalidate(EInvalidateWidget::Layout | EInvalidateWidgetReason::ChildOrder);
	}

	void AddCurrentSlot(FSlot* InSlot)
	{
		this->Children.Add(InSlot);
		Invalidate(EInvalidateWidget::Layout | EInvalidateWidgetReason::ChildOrder);
	}

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
	mutable TChildrenLayout<FSlot> NewChildren;

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

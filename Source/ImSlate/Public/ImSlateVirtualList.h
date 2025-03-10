// Copyright ImSlate, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "ImSlateFactory.h"
#include "ImSlateFwd.h"
#include "Templates/UniqueObj.h"
#include "UnrealCompatibility.h"
#include "Widgets/SCompoundWidget.h"

class SScrollBar;
namespace ImSlate
{
class IImSlateListData;
struct FVirtualListSlots;
class FVirtualListSlot;

class IMSLATE_API SImSlateVirtualList
	: public SCompoundWidget
	, public IScrollableWidget
	, public IImSlateCtxBase
{
public:
	using Super = SCompoundWidget;
	SLATE_BEGIN_ARGS(SImSlateVirtualList) {}
	SLATE_ARGUMENT(TSharedPtr<SScrollBar>, ScrollBar)
	SLATE_ARGUMENT_DEFAULT(UObject*, WorldCtxPtr){nullptr};
	SLATE_END_ARGS()

	SImSlateVirtualList();
	~SImSlateVirtualList();

	void Construct(const FArguments& InArgs);

public:
	// Set buffer rows (a few more rows outside the visible area)
	void SetOverCountRowNum(int32 InNum);

	// Set Tile mode and Item width
	void SetTileOrthVal(float InOrth);

	// Bind data source
	void SetData(TSharedPtr<IImSlateListData> InDataBinding, float InVirtualPos = -0.f, float InTileAxis = 0.f);

	bool ScrollToPos(float InVirtualPos, bool bItemAlign = false);

	// make sure item is at least be visible
	void ScrollToItem(int32 InDataIndex, bool bCenterAlign = false);

	// Update specified data, default is to update all visible data
	void Update(int32 InDataIndex = -1, bool bReConstruct = false);

	void SetBackgroundContent(TSharedRef<SWidget> InWidget);

	using FOnItemInputEvent = TDelegate<bool(SImSlateVirtualList*, const FGeometry&, const FKeyEvent&)>;
	void SetItemEventOnInputKey(FOnItemInputEvent Delegate) { OnItemKeyEvent = MoveTemp(Delegate); }

	void SetScrollbarUserVisibility(TAttribute<EVisibility> InUserVisibility);

	TSharedPtr<SScrollBar> GetScrollBar() const { return ScrollBar; }

public:
	float GetVirtualPos() const;
	int32 DataIndexFromOffset(float InOffset, int32 OutRangeIndex = INDEX_NONE) const;
	int32 UpperDataIndex(float InOffset) const;
	bool RowIndexRange(int32 InIdx, int32& Lower, int32& Upper) const;
	void RowIndexRange(int32 InIdx, const TFunctionRef<void(int32)>& Func) const;
	float GetDataOffset(int32 InIndex) const;
	float GetDataAxis(int32 InIndex) const;
	float GetCachedTotalAxis(bool bFullItems = false) const;
	int32 GetDataCount() const;
	float GetItemAxis(int32 InIndex = 0) const;

protected:
	TSharedPtr<SScrollBar> ScrollBar = nullptr;

	// Begin IImSlateListData Wrapper
	void OnPosChanged(float InVirtualPos);
	void OnSetData(int32 Index, TSharedRef<SWidget> Widget);
	void GenerateDataWidget(int32 InIndex, TSharedRef<SWidget>& InOutWidget);
	bool IsHeterogeneous() const;
	bool NeedPrepassItem() const;
	void RefreshVisibleWidget();

	// End IImSlateListData Wrapper

	void ReloadToPos(float InVirtualPos, bool bItemAlign = false);
	bool InnerReloadToPos(float InVirtualPos = 0.f, bool bItemAlign = false);
	bool InnerScrollToPos(float InVirtualPos, float InListAxis, bool bItemAlign = false);

	void OnScrollFraction(float InFraction);
	bool UpdateRange(float InListAxis, float InVirtualPos, bool bForce = false);

	struct FIndexRange
	{
		int32 StartIndex;
		int32 EndIndex;
		int32 OldStartIndex;
		int32 OldEndIndex;
		bool bChanged;

		FIndexRange() { Reset(); }
		void Reset(bool bInChanged = false)
		{
			StartIndex = -1;
			OldStartIndex = -1;
			EndIndex = 0;
			OldEndIndex = 0;
			SetChanged(bInChanged);
		}

		bool HasChanged() const { return bChanged; }
		void SetChanged(bool bInChanged) { bChanged = bInChanged; }
		void AppendChanged(bool bInChanged) { bChanged = bChanged || bInChanged; }

		bool HasValidRange() const { return StartIndex >= 0 && EndIndex >= StartIndex; }
		bool SetRange(int32 NewStart, int32 NewEnd)
		{
			AppendChanged(NewStart != StartIndex || NewEnd != EndIndex);

			StartIndex = NewStart;
			EndIndex = NewEnd;
			return HasChanged();
		}
		void StoreRange()
		{
			OldStartIndex = StartIndex;
			OldEndIndex = EndIndex;
			SetChanged(false);
		}

		bool IsInRange(int32 InIdx) const { return StartIndex <= InIdx && InIdx <= EndIndex; }
	};

	mutable FIndexRange IndexRange;
	float VirtualPos = 0.f;
	TEnumAsByte<EOrientation> Orientation = EOrientation::Orient_Vertical;

	template<typename Vec2Type>
	auto GetOrientationAxis(const Vec2Type& In) const
	{
		return Orientation == EOrientation::Orient_Vertical ? In.Y : In.X;
	}
	template<typename Vec2Type>
	auto GetOrientationOrth(const Vec2Type& In) const
	{
		return Orientation == EOrientation::Orient_Vertical ? In.X : In.Y;
	}
	template<typename Vec2Type>
	auto GetOrientationAxisVec(const Vec2Type& In) const
	{
		return Orientation == EOrientation::Orient_Vertical ? Vec2Type(0, In.Y) : Vec2Type(In.X, 0);
	}
	template<typename Vec2Type>
	auto GetOrientationOrthVec(const Vec2Type& In) const
	{
		return Orientation == EOrientation::Orient_Vertical ? Vec2Type(In.X, 0) : Vec2Type(0, In.Y);
	}
	void ResetAxises(int32 FromIdx = 0);
	bool UpdateData(int32 InDataIndex, bool bReConstruct);

	int32 OverCountRowNum = 2;

	float TileOrthValue = -1.f;
	float GetTileOrth() const { return TileOrthValue; }

private:
	SImSlateVirtualList(const SImSlateVirtualList&) = delete;
	SImSlateVirtualList& operator=(const SImSlateVirtualList&) = delete;
	SImSlateVirtualList(SImSlateVirtualList&&) = delete;
	SImSlateVirtualList& operator=(SImSlateVirtualList&&) = delete;
	friend class IImSlateListData;

	bool EnsureDataAxises(int32 InIndex) const;
	bool EnsureDataAxises(float InOffset) const;
	void AppendDataAxis(int32 InDataIndex);
	bool UpdateSlotedAxis(const FVirtualListSlot* Slot, bool bPrepass);
	bool UpdateSlotedAxisDelta(const FVirtualListSlot* Slot, float Delta);

	TArray<float, TInlineAllocator<4>> DataOffsets;
	TArray<float, TInlineAllocator<4>> DataAxises;
	TBitArray<TInlineAllocator<4>> CachedWidgets;
	float LastRowOffset = 0.f;
	float LastRowAxis = 0.f;
	int32 TotalCol = 0;
	int32 GetTotalCol() const { return TotalCol; }
	bool HasValidCol() const { return TotalCol > 0; }
	TUniqueObj<FVirtualListSlots> SlotCollection;
	virtual FChildren* GetChildren() override;
	static FVector2D MinItemSize;

protected:
	SImSlateVirtualList* MutableThis() const
	{
		auto This = this;
		return const_cast<SImSlateVirtualList*>(This);
	}
	TSharedRef<SImSlateVirtualList> ToSharedRef() const { return StaticCastSharedRef<SImSlateVirtualList>(MutableThis()->AsShared()); }

	// Begin SWidget overrides.
	virtual bool CustomPrepass(float LayoutScaleMultiplier) override;
	virtual int32 OnPaint(const FPaintArgs&, const FGeometry&, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual void OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const override;
	virtual void CacheDesiredSize(float LayoutScaleMultiplier) override;

	virtual void OnFocusLost(const FFocusEvent& InFocusEvent) override;
	virtual void OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override;
	virtual bool SupportsKeyboardFocus() const override;
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	virtual void OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	FReply OnMouseButtonDoubleClick(const FGeometry& InMyGeometry, const FPointerEvent& MouseEvent) override;
	FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

	virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnKeyUp(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

	virtual FReply OnTouchStarted(const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent) override;
	virtual FReply OnTouchMoved(const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent) override;
	virtual FReply OnTouchEnded(const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent) override;
	virtual FReply OnTouchFirstMove(const FGeometry& MyGeometry, const FPointerEvent& TouchEvent) override;
	virtual FReply OnTouchForceChanged(const FGeometry& MyGeometry, const FPointerEvent& TouchEvent) override;
	virtual FReply OnPreviewMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	// End SWidget overrides.
	//
	// IScrollableWidget interface
	virtual FVector2D GetScrollDistance() override;
	virtual FVector2D GetScrollDistanceRemaining() override;
	virtual TSharedRef<SWidget> GetScrollWidget() override;

	virtual float GetListAxis() const;
	virtual float GetListOrth() const;

	void SetItemPadding(FMargin InPadding);
	FMargin ItemPadding;

private:
	mutable TOptional<float> OverrideAxis;
	void UpdateScrollState() const;
	bool GenerateWidgetIfNeeded();
	TSharedPtr<IImSlateListData> DataBinding;
	TMap<FName, TSharedRef<SWidget>> WidgetCache;
	FOnItemInputEvent OnItemKeyEvent;
	TSharedPtr<SBox> BackgroundBox;

protected:
	bool bStartedTouchInteraction = false;
	FVector2D PressedScreenSpacePosition;

protected:
	// OverScroll
	FOverscroll Overscroll;
	EAllowOverscroll AllowOverscroll = EAllowOverscroll::No;
	EConsumeMouseWheel ConsumeMouseWheel = EConsumeMouseWheel::WhenScrollingPossible;

protected:
	float AmountScrolledWhileRightMouseDown = 0.f;
	bool IsUserScrolling() const;
	bool IsRightClickScrolling() const;
	FVector2D SoftwareCursorPosition{ForceInitToZero};
	bool bShowSoftwareCursor = false;
	FOnContextMenuOpening OnContextMenuOpening;
	virtual void OnRightMouseButtonDown(const FPointerEvent& MouseEvent) {}
	virtual void OnRightMouseButtonUp(const FPointerEvent& MouseEvent);
	virtual void NotifyItemScrolledIntoView() {}
};

}  // namespace ImSlate

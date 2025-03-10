// Copyright ImSlate, Inc. All Rights Reserved.
#include "ImSlateVirtualList.h"

#include "UMG.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/Image.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Engine/World.h"
#include "ImSlateListDataInc.h"
#include "Misc/ScopeExit.h"
#include "PrivateFieldAccessor.h"
#include "ProtectFieldAccessor.h"
#include "SImSlateLayout.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/Layout/SScrollBar.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Views/STableViewBase.h"

namespace ImSlate
{
extern void PrepassInternal(const TSharedRef<SWidget>& InWidget, float LayoutScaleMultiplier);
static void SlatePrepassWidget(const TSharedRef<SWidget>& WidgetRef)
{
	auto& PrepassLayoutScaleMultiplier = GS_ACCESS_PROTECT(&WidgetRef.Get(), SWidget, PrepassLayoutScaleMultiplier)->PrepassLayoutScaleMultiplier;
	WidgetRef->SlatePrepass(PrepassLayoutScaleMultiplier.Get(FSlateApplicationBase::Get().GetApplicationScale()));
}
class FVirtualListSlot
	: public TSlotBase<FVirtualListSlot>
#if UE_5_00_OR_LATER
	, public TAlignmentWidgetSlotMixin<FVirtualListSlot>
#else
	, public TSupportsContentAlignmentMixin<FVirtualListSlot>
#endif
{
public:
	const int32 DataIndex;
	mutable float CachedAxis = 0.f;

public:
	FVirtualListSlot(int32 InIndex)
		: TSlotBase<FVirtualListSlot>()
#if UE_5_00_OR_LATER
		, TAlignmentWidgetSlotMixin<FVirtualListSlot>(HAlign_Fill, VAlign_Fill)
#else
		, TSupportsContentAlignmentMixin<FVirtualListSlot>(HAlign_Fill, VAlign_Fill)
#endif
		, DataIndex(InIndex)
	{
	}

	template<typename S = SWidget>
	auto& GetTypedWidget() const
	{
		return const_cast<TSharedRef<S>&>(GetWidget());
	}
};

struct FVirtualListSlots
{
public:
	FVirtualListSlots(SWidget* InOwner)
		: MyChildren(InOwner)
	{
	}
	void Empty() { MyChildren.Empty(); }
	FVirtualListSlot& AppendSlot(int32 InDataIndex)
	{
		auto NewSlot = new FVirtualListSlot(InDataIndex);
		MyChildren.Add(NewSlot);
		return *NewSlot;
	}
	FVirtualListSlot& InsertSlot(int32 InDataIndex)
	{
		auto NewSlot = new FVirtualListSlot(InDataIndex);
		MyChildren.Insert(NewSlot);
		return *NewSlot;
	}
	bool RemoveLast(int32 InCnt = 1) { return MyChildren.RemoveLast(InCnt); }

	FVirtualListSlot& At(int32 Index) { return MyChildren[Index]; }
	const FVirtualListSlot& At(int32 Index) const { return MyChildren[Index]; }
	int32 FirstDataIndex(int32 InOutIndex)
	{
		if (GetChildren().Num() > 0)
			return FMath::Min(GetChildren().First().DataIndex - 1, InOutIndex);
		return InOutIndex;
	}
	int32 LastDataIndex(int32 InOutIndex)
	{
		if (GetChildren().Num() > 0)
			return FMath::Max(GetChildren().Last().DataIndex + 1, InOutIndex);
		return InOutIndex;
	}
	int32 FirstDataIndex() const
	{
		if (GetChildren().Num() > 0)
			return GetChildren().First().DataIndex;
		return INDEX_NONE;
	}
	int32 LastDataIndex() const
	{
		if (GetChildren().Num() > 0)
			return GetChildren().Last().DataIndex;
		return INDEX_NONE;
	}
	TChildrenLayout<FVirtualListSlot>& GetChildren() { return MyChildren; }
	const TChildrenLayout<FVirtualListSlot>& GetChildren() const { return MyChildren; }
	TChildrenLayout<FVirtualListSlot> MyChildren;

	const FVirtualListSlot* FindByDataIndex(int32 InDataIndex) const
	{
		do
		{
			auto SlotNum = MyChildren.Num();
			if (SlotNum == 0)
				break;
			auto& FirstSlot = MyChildren.First();
			if (FirstSlot.DataIndex > InDataIndex)
				break;

			auto SlotIndex = InDataIndex - FirstSlot.DataIndex;
			if (SlotIndex >= SlotNum)
				break;

			if (!ensure(MyChildren[SlotIndex].DataIndex == InDataIndex))
			{
				for (auto i = 0; i < GetChildren().Num(); ++i)
				{
					auto DataIdx = At(i).DataIndex;
					if (DataIdx == InDataIndex)
					{
						return &MyChildren[i];
					}
				}
				break;
			}

			return &MyChildren[SlotIndex];
		} while (false);

		return nullptr;
	}

	template<typename F>
	float GetAxisByDataIndex(int32 InDataIndex, float MinAxis, const F& OnGetAxisRef) const
	{
#if WITH_EDITOR && 0
		if (auto Ptr = FindByDataIndex(InDataIndex))
		{
			auto ItemAxis = GetOrientationAxis(Ptr->GetWidget()->GetCachedGeometry().GetLocalSize());
			return ItemAxis <= 0.f ? OnGetAxisRef() : FMath::Max(MinAxis, ItemAxis);
		}
		else
#endif
		{
			auto ItemAxis = OnGetAxisRef();
			return FMath::Max(MinAxis, ItemAxis);
		}
	}

	template<typename F>
	void RefreshSlotedWidgets(const F& OnSetDataRef) const
	{
		for (auto i = 0; i < GetChildren().Num(); ++i)
		{
			auto DataIdx = MyChildren[i].DataIndex;
			auto& DataWidget = MyChildren[i].GetTypedWidget();
			OnSetDataRef(DataIdx, DataWidget);
		}
	}
};

SImSlateVirtualList::SImSlateVirtualList()
	: SlotCollection(this)
{
}
SImSlateVirtualList::~SImSlateVirtualList() = default;

void SImSlateVirtualList::Construct(const FArguments& InArgs)
{
	SetClipping(EWidgetClipping::OnDemand);
	SetCanTick(true);

	if (InArgs._WorldCtxPtr)
		ImSlateWorldCtx = InArgs._WorldCtxPtr;

	ScrollBar = InArgs._ScrollBar ? InArgs._ScrollBar
								  : SNew(SScrollBar)
									.Padding(FMargin(1.f, 0.f));
	ScrollBar->SetOnUserScrolled(CreateWeakLambda(this, [this](float In) { OnScrollFraction(In); }));
	ScrollBar->SetScrollBarAlwaysVisible(false);
	ScrollBar->SetScrollBarTrackAlwaysVisible(true);
	{
		auto TmpScrollBar = GS_ACCESS_PROTECT(ScrollBar, SScrollBar, Orientation, bHideWhenNotInUse);
		TmpScrollBar->Orientation = Orient_Vertical;
		TmpScrollBar->bHideWhenNotInUse = true;
	}
}

FChildren* SImSlateVirtualList::GetChildren()
{
	return &SlotCollection->GetChildren();
}

FVector2D SImSlateVirtualList::MinItemSize{6.f, 6.f};

void SImSlateVirtualList::SetData(TSharedPtr<IImSlateListData> InDataBinding, float InVirtualPos, float InTileAxisVal)
{
	if (!InDataBinding.IsValid())
		return;

	bool bShouldReload = false;
	if (InDataBinding != DataBinding)
	{
		if (DataBinding.IsValid())
			DataBinding->SetVirtualList(nullptr);

		DataBinding = InDataBinding;
		DataBinding->SetVirtualList(ToSharedRef());
		TileOrthValue = InTileAxisVal;
		bShouldReload = true;
	}
	else if (TileOrthValue != InTileAxisVal)
	{
		TileOrthValue = InTileAxisVal;
		bShouldReload = true;
	}

	if (bShouldReload)
		ReloadToPos(VirtualPos);
}

void SImSlateVirtualList::ReloadToPos(float InVirtualPos, bool bItemAlign)
{
	if (InVirtualPos < 0.f)
		InVirtualPos = GetVirtualPos();

	if (!HasValidCol())
	{
		IndexRange.Reset(true);
	}
	else
	{
		auto WidgetOrth = GetOrientationOrth(GetCachedGeometry().GetLocalSize());
		if (WidgetOrth > 0.f)
		{
			TotalCol = GetTileOrth() > 0.f ? FMath::Max(1, int32(WidgetOrth / GetTileOrth())) : 1;
		}
		InnerReloadToPos(InVirtualPos, bItemAlign);
	}
}

bool SImSlateVirtualList::InnerReloadToPos(float InVirtualPos, bool bItemAlign)
{
	ResetAxises();

	if (!HasValidCol())
		return false;

	return InnerScrollToPos(InVirtualPos, GetListAxis(), bItemAlign);
}

bool SImSlateVirtualList::ScrollToPos(float InVirtualPos, bool bItemAlign)
{
	VirtualPos = InVirtualPos < 0.f ? FLT_MAX : InVirtualPos;
	if (!HasValidCol() || !IndexRange.HasValidRange())
		return false;

	return InnerScrollToPos(InVirtualPos, GetListAxis(), bItemAlign);
}

bool SImSlateVirtualList::InnerScrollToPos(float InVirtualPos, float InListAxis, bool bItemAlign)
{
	if (!ImEnsure(InListAxis > 0.f))
		return false;

	auto OldVal = OverrideAxis;
	OverrideAxis = InListAxis;
	ON_SCOPE_EXIT { OverrideAxis = OldVal; };

	if (GetDataCount() > 0)
	{
		EnsureDataAxises(InVirtualPos);
		auto TotalAxis = GetCachedTotalAxis();
		InVirtualPos = FMath::Clamp(InVirtualPos < 0 ? FLT_MAX : InVirtualPos, 0.f, FMath::Max(0.f, TotalAxis - InListAxis));
		if (bItemAlign)
			InVirtualPos = GetDataOffset(UpperDataIndex(InVirtualPos));

		UpdateRange(InListAxis, InVirtualPos);
		ScrollBar->SetState(VirtualPos / TotalAxis, InListAxis / TotalAxis);
	}
	else
	{
		VirtualPos = 0.f;
		ScrollBar->SetState(0.f, 1.f);
	}
	return true;
}

void SImSlateVirtualList::UpdateScrollState() const
{
	if (ScrollBar)
	{
		auto ListAxis = GetListAxis();
		auto TotalAxis = GetCachedTotalAxis();
		auto TotalOffset = TotalAxis - ListAxis;
		if (TotalOffset <= 0.f)
			ScrollBar->SetState(0.f, 1.f);
		else
			ScrollBar->SetState(VirtualPos / TotalAxis, ListAxis / TotalAxis);
	}
}

void SImSlateVirtualList::RefreshVisibleWidget()
{
	SlotCollection->RefreshSlotedWidgets([this](auto& Index, auto& Widget) { OnSetData(Index, Widget); });
}

void SImSlateVirtualList::Update(int32 InDataIndex, bool bReConstruct)
{
	if (!DataBinding.IsValid())
		return;

	if (!bReConstruct || !IsHeterogeneous())
	{
		if (!HasValidCol())
		{
			IndexRange.SetChanged(true);
		}
		else if (InDataIndex < 0)
		{
			RefreshVisibleWidget();
			IndexRange.AppendChanged(EnsureDataAxises(-1.f));
		}
		else if (InDataIndex >= DataAxises.Num())
		{
			IndexRange.AppendChanged(EnsureDataAxises(-1.f) && InDataIndex <= DataAxises.Num());
		}
		else if (GetListAxis() > 0.f)
		{
			UpdateData(InDataIndex, bReConstruct);
		}
		return;
	}

	// bReConstruct && IsHeterogeneous()
	if (InDataIndex < 0)
	{
		//reload all
		InnerReloadToPos();
		return;
	}
	else
	{
		// not in cache, test if in range
		if (InDataIndex >= DataAxises.Num())
		{
			IndexRange.AppendChanged(EnsureDataAxises(-1.f) && InDataIndex <= DataAxises.Num());
			return;
		}
	}

	// refresh widget
	UpdateData(InDataIndex, bReConstruct);
}

void SImSlateVirtualList::SetBackgroundContent(TSharedRef<SWidget> InWidget)
{
	if (!BackgroundBox)
	{
		BackgroundBox = SNew(SBox);
	}
	BackgroundBox->SetContent(InWidget);
}

// Begin IImSlateListData Wrapper
void IImSlateListData::SetVirtualPos(float VirtualPos, bool bItemAlign, bool bReset)
{
	if (auto Pined = GetVirtualList())
	{
		if (bReset)
			Pined->ReloadToPos(VirtualPos, bItemAlign);
		else
			Pined->ScrollToPos(VirtualPos, bItemAlign);
	}
}

float IImSlateListData::GetVirtualPos() const
{
	if (auto Pined = GetVirtualList())
	{
		return Pined->GetVirtualPos();
	}
	return 0.f;
}

void IImSlateListData::UpdateItem(int32 InIndex, bool bReConstructWidget)
{
	if (auto Pined = GetVirtualList())
	{
		Pined->Update(InIndex, bReConstructWidget || IsHeterogeneous());
	}
}

void IImSlateListData::ScrollToItem(int32 InIndex, bool bCenterAlign)
{
	if (auto Pined = GetVirtualList())
	{
		Pined->ScrollToItem(InIndex, bCenterAlign);
	}
}

int32 SImSlateVirtualList::GetDataCount() const
{
	return DataBinding->GetDataCount();
}

void SImSlateVirtualList::OnPosChanged(float InVirtualPos)
{
	return DataBinding->OnPosChanged(InVirtualPos);
}

void SImSlateVirtualList::OnSetData(int32 Index, TSharedRef<SWidget> Widget)
{
	return DataBinding->OnSetData(Index, Widget);
}

float SImSlateVirtualList::GetItemAxis(int32 InIndex) const
{
	return SlotCollection->GetAxisByDataIndex(InIndex, GetOrientationAxis(MinItemSize), [&] { return DataBinding->GetItemAxis(InIndex); });
}

void SImSlateVirtualList::GenerateDataWidget(int32 InIndex, TSharedRef<SWidget>& InOutWidget)
{
	if (InOutWidget == SNullWidget::NullWidget || DataBinding->IsHeterogeneous())
	{
		DataBinding->GenerateDataWidget(InIndex, InOutWidget);
	}
}

bool SImSlateVirtualList::IsHeterogeneous() const
{
	return DataBinding->IsHeterogeneous();
}

bool SImSlateVirtualList::NeedPrepassItem() const
{
	return DataBinding->NeedPrepassItem();
}

// End IImSlateListData Wrapper

void SImSlateVirtualList::ScrollToItem(int32 InIndex, bool bCenterAlign)
{
	ImEnsureMsgf(HasValidCol(), TEXT("unable to caculate offset due to widget size is unkown"));

	InIndex = FMath::Clamp(InIndex, 0, GetDataCount() - 1);
	EnsureDataAxises(InIndex);

	auto Offset = GetDataOffset(InIndex);
	auto ListAxis = GetListAxis();
	if (ListAxis <= 0.f)
	{
		VirtualPos = Offset;
		IndexRange.SetChanged(true);
	}
	else if (bCenterAlign)
	{
		Offset += ListAxis / 2 - DataAxises[InIndex] / 2;
		ScrollToPos(Offset);
	}
	else if (Offset < VirtualPos)
	{
		ScrollToPos(Offset);
	}
	else
	{
		if (Offset > VirtualPos + ListAxis)
		{
			ScrollToPos(Offset - ListAxis + DataAxises[InIndex]);
		}
	}
}

void SImSlateVirtualList::SetOverCountRowNum(int32 InNum)
{
	check(InNum >= 0);
	OverCountRowNum = InNum;
}

void SImSlateVirtualList::SetScrollbarUserVisibility(TAttribute<EVisibility> InUserVisibility)
{
	if (ScrollBar)
	{
		ScrollBar->SetUserVisibility(MoveTemp(InUserVisibility));
	}
}

float SImSlateVirtualList::GetVirtualPos() const
{
	if (!IndexRange.HasValidRange() || VirtualPos < 0.f)
		return 0.f;

	return VirtualPos;
}

void SImSlateVirtualList::OnScrollFraction(float InFraction /*0.f ~ 1.f*/)
{
	auto NewVirtualPos = InFraction * GetCachedTotalAxis();
	UpdateRange(GetListAxis(), NewVirtualPos);
}

bool SImSlateVirtualList::UpdateRange(float InListAxis, float InVirtualPos, bool bForce)
{
	do
	{
		if (!DataBinding)
			break;

		check(InListAxis > 0.f);
		EnsureDataAxises(InVirtualPos + InListAxis);
		if (!bForce && !IndexRange.HasChanged())
		{
			if (FMath::IsNearlyEqual(VirtualPos, InVirtualPos))
				break;

			if (InVirtualPos <= 0.f && VirtualPos <= 0.f)
				break;
		}

		auto TotalDataCount = GetDataCount();
		if (TotalDataCount <= 0 || GetDataAxis(0) <= 0.1f)
			break;
		if (!ImEnsure(GetCachedTotalAxis() > 0.f))
			break;

		auto MaxOffset = GetCachedTotalAxis() - InListAxis;

		bool bNoMore = !bForce && !IndexRange.HasChanged() && InVirtualPos >= MaxOffset && VirtualPos >= MaxOffset;
		if (bNoMore)
			break;

		InVirtualPos = FMath::Clamp(InVirtualPos, 0.f, MaxOffset);
		auto TmpStartIndex = DataIndexFromOffset(InVirtualPos, 0);
		auto TmpEndIndex = InVirtualPos >= MaxOffset ? GetDataCount() - 1 : UpperDataIndex(InVirtualPos + InListAxis);

		auto OldPos = VirtualPos;
		VirtualPos = FMath::Max(0.f, InVirtualPos);

#if WITH_EDITOR
		if (!ImEnsure(TmpEndIndex < GetDataCount()))
		{
			const auto Offset = TmpEndIndex - GetDataCount() + 1;
			TmpEndIndex = GetDataCount() - 1;
			TmpStartIndex -= Offset;
		}
		else if (!ImEnsure(TmpEndIndex >= 0))
		{
			TmpEndIndex = 0;
		}

		if (!ImEnsure(TmpStartIndex >= 0))
			TmpStartIndex = 0;
#endif

		if (IndexRange.SetRange(TmpStartIndex, TmpEndIndex) || OldPos != VirtualPos)
		{
			OnPosChanged(VirtualPos);
		}
		return true;
	} while (false);
	return false;
}

bool SImSlateVirtualList::EnsureDataAxises(int32 InIndex) const
{
	check(DataOffsets.Num() == DataAxises.Num());
	auto Max = InIndex >= GetDataCount() ? 0 : (InIndex < 0 ? GetDataCount() : FMath::Min(InIndex + OverCountRowNum * FMath::Max(1, GetTotalCol()) + 1, GetDataCount()));

	const auto OldNum = DataAxises.Num();
	for (auto i = DataAxises.Num(); i < Max; ++i)
	{
		MutableThis()->AppendDataAxis(i);
	}

	UpdateScrollState();
	return OldNum != DataAxises.Num();
}

bool SImSlateVirtualList::EnsureDataAxises(float InOffset) const
{
	check(DataOffsets.Num() == DataAxises.Num());
	auto ListAxis = GetListAxis();
	InOffset = FMath::Max(GetVirtualPos() + ListAxis, InOffset);

	const auto OldNum = DataAxises.Num();
	auto Max = GetDataCount();
	auto i = DataAxises.Num();
	for (; i < Max && GetCachedTotalAxis() < InOffset; ++i)
	{
		MutableThis()->AppendDataAxis(i);
	}
	Max = FMath::Min(Max, i + OverCountRowNum * FMath::Max(1, GetTotalCol()));
	for (; i < Max; ++i)
	{
		MutableThis()->AppendDataAxis(i);
	}
	UpdateScrollState();
	return OldNum != DataAxises.Num();
}

void SImSlateVirtualList::AppendDataAxis(int32 InDataIndex)
{
	auto ItemAxis = GetItemAxis(InDataIndex);
	ImEnsure(HasValidCol());

	if (GetTotalCol() <= 1 || (InDataIndex % GetTotalCol()) == 0)
	{
		LastRowOffset += LastRowAxis;
		LastRowAxis = ItemAxis;
	}
	else
	{
		LastRowAxis = FMath::Max(ItemAxis, LastRowAxis);
	}
	DataAxises.Add(ItemAxis);
	DataOffsets.Add(LastRowOffset);
	CachedWidgets.Add(false);
}

bool SImSlateVirtualList::UpdateSlotedAxisDelta(const FVirtualListSlot* Slot, float Delta)
{
	checkSlow(Slot && DataAxises.IsValidIndex(Slot->DataIndex));
	const auto DataIdx = Slot->DataIndex;
	CachedWidgets[DataIdx] = true;
	if (Delta != 0.f)
	{
		DataAxises[DataIdx] += Delta;
		const auto NextColIdx = (DataIdx / GetTotalCol() + 1) * GetTotalCol();
		if (NextColIdx < DataOffsets.Num())
		{
			for (auto i = NextColIdx; i < DataOffsets.Num(); ++i)
			{
				DataOffsets[i] += Delta;
			}
			LastRowOffset += Delta;
		}
		else
		{
			for (auto i = (DataIdx / GetTotalCol()) * GetTotalCol(); i < NextColIdx; ++i)
			{
				LastRowAxis = FMath::Max(DataAxises[i], LastRowAxis);
			}
		}

		// keep first widgets offset
		if (DataIdx < IndexRange.StartIndex)
			VirtualPos += Delta;

		return true;
	}
	return false;
}

bool SImSlateVirtualList::UpdateSlotedAxis(const FVirtualListSlot* Slot, bool bPrepass)
{
	check(Slot && DataAxises.IsValidIndex(Slot->DataIndex));
	const auto DataIdx = Slot->DataIndex;
	if (bPrepass)
	{
		SlatePrepassWidget(Slot->GetTypedWidget());
		auto NewAxis = GetOrientationAxis(Slot->GetTypedWidget()->GetDesiredSize());
		auto Delta = NewAxis - DataAxises[DataIdx];
		return UpdateSlotedAxisDelta(Slot, Delta);
	}
	else
	{
		auto NewAxis = GetItemAxis(Slot->DataIndex);
		auto Delta = NewAxis - DataAxises[DataIdx];
		return UpdateSlotedAxisDelta(Slot, Delta);
	}
}

void SImSlateVirtualList::ResetAxises(int32 FromIdx)
{
	FromIdx = !HasValidCol() ? FromIdx : (FromIdx / GetTotalCol()) * GetTotalCol();
	if (FromIdx <= 0)
	{
		DataAxises.Empty(GetDataCount());
		DataOffsets.Empty(GetDataCount());
		CachedWidgets.Empty(GetDataCount());

		LastRowOffset = 0.f;
		LastRowAxis = 0.f;

		SlotCollection->Empty();
		EnsureDataAxises(0);
		IndexRange.Reset(true);
	}
	else if (FromIdx < DataAxises.Num())
	{
		auto ReducedNum = DataAxises.Num() - FromIdx;

		DataAxises.SetNum(FromIdx);
		DataOffsets.SetNum(FromIdx);
		CachedWidgets.Empty(FromIdx);

		LastRowAxis = DataAxises.Last();
		LastRowOffset = DataOffsets.Last();

		SlotCollection->RemoveLast(ReducedNum);
		UpdateRange(GetListAxis(), VirtualPos, true);
	}
}

bool SImSlateVirtualList::UpdateData(int32 InDataIndex, bool bReConstruct)
{
	check(InDataIndex >= 0);
	float Delta = 0.f;
	if (InDataIndex >= IndexRange.StartIndex && InDataIndex <= IndexRange.EndIndex)
	{
		if (auto ElmPtr = SlotCollection->FindByDataIndex(InDataIndex))
		{
			auto& DataWidget = ElmPtr->GetTypedWidget();
			bool bNeedPrepassAxis = NeedPrepassItem();
			if (bReConstruct && IsHeterogeneous())
			{
				auto OldWidget = DataWidget;
				GenerateDataWidget(ElmPtr->DataIndex, DataWidget);
				if (OldWidget != DataWidget)
					bNeedPrepassAxis = true;
			}

			if (DataWidget != SNullWidget::NullWidget)
			{
				if ((bReConstruct || bNeedPrepassAxis) && UpdateSlotedAxis(ElmPtr, bNeedPrepassAxis))
				{
					UpdateRange(GetListAxis(), VirtualPos, true);
					UpdateScrollState();
				}
			}
			OnSetData(ElmPtr->DataIndex, DataWidget);
			return true;
		}
	}
	else if (CachedWidgets.IsValidIndex(InDataIndex))
	{
		CachedWidgets[InDataIndex] = (NeedPrepassItem() || (bReConstruct && IsHeterogeneous())) ? false : true;
	}

	return false;
}

void SImSlateVirtualList::SetTileOrthVal(float InTileOrthVal)
{
	if (TileOrthValue != InTileOrthVal)
	{
		TileOrthValue = InTileOrthVal;
		ReloadToPos(VirtualPos);
	}
}

void SImSlateVirtualList::SetItemPadding(FMargin InPadding)
{
	if (ItemPadding != InPadding)
	{
		ItemPadding = InPadding;
		ReloadToPos(VirtualPos);
	}
}

//  <=
//     ^
int32 SImSlateVirtualList::DataIndexFromOffset(float InOffset, int32 OutRangeIndex) const
{
	EnsureDataAxises(InOffset);
	auto Ret = (InOffset < 0.f || DataOffsets.Num() == 0 || InOffset > (LastRowOffset + LastRowAxis))  //
				   ? OutRangeIndex
				   : (InOffset > DataOffsets.Last() ? DataOffsets.Num() - 1 : FMath::Max(0, Algo::LowerBound(DataOffsets, InOffset) - GetTotalCol()));
	ImEnsure(OutRangeIndex == Ret || (Ret >= 0 && Ret < GetDataCount()));
	return Ret;
}
//   <
//     ^
int32 SImSlateVirtualList::UpperDataIndex(float InOffset) const
{
	check(GetDataCount() > 0);
	EnsureDataAxises(InOffset);
	auto Ret = (InOffset < 0.f || DataOffsets.Num() == 0 || InOffset > (LastRowOffset + LastRowAxis))  //
				   ? GetDataCount() - 1
				   : ((InOffset > DataOffsets.Last()) ? DataOffsets.Num() - 1 : Algo::UpperBound(DataOffsets, InOffset) - GetTotalCol());
	ImEnsure(Ret >= 0 && Ret < GetDataCount());
	return Ret;
}

bool SImSlateVirtualList::RowIndexRange(int32 InDataIdx, int32& Lower, int32& Upper) const
{
	if (GetTotalCol() <= 1)
	{
		Lower = InDataIdx;
		Upper = InDataIdx + 1;
		return true;
	}
	else if (ImEnsure(IndexRange.IsInRange(InDataIdx)))
	{
		Lower = (InDataIdx / GetTotalCol()) * GetTotalCol();
		Upper = FMath::Min(GetDataCount(), Lower + GetTotalCol() + 1);
		return true;
	}
	return false;
}

void SImSlateVirtualList::RowIndexRange(int32 InDataIdx, const TFunctionRef<void(int32)>& Func) const
{
	ImEnsure(HasValidCol());
	if (GetTotalCol() <= 1)
	{
		Func(InDataIdx);
	}
	else if (ImEnsure(IndexRange.IsInRange(InDataIdx)))
	{
		auto Lower = (InDataIdx / GetTotalCol()) * GetTotalCol();
		auto Upper = FMath::Min(GetDataCount(), Lower + GetTotalCol() + 1);
		for (auto i = Lower; i < Upper; ++i)
			Func(i);
	}
}

float SImSlateVirtualList::GetDataOffset(int32 InIndex) const
{
	check(InIndex >= 0 && InIndex < GetDataCount());
	EnsureDataAxises(InIndex);
	return DataOffsets[InIndex];
}

float SImSlateVirtualList::GetDataAxis(int32 InIndex) const
{
	check(InIndex >= 0 && InIndex < GetDataCount());
	EnsureDataAxises(InIndex);
	return DataAxises[InIndex];
}

float SImSlateVirtualList::GetCachedTotalAxis(bool bFullItems) const
{
	if (bFullItems)
		EnsureDataAxises(-1);

	return LastRowOffset + LastRowAxis;
}

bool SImSlateVirtualList::CustomPrepass(float LayoutScaleMultiplier)
{
	const auto ListAxis = GetOrientationAxis(GetCachedGeometry().GetLocalSize());
	if (ListAxis <= 0.f)
		return false;

	if (!IndexRange.HasValidRange())
	{
		if (!InnerScrollToPos(VirtualPos, ListAxis, false))
			return false;
	}

	bHasCustomPrepass = false;
	return GenerateWidgetIfNeeded();
}

bool SImSlateVirtualList::GenerateWidgetIfNeeded()
{
	bool bItemChanged = false;
	const auto ItemCount = GetDataCount();
	if (IndexRange.OldStartIndex >= 0 && IndexRange.StartIndex > IndexRange.OldStartIndex)
	{
		// 顶部减少
		int32 Inner = FMath::Max(IndexRange.StartIndex - OverCountRowNum, 0);
		Inner = FMath::Min(Inner, SlotCollection->GetChildren().Last().DataIndex + 1);
		int32 Outer = FMath::Max(IndexRange.OldStartIndex - OverCountRowNum, 0) - 1;
		Outer = SlotCollection->FirstDataIndex(Outer);

		if (Outer + 1 < Inner)
		{
			bItemChanged = SlotCollection->GetChildren().RemoveFirst(Inner - (Outer + 1));
		}
	}

	if (IndexRange.EndIndex < IndexRange.OldEndIndex)
	{
		// 底部减少
		int32 Inner = FMath::Min(IndexRange.EndIndex + OverCountRowNum, ItemCount - 1);
		Inner = FMath::Max(Inner, SlotCollection->GetChildren().First().DataIndex - 1);
		int32 Outer = FMath::Min(IndexRange.OldEndIndex + OverCountRowNum, ItemCount - 1) + 1;
		Outer = SlotCollection->LastDataIndex(Outer);

		if (Outer - 1 > Inner)
		{
			bItemChanged = SlotCollection->GetChildren().RemoveLast((Outer - 1) - Inner);
		}
	}

	if (IndexRange.StartIndex < IndexRange.OldStartIndex)
	{
		// 顶部添加
		int32 Outer = FMath::Max(IndexRange.StartIndex - OverCountRowNum, 0);
		int32 Inner = FMath::Min(IndexRange.EndIndex + OverCountRowNum, ItemCount - 1);
		Inner = SlotCollection->FirstDataIndex(Inner);

		for (auto i = Inner; i >= Outer; --i)
		{
			TSharedRef<SWidget> DataWidget = SNullWidget::NullWidget;
			GenerateDataWidget(i, DataWidget);
			auto& NewSlot = SlotCollection->InsertSlot(i)[DataWidget];
			if (NeedPrepassItem() && !CachedWidgets[i] && DataWidget != SNullWidget::NullWidget)
			{
				UpdateSlotedAxis(&NewSlot, true);
			}
			OnSetData(i, DataWidget);
			bItemChanged = true;
		}
	}

	if ((IndexRange.OldEndIndex >= 0 && IndexRange.EndIndex > IndexRange.OldEndIndex) || (IndexRange.OldStartIndex == -1 && IndexRange.OldEndIndex == 0))
	{
		// 底部添加
		int32 Outer = FMath::Min(IndexRange.EndIndex + OverCountRowNum, ItemCount - 1);
		int32 Inner = FMath::Max(IndexRange.StartIndex - OverCountRowNum, 0);
		Inner = SlotCollection->LastDataIndex(Inner);

		for (auto i = Inner; i <= Outer; ++i)
		{
			TSharedRef<SWidget> DataWidget = SNullWidget::NullWidget;
			GenerateDataWidget(i, DataWidget);
			auto& NewSlot = SlotCollection->AppendSlot(i)[DataWidget];
			if (NeedPrepassItem() && !CachedWidgets[i] && DataWidget != SNullWidget::NullWidget)
			{
				UpdateSlotedAxis(&NewSlot, true);
			}

			OnSetData(i, DataWidget);
			bItemChanged = true;
		}
	}
	IndexRange.StoreRange();
	UpdateScrollState();

	return bItemChanged;
}

float SImSlateVirtualList::GetListAxis() const
{
	auto ListAxis = GetOrientationAxis(GetCachedGeometry().GetLocalSize());
	ListAxis = (ListAxis <= 0.f) ? GetOrientationAxis(GetDesiredSize()) : ListAxis;
	ListAxis = (ListAxis > 0.f) ? ListAxis : 22.f;
	return OverrideAxis.Get(ListAxis);
}

float SImSlateVirtualList::GetListOrth() const
{
	auto WidgetOrth = GetOrientationOrth(GetCachedGeometry().GetLocalSize());
	WidgetOrth = (WidgetOrth <= 0.f) ? GetOrientationOrth(GetDesiredSize()) : WidgetOrth;
	WidgetOrth = (WidgetOrth > 0.f) ? WidgetOrth : 100.f;
	return WidgetOrth;
}

void SImSlateVirtualList::OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const
{
	if (!DataBinding)
		return;

#if WITH_EDITOR
	if (SlotCollection->GetChildren().Num() == 0 || (SlotCollection->GetChildren().Num() > 0 && SlotCollection->At(0).DataIndex >= GetDataCount()))
	{
		return;
	}
#endif

	const auto ColNum = ImEnsure(HasValidCol()) ? GetTotalCol() : 1;
	const auto ScrollBarSize = GS_ACCESS_PROTECT(ScrollBar, SScrollBar, ThicknessSpacer)->ThicknessSpacer->GetSize();
	const auto PanelSize = AllottedGeometry.GetLocalSize();
	const auto PanelOrth = GetOrientationOrth(PanelSize) - (ColNum > 1 ? GetOrientationOrth(ScrollBarSize) : 0.f);
	const auto PanelAxis = GetOrientationAxis(PanelSize);

	if (!GetCachedGeometry().GetLocalSize().IsZero() && GetCachedGeometry().GetLocalSize() != AllottedGeometry.GetLocalSize())
	{
		MutableThis()->InnerScrollToPos(VirtualPos, PanelAxis, false);
	}

	// Arrange
	if (SlotCollection->GetChildren().Num() > 0)
	{
		if (ScrollBar->ShouldBeVisible() != EVisibility::Collapsed)
		{
			ArrangedChildren.AddWidget(ScrollBar->GetVisibility(),
									   AllottedGeometry.MakeChild(ScrollBar.ToSharedRef(), GetOrientationOrthVec(PanelSize) - GetOrientationOrthVec(ScrollBarSize), GetOrientationOrthVec(ScrollBarSize) + GetOrientationOrthVec(PanelSize)));
		}
		// Scroll
		float ArrangingOffset = GetDataOffset(SlotCollection->At(0).DataIndex);

		struct FStretchInfo
		{
			float ColLeft = 0.f;
			float ColRight = 0.f;
			TArray<const FVirtualListSlot*> Slots;
		};

		struct FRowInfo
		{
			TArray<FStretchInfo, TInlineAllocator<4>> ColumnInfo = {{}};
			float CurColLeft = 0.f;
			float CurColRight = 0.f;
			float RowTop = 0.f;
			float RowMaxAxis = 0.f;
			FStretchInfo& AddColumn(float InColRight)
			{
				if (ColumnInfo.Num() > 0)
				{
					Column().ColLeft = CurColLeft;
					Column().ColRight = CurColRight;
					CurColLeft = CurColRight;
				}
				else
				{
					CurColLeft = 0.f;
				}
				CurColRight = InColRight;

				FStretchInfo& Ref = ColumnInfo.Emplace_GetRef(FStretchInfo());
				Ref.ColLeft = CurColLeft;
				Ref.ColRight = CurColRight;
				return Ref;
			}
			FStretchInfo& Column() { return ColumnInfo.Last(); }
		};

		FRowInfo CurRowInfo;
		int32 RowIdx = 0;
		const auto ColOrth = PanelOrth / ColNum;
		auto ArrangeCurrentRow = [&] /* return should continue next */ {
			ImEnsure(!GIsEditor || GetDataAxis(RowIdx++) <= CurRowInfo.RowMaxAxis);
			ON_SCOPE_EXIT { ArrangingOffset += CurRowInfo.RowMaxAxis; };
			if (ArrangingOffset + CurRowInfo.RowMaxAxis < VirtualPos)
				return true;

			if (ArrangingOffset >= VirtualPos + PanelAxis)
				return false;

			for (auto i = 0; i < CurRowInfo.ColumnInfo.Num(); ++i)
			{
				const auto& ColInfo = CurRowInfo.ColumnInfo[i];
				float OffsetInCol = 0.f;
				for (auto ChildSlot : ColInfo.Slots)
				{
					const auto& CurChild = *ChildSlot;

					EVisibility ChildVisibility = CurChild.GetWidget()->GetVisibility();
					ChildVisibility = (ChildVisibility == EVisibility::Collapsed) ? EVisibility::Hidden : ChildVisibility;

					// const ImVec2 ChildDesiredSize = CurChild.GetWidget()->GetDesiredSize();
					ImVec2 SlotSize = ImVec2(ColOrth, GetDataAxis(CurChild.DataIndex));

					// Figure out the size and local position of the child within the slot
					AlignmentArrangeResult XAlignmentResult = AlignChild<Orient_Horizontal>(SlotSize.X, CurChild, ItemPadding);
					AlignmentArrangeResult YAlignmentResult = AlignChild<Orient_Vertical>(SlotSize.Y, CurChild, ItemPadding);

					const ImVec2 LocalPosition = ImVec2(ColInfo.ColLeft + OffsetInCol + XAlignmentResult.Offset, CurRowInfo.RowTop + YAlignmentResult.Offset);
					const ImVec2 LocalSize = ImVec2(XAlignmentResult.Size, YAlignmentResult.Size);

					// Add the information about this child to the output list (ArrangedChildren)
					ArrangedChildren.AddWidget(ChildVisibility, AllottedGeometry.MakeChild(CurChild.GetWidget(), LocalPosition, LocalSize));

					OffsetInCol += GetOrientationOrth(SlotSize);
				}
			}
			return true;
		};

		int32 LastChildVisibleIdx = -1;
		int32 ColumnIdx = 0;
		for (int32 ChildIndex = 0; ChildIndex < SlotCollection->GetChildren().Num(); ++ChildIndex)
		{
			const auto& CurChild = SlotCollection->At(ChildIndex);
			const bool bArrangeOnNewRow = (ColNum <= 1 || !(CurChild.DataIndex % ColNum));
			if (bArrangeOnNewRow)
				ColumnIdx = 0;

			if (CurChild.GetWidget()->GetVisibility() != EVisibility::Collapsed)
			{
				auto ChildDesiredSize = CurChild.GetWidget()->GetDesiredSize();

				if (bArrangeOnNewRow)
				{
					if (LastChildVisibleIdx >= 0)
					{
						if (!ArrangeCurrentRow())
							return;
					}

					CurRowInfo.RowTop = GetDataOffset(CurChild.DataIndex) - VirtualPos;
					CurRowInfo.RowMaxAxis = 0.f;
					CurRowInfo.CurColLeft = 0.f;
					CurRowInfo.CurColRight = ColOrth + ColumnIdx * ColOrth;
					CurRowInfo.ColumnInfo.Empty();
					CurRowInfo.AddColumn(CurRowInfo.CurColRight);
				}

				CurRowInfo.Column().Slots.Add(&CurChild);
				CurRowInfo.RowMaxAxis = FMath::Max(CurRowInfo.RowMaxAxis, GetDataAxis(CurChild.DataIndex));

				if (LastChildVisibleIdx < 0)
					LastChildVisibleIdx = ChildIndex;
			}

			++ColumnIdx;
		}

		if (LastChildVisibleIdx >= 0)
			ArrangeCurrentRow();
	}
}

int32 SImSlateVirtualList::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled)
	const
{
	FArrangedChildren ArrangedChildren(EVisibility::Visible);

	const bool bHasBackground = BackgroundBox && BackgroundBox->GetVisibility() != EVisibility::Collapsed;
	if (bHasBackground)
	{
		const auto PanelSize = AllottedGeometry.GetLocalSize();
		ArrangedChildren.AddWidget(BackgroundBox->GetVisibility(), AllottedGeometry.MakeChild(BackgroundBox.ToSharedRef(), ImVec2(0.f, 0.f), PanelSize));
	}

	this->ArrangeChildren(AllottedGeometry, ArrangedChildren);

	const bool bShouldBeEnabled = ShouldBeEnabled(bParentEnabled);
	int32 ContentLayerId = LayerId + 1;
	int32 MaxLayerId = ContentLayerId;

	if (bHasBackground)
	{
		const FPaintArgs NewArgs = Args.WithNewParent(this);
		const int32 CurWidgetsMaxLayerId = BackgroundBox->Paint(NewArgs, AllottedGeometry, MyCullingRect, OutDrawElements, ContentLayerId, InWidgetStyle, bShouldBeEnabled);
		MaxLayerId = FMath::Max(MaxLayerId, CurWidgetsMaxLayerId);
	}

	if (ArrangedChildren.Num() > 0)
	{
		// Because we paint multiple children, we must track the maximum layer id that they produced in case one of our parents wants to an overlay for all of its contents.
		const FPaintArgs NewArgs = Args.WithNewParent(this);

		FSlateRect MyNewCullingRect(AllottedGeometry.GetRenderBoundingRect(CullingBoundsExtension));
		FSlateClippingZone ClippingZone(MyNewCullingRect);
		ClippingZone.SetShouldIntersectParent(true);
		ClippingZone.SetAlwaysClip(true);
		OutDrawElements.PushClip(ClippingZone);
		ON_SCOPE_EXIT { OutDrawElements.PopClip(); };

		for (int32 ChildIndex = 0; ChildIndex < ArrangedChildren.Num(); ++ChildIndex)
		{
			const FArrangedWidget& CurWidget = ArrangedChildren[ChildIndex];

			if (!IsChildWidgetCulled(MyNewCullingRect, CurWidget))
			{
				const int32 CurWidgetsMaxLayerId = CurWidget.Widget->Paint(NewArgs, CurWidget.Geometry, MyNewCullingRect, OutDrawElements, ContentLayerId, InWidgetStyle, bShouldBeEnabled);
				MaxLayerId = FMath::Max(MaxLayerId, CurWidgetsMaxLayerId);
			}
		}
	}

	if (bShowSoftwareCursor)
	{
		const FSlateBrush* Brush = FCoreStyle::Get().GetBrush(TEXT("SoftwareCursor_Grab"));
		auto InLocalOffset = SoftwareCursorPosition - (Brush->ImageSize / 2);
		float InLocalScale = 1.f;
		FSlateDrawElement::MakeBox(OutDrawElements, ++MaxLayerId, AllottedGeometry.ToPaintGeometry(Brush->ImageSize, FSlateLayoutTransform(InLocalScale, TransformPoint(InLocalScale, UE::Slate::CastToVector2f(InLocalOffset)))), Brush);
	}

	return MaxLayerId;
}

extern void SetDesiredSize(SWidget* InWidget, const ImVec2& InSize);
void SImSlateVirtualList::CacheDesiredSize(float LayoutScaleMultiplier)
{
	if (ScrollBar->GetVisibility() != EVisibility::Collapsed)
	{
		PrepassInternal(ScrollBar.ToSharedRef(), LayoutScaleMultiplier);
	}

	if (BackgroundBox && BackgroundBox->GetVisibility() != EVisibility::Collapsed)
	{
		PrepassInternal(BackgroundBox.ToSharedRef(), LayoutScaleMultiplier);
	}

	ImVec2 Size = (FVector2D)GetDesiredSize();
	Size = Size.HasValidSize() ? Size : Super::ComputeDesiredSize(LayoutScaleMultiplier);

	ImSlate::SetDesiredSize(this, Size);
}

bool SImSlateVirtualList::IsRightClickScrolling() const
{
	return AmountScrolledWhileRightMouseDown >= FSlateApplication::Get().GetDragTriggerDistance() && (this->ScrollBar->IsNeeded() || AllowOverscroll == EAllowOverscroll::Yes);
}

bool SImSlateVirtualList::IsUserScrolling() const
{
	bool bUserScroll = this->ScrollBar->IsNeeded() && this->ScrollBar->IsScrolling();
	return bUserScroll || IsRightClickScrolling();
}

FVector2D SImSlateVirtualList::GetScrollDistance()
{
	return FVector2D(0, ScrollBar->DistanceFromTop());
}

FVector2D SImSlateVirtualList::GetScrollDistanceRemaining()
{
	return FVector2D(0, ScrollBar->DistanceFromBottom());
}

TSharedRef<SWidget> SImSlateVirtualList::GetScrollWidget()
{
	return SharedThis(this);
}

void SImSlateVirtualList::OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.IsTouchEvent())
	{
		if (!bStartedTouchInteraction)
		{
			if (!!MyGeometry.IsUnderLocation(MouseEvent.GetLastScreenSpacePosition()))
			{
				bStartedTouchInteraction = true;
			}
		}
	}
}

void SImSlateVirtualList::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	SCompoundWidget::OnMouseLeave(MouseEvent);

	bStartedTouchInteraction = false;
	if (!HasMouseCapture())
	{
		AmountScrolledWhileRightMouseDown = 0;
	}
}

FReply SImSlateVirtualList::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.IsMouseButtonDown(EKeys::RightMouseButton) && !MouseEvent.IsTouchEvent())
	{
		float ScrollAxis = GetOrientationAxis(MouseEvent.GetCursorDelta());
		const float ScrollByAmount = ScrollAxis / MyGeometry.Scale;
		AmountScrolledWhileRightMouseDown += FMath::Abs(ScrollByAmount);

		if (IsRightClickScrolling())
		{
			bool bScrolled = InnerScrollToPos(FMath::Max(0.f, VirtualPos - ScrollByAmount), GetOrientationAxis(MyGeometry.GetLocalSize()));

			FReply Reply = FReply::Handled();
			if (this->HasMouseCapture() == false)
			{
				Reply.CaptureMouse(AsShared()).UseHighPrecisionMouseMovement(AsShared());
				SoftwareCursorPosition = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
				bShowSoftwareCursor = true;
			}
			SoftwareCursorPosition += FVector2D(0.f, bScrolled ? ScrollAxis / MyGeometry.Scale : 0.f);
			return Reply;
		}
	}

	return FReply::Unhandled();
}

FReply SImSlateVirtualList::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (ScrollBar->IsNeeded() && DataBinding->GetDataCount() > 0)
	{
		auto NewVirtualPos = VirtualPos;
		NewVirtualPos -= MouseEvent.GetWheelDelta() * GetGlobalScrollAmount();
		if (UpdateRange(GetListAxis(), NewVirtualPos))
		{
			return FReply::Handled().PreventThrottling();
		}
	}

	return FReply::Unhandled().PreventThrottling();
}

FReply SImSlateVirtualList::OnPreviewMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.IsTouchEvent() /* && ScrollBar->IsNeeded() && DataBinding->GetDataCount() > 0*/)
	{
		AmountScrolledWhileRightMouseDown = 0;
		PressedScreenSpacePosition = MouseEvent.GetScreenSpacePosition();
		bStartedTouchInteraction = true;
		return FReply::Unhandled();
	}
	else
	{
		return FReply::Unhandled();
	}
}

FReply SImSlateVirtualList::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		OnRightMouseButtonDown(MouseEvent);
	}

	if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton && ScrollBar->IsNeeded())
	{
		AmountScrolledWhileRightMouseDown = 0;
		return FReply::Handled().PreventThrottling();
	}
	else if (this->HasMouseCapture())
	{
		return FReply::Handled().PreventThrottling();
	}
	return FReply::Unhandled().PreventThrottling();
}

FReply SImSlateVirtualList::OnMouseButtonDoubleClick(const FGeometry& InMyGeometry, const FPointerEvent& MouseEvent)
{
	if (this->HasMouseCapture())
	{
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FReply SImSlateVirtualList::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		OnRightMouseButtonUp(MouseEvent);

		FReply Reply = FReply::Handled().ReleaseMouseCapture();
		bShowSoftwareCursor = false;

		if (HasMouseCapture())
		{
			FSlateRect ListScreenSpaceRect = MyGeometry.GetLayoutBoundingRect();
			FVector2D CursorPosition = MyGeometry.LocalToAbsolute(SoftwareCursorPosition);

			FIntPoint BestPositionInList(FMath::RoundToInt(FMath::Clamp(CursorPosition.X, ListScreenSpaceRect.Left, ListScreenSpaceRect.Right)),
										 FMath::RoundToInt(FMath::Clamp(CursorPosition.Y, ListScreenSpaceRect.Top, ListScreenSpaceRect.Bottom)));

			Reply.SetMousePos(BestPositionInList);
		}

		return Reply;
	}
	return FReply::Unhandled();
}

void SImSlateVirtualList::OnRightMouseButtonUp(const FPointerEvent& MouseEvent)
{
	const FVector2D& SummonLocation = MouseEvent.GetScreenSpacePosition();
	const bool bShouldOpenContextMenu = !IsRightClickScrolling();
	const bool bContextMenuOpeningBound = OnContextMenuOpening.IsBound();

	if (bShouldOpenContextMenu && bContextMenuOpeningBound)
	{
		if (TSharedPtr<SWidget> MenuContent = OnContextMenuOpening.Execute())
		{
			bShowSoftwareCursor = false;
			FWidgetPath WidgetPath = MouseEvent.GetEventPath() != nullptr ? *MouseEvent.GetEventPath() : FWidgetPath();
			FSlateApplication::Get().PushMenu(AsShared(), WidgetPath, MenuContent.ToSharedRef(), SummonLocation, FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu));
		}
	}

	AmountScrolledWhileRightMouseDown = 0;
}

FReply SImSlateVirtualList::OnTouchStarted(const FGeometry& MyGeometry, const FPointerEvent& TouchEvent)
{
	return FReply::Unhandled();
}

FReply SImSlateVirtualList::OnTouchEnded(const FGeometry& MyGeometry, const FPointerEvent& TouchEvent)
{
	AmountScrolledWhileRightMouseDown = 0;
	bStartedTouchInteraction = false;

	ScrollBar->EndScrolling();
	if (HasMouseCapture())
	{
		return FReply::Handled().ReleaseMouseCapture();
	}
	else
	{
		return FReply::Handled();
	}
}

FReply SImSlateVirtualList::OnTouchFirstMove(const FGeometry& MyGeometry, const FPointerEvent& TouchEvent)
{
	return Super::OnTouchFirstMove(MyGeometry, TouchEvent);
}

FReply SImSlateVirtualList::OnTouchMoved(const FGeometry& MyGeometry, const FPointerEvent& TouchEvent)
{
	if (bStartedTouchInteraction)
	{
		float ScrollAxis = GetOrientationAxis(TouchEvent.GetCursorDelta());
		const float ScrollByAmount = ScrollAxis / MyGeometry.Scale;

		AmountScrolledWhileRightMouseDown += FMath::Abs(ScrollByAmount);

		//if (FSlateApplication::Get().HasTraveledFarEnoughToTriggerDrag(TouchEvent, PressedScreenSpacePosition))
		auto DragDist = (PressedScreenSpacePosition - (FVector2D)TouchEvent.GetScreenSpacePosition()).GetAbs();
		if (GetOrientationAxis(DragDist) >= FSlateApplication::Get().GetDragTriggerDistance())
		{
			InnerScrollToPos(FMath::Max(0.f, VirtualPos - ScrollByAmount), GetOrientationAxis(MyGeometry.GetLocalSize()));

			ScrollBar->BeginScrolling();
			return FReply::Handled().CaptureMouse(AsShared()).PreventThrottling();
		}
		return FReply::Handled().PreventThrottling();
	}
	else
	{
		return FReply::Handled().PreventThrottling();
	}
}

FReply SImSlateVirtualList::OnTouchForceChanged(const FGeometry& MyGeometry, const FPointerEvent& TouchEvent)
{
	return Super::OnTouchForceChanged(MyGeometry, TouchEvent);
}

void SImSlateVirtualList::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	if (!DataBinding)
		return;

	auto LocalSize = AllottedGeometry.GetLocalSize();
	if (!HasValidCol() || IndexRange.HasChanged())
	{
		TotalCol = GetTileOrth() > 0.f ? FMath::Max(1, int32(GetOrientationOrth(LocalSize) / GetTileOrth())) : 1;
		if (InnerScrollToPos(VirtualPos, GetOrientationAxis(LocalSize), false))
			GenerateWidgetIfNeeded();
	}
	else
	{
		auto OldSize = GetCachedGeometry().GetLocalSize();
		if (OldSize != LocalSize)
		{
			if (GetTileOrth() <= 0.f || GetTotalCol() == int32(GetOrientationOrth(LocalSize) / GetTileOrth()))
			{
				if (GetOrientationAxis(OldSize) != GetOrientationAxis(LocalSize) && ImEnsure(UpdateRange(GetOrientationAxis(LocalSize), VirtualPos, true)))
					GenerateWidgetIfNeeded();
			}
			else
			{
				TotalCol = FMath::Max(1, int32(GetOrientationOrth(LocalSize) / GetTileOrth()));
				if (ImEnsure(InnerReloadToPos(VirtualPos)))
					GenerateWidgetIfNeeded();
			}
		}
	}

	Super::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
}

FReply SImSlateVirtualList::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (OnItemKeyEvent.IsBound() && OnItemKeyEvent.Execute(this, MyGeometry, InKeyEvent))
	{
		return FReply::Handled().PreventThrottling();
	}
	else if (!IsHeterogeneous())
	{
		if (InKeyEvent.IsControlDown() && InKeyEvent.GetKey() == EKeys::Home)
		{
			ScrollToItem(0);
			return FReply::Handled().PreventThrottling();
		}
		else if (InKeyEvent.IsControlDown() && InKeyEvent.GetKey() == EKeys::End)
		{
			ScrollToItem(GetDataCount() - 1);
			return FReply::Handled().PreventThrottling();
		}
	}
	return Super::OnKeyDown(MyGeometry, InKeyEvent).PreventThrottling();
}

FReply SImSlateVirtualList::OnKeyUp(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (OnItemKeyEvent.IsBound() && OnItemKeyEvent.Execute(this, MyGeometry, InKeyEvent))
	{
		return FReply::Handled().PreventThrottling();
	}
	return Super::OnKeyUp(MyGeometry, InKeyEvent).PreventThrottling();
}

void SImSlateVirtualList::OnFocusLost(const FFocusEvent& InFocusEvent)
{
	bShowSoftwareCursor = false;
}

void SImSlateVirtualList::OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent)
{
	Super::OnMouseCaptureLost(CaptureLostEvent);

	bShowSoftwareCursor = false;
}

bool SImSlateVirtualList::SupportsKeyboardFocus() const
{
	return true;  // focusable.
}

}  // namespace ImSlate

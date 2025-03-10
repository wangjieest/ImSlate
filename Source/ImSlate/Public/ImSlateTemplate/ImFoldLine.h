// Copyright ImSlate, Inc. All Rights Reserved.
#pragma once

#include "Components/TextBlock.h"
#include "ImSlateFactory.h"
#include "Widgets/Text/STextBlock.h"

//
#include "ImFoldLine.generated.h"

class IMSLATE_API SImFoldLine : public STextBlock
{
public:
	using FArguments = STextBlock::FArguments;
	void Construct(const FArguments& Args, bool bInIsFolded = false)
	{
		bIsFolded = bInIsFolded;
		return STextBlock::Construct(Args);
	}
	bool GetIsFolded() const { return bIsFolded; }
	void SetFold(bool bInIsFolded = true) { bIsFolded = bInIsFolded; }

protected:
	bool bIsFolded = false;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled)
		const override;
	friend class UImFoldLine;
	FInvalidatableBrushAttribute BorderImage;
};

UCLASS(BlueprintType)
class IMSLATE_API UImFoldLine
	: public UTextBlock
	, public TImFactory<SImFoldLine>
{
	GENERATED_BODY()
public:
	TSharedRef<SImFoldLine> ConstructImWidget() const;
	virtual TSharedRef<SWidget> RebuildWidget() override;

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Appearance, meta = (DisplayName = "BackgroudBrush"))
	FSlateBrush Background;

protected:
	IM_SLATE_PALETTECATEGORY()
};

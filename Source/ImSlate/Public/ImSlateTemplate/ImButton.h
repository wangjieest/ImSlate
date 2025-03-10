// Copyright ImSlate, Inc. All Rights Reserved.
#pragma once

#include "Components/Button.h"
#include "ImSlateFactory.h"
#include "ImMultiStateButton.h"

//
#include "ImButton.generated.h"


/**
 * Slate's Buttons are clickable Widgets that can contain arbitrary widgets as its Content().
 */
class SImButton : public SButton
{
public:
	using FArguments = SButton::FArguments;

	SImButton();

	/** @return An image that represents this button's border*/
	virtual const FSlateBrush* GetBorder() const override;

	/**
	 * Construct this widget
	 *
	 * @param	InArgs	The declaration data for this widget
	 */
	void Construct(const FArguments& InArgs, UImMultiStateButton* InButtonObject);

	void SetButtonExtraStyle(const FImButtonExtraStyle* ButtonStyle);

	virtual bool IsFocused() const;

	virtual bool IsDragging() const;

	void EnterCustomState(FImCustomWidgetState* State);

	void QuitCustomState();

public:
	FReply OnFocusReceived(const FGeometry& MyGeometry, const FFocusEvent& InFocusEvent) override;
	void OnFocusLost(const FFocusEvent& InFocusEvent) override;
	FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	FReply OnDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	void OnDropOperation();

protected:
	
	const FSlateBrush* FocusedImage = nullptr;

	const FSlateBrush* DraggedImage = nullptr;

	bool bUseCustomState = false;
	FImCustomWidgetState CustomState;

	bool bInDragDrop = false;
	virtual void Drop();

protected:

	TWeakObjectPtr<UImMultiStateButton> ButtonObject;
};

UCLASS(BlueprintType)
class IMSLATE_API UImButton
	: public UButton
	, public TImFactory<SButton>
{
	GENERATED_BODY()
public:
	TSharedRef<SButton> ConstructImWidget() const;

protected:
	IM_SLATE_PALETTECATEGORY()
};

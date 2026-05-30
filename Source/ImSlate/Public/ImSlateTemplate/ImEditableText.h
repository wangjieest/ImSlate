// Copyright ImSlate, Inc. All Rights Reserved.
#pragma once

#include "SlateCore.h"

#include "Components/EditableText.h"
#include "ImSlateFactory.h"

//
#include "ImEditableText.generated.h"

class IMSLATE_API SImEditableText : public SEditableText
{
protected:
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled)
		const override;
	virtual FReply OnFocusReceived(const FGeometry& MyGeometry, const FFocusEvent& InFocusEvent) override;
	virtual FReply OnKeyChar(const FGeometry& MyGeometry, const FCharacterEvent& InCharacterEvent) override;
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

public:
	virtual ~SImEditableText();

	void SetBorderImage(const TAttribute<const FSlateBrush*>& InBrushAttribute);
	void SetVirtualKeyboardCommitCallback(TFunction<void(const FString&, ETextCommit::Type)> InCallback) { VKCommitCallback = MoveTemp(InCallback); }
	void SetVirtualKeyboardSuggestionProvider(TFunction<void(const FString&, TArray<FString>&)> InProvider) { VKSuggestionProvider = MoveTemp(InProvider); }

	// Preview display mode: used by the virtual keyboard's preview text row.
	// In this mode the widget only *displays* text (driven externally via SetText/GoTo);
	// it never grabs focus or pops the keyboard, and it self-draws the caret (engine
	// only draws the native caret when focused, which we deliberately avoid here).
	void SetPreviewDisplayMode(bool bEnable) { bPreviewDisplayMode = bEnable; }
	// Caret blink toggle, driven by the keyboard's blink timer (OnPaint is const, only reads it).
	void SetPreviewCaretVisible(bool bVisible) { bPreviewCaretVisible = bVisible; }

private:
	FInvalidatableBrushAttribute BorderImage;
	TFunction<void(const FString&, ETextCommit::Type)> VKCommitCallback;
	TFunction<void(const FString&, TArray<FString>&)> VKSuggestionProvider;
	bool bPreviewDisplayMode = false;
	bool bPreviewCaretVisible = true;
};

UCLASS(BlueprintType)
class IMSLATE_API UImEditableText
	: public UEditableText
	, public TImFactory<SEditableText>
{
	GENERATED_BODY()
public:
	TSharedRef<SEditableText> ConstructImWidget() const;

protected:
	//~ Begin UWidget Interface
	virtual TSharedRef<SWidget> RebuildWidget() override;
	// End of UWidget
public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Appearance, meta = (DisplayName = "BackgroudBrush"))
	FSlateBrush BackgroundImage;

protected:
	IM_SLATE_PALETTECATEGORY()
};

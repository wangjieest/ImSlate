// Copyright ImSlate, Inc. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "Components/CheckBox.h"
#include "Templates/SharedPointer.h"
#include "ImSlateFactory.h"
#include "Widgets/Input/SCheckBox.h"

#include "ImCheckBox.generated.h"

USTRUCT(BlueprintType)
struct IMSLATE_API FImCheckBoxExtraStyle
{
	GENERATED_USTRUCT_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Appearance)
	FSlateBrush DisabledCheckedImage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Appearance)
	FSlateBrush DisabledUncheckedImage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Appearance)
	FSlateBrush DisabledUndeterminedImage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Appearance)
	FSlateBrush FocusedCheckedImage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Appearance)
	FSlateBrush FocusedUncheckedImage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Appearance)
	FSlateBrush FocusedUndeterminedImage;
};

class IMSLATE_API SImCheckBox : public SCheckBox
{
public:
	using FArguments = SCheckBox::FArguments;

	SImCheckBox();

	void Construct(const FArguments& InArgs);

	void SetStyle(const FCheckBoxStyle* InStyle, const FImCheckBoxExtraStyle* InExtraStyle = nullptr);

	void BuildCheckBox(TSharedRef<SWidget> InContent);
	const FSlateBrush* OnGetCheckImage() const;
	bool IsFocused() const { return HasKeyboardFocus(); }

	void SetOnCheckStateChanged(FOnCheckStateChanged InDelegate);

protected:
	const FImCheckBoxExtraStyle* ExtraStyle = nullptr;
};

UCLASS()
class IMSLATE_API UImCheckBox
	: public UCheckBox
	, public TImFactory<SImCheckBox>
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style", meta = (DisplayName = "Extra Style"))
	FImCheckBoxExtraStyle ExtraStyle;

public:
	TSharedRef<SImCheckBox> ConstructImWidget() const;

	void SynchronizeProperties() override;
	void ReleaseSlateResources(bool bReleaseChildren) override;

protected:
	TSharedPtr<SImCheckBox> MyImCheckbox;

	TSharedRef<SWidget> RebuildWidget() override;
};

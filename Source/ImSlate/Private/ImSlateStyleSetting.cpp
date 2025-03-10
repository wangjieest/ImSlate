// Copyright ImSlate, Inc. All Rights Reserved.
#include "ImSlateStyleSetting.h"

//
#include "ClassDataStorage.h"
#include "Editor/UnrealEditorUtils.h"

namespace ImSlate
{
FText GetPaletteCategory()
{
	return NSLOCTEXT("ImSlate", "ImSlate", "ImSlate");
}
namespace Factory
{
	struct FDefalutClassData
	{
		TSubclassOf<UWidget> Class;
	};

	struct FDefalutClassDataMap : public ClassStorage::TClassStorageImpl<FDefalutClassData>
	{
	};

	FDefalutClassDataMap DefaultClassStorage;

	UClass* GetDefaultImpl(TSubclassOf<UWidget> InClass)
	{
		check(InClass.Get());
		FDefalutClassData* Ptr = DefaultClassStorage.FindData(InClass.Get());
		return ensure(Ptr && Ptr->Class) ? Ptr->Class : InClass;
	}

	void ModifyDefault(UClass* InClass, UClass* InNativeClass)
	{
		check(InNativeClass);
		ensure(InClass);
		DefaultClassStorage.ModifyData(
			InNativeClass,
			true,
			[&](FDefalutClassData& InOutData) { InOutData.Class = InClass; },
			false);
	}
	template<typename U>
	void ModifyDefault(TSubclassOf<U> InClass)
	{
		ModifyDefault(InClass.Get(), U::StaticClass());
	}
}  // namespace Factory
}  // namespace ImSlate

UXImSlateStyleSetting::UXImSlateStyleSetting()
{
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		UnrealEditorUtils::AddConfigurationOnProject(this, FName("ImSlateStyle"));
	}
}

void UXImSlateStyleSetting::PreloadClasses() const
{
	using namespace ImSlate::Factory;
#if 1
	for (FProperty* Property = GetClass()->PropertyLink; Property != nullptr; Property = Property->PropertyLinkNext)
	{
		if (FClassProperty* ClsProp = CastField<FClassProperty>(Property))
		{
			ModifyDefault(*ClsProp->ContainerPtrToValuePtr<UClass*>(this), ClsProp->MetaClass);
		}
	}
#else
	ModifyDefault(ImSlateButton);
	ModifyDefault(ImSlateImageButton);
	ModifyDefault(ImSlateText);
	ModifyDefault(ImSlateBorder);
	ModifyDefault(ImSlateEditableText);
	ModifyDefault(ImSlateSpinBox);
	ModifyDefault(ImSlateCheckBox);
	ModifyDefault(ImSlateNumericWidget);
	ModifyDefault(ImSlateColorWidget);
	ModifyDefault(ImSlateMobilityWidget);
	ModifyDefault(ImSlateImage);
	ModifyDefault(ImSlateComboButton);
	ModifyDefault(ImSlateVirtualComboButton);
	ModifyDefault(ImSlateFoldLine);
	ModifyDefault(ImSlateEditableTextBox);
	ModifyDefault(ImSlateCollisionData);
	ModifyDefault(ImVirtualList);
#endif
}

void UXImSlateStyleSetting::PostLoad()
{
	if (HasAnyFlags(RF_ClassDefaultObject))
		LoadConfig();
	Super::PostLoad();
	PreloadClasses();
}

void UXImSlateStyleSetting::PostInitProperties()
{
	Super::PostInitProperties();
	if (HasAnyFlags(RF_ClassDefaultObject))
		PreloadClasses();
}

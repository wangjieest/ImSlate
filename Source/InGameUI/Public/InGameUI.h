// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GMPHelper.h"
#include "InGameUI.generated.h"

// This class does not need to be modified.
UINTERFACE()
class UInGameUIInc : public UInterface
{
	GENERATED_BODY()
};

class INGAMEUI_API IInGameUIInc
{
	GENERATED_BODY()
protected:
	// ShowWidget时调用，不分辩Create(OnCreate?)/SetVisibility
	UFUNCTION(BlueprintNativeEvent, Category = "InGame UI")
	void OnShown(const FGMPStructUnion& InPayload);
	virtual void OnShown_Implementation(const FGMPStructUnion& InPayload);

	UFUNCTION(BlueprintNativeEvent, Category = "InGame UI")
	void OnDestroyed();
	virtual void OnDestroyed_Implementation();
};

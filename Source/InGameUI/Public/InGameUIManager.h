// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "UObject/NoExportTypes.h"
#include "GMPHelper.h"
#include "ImSlateWidgetPool.h"
#include "Components/CanvasPanel.h"
#include "InGameUI.h"
#include "InGameUIManager.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogInGameUIManager, Log, All);
DECLARE_DYNAMIC_DELEGATE(FOnShownComplete);
DECLARE_DYNAMIC_DELEGATE_OneParam(FOnShownFailed, const FString&, ErrorMsg);

USTRUCT(BlueprintType)
struct FInGameUIRegisterInfo : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere)
	FName RegName;

	UPROPERTY(EditAnywhere, meta = (MustImplement = "/Script/InGameUI.InGameUIInc"))
	TSoftClassPtr<UWidget> WidgetClass;
};



/*
* 自定义CanvasPanel，用于接管界面生命周期，以及控制在Parent显示
*/
UCLASS()
class INGAMEUI_API UInGameMainPanelBase : public UCanvasPanel
{
	GENERATED_BODY()
public:
	virtual void AddToGame(UWorld* InWorld, int32 ZOrder = 0);

	template<typename WidgetT>
	FORCEINLINE_DEBUGGABLE WidgetT* ConstructWidget(TSubclassOf<UWidget> WidgetClass = WidgetT::StaticClass(), FName WidgetName = NAME_None)
	{
		static_assert(TIsDerivedFrom<WidgetT, UWidget>::IsDerived, "Custom ConstructWidget can only create UWidget objects.");

		if (WidgetClass->IsChildOf<UUserWidget>())
		{
			UUserWidget* NewWidget = NewObject<UUserWidget>(this, *WidgetClass, WidgetName, RF_Transactional);
			NewWidget->Initialize();
			return Cast<WidgetT>(NewWidget);
		}

		return NewObject<WidgetT>(this, WidgetClass, WidgetName, RF_Transactional);
	}

protected:
	virtual void OnLeaveWorld(UWorld* InWorld);
	virtual void OnSlotAdded(UPanelSlot* Slot) override;
	virtual void OnSlotRemoved(UPanelSlot* Slot) override;
};

/**
 * UI Manager 管理器
 * 
 */
UCLASS(config = Game, defaultConfig)
class UInGameUIManager : public UObject
{
	GENERATED_BODY()
	friend UInGameMainPanelBase;

public:
	static UInGameUIManager* Get(UObject* Ctx, bool bCreate = true);
	bool InitUIManager(const TArray<FInGameUIRegisterInfo*>& RowItems, TSubclassOf<UInGameMainPanelBase> OverrideCls);
	bool ShowWidgetByName(FName InName, const FGMPStructUnion& InPayLoad, bool bBringToTop, const FOnShownComplete& OnComplete, const FOnShownFailed& OnFailed);
	bool RegisterWidget(const FInGameUIRegisterInfo& RegInfo);
	bool UnregisterWidget(const FInGameUIRegisterInfo& RegInfo);
	void SetWidgetVisibility(FName InName, ESlateVisibility InVisibility);
	bool BringWidgetToTop(FName InName);

	// 通过名字检测当前UI是否被注册
	FORCEINLINE bool IsUIRegisted(FName InName) const { return !!GlobalUIMap.Find(InName); }
	FORCEINLINE bool IsUICreated(FName InName) const { return CreatedWidgetMap.Find(InName) && IsValid(CreatedWidgetMap[InName]); };

	bool DestroyAllWidget();
	void HiddenAll();
	bool DestroyWidget(FName InName);

protected:
	UPROPERTY(Config, EditAnywhere)
	TSoftClassPtr<UInGameMainPanelBase> MainPanelClass;
	UPROPERTY(Transient)
	UClass* MainPanelClassHolder = nullptr;

	UClass* GetManPanerlClass(TSubclassOf<UInGameMainPanelBase> InClass);

private:


	// 更新当前栈中的所有UMG ZOrder
	void UpdateAllWidgetZOrder();

	// 保存当前游戏中的UI栈，保持顺序关系	
	TArray<FName> ActivateWidgetStack;

	// 对应当前游戏中存在的Widget
	UPROPERTY(Transient)
	TMap<FName, UWidget*> CreatedWidgetMap;

	// UPROPERTY(Transient)
	// FUWidgetPool WidgetPool;

	// 记录全局注册的UI信息，对应DataTable
	UPROPERTY(Transient)
	TMap<FName, TSoftClassPtr<UWidget>> GlobalUIMap;

	UPROPERTY(Transient)
	UInGameMainPanelBase* MainPanel = nullptr;

	// MainPanel所在Parent的ZOrder
	const int32 DefaultZOrder = 10;
};

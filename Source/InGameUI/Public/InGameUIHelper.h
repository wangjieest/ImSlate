// Fill out your copyright notice in the Description page of Project Settings.

#pragma once
#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Engine/DataTable.h"
#include "InGameUIManager.h"
#include "InGameUIHelper.generated.h"

UCLASS(Transient, meta = (NeuronAction))
class INGAMEUI_API UInGameUIHelper : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	/*
	* @func: 初始化 UI Manager
	* @param Ctx: 上下文
	* @param Table: DataTable指针，RowStruct必须继承自InGameUIRegisterInfo
	*/
	UFUNCTION(BlueprintCallable, Category = "InGame UI Manager (In Game)", meta = (WorldContext = "Ctx", CustomDataTableFilter = "Table"))
	static bool InitInGameUIManager(UObject* Ctx, UPARAM(meta = (DataTableMetaStruct = "InGameUIRegisterInfo")) UDataTable* Table, TSubclassOf<UInGameMainPanelBase> OverrideCls = nullptr);

	UFUNCTION(BlueprintCallable, Category = "InGame UI Manager (In Game)", meta = (WorldContext = "Ctx"))
	static bool RegisterInGameUI(UObject* Ctx, FInGameUIRegisterInfo RegInfo);
	UFUNCTION(BlueprintCallable, Category = "InGame UI Manager (In Game)", meta = (WorldContext = "Ctx"))
	static bool UnregisterInGameUI(UObject* Ctx, FInGameUIRegisterInfo RegInfo);

	/*
	* @func: 异步创建Widget，并添加到Viewport
	* @param Ctx: 上下文 
	* @paran InName: 通过DataTable注册的Widget Name
	* @param InPayload: 传入参数，Widget创建后将调用IInGameUI的OnShown方法
	* @param OnComplete: 成功后回调
	* @param OnFailed: 失败回调
	*/
	UFUNCTION(BlueprintCallable, BlueprintInternalUseOnly, Category = "InGame UI Manager (In Game)", meta = (WorldContext = "Ctx", NeuronAction, HideThen))
	static void ShowInGameWidget(UObject* Ctx, 
								FName InName, 
								FGMPStructUnion InPayload, 
								bool bBringToTop, 
								const FOnShownComplete& OnComplete, 
								const FOnShownFailed& OnFailed);

	/*
	* @func 将界面隐藏
	* @param Ctx: 上下文
	* @param InName: 通过DataTable注册的Widget Name
	* @param bDestroy: 是否销毁，false只是隐藏
	*/
	UFUNCTION(BlueprintCallable, Category = "InGame UI Manager (In Game)", meta = (WorldContext = "Ctx"))
	static void HideInGameWidget(UObject* Ctx, FName InName, bool bDestroy = false);

	/*
	* @func 将所有界面隐藏
	* @param Ctx: 上下文
	* @param InName: 通过DataTable注册的Widget Name
	* @param bDestroy: 是否销毁
	*/
	UFUNCTION(BlueprintCallable, Category = "InGame UI Manager (In Game)", meta = (WorldContext = "Ctx"))
	static void HideAllInGameWidget(UObject* Ctx, bool bDestroy = false);
};
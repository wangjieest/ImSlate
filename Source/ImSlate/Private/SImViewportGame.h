// Copyright ImSlate, Inc. All Rights Reserved.
#pragma once
#include "SImSlateViewport.h"

class ILevelEditor;
namespace ImSlate
{
class IMSLATE_API SImViewportGame final : public SImSlateViewport
{
public:
	using FArguments = SImSlateViewport::FArguments;
	void Construct(const FArguments& InArgs, int32 InZOrder = 1024);

public:
	virtual void BringWindowToFront(const TSharedRef<SImSlateWindow>& InWindow) override;
	virtual void AddWindow(const TSharedRef<SImSlateWindow>& InWindow) override;
	virtual int32 RemoveWindow(const TSharedRef<SImSlateWindow>& InWidget) override;
	virtual bool IsGameViewport() const override { return true; }

	virtual bool Contains(const FGeometry& WindowGeometry) const;
	virtual void SetWindowPos(SImSlateWindow* InWindow, TOptional<FVector2D> InPos) override;
	virtual void SetWindowSize(SImSlateWindow* InWindow, TOptional<FVector2D> InSize) override;
	virtual FVector2D GetWindowPos(const SImSlateWindow* InWindow) const override;
	virtual FVector2D GetWindowSize(const SImSlateWindow* InWindow) const override;

protected:
	virtual void OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const override;
#if WITH_EDITOR
	TWeakPtr<ILevelEditor> WeakLevelEditor;
	static TWeakPtr<ILevelEditor> CurrentWeakLevelEditor;
#endif
};

}  // namespace ImSlate

// Copyright ImSlate, Inc. All Rights Reserved.
#include "EngineMinimal.h"
#include "SlateGlobals.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GMPCore.h"
#include "GMPSignals.h"
#include "GenericSingletons.h"
#include "ImSlate.h"
#include "Misc/CoreDelegates.h"
#include "Modules/ModuleManager.h"
#include "SImSlateViewport.h"
#include "SImViewportGame.h"
#include "SImViewportHost.h"
#include "UnrealEngine.h"
#include "WorldLocalStorages.h"

#if WITH_EDITOR
#include "Editor/LevelEditor/Public/SLevelViewport.h"
#include "Editor/UnrealEd/Public/LevelEditorViewport.h"
#include "Editor/UnrealEd/Public/SEditorViewport.h"
#endif

#if WITH_EDITOR
ImSlate::ImSlateContext* EditorGImSlate = nullptr;
#else
IMSLATE_API ImSlate::ImSlateContext* GImSlate = nullptr;
#endif

namespace ImSlate
{
struct FWorldContextRoot;
FORCEINLINE ImSlateContext*& GetGlobalImSlate()
{
#if WITH_EDITOR
	return EditorGImSlate;
#else
	return GImSlate;
#endif
}
extern void BeginFrameImpl(float DeltaTime, ImSlateContext* Ptr);
extern void EndFrameImpl(ImSlateContext* Ptr);

static TArray<FWorldContextRoot*, TInlineAllocator<4>> ImSlateRoots;
static auto& GetSlateRoots()
{
	return ImSlateRoots;
}

struct FWorldContextRoot : public ImSlateContext
{
	template<typename F>
	static bool OnViewportCreated(UWorld* World, F Func /* = [World] { OnWorldViewport(World) */)
	{
		if (auto Viewport = World->GetGameViewport())
		{
#if WITH_EDITOR || 1
			CallOnWorldNextTick(World, MoveTemp(Func));
#else
			OnWorldViewport(World);
#endif
		}
		else
		{
			UGameViewportClient::OnViewportCreated().AddWeakLambda(World, MoveTemp(Func));
		}
	}
	static void OnWorldViewport(UWorld* World) { WorldLocalStorages::GetLocalValue<ImSlate::FWorldContextRoot>(World, World); }

	FWorldContextRoot(UWorld* InWorld)
	{
		RawWorldPtr = InWorld;
		CurrentWorld = InWorld;
#if WITH_EDITOR
		//ensureAlways(InWorld == GWorld);
		GetSlateRoots().Add(this);
#endif
		if (GetGlobalImSlate() == nullptr)
		{
			GetGlobalImSlate() = this;
		}

		this->PIEInstanceID = GetWorldChecked()->GetOutermost()->GetPIEInstanceID();
		if (auto Viewport = InWorld->GetGameViewport())
		{
			this->Viewports.Add(SNew(SImViewportGame, 1023)
								.GameViewportClient(Viewport)
								.Visibility(EVisibility::SelfHitTestInvisible));
			Viewport->OnTick().AddRaw(this, &FWorldContextRoot::Tick);
			WeakViewPortClient = Viewport;
		}
#if WITH_EDITOR
		else if (InWorld->WorldType == EWorldType::Editor)
		{
			for (FLevelEditorViewportClient* ViewClient : GEditor->GetLevelViewportClients())
			{
				if (!ViewClient->IsLevelEditorClient())
					continue;
				auto EditorViewport = ViewClient->GetEditorViewportWidget();
				if (!EditorViewport)
					continue;
				auto IncLevelEditor = StaticCastSharedPtr<SLevelViewport>(EditorViewport)->GetParentLevelEditor().Pin();
				if (!IncLevelEditor)
					continue;
				this->Viewports.Add(SNew(SImViewportGame, 1023)
									.LevelEditor(IncLevelEditor)
									.Visibility(EVisibility::SelfHitTestInvisible));
			}
			GEditor->OnPostEditorTick().AddRaw(this, &FWorldContextRoot::TickEditor);
			BeginFrameImpl(0.01f, this);
		}
#endif
	}
	~FWorldContextRoot()
	{
		if (WeakViewPortClient.IsValid())
		{
			WeakViewPortClient->OnTick().RemoveAll(this);
		}
#if WITH_EDITOR
		if (GIsEditor)
		{
			GEditor->OnPostEditorTick().RemoveAll(this);
		}
#endif
		for (auto& Viewport : this->Viewports)
		{
			Viewport->RemoveAllWindow();
			Viewport->ClearChildren();
		}
		this->Viewports.Reset();

		if (GetGlobalImSlate() == this)
		{
			GetGlobalImSlate() = nullptr;
		}
#if WITH_EDITOR
		GetSlateRoots().Remove(this);
#endif
	}
	GMP::TSignal<false, float, UWorld*> ImSlateSignals;

protected:
	void Tick(float DeltaTime)
	{
		if (this->bIsFrameStarted)
		{
			// Ending frame will produce render output that we capture and store for later use. This also puts context to
			// state in which it does not allow to draw controls, so we want to immediately start a new frame.
			EndFrameImpl(this);
		}

		// Begin a new frame and set the context back to a state in which it allows to draw controls.
		BeginFrameImpl(DeltaTime, this);
		ImSlateSignals.FireWithSigSource(RawWorldPtr, DeltaTime, RawWorldPtr);
	}
#if WITH_EDITOR
	void TickEditor(float DeltaTime)
	{
		static_assert(sizeof(UWorld*) == sizeof(GWorld), "err");
		TGuardValue<UWorld*> WroldGuard(reinterpret_cast<UWorld*&>(GWorld), RawWorldPtr);
		Tick(DeltaTime);
	}
#endif
	TWeakObjectPtr<UGameViewportClient> WeakViewPortClient;
};
}  // namespace ImSlate

class FImSlateModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		GMP::OnGMPTagReady(FSimpleDelegate::CreateLambda([this] {
			FGMPHelper::UnsafeListenMessage(MSGKEY("GameInstance.OnStart"), [this](UWorld* World) { OnGameInstanceStart(World); });
			FCoreUObjectDelegates::PostLoadMapWithWorld.AddRaw(this, &FImSlateModule::OnGameInstanceStart);
		}));
		FWorldDelegates::OnWorldCleanup.AddRaw(this, &FImSlateModule::OnWorldCleanup);
	}

	virtual void ShutdownModule() override
	{
		FCoreUObjectDelegates::PostLoadMapWithWorld.RemoveAll(this);
		FWorldDelegates::OnWorldCleanup.RemoveAll(this);
	}

	void OnWorldCleanup(UWorld* InWorld, bool bSessionEnded, bool bCleanupResources)
	{
		WorldLocalStorages::RemoveLocalValue<ImSlate::FWorldContextRoot>(InWorld);
#if WITH_EDITOR && 0
		for (auto& Item : ImSlate::GetSlateRoots())
		{
			if (Item->RawWorldPtr == InWorld)
			{
				// delete Ctx;
				break;
			}
		}
#endif
	}

	void OnGameInstanceStart(UWorld* World)
	{
#if 0
#if WITH_EDITOR
		if (!ensure(World) || !World->IsGameWorld())
			return;
#else
		if (!World)
			return;
#endif
		OnViewportCreated(World, [World] { OnWorldViewport(World));
		if (auto Viewport = World->GetGameViewport())
		{
#if WITH_EDITOR
			CallOnWorldNextTick(World, [World] { OnWorldViewport(World); });
#else
			OnWorldViewport(World);
#endif
		}
		else
		{
			UGameViewportClient::OnViewportCreated().AddWeakLambda(World, [World] { OnWorldViewport(World); });
		}
#endif
	}
};

namespace ImSlate
{
UWorld* GetWorldChecked(const UObject* InCtx)
{
	auto World = GEngine->GetWorldFromContextObject(InCtx, EGetWorldErrorMode::ReturnNull);
	World = World ? World : GWorld;
#if WITH_EDITOR
	if (GIsEditor && (GIsPlayInEditorWorld || (GEditor && GEditor->PlayWorld != nullptr)))
	{
		static auto FindFirstPIEWorld = [] {
			UWorld* World = nullptr;
			auto& WorldContexts = GEngine->GetWorldContexts();
			for (const FWorldContext& Context : WorldContexts)
			{
				auto CurWorld = Context.World();
				if (IsValid(CurWorld) && (CurWorld->WorldType == EWorldType::PIE /* || CurWorld->WorldType == EWorldType::Game*/))
				{
					World = CurWorld;
					break;
				}
			}

			ensure(World);
			return World;
		};

		FWorldContext* WorldContext = GEngine->GetWorldContextFromPIEInstance(FMath::Max(0, (int32)GPlayInEditorID));
		if (ensure(WorldContext && (WorldContext->WorldType == EWorldType::PIE /* || WorldContext->WorldType == EWorldType::Game*/)))
		{
			World = WorldContext->World();
		}
		else
		{
			World = FindFirstPIEWorld();
		}
	}
#else
	check(World && World->IsGameWorld());
#endif
	return World;
}
IMSLATE_API UWorld* GetWorldChecked(ImSlateContext* g)
{
	return g->GetWorldChecked();
}

FWorldContextRoot* GetGImSlate(const UObject* InObj)
{
#if WITH_EDITOR
	if (GIsEditor)
	{
		UWorld* World = GEngine->GetWorldFromContextObject(InObj, EGetWorldErrorMode::ReturnNull);
		World = World ? World : GetWorldChecked();
		for (auto& Item : GetSlateRoots())
		{
			if (Item->RawWorldPtr == World)
			{
				return Item;
			}
		}
		return &WorldLocalStorages::GetLocalValue<FWorldContextRoot>(World, World);
	}
	else
#endif
	{
		return static_cast<FWorldContextRoot*>(GetGlobalImSlate());
	}
}
ImSlateContext* GetGImSlate()
{
	return GetGImSlate(nullptr);
}

TSharedPtr<void> ImSlateTicker::BindDelegate(ImSlateTicker::FOnTickWithWorld Delegate, UWorld* InWorld)
{
	auto& g = *GetGImSlate(InWorld);
	auto Elm = g.ImSlateSignals.Connect((UObject*)nullptr, MoveTemp(Delegate));
	return g.ImSlateSignals.BindSignalConnection(Elm);
}
TSharedPtr<void> ImSlateTicker::BindDelegate(ImSlateTicker::FOnTick Delegate, UWorld* InWorld)
{
	auto& g = *GetGImSlate(InWorld);
	auto Elm = g.ImSlateSignals.Connect((UObject*)nullptr, MoveTemp(Delegate));
	return g.ImSlateSignals.BindSignalConnection(Elm);
}

}  // namespace ImSlate

IMPLEMENT_MODULE(FImSlateModule, ImSlate)

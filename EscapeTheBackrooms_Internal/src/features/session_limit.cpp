#include "features/session_limit.hpp"

#include "game/unreal_safety.hpp"

#include <SDK/AdvancedSessions_parameters.hpp>
#include <SDK/CoreUObject_classes.hpp>

#include <algorithm>
#include <cstdint>
#include <string>

namespace etb::features
{
namespace
{
constexpr std::ptrdiff_t kCreateWidgetMaxPlayer = 0x2E8;
constexpr std::ptrdiff_t kCreateWidgetMaximumPlayers = 0x308;
constexpr std::ptrdiff_t kCreateWidgetSlider = 0x2C8;
constexpr std::ptrdiff_t kSliderValue = 0x108;
constexpr std::ptrdiff_t kGameInstanceMaxPlayers = 0x3D4;
constexpr std::ptrdiff_t kGameStateMaxPlayers = 0x310;
constexpr std::ptrdiff_t kLobbyGameStateMaxPlayers = 0x398;
constexpr std::ptrdiff_t kGameSessionMaxPlayers = 0x224;
constexpr std::ptrdiff_t kGameSessionMaxPartySize = 0x228;
constexpr std::ptrdiff_t kAdvancedCreatePublicConnections = 0x20;
constexpr std::ptrdiff_t kAdvancedUpdatePublicConnections = 0x18;
constexpr std::ptrdiff_t kCreatePublicConnections = 0x10;
constexpr std::ptrdiff_t kGetMaxPlayersReturnValue = 0x8;

template <typename T>
bool WriteValue(const void* base, const std::ptrdiff_t offset, const T value)
{
    auto* destination = reinterpret_cast<T*>(reinterpret_cast<std::uintptr_t>(base) + offset);
    if (!game::IsReadable(destination, sizeof(T)))
        return false;

    __try
    {
        *destination = value;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool HasCastFlag(const SDK::UObject* object, const SDK::EClassCastFlags flag)
{
    if (object == nullptr || object->Class == nullptr)
        return false;

    __try
    {
        return static_cast<std::uint64_t>(object->Class->CastFlags & flag) != 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

SDK::UObject* ReadObject(const void* base, const std::ptrdiff_t offset)
{
    auto* source = reinterpret_cast<SDK::UObject* const*>(reinterpret_cast<std::uintptr_t>(base) + offset);
    if (!game::IsReadable(source, sizeof(*source)))
        return nullptr;

    __try
    {
        return *source;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return nullptr;
    }
}

bool CallProcessEvent(const SDK::UObject* object, SDK::UFunction* function, void* parameters)
{
    if (!game::IsReadable(object, sizeof(SDK::UObject)) ||
        !game::IsReadable(function, sizeof(SDK::UFunction)))
        return false;

    __try
    {
        object->ProcessEvent(function, parameters);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool CallNativeProcessEvent(const SDK::UObject* object, SDK::UFunction* function,
                            void* parameters)
{
    if (!game::IsReadable(object, sizeof(SDK::UObject)) ||
        !game::IsReadable(function, sizeof(SDK::UFunction)))
        return false;

    __try
    {
        const auto flags = function->FunctionFlags;
        function->FunctionFlags |= 0x400;
        object->ProcessEvent(function, parameters);
        function->FunctionFlags = flags;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}
}

SessionLimit& SessionLimit::Instance()
{
    static SessionLimit instance;
    return instance;
}

bool SessionLimit::NeedsInspection() const noexcept
{
    return settings_.enabled;
}

bool SessionLimit::NeedsGameThreadTick() const noexcept
{
    return settings_.enabled;
}

int SessionLimit::DesiredPlayers() const noexcept
{
    return std::clamp(settings_.maximumPlayers, 2, 128);
}

void SessionLimit::ResolveStep(const std::size_t budget)
{
    if (resolved_.load(std::memory_order_relaxed))
        return;

    resolving_.store(true, std::memory_order_relaxed);
    const int count = SDK::UObject::GObjects->Num();
    if (count <= 0)
        return;

    const std::size_t end = std::min<std::size_t>(static_cast<std::size_t>(count), resolutionCursor_ + budget);
    for (; resolutionCursor_ < end; ++resolutionCursor_)
    {
        SDK::UObject* object = SDK::UObject::GObjects->GetByIndex(static_cast<SDK::int32>(resolutionCursor_));
        if (object == nullptr)
            continue;

        PatchObject(object);

        const bool isClass = HasCastFlag(object, SDK::EClassCastFlags::Class);
        const bool isFunction = HasCastFlag(object, SDK::EClassCastFlags::Function);
        if (!isClass && !isFunction)
            continue;

        std::string name;
        if (!game::TryFNameToString(object->Name, name))
            continue;

        if (isClass)
        {
            auto* type = static_cast<SDK::UClass*>(object);
            if (name == "W_CreateServer_C")
                createWidgetClass_ = type;
            else if (name == "BP_MyGameInstance_C")
                gameInstanceClass_ = type;
            else if (name == "MP_GameState_C")
                gameStateClass_ = type;
            else if (name == "Lobby_GS_C")
                lobbyGameStateClass_ = type;
            else if (name == "GameSession")
                gameSessionClass_ = type;
            else if (name == "AdvancedGameSession")
                advancedGameSessionClass_ = type;
            else if (name == "AdvancedSessionsLibrary")
                advancedSessionsLibraryClass_ = type;
            else if (name == "UpdateSessionCallbackProxyAdvanced")
                updateSessionProxyClass_ = type;
            continue;
        }

        if (name != "CreateAdvancedSession" && name != "UpdateSession" &&
            name != "CreateSession" && name != "GetMaxPlayersForGameMode" &&
            name != "ChangeMaxPlayerSlider" && name != "SetValue" &&
            name != "GetSessionSettings" && name != "Activate")
            continue;

        SDK::UObject* outer = object->Outer;
        std::string outerName;
        if (outer == nullptr || !game::TryFNameToString(outer->Name, outerName))
            continue;

        auto* function = static_cast<SDK::UFunction*>(object);
        if (name == "CreateAdvancedSession" && outerName == "CreateSessionCallbackProxyAdvanced")
            createAdvancedSessionFunction_ = function;
        else if (name == "UpdateSession" && outerName == "UpdateSessionCallbackProxyAdvanced")
            updateAdvancedSessionFunction_ = function;
        else if (name == "CreateSession" && outerName == "CreateSessionCallbackProxy")
            createSessionFunction_ = function;
        else if (name == "GetMaxPlayersForGameMode" && outerName == "FancySessionUtilsLibrary")
            getMaxPlayersFunction_ = function;
        else if (name == "ChangeMaxPlayerSlider" && outerName == "W_CreateServer_C")
            changeMaxPlayerSliderFunction_ = function;
        else if (name == "SetValue" && outerName == "Slider")
            sliderSetValueFunction_ = function;
        else if (name == "GetSessionSettings" && outerName == "AdvancedSessionsLibrary")
            getSessionSettingsFunction_ = function;
        else if (name == "Activate" && outerName == "BlueprintAsyncActionBase")
            activateAsyncActionFunction_ = function;
    }

    if (resolutionCursor_ >= static_cast<std::size_t>(count))
    {
        resolved_.store(true, std::memory_order_relaxed);
        resolving_.store(false, std::memory_order_relaxed);
        resolutionCursor_ = 0;
    }
}

void SessionLimit::PatchObject(const SDK::UObject* object)
{
    if (object == nullptr || object->Class == nullptr)
        return;

    const int players = DesiredPlayers();
    bool patched = false;
    if (object->Class == createWidgetClass_)
    {
        createWidgetDetected_.store(true, std::memory_order_relaxed);
        patched |= WriteValue(object, kCreateWidgetMaximumPlayers, players);
        patched |= WriteValue(object, kCreateWidgetMaxPlayer, players);
        SDK::UObject* slider = ReadObject(object, kCreateWidgetSlider);
        if (slider != nullptr)
            patched |= WriteValue(slider, kSliderValue, 1.0f);
    }
    else if (object->Class == gameInstanceClass_)
    {
        if (gameInstanceObject_ != object || gameInstanceObjectIndex_ != object->Index)
        {
            gameInstanceObject_ = const_cast<SDK::UObject*>(object);
            gameInstanceObjectIndex_ = object->Index;
            currentSessionUpdateRequested_.store(false, std::memory_order_relaxed);
            nextCurrentSessionUpdateMs_ = 0;
        }
        patched |= WriteValue(object, kGameInstanceMaxPlayers, players);
    }
    else if (object->Class == gameStateClass_)
    {
        patched |= WriteValue(object, kGameStateMaxPlayers, players);
    }
    else if (object->Class == lobbyGameStateClass_)
    {
        patched |= WriteValue(object, kLobbyGameStateMaxPlayers, players);
    }
    else if (object->Class == gameSessionClass_ || object->Class == advancedGameSessionClass_)
    {
        if (hostGameSessionObject_ != object || hostGameSessionObjectIndex_ != object->Index)
        {
            hostGameSessionObject_ = const_cast<SDK::UObject*>(object);
            hostGameSessionObjectIndex_ = object->Index;
            currentSessionDetected_.store(false, std::memory_order_relaxed);
            currentSessionUpdateRequested_.store(false, std::memory_order_relaxed);
            nextCurrentSessionUpdateMs_ = 0;
        }
        patched |= WriteValue(object, kGameSessionMaxPlayers, players);
        patched |= WriteValue(object, kGameSessionMaxPartySize, players);
    }

    if (patched)
        patchedRuntimeObjects_.fetch_add(1, std::memory_order_relaxed);
}

void SessionLimit::TryUpdateCurrentSession()
{
    const int players = DesiredPlayers();
    if (currentSessionUpdatePlayers_ != players)
    {
        currentSessionUpdatePlayers_ = players;
        currentSessionUpdateRequested_.store(false, std::memory_order_relaxed);
        nextCurrentSessionUpdateMs_ = 0;
    }
    if (currentSessionUpdateInProgress_ ||
        currentSessionUpdateRequested_.load(std::memory_order_relaxed))
        return;

    const std::uint64_t now = GetTickCount64();
    if (now < nextCurrentSessionUpdateMs_)
        return;
    nextCurrentSessionUpdateMs_ = now + 2000;

    if (!game::IsLiveUObject(gameInstanceObject_) ||
        gameInstanceObject_->Index != gameInstanceObjectIndex_ ||
        !game::IsLiveUObject(hostGameSessionObject_) ||
        hostGameSessionObject_->Index != hostGameSessionObjectIndex_ ||
        !game::IsLiveUObject(advancedSessionsLibraryClass_) ||
        !game::IsLiveUObject(updateSessionProxyClass_) ||
        !game::IsLiveUObject(getSessionSettingsFunction_) ||
        !game::IsLiveUObject(updateAdvancedSessionFunction_) ||
        !game::IsLiveUObject(activateAsyncActionFunction_))
        return;

    SDK::UObject* library = advancedSessionsLibraryClass_->ClassDefaultObject;
    SDK::UObject* updater = updateSessionProxyClass_->ClassDefaultObject;
    if (!game::IsLiveUObject(library) || !game::IsLiveUObject(updater))
        return;

    SDK::Params::AdvancedSessionsLibrary_GetSessionSettings current{};
    current.WorldContextObject = gameInstanceObject_;
    current.Result = SDK::EBlueprintResultSwitch::OnFailure;
    currentSessionUpdateInProgress_ = true;
    const bool queried = CallNativeProcessEvent(library, getSessionSettingsFunction_, &current);
    if (!queried || current.Result != SDK::EBlueprintResultSwitch::OnSuccess)
    {
        currentSessionUpdateInProgress_ = false;
        return;
    }

    currentSessionDetected_.store(true, std::memory_order_relaxed);
    currentSessionConnections_.store(current.NumConnections, std::memory_order_relaxed);
    if (current.NumConnections == players)
    {
        advertisedConnections_.store(players, std::memory_order_relaxed);
        currentSessionUpdateRequested_.store(true, std::memory_order_relaxed);
        currentSessionUpdateInProgress_ = false;
        return;
    }

    SDK::Params::UpdateSessionCallbackProxyAdvanced_UpdateSession update{};
    update.WorldContextObject = gameInstanceObject_;
    update.ExtraSettings = current.ExtraSettings;
    update.PublicConnections = players;
    update.PrivateConnections = current.NumPrivateConnections;
    update.bUseLAN = current.bIsLAN;
    update.bAllowInvites = current.bAllowInvites;
    update.bAllowJoinInProgress = current.bAllowJoinInProgress;
    update.bRefreshOnlineData = true;
    update.bIsDedicatedServer = current.bIsDedicated;
    update.bIsGameSession = true;
    const bool submitted = CallNativeProcessEvent(updater, updateAdvancedSessionFunction_, &update);
    auto* proxy = reinterpret_cast<SDK::UObject*>(update.ReturnValue);
    if (submitted && game::IsLiveUObject(proxy) &&
        CallNativeProcessEvent(proxy, activateAsyncActionFunction_, nullptr))
    {
        advertisedConnections_.store(players, std::memory_order_relaxed);
        currentSessionUpdateRequested_.store(true, std::memory_order_relaxed);
    }
    currentSessionUpdateInProgress_ = false;
}

void SessionLimit::OnBeforeProcessEvent(const SDK::UObject* object, SDK::UFunction* function, void* parameters)
{
    if (!settings_.enabled)
        return;

    OnGameThreadTick();
    PatchObject(object);
    if (parameters == nullptr)
        return;

    if (function == changeMaxPlayerSliderFunction_)
        WriteValue(parameters, 0, 1.0f);

    const int players = DesiredPlayers();
    bool patched = false;
    if (function == createAdvancedSessionFunction_)
        patched = WriteValue(parameters, kAdvancedCreatePublicConnections, players);
    else if (function == updateAdvancedSessionFunction_)
        patched = WriteValue(parameters, kAdvancedUpdatePublicConnections, players);
    else if (function == createSessionFunction_)
        patched = WriteValue(parameters, kCreatePublicConnections, players);

    if (patched)
    {
        advertisedConnections_.store(players, std::memory_order_relaxed);
        patchedSessionCalls_.fetch_add(1, std::memory_order_relaxed);
    }
}

void SessionLimit::OnAfterProcessEvent(const SDK::UObject* object, SDK::UFunction* function, void* parameters)
{
    if (!settings_.enabled)
        return;

    if (function == getMaxPlayersFunction_ && parameters != nullptr)
        WriteValue(parameters, kGetMaxPlayersReturnValue, DesiredPlayers());
    PatchObject(object);

    const int players = DesiredPlayers();
    if (object != nullptr && object->Class == createWidgetClass_ &&
        function != changeMaxPlayerSliderFunction_ && changeMaxPlayerSliderFunction_ != nullptr &&
        (lastCreateWidget_ != object || lastCreateWidgetPlayers_ != players))
    {
        lastCreateWidget_ = object;
        lastCreateWidgetPlayers_ = players;
        SDK::UObject* slider = ReadObject(object, kCreateWidgetSlider);
        float sliderValue = 1.0f;
        if (slider != nullptr && sliderSetValueFunction_ != nullptr)
            CallProcessEvent(slider, sliderSetValueFunction_, &sliderValue);
        uiRefreshApplied_.store(
            CallProcessEvent(object, changeMaxPlayerSliderFunction_, &sliderValue),
            std::memory_order_relaxed);
    }
}

void SessionLimit::OnGameThreadTick()
{
    if (!settings_.enabled)
        return;

    const std::uint64_t now = GetTickCount64();
    std::uint64_t previous = lastTickMilliseconds_.load(std::memory_order_relaxed);
    if (now - previous < 16 ||
        !lastTickMilliseconds_.compare_exchange_strong(previous, now, std::memory_order_relaxed))
        return;

    ResolveStep(3000);
    if (!resolved_.load(std::memory_order_relaxed))
    {
        TryUpdateCurrentSession();
        return;
    }

    const int count = SDK::UObject::GObjects->Num();
    if (count <= 0)
        return;

    if (now >= nextRuntimeScanMs_)
    {
        constexpr std::size_t budget = 256;
        const std::size_t end = std::min<std::size_t>(static_cast<std::size_t>(count),
                                                      runtimeCursor_ + budget);
        for (; runtimeCursor_ < end; ++runtimeCursor_)
        {
            SDK::UObject* object = SDK::UObject::GObjects->GetByIndex(
                static_cast<SDK::int32>(runtimeCursor_));
            if (object != nullptr)
                PatchObject(object);
        }
        if (runtimeCursor_ >= static_cast<std::size_t>(count))
        {
            runtimeCursor_ = 0;
            nextRuntimeScanMs_ = now + 5000;
        }
    }
    TryUpdateCurrentSession();
}

void SessionLimit::Reset()
{
    resolutionCursor_ = 0;
    runtimeCursor_ = 0;
    resolving_.store(false, std::memory_order_relaxed);
    resolved_.store(false, std::memory_order_relaxed);
    createWidgetDetected_.store(false, std::memory_order_relaxed);
    uiRefreshApplied_.store(false, std::memory_order_relaxed);
    currentSessionDetected_.store(false, std::memory_order_relaxed);
    currentSessionUpdateRequested_.store(false, std::memory_order_relaxed);
    currentSessionConnections_.store(0, std::memory_order_relaxed);
    advertisedConnections_.store(0, std::memory_order_relaxed);
    patchedSessionCalls_.store(0, std::memory_order_relaxed);
    patchedRuntimeObjects_.store(0, std::memory_order_relaxed);
    createWidgetClass_ = nullptr;
    gameInstanceClass_ = nullptr;
    gameStateClass_ = nullptr;
    lobbyGameStateClass_ = nullptr;
    gameSessionClass_ = nullptr;
    advancedGameSessionClass_ = nullptr;
    advancedSessionsLibraryClass_ = nullptr;
    updateSessionProxyClass_ = nullptr;
    createAdvancedSessionFunction_ = nullptr;
    updateAdvancedSessionFunction_ = nullptr;
    createSessionFunction_ = nullptr;
    getMaxPlayersFunction_ = nullptr;
    changeMaxPlayerSliderFunction_ = nullptr;
    sliderSetValueFunction_ = nullptr;
    getSessionSettingsFunction_ = nullptr;
    activateAsyncActionFunction_ = nullptr;
    gameInstanceObject_ = nullptr;
    gameInstanceObjectIndex_ = -1;
    hostGameSessionObject_ = nullptr;
    hostGameSessionObjectIndex_ = -1;
    lastCreateWidget_ = nullptr;
    lastCreateWidgetPlayers_ = 0;
    lastTickMilliseconds_.store(0, std::memory_order_relaxed);
    nextCurrentSessionUpdateMs_ = 0;
    nextRuntimeScanMs_ = 0;
    currentSessionUpdatePlayers_ = 0;
    currentSessionUpdateInProgress_ = false;
}

SessionLimitStatus SessionLimit::Status() const noexcept
{
    SessionLimitStatus status{};
    status.resolving = resolving_.load(std::memory_order_relaxed);
    status.resolved = resolved_.load(std::memory_order_relaxed);
    status.createWidgetDetected = createWidgetDetected_.load(std::memory_order_relaxed);
    status.uiFunctionsReady = changeMaxPlayerSliderFunction_ != nullptr;
    status.uiRefreshApplied = uiRefreshApplied_.load(std::memory_order_relaxed);
    status.currentSessionDetected = currentSessionDetected_.load(std::memory_order_relaxed);
    status.currentSessionUpdateRequested =
        currentSessionUpdateRequested_.load(std::memory_order_relaxed);
    status.currentSessionConnections = currentSessionConnections_.load(std::memory_order_relaxed);
    status.advertisedConnections = advertisedConnections_.load(std::memory_order_relaxed);
    status.patchedSessionCalls = patchedSessionCalls_.load(std::memory_order_relaxed);
    status.patchedRuntimeObjects = patchedRuntimeObjects_.load(std::memory_order_relaxed);
    return status;
}
}

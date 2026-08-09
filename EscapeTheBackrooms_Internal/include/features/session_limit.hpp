#pragma once

#include "core/config.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace SDK
{
class UClass;
class UFunction;
class UObject;
}

namespace etb::features
{
struct SessionLimitSettings
{
    CFG_VAR("session_player_limit_enabled", bool, enabled, false, false, "Player Limit", "", "Status");
    CFG_VAR("session_max_players", int, maximumPlayers, 16, true, "Player Limit", "Maximum Players", "Status");
};

struct SessionLimitStatus
{
    bool resolving = false;
    bool resolved = false;
    bool createWidgetDetected = false;
    bool uiFunctionsReady = false;
    bool uiRefreshApplied = false;
    bool currentSessionDetected = false;
    bool currentSessionUpdateRequested = false;
    int currentSessionConnections = 0;
    int advertisedConnections = 0;
    std::size_t patchedSessionCalls = 0;
    std::size_t patchedRuntimeObjects = 0;
};

class SessionLimit final
{
public:
    static SessionLimit& Instance();

    bool NeedsInspection() const noexcept;
    bool NeedsGameThreadTick() const noexcept;
    void OnBeforeProcessEvent(const SDK::UObject* object, SDK::UFunction* function, void* parameters);
    void OnAfterProcessEvent(const SDK::UObject* object, SDK::UFunction* function, void* parameters);
    void OnGameThreadTick();
    void Reset();

    SessionLimitSettings& Settings() noexcept { return settings_; }
    const SessionLimitSettings& Settings() const noexcept { return settings_; }
    SessionLimitStatus Status() const noexcept;

private:
    SessionLimit() = default;

    void ResolveStep(std::size_t budget);
    void PatchObject(const SDK::UObject* object);
    void TryUpdateCurrentSession();
    int DesiredPlayers() const noexcept;

    SessionLimitSettings settings_{};
    SDK::UClass* createWidgetClass_ = nullptr;
    SDK::UClass* gameInstanceClass_ = nullptr;
    SDK::UClass* gameStateClass_ = nullptr;
    SDK::UClass* lobbyGameStateClass_ = nullptr;
    SDK::UClass* gameSessionClass_ = nullptr;
    SDK::UClass* advancedGameSessionClass_ = nullptr;
    SDK::UClass* advancedSessionsLibraryClass_ = nullptr;
    SDK::UClass* updateSessionProxyClass_ = nullptr;
    SDK::UFunction* createAdvancedSessionFunction_ = nullptr;
    SDK::UFunction* updateAdvancedSessionFunction_ = nullptr;
    SDK::UFunction* createSessionFunction_ = nullptr;
    SDK::UFunction* getMaxPlayersFunction_ = nullptr;
    SDK::UFunction* changeMaxPlayerSliderFunction_ = nullptr;
    SDK::UFunction* sliderSetValueFunction_ = nullptr;
    SDK::UFunction* getSessionSettingsFunction_ = nullptr;
    SDK::UFunction* activateAsyncActionFunction_ = nullptr;
    SDK::UObject* gameInstanceObject_ = nullptr;
    std::int32_t gameInstanceObjectIndex_ = -1;
    SDK::UObject* hostGameSessionObject_ = nullptr;
    std::int32_t hostGameSessionObjectIndex_ = -1;
    const SDK::UObject* lastCreateWidget_ = nullptr;
    int lastCreateWidgetPlayers_ = 0;
    std::size_t resolutionCursor_ = 0;
    std::size_t runtimeCursor_ = 0;
    std::atomic_uint64_t lastTickMilliseconds_{0};
    std::atomic_bool resolving_{false};
    std::atomic_bool resolved_{false};
    std::atomic_bool createWidgetDetected_{false};
    std::atomic_bool uiRefreshApplied_{false};
    std::atomic_bool currentSessionDetected_{false};
    std::atomic_bool currentSessionUpdateRequested_{false};
    std::atomic_int currentSessionConnections_{0};
    std::atomic_int advertisedConnections_{0};
    std::atomic_size_t patchedSessionCalls_{0};
    std::atomic_size_t patchedRuntimeObjects_{0};
    std::uint64_t nextCurrentSessionUpdateMs_ = 0;
    std::uint64_t nextRuntimeScanMs_ = 0;
    int currentSessionUpdatePlayers_ = 0;
    bool currentSessionUpdateInProgress_ = false;
};
}

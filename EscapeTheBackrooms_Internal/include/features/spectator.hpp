#pragma once

#include "core/config.hpp"

#include <Windows.h>

#include <atomic>
#include <cstddef>
#include <string>

namespace SDK
{
class UObject;
}

namespace etb::features
{
class SpectatorRuntime;

struct SpectatorSettings
{
    // Spectate Teammates (观战队友) - F2
    CFG_VAR("spectate_enabled", bool, spectateEnabled, false, false, "Spectate Teammates", "", "Spectator");
    CFG_VAR("spectate_toggle_key", int, spectateToggleKey, VK_F2, true, "Spectate Teammates", "Toggle Key", "Spectator");
    CFG_VAR("spectate_next_key", int, spectateNextKey, VK_RIGHT, true, "Spectate Teammates", "Next Player Key", "Spectator");
    CFG_VAR("spectate_prev_key", int, spectatePrevKey, VK_LEFT, true, "Spectate Teammates", "Prev Player Key", "Spectator");

    // Free Camera (自由视角) - F3
    CFG_VAR("freecam_enabled", bool, freeCamEnabled, false, false, "Free Camera", "", "Spectator");
    CFG_VAR("freecam_toggle_key", int, freeCamToggleKey, VK_F3, true, "Free Camera", "Toggle Key", "Spectator");
    CFG_VAR("freecam_speed", float, freeCamSpeed, 1500.0f, true, "Free Camera", "Speed", "Spectator");
    CFG_VAR("freecam_noclip", bool, freeCamNoClip, true, false, "Free Camera", "No Clip", "Spectator");
};

struct SpectatorStatus
{
    bool gameThreadHookReady = false;
    bool controllerReady = false;
    bool spectatorPawnReady = false;
    bool isSpectating = false;
    bool isFreeCam = false;
    bool authority = false;
    std::size_t playerCount = 0;
    int currentSpectateIndex = -1;
    std::string currentPlayerName;
};

class Spectator final
{
public:
    static Spectator& Instance();

    void Update(bool inputBlocked = false);
    void OnGameThreadTick(const SDK::UObject* tickObject, float deltaSeconds);
    void SetGameThreadHookReady(bool ready) noexcept;
    bool NeedsGameThreadTick() const noexcept;
    void Reset();

    bool EnterSpectator();
    bool ExitSpectator();
    bool ToggleSpectator();
    bool SpectateNext();
    bool SpectatePrevious();
    bool SpectateIndex(int index);
    bool EnterFreeCam();
    bool ExitFreeCam();

    SpectatorSettings& Settings() noexcept { return settings_; }
    const SpectatorSettings& Settings() const noexcept { return settings_; }
    SpectatorStatus Status() const noexcept;

private:
    friend class SpectatorRuntime;
    Spectator() = default;

    SpectatorSettings settings_{};
    bool spectateToggleKeyDown_ = false;
    bool freeCamToggleKeyDown_ = false;
    bool nextKeyDown_ = false;
    bool prevKeyDown_ = false;

    std::atomic_bool spectateDesired_{false};
    std::atomic_bool freeCamDesired_{false};
    std::atomic_bool configuredNoClip_{true};
    std::atomic<float> configuredSpeed_{1500.0f};
    std::atomic_bool gameplayInputBlocked_{true};
    std::atomic_bool runtimeActive_{false};
    std::atomic_bool cleanupPending_{false};
    std::atomic_bool gameThreadHookReady_{false};
    std::atomic<const SDK::UObject*> gameThreadTickObject_{nullptr};
    std::atomic_int cycleRequest_{0};
    std::atomic_int indexRequest_{-1};
};
}

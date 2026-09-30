#pragma once

#include "core/config.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace SDK
{
class UObject;
class UWorld;
}

namespace etb::features
{
struct ExitActivatorSettings
{
    CFG_VAR("exit_activator_enabled", bool, enabled, false, false, "Exit Control", "", "Classes");
    CFG_VAR("exit_trigger_collision", bool, forceTriggerCollision, true, true, "Exit Control", "Trigger Collision", "Classes");
    CFG_VAR("exit_scan_budget", int, scanActorsPerTick, 300, true, "Exit Control", "Scan Budget", "Classes");
    CFG_VAR("host_exit_teleport_min_distance", float, exitTeleportMinDistance, 0.0f, true, "Exit Teleport", "Minimum Distance", "Status");
    CFG_VAR("host_member_attributes", bool, maintainMemberAttributes, false, false, "Member Attributes", "", "Status");
    CFG_VAR("host_member_walk_speed", float, memberWalkSpeed, 300.0f, true, "Member Attributes", "Walk", "Status");
    CFG_VAR("host_member_sprint_speed", float, memberSprintSpeed, 600.0f, true, "Member Attributes", "Sprint", "Status");
    CFG_VAR("host_member_crouch_speed", float, memberCrouchSpeed, 150.0f, true, "Member Attributes", "Crouch", "Status");
    CFG_VAR("host_member_max_stamina", float, memberMaxStamina, 100.0f, true, "Member Attributes", "Stamina", "Status");
    CFG_VAR("host_member_infinite_stamina", bool, memberInfiniteStamina, true, true, "Member Attributes", "Infinite Stamina", "Status");
};

struct ActivatedExitMarker
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    std::string name;
};

struct ExitActivatorStatus
{
    bool gameThreadHookReady = false;
    bool hostAuthority = false;
    bool scanning = false;
    bool completed = false;
    std::size_t candidates = 0;
    std::size_t activated = 0;
    std::size_t lastTeleportedPlayers = 0;
    std::size_t lastModifiedMembers = 0;
    bool clownManagerDetected = false;
    bool rollercoasterManagerDetected = false;
    int rollercoasterTime = -1;
};

class ExitActivator final
{
public:
    static ExitActivator& Instance();

    void Update();
    void OnGameThreadTick(const SDK::UObject* tickObject);
    void SetGameThreadHookReady(bool ready) noexcept;
    bool NeedsGameThreadTick() const noexcept;
    void DrainNotifications(std::vector<std::string>& destination);
    void SnapshotMarkers(std::vector<ActivatedExitMarker>& destination);
    void RequestTeleportAllPlayers() noexcept;
    void RequestTeleportToExit() noexcept;
    void RequestApplyMemberAttributes() noexcept;
    void RequestStartClownChallenge() noexcept;
    void RequestCompleteClownChallenge() noexcept;
    void RequestStartRollercoaster() noexcept;
    void Reset();

    ExitActivatorSettings& Settings() noexcept { return settings_; }
    const ExitActivatorSettings& Settings() const noexcept { return settings_; }
    ExitActivatorStatus Status() const noexcept;

private:
    ExitActivator() = default;

    ExitActivatorSettings settings_{};
    std::atomic_bool runActive_{false};
    std::atomic_bool restartRequested_{false};
    std::atomic_bool forceTriggerCollision_{true};
    std::atomic<int> scanActorsPerTick_{300};
    std::atomic_bool hookReady_{false};
    std::atomic_bool hostAuthority_{false};
    std::atomic_bool scanning_{false};
    std::atomic_bool completed_{false};
    std::atomic_size_t candidates_{0};
    std::atomic_size_t activated_{0};
    std::atomic_size_t lastTeleportedPlayers_{0};
    std::atomic_size_t lastModifiedMembers_{0};
    std::atomic_bool teleportAllRequested_{false};
    std::atomic_bool teleportToExitRequested_{false};
    std::atomic<float> exitTeleportMinDistance_{0.0f};
    std::atomic_bool applyMemberAttributesRequested_{false};
    std::atomic_bool maintainMemberAttributes_{false};
    std::atomic<float> memberWalkSpeed_{300.0f};
    std::atomic<float> memberSprintSpeed_{600.0f};
    std::atomic<float> memberCrouchSpeed_{150.0f};
    std::atomic<float> memberMaxStamina_{100.0f};
    std::atomic_bool memberInfiniteStamina_{true};
    std::atomic<std::uint64_t> nextMemberAttributesApplyTick_{0};
    std::atomic_bool startClownRequested_{false};
    std::atomic_bool completeClownRequested_{false};
    std::atomic_bool startRollercoasterRequested_{false};
    std::atomic_bool clownManagerDetected_{false};
    std::atomic_bool rollercoasterManagerDetected_{false};
    std::atomic_int rollercoasterTime_{-1};
    std::mutex notificationMutex_{};
    std::vector<std::string> notificationQueue_{};
    std::vector<ActivatedExitMarker> markers_{};
    bool previousUiEnabled_ = false;
};
}

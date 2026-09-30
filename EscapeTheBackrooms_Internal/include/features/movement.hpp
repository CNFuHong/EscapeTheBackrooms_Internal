#pragma once

#include "core/config.hpp"

#include <Windows.h>

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace SDK
{
class UObject;
class UFunction;
}

namespace etb::features
{
struct MovementSettings
{
    CFG_VAR("movement_infinite_stamina", bool, infiniteStamina, true, false, "Infinite Stamina", "", "Movement");
    CFG_VAR("movement_speed_enabled", bool, speedEnabled, false, false, "Speed", "", "Movement");
    CFG_VAR("movement_walk_speed", float, walkSpeed, 300.0f, true, "Speed", "Walk Speed", "Movement");
    CFG_VAR("movement_sprint_speed", float, sprintSpeed, 600.0f, true, "Speed", "Sprint Speed", "Movement");
    CFG_VAR("movement_auto_sprint", bool, autoSprint, false, false, "Auto Sprint", "", "Movement");
    CFG_VAR("movement_quick_stop", bool, quickStop, false, false, "Quick Stop", "", "Movement");
    CFG_VAR("movement_bhop", bool, bunnyHop, false, false, "BHop", "", "Movement");
    CFG_VAR("movement_bhop_auto_jump", bool, bhopAutoJump, false, false, "BHop", "Auto Jump", "Movement");
    CFG_VAR("movement_bhop_auto_jump_speed", float, bhopAutoJumpSpeed, 0.5f, true, "BHop", "Auto Jump Speed", "Movement");
    CFG_VAR("movement_crouch_enabled", bool, crouchSpeedEnabled, false, false, "Crouch Speed", "", "Movement");
    CFG_VAR("movement_crouch_speed", float, crouchSpeed, 150.0f, true, "Crouch Speed", "Speed", "Movement");
    CFG_VAR("movement_jump_enabled", bool, jumpEnabled, false, false, "Jump", "", "Movement");
    CFG_VAR("movement_jump_velocity", float, jumpVelocity, 420.0f, true, "Jump", "Velocity", "Movement");
    CFG_VAR("movement_gravity_enabled", bool, gravityEnabled, false, false, "Gravity", "", "Movement");
    CFG_VAR("movement_gravity_scale", float, gravityScale, 1.0f, true, "Gravity", "Scale", "Movement");
    CFG_VAR("movement_flight_enabled", bool, flightEnabled, false, false, "Flight", "", "Movement");
    CFG_VAR("movement_fly_speed", float, flySpeed, 1200.0f, true, "Flight", "Speed", "Movement");
    CFG_VAR("movement_noclip_enabled", bool, noClipEnabled, false, false, "No Clip", "", "Movement");
    CFG_VAR("movement_flight_key", int, flightToggleKey, 'C', true, "Flight", "Key", "Movement");
    CFG_VAR("movement_noclip_key", int, noClipToggleKey, 'C', true, "No Clip", "Key", "Movement");
    CFG_VAR("movement_air_turn_enabled", bool, airTurnEnabled, false, false, "Air Turn", "", "Movement");
    CFG_VAR("movement_air_turn_control", float, airTurnControl, 1.0f, true, "Air Turn", "Air Control", "Movement");
    CFG_VAR("movement_air_turn_accel", float, airTurnAcceleration, 6000.0f, true, "Air Turn", "Acceleration", "Movement");
    CFG_VAR("movement_host_accept_client", bool, hostAcceptClientMovement, false, true, "Movement", "Host Accept Client", "Movement");
    CFG_VAR("movement_server_move_bypass", bool, serverMoveBypass, false, true, "Movement", "Server Move Bypass", "Movement");
};

struct MovementStatus
{
    bool pawnAttached = false;
    bool movementComponentReady = false;
    bool hostAuthority = false;
    std::size_t hostAuthorizedPawns = 0;
    bool telemetryReady = false;
    bool positionReady = false;
    float positionX = 0.0f;
    float positionY = 0.0f;
    float positionZ = 0.0f;
    float velocityX = 0.0f;
    float velocityY = 0.0f;
    float velocityZ = 0.0f;
    float horizontalSpeed = 0.0f;
    float totalSpeed = 0.0f;
    int movementMode = 0;
};

class Movement final
{
public:
    static Movement& Instance();

    void Update(bool inputBlocked = false);
    void OnGameThreadTick(const SDK::UObject* tickObject);
    void SetGameThreadHookReady(bool ready) noexcept;
    bool NeedsGameThreadTick() const noexcept;

    void SuspendHostAuthorization(std::uint64_t milliseconds) noexcept;

    bool ServerMoveBypassEnabled() const noexcept;
    bool IsLocalMovementObject(const SDK::UObject* object) const noexcept;
    static bool IsMovementCorrectionRpc(SDK::UFunction* function);

    void Reset();

    MovementSettings& Settings() noexcept { return settings_; }
    const MovementSettings& Settings() const noexcept { return settings_; }
    const MovementStatus& Status() const noexcept { return status_; }

private:
    Movement() = default;

    MovementSettings settings_{};
    MovementStatus status_{};
    std::atomic_bool bunnyHopRequested_{false};
    std::atomic_bool autoJumpRequested_{false};
    std::atomic_bool bunnyHopMovementBlocked_{false};
    std::atomic_bool gameplayInputBlocked_{true};
    std::atomic_bool gameThreadHookReady_{false};
    std::atomic<const SDK::UObject*> gameThreadTickObject_{nullptr};
    std::atomic_bool serverMoveBypass_{false};
    std::atomic<const SDK::UObject*> localMovementObject_{nullptr};
};
}

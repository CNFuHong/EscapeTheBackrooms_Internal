#pragma once

#include "core/config.hpp"

#include <Windows.h>

#include <cstddef>

namespace etb::features
{
struct MovementSettings
{
    CFG_VAR("movement_infinite_stamina", bool, infiniteStamina, true, false, "Infinite Stamina", "", "Movement");
    CFG_VAR("movement_speed_enabled", bool, speedEnabled, false, false, "Speed", "", "Movement");
    CFG_VAR("movement_walk_speed", float, walkSpeed, 300.0f, true, "Speed", "Walk Speed", "Movement");
    CFG_VAR("movement_sprint_speed", float, sprintSpeed, 600.0f, true, "Speed", "Sprint Speed", "Movement");
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
    CFG_VAR("movement_host_accept_client", bool, hostAcceptClientMovement, false, true, "Movement", "Host Accept Client", "Movement");
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
    void Reset();

    MovementSettings& Settings() noexcept { return settings_; }
    const MovementSettings& Settings() const noexcept { return settings_; }
    const MovementStatus& Status() const noexcept { return status_; }

private:
    Movement() = default;

    MovementSettings settings_{};
    MovementStatus status_{};
};
}

#pragma once

#include "core/config.hpp"

#include <Windows.h>

#include <atomic>
#include <cstdint>

namespace SDK
{
class UObject;
}

namespace etb::features
{
class VehicleRuntime;

struct VehicleFlightSettings
{
    CFG_VAR("vehicle_flight", bool, enabled, false, false, "Vehicle Flight", "", "Movement");
    CFG_VAR("vehicle_noclip", bool, noClip, false, false, "Vehicle No Clip", "", "Movement");
    CFG_VAR("vehicle_horizontal_speed", float, horizontalSpeed, 1200.0f, true, "Vehicle Flight", "Horizontal Speed", "Movement");
    CFG_VAR("vehicle_vertical_speed", float, verticalSpeed, 900.0f, true, "Vehicle Flight", "Vertical Speed", "Movement");
    CFG_VAR("vehicle_turn_speed", float, turnSpeedDegrees, 90.0f, true, "Vehicle Flight", "Turn Speed", "Movement");
    CFG_VAR("vehicle_boost", float, boostMultiplier, 2.0f, true, "Vehicle Flight", "Boost", "Movement");
    CFG_VAR("vehicle_network_rate", float, networkRateHz, 30.0f, true, "Vehicle Flight", "Network Rate", "Movement");
    CFG_VAR("vehicle_toggle_key", int, toggleKey, 'V', true, "Vehicle Flight", "Key", "Movement");
    CFG_VAR("vehicle_noclip_key", int, noClipToggleKey, 'V', true, "Vehicle No Clip", "Key", "Movement");
};

struct VehicleFlightStatus
{
    bool gameThreadHookReady = false;
    bool vehicleDetected = false;
    bool active = false;
    bool authority = false;
    bool networkSync = false;
    float speedMetersPerSecond = 0.0f;
};

class VehicleFlight final
{
public:
    static VehicleFlight& Instance();

    void UpdateInput(bool inputBlocked);
    void OnGameThreadTick(const SDK::UObject* tickObject, float deltaSeconds);

    void RequestRestore() noexcept;
    void Reset() noexcept;
    void SetGameThreadHookReady(bool ready) noexcept;
    bool NeedsGameThreadTick() const noexcept;
    bool HasActiveVehicle() const noexcept;

    VehicleFlightSettings& Settings() noexcept { return settings_; }
    const VehicleFlightSettings& Settings() const noexcept { return settings_; }
    VehicleFlightStatus Status() const noexcept;

private:
    friend class VehicleRuntime;
    VehicleFlight() = default;

    VehicleFlightSettings settings_{};
    bool toggleKeyDown_ = false;
    bool noClipKeyDown_ = false;

    std::atomic_bool configuredEnabled_{false};
    std::atomic_bool configuredNoClip_{false};
    std::atomic<float> configuredHorizontalSpeed_{1200.0f};
    std::atomic<float> configuredVerticalSpeed_{900.0f};
    std::atomic<float> configuredTurnSpeed_{90.0f};
    std::atomic<float> configuredBoostMultiplier_{2.0f};
    std::atomic<float> configuredNetworkRateHz_{30.0f};
    std::atomic_uint32_t inputMask_{0};

    std::atomic_bool hookReady_{false};
    std::atomic_bool vehicleDetected_{false};
    std::atomic_bool runtimeActive_{false};
    std::atomic_bool authority_{false};
    std::atomic_bool networkSync_{false};
    std::atomic<float> speedMetersPerSecond_{0.0f};
};
}

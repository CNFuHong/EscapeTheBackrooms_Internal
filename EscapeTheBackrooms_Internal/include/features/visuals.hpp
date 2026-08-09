#pragma once

#include "core/config.hpp"

#include <Windows.h>
#include <atomic>

namespace SDK
{
class UObject;
class UFunction;
}

namespace etb::features
{
struct VisualSettings
{
    CFG_VAR("visual_night_vision", bool, nightVisionEnabled, true, false, "Environment Brightness", "", "Render");
    CFG_VAR("visual_night_strength", float, nightVisionStrength, 1.0f, true, "Environment Brightness", "Strength", "Render");
    CFG_VAR("visual_exposure_boost", bool, nightVisionExposureBoost, false, true, "Environment Brightness", "Exposure Boost", "Render");
    CFG_VAR("visual_exposure_bias", float, nightVisionExposureBias, 3.0f, true, "Environment Brightness", "Exposure Bias", "Render");
    CFG_VAR("visual_remove_fog", bool, nightVisionRemoveFog, false, true, "Environment Brightness", "Remove Fog", "Render");
    CFG_VAR("visual_persistent_light", bool, nightVisionPersistentLight, true, true, "Environment Brightness", "Persistent Light", "Render");
    CFG_VAR("visual_extend_flashlight", bool, nightVisionExtendFlashlight, false, true, "Environment Brightness", "Extend Flashlight", "Render");
    CFG_VAR("visual_light_range", float, nightVisionLightRangeMeters, 150.0f, true, "Environment Brightness", "Light Range", "Render");
    CFG_VAR("visual_light_intensity_v2", float, nightVisionLightIntensity, 1.0f, true, "Environment Brightness", "Light Intensity", "Render");
    CFG_VAR("visual_third_person", bool, thirdPersonEnabled, false, false, "Third Person", "", "Render");
    CFG_VAR("visual_third_distance", float, thirdPersonDistance, 300.0f, true, "Third Person", "Distance", "Render");
    CFG_VAR("visual_third_height", float, thirdPersonHeight, 100.0f, true, "Third Person", "Height", "Render");
    CFG_VAR("visual_third_key", int, thirdPersonToggleKey, 'Z', true, "Third Person", "Key", "Render");
    CFG_VAR("visual_derp", bool, derpEnabled, false, false, "Derp", "", "Render");
    CFG_VAR("visual_derp_network", bool, derpNetworkVisible, false, true, "Derp", "Network Visible", "Render");
    CFG_VAR("visual_derp_pitch", float, derpPitchSpeed, 0.0f, true, "Derp", "Pitch", "Render");
    CFG_VAR("visual_derp_yaw", float, derpYawSpeed, 180.0f, true, "Derp", "Yaw", "Render");
    CFG_VAR("visual_derp_roll", float, derpRollSpeed, 0.0f, true, "Derp", "Roll", "Render");
};

struct VisualStatus
{
    bool cameraReady = false;
    bool springArmReady = false;
    bool usingVehicle = false;
    bool modelReady = false;
    unsigned int suppressedFogComponents = 0;
    unsigned int boostedSkyLights = 0;
    unsigned int boostedDirectionalLights = 0;
    bool darknessLightReady = false;
};

class Visuals final
{
public:
    static Visuals& Instance();

    void Update(bool inputBlocked = false);
    void OnGameThreadTick(const SDK::UObject* tickObject, float deltaSeconds);
    bool OnBeforeNetworkDerpEvent(const SDK::UObject* object, SDK::UFunction* function,
                                  void* parameters);
    bool NeedsGameThreadTick() const noexcept;
    void Reset();

    VisualSettings& Settings() noexcept { return settings_; }
    const VisualSettings& Settings() const noexcept { return settings_; }
    const VisualStatus& Status() const noexcept { return status_; }

private:
    Visuals() = default;

    VisualSettings settings_{};
    VisualStatus status_{};
    bool thirdPersonKeyDown_ = false;
    bool derpConfiguredLastFrame_ = false;
    bool nightVisionConfiguredLastFrame_ = false;
    float nightVisionStrengthLastFrame_ = 1.0f;
    bool nightVisionRemoveFogLastFrame_ = false;
    bool nightVisionPersistentLightLastFrame_ = true;
    bool nightVisionExtendFlashlightLastFrame_ = false;
    float nightVisionLightRangeLastFrame_ = 150.0f;
    float nightVisionLightIntensityLastFrame_ = 1.0f;
    std::atomic_bool derpConfigured_{false};
    std::atomic_bool derpRestorePending_{false};
    std::atomic_bool nightVisionConfigured_{false};
    std::atomic_bool nightVisionRestorePending_{false};
};
}

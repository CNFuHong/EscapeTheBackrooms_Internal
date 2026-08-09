#pragma once

#include "features/array_list.hpp"
#include "core/config.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace etb::features
{
enum class NotificationType : int
{
    Info,
    Success,
    Warning,
    Error
};

struct NotificationSettings
{
    CFG_VAR("notifications_enabled", bool, enabled, true, false, "Notifications", "", "HUD");
    CFG_VAR("notifications_toggle", bool, showOnToggle, true, true, "Notifications", "Show On Toggle", "HUD");
    CFG_VAR("notifications_scene", bool, sceneSummary, true, true, "Notifications", "Scene Summary", "HUD");
    CFG_VAR("notifications_discovery", bool, discoveryAlerts, true, true, "Notifications", "Discovery Alerts", "HUD");
    CFG_VAR("notifications_discovery_radius", float, discoveryExclusionRadiusMeters, 5.0f, true, "Notifications", "Discovery Radius", "HUD");
    CFG_VAR("notifications_sanity", bool, sanityWarnings, true, true, "Notifications", "Sanity Warnings", "HUD");
    CFG_VAR("notifications_control", bool, controlModeAlerts, true, true, "Notifications", "Control Alerts", "HUD");
    CFG_VAR("notifications_size", float, sizePercent, 100.0f, true, "Notifications", "Size", "HUD");
    CFG_VAR("notifications_duration", float, durationSeconds, 2.0f, true, "Notifications", "Duration", "HUD");
    CFG_VAR("notifications_duration_multiplier", float, durationMultiplier, 1.5f, true, "Notifications", "Duration Multiplier", "HUD");
    CFG_VAR("notifications_gradient", bool, colorGradient, false, true, "Notifications", "Gradient", "HUD");
    CFG_VAR("notifications_glow", bool, glow, true, true, "Notifications", "Glow", "HUD");
    CFG_VAR("notifications_glow_strength", float, glowStrength, 8.0f, true, "Notifications", "Glow Strength", "HUD");
    CFG_VAR("notifications_acrylic", bool, acrylicBackground, false, true, "Notifications", "Acrylic", "HUD");
    CFG_VAR("notifications_limit", bool, limitNotifications, false, true, "Notifications", "Limit", "HUD");
    CFG_VAR("notifications_maximum", int, maximumNotifications, 12, true, "Notifications", "Maximum", "HUD");
    CFG_VAR("notifications_accent", ::etb::core::config::Color4, accentColor, ::etb::core::config::MakeColor(0.20f, 0.68f, 1.0f, 1.0f), true, "Notifications", "Accent", "HUD");
    CFG_VAR("notifications_gradient_color", ::etb::core::config::Color4, gradientColor, ::etb::core::config::MakeColor(0.67f, 0.32f, 1.0f, 1.0f), true, "Notifications", "Gradient Color", "HUD");
};

class Notifications final
{
public:
    static Notifications& Instance();

    bool InitializeRenderResources(float dpiScale);
    void ShutdownRenderResources();
    void Draw();
    void Reset();
    void Push(std::string message, NotificationType type = NotificationType::Info,
              float durationSeconds = 0.0f);

    NotificationSettings& Settings() noexcept { return settings_; }
    const NotificationSettings& Settings() const noexcept { return settings_; }

private:
    struct RuntimeNotification
    {
        std::string message;
        NotificationType type = NotificationType::Info;
        float duration = 2.0f;
        float currentDuration = 0.0f;
        float timeShown = 0.0f;
        bool timeUp = false;
    };

    Notifications() = default;

    void ObserveModuleToggles();
    void ObserveGameplayEvents();
    void RenderSolaris(float deltaSeconds);

    NotificationSettings settings_{};
    std::vector<RuntimeNotification> notifications_{};
    std::array<bool, ArrayListHud::ModuleCount> previousModuleStates_{};
    bool moduleStatesInitialized_ = false;
    std::uint64_t pendingSceneSignature_ = 0;
    std::uint64_t summarizedSceneSignature_ = 0;
    std::uint64_t observedScanRevision_ = 0;
    double sceneSummaryDue_ = 0.0;
    std::array<std::size_t, 6> previousSceneCounts_{};
    bool sceneCountsInitialized_ = false;
    bool sanityLowEpisode_ = false;
    bool sanityWasKnown_ = false;
    float previousSanityPercent_ = -1.0f;
    std::uint8_t sanityStageMask_ = 0;
    bool controlModeInitialized_ = false;
    bool previousVehicleMode_ = false;
    float dpiScale_ = 1.0f;
};
}

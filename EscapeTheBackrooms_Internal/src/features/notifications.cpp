#include "features/notifications.hpp"

#include "features/esp.hpp"
#include "features/exit_activator.hpp"
#include "features/pickup.hpp"
#include "features/visuals.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <utility>

namespace etb::features
{
namespace
{
constexpr int ShadowCornerSize = 16;
constexpr int ShadowEdgeSize = 1;
constexpr int ShadowPadding = 2;
constexpr int ShadowTextureSize = ShadowCornerSize + ShadowEdgeSize + ShadowPadding;
constexpr int ShadowAllocationSize = ShadowTextureSize + ShadowPadding;
constexpr float ShadowFalloffPower = 4.8f;
constexpr float ShadowDistanceFieldOffset = 3.8f;

struct ShadowTextureState
{
    ImFontAtlas* atlas = nullptr;
    ImFontAtlasRectId rectId = ImFontAtlasRectId_Invalid;
};

ShadowTextureState g_shadowTexture{};

float Lerp(const float from, const float to, const float amount)
{
    return from + amount * (to - from);
}

float DistanceFromRectangle(const ImVec2 sample, const ImVec2 minimum, const ImVec2 maximum)
{
    const ImVec2 center((minimum.x + maximum.x) * 0.5f,
                        (minimum.y + maximum.y) * 0.5f);
    const ImVec2 halfSize((maximum.x - minimum.x) * 0.5f,
                          (maximum.y - minimum.y) * 0.5f);
    const ImVec2 local(sample.x - center.x, sample.y - center.y);
    const ImVec2 axis(std::fabs(local.x) - halfSize.x,
                      std::fabs(local.y) - halfSize.y);
    const float outsideX = std::max(axis.x, 0.0f);
    const float outsideY = std::max(axis.y, 0.0f);
    const float outside = std::sqrt(outsideX * outsideX + outsideY * outsideY);
    const float inside = std::min(std::max(axis.x, axis.y), 0.0f);
    return outside + inside;
}

void GaussianBlurPass(const std::array<float, 33 * 33>& source,
                      std::array<float, 33 * 33>& destination,
                      const bool horizontal)
{
    constexpr std::array<float, 15> coefficients{
        0.0f, 0.0f, 0.000003f, 0.000229f, 0.005977f,
        0.060598f, 0.24173f, 0.382925f, 0.24173f, 0.060598f,
        0.005977f, 0.000229f, 0.000003f, 0.0f, 0.0f
    };
    constexpr int radius = static_cast<int>(coefficients.size() / 2);
    constexpr int size = 33;

    for (int y = 0; y < size; ++y)
    {
        for (int x = 0; x < size; ++x)
        {
            float result = 0.0f;
            for (int sample = 0; sample < static_cast<int>(coefficients.size()); ++sample)
            {
                const int sampleX = horizontal ? x + sample - radius : x;
                const int sampleY = horizontal ? y : y + sample - radius;
                if (sampleX >= 0 && sampleX < size && sampleY >= 0 && sampleY < size)
                    result += source[sampleX + sampleY * size] * coefficients[sample];
            }
            destination[x + y * size] = result;
        }
    }
}

bool BuildShadowTexture(ImFontAtlas* atlas)
{
    if (atlas == nullptr)
        return false;

    ImFontAtlasRect rect{};
    const ImFontAtlasRectId rectId = atlas->AddCustomRect(
        ShadowAllocationSize, ShadowAllocationSize, &rect);
    if (rectId == ImFontAtlasRectId_Invalid || atlas->TexData == nullptr ||
        atlas->TexData->Pixels == nullptr)
    {
        return false;
    }

    ImTextureData* texture = atlas->TexData;
    for (int y = 0; y < ShadowAllocationSize; ++y)
    {
        for (int x = 0; x < ShadowAllocationSize; ++x)
        {
            if (texture->Format == ImTextureFormat_Alpha8)
                *static_cast<ImU8*>(texture->GetPixelsAt(rect.x + x, rect.y + y)) = 0;
            else
                *static_cast<ImU32*>(texture->GetPixelsAt(rect.x + x, rect.y + y)) = IM_COL32(255, 255, 255, 0);
        }
    }

    constexpr int sourceSize = ShadowCornerSize + ShadowEdgeSize + ShadowCornerSize;
    static_assert(sourceSize == 33);
    std::array<float, sourceSize * sourceSize> distanceField{};
    std::array<float, sourceSize * sourceSize> temporary{};
    const ImVec2 shadowMinimum(static_cast<float>(ShadowCornerSize),
                               static_cast<float>(ShadowCornerSize));
    const ImVec2 shadowMaximum(static_cast<float>(ShadowCornerSize + ShadowEdgeSize),
                               static_cast<float>(ShadowCornerSize + ShadowEdgeSize));

    for (int y = 0; y < sourceSize; ++y)
    {
        for (int x = 0; x < sourceSize; ++x)
        {
            const float distance = DistanceFromRectangle(
                ImVec2(static_cast<float>(x), static_cast<float>(y)),
                shadowMinimum, shadowMaximum);
            const float normalized = std::clamp(
                (distance + ShadowDistanceFieldOffset) /
                    (ShadowCornerSize + ShadowDistanceFieldOffset),
                0.0f, 1.0f);
            distanceField[x + y * sourceSize] = std::pow(1.0f - normalized,
                                                         ShadowFalloffPower);
        }
    }

    GaussianBlurPass(distanceField, temporary, true);
    GaussianBlurPass(temporary, distanceField, false);

    const int destinationX = rect.x + ShadowPadding;
    const int destinationY = rect.y + ShadowPadding;
    for (int y = 0; y < ShadowTextureSize; ++y)
    {
        for (int x = 0; x < ShadowTextureSize; ++x)
        {
            const auto alpha = static_cast<ImU8>(255.0f * distanceField[x + y * sourceSize]);
            if (texture->Format == ImTextureFormat_Alpha8)
                *static_cast<ImU8*>(texture->GetPixelsAt(destinationX + x, destinationY + y)) = alpha;
            else
                *static_cast<ImU32*>(texture->GetPixelsAt(destinationX + x, destinationY + y)) =
                    IM_COL32(255, 255, 255, alpha);
        }
    }

    g_shadowTexture.atlas = atlas;
    g_shadowTexture.rectId = rectId;
    return true;
}

void AddShadowRect(ImDrawList* drawList, const ImVec2& objectMinimum,
                   const ImVec2& objectMaximum, const ImU32 color,
                   const float thickness, const ImVec2& offset = ImVec2())
{
    if (drawList == nullptr || (color & IM_COL32_A_MASK) == 0 || thickness <= 0.0f ||
        g_shadowTexture.atlas == nullptr ||
        g_shadowTexture.rectId == ImFontAtlasRectId_Invalid)
    {
        return;
    }

    ImFontAtlasRect rect{};
    ImFontAtlas* atlas = g_shadowTexture.atlas;
    if (!atlas->GetCustomRect(g_shadowTexture.rectId, &rect))
        return;

    const float inverseWidth = 1.0f / static_cast<float>(atlas->TexData->Width);
    const float inverseHeight = 1.0f / static_cast<float>(atlas->TexData->Height);
    const int textureX = rect.x + ShadowPadding;
    const int textureY = rect.y + ShadowPadding;

    for (int x = 0; x < 3; ++x)
    {
        for (int y = 0; y < 3; ++y)
        {
            int sourceX = textureX;
            int sourceY = textureY;
            int sourceWidth = ShadowCornerSize;
            int sourceHeight = ShadowCornerSize;
            bool flipHorizontal = false;
            bool flipVertical = false;

            if (x == 1)
            {
                sourceX += ShadowCornerSize;
                sourceWidth = ShadowEdgeSize;
            }
            else if (x == 2)
            {
                flipHorizontal = true;
            }
            if (y == 1)
            {
                sourceY += ShadowCornerSize;
                sourceHeight = ShadowEdgeSize;
            }
            else if (y == 2)
            {
                flipVertical = true;
            }

            const ImVec2 uv0(sourceX * inverseWidth, sourceY * inverseHeight);
            const ImVec2 uv1((sourceX + sourceWidth) * inverseWidth,
                             (sourceY + sourceHeight) * inverseHeight);
            const ImVec2 uvMinimum(flipHorizontal ? uv1.x : uv0.x,
                                   flipVertical ? uv1.y : uv0.y);
            const ImVec2 uvMaximum(flipHorizontal ? uv0.x : uv1.x,
                                   flipVertical ? uv0.y : uv1.y);

            ImVec2 drawMinimum{};
            ImVec2 drawMaximum{};
            if (x == 0)
            {
                drawMinimum.x = objectMinimum.x - thickness;
                drawMaximum.x = objectMinimum.x;
            }
            else if (x == 1)
            {
                drawMinimum.x = objectMinimum.x;
                drawMaximum.x = objectMaximum.x;
            }
            else
            {
                drawMinimum.x = objectMaximum.x;
                drawMaximum.x = objectMaximum.x + thickness;
            }
            if (y == 0)
            {
                drawMinimum.y = objectMinimum.y - thickness;
                drawMaximum.y = objectMinimum.y;
            }
            else if (y == 1)
            {
                drawMinimum.y = objectMinimum.y;
                drawMaximum.y = objectMaximum.y;
            }
            else
            {
                drawMinimum.y = objectMaximum.y;
                drawMaximum.y = objectMaximum.y + thickness;
            }

            drawList->AddImage(atlas->TexRef,
                               ImVec2(drawMinimum.x + offset.x, drawMinimum.y + offset.y),
                               ImVec2(drawMaximum.x + offset.x, drawMaximum.y + offset.y),
                               uvMinimum, uvMaximum, color);
        }
    }
}

ImVec4 TypeColor(const NotificationSettings& settings, const NotificationType type)
{
    switch (type)
    {
    case NotificationType::Success: return ImVec4(0.0f, 1.0f, 0.0f, 1.0f);
    case NotificationType::Warning: return ImVec4(1.0f, 0.80f, 0.0f, 1.0f);
    case NotificationType::Error:   return ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
    case NotificationType::Info:
    default:
        return ImVec4(settings.accentColor[0], settings.accentColor[1],
                      settings.accentColor[2], settings.accentColor[3]);
    }
}

void DrawGradient(ImDrawList* drawList, const ImVec2& minimum, const ImVec2& maximum,
                  const ImVec4& first, const ImVec4& second)
{
    drawList->AddRectFilledMultiColor(
        minimum, maximum,
        ImGui::ColorConvertFloat4ToU32(first),
        ImGui::ColorConvertFloat4ToU32(second),
        ImGui::ColorConvertFloat4ToU32(second),
        ImGui::ColorConvertFloat4ToU32(first));
}
}

Notifications& Notifications::Instance()
{
    static Notifications instance;
    return instance;
}

bool Notifications::InitializeRenderResources(const float dpiScale)
{
    dpiScale_ = dpiScale > 0.0f && std::isfinite(dpiScale) ? dpiScale : 1.0f;
    if (ImGui::GetCurrentContext() == nullptr)
        return false;
    return BuildShadowTexture(ImGui::GetIO().Fonts);
}

void Notifications::ShutdownRenderResources()
{
    if (g_shadowTexture.atlas != nullptr &&
        g_shadowTexture.rectId != ImFontAtlasRectId_Invalid &&
        ImGui::GetCurrentContext() != nullptr)
    {
        g_shadowTexture.atlas->RemoveCustomRect(g_shadowTexture.rectId);
    }
    g_shadowTexture = {};
    dpiScale_ = 1.0f;
}

void Notifications::Push(std::string message, const NotificationType type,
                         const float durationSeconds)
{
    if (message.empty())
        return;
    if (message.size() > 256)
        message = message.substr(0, 253) + "...";

    RuntimeNotification notification{};
    notification.message = std::move(message);
    notification.type = type;
    const float baseDuration = durationSeconds > 0.0f
        ? durationSeconds : std::clamp(settings_.durationSeconds, 0.25f, 15.0f);
    notification.duration = std::clamp(
        baseDuration * std::clamp(settings_.durationMultiplier, 0.5f, 3.0f), 0.25f, 30.0f);
    notifications_.push_back(std::move(notification));

    constexpr std::size_t maximumQueue = 256;
    if (notifications_.size() > maximumQueue)
        notifications_.erase(notifications_.begin(),
                             notifications_.begin() + (notifications_.size() - maximumQueue));
}

void Notifications::ObserveModuleToggles()
{
    // This walks every module and formats its mode text, and the HUD does the same walk
    // for its own layout. Toggle notifications do not need every-frame resolution, so
    // throttle the whole capture to 10 Hz to cut that computation.
    static std::chrono::steady_clock::time_point nextCapture{};
    const auto now = std::chrono::steady_clock::now();
    if (now < nextCapture)
        return;
    nextCapture = now + std::chrono::milliseconds(100);

    std::array<HudModuleState, ArrayListHud::ModuleCount> states{};
    ArrayListHud::Instance().CaptureModuleStates(states);

    if (!moduleStatesInitialized_)
    {
        for (std::size_t index = 0; index < states.size(); ++index)
            previousModuleStates_[index] = states[index].enabled;
        moduleStatesInitialized_ = true;
        return;
    }

    for (std::size_t index = 0; index < states.size(); ++index)
    {
        if (states[index].enabled == previousModuleStates_[index])
            continue;

        previousModuleStates_[index] = states[index].enabled;
        if (!settings_.enabled || !settings_.showOnToggle)
            continue;

        std::string message(states[index].name);
        message += states[index].enabled ? " was enabled" : " was disabled";
        Push(std::move(message), NotificationType::Info, 2.0f);
    }
}

void Notifications::ObserveGameplayEvents()
{
    const double now = ImGui::GetTime();
    const EspStats& stats = Esp::Instance().Stats();

    std::vector<std::string> pickupMessages;
    Pickup::Instance().DrainNotifications(pickupMessages);
    for (std::string& message : pickupMessages)
        Push(std::move(message), NotificationType::Success, 3.5f);

    std::vector<std::string> exitMessages;
    ExitActivator::Instance().DrainNotifications(exitMessages);
    for (std::string& message : exitMessages)
        Push(std::move(message), NotificationType::Success, 4.0f);

    if (settings_.controlModeAlerts)
    {
        const bool vehicleMode = Visuals::Instance().Status().usingVehicle;
        if (!controlModeInitialized_)
        {
            previousVehicleMode_ = vehicleMode;
            controlModeInitialized_ = true;
        }
        else if (vehicleMode != previousVehicleMode_)
        {
            previousVehicleMode_ = vehicleMode;
            Push(vehicleMode ? "Vehicle control acquired" : "Returned to player control",
                 vehicleMode ? NotificationType::Success : NotificationType::Info, 2.5f);
        }
    }

    if (stats.worldReady && stats.sceneSignature != 0 &&
        stats.sceneSignature != pendingSceneSignature_)
    {
        pendingSceneSignature_ = stats.sceneSignature;
        sceneSummaryDue_ = now + 1.0;
        sceneCountsInitialized_ = false;
        observedScanRevision_ = stats.scanRevision;
        sanityLowEpisode_ = false;
        sanityWasKnown_ = false;
        previousSanityPercent_ = -1.0f;
        sanityStageMask_ = 0;
    }

    if (settings_.sceneSummary && stats.worldReady && pendingSceneSignature_ != 0 &&
        summarizedSceneSignature_ != pendingSceneSignature_ && now >= sceneSummaryDue_)
    {
        char message[192]{};
        std::snprintf(message, sizeof(message), "[Scene] Actors %zu | Tracked %zu",
                      stats.scannedActors, stats.cachedEntities);
        Push(message, NotificationType::Info, 4.0f);

        std::snprintf(message, sizeof(message), "[Threats] Entities %zu | Other players %zu",
                      stats.monsters, stats.players);
        Push(message, stats.monsters > 0 ? NotificationType::Warning : NotificationType::Success,
             4.0f);

        std::snprintf(message, sizeof(message), "[Loot] Items %zu | Exits %zu",
                      stats.items, stats.exits);
        Push(message, stats.exits > 0 ? NotificationType::Success : NotificationType::Info, 4.0f);

        std::snprintf(message, sizeof(message), "[Objectives] Levers %zu | Valves %zu",
                      stats.levers, stats.valves);
        Push(message, (stats.levers + stats.valves) > 0 ? NotificationType::Success :
                                                         NotificationType::Info, 4.0f);

        std::snprintf(message, sizeof(message), "[Motion] Dynamic classes %zu",
                      stats.dynamicEntities);
        Push(message, NotificationType::Info, 4.0f);

        std::snprintf(message, sizeof(message), stats.monsters > 0
            ? "[Danger] %zu hostile entities detected"
            : "[Safe] No hostile entities detected", stats.monsters);
        Push(message, stats.monsters > 0 ? NotificationType::Warning : NotificationType::Success,
             4.0f);

        summarizedSceneSignature_ = pendingSceneSignature_;
        previousSceneCounts_ = {stats.alertMonsters, stats.alertPlayers, stats.alertItems,
                                stats.alertExits, stats.alertObjectives,
                                stats.alertDynamicEntities};
        sceneCountsInitialized_ = true;
        observedScanRevision_ = stats.scanRevision;
    }

    if (settings_.discoveryAlerts && stats.worldReady && sceneCountsInitialized_ &&
        summarizedSceneSignature_ == stats.sceneSignature &&
        observedScanRevision_ != stats.scanRevision)
    {
        const std::array<std::size_t, 6> counts{
            stats.alertMonsters, stats.alertPlayers, stats.alertItems, stats.alertExits,
            stats.alertObjectives, stats.alertDynamicEntities
        };
        const auto increasedBy = [&counts, this](const std::size_t index)
        {
            return counts[index] > previousSceneCounts_[index]
                ? counts[index] - previousSceneCounts_[index] : 0;
        };

        char message[160]{};
        if (const std::size_t added = increasedBy(0); added > 0)
        {
            std::snprintf(message, sizeof(message), "[Danger] New entities detected +%zu", added);
            Push(message, NotificationType::Warning, 3.0f);
        }
        if (const std::size_t added = increasedBy(1); added > 0)
        {
            std::snprintf(message, sizeof(message), "[Players] Newly detected +%zu", added);
            Push(message, NotificationType::Success, 3.0f);
        }
        if (const std::size_t added = increasedBy(2); added > 0)
        {
            std::snprintf(message, sizeof(message), "[Loot] New items detected +%zu", added);
            Push(message, NotificationType::Info, 3.0f);
        }
        if (const std::size_t added = increasedBy(3); added > 0)
        {
            std::snprintf(message, sizeof(message), "[Exit] New routes detected +%zu", added);
            Push(message, NotificationType::Success, 3.0f);
        }
        if (const std::size_t added = increasedBy(4); added > 0)
        {
            std::snprintf(message, sizeof(message), "[Objective] New levers/valves +%zu", added);
            Push(message, NotificationType::Success, 3.0f);
        }
        if (const std::size_t added = increasedBy(5); added > 0)
        {
            std::snprintf(message, sizeof(message), "[Motion] New moving classes +%zu", added);
            Push(message, NotificationType::Warning, 3.0f);
        }

        previousSceneCounts_ = counts;
        observedScanRevision_ = stats.scanRevision;
    }

    if (!settings_.sanityWarnings || stats.localMaxSanity <= 0.0f ||
        !std::isfinite(stats.localSanity) || !std::isfinite(stats.localMaxSanity))
        return;

    const float sanityPercent = std::clamp(
        stats.localSanity / stats.localMaxSanity * 100.0f, 0.0f, 100.0f);
    if (!sanityWasKnown_)
    {
        previousSanityPercent_ = sanityPercent;
        sanityLowEpisode_ = sanityPercent <= 50.0f;
        sanityWasKnown_ = true;
        return;
    }

    struct SanityStage
    {
        float threshold;
        std::uint8_t mask;
        const char* label;
        NotificationType type;
    };
    constexpr std::array<SanityStage, 3> stages{{
        {50.0f, 1u << 0, "LOW",      NotificationType::Warning},
        {30.0f, 1u << 1, "CRITICAL", NotificationType::Warning},
        {10.0f, 1u << 2, "DANGER",   NotificationType::Error},
    }};

    for (const SanityStage& stage : stages)
    {
        if (sanityPercent >= stage.threshold + 5.0f)
            sanityStageMask_ &= static_cast<std::uint8_t>(~stage.mask);

        if (previousSanityPercent_ > stage.threshold && sanityPercent <= stage.threshold &&
            (sanityStageMask_ & stage.mask) == 0)
        {
            sanityStageMask_ |= stage.mask;
            sanityLowEpisode_ = true;
            for (int warning = 1; warning <= 3; ++warning)
            {
                char message[160]{};
                std::snprintf(message, sizeof(message),
                              "[Sanity] %s STAGE %.0f%% - warning %d/3",
                              stage.label, stage.threshold, warning);
                Push(message, stage.type, 3.5f);
            }
        }
    }

    if (sanityLowEpisode_ && previousSanityPercent_ < 55.0f && sanityPercent >= 55.0f)
    {
        char message[128]{};
        std::snprintf(message, sizeof(message), "[Sanity] Recovered to %.0f%%", sanityPercent);
        Push(message, NotificationType::Success, 3.0f);
        sanityLowEpisode_ = false;
    }
    previousSanityPercent_ = sanityPercent;
}

void Notifications::Draw()
{
    ObserveModuleToggles();
    if (!settings_.enabled || ImGui::GetCurrentContext() == nullptr)
        return;

    ObserveGameplayEvents();

    std::erase_if(notifications_, [](const RuntimeNotification& notification)
    {
        return notification.timeUp && notification.timeShown > notification.duration + 0.5f;
    });
    RenderSolaris(ImGui::GetIO().DeltaTime);
}

void Notifications::RenderSolaris(const float deltaSeconds)
{
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    ImFont* font = ImGui::GetFont();
    if (drawList == nullptr || font == nullptr)
        return;

    float y = display.y - 10.0f;
    const float scale = settings_.sizePercent / 100.0f;
    int visibleCount = 0;

    for (RuntimeNotification& notification : notifications_)
    {
        if (settings_.limitNotifications && visibleCount >= settings_.maximumNotifications)
            break;

        notification.timeShown += deltaSeconds;
        notification.timeUp = notification.timeShown >= notification.duration;
        notification.currentDuration = Lerp(notification.currentDuration,
                                             notification.timeUp ? 0.0f : 1.0f,
                                             deltaSeconds * 5.0f);

        const float percentDone = std::clamp(
            notification.timeShown / notification.duration, 0.0f, 1.0f);
        const float fontSize = 20.0f * dpiScale_ * scale;
        const ImVec2 textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f,
                                                    notification.message.c_str());
        const ImVec2 boxSize(std::max(200.0f, 50.0f + textSize.x), textSize.y + 30.0f);

        const float beginX = display.x - boxSize.x - 10.0f;
        const float endX = display.x + boxSize.x;
        const float x = Lerp(endX, beginX, notification.currentDuration);
        y = Lerp(y, y - boxSize.y, notification.currentDuration);
        if (x > display.x + boxSize.x && y > display.y + boxSize.y)
            continue;

        const bool antiVoid = notification.message.find("[Anti Void]") != std::string::npos;
        ImVec4 themeColor = antiVoid
            ? ImVec4(1.0f, 0.85f, 0.0f, 1.0f)
            : TypeColor(settings_, notification.type);
        themeColor.w = 0.70f;
        const float maximumX = x + boxSize.x;
        ImVec2 progressMaximum(x + boxSize.x * percentDone + 6.0f,
                               y + boxSize.y - 10.0f);
        progressMaximum.x = std::clamp(progressMaximum.x, x, maximumX);
        const ImVec2 backgroundMinimum(x + boxSize.x * percentDone, y);
        const ImVec2 backgroundMaximum(x + boxSize.x, y + boxSize.y - 10.0f);

        if (settings_.glow)
        {
            const ImVec2 fullBoxMaximum(x + boxSize.x, y + boxSize.y - 10.0f);
            const float gap = 6.0f * scale;
            const ImVec2 haloMinimum(x - gap, y - gap);
            const ImVec2 haloMaximum(fullBoxMaximum.x + gap, fullBoxMaximum.y + gap);
            ImVec4 inner = antiVoid
                ? ImVec4(1.0f, 0.90f, 0.0f, 1.0f)
                : ImVec4(themeColor.x, themeColor.y, themeColor.z, 1.0f);
            ImVec4 outer = antiVoid
                ? ImVec4(1.0f, 0.60f, 0.0f, 0.5f)
                : ImVec4(themeColor.x, themeColor.y, themeColor.z, 0.5f);
            AddShadowRect(drawList, haloMinimum, haloMaximum,
                          ImGui::ColorConvertFloat4ToU32(inner),
                          28.0f * settings_.glowStrength);
            AddShadowRect(drawList, haloMinimum, haloMaximum,
                          ImGui::ColorConvertFloat4ToU32(outer),
                          52.0f * settings_.glowStrength);
        }

        drawList->PushClipRect(ImVec2(x, y),
                               ImVec2(x + boxSize.x * percentDone,
                                      y + boxSize.y - 10.0f));
        if (!settings_.colorGradient)
        {
            drawList->AddRectFilled(ImVec2(x, y), progressMaximum,
                                    ImGui::ColorConvertFloat4ToU32(themeColor), 5.0f);
        }
        else
        {
            ImVec4 gradient(settings_.gradientColor[0], settings_.gradientColor[1],
                            settings_.gradientColor[2], 0.70f);
            DrawGradient(drawList, ImVec2(x, y), progressMaximum, themeColor, gradient);
        }
        drawList->PopClipRect();

        drawList->PushClipRect(backgroundMinimum, backgroundMaximum);
        if (!settings_.acrylicBackground)
        {
            drawList->AddRectFilled(
                ImVec2(x + boxSize.x * percentDone - 6.0f, y),
                backgroundMaximum, ImColor(0.0f, 0.0f, 0.0f, 0.70f), 5.0f);
        }
        drawList->PopClipRect();

        drawList->AddText(font, fontSize, ImVec2(x + 10.0f, y + 10.0f),
                          IM_COL32_WHITE, notification.message.c_str());
        if (!notification.timeUp)
            ++visibleCount;
    }
}

bool Notifications::HasVisible() const noexcept
{
    return !notifications_.empty();
}

bool Notifications::WantsFrame() const noexcept
{
    if (!settings_.enabled)
        return !notifications_.empty();
    if (!notifications_.empty())
        return true;
    // Any of these can still produce a notification this frame.
    if (settings_.sceneSummary || settings_.sanityWarnings || settings_.discoveryAlerts ||
        settings_.controlModeAlerts)
        return true;
    // Toggle notifications only make sense while the HUD that lists modules is drawn.
    return settings_.showOnToggle && ArrayListHud::Instance().Settings().enabled;
}

void Notifications::Reset()
{
    notifications_.clear();
    previousModuleStates_.fill(false);
    moduleStatesInitialized_ = false;
    pendingSceneSignature_ = 0;
    summarizedSceneSignature_ = 0;
    observedScanRevision_ = 0;
    sceneSummaryDue_ = 0.0;
    previousSceneCounts_.fill(0);
    sceneCountsInitialized_ = false;
    sanityLowEpisode_ = false;
    sanityWasKnown_ = false;
    previousSanityPercent_ = -1.0f;
    sanityStageMask_ = 0;
    controlModeInitialized_ = false;
    previousVehicleMode_ = false;
}
}

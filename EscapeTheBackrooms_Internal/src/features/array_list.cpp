#include "features/array_list.hpp"

#include "features/esp.hpp"
#include "features/exit_activator.hpp"
#include "features/movement.hpp"
#include "features/notifications.hpp"
#include "features/pickup.hpp"
#include "features/spectator.hpp"
#include "features/visuals.hpp"
#include "features/vehicle_flight.hpp"

#include <Windows.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace etb::features
{
namespace
{
using Clock = std::chrono::steady_clock;

constexpr float SlideDurationSeconds = 0.400f;
constexpr float GravitySlideDurationSeconds = 0.300f;
constexpr float ExitFinishDelaySeconds = 0.075f;
constexpr float HueSpeedDegreesPerSecond = 40.0f;
constexpr float LayoutSmoothRate = 10.0f;

enum Category : int
{
    CategoryEsp = 0,
    CategoryMovement = 1,
    CategoryVisual = 2,
    CategoryHost = 3
};

struct ModuleSnapshot
{
    bool enabled = false;
    bool* toggle = nullptr;
    int keybind = 0;
    int category = CategoryEsp;
    std::array<char, 48> modeText{};
};

using QueryModuleFn = ModuleSnapshot(*)();

constexpr std::size_t NoGate = static_cast<std::size_t>(-1);

struct ModuleDescriptor
{
    std::string text;
    QueryModuleFn query = nullptr;

    // Auto (config-driven) rows: state comes from the config registry instead of a
    // hand-written query function. `gate` is the index of the feature row that must be
    // enabled for a parameter row to show (NoGate = the row's own toggle governs it).
    ::etb::core::config::IConfigItem* config = nullptr;
    bool* toggle = nullptr;
    int keybind = 0;
    int category = CategoryVisual;
    std::size_t gate = NoGate;
    std::string valueText; // pre-computed right-hand text for auto feature rows
    ArrayListHud::ModuleModeTextFn modeText = nullptr;
};

struct RowLayout
{
    std::size_t id = 0;
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    std::array<char, 96> mainText{};
    std::array<char, 64> modeText{};
};

template <typename... Arguments>
ModuleSnapshot WithMode(ModuleSnapshot snapshot, const char* format, Arguments... arguments)
{
    std::snprintf(snapshot.modeText.data(), snapshot.modeText.size(), format, arguments...);
    return snapshot;
}

float SecondsBetween(const Clock::time_point newer, const Clock::time_point older)
{
    return std::chrono::duration<float>(newer - older).count();
}

float DecelerateFactor2(const float input)
{
    const float t = std::clamp(input, 0.0f, 1.0f);
    const float inverse = 1.0f - t;
    return 1.0f - inverse * inverse * inverse * inverse;
}

ModuleSnapshot EspModule()
{
    auto& settings = Esp::Instance().Settings();
    return WithMode(ModuleSnapshot{settings.enabled, &settings.enabled, 0, CategoryEsp},
                    "D%.0fm R%dms C%zu", settings.maxDistanceMeters,
                    settings.refreshIntervalMs, Esp::Instance().Stats().cachedEntities);
}

ModuleSnapshot BoxesModule()
{
    auto& settings = Esp::Instance().Settings();
    return WithMode(ModuleSnapshot{settings.enabled && settings.boxes, &settings.boxes,
                                   0, CategoryEsp}, "S%.2fx", settings.cardScale);
}

ModuleSnapshot ThreeDBoxesModule()
{
    auto& settings = Esp::Instance().Settings();
    return WithMode(ModuleSnapshot{settings.enabled && settings.threeDBoxes,
                                   &settings.threeDBoxes, 0, CategoryEsp}, "3D");
}

ModuleSnapshot FilledBoxesModule()
{
    auto& settings = Esp::Instance().Settings();
    ModuleSnapshot result{settings.enabled && settings.filledBoxes,
                          &settings.filledBoxes, 0, CategoryEsp};
    std::snprintf(result.modeText.data(), result.modeText.size(), "%.0f%%",
                  settings.fillOpacity * 100.0f);
    return result;
}

ModuleSnapshot LabelsModule()
{
    auto& settings = Esp::Instance().Settings();
    ModuleSnapshot result{settings.enabled && settings.labels, &settings.labels, 0, CategoryEsp};
    std::snprintf(result.modeText.data(), result.modeText.size(), "%.2fx", settings.cardScale);
    return result;
}

ModuleSnapshot DistanceModule()
{
    auto& settings = Esp::Instance().Settings();
    ModuleSnapshot result{settings.enabled && settings.distance, &settings.distance, 0, CategoryEsp};
    std::snprintf(result.modeText.data(), result.modeText.size(), "%.0fm", settings.maxDistanceMeters);
    return result;
}

ModuleSnapshot TracerModule()
{
    auto& settings = Esp::Instance().Settings();
    ModuleSnapshot result{settings.enabled && settings.tracers, &settings.tracers, 0, CategoryEsp};
    std::snprintf(result.modeText.data(), result.modeText.size(), "%s",
                  settings.monsterTracersOnly ? "Monsters" : "All");
    return result;
}

ModuleSnapshot MonstersModule()
{
    auto& settings = Esp::Instance().Settings();
    return WithMode(ModuleSnapshot{settings.enabled && settings.monsters, &settings.monsters,
                                   0, CategoryEsp}, "D%.0fm N%zu",
                    settings.maxDistanceMeters, Esp::Instance().Stats().monsters);
}

ModuleSnapshot PlayersModule()
{
    auto& settings = Esp::Instance().Settings();
    return WithMode(ModuleSnapshot{settings.enabled && settings.players, &settings.players,
                                   0, CategoryEsp}, "D%.0fm N%zu Name:%s San:%s",
                    settings.maxDistanceMeters, Esp::Instance().Stats().players,
                    settings.playerNameTags ? "Y" : "N", settings.playerSanity ? "Y" : "N");
}

ModuleSnapshot EntitySpeedModule()
{
    auto& settings = Esp::Instance().Settings();
    return WithMode(ModuleSnapshot{settings.entitySpeed, &settings.entitySpeed, 0, CategoryEsp},
                    ">=1.0m/s 100ms");
}

ModuleSnapshot ItemsModule()
{
    auto& settings = Esp::Instance().Settings();
    return WithMode(ModuleSnapshot{settings.enabled && settings.items, &settings.items,
                                   0, CategoryEsp}, "D%.0fm N%zu Stack %.0fpx/%.0fm",
                    settings.maxDistanceMeters, Esp::Instance().Stats().items,
                    settings.itemMergeScreenDistancePixels, settings.itemMergeDepthMeters);
}

ModuleSnapshot ExitsModule()
{
    auto& settings = Esp::Instance().Settings();
    return WithMode(ModuleSnapshot{settings.enabled && settings.exits, &settings.exits,
                                   0, CategoryEsp}, "D%.0fm N%zu",
                    settings.maxDistanceMeters, Esp::Instance().Stats().exits);
}

ModuleSnapshot PathToExitModule()
{
    auto& settings = Esp::Instance().Settings();
    return WithMode(ModuleSnapshot{settings.enabled && settings.pathToExit,
                                   &settings.pathToExit, 0, CategoryEsp}, "N%zu",
                    Esp::Instance().Stats().exits);
}

ModuleSnapshot ClassNameModule()
{
    auto& settings = Esp::Instance().Settings();
    ModuleSnapshot result{settings.enabled && settings.classNamesEnabled,
                          &settings.classNamesEnabled, 0, CategoryEsp};
    std::snprintf(result.modeText.data(), result.modeText.size(), "D%.0fm S%.2fx",
                  settings.classNameMaxDistanceMeters, settings.classNameScale);
    return result;
}

ModuleSnapshot LeverModule()
{
    auto& settings = Esp::Instance().Settings();
    ModuleSnapshot result{settings.enabled && settings.leverHighlight,
                          &settings.leverHighlight, 0, CategoryEsp};
    std::snprintf(result.modeText.data(), result.modeText.size(), "D%.0fm N%zu",
                  settings.leverMaxDistanceMeters, Esp::Instance().Stats().levers);
    return result;
}

ModuleSnapshot ValveModule()
{
    auto& settings = Esp::Instance().Settings();
    ModuleSnapshot result{settings.valveHighlight, &settings.valveHighlight, 0, CategoryEsp};
    std::snprintf(result.modeText.data(), result.modeText.size(), "D%.0fm N%zu",
                  settings.valveMaxDistanceMeters, Esp::Instance().Stats().valves);
    return result;
}

ModuleSnapshot DynamicModule()
{
    auto& settings = Esp::Instance().Settings();
    ModuleSnapshot result{settings.enabled && settings.dynamicMarkersEnabled,
                          &settings.dynamicMarkersEnabled, 0, CategoryEsp};
    std::snprintf(result.modeText.data(), result.modeText.size(), "D%.0fm V%.2f T%.0fs",
                  settings.dynamicMaxDistanceMeters, settings.dynamicMinimumSpeed,
                  settings.dynamicLingerSeconds);
    return result;
}

ModuleSnapshot SpawnMarkerModule()
{
    auto& settings = Esp::Instance().Settings();
    ModuleSnapshot result{settings.spawnMarkersEnabled, &settings.spawnMarkersEnabled,
                          0, CategoryEsp};
    std::snprintf(result.modeText.data(), result.modeText.size(), "T10s D:Unlimited");
    return result;
}

ModuleSnapshot SpeedModule()
{
    auto& settings = Movement::Instance().Settings();
    ModuleSnapshot result{settings.speedEnabled, &settings.speedEnabled, 0, CategoryMovement};
    std::snprintf(result.modeText.data(), result.modeText.size(), "W%.0f S%.0f",
                  settings.walkSpeed, settings.sprintSpeed);
    return result;
}

ModuleSnapshot InfiniteStaminaModule()
{
    auto& settings = Movement::Instance().Settings();
    return WithMode(ModuleSnapshot{settings.infiniteStamina, &settings.infiniteStamina,
                                   0, CategoryMovement}, "Max");
}

ModuleSnapshot AutoSprintModule()
{
    auto& settings = Movement::Instance().Settings();
    return WithMode(ModuleSnapshot{settings.autoSprint, &settings.autoSprint,
                                   0, CategoryMovement}, "W");
}

ModuleSnapshot BunnyHopModule()
{
    auto& settings = Movement::Instance().Settings();
    return WithMode(ModuleSnapshot{settings.bunnyHop, &settings.bunnyHop,
                                   VK_SPACE, CategoryMovement}, "Hold Space");
}

ModuleSnapshot CrouchModule()
{
    auto& settings = Movement::Instance().Settings();
    ModuleSnapshot result{settings.crouchSpeedEnabled, &settings.crouchSpeedEnabled,
                          0, CategoryMovement};
    std::snprintf(result.modeText.data(), result.modeText.size(), "V%.0f", settings.crouchSpeed);
    return result;
}

ModuleSnapshot JumpModule()
{
    auto& settings = Movement::Instance().Settings();
    ModuleSnapshot result{settings.jumpEnabled, &settings.jumpEnabled, 0, CategoryMovement};
    std::snprintf(result.modeText.data(), result.modeText.size(), "Z%.0f", settings.jumpVelocity);
    return result;
}

ModuleSnapshot GravityModule()
{
    auto& settings = Movement::Instance().Settings();
    ModuleSnapshot result{settings.gravityEnabled, &settings.gravityEnabled, 0, CategoryMovement};
    std::snprintf(result.modeText.data(), result.modeText.size(), "%.2fx", settings.gravityScale);
    return result;
}

ModuleSnapshot FlyModule()
{
    auto& settings = Movement::Instance().Settings();
    ModuleSnapshot result{settings.flightEnabled, &settings.flightEnabled,
                          settings.flightToggleKey, CategoryMovement};
    std::snprintf(result.modeText.data(), result.modeText.size(), "V%.0f", settings.flySpeed);
    return result;
}

ModuleSnapshot NoClipModule()
{
    auto& settings = Movement::Instance().Settings();
    return WithMode(ModuleSnapshot{settings.noClipEnabled, &settings.noClipEnabled,
                                   settings.noClipToggleKey, CategoryMovement}, "Collision:Off");
}

ModuleSnapshot AirTurnModule()
{
    auto& settings = Movement::Instance().Settings();
    ModuleSnapshot result{settings.airTurnEnabled, &settings.airTurnEnabled, 0, CategoryMovement};
    std::snprintf(result.modeText.data(), result.modeText.size(), "C%.1f A%.0f",
                  settings.airTurnControl, settings.airTurnAcceleration);
    return result;
}

ModuleSnapshot VehicleFlightModule()
{
    auto& settings = VehicleFlight::Instance().Settings();
    ModuleSnapshot result{settings.enabled, &settings.enabled,
                          settings.toggleKey, CategoryMovement};
    std::snprintf(result.modeText.data(), result.modeText.size(), "H%.0f V%.0f B%.1fx",
                  settings.horizontalSpeed, settings.verticalSpeed, settings.boostMultiplier);
    return result;
}

ModuleSnapshot VehicleNoClipModule()
{
    auto& settings = VehicleFlight::Instance().Settings();
    return WithMode(ModuleSnapshot{settings.noClip, &settings.noClip,
                                   settings.noClipToggleKey, CategoryMovement}, "Collision:Off");
}

ModuleSnapshot SpectateTeammatesModule()
{
    auto& feature = Spectator::Instance();
    auto& settings = feature.Settings();
    const SpectatorStatus status = feature.Status();
    ModuleSnapshot result{settings.spectateEnabled, &settings.spectateEnabled,
                          settings.spectateToggleKey, CategoryMovement};
    std::snprintf(result.modeText.data(), result.modeText.size(), "Watch %zu",
                  status.playerCount);
    return result;
}

ModuleSnapshot FreeCameraModule()
{
    auto& settings = Spectator::Instance().Settings();
    ModuleSnapshot result{settings.freeCamEnabled, &settings.freeCamEnabled,
                          settings.freeCamToggleKey, CategoryMovement};
    std::snprintf(result.modeText.data(), result.modeText.size(), "V%.0f %s",
                  settings.freeCamSpeed, settings.freeCamNoClip ? "NoClip" : "Collision");
    return result;
}

ModuleSnapshot NightVisionModule()
{
    auto& settings = Visuals::Instance().Settings();
    const auto& status = Visuals::Instance().Status();
    ModuleSnapshot result{settings.nightVisionEnabled, &settings.nightVisionEnabled,
                          0, CategoryVisual};
    if (settings.nightVisionExposureBoost)
        std::snprintf(result.modeText.data(), result.modeText.size(),
                      "Env %.1fx Exp:+%.1fEV Sky%u Light%u Fog:%s Hand:%s",
                      settings.nightVisionStrength, settings.nightVisionExposureBias,
                      status.boostedSkyLights, status.boostedDirectionalLights,
                      settings.nightVisionRemoveFog ? "Off" : "Game",
                      status.darknessLightReady ? "Ready" : "Off");
    else
        std::snprintf(result.modeText.data(), result.modeText.size(),
                      "Env %.1fx Exp:Off Sky%u Light%u Fog:%s Hand:%s",
                      settings.nightVisionStrength,
                      status.boostedSkyLights, status.boostedDirectionalLights,
                      settings.nightVisionRemoveFog ? "Off" : "Game",
                      status.darknessLightReady ? "Ready" : "Off");
    return result;
}

ModuleSnapshot ThirdPersonModule()
{
    auto& settings = Visuals::Instance().Settings();
    ModuleSnapshot result{settings.thirdPersonEnabled, &settings.thirdPersonEnabled,
                          settings.thirdPersonToggleKey, CategoryVisual};
    std::snprintf(result.modeText.data(), result.modeText.size(), "D%.0f H%+.0f",
                  settings.thirdPersonDistance, settings.thirdPersonHeight);
    return result;
}

ModuleSnapshot DerpModule()
{
    auto& settings = Visuals::Instance().Settings();
    ModuleSnapshot result{settings.derpEnabled, &settings.derpEnabled, 0, CategoryVisual};
    std::snprintf(result.modeText.data(), result.modeText.size(), "%s P%+.0f Y%+.0f R%+.0f",
                  settings.derpNetworkVisible ? "Net" : "Local",
                  settings.derpPitchSpeed, settings.derpYawSpeed, settings.derpRollSpeed);
    return result;
}

ModuleSnapshot ArrayListModule()
{
    auto& settings = ArrayListHud::Instance().Settings();
    ModuleSnapshot result{settings.enabled, &settings.enabled, 0, CategoryVisual};
    const char* mode = "None";
    switch (settings.mode)
    {
    case ArrayListMode::Outline: mode = "Outline"; break;
    case ArrayListMode::EdgeLine: mode = "Edge Line"; break;
    case ArrayListMode::Rounded: mode = "Rounded"; break;
    case ArrayListMode::None:
    default: break;
    }
    std::snprintf(result.modeText.data(), result.modeText.size(), "%s %.0fpx P%.0f",
                  mode, settings.textSize, settings.rowPadding);
    return result;
}

ModuleSnapshot NotificationsModule()
{
    auto& settings = Notifications::Instance().Settings();
    ModuleSnapshot result{settings.enabled, &settings.enabled, 0, CategoryVisual};
    if (settings.limitNotifications)
        std::snprintf(result.modeText.data(), result.modeText.size(), "Sol %.1fs x%.1f %.0f%% G:%s Max%d",
                      settings.durationSeconds, settings.durationMultiplier, settings.sizePercent,
                      settings.glow ? "Y" : "N", settings.maximumNotifications);
    else
        std::snprintf(result.modeText.data(), result.modeText.size(), "Sol %.1fs x%.1f %.0f%% G:%s Unlimited",
                      settings.durationSeconds, settings.durationMultiplier, settings.sizePercent,
                      settings.glow ? "Y" : "N");
    return result;
}

ModuleSnapshot PickupModule()
{
    auto& pickup = Pickup::Instance();
    auto& settings = pickup.Settings();
    const PickupStatus status = pickup.Status();
    ModuleSnapshot result{settings.enabled, &settings.enabled,
                          settings.hotkey, CategoryMovement};
    std::snprintf(result.modeText.data(), result.modeText.size(), "%s D%.0fm Max%d B%d %s Q%zu",
                  settings.autoPickup ? "Auto" : "Manual", settings.radiusMeters,
                  settings.maxItemsPerActivation, settings.scanActorsPerTick,
                  settings.excludeFlashlights ? "NoFlash" : "All", status.queued);
    return result;
}

ModuleSnapshot HostMovementModule()
{
    auto& movement = Movement::Instance();
    auto& settings = movement.Settings();
    ModuleSnapshot result{settings.hostAcceptClientMovement,
                          &settings.hostAcceptClientMovement, 0, CategoryHost};
    std::snprintf(result.modeText.data(), result.modeText.size(), "Clients:%zu",
                  movement.Status().hostAuthorizedPawns);
    return result;
}

ModuleSnapshot ForceExitsModule()
{
    auto& feature = ExitActivator::Instance();
    auto& settings = feature.Settings();
    const ExitActivatorStatus status = feature.Status();
    ModuleSnapshot result{settings.enabled, &settings.enabled, 0, CategoryHost};
    std::snprintf(result.modeText.data(), result.modeText.size(), "%s B%d C%zu A%zu",
                  status.scanning ? "Scanning" : (status.completed ? "Done" : "Ready"),
                  settings.scanActorsPerTick, status.candidates, status.activated);
    return result;
}

ModuleSnapshot MemberAttributesModule()
{
    auto& settings = ExitActivator::Instance().Settings();
    ModuleSnapshot result{settings.maintainMemberAttributes,
                          &settings.maintainMemberAttributes, 0, CategoryHost};
    std::snprintf(result.modeText.data(), result.modeText.size(), "W%.0f S%.0f C%.0f St:%s",
                  settings.memberWalkSpeed, settings.memberSprintSpeed, settings.memberCrouchSpeed,
                  settings.memberInfiniteStamina ? "Inf" : "Game");
    return result;
}

ModuleSnapshot HostPickupModule()
{
    auto& settings = Pickup::Instance().Settings();
    ModuleSnapshot result{settings.hostAcceptMemberPickup,
                          &settings.hostAcceptMemberPickup, 0, CategoryHost};
    std::snprintf(result.modeText.data(), result.modeText.size(), "D%.0fm Auth:%zu",
                  settings.hostMaximumRadiusMeters, Pickup::Instance().Status().hostAuthorized);
    return result;
}

const std::array<ModuleDescriptor, 42> kCuratedModules{{
    {"ESP", EspModule},
    {"ESP Boxes", BoxesModule},
    {"ESP 3D Boxes", ThreeDBoxesModule},
    {"ESP Filled Boxes", FilledBoxesModule},
    {"ESP Labels", LabelsModule},
    {"ESP Distance", DistanceModule},
    {"ESP Tracers", TracerModule},
    {"Monster ESP", MonstersModule},
    {"Player ESP", PlayersModule},
    {"Entity Speed ESP", EntitySpeedModule},
    {"Item ESP", ItemsModule},
    {"Exit ESP", ExitsModule},
    {"Path to Exit", PathToExitModule},
    {"Class Name ESP", ClassNameModule},
    {"Lever ESP", LeverModule},
    {"Valve ESP", ValveModule},
    {"Dynamic Markers", DynamicModule},
    {"New Spawn ESP", SpawnMarkerModule},
    {"Infinite Stamina", InfiniteStaminaModule},
    {"Auto Sprint", AutoSprintModule},
    {"BHop", BunnyHopModule},
    {"Speed", SpeedModule},
    {"Crouch Speed", CrouchModule},
    {"High Jump", JumpModule},
    {"Gravity", GravityModule},
    {"Fly", FlyModule},
    {"NoClip", NoClipModule},
    {"Air Turn", AirTurnModule},
    {"Vehicle Fly", VehicleFlightModule},
    {"Vehicle NoClip", VehicleNoClipModule},
    {"Spectate Teammates", SpectateTeammatesModule},
    {"Free Camera", FreeCameraModule},
    {"Environment Brightness", NightVisionModule},
    {"Third Person", ThirdPersonModule},
    {"Derp", DerpModule},
    {"ArrayList", ArrayListModule},
    {"Notifications", NotificationsModule},
    {"Range Pickup", PickupModule},
    {"Force Exits", ForceExitsModule},
    {"Member Attributes", MemberAttributesModule},
    {"Host Movement", HostMovementModule},
    {"Host Pickup", HostPickupModule},
}};

int MapConfigCategory(const std::string& category)
{
    if (category == "ESP" || category == "Classes")
        return CategoryEsp;
    if (category == "Movement" || category == "Pickup" || category == "Spectator")
        return CategoryMovement;
    if (category == "Status" || category == "Host")
        return CategoryHost;
    return CategoryVisual;
}

// Config feature names that already have a curated row under a different label.
const char* CuratedAlias(const std::string& feature)
{
    if (feature == "Jump") return "High Jump";
    if (feature == "Flight") return "Fly";
    if (feature == "Classes") return "Class Name ESP";
    if (feature == "No Clip") return "NoClip";
    if (feature == "Member Attributes") return "Member Attributes";
    if (feature == "Vehicle Flight") return "Vehicle Fly";
    if (feature == "Vehicle No Clip") return "Vehicle NoClip";
    if (feature == "Movement") return "Host Movement";
    return nullptr;
}

bool FindCuratedIndex(const std::string& name, std::size_t& index)
{
    for (std::size_t i = 0; i < kCuratedModules.size(); ++i)
    {
        if (kCuratedModules[i].query != nullptr && kCuratedModules[i].text == name)
        {
            index = i;
            return true;
        }
    }
    return false;
}

// Auto-syncs every CFG_VAR into the list: one row per feature that has a master toggle,
// plus one row per parameter (shown while its feature is enabled). New features therefore
// appear without touching this file.
void BuildConfigModules(std::vector<ModuleDescriptor>& modules)
{
    struct Group
    {
        std::string feature;
        bool* toggle = nullptr;
        int keybind = 0;
        int category = CategoryVisual;
        std::vector<::etb::core::config::IConfigItem*> params;
    };

    std::vector<Group> groups;
    for (::etb::core::config::IConfigItem* item : ::etb::core::config::Items())
    {
        if (item == nullptr || item->feature_name.empty() || item->IsColor())
            continue;

        Group* group = nullptr;
        for (Group& candidate : groups)
        {
            if (candidate.feature == item->feature_name)
            {
                group = &candidate;
                break;
            }
        }
        if (group == nullptr)
        {
            groups.emplace_back();
            group = &groups.back();
            group->feature = item->feature_name;
            group->category = MapConfigCategory(item->category);
        }

        if (!item->is_param)
        {
            if (item->IsBool() && group->toggle == nullptr)
            {
                group->toggle = static_cast<bool*>(item->RawPtr());
                group->keybind = item->hotkey;
            }
        }
        else
        {
            group->params.push_back(item);
        }
    }

    for (Group& group : groups)
    {
        if (group.toggle == nullptr)
            continue; // no master switch -> nothing to toggle, nothing to show

        // Curated rows already represent this feature; don't add a second row for it.
        // Compare toggle pointers as well as names, because a curated row may use a
        // different label (Vehicle Flight vs Vehicle Fly). Duplicate rows also produced
        // duplicate toggle notifications when the key was pressed.
        bool alreadyCovered = false;
        for (const ModuleDescriptor& existing : modules)
        {
            if (existing.query == nullptr)
                continue;
            const ModuleSnapshot snapshot = existing.query();
            if (snapshot.toggle != nullptr && snapshot.toggle == group.toggle)
            {
                alreadyCovered = true;
                break;
            }
        }
        if (alreadyCovered)
            continue;

        const char* alias = CuratedAlias(group.feature);
        std::size_t gate = NoGate;
        if (FindCuratedIndex(alias != nullptr ? alias : group.feature, gate))
            continue;

        if (modules.size() >= ArrayListHud::ModuleCount)
            break;

        // Only switchable features get a row. Parameters (and enabled sub-features) are
        // folded into this row's right-hand text instead of being listed separately.
        std::string summary;
        for (::etb::core::config::IConfigItem* param : group.params)
        {
            std::string part;
            if (param->IsBool())
            {
                if (!param->IsBoolAndTrue())
                    continue; // only show sub-features that are currently on
                part = param->param_name;
            }
            else
            {
                const std::string value = param->GetStringValue();
                if (value.empty())
                    continue;
                part = param->param_name.empty() ? value : (param->param_name + " " + value);
            }
            if (part.empty())
                continue;
            if (!summary.empty())
                summary += " ";
            summary += part;
            if (summary.size() > 44)
                break;
        }

        ModuleDescriptor descriptor;
        descriptor.text = group.feature;
        descriptor.toggle = group.toggle;
        descriptor.keybind = group.keybind;
        descriptor.category = group.category;
        descriptor.valueText = std::move(summary);
        modules.push_back(std::move(descriptor));
    }
}

std::vector<ModuleDescriptor>& Modules()
{
    static std::vector<ModuleDescriptor> modules;
    static bool built = false;
    if (!built)
    {
        built = true;
        modules.reserve(ArrayListHud::ModuleCount);
        for (const ModuleDescriptor& curated : kCuratedModules)
            modules.push_back(curated);
        BuildConfigModules(modules);
    }
    return modules;
}

ModuleSnapshot SnapshotFor(const ModuleDescriptor& descriptor,
                           const std::vector<ModuleDescriptor>& modules)
{
    if (descriptor.query != nullptr)
        return descriptor.query();

    ModuleSnapshot snapshot;
    snapshot.toggle = descriptor.toggle;
    snapshot.keybind = descriptor.keybind;
    snapshot.category = descriptor.category;

    if (descriptor.gate != NoGate && descriptor.gate < modules.size())
    {
        const ModuleDescriptor& gate = modules[descriptor.gate];
        snapshot.enabled = gate.toggle != nullptr ? *gate.toggle : true;
    }
    else
    {
        snapshot.enabled = descriptor.toggle != nullptr ? *descriptor.toggle : true;
    }

    if (descriptor.modeText != nullptr)
    {
        descriptor.modeText(snapshot.modeText.data(), snapshot.modeText.size());
    }
    else if (!descriptor.valueText.empty())
    {
        std::snprintf(snapshot.modeText.data(), snapshot.modeText.size(), "%s",
                      descriptor.valueText.c_str());
    }
    else if (descriptor.config != nullptr)
    {
        if (descriptor.config->IsBool())
            std::snprintf(snapshot.modeText.data(), snapshot.modeText.size(), "%s",
                          descriptor.config->IsBoolAndTrue() ? "On" : "Off");
        else
        {
            const std::string value = descriptor.config->GetStringValue();
            if (!value.empty())
                std::snprintf(snapshot.modeText.data(), snapshot.modeText.size(), "%s",
                              value.c_str());
        }
    }
    return snapshot;
}

void GetKeybindName(const int key, char* destination, const std::size_t capacity)
{
    if (destination == nullptr || capacity == 0)
        return;
    destination[0] = '\0';

    switch (key)
    {
    case 0: return;
    case VK_LBUTTON: std::snprintf(destination, capacity, "Mouse 1"); return;
    case VK_RBUTTON: std::snprintf(destination, capacity, "Mouse 2"); return;
    case VK_MBUTTON: std::snprintf(destination, capacity, "Mouse 3"); return;
    case VK_XBUTTON1: std::snprintf(destination, capacity, "Mouse 4"); return;
    case VK_XBUTTON2: std::snprintf(destination, capacity, "Mouse 5"); return;
    case VK_INSERT: std::snprintf(destination, capacity, "Insert"); return;
    case VK_DELETE: std::snprintf(destination, capacity, "Delete"); return;
    case VK_HOME: std::snprintf(destination, capacity, "Home"); return;
    case VK_END: std::snprintf(destination, capacity, "End"); return;
    case VK_PRIOR: std::snprintf(destination, capacity, "Page Up"); return;
    case VK_NEXT: std::snprintf(destination, capacity, "Page Down"); return;
    default: break;
    }

    UINT scanCode = MapVirtualKeyA(static_cast<UINT>(key), MAPVK_VK_TO_VSC);
    if (key == VK_LEFT || key == VK_UP || key == VK_RIGHT || key == VK_DOWN ||
        key == VK_RCONTROL || key == VK_RMENU || key == VK_LWIN || key == VK_RWIN)
    {
        scanCode |= 0x100;
    }

    if (GetKeyNameTextA(static_cast<LONG>(scanCode << 16), destination,
                        static_cast<int>(capacity)) <= 0)
    {
        std::snprintf(destination, capacity, "Key %d", key);
    }
}

ImU32 PackColor(const std::array<float, 4>& color)
{
    return ImGui::ColorConvertFloat4ToU32(ImVec4(color[0], color[1], color[2], color[3]));
}

ImU32 ShadowColor(const ImU32 color)
{
    const ImVec4 source = ImGui::ColorConvertU32ToFloat4(color);
    return ImGui::ColorConvertFloat4ToU32(
        ImVec4(source.x / 3.5f, source.y / 3.5f, source.z / 3.5f, source.w));
}

ImU32 RainbowColor(const float hueDegrees, const ArrayListRainbow rainbow)
{
    const float saturation = rainbow == ArrayListRainbow::Saturated ? 0.9f : 0.35f;
    float red = 0.0f;
    float green = 0.0f;
    float blue = 0.0f;
    float hue = std::fmod(hueDegrees, 360.0f);
    if (hue < 0.0f)
        hue += 360.0f;
    ImGui::ColorConvertHSVtoRGB(hue / 360.0f, saturation, 1.0f, red, green, blue);
    return ImGui::ColorConvertFloat4ToU32(ImVec4(red, green, blue, 1.0f));
}

float BorderX(const RowLayout& row, const bool leftGravity)
{
    return leftGravity ? row.x + row.width : row.x;
}

void DrawLine(ImDrawList* drawList, const ImVec2& from, const ImVec2& to,
              const ImU32 color, const float thickness)
{
    drawList->AddLine(from, to, color, thickness);
}

void PathQuadratic(ImDrawList* drawList, const ImVec2& control, const ImVec2& end)
{
    drawList->PathBezierQuadraticCurveTo(control, end, 8);
}

void DrawBackgroundConnector(ImDrawList* drawList, const float farX, const float y,
                             const float radius, const bool top, const bool freeRight,
                             const ImU32 color)
{
    if (radius <= 0.0f)
        return;

    const float direction = freeRight ? radius : -radius;
    const float dy = top ? -radius : radius;
    const ImVec2 center(farX + direction, y + dy);
    const float startDegrees = dy > 0.0f ? 270.0f : 90.0f;
    const float sweepDegrees = freeRight ? (top ? 90.0f : -90.0f)
                                         : (top ? -90.0f : 90.0f);

    drawList->PathClear();
    drawList->PathLineTo(ImVec2(farX, y));
    drawList->PathLineTo(ImVec2(farX + direction, y));
    drawList->PathArcTo(center, radius,
                        startDegrees * IM_PI / 180.0f,
                        (startDegrees + sweepDegrees) * IM_PI / 180.0f, 8);
    drawList->PathLineTo(ImVec2(farX, y));
    drawList->PathFillConvex(color);
}

void DrawRowBackground(ImDrawList* drawList, const ArrayListSettings& settings,
                       const RowLayout& row, const float x, const float y, const float height,
                       const std::size_t index, const std::size_t count,
                       const RowLayout* previous, const RowLayout* next)
{
    const ImU32 background = PackColor(settings.backgroundColor);
    if (settings.mode != ArrayListMode::Rounded)
    {
        drawList->AddRectFilled(ImVec2(x, y), ImVec2(x + row.width, y + height), background);
        return;
    }

    const bool leftGravity = settings.horizontalGravity == ArrayListHorizontalGravity::Left;
    const bool topGravity = settings.verticalGravity == ArrayListVerticalGravity::Top;
    const bool freeRight = leftGravity;
    const float radius = std::min(settings.cornerRadius, std::min(row.width, height) * 0.5f);

    const bool longerThanPrevious = previous != nullptr && row.width > previous->width;
    const bool longerThanNext = next != nullptr && row.width > next->width;
    const bool roundTop = (index == 0 && !topGravity) || longerThanPrevious;
    const bool roundBottom = (index + 1 == count && topGravity) || longerThanNext;
    const float topRadius = longerThanPrevious
        ? std::min(radius, std::abs(row.width - previous->width) * 0.5f) : radius;
    const float bottomRadius = longerThanNext
        ? std::min(radius, std::abs(row.width - next->width) * 0.5f) : radius;

    const bool roundTopLeft = roundTop && !freeRight;
    const bool roundTopRight = roundTop && freeRight;
    const bool roundBottomLeft = roundBottom && !freeRight;
    const bool roundBottomRight = roundBottom && freeRight;

    drawList->PathClear();
    drawList->PathLineTo(ImVec2(x + (roundTopLeft ? topRadius : 0.0f), y));
    drawList->PathLineTo(ImVec2(x + row.width - (roundTopRight ? topRadius : 0.0f), y));
    if (roundTopRight)
        PathQuadratic(drawList, ImVec2(x + row.width, y), ImVec2(x + row.width, y + topRadius));
    drawList->PathLineTo(ImVec2(x + row.width,
                                y + height - (roundBottomRight ? bottomRadius : 0.0f)));
    if (roundBottomRight)
        PathQuadratic(drawList, ImVec2(x + row.width, y + height),
                      ImVec2(x + row.width - bottomRadius, y + height));
    drawList->PathLineTo(ImVec2(x + (roundBottomLeft ? bottomRadius : 0.0f), y + height));
    if (roundBottomLeft)
        PathQuadratic(drawList, ImVec2(x, y + height), ImVec2(x, y + height - bottomRadius));
    drawList->PathLineTo(ImVec2(x, y + (roundTopLeft ? topRadius : 0.0f)));
    if (roundTopLeft)
        PathQuadratic(drawList, ImVec2(x, y), ImVec2(x + topRadius, y));
    drawList->PathFillConvex(background);

    if (longerThanPrevious)
        DrawBackgroundConnector(drawList, BorderX(*previous, leftGravity), y,
                                topRadius, true, freeRight, background);
    if (longerThanNext)
        DrawBackgroundConnector(drawList, BorderX(*next, leftGravity), y + height,
                                bottomRadius, false, freeRight, background);
}

void DrawCornerArc(ImDrawList* drawList, const int type, const float cornerX,
                   const float cornerY, const float radius, const ImU32 color,
                   const float thickness)
{
    float centerX = 0.0f;
    float centerY = 0.0f;
    float startDegrees = 0.0f;
    switch (type)
    {
    case 0: centerX = cornerX + radius; centerY = cornerY + radius; startDegrees = 180.0f; break;
    case 1: centerX = cornerX - radius; centerY = cornerY + radius; startDegrees = 270.0f; break;
    case 2: centerX = cornerX - radius; centerY = cornerY - radius; startDegrees = 0.0f; break;
    default: centerX = cornerX + radius; centerY = cornerY - radius; startDegrees = 90.0f; break;
    }

    drawList->PathClear();
    drawList->PathArcTo(ImVec2(centerX, centerY), radius,
                        startDegrees * IM_PI / 180.0f,
                        (startDegrees + 90.0f) * IM_PI / 180.0f, 8);
    drawList->PathStroke(color, 0, thickness);
}

void DrawConnector(ImDrawList* drawList, const float longerX, const float shorterX,
                   const float y, const float radius, const bool top, const bool freeRight,
                   const ImU32 color, const float thickness)
{
    if (radius <= 0.0f)
    {
        DrawLine(drawList, ImVec2(longerX, y), ImVec2(shorterX, y), color, thickness);
        return;
    }

    const float lineFrom = longerX < shorterX ? longerX + radius : longerX - radius;
    const float lineTo = shorterX > longerX ? shorterX - radius : shorterX + radius;
    DrawLine(drawList, ImVec2(lineFrom, y), ImVec2(lineTo, y), color, thickness);

    int longerArc = 0;
    int shorterArc = 0;
    if (top)
    {
        longerArc = freeRight ? 1 : 0;
        shorterArc = freeRight ? 3 : 2;
    }
    else
    {
        longerArc = freeRight ? 2 : 3;
        shorterArc = freeRight ? 0 : 1;
    }
    DrawCornerArc(drawList, longerArc, longerX, y, radius, color, thickness);
    DrawCornerArc(drawList, shorterArc, shorterX, y, radius, color, thickness);
}

void DrawRowBorder(ImDrawList* drawList, const ArrayListSettings& settings,
                   const RowLayout& row, const float x, const float y, const float height,
                   const std::size_t index, const std::size_t count,
                   const RowLayout* previous, const RowLayout* next,
                   const ImU32 color, const float thickness)
{
    const bool leftGravity = settings.horizontalGravity == ArrayListHorizontalGravity::Left;
    const bool topGravity = settings.verticalGravity == ArrayListVerticalGravity::Top;

    if (settings.mode == ArrayListMode::None)
        return;
    if (settings.mode == ArrayListMode::EdgeLine)
    {
        const float lineX = leftGravity ? x + row.width : x;
        DrawLine(drawList, ImVec2(lineX, y), ImVec2(lineX, y + height), color, thickness);
        return;
    }

    const bool freeRight = leftGravity;
    const float radius = settings.mode == ArrayListMode::Rounded
        ? std::min(settings.cornerRadius, std::min(row.width, height) * 0.5f) : 0.0f;
    const float borderX = freeRight ? x + row.width : x;
    const bool topOuter = index == 0 && !topGravity;
    const bool bottomOuter = index + 1 == count && topGravity;
    const bool widthChangeTop = previous != nullptr && previous->width != row.width;
    const bool widthChangeBottom = next != nullptr && next->width != row.width;
    const float topRadius = widthChangeTop
        ? std::min(radius, std::abs(row.width - previous->width) * 0.5f)
        : (topOuter ? radius : 0.0f);
    const float bottomRadius = widthChangeBottom
        ? std::min(radius, std::abs(row.width - next->width) * 0.5f)
        : (bottomOuter ? radius : 0.0f);

    DrawLine(drawList, ImVec2(borderX, y + topRadius),
             ImVec2(borderX, y + height - bottomRadius), color, thickness);

    if (topOuter)
    {
        if (freeRight)
        {
            DrawLine(drawList, ImVec2(x, y), ImVec2(x + row.width - radius, y), color, thickness);
            if (radius > 0.0f)
                DrawCornerArc(drawList, 1, x + row.width, y, radius, color, thickness);
        }
        else
        {
            DrawLine(drawList, ImVec2(x + radius, y), ImVec2(x + row.width, y), color, thickness);
            if (radius > 0.0f)
                DrawCornerArc(drawList, 0, x, y, radius, color, thickness);
        }
    }

    if (bottomOuter)
    {
        if (freeRight)
        {
            DrawLine(drawList, ImVec2(x, y + height),
                     ImVec2(x + row.width - radius, y + height), color, thickness);
            if (radius > 0.0f)
                DrawCornerArc(drawList, 2, x + row.width, y + height, radius, color, thickness);
        }
        else
        {
            DrawLine(drawList, ImVec2(x + radius, y + height),
                     ImVec2(x + row.width, y + height), color, thickness);
            if (radius > 0.0f)
                DrawCornerArc(drawList, 3, x, y + height, radius, color, thickness);
        }
    }

    if (previous != nullptr && row.width > previous->width)
        DrawConnector(drawList, borderX, BorderX(*previous, leftGravity), y,
                      topRadius, true, freeRight, color, thickness);
    if (next != nullptr && row.width > next->width)
        DrawConnector(drawList, borderX, BorderX(*next, leftGravity), y + height,
                      bottomRadius, false, freeRight, color, thickness);
}

void DrawTextWithShadow(ImDrawList* drawList, ImFont* font, const float textSize,
                        const ImVec2 position, const char* text, const ImU32 color,
                        const bool shadowEnabled, const float shadowOffset)
{
    if (text == nullptr || text[0] == '\0')
        return;
    if (shadowEnabled)
        drawList->AddText(font, textSize,
                          ImVec2(position.x + shadowOffset, position.y + shadowOffset),
                          ShadowColor(color), text);
    drawList->AddText(font, textSize, position, color, text);
}
}

ArrayListHud& ArrayListHud::Instance()
{
    static ArrayListHud instance;
    return instance;
}

void ArrayListHud::Draw(const bool menuVisible)
{
    if (!settings_.enabled)
        return;

    ImGuiIO& io = ImGui::GetIO();
    ImFont* font = ImGui::GetFont();
    if (font == nullptr || io.DisplaySize.x <= 0.0f || io.DisplaySize.y <= 0.0f)
        return;

    const Clock::time_point now = Clock::now();
    float layoutDelta = 0.0f;
    if (hasLayoutTime_)
        layoutDelta = std::clamp(SecondsBetween(now, lastLayoutTime_), 0.0f, 0.050f);
    else
        hasLayoutTime_ = true;
    lastLayoutTime_ = now;

    if (settings_.color == ArrayListColor::Rainbow)
    {
        float rainbowDelta = 0.0f;
        if (hasRainbowTime_)
            rainbowDelta = std::clamp(SecondsBetween(now, lastRainbowTime_), 0.0f, 0.050f);
        else
            hasRainbowTime_ = true;
        lastRainbowTime_ = now;
        baseHueDegrees_ = std::fmod(baseHueDegrees_ + rainbowDelta * HueSpeedDegreesPerSecond, 360.0f);
    }

    const bool gravityChanged = settings_.horizontalGravity != lastHorizontalGravity_ ||
                                settings_.verticalGravity != lastVerticalGravity_;
    if (gravityChanged)
    {
        if (settings_.gravityChangingAnimationEnabled)
        {
            gravityFromHorizontal_ = lastHorizontalGravity_;
            gravityFromVertical_ = lastVerticalGravity_;
            gravityTransition_ = true;
            gravityStart_ = now;
        }
        else
        {
            for (ItemRuntime& item : items_)
                item.hasCurrentY = false;
            gravityTransition_ = false;
        }
        lastHorizontalGravity_ = settings_.horizontalGravity;
        lastVerticalGravity_ = settings_.verticalGravity;
    }

    float gravityProgress = 1.0f;
    if (gravityTransition_)
    {
        gravityProgress = DecelerateFactor2(
            SecondsBetween(now, gravityStart_) / GravitySlideDurationSeconds);
        if (gravityProgress >= 1.0f)
            gravityTransition_ = false;
    }

    const std::vector<ModuleDescriptor>& modules = Modules();
    for (std::size_t id = 0; id < modules.size() && id < items_.size(); ++id)
    {
        const ModuleSnapshot snapshot = SnapshotFor(modules[id], modules);
        ItemRuntime& runtime = items_[id];
        if (snapshot.enabled)
        {
            if (!runtime.present)
            {
                runtime = {};
                runtime.present = true;
                runtime.entering = true;
                runtime.slideProgress = 0.0f;
                runtime.enterStart = now;
            }
            else if (runtime.exiting || runtime.waitingExitFinish)
            {
                runtime.exiting = false;
                runtime.waitingExitFinish = false;
                runtime.exitProgress = 0.0f;
            }
            runtime.keybind = snapshot.keybind;
            runtime.category = snapshot.category;
            runtime.modeText = snapshot.modeText;
        }
        else if (runtime.present && !runtime.exiting && !runtime.waitingExitFinish)
        {
            runtime.exiting = true;
            runtime.exitProgress = 0.0f;
            runtime.exitStart = now;
        }

        if (runtime.entering)
        {
            runtime.slideProgress = DecelerateFactor2(
                SecondsBetween(now, runtime.enterStart) / SlideDurationSeconds);
            if (runtime.slideProgress >= 1.0f)
                runtime.entering = false;
        }
        if (runtime.exiting)
        {
            runtime.exitProgress = DecelerateFactor2(
                SecondsBetween(now, runtime.exitStart) / SlideDurationSeconds);
        }
        if (runtime.waitingExitFinish && now >= runtime.exitFinishAt)
            runtime = {};
    }

    const float textSize = std::clamp(settings_.textSize, 6.0f, 64.0f);
    const float rowPadding = std::max(0.0f, settings_.rowPadding);
    ImFontBaked* bakedFont = font->GetFontBaked(textSize);
    if (bakedFont == nullptr)
        return;
    const float fontHeight = bakedFont->Ascent - bakedFont->Descent;
    const float rowHeight = fontHeight + rowPadding;
    const bool rightGravity = settings_.horizontalGravity == ArrayListHorizontalGravity::Right;
    const bool topGravity = settings_.verticalGravity == ArrayListVerticalGravity::Top;

    std::array<RowLayout, ModuleCount> rows{};
    std::size_t rowCount = 0;
    for (std::size_t id = 0; id < items_.size(); ++id)
    {
        const ItemRuntime& runtime = items_[id];
        if (!runtime.present)
            continue;

        RowLayout& row = rows[rowCount++];
        row.id = id;
        std::snprintf(row.mainText.data(), row.mainText.size(), "%s",
                      id < modules.size() ? modules[id].text.c_str() : "");
        if (settings_.showKeybinds && runtime.keybind != 0)
        {
            std::array<char, 32> keyName{};
            GetKeybindName(runtime.keybind, keyName.data(), keyName.size());
            const std::size_t used = std::strlen(row.mainText.data());
            std::snprintf(row.mainText.data() + used, row.mainText.size() - used,
                          " [%s]", keyName.data());
        }
        if (settings_.showModes && runtime.modeText[0] != '\0')
            std::snprintf(row.modeText.data(), row.modeText.size(), "%s", runtime.modeText.data());

        const float mainWidth = font->CalcTextSizeA(textSize, FLT_MAX, 0.0f,
                                                     row.mainText.data()).x;
        const float modeWidth = row.modeText[0] == '\0' ? 0.0f
            : font->CalcTextSizeA(textSize, FLT_MAX, 0.0f, row.modeText.data()).x;
        const float modeGap = modeWidth > 0.0f ? std::max(3.0f, textSize * 0.30f) : 0.0f;
        row.width = mainWidth + modeGap + modeWidth + rowPadding * 2.0f;
    }

    std::sort(rows.begin(), rows.begin() + rowCount,
              [topGravity](const RowLayout& left, const RowLayout& right)
              {
                  if (left.width == right.width)
                      return left.id < right.id;
                  return topGravity ? left.width > right.width : left.width < right.width;
              });

    const float contentHeight = rowHeight * static_cast<float>(rowCount);
    float y = !topGravity && io.DisplaySize.y >= contentHeight
        ? io.DisplaySize.y - contentHeight : 0.0f;
    for (std::size_t index = 0; index < rowCount; ++index)
    {
        rows[index].x = rightGravity ? io.DisplaySize.x - rows[index].width : 0.0f;
        rows[index].y = y;
        y += rowHeight;
    }

    std::array<RowLayout, ModuleCount> oldRows{};
    if (gravityTransition_)
    {
        oldRows = rows;
        const bool oldRight = gravityFromHorizontal_ == ArrayListHorizontalGravity::Right;
        const bool oldTop = gravityFromVertical_ == ArrayListVerticalGravity::Top;
        std::sort(oldRows.begin(), oldRows.begin() + rowCount,
                  [oldTop](const RowLayout& left, const RowLayout& right)
                  {
                      if (left.width == right.width)
                          return left.id < right.id;
                      return oldTop ? left.width > right.width : left.width < right.width;
                  });
        const float oldContentHeight = rowHeight * static_cast<float>(rowCount);
        float oldY = !oldTop && io.DisplaySize.y >= oldContentHeight
            ? io.DisplaySize.y - oldContentHeight : 0.0f;
        for (std::size_t index = 0; index < rowCount; ++index)
        {
            oldRows[index].x = oldRight ? io.DisplaySize.x - oldRows[index].width : 0.0f;
            oldRows[index].y = oldY;
            oldY += rowHeight;
        }
    }

    std::array<float, ModuleCount> positions{};
    std::array<float, ModuleCount> rowY{};
    const float layoutAlpha = std::clamp(layoutDelta * LayoutSmoothRate, 0.0f, 1.0f);
    for (std::size_t index = 0; index < rowCount; ++index)
    {
        RowLayout& row = rows[index];
        ItemRuntime& runtime = items_[row.id];
        float targetX = row.x;
        float targetY = row.y;

        if (gravityTransition_)
        {
            const auto old = std::find_if(oldRows.begin(), oldRows.begin() + rowCount,
                                          [&row](const RowLayout& candidate)
                                          { return candidate.id == row.id; });
            if (old != oldRows.begin() + rowCount)
            {
                targetX = old->x + (row.x - old->x) * gravityProgress;
                targetY = old->y + (row.y - old->y) * gravityProgress;
            }
            runtime.currentY = targetY;
            runtime.hasCurrentY = true;
            rowY[index] = targetY;
        }
        else if (!runtime.hasCurrentY || !settings_.itemClosingAnimationEnabled)
        {
            runtime.currentY = targetY;
            runtime.hasCurrentY = true;
            rowY[index] = targetY;
        }
        else
        {
            const float smoothed = runtime.currentY + (targetY - runtime.currentY) * layoutAlpha;
            if (std::abs(targetY - smoothed) < 0.5f)
                runtime.currentY = targetY;
            else
                runtime.currentY = smoothed;
            rowY[index] = runtime.currentY;
        }

        const float direction = rightGravity ? 1.0f : -1.0f;
        positions[index] = targetX + direction * io.DisplaySize.x * (1.0f - runtime.slideProgress) +
                           direction * io.DisplaySize.x * runtime.exitProgress;
    }

    auto colorFor = [this](const ItemRuntime& item, const std::size_t index) -> ImU32
    {
        switch (settings_.color)
        {
        case ArrayListColor::Rainbow:
            return RainbowColor(baseHueDegrees_ + static_cast<float>(index) *
                                settings_.rainbowStepDegrees, settings_.rainbow);
        case ArrayListColor::Categorized:
        {
            const std::size_t category = std::clamp(item.category, 0, 3);
            return PackColor(settings_.categoryColors[category]);
        }
        case ArrayListColor::Custom:
        default:
            return PackColor(settings_.customColor);
        }
    };

    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    for (std::size_t index = 0; index < rowCount; ++index)
    {
        DrawRowBackground(drawList, settings_, rows[index], positions[index], rowY[index],
                          rowHeight, index, rowCount,
                          index > 0 ? &rows[index - 1] : nullptr,
                          index + 1 < rowCount ? &rows[index + 1] : nullptr);
    }

    const float shadowOffset = textSize / 10.0f;
    for (std::size_t index = 0; index < rowCount; ++index)
    {
        const RowLayout& row = rows[index];
        const ItemRuntime& runtime = items_[row.id];
        const ImU32 mainColor = colorFor(runtime, index);
        const ImU32 modeColor = PackColor(settings_.modeTextColor);
        const float mainWidth = font->CalcTextSizeA(textSize, FLT_MAX, 0.0f,
                                                     row.mainText.data()).x;
        const float modeWidth = row.modeText[0] == '\0' ? 0.0f
            : font->CalcTextSizeA(textSize, FLT_MAX, 0.0f, row.modeText.data()).x;
        const float modeGap = modeWidth > 0.0f ? std::max(3.0f, textSize * 0.30f) : 0.0f;
        const float textY = rowY[index] + (rowHeight - fontHeight) * 0.5f;

        float mainX = positions[index] + rowPadding;
        float modeX = mainX + mainWidth + modeGap;
        if (rightGravity)
        {
            modeX = positions[index] + row.width - rowPadding - modeWidth;
            mainX = modeX - modeGap - mainWidth;
        }
        DrawTextWithShadow(drawList, font, textSize, ImVec2(mainX, textY), row.mainText.data(),
                           mainColor, settings_.textShadowEnabled, shadowOffset);
        DrawTextWithShadow(drawList, font, textSize, ImVec2(modeX, textY), row.modeText.data(),
                           modeColor, settings_.textShadowEnabled, shadowOffset);
    }

    const float borderThickness = settings_.borderThickness > 0.0f
        ? settings_.borderThickness : textSize / 10.0f;
    for (std::size_t index = 0; index < rowCount; ++index)
    {
        const RowLayout& row = rows[index];
        const ItemRuntime& runtime = items_[row.id];
        DrawRowBorder(drawList, settings_, row, positions[index], rowY[index], rowHeight,
                      index, rowCount,
                      index > 0 ? &rows[index - 1] : nullptr,
                      index + 1 < rowCount ? &rows[index + 1] : nullptr,
                      colorFor(runtime, index), borderThickness);
    }

    (void)menuVisible;
    (void)settings_.touchModelEnabled;

    for (std::size_t index = 0; index < rowCount; ++index)
    {
        ItemRuntime& runtime = items_[rows[index].id];
        if (!runtime.exiting)
            continue;

        const bool fullyOut = rightGravity
            ? rows[index].x + io.DisplaySize.x * runtime.exitProgress >= io.DisplaySize.x
            : rows[index].x - io.DisplaySize.x * runtime.exitProgress + rows[index].width <= 0.0f;
        if (!fullyOut && runtime.exitProgress < 1.0f)
            continue;

        runtime.exiting = false;
        runtime.waitingExitFinish = true;
        runtime.exitFinishAt = now + std::chrono::duration_cast<Clock::duration>(
            std::chrono::duration<float>(settings_.itemClosingAnimationEnabled
                                             ? 0.0f : ExitFinishDelaySeconds));
    }

    DrawSelfInfo();
}

void ArrayListHud::DrawSelfInfo()
{
    if (!settings_.selfInfoEnabled)
        return;

    ImGuiIO& io = ImGui::GetIO();
    ImFont* font = ImGui::GetFont();
    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    if (font == nullptr || drawList == nullptr || io.DisplaySize.x <= 0.0f ||
        io.DisplaySize.y <= 0.0f)
        return;

    const MovementStatus& status = Movement::Instance().Status();
    std::array<std::array<char, 160>, 8> lines{};
    std::size_t lineCount = 0;
    auto addLine = [&lines, &lineCount](const char* format, auto... arguments)
    {
        if (lineCount >= lines.size())
            return;
        std::snprintf(lines[lineCount].data(), lines[lineCount].size(), format, arguments...);
        ++lineCount;
    };

    if (settings_.selfInfoFps)
        addLine("FPS  %.1f", io.Framerate);
    if (settings_.selfInfoSpeed && status.telemetryReady)
        addLine("Speed  %.2f m/s  Total %.2f m/s", status.horizontalSpeed, status.totalSpeed);
    if (settings_.selfInfoVelocity && status.telemetryReady)
        addLine("Velocity  X %.2f  Y %.2f  Z %.2f m/s",
                status.velocityX, status.velocityY, status.velocityZ);
    if (settings_.selfInfoPosition && status.positionReady)
        addLine("Position  X %.2f  Y %.2f  Z %.2f m",
                status.positionX, status.positionY, status.positionZ);
    if (settings_.selfInfoMovementMode && status.telemetryReady)
    {
        const char* movementMode = "Unknown";
        switch (status.movementMode)
        {
        case 0: movementMode = "None"; break;
        case 1: movementMode = "Walking"; break;
        case 2: movementMode = "Nav Walking"; break;
        case 3: movementMode = "Falling"; break;
        case 4: movementMode = "Swimming"; break;
        case 5: movementMode = "Flying"; break;
        case 6: movementMode = "Custom"; break;
        default: break;
        }
        addLine("Movement  %s", movementMode);
    }
    if (lineCount == 0)
        return;

    const float textSize = std::clamp(settings_.selfInfoTextSize, 8.0f, 32.0f);
    const float padding = std::clamp(settings_.selfInfoPadding, 0.0f, 20.0f);
    ImFontBaked* bakedFont = font->GetFontBaked(textSize);
    if (bakedFont == nullptr)
        return;
    const float fontHeight = bakedFont->Ascent - bakedFont->Descent;
    const float rowHeight = fontHeight + 3.0f;
    float maximumWidth = 0.0f;
    for (std::size_t index = 0; index < lineCount; ++index)
    {
        maximumWidth = std::max(maximumWidth,
            font->CalcTextSizeA(textSize, FLT_MAX, 0.0f, lines[index].data()).x);
    }

    const ImVec2 panelSize(maximumWidth + padding * 2.0f,
                           rowHeight * static_cast<float>(lineCount) + padding * 2.0f);
    constexpr float margin = 8.0f;
    const bool alignRight = settings_.selfInfoHorizontalGravity == ArrayListHorizontalGravity::Right;
    const bool alignBottom = settings_.selfInfoVerticalGravity == ArrayListVerticalGravity::Bottom;
    constexpr float lowerMiddleCenterRatio = 0.64f;
    const float lowerMiddleY = std::clamp(
        io.DisplaySize.y * lowerMiddleCenterRatio - panelSize.y * 0.5f,
        margin, std::max(margin, io.DisplaySize.y - panelSize.y - margin));
    const ImVec2 panelMinimum(
        alignRight ? io.DisplaySize.x - panelSize.x - margin : margin,
        alignBottom ? lowerMiddleY : margin);
    const ImVec2 panelMaximum(panelMinimum.x + panelSize.x, panelMinimum.y + panelSize.y);
    drawList->AddRectFilled(panelMinimum, panelMaximum,
                            PackColor(settings_.selfInfoBackground), 4.0f);

    ImU32 accent = IM_COL32_WHITE;
    switch (settings_.color)
    {
    case ArrayListColor::Rainbow:
        accent = RainbowColor(baseHueDegrees_, settings_.rainbow);
        break;
    case ArrayListColor::Categorized:
        accent = PackColor(settings_.categoryColors[CategoryMovement]);
        break;
    case ArrayListColor::Custom:
        accent = PackColor(settings_.customColor);
        break;
    }
    const float accentWidth = std::max(1.0f, settings_.borderThickness > 0.0f
        ? settings_.borderThickness : textSize / 10.0f);
    if (alignRight)
        drawList->AddRectFilled(ImVec2(panelMaximum.x - accentWidth, panelMinimum.y),
                                panelMaximum, accent, 4.0f);
    else
        drawList->AddRectFilled(panelMinimum,
                                ImVec2(panelMinimum.x + accentWidth, panelMaximum.y), accent, 4.0f);

    const float shadowOffset = textSize / 10.0f;
    for (std::size_t index = 0; index < lineCount; ++index)
    {
        const float textWidth = font->CalcTextSizeA(textSize, FLT_MAX, 0.0f,
                                                    lines[index].data()).x;
        const float x = alignRight
            ? panelMaximum.x - padding - accentWidth - textWidth
            : panelMinimum.x + padding + accentWidth;
        const float y = panelMinimum.y + padding + rowHeight * static_cast<float>(index);
        DrawTextWithShadow(drawList, font, textSize, ImVec2(x, y), lines[index].data(),
                           accent, settings_.textShadowEnabled, shadowOffset);
    }
}

void ArrayListHud::Reset()
{
    items_.fill({});
    baseHueDegrees_ = 0.0f;
    hasRainbowTime_ = false;
    hasLayoutTime_ = false;
    gravityTransition_ = false;
    lastHorizontalGravity_ = settings_.horizontalGravity;
    lastVerticalGravity_ = settings_.verticalGravity;
    gravityFromHorizontal_ = settings_.horizontalGravity;
    gravityFromVertical_ = settings_.verticalGravity;
}

bool ArrayListHud::RegisterModule(const ModuleSpec& spec)
{
    std::vector<ModuleDescriptor>& modules = Modules();
    if (modules.size() >= ModuleCount)
        return false;

    ModuleDescriptor descriptor;
    descriptor.text = spec.name != nullptr ? spec.name : "";
    descriptor.toggle = spec.toggle;
    descriptor.keybind = spec.keybind;
    descriptor.category = static_cast<int>(spec.category);
    descriptor.modeText = spec.modeText;
    modules.push_back(std::move(descriptor));
    return true;
}

std::size_t ArrayListHud::ModuleTotal() const noexcept
{
    return Modules().size();
}

void ArrayListHud::CaptureModuleStates(std::array<HudModuleState, ModuleCount>& states) const
{
    const std::vector<ModuleDescriptor>& modules = Modules();
    for (std::size_t id = 0; id < modules.size() && id < states.size(); ++id)
    {
        const ModuleSnapshot snapshot = SnapshotFor(modules[id], modules);
        states[id] = HudModuleState{
            modules[id].text.c_str(),
            snapshot.toggle != nullptr ? *snapshot.toggle : snapshot.enabled
        };
    }
}
}

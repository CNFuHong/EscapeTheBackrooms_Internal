#pragma once

#include "core/config.hpp"

#include <cstddef>
#include <cstdint>

namespace etb::features
{
struct EspSettings
{
    CFG_VAR("esp_enabled", bool, enabled, true, false, "ESP", "", "ESP");
    CFG_VAR("esp_boxes", bool, boxes, true, true, "ESP", "Boxes", "ESP");
    CFG_VAR("esp_3d_boxes", bool, threeDBoxes, false, true, "ESP", "3D Boxes", "ESP");
    CFG_VAR("esp_filled_boxes", bool, filledBoxes, true, true, "ESP", "Filled Boxes", "ESP");
    CFG_VAR("esp_labels", bool, labels, true, true, "ESP", "Labels", "ESP");
    CFG_VAR("esp_distance", bool, distance, true, true, "ESP", "Distance", "ESP");
    CFG_VAR("esp_tracers", bool, tracers, false, true, "ESP", "Tracers", "ESP");
    CFG_VAR("esp_monster_tracers_only", bool, monsterTracersOnly, true, true, "ESP", "Monster Tracers Only", "ESP");
    CFG_VAR("esp_monsters", bool, monsters, true, true, "ESP", "Monsters", "ESP");
    CFG_VAR("esp_players", bool, players, true, true, "ESP", "Players", "ESP");
    CFG_VAR("esp_items", bool, items, true, true, "ESP", "Items", "ESP");
    CFG_VAR("esp_item_merge_screen", float, itemMergeScreenDistancePixels, 150.0f, true, "ESP", "Item Merge Screen", "ESP");
    CFG_VAR("esp_item_merge_depth", float, itemMergeDepthMeters, 10.0f, true, "ESP", "Item Merge Depth", "ESP");
    CFG_VAR("esp_exits", bool, exits, true, true, "ESP", "Exits", "ESP");
    CFG_VAR("esp_path_to_exit", bool, pathToExit, false, true, "ESP", "Path to Exit", "ESP");
    CFG_VAR("esp_entity_tracking", bool, entityTracking, false, false, "Entity Tracking", "", "ESP");
    CFG_VAR("esp_entity_tracking_count", int, entityTrackingCount, 3, true, "Entity Tracking", "Tracked Entities", "ESP");
    CFG_VAR("esp_player_names", bool, playerNameTags, true, true, "ESP", "Player Names", "ESP");
    CFG_VAR("esp_player_sanity", bool, playerSanity, true, true, "ESP", "Player Sanity", "ESP");
    CFG_VAR("esp_entity_speed", bool, entitySpeed, true, true, "ESP", "Entity Speed", "ESP");
    CFG_VAR("esp_max_distance", float, maxDistanceMeters, 250.0f, true, "ESP", "Maximum Distance", "ESP");
    CFG_VAR("esp_fill_opacity", float, fillOpacity, 0.18f, true, "ESP", "Fill Opacity", "ESP");
    CFG_VAR("esp_card_scale", float, cardScale, 1.0f, true, "ESP", "Card Scale", "ESP");
    CFG_VAR("esp_refresh_interval", int, refreshIntervalMs, 650, true, "ESP", "Refresh Interval", "ESP");
    CFG_VAR("esp_china_hat", bool, chinaHat, true, true, "China Hat", "Enabled", "ESP");
    CFG_VAR("esp_china_hat_self", bool, chinaHatSelf, true, true, "China Hat", "Self", "ESP");
    CFG_VAR("esp_china_hat_monsters", bool, chinaHatMonsters, true, true, "China Hat", "Monsters", "ESP");
    CFG_VAR("esp_china_hat_players", bool, chinaHatPlayers, true, true, "China Hat", "Players", "ESP");
    CFG_VAR("esp_china_hat_color", ::etb::core::config::Color4, chinaHatColor,
            ::etb::core::config::MakeColor(0.94f, 0.82f, 0.47f, 0.92f), true, "China Hat", "Color", "ESP");
    CFG_VAR("esp_china_hat_width", float, chinaHatWidth, 0.85f, true, "China Hat", "Brim Radius", "ESP");
    CFG_VAR("esp_china_hat_height", float, chinaHatHeight, 0.32f, true, "China Hat", "Cone Height", "ESP");
    CFG_VAR("esp_china_hat_droop", float, chinaHatDroop, 0.15f, true, "China Hat", "Brim Droop", "ESP");
    CFG_VAR("esp_china_hat_segments", int, chinaHatSegments, 32, true, "China Hat", "Segments", "ESP");
    CFG_VAR("esp_china_hat_filled", bool, chinaHatFilled, true, true, "China Hat", "Filled", "ESP");
    CFG_VAR("esp_china_hat_fill_opacity", float, chinaHatFillOpacity, 0.30f, true, "China Hat", "Fill Opacity", "ESP");
    CFG_VAR("esp_china_hat_outline", bool, chinaHatOutline, true, true, "China Hat", "Outline", "ESP");
    CFG_VAR("esp_china_hat_thickness", float, chinaHatThickness, 1.6f, true, "China Hat", "Outline Thickness", "ESP");
    CFG_VAR("esp_china_hat_offset", float, chinaHatOffset, 0.06f, true, "China Hat", "Vertical Offset", "ESP");
    CFG_VAR("esp_china_hat_tilt", float, chinaHatTilt, 0.0f, true, "China Hat", "Side Tilt", "ESP");
    CFG_VAR("esp_china_hat_tilt_forward", float, chinaHatTiltForward, 0.0f, true, "China Hat", "Forward Tilt", "ESP");
    CFG_VAR("esp_china_hat_rim", bool, chinaHatFarRim, true, true, "China Hat", "Rim Arc", "ESP");
    CFG_VAR("esp_china_hat_ribs", bool, chinaHatRibs, true, true, "China Hat", "Ribs", "ESP");
    CFG_VAR("esp_china_hat_rib_count", int, chinaHatRibCount, 8, true, "China Hat", "Rib Count", "ESP");
    CFG_VAR("class_names_enabled", bool, classNamesEnabled, false, false, "Classes", "", "Classes");
    CFG_VAR("class_names_distance", bool, classNameDistance, true, true, "Classes", "Distance", "Classes");
    CFG_VAR("class_names_max_distance", float, classNameMaxDistanceMeters, 100.0f, true, "Classes", "Maximum Distance", "Classes");
    CFG_VAR("class_names_scale", float, classNameScale, 0.85f, true, "Classes", "Scale", "Classes");
    CFG_VAR("lever_highlight", bool, leverHighlight, true, false, "Lever ESP", "", "Classes");
    CFG_VAR("lever_max_distance", float, leverMaxDistanceMeters, 1000.0f, true, "Lever ESP", "Maximum Distance", "Classes");
    CFG_VAR("valve_highlight", bool, valveHighlight, true, false, "Valve ESP", "", "Classes");
    CFG_VAR("valve_max_distance", float, valveMaxDistanceMeters, 1000.0f, true, "Valve ESP", "Maximum Distance", "Classes");
    CFG_VAR("dynamic_markers_enabled", bool, dynamicMarkersEnabled, true, false, "Dynamic Markers", "", "Classes");
    CFG_VAR("dynamic_max_distance", float, dynamicMaxDistanceMeters, 500.0f, true, "Dynamic Markers", "Maximum Distance", "Classes");
    CFG_VAR("dynamic_minimum_speed", float, dynamicMinimumSpeed, 0.20f, true, "Dynamic Markers", "Minimum Speed", "Classes");
    CFG_VAR("dynamic_detection_window", float, dynamicDetectionWindowSeconds, 2.0f, true, "Dynamic Markers", "Detection Window", "Classes");
    CFG_VAR("dynamic_linger_seconds", float, dynamicLingerSeconds, 10.0f, true, "Dynamic Markers", "Linger", "Classes");
    CFG_VAR("spawn_markers_enabled", bool, spawnMarkersEnabled, true, false, "New Spawn ESP", "", "Classes");
};

struct EspStats
{
    std::size_t scannedActors = 0;
    std::size_t cachedEntities = 0;
    std::size_t drawnEntities = 0;
    std::size_t monsters = 0;
    std::size_t players = 0;
    std::size_t items = 0;
    std::size_t exits = 0;
    std::size_t levers = 0;
    std::size_t valves = 0;
    std::size_t dynamicEntities = 0;
    std::size_t alertMonsters = 0;
    std::size_t alertPlayers = 0;
    std::size_t alertItems = 0;
    std::size_t alertExits = 0;
    std::size_t alertObjectives = 0;
    std::size_t alertDynamicEntities = 0;
    float localSanity = -1.0f;
    float localMaxSanity = -1.0f;
    std::uint64_t sceneSignature = 0;
    std::uint64_t scanRevision = 0;
    bool worldReady = false;
};

class Esp final
{
public:
    static Esp& Instance();

    void UpdateAndDraw();
    bool WantsFrame() const noexcept;
    void Reset();

    EspSettings& Settings() noexcept { return settings_; }
    const EspSettings& Settings() const noexcept { return settings_; }
    const EspStats& Stats() const noexcept { return stats_; }

private:
    Esp() = default;

    EspSettings settings_{};
    EspStats stats_{};
};
}

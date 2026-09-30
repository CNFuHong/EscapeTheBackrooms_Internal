#pragma once

#include <imgui.h>

#include <string>

namespace etb::render
{
enum class EspCategory
{
    Monster,
    Player,
    Item,
    Exit,
    Lever,
    Valve,
    Dynamic,
    Spawn,
    ForcedExit
};

struct EspVisual
{
    ImVec2 head{};
    ImVec2 foot{};
    std::string name;
    float distanceMeters = 0.0f;
    EspCategory category = EspCategory::Item;
    bool compact = false;
    float sanity = -1.0f;
    float maxSanity = -1.0f;
    float speedMetersPerSecond = -1.0f;
    int stackCount = 1;
    // 新生成只是临时状态；实体的已知类别仍决定颜色和最高显示优先级。
    bool newlySpawned = false;
    ImVec2 corners[8]{};
    bool hasCorners = false;
    float worldX = 0.0f;
    float worldY = 0.0f;
    float worldZ = 0.0f;
    float worldHalfHeight = 0.0f;
    bool hasWorld = false;
};

struct EspDrawStyle
{
    bool boxes = true;
    bool threeD = false;
    bool filledBoxes = true;
    bool labels = true;
    bool distance = true;
    bool tracers = false;
    bool monsterTracersOnly = true;
    float fillOpacity = 0.18f;
    float cardScale = 1.0f;
};

struct ChinaHatStyle
{
    bool enabled = false;
    bool monsters = true;
    bool players = true;
    bool self = true;
    ImU32 color = IM_COL32(240, 209, 120, 235);
    float widthScale = 0.85f;
    float heightScale = 0.32f;
    float brimDroop = 0.15f;
    int segments = 32;
    bool filled = true;
    float fillOpacity = 0.30f;
    bool outline = true;
    float outlineThickness = 1.6f;
    float verticalOffset = 0.06f;
    float tilt = 0.0f;
    float tiltForward = 0.0f;
    bool farRim = true;
    bool ribs = true;
    int ribCount = 8;
};

struct ChinaHatMesh
{
    static constexpr int kMaxSegments = 96;
    ImVec2 apex{};
    ImVec2 brim[kMaxSegments]{};
    int segmentCount = 0;
    bool valid = false;
};

ImU32 CategoryColor(EspCategory category, int alpha = 255);
void DrawEspVisual(ImDrawList* drawList, const EspVisual& visual, const EspDrawStyle& style,
                   int labelStackIndex, const ImVec2& displaySize);
void DrawChinaHat(ImDrawList* drawList, const ChinaHatMesh& mesh, const ChinaHatStyle& style);
void DrawClassNameLabel(ImDrawList* drawList, const ImVec2& position, const std::string& className,
                        float distanceMeters, bool showDistance, float scale, int labelStackIndex);
void DrawPlayerNameTag(ImDrawList* drawList, const EspVisual& visual, bool showDistance,
                       bool showSanity, float scale, int labelStackIndex,
                       const ImVec2& displaySize);
}

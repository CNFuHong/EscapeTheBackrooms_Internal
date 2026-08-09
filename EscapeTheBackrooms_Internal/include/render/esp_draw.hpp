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
};

struct EspDrawStyle
{
    bool boxes = true;
    bool filledBoxes = true;
    bool labels = true;
    bool distance = true;
    bool tracers = false;
    bool monsterTracersOnly = true;
    float fillOpacity = 0.18f;
    float cardScale = 1.0f;
};

ImU32 CategoryColor(EspCategory category, int alpha = 255);
void DrawEspVisual(ImDrawList* drawList, const EspVisual& visual, const EspDrawStyle& style,
                   int labelStackIndex, const ImVec2& displaySize);
void DrawClassNameLabel(ImDrawList* drawList, const ImVec2& position, const std::string& className,
                        float distanceMeters, bool showDistance, float scale, int labelStackIndex);
void DrawPlayerNameTag(ImDrawList* drawList, const EspVisual& visual, bool showDistance,
                       bool showSanity, float scale, int labelStackIndex,
                       const ImVec2& displaySize);
}

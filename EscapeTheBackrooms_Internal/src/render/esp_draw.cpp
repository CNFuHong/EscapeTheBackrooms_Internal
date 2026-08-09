#include "render/esp_draw.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>

namespace etb::render
{
namespace
{
ImU32 WithAlpha(ImU32 color, int alpha)
{
    return (color & 0x00FFFFFFu) | (static_cast<ImU32>(std::clamp(alpha, 0, 255)) << 24u);
}

const char* CategoryTag(EspCategory category)
{
    switch (category)
    {
    case EspCategory::Monster: return "THREAT";
    case EspCategory::Player:  return "PLAYER";
    case EspCategory::Item:    return "ITEM";
    case EspCategory::Exit:    return "EXIT";
    case EspCategory::Lever:   return "LEVER";
    case EspCategory::Valve:   return "VALVE";
    case EspCategory::Dynamic: return "DYNAMIC";
    case EspCategory::Spawn:   return "NEW SPAWN";
    case EspCategory::ForcedExit: return "FORCED EXIT";
    default:                   return "ENTITY";
    }
}

const char* NewlySpawnedCategoryTag(EspCategory category)
{
    switch (category)
    {
    case EspCategory::Monster: return "NEW THREAT";
    case EspCategory::Player:  return "NEW PLAYER";
    case EspCategory::Item:    return "NEW ITEM";
    case EspCategory::Exit:    return "NEW EXIT";
    case EspCategory::Lever:   return "NEW LEVER";
    case EspCategory::Valve:   return "NEW VALVE";
    default:                   return "NEW SPAWN";
    }
}
}

ImU32 CategoryColor(EspCategory category, int alpha)
{
    switch (category)
    {
    case EspCategory::Monster: return IM_COL32(255, 76, 96, alpha);
    case EspCategory::Player:  return IM_COL32(64, 176, 255, alpha);
    case EspCategory::Item:    return IM_COL32(83, 226, 142, alpha);
    case EspCategory::Exit:    return IM_COL32(211, 105, 255, alpha);
    case EspCategory::Lever:   return IM_COL32(255, 184, 45, alpha);
    case EspCategory::Valve:   return IM_COL32(65, 224, 210, alpha);
    case EspCategory::Dynamic: return IM_COL32(255, 218, 74, alpha);
    case EspCategory::Spawn:   return IM_COL32(255, 70, 190, alpha);
    case EspCategory::ForcedExit: return IM_COL32(80, 255, 90, alpha);
    default:                   return IM_COL32(245, 203, 72, alpha);
    }
}

void DrawEspVisual(ImDrawList* drawList, const EspVisual& visual, const EspDrawStyle& style,
                   int labelStackIndex, const ImVec2& displaySize)
{
    if (drawList == nullptr)
        return;

    const float rawHeight = std::abs(visual.foot.y - visual.head.y);
    if (!std::isfinite(rawHeight) || rawHeight > displaySize.y * 2.0f)
        return;

    const bool square = visual.compact;
    const float height = std::clamp(rawHeight, 10.0f, displaySize.y * 0.9f);
    const float width = square ? height : std::clamp(height * 0.56f, 14.0f, 280.0f);
    const float centerX = (visual.head.x + visual.foot.x) * 0.5f;
    const float centerY = (visual.head.y + visual.foot.y) * 0.5f;
    const float top = centerY - height * 0.5f;
    const float bottom = centerY + height * 0.5f;
    const ImVec2 boxMin(centerX - width * 0.5f, top);
    const ImVec2 boxMax(centerX + width * 0.5f, bottom);
    const ImU32 color = CategoryColor(visual.category);

    if (style.tracers && (!style.monsterTracersOnly || visual.category == EspCategory::Monster))
    {
        const ImVec2 origin(displaySize.x * 0.5f, displaySize.y - 4.0f);
        drawList->AddLine(origin, ImVec2(centerX, bottom), IM_COL32(0, 0, 0, 125), 3.2f);
        drawList->AddLine(origin, ImVec2(centerX, bottom), WithAlpha(color, 185), 1.35f);
    }

    if (style.boxes)
    {
        const float rounding = std::clamp(width * 0.055f, 3.0f, 7.0f);
        drawList->AddRect(ImVec2(boxMin.x + 2.0f, boxMin.y + 3.0f),
                          ImVec2(boxMax.x + 2.0f, boxMax.y + 3.0f),
                          IM_COL32(0, 0, 0, 90), rounding, 0, 3.0f);

        if (style.filledBoxes)
        {
            const int alpha = static_cast<int>(std::clamp(style.fillOpacity, 0.0f, 0.55f) * 255.0f);
            drawList->AddRectFilled(boxMin, boxMax, IM_COL32(8, 11, 18, alpha), rounding);
            drawList->AddRectFilled(ImVec2(boxMin.x, boxMin.y), ImVec2(boxMax.x, boxMin.y + 3.0f),
                                    WithAlpha(color, std::min(alpha + 38, 135)), rounding);
        }

        drawList->AddRect(boxMin, boxMax, IM_COL32(4, 6, 10, 220), rounding, 0, 3.4f);
        drawList->AddRect(boxMin, boxMax, WithAlpha(color, 225), rounding, 0, 1.45f);

        const float corner = std::clamp(std::min(width, height) * 0.24f, 5.0f, 18.0f);
        const float thick = 2.5f;
        drawList->AddLine(boxMin, ImVec2(boxMin.x + corner, boxMin.y), color, thick);
        drawList->AddLine(boxMin, ImVec2(boxMin.x, boxMin.y + corner), color, thick);
        drawList->AddLine(ImVec2(boxMax.x, boxMin.y), ImVec2(boxMax.x - corner, boxMin.y), color, thick);
        drawList->AddLine(ImVec2(boxMax.x, boxMin.y), ImVec2(boxMax.x, boxMin.y + corner), color, thick);
        drawList->AddLine(ImVec2(boxMin.x, boxMax.y), ImVec2(boxMin.x + corner, boxMax.y), color, thick);
        drawList->AddLine(ImVec2(boxMin.x, boxMax.y), ImVec2(boxMin.x, boxMax.y - corner), color, thick);
        drawList->AddLine(boxMax, ImVec2(boxMax.x - corner, boxMax.y), color, thick);
        drawList->AddLine(boxMax, ImVec2(boxMax.x, boxMax.y - corner), color, thick);
    }

    if (!style.labels)
        return;

    char telemetryText[64]{};
    const bool speedReady = std::isfinite(visual.speedMetersPerSecond) &&
                            visual.speedMetersPerSecond >= 0.0f;
    if (style.distance && speedReady)
        std::snprintf(telemetryText, sizeof(telemetryText), "%.0f m | %.1f m/s",
                      visual.distanceMeters, visual.speedMetersPerSecond);
    else if (style.distance)
        std::snprintf(telemetryText, sizeof(telemetryText), "%.0f m", visual.distanceMeters);
    else if (speedReady)
        std::snprintf(telemetryText, sizeof(telemetryText), "%.1f m/s",
                      visual.speedMetersPerSecond);
    const bool telemetryReady = style.distance || speedReady;

    const float scale = std::clamp(style.cardScale, 0.65f, 1.60f);
    ImFont* font = ImGui::GetFont();
    const float fontSize = ImGui::GetFontSize() * scale;
    const char* tag = visual.newlySpawned
        ? NewlySpawnedCategoryTag(visual.category) : CategoryTag(visual.category);
    std::string displayName = visual.name;
    if (visual.stackCount > 1)
    {
        displayName += "  x";
        displayName += std::to_string(visual.stackCount);
    }
    const ImVec2 tagSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, tag);
    const ImVec2 nameSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, displayName.c_str());
    const ImVec2 distSize = telemetryReady
        ? font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, telemetryText)
        : ImVec2(0.0f, 0.0f);
    const float labelWidth = std::clamp(
        std::max(nameSize.x + 20.0f * scale, tagSize.x + distSize.x + 31.0f * scale),
        92.0f * scale,
        260.0f * scale);
    const float labelHeight = 39.0f * scale;
    const int slot = labelStackIndex % 5;
    const int row = labelStackIndex / 5;
    constexpr float horizontalSlots[5] = {0.0f, -1.05f, 1.05f, -2.10f, 2.10f};
    const float horizontalOffset = horizontalSlots[slot] * (labelWidth + 4.0f * scale);
    const float stackOffset = static_cast<float>(row) * (labelHeight + 5.0f * scale);

    const float unclampedX = centerX + horizontalOffset - labelWidth * 0.5f;
    const float unclampedY = top - labelHeight - 7.0f * scale - stackOffset;
    const ImVec2 labelMin(std::clamp(unclampedX, 4.0f, std::max(4.0f, displaySize.x - labelWidth - 4.0f)),
                          std::clamp(unclampedY, 4.0f, std::max(4.0f, displaySize.y - labelHeight - 4.0f)));
    const ImVec2 labelMax(labelMin.x + labelWidth, labelMin.y + labelHeight);
    const float rounding = 6.0f * scale;

    if (labelStackIndex > 0)
    {
        const ImVec2 labelAnchor((labelMin.x + labelMax.x) * 0.5f, labelMax.y);
        const ImVec2 entityAnchor(centerX, top);
        drawList->AddLine(labelAnchor, entityAnchor, IM_COL32(0, 0, 0, 115), 2.8f);
        drawList->AddLine(labelAnchor, entityAnchor, WithAlpha(color, 145), 1.1f);
    }

    drawList->AddRectFilled(ImVec2(labelMin.x + 2.0f * scale, labelMin.y + 3.0f * scale),
                            ImVec2(labelMax.x + 2.0f * scale, labelMax.y + 3.0f * scale),
                            IM_COL32(0, 0, 0, 95), rounding);
    drawList->AddRectFilled(labelMin, labelMax, IM_COL32(11, 15, 23, 232), rounding);
    drawList->AddRectFilled(labelMin, ImVec2(labelMin.x + 5.0f * scale, labelMax.y), color, rounding,
                            ImDrawFlags_RoundCornersLeft);
    drawList->AddRect(labelMin, labelMax, IM_COL32(255, 255, 255, 28), rounding, 0, 1.0f);

    drawList->AddText(font, fontSize,
                      ImVec2(labelMin.x + 12.0f * scale, labelMin.y + 3.0f * scale),
                      WithAlpha(color, 235), tag);
    if (telemetryReady)
        drawList->AddText(font, fontSize,
                          ImVec2(labelMax.x - distSize.x - 9.0f * scale, labelMin.y + 3.0f * scale),
                          IM_COL32(173, 184, 202, 235), telemetryText);

    const ImVec4 oldClip(labelMin.x + 9.0f * scale, labelMin.y,
                         labelMax.x - 7.0f * scale, labelMax.y);
    drawList->PushClipRect(ImVec2(oldClip.x, oldClip.y), ImVec2(oldClip.z, oldClip.w), true);
    drawList->AddText(font, fontSize,
                      ImVec2(labelMin.x + 12.0f * scale, labelMin.y + 19.0f * scale),
                      IM_COL32(244, 247, 252, 255), displayName.c_str());
    drawList->PopClipRect();
}

void DrawClassNameLabel(ImDrawList* drawList, const ImVec2& position, const std::string& className,
                        float distanceMeters, bool showDistance, float scale, int labelStackIndex)
{
    if (drawList == nullptr || className.empty())
        return;

    scale = std::clamp(scale, 0.60f, 1.40f);
    char distanceText[32]{};
    if (showDistance)
        std::snprintf(distanceText, sizeof(distanceText), "%.0f m", distanceMeters);

    ImFont* font = ImGui::GetFont();
    const float fontSize = ImGui::GetFontSize() * scale;
    const ImVec2 nameSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, className.c_str());
    const ImVec2 distanceSize = showDistance
        ? font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, distanceText)
        : ImVec2(0.0f, 0.0f);
    const float gap = showDistance ? 10.0f * scale : 0.0f;
    const float width = std::clamp(nameSize.x + distanceSize.x + gap + 16.0f * scale,
                                   72.0f * scale, 360.0f * scale);
    const float height = 23.0f * scale;
    const int slot = labelStackIndex % 5;
    const int row = labelStackIndex / 5;
    constexpr float horizontalSlots[5] = {0.0f, -1.05f, 1.05f, -2.10f, 2.10f};
    const float horizontalOffset = horizontalSlots[slot] * (width + 4.0f * scale);
    const float stackOffset = static_cast<float>(row) * (height + 3.0f * scale);
    const ImVec2 min(position.x + horizontalOffset - width * 0.5f,
                     position.y - height * 0.5f - stackOffset);
    const ImVec2 max(min.x + width, min.y + height);
    const float rounding = 4.0f * scale;

    drawList->AddRectFilled(ImVec2(min.x + 1.0f, min.y + 2.0f),
                            ImVec2(max.x + 1.0f, max.y + 2.0f),
                            IM_COL32(0, 0, 0, 105), rounding);
    drawList->AddRectFilled(min, max, IM_COL32(14, 18, 25, 212), rounding);
    drawList->AddRect(min, max, IM_COL32(120, 158, 205, 150), rounding, 0, 1.0f);

    const float textY = min.y + (height - fontSize) * 0.5f;
    drawList->PushClipRect(min, max, true);
    drawList->AddText(font, fontSize, ImVec2(min.x + 8.0f * scale, textY),
                      IM_COL32(218, 229, 244, 245), className.c_str());
    if (showDistance)
    {
        drawList->AddText(font, fontSize,
                          ImVec2(max.x - distanceSize.x - 7.0f * scale, textY),
                          IM_COL32(119, 183, 255, 245), distanceText);
    }
    drawList->PopClipRect();
}

void DrawPlayerNameTag(ImDrawList* drawList, const EspVisual& visual, const bool showDistance,
                       const bool showSanity, float scale, const int labelStackIndex,
                       const ImVec2& displaySize)
{
    if (drawList == nullptr || visual.name.empty())
        return;

    scale = std::clamp(scale, 0.65f, 1.60f);
    ImFont* font = ImGui::GetFont();
    const float fontSize = ImGui::GetFontSize() * scale;
    char telemetryText[64]{};
    char sanityText[48]{};
    const bool speedReady = std::isfinite(visual.speedMetersPerSecond) &&
                            visual.speedMetersPerSecond >= 0.0f;
    if (showDistance && speedReady)
        std::snprintf(telemetryText, sizeof(telemetryText), "%.0f m | %.1f m/s",
                      visual.distanceMeters, visual.speedMetersPerSecond);
    else if (showDistance)
        std::snprintf(telemetryText, sizeof(telemetryText), "%.0f m", visual.distanceMeters);
    else if (speedReady)
        std::snprintf(telemetryText, sizeof(telemetryText), "%.1f m/s",
                      visual.speedMetersPerSecond);
    const bool telemetryReady = showDistance || speedReady;
    const bool sanityReady = showSanity && std::isfinite(visual.sanity) &&
                             std::isfinite(visual.maxSanity) && visual.maxSanity > 0.0f;
    if (sanityReady)
        std::snprintf(sanityText, sizeof(sanityText), "SANITY %.0f / %.0f",
                      visual.sanity, visual.maxSanity);

    const ImVec2 nameSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, visual.name.c_str());
    const ImVec2 distanceSize = telemetryReady
        ? font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, telemetryText) : ImVec2{};
    const ImVec2 sanitySize = sanityReady
        ? font->CalcTextSizeA(fontSize * 0.88f, FLT_MAX, 0.0f, sanityText) : ImVec2{};
    const float width = std::clamp(
        std::max(nameSize.x + distanceSize.x + 30.0f * scale,
                 sanitySize.x + 22.0f * scale), 120.0f * scale, 320.0f * scale);
    const float height = (sanityReady ? 43.0f : 25.0f) * scale;
    const int slot = labelStackIndex % 5;
    const int row = labelStackIndex / 5;
    constexpr float horizontalSlots[5] = {0.0f, -1.05f, 1.05f, -2.10f, 2.10f};
    const float horizontalOffset = horizontalSlots[slot] * (width + 4.0f * scale);
    const float stackOffset = static_cast<float>(row) * (height + 4.0f * scale);
    const float centerX = (visual.head.x + visual.foot.x) * 0.5f;
    const float rawX = centerX + horizontalOffset - width * 0.5f;
    const float rawY = std::min(visual.head.y, visual.foot.y) - height - 8.0f * scale - stackOffset;
    const ImVec2 minimum(
        std::clamp(rawX, 4.0f, std::max(4.0f, displaySize.x - width - 4.0f)),
        std::clamp(rawY, 4.0f, std::max(4.0f, displaySize.y - height - 4.0f)));
    const ImVec2 maximum(minimum.x + width, minimum.y + height);
    const ImU32 accent = CategoryColor(EspCategory::Player);
    const float rounding = 5.0f * scale;

    drawList->AddRectFilled(ImVec2(minimum.x + 2.0f, minimum.y + 3.0f),
                            ImVec2(maximum.x + 2.0f, maximum.y + 3.0f),
                            IM_COL32(0, 0, 0, 100), rounding);
    drawList->AddRectFilled(minimum, maximum, IM_COL32(10, 14, 22, 232), rounding);
    drawList->AddRectFilled(minimum, ImVec2(minimum.x + 4.0f * scale, maximum.y),
                            accent, rounding, ImDrawFlags_RoundCornersLeft);
    drawList->AddRect(minimum, maximum, WithAlpha(accent, 155), rounding, 0, 1.0f);

    drawList->AddText(font, fontSize,
                      ImVec2(minimum.x + 11.0f * scale, minimum.y + 3.0f * scale),
                      IM_COL32(244, 247, 252, 255), visual.name.c_str());
    if (telemetryReady)
        drawList->AddText(font, fontSize,
                          ImVec2(maximum.x - distanceSize.x - 8.0f * scale,
                                 minimum.y + 3.0f * scale),
                          IM_COL32(157, 202, 255, 245), telemetryText);

    if (sanityReady)
    {
        const float ratio = std::clamp(visual.sanity / visual.maxSanity, 0.0f, 1.0f);
        const float lineY = minimum.y + 23.0f * scale;
        drawList->AddText(font, fontSize * 0.88f,
                          ImVec2(minimum.x + 11.0f * scale, lineY),
                          ratio > 0.35f ? IM_COL32(107, 235, 166, 245)
                                        : IM_COL32(255, 102, 112, 245), sanityText);
        const ImVec2 barMin(minimum.x + 10.0f * scale, maximum.y - 5.0f * scale);
        const ImVec2 barMax(maximum.x - 8.0f * scale, maximum.y - 2.0f * scale);
        drawList->AddRectFilled(barMin, barMax, IM_COL32(36, 43, 56, 230), 2.0f);
        drawList->AddRectFilled(barMin,
                                ImVec2(barMin.x + (barMax.x - barMin.x) * ratio, barMax.y),
                                ratio > 0.35f ? IM_COL32(70, 219, 139, 255)
                                              : IM_COL32(255, 82, 96, 255), 2.0f);
    }
}
}

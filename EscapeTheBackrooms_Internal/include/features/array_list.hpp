#pragma once

#include "core/config.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>

namespace etb::features
{
enum class ArrayListMode : int
{
    None,
    Outline,
    EdgeLine,
    Rounded
};

enum class ArrayListColor : int
{
    Rainbow,
    Categorized,
    Custom
};

enum class ArrayListRainbow : int
{
    Pastel,
    Saturated
};

enum class ArrayListHorizontalGravity : int
{
    Left,
    Right
};

enum class ArrayListVerticalGravity : int
{
    Top,
    Bottom
};

struct ArrayListSettings
{
    CFG_VAR("arraylist_enabled", bool, enabled, true, false, "ArrayList", "", "HUD");
    CFG_VAR("arraylist_mode", ArrayListMode, mode, ArrayListMode::Outline, true, "ArrayList", "Mode", "HUD");
    CFG_VAR("arraylist_color", ArrayListColor, color, ArrayListColor::Rainbow, true, "ArrayList", "Color", "HUD");
    CFG_VAR("arraylist_rainbow", ArrayListRainbow, rainbow, ArrayListRainbow::Pastel, true, "ArrayList", "Rainbow", "HUD");
    CFG_VAR("arraylist_custom_color", ::etb::core::config::Color4, customColor, ::etb::core::config::MakeColor(1.0f, 1.0f, 1.0f, 1.0f), true, "ArrayList", "Custom Color", "HUD");
    CFG_VAR("arraylist_background", ::etb::core::config::Color4, backgroundColor, ::etb::core::config::MakeColor(0.0f, 0.0f, 0.0f, 154.0f / 255.0f), true, "ArrayList", "Background", "HUD");
    CFG_VAR("arraylist_mode_text", ::etb::core::config::Color4, modeTextColor, ::etb::core::config::MakeColor(197.0f / 255.0f, 197.0f / 255.0f, 197.0f / 255.0f, 1.0f), true, "ArrayList", "Mode Text", "HUD");
    CFG_VAR("arraylist_text_size", float, textSize, 18.1f, true, "ArrayList", "Text Size", "HUD");
    CFG_VAR("arraylist_border", float, borderThickness, 2.4f, true, "ArrayList", "Border", "HUD");
    CFG_VAR("arraylist_corner_radius", float, cornerRadius, 8.0f, true, "ArrayList", "Corner Radius", "HUD");
    CFG_VAR("arraylist_row_padding", float, rowPadding, 6.0f, true, "ArrayList", "Row Padding", "HUD");
    CFG_VAR("arraylist_rainbow_step", float, rainbowStepDegrees, 29.5f, true, "ArrayList", "Rainbow Step", "HUD");
    CFG_VAR("arraylist_text_shadow", bool, textShadowEnabled, true, true, "ArrayList", "Text Shadow", "HUD");
    CFG_VAR("arraylist_touch_model", bool, touchModelEnabled, true, true, "ArrayList", "Touch Model", "HUD");
    CFG_VAR("arraylist_show_modes", bool, showModes, true, true, "ArrayList", "Show Modes", "HUD");
    CFG_VAR("arraylist_show_keybinds", bool, showKeybinds, true, true, "ArrayList", "Show Keybinds", "HUD");
    CFG_VAR("arraylist_gravity_animation", bool, gravityChangingAnimationEnabled, true, true, "ArrayList", "Gravity Animation", "HUD");
    CFG_VAR("arraylist_closing_animation", bool, itemClosingAnimationEnabled, true, true, "ArrayList", "Closing Animation", "HUD");
    CFG_VAR("self_info_enabled", bool, selfInfoEnabled, true, false, "Self Information", "", "HUD");
    CFG_VAR("self_info_fps", bool, selfInfoFps, true, true, "Self Information", "FPS", "HUD");
    CFG_VAR("self_info_position", bool, selfInfoPosition, true, true, "Self Information", "Position", "HUD");
    CFG_VAR("self_info_speed", bool, selfInfoSpeed, true, true, "Self Information", "Speed", "HUD");
    CFG_VAR("self_info_velocity", bool, selfInfoVelocity, true, true, "Self Information", "Velocity", "HUD");
    CFG_VAR("self_info_movement", bool, selfInfoMovementMode, true, true, "Self Information", "Movement", "HUD");
    CFG_VAR("self_info_text_size", float, selfInfoTextSize, 15.0f, true, "Self Information", "Text Size", "HUD");
    CFG_VAR("self_info_padding", float, selfInfoPadding, 6.0f, true, "Self Information", "Padding", "HUD");
    CFG_VAR("self_info_background", ::etb::core::config::Color4, selfInfoBackground, ::etb::core::config::MakeColor(0.0f, 0.0f, 0.0f, 154.0f / 255.0f), true, "Self Information", "Background", "HUD");
    CFG_VAR("self_info_horizontal", ArrayListHorizontalGravity, selfInfoHorizontalGravity, ArrayListHorizontalGravity::Right, true, "Self Information", "Horizontal", "HUD");
    CFG_VAR("self_info_vertical", ArrayListVerticalGravity, selfInfoVerticalGravity, ArrayListVerticalGravity::Bottom, true, "Self Information", "Vertical", "HUD");
    CFG_VAR("arraylist_horizontal", ArrayListHorizontalGravity, horizontalGravity, ArrayListHorizontalGravity::Right, true, "ArrayList", "Horizontal", "HUD");
    CFG_VAR("arraylist_vertical", ArrayListVerticalGravity, verticalGravity, ArrayListVerticalGravity::Top, true, "ArrayList", "Vertical", "HUD");
    CFG_VAR("arraylist_category_colors", ::etb::core::config::CategoryColors, categoryColors, ::etb::core::config::MakeCategoryColors(), true, "ArrayList", "Category Colors", "HUD");
};

struct HudModuleState
{
    const char* name = "";
    bool enabled = false;
};

enum class ModuleCategory : int
{
    Esp = 0,
    Movement = 1,
    Visual = 2,
    Host = 3
};

class ArrayListHud final
{
public:
    static constexpr std::size_t ModuleCount = 320;

    using ModuleModeTextFn = const char* (*)(char* buffer, std::size_t capacity);

    struct ModuleSpec
    {
        const char* name = "";
        ModuleCategory category = ModuleCategory::Visual;
        bool* toggle = nullptr;
        int keybind = 0;
        ModuleModeTextFn modeText = nullptr;
    };

    static ArrayListHud& Instance();

    bool RegisterModule(const ModuleSpec& spec);
    std::size_t ModuleTotal() const noexcept;

    void Draw(bool menuVisible);
    void Reset();
    void CaptureModuleStates(std::array<HudModuleState, ModuleCount>& states) const;

    ArrayListSettings& Settings() noexcept { return settings_; }
    const ArrayListSettings& Settings() const noexcept { return settings_; }

private:
    ArrayListHud() = default;
    void DrawSelfInfo();

    struct ItemRuntime
    {
        bool present = false;
        bool entering = false;
        bool exiting = false;
        bool waitingExitFinish = false;
        bool hasCurrentY = false;
        float slideProgress = 1.0f;
        float exitProgress = 0.0f;
        float currentY = 0.0f;
        int keybind = 0;
        int category = 0;
        std::array<char, 48> modeText{};
        std::chrono::steady_clock::time_point enterStart{};
        std::chrono::steady_clock::time_point exitStart{};
        std::chrono::steady_clock::time_point exitFinishAt{};
    };

    ArrayListSettings settings_{};
    std::array<ItemRuntime, ModuleCount> items_{};

    float baseHueDegrees_ = 0.0f;
    bool hasRainbowTime_ = false;
    bool hasLayoutTime_ = false;
    std::chrono::steady_clock::time_point lastRainbowTime_{};
    std::chrono::steady_clock::time_point lastLayoutTime_{};

    ArrayListHorizontalGravity lastHorizontalGravity_ = ArrayListHorizontalGravity::Right;
    ArrayListVerticalGravity lastVerticalGravity_ = ArrayListVerticalGravity::Top;
    ArrayListHorizontalGravity gravityFromHorizontal_ = ArrayListHorizontalGravity::Right;
    ArrayListVerticalGravity gravityFromVertical_ = ArrayListVerticalGravity::Top;
    bool gravityTransition_ = false;
    std::chrono::steady_clock::time_point gravityStart_{};
};
}

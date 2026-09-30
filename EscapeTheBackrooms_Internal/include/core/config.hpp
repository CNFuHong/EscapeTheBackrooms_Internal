#pragma once

#include <nlohmann/json.hpp>

#include <array>
#include <cstdio>
#include <string>
#include <type_traits>
#include <vector>

namespace etb::core::config
{
using Json = nlohmann::json;
using Color4 = std::array<float, 4>;
using CategoryColors = std::array<Color4, 4>;

constexpr Color4 MakeColor(const float red, const float green, const float blue, const float alpha)
{
    return {red, green, blue, alpha};
}

constexpr CategoryColors MakeCategoryColors()
{
    return {{
        {1.0f, 1.0f, 1.0f, 1.0f},
        {1.0f, 1.0f, 1.0f, 1.0f},
        {1.0f, 1.0f, 1.0f, 1.0f},
        {1.0f, 1.0f, 1.0f, 1.0f},
    }};
}

class IConfigItem
{
public:
    virtual ~IConfigItem() = default;
    virtual void ResetDefault() = 0;
    virtual void Save(Json& json) = 0;
    virtual void Load(const Json& json) = 0;
    virtual std::string GetStringValue() = 0;
    virtual bool IsBoolAndTrue() = 0;
    virtual bool IsBool() = 0;
    virtual void SetBoolValue(bool value) = 0;
    virtual bool IsColor() = 0;
    virtual void* RawPtr() = 0;

    std::string name;
    bool is_param = false;
    std::string feature_name;
    std::string param_name;
    std::string category;
    int hotkey = 0;
    int hotkey_mode = 1;
};

std::vector<IConfigItem*>& Items();
void RegisterItem(IConfigItem* item);

template <typename T>
class ConfigItem final : public IConfigItem
{
public:
    T* ptr = nullptr;
    T defaultValue{};

    ConfigItem(std::string id, T& variable, const bool isParameter = false,
               std::string featureName = {}, std::string parameterName = {},
               std::string categoryName = {})
    {
        name = std::move(id);
        ptr = &variable;
        defaultValue = variable;
        is_param = isParameter;
        feature_name = std::move(featureName);
        param_name = std::move(parameterName);
        category = std::move(categoryName);
        RegisterItem(this);
    }

    void ResetDefault() override
    {
        *ptr = defaultValue;
        hotkey = 0;
        hotkey_mode = 1;
    }

    void Save(Json& json) override
    {
        if constexpr (std::is_same_v<T, bool>)
        {
            json[name] = Json{
                {"value", *ptr},
                {"hotkey", hotkey},
                {"hotkey_mode", hotkey_mode},
                {"has_hotkey", hotkey != 0}
            };
        }
        else if constexpr (std::is_enum_v<T>)
        {
            json[name] = static_cast<std::underlying_type_t<T>>(*ptr);
        }
        else
        {
            json[name] = *ptr;
        }
    }

    void Load(const Json& json) override
    {
        if (!json.contains(name))
            return;

        const Json& node = json.at(name);
        const Json& value = node.is_object() && node.contains("value") ? node.at("value") : node;
        if constexpr (std::is_enum_v<T>)
        {
            *ptr = static_cast<T>(value.get<std::underlying_type_t<T>>());
        }
        else
        {
            *ptr = value.get<T>();
        }

        if constexpr (std::is_same_v<T, bool>)
        {
            if (node.is_object())
            {
                hotkey = node.value("hotkey", 0);
                hotkey_mode = node.value("hotkey_mode", 1);
                if (node.contains("has_hotkey") && !node.value("has_hotkey", false))
                    hotkey = 0;
                if (hotkey < 0 || hotkey >= 256)
                    hotkey = 0;
                if (hotkey_mode < 0 || hotkey_mode > 2)
                    hotkey_mode = 1;
            }
        }
    }

    std::string GetStringValue() override
    {
        if constexpr (std::is_arithmetic_v<T> && !std::is_same_v<T, bool>)
        {
            if constexpr (std::is_floating_point_v<T>)
            {
                char buffer[32]{};
                std::snprintf(buffer, sizeof(buffer), "%.1f", static_cast<float>(*ptr));
                return buffer;
            }
            else
            {
                return std::to_string(*ptr);
            }
        }
        return {};
    }

    bool IsBoolAndTrue() override
    {
        if constexpr (std::is_same_v<T, bool>)
            return *ptr;
        return false;
    }

    bool IsBool() override
    {
        return std::is_same_v<T, bool>;
    }

    void SetBoolValue(const bool value) override
    {
        if constexpr (std::is_same_v<T, bool>)
            *ptr = value;
    }

    bool IsColor() override
    {
        return std::is_same_v<T, Color4>;
    }

    void* RawPtr() override
    {
        return static_cast<void*>(ptr);
    }
};

bool Save(std::string_view name, std::string& message);
bool Load(std::string_view name, std::string& message);
bool LoadStartupOnce(std::string& loadedName, std::string& message);
bool OpenDirectory(std::string& message);
std::string PathUtf8(std::string_view name);
}

#define CFG_VAR(id, type, variable, defaultValue, isParameter, featureName, parameterName, categoryName) \
    inline static type variable = defaultValue; \
    ::etb::core::config::ConfigItem<type> _item_##variable{id, variable, isParameter, featureName, parameterName, categoryName}

#define CFG_VAR_OLD(id, type, variable, defaultValue) \
    inline static type variable = defaultValue; \
    ::etb::core::config::ConfigItem<type> _item_##variable{id, variable, false, "", "", ""}

namespace etb::core::config
{
struct MenuSettings
{
    CFG_VAR("menu_chinese", bool, chineseMode, true, true, "Menu", "Chinese", "Menu");
    CFG_VAR("menu_disable_end_unload", bool, disableEndUnload, false, true, "Menu", "Disable End Unload", "Menu");
    CFG_VAR("menu_imgui_cursor", bool, mouseCircle, true, true, "Menu", "Mouse Circle", "Menu");
    CFG_VAR("menu_mouse_style", int, mouseStyle, 0, true, "Menu", "Cursor Style", "Menu");
    CFG_VAR("menu_mouse_radius", float, mouseRadius, 7.5f, true, "Menu", "Cursor Radius", "Menu");
    CFG_VAR("menu_mouse_thickness", float, mouseThickness, 1.5f, true, "Menu", "Cursor Thickness", "Menu");
    CFG_VAR("menu_mouse_color_r", int, mouseColorR, 255, true, "Menu", "Cursor R", "Menu");
    CFG_VAR("menu_mouse_color_g", int, mouseColorG, 255, true, "Menu", "Cursor G", "Menu");
    CFG_VAR("menu_mouse_color_b", int, mouseColorB, 255, true, "Menu", "Cursor B", "Menu");
    CFG_VAR("menu_mouse_color_a", int, mouseColorA, 255, true, "Menu", "Cursor A", "Menu");
    CFG_VAR("menu_mouse_accent_r", int, mouseAccentR, 0, true, "Menu", "Accent R", "Menu");
    CFG_VAR("menu_mouse_accent_g", int, mouseAccentG, 0, true, "Menu", "Accent G", "Menu");
    CFG_VAR("menu_mouse_accent_b", int, mouseAccentB, 0, true, "Menu", "Accent B", "Menu");
    CFG_VAR("menu_mouse_glow", bool, mouseGlowEnabled, true, true, "Menu", "Cursor Glow", "Menu");
    CFG_VAR("menu_mouse_glow_radius", float, mouseGlowRadius, 2.5f, true, "Menu", "Glow Radius", "Menu");
    CFG_VAR("menu_mouse_follow", bool, mouseFollowEnabled, true, true, "Menu", "Linear Follow", "Menu");
    CFG_VAR("menu_mouse_follow_speed", float, mouseFollowSpeed, 14.0f, true, "Menu", "Follow Speed", "Menu");
    CFG_VAR("menu_window_x", float, windowX, -1.0f, false, "Menu", "Window X", "Menu");
    CFG_VAR("menu_window_y", float, windowY, -1.0f, false, "Menu", "Window Y", "Menu");
    CFG_VAR("menu_window_width", float, windowWidth, 460.0f, false, "Menu", "Window Width", "Menu");
    CFG_VAR("menu_window_height", float, windowHeight, 420.0f, false, "Menu", "Window Height", "Menu");
};

inline MenuSettings g_MenuSettings;
}

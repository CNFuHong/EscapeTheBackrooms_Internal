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

    ConfigItem(std::string id, T& variable, const bool isParameter = false,
               std::string featureName = {}, std::string parameterName = {},
               std::string categoryName = {})
    {
        name = std::move(id);
        ptr = &variable;
        is_param = isParameter;
        feature_name = std::move(featureName);
        param_name = std::move(parameterName);
        category = std::move(categoryName);
        RegisterItem(this);
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
};

inline MenuSettings g_MenuSettings;
}

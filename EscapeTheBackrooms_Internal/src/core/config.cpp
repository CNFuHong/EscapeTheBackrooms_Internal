#include "core/config.hpp"

#include "features/array_list.hpp"
#include "features/esp.hpp"
#include "features/exit_activator.hpp"
#include "features/movement.hpp"
#include "features/notifications.hpp"
#include "features/pickup.hpp"
#include "features/session_limit.hpp"
#include "features/spectator.hpp"
#include "features/spawner.hpp"
#include "features/vehicle_flight.hpp"
#include "features/visuals.hpp"

#include <Windows.h>
#include <Shellapi.h>

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>

namespace etb::core::config
{
namespace
{
constexpr int kConfigVersion = 3;
std::mutex g_startupMutex;
bool g_startupAttempted = false;
bool g_startupLoaded = false;
std::string g_startupName = "default";
std::string g_startupMessage;

std::filesystem::path ConfigDirectory()
{
    wchar_t localAppData[32768]{};
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData,
                                                  static_cast<DWORD>(std::size(localAppData)));
    if (length > 0 && length < std::size(localAppData))
    {
        return std::filesystem::path(localAppData) / L"EscapeTheBackrooms" /
               L"Saved" / L"Config";
    }

    wchar_t modulePath[32768]{};
    const DWORD moduleLength = GetModuleFileNameW(nullptr, modulePath,
                                                  static_cast<DWORD>(std::size(modulePath)));
    if (moduleLength > 0 && moduleLength < std::size(modulePath))
        return std::filesystem::path(modulePath).parent_path();
    return std::filesystem::current_path();
}

std::wstring Utf8ToWide(const std::string_view value)
{
    if (value.empty())
        return {};
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                           value.data(), static_cast<int>(value.size()),
                                           nullptr, 0);
    if (length <= 0)
        return {};
    std::wstring wide(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                        value.data(), static_cast<int>(value.size()),
                        wide.data(), length);
    return wide;
}

std::string WideToUtf8(const std::wstring_view value)
{
    if (value.empty())
        return {};
    const int length = WideCharToMultiByte(CP_UTF8, 0, value.data(),
                                           static_cast<int>(value.size()),
                                           nullptr, 0, nullptr, nullptr);
    if (length <= 0)
        return {};
    std::string utf8(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                        utf8.data(), length, nullptr, nullptr);
    return utf8;
}

std::wstring NormalizeName(const std::string_view name)
{
    std::wstring normalized = Utf8ToWide(name);
    if (normalized.size() >= 5)
    {
        std::wstring suffix = normalized.substr(normalized.size() - 5);
        std::transform(suffix.begin(), suffix.end(), suffix.begin(), [](const wchar_t character)
        {
            return static_cast<wchar_t>(std::towlower(character));
        });
        if (suffix == L".json")
            normalized.resize(normalized.size() - 5);
    }
    for (wchar_t& character : normalized)
    {
        if (character < 32 || character == L'<' || character == L'>' ||
            character == L':' || character == L'"' || character == L'/' ||
            character == L'\\' || character == L'|' || character == L'?' ||
            character == L'*')
        {
            character = L'_';
        }
    }
    while (!normalized.empty() && (normalized.front() == L' ' || normalized.front() == L'.'))
        normalized.erase(normalized.begin());
    while (!normalized.empty() && (normalized.back() == L' ' || normalized.back() == L'.'))
        normalized.pop_back();
    if (normalized.empty())
        normalized = L"default";
    if (normalized.size() > 64)
        normalized.resize(64);
    return normalized;
}

std::filesystem::path ConfigPath(const std::string_view name)
{
    return ConfigDirectory() / (NormalizeName(name) + L".json");
}

std::filesystem::path StartupStatePath()
{
    return ConfigDirectory() / L".state" / L"startup.json";
}

bool WriteJsonAtomically(const std::filesystem::path& path, const Json& json,
                         std::string& message)
{
    std::filesystem::create_directories(path.parent_path());
    const std::filesystem::path temporary = path.wstring() + L".tmp";
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output)
    {
        message = "Cannot open configuration file for writing";
        return false;
    }
    output << json.dump(4);
    output.flush();
    if (!output)
    {
        message = "Failed while writing configuration file";
        return false;
    }
    output.close();
    if (!MoveFileExW(temporary.c_str(), path.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        message = "Cannot replace configuration file: " + std::to_string(GetLastError());
        return false;
    }
    return true;
}

void RememberConfigName(const std::string_view name)
{
    const std::string normalized = WideToUtf8(NormalizeName(name));
    if (normalized.empty())
        return;
    Json state;
    state["name"] = normalized;
    std::string ignored;
    WriteJsonAtomically(StartupStatePath(), state, ignored);
}

std::string ResolveStartupName()
{
    try
    {
        std::ifstream input(StartupStatePath(), std::ios::binary);
        if (input)
        {
            const Json state = Json::parse(input, nullptr, true, true);
            if (state.contains("name") && state.at("name").is_string())
            {
                const std::string name = WideToUtf8(NormalizeName(
                    state.at("name").get<std::string>()));
                if (!name.empty() && std::filesystem::exists(ConfigPath(name)))
                    return name;
            }
        }
    }
    catch (...)
    {
    }

    try
    {
        std::filesystem::path newest;
        std::filesystem::file_time_type newestTime{};
        bool found = false;
        const std::filesystem::path directory = ConfigDirectory();
        if (std::filesystem::exists(directory))
        {
            for (const auto& entry : std::filesystem::directory_iterator(directory))
            {
                if (!entry.is_regular_file())
                    continue;
                std::wstring extension = entry.path().extension().wstring();
                std::transform(extension.begin(), extension.end(), extension.begin(),
                               [](const wchar_t character)
                               {
                                   return static_cast<wchar_t>(std::towlower(character));
                               });
                if (extension != L".json")
                    continue;
                const auto modified = entry.last_write_time();
                if (!found || modified > newestTime)
                {
                    newest = entry.path();
                    newestTime = modified;
                    found = true;
                }
            }
        }
        if (found)
        {
            const std::string name = WideToUtf8(newest.stem().wstring());
            if (!name.empty())
                return name;
        }
    }
    catch (...)
    {
    }
    return "default";
}

void EnsureRegistered()
{
    (void)features::Esp::Instance();
    (void)features::Movement::Instance();
    (void)features::Pickup::Instance();
    (void)features::SessionLimit::Instance();
    (void)features::Visuals::Instance();
    (void)features::VehicleFlight::Instance();
    (void)features::ArrayListHud::Instance();
    (void)features::Notifications::Instance();
    (void)features::ExitActivator::Instance();
    (void)features::Spectator::Instance();
    (void)features::Spawner::Instance();
    (void)g_MenuSettings;
}

template <typename T>
void Clamp(T& value, const T minimum, const T maximum)
{
    value = std::clamp(value, minimum, maximum);
}

template <typename Enum>
void ClampEnum(Enum& value, const int minimum, const int maximum)
{
    value = static_cast<Enum>(std::clamp(static_cast<int>(value), minimum, maximum));
}

void Validate()
{
    auto& esp = features::Esp::Instance().Settings();
    Clamp(esp.itemMergeScreenDistancePixels, 20.0f, 400.0f);
    Clamp(esp.itemMergeDepthMeters, 1.0f, 50.0f);
    Clamp(esp.maxDistanceMeters, 25.0f, 1500.0f);
    Clamp(esp.fillOpacity, 0.0f, 0.5f);
    Clamp(esp.cardScale, 0.65f, 1.6f);
    Clamp(esp.refreshIntervalMs, 100, 2000);
    Clamp(esp.classNameMaxDistanceMeters, 5.0f, 1000.0f);
    Clamp(esp.classNameScale, 0.6f, 1.4f);
    Clamp(esp.leverMaxDistanceMeters, 10.0f, 2000.0f);
    Clamp(esp.valveMaxDistanceMeters, 10.0f, 2000.0f);
    Clamp(esp.dynamicMaxDistanceMeters, 10.0f, 1500.0f);
    Clamp(esp.dynamicMinimumSpeed, 0.02f, 5.0f);
    Clamp(esp.dynamicDetectionWindowSeconds, 0.3f, 10.0f);
    Clamp(esp.dynamicLingerSeconds, 1.0f, 30.0f);

    auto& movement = features::Movement::Instance().Settings();
    Clamp(movement.walkSpeed, 50.0f, 3000.0f);
    Clamp(movement.sprintSpeed, 50.0f, 5000.0f);
    Clamp(movement.crouchSpeed, 25.0f, 3000.0f);
    Clamp(movement.jumpVelocity, 0.0f, 3000.0f);
    Clamp(movement.gravityScale, 0.0f, 5.0f);
    Clamp(movement.flySpeed, 50.0f, 5000.0f);
    Clamp(movement.flightToggleKey, 0, 255);
    Clamp(movement.noClipToggleKey, 0, 255);

    auto& pickup = features::Pickup::Instance().Settings();
    Clamp(pickup.autoPickupIntervalSeconds, 0.25f, 5.0f);
    Clamp(pickup.scanActorsPerTick, 50, 2000);
    Clamp(pickup.radiusMeters, 1.0f, 800.0f);
    Clamp(pickup.maxItemsPerActivation, 1, 32);
    Clamp(pickup.hotkey, 0, 255);
    Clamp(pickup.hostMaximumRadiusMeters, 1.0f, 800.0f);

    auto& visuals = features::Visuals::Instance().Settings();
    Clamp(visuals.nightVisionStrength, 1.0f, 20.0f);
    Clamp(visuals.nightVisionExposureBias, 0.0f, 8.0f);
    Clamp(visuals.nightVisionLightRangeMeters, 20.0f, 300.0f);
    Clamp(visuals.nightVisionLightIntensity, 1.0f, 20.0f);
    Clamp(visuals.thirdPersonDistance, 50.0f, 1000.0f);
    Clamp(visuals.thirdPersonHeight, -500.0f, 500.0f);
    Clamp(visuals.thirdPersonToggleKey, 0, 255);
    Clamp(visuals.derpPitchSpeed, -1080.0f, 1080.0f);
    Clamp(visuals.derpYawSpeed, -1080.0f, 1080.0f);
    Clamp(visuals.derpRollSpeed, -1080.0f, 1080.0f);

    auto& vehicle = features::VehicleFlight::Instance().Settings();
    Clamp(vehicle.horizontalSpeed, 50.0f, 10000.0f);
    Clamp(vehicle.verticalSpeed, 50.0f, 10000.0f);
    Clamp(vehicle.turnSpeedDegrees, 0.0f, 360.0f);
    Clamp(vehicle.boostMultiplier, 1.0f, 5.0f);
    Clamp(vehicle.networkRateHz, 5.0f, 60.0f);
    Clamp(vehicle.toggleKey, 0, 255);
    Clamp(vehicle.noClipToggleKey, 0, 255);

    auto& spectator = features::Spectator::Instance().Settings();
    Clamp(spectator.spectateToggleKey, 0, 255);
    Clamp(spectator.spectateNextKey, 0, 255);
    Clamp(spectator.spectatePrevKey, 0, 255);
    Clamp(spectator.freeCamToggleKey, 0, 255);
    Clamp(spectator.freeCamSpeed, 100.0f, 10000.0f);

    auto& session = features::SessionLimit::Instance().Settings();
    Clamp(session.maximumPlayers, 1, 64);

    auto& arrayList = features::ArrayListHud::Instance().Settings();
    ClampEnum(arrayList.mode, 0, 3);
    ClampEnum(arrayList.color, 0, 2);
    ClampEnum(arrayList.rainbow, 0, 1);
    ClampEnum(arrayList.selfInfoHorizontalGravity, 0, 1);
    ClampEnum(arrayList.selfInfoVerticalGravity, 0, 1);
    ClampEnum(arrayList.horizontalGravity, 0, 1);
    ClampEnum(arrayList.verticalGravity, 0, 1);
    Clamp(arrayList.textSize, 6.0f, 32.0f);
    Clamp(arrayList.borderThickness, 0.0f, 6.0f);
    Clamp(arrayList.cornerRadius, 0.0f, 24.0f);
    Clamp(arrayList.rowPadding, 0.0f, 20.0f);
    Clamp(arrayList.rainbowStepDegrees, 0.0f, 60.0f);
    Clamp(arrayList.selfInfoTextSize, 8.0f, 32.0f);
    Clamp(arrayList.selfInfoPadding, 0.0f, 20.0f);

    auto& notifications = features::Notifications::Instance().Settings();
    Clamp(notifications.discoveryExclusionRadiusMeters, 0.0f, 50.0f);
    Clamp(notifications.sizePercent, 50.0f, 150.0f);
    Clamp(notifications.durationSeconds, 0.5f, 10.0f);
    Clamp(notifications.durationMultiplier, 0.5f, 3.0f);
    Clamp(notifications.glowStrength, 0.5f, 25.0f);
    Clamp(notifications.maximumNotifications, 1, 25);

    auto& exits = features::ExitActivator::Instance().Settings();
    Clamp(exits.scanActorsPerTick, 50, 2000);
    Clamp(exits.memberWalkSpeed, 50.0f, 5000.0f);
    Clamp(exits.memberSprintSpeed, 50.0f, 8000.0f);
    Clamp(exits.memberCrouchSpeed, 25.0f, 5000.0f);
    Clamp(exits.memberMaxStamina, 1.0f, 10000.0f);

    auto& menu = g_MenuSettings;
    Clamp(menu.windowX, -1.0f, 100000.0f);
    Clamp(menu.windowY, -1.0f, 100000.0f);
    Clamp(menu.windowWidth, 200.0f, 8192.0f);
    Clamp(menu.windowHeight, 120.0f, 8192.0f);
}
}

std::vector<IConfigItem*>& Items()
{
    static std::vector<IConfigItem*> items;
    return items;
}

void RegisterItem(IConfigItem* item)
{
    if (item == nullptr)
        return;
    auto& items = Items();
    const auto existing = std::find_if(items.begin(), items.end(), [&](const IConfigItem* entry)
    {
        return entry != nullptr && entry->name == item->name;
    });
    if (existing == items.end())
        items.push_back(item);
    else
        *existing = item;
}

std::string PathUtf8(const std::string_view name)
{
    const std::wstring wide = ConfigPath(name).wstring();
    if (wide.empty())
        return {};
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()),
                                          nullptr, 0, nullptr, nullptr);
    std::string utf8(static_cast<std::size_t>(std::max(bytes, 0)), '\0');
    if (bytes > 0)
    {
        WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()),
                            utf8.data(), bytes, nullptr, nullptr);
    }
    return utf8;
}

bool Save(const std::string_view name, std::string& message)
{
    try
    {
        EnsureRegistered();
        Json json;
        json["version"] = kConfigVersion;
        for (IConfigItem* item : Items())
        {
            if (item != nullptr)
                item->Save(json);
        }

        if (!WriteJsonAtomically(ConfigPath(name), json, message))
            return false;

        RememberConfigName(name);
        message = "Configuration saved";
        return true;
    }
    catch (const std::exception& exception)
    {
        message = exception.what();
        return false;
    }
}

bool Load(const std::string_view name, std::string& message)
{
    try
    {
        EnsureRegistered();
        std::ifstream input(ConfigPath(name), std::ios::binary);
        if (!input)
        {
            message = "Configuration file does not exist";
            return false;
        }

        const Json json = Json::parse(input, nullptr, true, true);
        const int version = json.value("version", 1);
        std::size_t invalid = 0;
        for (IConfigItem* item : Items())
        {
            if (item != nullptr)
                item->ResetDefault();
        }
        for (IConfigItem* item : Items())
        {
            if (item == nullptr)
                continue;
            try
            {
                item->Load(json);
            }
            catch (...)
            {
                ++invalid;
            }
        }
        if (version < 3)
        {
            auto& spectator = features::Spectator::Instance().Settings();
            if (spectator.spectateToggleKey == 'F')
                spectator.spectateToggleKey = VK_F2;
        }
        Validate();
        RememberConfigName(name);
        message = invalid == 0 ? "Configuration loaded" :
            "Configuration loaded with " + std::to_string(invalid) + " invalid values ignored";
        return true;
    }
    catch (const std::exception& exception)
    {
        message = exception.what();
        return false;
    }
}

bool LoadStartupOnce(std::string& loadedName, std::string& message)
{
    std::lock_guard lock(g_startupMutex);
    if (!g_startupAttempted)
    {
        g_startupAttempted = true;
        g_startupName = ResolveStartupName();
        g_startupLoaded = Load(g_startupName, g_startupMessage);
    }
    loadedName = g_startupName;
    message = g_startupMessage;
    return g_startupLoaded;
}

bool OpenDirectory(std::string& message)
{
    try
    {
        const std::filesystem::path directory = ConfigDirectory();
        std::filesystem::create_directories(directory);
        const HINSTANCE result = ShellExecuteW(nullptr, L"open", directory.c_str(),
                                               nullptr, nullptr, SW_SHOWNORMAL);
        if (reinterpret_cast<INT_PTR>(result) <= 32)
        {
            message = "Cannot open configuration directory";
            return false;
        }
        message = "Configuration directory opened";
        return true;
    }
    catch (const std::exception& exception)
    {
        message = exception.what();
        return false;
    }
}
}

#include "ui/menu.hpp"

#include "core/config.hpp"
#include "features/array_list.hpp"
#include "features/esp.hpp"
#include "features/exit_activator.hpp"
#include "features/model_browser.hpp"
#include "features/movement.hpp"
#include "features/notifications.hpp"
#include "features/pickup.hpp"
#include "features/session_limit.hpp"
#include "features/spectator.hpp"
#include "features/spawner.hpp"
#include "features/visuals.hpp"
#include "features/vehicle_flight.hpp"
#include "render/renderer.hpp"

#include <Windows.h>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace etb::ui
{
namespace
{
int g_capturingBinding = 0;
bool g_waitingForKeyRelease = false;
bool& g_chineseMode = core::config::MenuSettings::chineseMode;
bool g_configLoadAttempted = false;
bool g_configStatusSuccess = true;
std::string g_configStatus;
char g_configName[128] = "default";
bool g_spawnWindowOpen = false;
int g_selectedSpawnEntry = -1;
char g_spawnerFilter[64] = "bp_";
std::string g_spawnerStatus;
bool g_modelWindowOpen = false;
int g_selectedModelEntry = -1;
char g_modelFilter[64] = "";
std::string g_modelStatus;
bool g_windowPlacementDirty = true;

const char* Language(const char* english, const char* chinese)
{
    return g_chineseMode ? chinese : english;
}

void ApplyMenuWindowPlacement()
{
    const ImVec2 display = ImGui::GetIO().DisplaySize;

    constexpr float kDefaultWidth = 460.0f;
    constexpr float kDefaultHeight = 420.0f;
    constexpr float kMinVisible = 64.0f;

    float width = core::config::MenuSettings::windowWidth;
    float height = core::config::MenuSettings::windowHeight;
    float x = core::config::MenuSettings::windowX;
    float y = core::config::MenuSettings::windowY;

    if (!std::isfinite(width) || width < 200.0f)
        width = kDefaultWidth;
    if (!std::isfinite(height) || height < 120.0f)
        height = kDefaultHeight;
    width = std::clamp(width, 200.0f, display.x);
    height = std::clamp(height, 120.0f, display.y);

    const bool hasPosition = std::isfinite(x) && std::isfinite(y) && x >= 0.0f && y >= 0.0f;
    const bool visible = hasPosition &&
        x + width > kMinVisible &&
        y + 24.0f > 0.0f &&
        y < display.y - 24.0f &&
        x < display.x - kMinVisible;

    if (!visible)
    {
        width = std::max(200.0f, std::min(kDefaultWidth, display.x - 40.0f));
        height = std::max(120.0f, std::min(kDefaultHeight, display.y - 60.0f));
        x = std::floor((display.x - width) * 0.5f);
        y = std::floor((display.y - height) * 0.5f);
    }

    core::config::MenuSettings::windowX = x;
    core::config::MenuSettings::windowY = y;
    core::config::MenuSettings::windowWidth = width;
    core::config::MenuSettings::windowHeight = height;

    ImGui::SetNextWindowPos(ImVec2(x, y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Always);
}

void FullWidthSliderFloat(const char* label, const char* id, float* value,
                          float minimum, float maximum, const char* format)
{
    ImGui::TextUnformatted(label);
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::SliderFloat(id, value, minimum, maximum, format);
}

void FullWidthSliderInt(const char* label, const char* id, int* value,
                        int minimum, int maximum, const char* format)
{
    ImGui::TextUnformatted(label);
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::SliderInt(id, value, minimum, maximum, format);
}

bool AnyVirtualKeyDown()
{
    for (int key = 1; key < 256; ++key)
    {
        if ((GetAsyncKeyState(key) & 0x8000) != 0)
            return true;
    }
    return false;
}

std::string VirtualKeyName(int key)
{
    switch (key)
    {
    case 0: return Language("None", "无");
    case VK_LBUTTON: return Language("Mouse 1", "鼠标 1");
    case VK_RBUTTON: return Language("Mouse 2", "鼠标 2");
    case VK_MBUTTON: return Language("Mouse 3", "鼠标 3");
    case VK_XBUTTON1: return Language("Mouse 4", "鼠标 4");
    case VK_XBUTTON2: return Language("Mouse 5", "鼠标 5");
    case VK_INSERT: return Language("Insert", "Insert");
    case VK_DELETE: return Language("Delete", "Delete");
    case VK_HOME: return Language("Home", "Home");
    case VK_END: return Language("End", "End");
    case VK_PRIOR: return Language("Page Up", "Page Up");
    case VK_NEXT: return Language("Page Down", "Page Down");
    default: break;
    }

    UINT scanCode = MapVirtualKeyA(static_cast<UINT>(key), MAPVK_VK_TO_VSC);
    if (key == VK_LEFT || key == VK_UP || key == VK_RIGHT || key == VK_DOWN ||
        key == VK_RCONTROL || key == VK_RMENU || key == VK_LWIN || key == VK_RWIN)
    {
        scanCode |= 0x100;
    }

    char name[64]{};
    if (GetKeyNameTextA(static_cast<LONG>(scanCode << 16), name, static_cast<int>(std::size(name))) > 0)
        return name;
    return Language("Key ", "按键 ") + std::to_string(key);
}

void KeyBindButton(const char* id, int bindingId, int& key)
{
    const bool capturing = g_capturingBinding == bindingId;
    const std::string label = capturing ? Language("Press a key...", "请按下按键...") : VirtualKeyName(key);

    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::Button((label + id).c_str(), ImVec2(-1.0f, 0.0f)))
    {
        g_capturingBinding = bindingId;
        g_waitingForKeyRelease = true;
    }

    if (!capturing)
        return;

    if (g_waitingForKeyRelease)
    {
        if (!AnyVirtualKeyDown())
            g_waitingForKeyRelease = false;
        return;
    }

    for (int candidate = 1; candidate < 256; ++candidate)
    {
        if ((GetAsyncKeyState(candidate) & 0x8000) == 0)
            continue;

        if (candidate != VK_ESCAPE)
            key = candidate;
        g_capturingBinding = 0;
        g_waitingForKeyRelease = false;
        break;
    }
}

void DrawVisualSettings(features::EspSettings& settings)
{
    ImGui::Checkbox(Language("Enable ESP", "启用 ESP"), &settings.enabled);
    ImGui::SeparatorText(Language("Drawing", "绘制"));
    ImGui::Checkbox(Language("Boxes", "方框"), &settings.boxes);
    ImGui::Checkbox(Language("3D boxes", "3D 方框"), &settings.threeDBoxes);
    ImGui::Checkbox(Language("Filled boxes", "填充方框"), &settings.filledBoxes);
    ImGui::Checkbox(Language("Labels", "标签"), &settings.labels);
    ImGui::Checkbox(Language("Distance", "距离"), &settings.distance);
    ImGui::Checkbox(Language("Tracers", "追踪线"), &settings.tracers);
    if (settings.tracers)
    {
        ImGui::Indent();
        ImGui::Checkbox(Language("Monsters only", "仅怪物"), &settings.monsterTracersOnly);
        ImGui::Unindent();
    }

    FullWidthSliderFloat(Language("Fill opacity", "填充透明度"), "##fill-opacity", &settings.fillOpacity, 0.0f, 0.50f, "%.2f");
    FullWidthSliderFloat(Language("Card size", "卡片大小"), "##card-size", &settings.cardScale, 0.65f, 1.60f, "%.2fx");

    ImGui::SeparatorText(Language("China hat", "中国帽"));
    ImGui::Checkbox(Language("Enable china hat", "启用中国帽"), &settings.chinaHat);
    if (settings.chinaHat)
    {
        ImGui::Indent();
        ImGui::Checkbox(Language("Self", "自身"), &settings.chinaHatSelf);
        ImGui::Checkbox(Language("Monsters", "怪物"), &settings.chinaHatMonsters);
        ImGui::Checkbox(Language("Players", "玩家"), &settings.chinaHatPlayers);
        ImGui::ColorEdit4(Language("Color", "颜色"), settings.chinaHatColor.data());
        FullWidthSliderFloat(Language("Brim radius", "帽檐半径"), "##china-hat-width", &settings.chinaHatWidth, 0.10f, 2.50f, "%.2f");
        FullWidthSliderFloat(Language("Cone height", "帽锥高度"), "##china-hat-height", &settings.chinaHatHeight, 0.05f, 2.00f, "%.2f");
        FullWidthSliderFloat(Language("Brim droop", "帽檐下垂"), "##china-hat-droop", &settings.chinaHatDroop, -0.60f, 1.20f, "%.2f");
        FullWidthSliderInt(Language("Segments", "分段"), "##china-hat-segments", &settings.chinaHatSegments, 6, 96, "%d");
        FullWidthSliderFloat(Language("Vertical offset", "垂直偏移"), "##china-hat-offset", &settings.chinaHatOffset, -1.0f, 1.0f, "%.2f");
        FullWidthSliderFloat(Language("Side tilt", "左右倾斜"), "##china-hat-tilt", &settings.chinaHatTilt, -1.50f, 1.50f, "%.2f");
        FullWidthSliderFloat(Language("Forward tilt", "前后倾斜"), "##china-hat-tilt-forward", &settings.chinaHatTiltForward, -1.50f, 1.50f, "%.2f");
        ImGui::Checkbox(Language("Filled", "填充"), &settings.chinaHatFilled);
        if (settings.chinaHatFilled)
        {
            FullWidthSliderFloat(Language("Fill opacity", "填充透明度"), "##china-hat-fill-opacity", &settings.chinaHatFillOpacity, 0.0f, 1.0f, "%.2f");
        }
        ImGui::Checkbox(Language("Outline", "描边"), &settings.chinaHatOutline);
        if (settings.chinaHatOutline)
        {
            FullWidthSliderFloat(Language("Outline thickness", "描边粗细"), "##china-hat-thickness", &settings.chinaHatThickness, 0.5f, 5.0f, "%.2f");
        }
        ImGui::Checkbox(Language("Rim arc", "帽檐弧线"), &settings.chinaHatFarRim);
        ImGui::Checkbox(Language("Ribs", "帽骨"), &settings.chinaHatRibs);
        if (settings.chinaHatRibs)
        {
            FullWidthSliderInt(Language("Rib count", "帽骨数量"), "##china-hat-rib-count", &settings.chinaHatRibCount, 2, 48, "%d");
        }
        ImGui::Unindent();
    }
}

void DrawFilterSettings(features::EspSettings& settings)
{
    ImGui::SeparatorText(Language("Entities", "实体"));
    ImGui::Checkbox(Language("Monsters", "怪物"), &settings.monsters);
    ImGui::Checkbox(Language("Players", "玩家"), &settings.players);
    if (settings.players)
    {
        ImGui::Indent();
        ImGui::Checkbox(Language("Player name tags", "玩家名称标签"), &settings.playerNameTags);
        ImGui::Checkbox(Language("Player sanity", "玩家理智值"), &settings.playerSanity);
        ImGui::Unindent();
    }
    ImGui::Checkbox(Language("Entity and player speed", "实体和玩家速度"), &settings.entitySpeed);
    ImGui::Checkbox(Language("Items", "物品"), &settings.items);
    if (settings.items)
    {
        ImGui::Indent();
        FullWidthSliderFloat(Language("Item merge screen radius", "物品合并屏幕半径"), "##item-merge-screen-radius",
                             &settings.itemMergeScreenDistancePixels,
                             20.0f, 400.0f, "%.0f px");
        FullWidthSliderFloat(Language("Item merge depth tolerance", "物品合并深度容差"), "##item-merge-depth",
                             &settings.itemMergeDepthMeters,
                             1.0f, 50.0f, "%.0f m");
        ImGui::Unindent();
    }
    ImGui::Checkbox(Language("Exits", "出口"), &settings.exits);
    ImGui::Checkbox(Language("Path to exit", "通往出口的路线"), &settings.pathToExit);
    ImGui::Checkbox(Language("Entity tracking route", "实体仇恨追踪路线"), &settings.entityTracking);
    if (settings.entityTracking)
    {
        ImGui::Indent();
        FullWidthSliderInt(Language("Tracked entities (nearest)", "追踪实体数量（最近的）"),
                           "##entity-tracking-count", &settings.entityTrackingCount, 1, 16, "%d");
        ImGui::Unindent();
    }

    ImGui::SeparatorText(Language("Limits", "限制"));
    FullWidthSliderFloat(Language("Maximum distance", "最大距离"), "##maximum-distance", &settings.maxDistanceMeters,
                         25.0f, 1500.0f, "%.0f m");
    FullWidthSliderInt(Language("Actor refresh", "Actor 刷新间隔"), "##actor-refresh", &settings.refreshIntervalMs,
                       100, 2000, "%d ms");
}

void DrawMovementSettings(features::MovementSettings& settings)
{
    ImGui::SeparatorText(Language("Listen server", "监听服务器"));
    ImGui::Checkbox(Language("Accept modded client movement", "接受修改客户端的移动"), &settings.hostAcceptClientMovement);

    ImGui::SeparatorText(Language("Overrides", "数值覆盖"));
    ImGui::Checkbox(Language("Infinite stamina", "无限体力"), &settings.infiniteStamina);

    ImGui::Checkbox(Language("Auto sprint", "自动疾跑"), &settings.autoSprint);
    ImGui::Checkbox(Language("Quick stop (release WASD to halt)", "快速停止（松开方向键立即停下）"),
                    &settings.quickStop);
    ImGui::Checkbox(Language("Bypass server movement correction (multiplayer)",
                             "绕过多人移动同步（不被服务器拉回）"),
                    &settings.serverMoveBypass);
    ImGui::Checkbox(Language("Bunny hop", "连跳"), &settings.bunnyHop);
    if (settings.bunnyHop)
    {
        ImGui::Indent();
        ImGui::Checkbox(Language("Auto jump (jump while moving)", "自动跳跃（移动时自动跳）"),
                        &settings.bhopAutoJump);
        if (settings.bhopAutoJump)
        {
            FullWidthSliderFloat(Language("Auto jump speed threshold (m/s)", "自动跳跃速度阈值（m/s）"),
                                 "##bhop-auto-jump-speed", &settings.bhopAutoJumpSpeed,
                                 0.0f, 10.0f, "%.2f");
        }
        ImGui::Unindent();
    }

    ImGui::Checkbox(Language("Override walk and sprint speed", "覆盖行走和奔跑速度"), &settings.speedEnabled);
    if (settings.speedEnabled)
    {
        ImGui::Indent();
        FullWidthSliderFloat(Language("Walk speed", "行走速度"), "##walk-speed", &settings.walkSpeed, 50.0f, 3000.0f, "%.0f");
        FullWidthSliderFloat(Language("Sprint speed", "奔跑速度"), "##sprint-speed", &settings.sprintSpeed, 50.0f, 5000.0f, "%.0f");
        ImGui::Unindent();
    }

    ImGui::Checkbox(Language("Override crouch speed", "覆盖蹲伏速度"), &settings.crouchSpeedEnabled);
    if (settings.crouchSpeedEnabled)
    {
        ImGui::Indent();
        FullWidthSliderFloat(Language("Crouch speed", "蹲伏速度"), "##crouch-speed", &settings.crouchSpeed,
                             25.0f, 3000.0f, "%.0f");
        ImGui::Unindent();
    }

    ImGui::Checkbox(Language("Override jump velocity", "覆盖跳跃速度"), &settings.jumpEnabled);
    if (settings.jumpEnabled)
    {
        ImGui::Indent();
        FullWidthSliderFloat(Language("Jump velocity", "跳跃速度"), "##jump-velocity", &settings.jumpVelocity,
                             0.0f, 3000.0f, "%.0f");
        ImGui::Unindent();
    }

    ImGui::Checkbox(Language("Override gravity", "覆盖重力"), &settings.gravityEnabled);
    if (settings.gravityEnabled)
    {
        ImGui::Indent();
        FullWidthSliderFloat(Language("Gravity scale", "重力倍率"), "##gravity-scale", &settings.gravityScale,
                             0.0f, 5.0f, "%.2f");
        ImGui::Unindent();
    }

    ImGui::SeparatorText(Language("Air turn", "空中转向"));
    ImGui::Checkbox(Language("Air steering", "空中转向"), &settings.airTurnEnabled);
    if (settings.airTurnEnabled)
    {
        ImGui::Indent();
        FullWidthSliderFloat(Language("Air control", "空中转向幅度"), "##air-control",
            &settings.airTurnControl, 0.0f, 1.0f, "%.2f");
        FullWidthSliderFloat(Language("Air acceleration", "空中转向加速度"), "##air-accel",
            &settings.airTurnAcceleration, 500.0f, 30000.0f, "%.0f");
        ImGui::Unindent();
    }

    ImGui::SeparatorText(Language("Flight", "飞行"));
    ImGui::Checkbox(Language("Flight mode", "飞行模式"), &settings.flightEnabled);
    ImGui::TextUnformatted(Language("Flight toggle key", "飞行切换键"));
    KeyBindButton("##flight-key", 1, settings.flightToggleKey);
    ImGui::Checkbox(Language("No clip", "穿墙"), &settings.noClipEnabled);
    ImGui::TextUnformatted(Language("No clip toggle key", "穿墙切换键"));
    KeyBindButton("##no-clip-key", 2, settings.noClipToggleKey);
    if (settings.flightEnabled || settings.noClipEnabled)
    {
        ImGui::Indent();
        FullWidthSliderFloat(Language("Flight speed", "飞行速度"), "##flight-speed", &settings.flySpeed,
                             50.0f, 5000.0f, "%.0f");
        ImGui::Unindent();
    }
}

void DrawSpectatorSettings(features::SpectatorSettings& settings,
                           const features::SpectatorStatus& status)
{
    ImGui::PushID("spectator-freecam");
    ImGui::SeparatorText(Language("Spectator / Free camera", "观战 / 自由视角"));
    ImGui::Text(Language("Game thread: %s", "游戏线程：%s"),
                status.gameThreadHookReady ? Language("Ready", "就绪") : Language("Unavailable", "不可用"));
    ImGui::Text(Language("Controller: %s | Camera target: %s", "控制器：%s | 相机目标：%s"),
                status.controllerReady ? Language("Ready", "就绪") : Language("Waiting", "等待中"),
                status.spectatorPawnReady ? Language("Ready", "就绪") : Language("Waiting", "等待中"));
    ImGui::Text(Language("State: %s | Authority: %s", "状态：%s | 权限：%s"),
                status.isFreeCam ? Language("Free camera", "自由视角") :
                (status.isSpectating ? Language("Watching", "观战中") : Language("Idle", "空闲")),
                status.authority ? Language("Host", "房主") : Language("Client", "客户端"));

    ImGui::SeparatorText(Language("Spectate Teammates", "观战队友"));
    if (ImGui::Checkbox(Language("Spectate teammates", "观战队友"), &settings.spectateEnabled) &&
        settings.spectateEnabled)
    {
        settings.freeCamEnabled = false;
    }
    if (status.isSpectating && !status.isFreeCam)
    {
        ImGui::Text(Language("Players: %zu | Index: %d", "玩家：%zu | 索引：%d"),
                    status.playerCount, status.currentSpectateIndex);
        if (!status.currentPlayerName.empty())
            ImGui::Text(Language("Watching: %s", "当前观战：%s"), status.currentPlayerName.c_str());
    }
    ImGui::TextUnformatted(Language("Spectate toggle key", "观战切换快捷键"));
    KeyBindButton("##spectate-toggle", 10, settings.spectateToggleKey);
    if (settings.spectateEnabled)
    {
        ImGui::TextUnformatted(Language("Next player key", "切换下一个玩家"));
        KeyBindButton("##spectate-next", 11, settings.spectateNextKey);
        ImGui::TextUnformatted(Language("Previous player key", "切换上一个玩家"));
        KeyBindButton("##spectate-prev", 12, settings.spectatePrevKey);
    }

    ImGui::SeparatorText(Language("Free Camera", "自由视角"));
    if (ImGui::Checkbox(Language("Free camera (fly anywhere)", "自由视角（自由飞行）"),
                        &settings.freeCamEnabled) && settings.freeCamEnabled)
    {
        settings.spectateEnabled = false;
    }
    ImGui::TextUnformatted(Language("Free camera toggle key", "自由视角切换快捷键"));
    KeyBindButton("##freecam-toggle", 13, settings.freeCamToggleKey);
    if (settings.freeCamEnabled)
    {
        ImGui::Indent();
        ImGui::Checkbox(Language("No clip", "穿墙"), &settings.freeCamNoClip);
        FullWidthSliderFloat(Language("Fly speed", "飞行速度"), "##freecam-speed",
                             &settings.freeCamSpeed, 100.0f, 10000.0f, "%.0f");
        ImGui::TextUnformatted(Language("WASD fly | Space/Ctrl up/down | Shift boost",
                                        "WASD 飞行 | 空格/Ctrl 升降 | Shift 加速"));
        ImGui::Unindent();
    }
    ImGui::PopID();
}

void DrawRenderSettings(features::VisualSettings& settings, const features::VisualStatus& status)
{
    ImGui::SeparatorText(Language("Environment brightness", "环境亮度"));
    ImGui::Checkbox(Language("Environment brightness", "环境亮度"), &settings.nightVisionEnabled);
    if (settings.nightVisionEnabled)
    {
        ImGui::Indent();
        FullWidthSliderFloat(Language("Environment brightness", "环境亮度"), "##night-vision-strength",
                             &settings.nightVisionStrength, 1.0f, 20.0f, "%.1fx");
        ImGui::Checkbox(Language("Legacy exposure boost", "旧版曝光增强"), &settings.nightVisionExposureBoost);
        if (settings.nightVisionExposureBoost)
        {
            FullWidthSliderFloat(Language("Exposure compensation", "曝光补偿"), "##night-exposure-bias",
                                 &settings.nightVisionExposureBias,
                                 0.0f, 8.0f, "+%.1f EV");
        }
        ImGui::Checkbox(Language("Remove distance fog (can darken fog-lit maps)", "移除远距离雾（依靠雾照明的关卡可能变暗）"),
                        &settings.nightVisionRemoveFog);
        ImGui::Checkbox(Language("Always-on flashlight lighting", "常驻手电筒补光"),
                        &settings.nightVisionPersistentLight);
        FullWidthSliderFloat(Language("Ambient light range", "环境光范围"), "##night-light-range",
                             &settings.nightVisionLightRangeMeters,
                             20.0f, 300.0f, "%.0f m");
        FullWidthSliderFloat(Language("Ambient light intensity", "环境光强度"), "##night-light-intensity",
                             &settings.nightVisionLightIntensity,
                             1.0f, 20.0f, "%.0f");
        ImGui::Checkbox(Language("Extend equipped flashlight", "扩展已装备手电筒"), &settings.nightVisionExtendFlashlight);
        if (settings.nightVisionExtendFlashlight)
        {
            ImGui::TextDisabled(Language("Also widens the forward flashlight beam.", "同时扩大前向手电筒光束。"));
        }
        ImGui::Text(Language("World lights: Sky %u | Other %u", "世界灯光：天空光 %u | 其他 %u"),
                    status.boostedSkyLights, status.boostedDirectionalLights);
        ImGui::Text(Language("Flashlight fallback: %s", "手电筒补光：%s"),
                    status.darknessLightReady ? Language("Ready", "就绪") : Language("Unavailable", "不可用"));
        ImGui::TextDisabled(Language("Boosts real light components, including Blueprint-owned lights.", "增强真实灯光组件，包括蓝图持有的灯光。"));
        ImGui::Unindent();
    }

    ImGui::SeparatorText(Language("Camera", "相机"));
    ImGui::Checkbox(Language("Third person", "第三人称"), &settings.thirdPersonEnabled);
    ImGui::TextUnformatted(Language("Third person toggle key", "第三人称切换键"));
    KeyBindButton("##third-person-key", 6, settings.thirdPersonToggleKey);
    if (settings.thirdPersonEnabled)
    {
        ImGui::Indent();
        FullWidthSliderFloat(Language("Camera distance", "相机距离"), "##third-person-distance", &settings.thirdPersonDistance,
                             50.0f, 1000.0f, "%.0f");
        FullWidthSliderFloat(Language("Camera height", "相机高度"), "##third-person-height", &settings.thirdPersonHeight,
                             -500.0f, 500.0f, "%.0f");
        ImGui::Unindent();
    }

    ImGui::SeparatorText(Language("Derp", "模型旋转"));
    ImGui::Checkbox(Language("Derp model rotation", "模型乱转"), &settings.derpEnabled);
    if (settings.derpEnabled)
    {
        ImGui::Indent();
        ImGui::Checkbox(Language("Network visible (host only)", "网络可见（仅房主）"),
                        &settings.derpNetworkVisible);
        FullWidthSliderFloat(Language("Pitch speed", "俯仰速度"), "##derp-pitch-speed", &settings.derpPitchSpeed,
                             -1080.0f, 1080.0f, "%+.0f deg/s");
        FullWidthSliderFloat(Language("Yaw speed", "偏航速度"), "##derp-yaw-speed", &settings.derpYawSpeed,
                             -10000.0f, 10000.0f, "%+.0f deg/s");
        FullWidthSliderFloat(Language("Roll speed", "翻滚速度"), "##derp-roll-speed", &settings.derpRollSpeed,
                             -1080.0f, 1080.0f, "%+.0f deg/s");
        ImGui::TextDisabled(Language("Mesh rotation is host-synchronized; camera and WASD stay independent.", "模型旋转由房主同步；相机和 WASD 保持独立。"));
        ImGui::Unindent();
    }

    ImGui::SeparatorText("Runtime");
    ImGui::Text("Camera: %s%s", status.cameraReady ? Language("Ready", "就绪") : Language("Waiting", "等待中"),
                status.usingVehicle ? Language(" (Vehicle)", "（载具）") : "");
    ImGui::Text("Spring arm: %s", status.usingVehicle ? Language("Not used (Vehicle)", "未使用（载具）") :
                (status.springArmReady ? Language("Ready", "就绪") : Language("Waiting", "等待中")));
    ImGui::Text("Third-person model: %s", status.modelReady ? Language("Ready", "就绪") : Language("Waiting", "等待中"));
    ImGui::Text("Suppressed fog components: %u", status.suppressedFogComponents);
    ImGui::Text("Boosted sky lights: %u", status.boostedSkyLights);
    ImGui::Text("Boosted directional lights: %u", status.boostedDirectionalLights);
}

void DrawClassSettings(features::EspSettings& settings)
{
    ImGui::SeparatorText("Class inspector");
    ImGui::Checkbox("Show all actor class names", &settings.classNamesEnabled);
    if (settings.classNamesEnabled)
    {
        ImGui::Indent();
        ImGui::Checkbox(Language("Show class distance", "显示类名距离"), &settings.classNameDistance);
        FullWidthSliderFloat(Language("Class maximum distance", "类名最大距离"), "##class-maximum-distance",
                             &settings.classNameMaxDistanceMeters, 5.0f, 1000.0f, "%.0f m");
        FullWidthSliderFloat(Language("Class label size", "类名标签大小"), "##class-label-size",
                             &settings.classNameScale, 0.60f, 1.40f, "%.2fx");
        ImGui::Unindent();
    }

    ImGui::SeparatorText(Language("Lever", "拉闸"));
    ImGui::Checkbox(Language("Highlight BP_Lever_C", "醒目标记 BP_Lever_C"), &settings.leverHighlight);
    if (settings.leverHighlight)
    {
        ImGui::Indent();
        FullWidthSliderFloat(Language("Lever maximum distance", "拉闸最大距离"), "##lever-maximum-distance",
                             &settings.leverMaxDistanceMeters, 10.0f, 2000.0f, "%.0f m");
        ImGui::Unindent();
    }

    ImGui::SeparatorText(Language("Valve", "阀门"));
    ImGui::Checkbox(Language("Highlight BP_Dark_Valve_C", "醒目标记 BP_Dark_Valve_C"), &settings.valveHighlight);
    if (settings.valveHighlight)
    {
        ImGui::Indent();
        FullWidthSliderFloat(Language("Valve maximum distance", "阀门最大距离"), "##valve-maximum-distance",
                             &settings.valveMaxDistanceMeters, 10.0f, 2000.0f, "%.0f m");
        ImGui::Unindent();
    }

    ImGui::SeparatorText(Language("Dynamic markers", "动态标记"));
    ImGui::Checkbox(Language("Mark moving unclassified actors", "标记移动的未分类 Actor"), &settings.dynamicMarkersEnabled);
    if (settings.dynamicMarkersEnabled)
    {
        ImGui::Indent();
        FullWidthSliderFloat(Language("Dynamic maximum distance", "动态标记最大距离"), "##dynamic-maximum-distance",
                             &settings.dynamicMaxDistanceMeters, 10.0f, 1500.0f, "%.0f m");
        FullWidthSliderFloat(Language("Minimum movement speed", "最小移动速度"), "##dynamic-minimum-speed",
                             &settings.dynamicMinimumSpeed, 0.02f, 5.0f, "%.2f m/s");
        FullWidthSliderFloat(Language("Three-move detection window", "三次移动检测时间窗"), "##dynamic-detection-window",
                             &settings.dynamicDetectionWindowSeconds, 0.30f, 10.0f, "%.2f s");
        FullWidthSliderFloat(Language("Marker display time", "标记显示时间"), "##dynamic-hold-time",
                             &settings.dynamicLingerSeconds, 1.0f, 30.0f, "%.1f s");
        ImGui::Unindent();
    }

    ImGui::SeparatorText(Language("New actor markers", "新生成实体标记"));
    ImGui::Checkbox(Language("Highlight newly generated classes", "醒目标记新生成的类"), &settings.spawnMarkersEnabled);
}
}

bool DrawMenu(const render::Renderer& renderer)
{
    if (!g_configLoadAttempted)
    {
        g_configLoadAttempted = true;
        std::string loadedName;
        std::string message;
        if (core::config::LoadStartupOnce(loadedName, message))
        {
            strncpy_s(g_configName, loadedName.c_str(), _TRUNCATE);
            g_configStatusSuccess = true;
            g_configStatus = Language("Configuration loaded automatically", "已自动加载配置");
            features::Notifications::Instance().Push(
                std::string(Language("[Config] Loaded ", "[配置] 已加载 ")) + loadedName,
                features::NotificationType::Success);
        }
        else if (message != "Configuration file does not exist")
        {
            g_configStatusSuccess = false;
            g_configStatus = message;
        }
    }

    features::ArrayListSettings& arrayListSettings = features::ArrayListHud::Instance().Settings();
    features::Notifications& notifications = features::Notifications::Instance();
    features::NotificationSettings& notificationSettings = notifications.Settings();
    features::Esp& esp = features::Esp::Instance();
    features::EspSettings& settings = esp.Settings();
    const features::EspStats& stats = esp.Stats();
    features::ExitActivator& exitActivator = features::ExitActivator::Instance();
    features::ExitActivatorSettings& exitSettings = exitActivator.Settings();
    const features::ExitActivatorStatus exitStatus = exitActivator.Status();
    features::Movement& movement = features::Movement::Instance();
    features::MovementSettings& movementSettings = movement.Settings();
    const features::MovementStatus& movementStatus = movement.Status();
    features::VehicleFlight& vehicleFlight = features::VehicleFlight::Instance();
    features::VehicleFlightSettings& vehicleSettings = vehicleFlight.Settings();
    const features::VehicleFlightStatus vehicleStatus = vehicleFlight.Status();
    features::Pickup& pickup = features::Pickup::Instance();
    features::PickupSettings& pickupSettings = pickup.Settings();
    const features::PickupStatus pickupStatus = pickup.Status();
    features::SessionLimit& sessionLimit = features::SessionLimit::Instance();
    features::SessionLimitSettings& sessionSettings = sessionLimit.Settings();
    const features::SessionLimitStatus sessionStatus = sessionLimit.Status();
    features::Visuals& visuals = features::Visuals::Instance();
    features::VisualSettings& visualSettings = visuals.Settings();
    const features::VisualStatus& visualStatus = visuals.Status();
    features::Spectator& spectator = features::Spectator::Instance();
    features::SpectatorSettings& spectatorSettings = spectator.Settings();
    const features::SpectatorStatus spectatorStatus = spectator.Status();

    if (g_windowPlacementDirty)
    {
        const ImVec2 display = ImGui::GetIO().DisplaySize;
        if (display.x >= 64.0f && display.y >= 64.0f)
        {
            ApplyMenuWindowPlacement();
            g_windowPlacementDirty = false;
        }
    }

    bool open = true;
    if (!ImGui::Begin("etb - internal", &open))
    {
        ImGui::End();
        return open;
    }

    if (!ImGui::IsWindowCollapsed())
    {
        const ImVec2 windowPos = ImGui::GetWindowPos();
        const ImVec2 windowSize = ImGui::GetWindowSize();
        core::config::MenuSettings::windowX = windowPos.x;
        core::config::MenuSettings::windowY = windowPos.y;
        core::config::MenuSettings::windowWidth = windowSize.x;
        core::config::MenuSettings::windowHeight = windowSize.y;
    }

    ImGui::Checkbox(Language("Chinese mode", "中文模式"), &g_chineseMode);
    ImGui::SeparatorText("按ins关闭菜单");

    if (ImGui::BeginTabBar("##main-tabs"))
    {
        if (ImGui::BeginTabItem("ESP"))
        {
            if (ImGui::BeginTable("##esp-columns", 2, ImGuiTableFlags_SizingStretchSame))
            {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                DrawVisualSettings(settings);
                ImGui::TableSetColumnIndex(1);
                DrawFilterSettings(settings);
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Host"))
        {
            ImGui::SeparatorText(Language("Session player limit", "房间玩家上限"));
            ImGui::Checkbox(Language("Override maximum players", "突破最大玩家数量"), &sessionSettings.enabled);
            FullWidthSliderInt(Language("Maximum players", "最大玩家数"), "##session-maximum-players",
                               &sessionSettings.maximumPlayers, 2, 128, "%d");

            ImGui::SeparatorText(Language("Host tools", "房主工具"));
            if (ImGui::Button(Language("Teleport me to the nearest exit", "传送我到最近的出口"),
                              ImVec2(-1.0f, 0.0f)))
                exitActivator.RequestTeleportToExit();
            FullWidthSliderFloat(Language("Ignore exits closer than", "只检测此距离（米）之外的出口"),
                                 "##exit-teleport-min-distance", &exitSettings.exitTeleportMinDistance,
                                 0.0f, 1000.0f, "%.0f m");
            if (ImGui::Button(Language("Teleport all members to me", "将所有成员传送到自身位置"), ImVec2(-1.0f, 0.0f)))
                exitActivator.RequestTeleportAllPlayers();

            ImGui::SeparatorText(Language("Member attributes", "成员属性"));
            ImGui::Checkbox(Language("Maintain member attributes", "持续维持成员属性"),
                            &exitSettings.maintainMemberAttributes);
            FullWidthSliderFloat(Language("Walk speed", "步行速度"), "##member-walk-speed",
                                 &exitSettings.memberWalkSpeed, 50.0f, 5000.0f, "%.0f");
            FullWidthSliderFloat(Language("Sprint speed", "冲刺速度"), "##member-sprint-speed",
                                 &exitSettings.memberSprintSpeed, 50.0f, 8000.0f, "%.0f");
            FullWidthSliderFloat(Language("Crouch speed", "蹲伏速度"), "##member-crouch-speed",
                                 &exitSettings.memberCrouchSpeed, 25.0f, 5000.0f, "%.0f");
            FullWidthSliderFloat(Language("Maximum stamina", "最大体力"), "##member-max-stamina",
                                 &exitSettings.memberMaxStamina, 1.0f, 10000.0f, "%.0f");
            ImGui::Checkbox(Language("Infinite stamina", "无限体力"),
                            &exitSettings.memberInfiniteStamina);
            if (ImGui::Button(Language("Apply attributes to all members", "应用属性到所有成员"),
                              ImVec2(-1.0f, 0.0f)))
                exitActivator.RequestApplyMemberAttributes();

            ImGui::SeparatorText(Language("Level 94: Animated King", "Level 94：杀手小丑"));
            if (ImGui::Button(Language("Start clown challenge now", "立即触发杀手小丑"), ImVec2(-1.0f, 0.0f)))
                exitActivator.RequestStartClownChallenge();
            if (ImGui::Button(Language("Complete 100-second challenge", "立即完成 100 秒挑战"), ImVec2(-1.0f, 0.0f)))
                exitActivator.RequestCompleteClownChallenge();
            if (ImGui::Button(Language("Start roller coaster now", "立即启动过山车"), ImVec2(-1.0f, 0.0f)))
                exitActivator.RequestStartRollercoaster();

            ImGui::SeparatorText(Language("Spawner (host)", "召唤（房主）"));
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputText("##spawner-filter", g_spawnerFilter, std::size(g_spawnerFilter));
            auto& spawnerSettings = features::Spawner::Instance().Settings();
            FullWidthSliderFloat(Language("Spawn distance", "生成距离"), "##spawner-distance",
                                 &spawnerSettings.spawnDistance, 0.0f, 2000.0f, "%.0f");
            FullWidthSliderInt(Language("Spawn count", "生成数量"), "##spawner-count",
                               &spawnerSettings.spawnCount, 1, 20, "%d");
            if (ImGui::Button(Language("Fetch class list", "获取类列表"), ImVec2(-1.0f, 0.0f)))
            {
                std::string message;
                if (features::Spawner::Instance().FetchList(g_spawnerFilter, 800, message))
                    g_spawnWindowOpen = true;
                g_spawnerStatus = message;
            }
            const auto fetchCat = [](features::SpawnCategory category)
            {
                std::string message;
                if (features::Spawner::Instance().FetchByCategory(category, 600, message))
                    g_spawnWindowOpen = true;
                g_spawnerStatus = message;
            };
            if (ImGui::Button(Language("Fetch buildings", "获取建筑"), ImVec2(-0.25f, 0.0f)))
                fetchCat(features::SpawnCategory::Building);
            ImGui::SameLine();
            if (ImGui::Button(Language("Items", "物品"), ImVec2(-0.25f, 0.0f)))
                fetchCat(features::SpawnCategory::Item);
            ImGui::SameLine();
            if (ImGui::Button(Language("Entities", "实体"), ImVec2(-0.25f, 0.0f)))
                fetchCat(features::SpawnCategory::Entity);
            ImGui::SameLine();
            if (ImGui::Button(Language("All", "全部"), ImVec2(-0.25f, 0.0f)))
            {
                std::string message;
                if (features::Spawner::Instance().FetchList("", 1200, message))
                    g_spawnWindowOpen = true;
                g_spawnerStatus = message;
            }
            static bool g_favLoaded = false;
            if (!g_favLoaded)
            {
                features::Spawner::Instance().LoadFavorites();
                g_favLoaded = true;
            }
            if (!g_spawnerStatus.empty())
            {
                ImGui::TextDisabled("%s", g_spawnerStatus.c_str());
                ImGui::SameLine();
                if (ImGui::SmallButton(Language("Clear", "清除")))
                {
                    g_spawnerStatus.clear();
                    features::Spawner::Instance().ClearList();
                }
            }
            ImGui::TextDisabled(Language("Leave the box empty to list every spawnable Actor class.",
                                         "输入框留空则列出全部可生成的 Actor 类。"));

            ImGui::SeparatorText(Language("Model browser", "模型浏览"));
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputText("##model-filter", g_modelFilter, std::size(g_modelFilter));
            if (ImGui::Button(Language("Scan loaded meshes", "扫描已加载模型"), ImVec2(-1.0f, 0.0f)))
            {
                std::string message;
                if (features::ModelBrowser::Instance().FetchModels(g_modelFilter, 4000, message))
                    g_modelWindowOpen = true;
                g_modelStatus = message;
            }
            if (!g_modelStatus.empty())
                ImGui::TextDisabled("%s", g_modelStatus.c_str());
            ImGui::TextDisabled(Language("Lists every Skeletal/Static mesh the game has loaded.",
                                         "列出游戏已加载的全部骨骼/静态网格。"));
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Movement"))
        {
            ImGui::SeparatorText(Language("Local character", "本地角色"));
            ImGui::Text(Language("Pawn: %s", "Pawn：%s"), movementStatus.pawnAttached ? Language("Ready", "就绪") : Language("Waiting", "等待中"));
            ImGui::Text(Language("Character movement: %s", "角色移动组件：%s"), movementStatus.movementComponentReady ? Language("Ready", "就绪") : Language("Waiting", "等待中"));
            ImGui::Text(Language("Authority: %s", "权限：%s"), movementStatus.hostAuthority ? Language("Host", "房主") : Language("Client", "客户端"));
            ImGui::Text(Language("Authorized pawns: %zu", "已授权 Pawn：%zu"), movementStatus.hostAuthorizedPawns);
            DrawMovementSettings(movementSettings);

            ImGui::SeparatorText(Language("Vehicle flight", "载具飞行"));
            ImGui::Text(Language("Game thread: %s", "游戏线程：%s"), vehicleStatus.gameThreadHookReady ? Language("Ready", "就绪") : Language("Unavailable", "不可用"));
            ImGui::Text(Language("Controlled boat: %s", "受控船只：%s"), vehicleStatus.vehicleDetected ? Language("Ready", "就绪") : Language("Waiting", "等待中"));
            ImGui::Text(Language("Runtime: %s / %s / sync %s", "运行状态：%s / %s / 同步 %s"),
                        vehicleStatus.active ? Language("Active", "活动") : Language("Idle", "空闲"),
                        vehicleStatus.authority ? Language("Host", "房主") : Language("Client", "客户端"),
                        vehicleStatus.networkSync ? Language("Ready", "就绪") : Language("Waiting", "等待中"));
            ImGui::Text(Language("Vehicle speed: %.2f m/s", "载具速度：%.2f m/s"), vehicleStatus.speedMetersPerSecond);
            ImGui::Checkbox(Language("Vehicle flight", "载具飞行"), &vehicleSettings.enabled);
            ImGui::TextUnformatted(Language("Vehicle flight toggle key", "载具飞行切换键"));
            KeyBindButton("##vehicle-flight-key", 4, vehicleSettings.toggleKey);
            ImGui::Checkbox(Language("Vehicle no clip", "载具穿墙"), &vehicleSettings.noClip);
            ImGui::TextUnformatted(Language("Vehicle no clip toggle key", "载具穿墙切换键"));
            KeyBindButton("##vehicle-no-clip-key", 5, vehicleSettings.noClipToggleKey);
            if (vehicleSettings.enabled || vehicleSettings.noClip)
            {
                ImGui::Indent();
                FullWidthSliderFloat(Language("Horizontal speed", "水平速度"), "##vehicle-horizontal-speed",
                                     &vehicleSettings.horizontalSpeed, 50.0f, 10000.0f, "%.0f");
                FullWidthSliderFloat(Language("Vertical speed", "垂直速度"), "##vehicle-vertical-speed",
                                     &vehicleSettings.verticalSpeed, 50.0f, 10000.0f, "%.0f");
                FullWidthSliderFloat(Language("Turn speed", "转向速度"), "##vehicle-turn-speed",
                                     &vehicleSettings.turnSpeedDegrees, 0.0f, 360.0f, "%.0f deg/s");
                FullWidthSliderFloat(Language("Boost multiplier", "加速倍率"), "##vehicle-boost",
                                     &vehicleSettings.boostMultiplier, 1.0f, 5.0f, "%.1fx");
                FullWidthSliderFloat(Language("Network rate", "网络同步频率"), "##vehicle-network-rate",
                                     &vehicleSettings.networkRateHz, 5.0f, 60.0f, "%.0f Hz");
                ImGui::TextUnformatted(Language("WASD camera-relative | Space/Ctrl vertical | Q/E turn | Shift boost", "WASD 按相机方向移动 | 空格/Ctrl 垂直移动 | Q/E 转向 | Shift 加速"));
                ImGui::Unindent();
            }

            DrawSpectatorSettings(spectatorSettings, spectatorStatus);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Pickup"))
        {
            ImGui::SeparatorText(Language("Range pickup", "范围拾取"));
            ImGui::Checkbox(Language("Range pickup", "范围拾取"), &pickupSettings.enabled);
            ImGui::Checkbox(Language("Automatic pickup", "自动拾取"), &pickupSettings.autoPickup);
            ImGui::Checkbox(Language("Exclude flashlights", "排除手电筒"), &pickupSettings.excludeFlashlights);
            if (pickupSettings.autoPickup)
            {
                ImGui::Indent();
                FullWidthSliderFloat(Language("Automatic pickup interval", "自动拾取间隔"), "##pickup-auto-interval",
                                     &pickupSettings.autoPickupIntervalSeconds,
                                     0.25f, 5.0f, "%.2f s");
                ImGui::Unindent();
            }
            ImGui::Text(Language("Game thread: %s", "游戏线程：%s"), pickupStatus.gameThreadHookReady ? Language("Ready", "就绪") : Language("Unavailable", "不可用"));
            ImGui::Text(Language("Last scan: %zu nearby / %zu sent / %zu queued", "上次扫描：附近 %zu / 已发送 %zu / 队列 %zu"),
                        pickupStatus.candidates, pickupStatus.requested, pickupStatus.queued);
            FullWidthSliderFloat(Language("Radius", "半径"), "##pickup-radius", &pickupSettings.radiusMeters,
                                 1.0f, 800.0f, "%.0f m");
            ImGui::Checkbox(Language("Teleport items to me before pickup (long range)",
                                     "拾取前先把物品瞬移到身边（远程拾取）"),
                            &pickupSettings.teleportItemsBeforePickup);
            FullWidthSliderInt(Language("Maximum per activation", "每次最多拾取"), "##pickup-maximum",
                               &pickupSettings.maxItemsPerActivation, 1, 32, "%d");
            FullWidthSliderInt(Language("Scan actors per game tick", "每个游戏 Tick 扫描 Actor 数"), "##pickup-scan-budget",
                               &pickupSettings.scanActorsPerTick, 50, 2000, "%d");
            ImGui::TextUnformatted(Language("Pickup hotkey", "拾取快捷键"));
            KeyBindButton("##pickup-key", 3, pickupSettings.hotkey);
            ImGui::BeginDisabled(!pickupSettings.enabled);
            if (ImGui::Button(pickupStatus.requestPending ? Language("Pickup pending...", "正在拾取...") : Language("Pick up nearby items", "拾取附近物品"),
                              ImVec2(-1.0f, 0.0f)))
                pickup.Request();
            ImGui::EndDisabled();

            ImGui::SeparatorText(Language("Host authorization", "房主授权"));
            ImGui::Checkbox(Language("Accept member range pickup (host)", "接受成员范围拾取（房主）"),
                            &pickupSettings.hostAcceptMemberPickup);
            FullWidthSliderFloat(Language("Host maximum radius", "房主最大半径"), "##pickup-host-radius",
                                 &pickupSettings.hostMaximumRadiusMeters,
                                 1.0f, 800.0f, "%.0f m");
            ImGui::Text(Language("Authorized requests: %zu", "已授权请求：%zu"), pickupStatus.hostAuthorized);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Render"))
        {
            DrawRenderSettings(visualSettings, visualStatus);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Classes"))
        {
            DrawClassSettings(settings);
            ImGui::SeparatorText(Language("Exit control", "出口控制"));
            ImGui::Checkbox(Language("Force activate existing exits", "强制激活现有出口"), &exitSettings.enabled);
            if (exitSettings.enabled)
            {
                ImGui::Indent();
                ImGui::Checkbox(Language("Enable exit trigger collision", "启用出口触发碰撞"),
                                &exitSettings.forceTriggerCollision);
                FullWidthSliderInt(Language("Exit scan actors per game tick", "每个游戏 Tick 扫描出口 Actor 数"), "##exit-scan-budget",
                                   &exitSettings.scanActorsPerTick, 50, 2000, "%d");
                ImGui::Text(Language("Game thread: %s | Authority: %s", "游戏线程：%s | 权限：%s"),
                            exitStatus.gameThreadHookReady ? Language("Ready", "就绪") : Language("Unavailable", "不可用"),
                            exitStatus.hostAuthority ? Language("Host", "房主") : Language("Client", "客户端"));
                ImGui::Text(Language("Scan: %s | Candidates: %zu | Applied: %zu", "扫描：%s | 候选：%zu | 已应用：%zu"),
                            exitStatus.scanning ? Language("Running", "运行中") :
                            (exitStatus.completed ? Language("Completed", "已完成") : Language("Waiting", "等待中")),
                            exitStatus.candidates, exitStatus.activated);
                ImGui::Unindent();
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("HUD"))
        {
            ImGui::SeparatorText(Language("ArrayList", "功能列表"));
            ImGui::Checkbox(Language("ArrayList", "功能列表"), &arrayListSettings.enabled);

            int mode = static_cast<int>(arrayListSettings.mode);
            ImGui::TextUnformatted(Language("Mode", "模式"));
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::Combo("##array-list-mode", &mode,
                             Language("None\0Outline\0Edge line\0Rounded\0", "无\0描边\0边缘线\0圆角\0")))
                arrayListSettings.mode = static_cast<features::ArrayListMode>(mode);

            int color = static_cast<int>(arrayListSettings.color);
            ImGui::TextUnformatted(Language("Color", "颜色"));
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::Combo("##array-list-color", &color,
                             Language("Rainbow\0Categorized\0Custom\0", "彩虹\0按类别\0自定义\0")))
                arrayListSettings.color = static_cast<features::ArrayListColor>(color);

            if (arrayListSettings.color == features::ArrayListColor::Rainbow)
            {
                int rainbow = static_cast<int>(arrayListSettings.rainbow);
                ImGui::TextUnformatted(Language("Rainbow style", "彩虹样式"));
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::Combo("##array-list-rainbow", &rainbow,
                                 Language("Pastel\0Saturated\0", "柔和\0饱和\0")))
                    arrayListSettings.rainbow = static_cast<features::ArrayListRainbow>(rainbow);
                FullWidthSliderFloat(Language("Rainbow step", "彩虹步进"), "##array-list-rainbow-step",
                                     &arrayListSettings.rainbowStepDegrees,
                                     0.0f, 60.0f, "%.1f deg");
            }
            else if (arrayListSettings.color == features::ArrayListColor::Custom)
            {
                ImGui::ColorEdit4(Language("Custom color", "自定义颜色"), arrayListSettings.customColor.data());
            }
            else
            {
                ImGui::ColorEdit4(Language("ESP category", "ESP 类别"), arrayListSettings.categoryColors[0].data());
                ImGui::ColorEdit4(Language("Movement category", "移动类别"), arrayListSettings.categoryColors[1].data());
                ImGui::ColorEdit4(Language("Visual category", "视觉类别"), arrayListSettings.categoryColors[2].data());
                ImGui::ColorEdit4(Language("Host category", "房主类别"), arrayListSettings.categoryColors[3].data());
            }

            ImGui::SeparatorText(Language("Text and rows", "文字与行"));
            ImGui::Checkbox(Language("Show Keybinds", "显示快捷键"), &arrayListSettings.showKeybinds);
            ImGui::Checkbox(Language("Show Modes", "显示参数"), &arrayListSettings.showModes);
            ImGui::Checkbox(Language("Text shadow", "文字阴影"), &arrayListSettings.textShadowEnabled);
            ImGui::Checkbox(Language("Touch model", "贴边模式"), &arrayListSettings.touchModelEnabled);
            FullWidthSliderFloat(Language("Text size", "文字大小"), "##array-list-text-size",
                                 &arrayListSettings.textSize, 6.0f, 32.0f, "%.1f px");
            FullWidthSliderFloat(Language("Row padding", "行间距"), "##array-list-row-padding",
                                 &arrayListSettings.rowPadding, 0.0f, 20.0f, "%.1f px");
            ImGui::ColorEdit4(Language("Background", "背景"), arrayListSettings.backgroundColor.data());
            if (arrayListSettings.showModes)
                ImGui::ColorEdit4(Language("Mode text", "参数文字"), arrayListSettings.modeTextColor.data());

            ImGui::SeparatorText(Language("Border", "边框"));
            FullWidthSliderFloat(Language("Border thickness (0 = auto)", "边框粗细（0 = 自动）"), "##array-list-border",
                                 &arrayListSettings.borderThickness, 0.0f, 6.0f, "%.1f px");
            if (arrayListSettings.mode == features::ArrayListMode::Rounded)
                FullWidthSliderFloat(Language("Corner radius", "圆角半径"), "##array-list-radius",
                                     &arrayListSettings.cornerRadius, 0.0f, 24.0f, "%.1f px");

            ImGui::SeparatorText(Language("Gravity and animation", "排列与动画"));
            int horizontal = static_cast<int>(arrayListSettings.horizontalGravity);
            ImGui::TextUnformatted(Language("Horizontal gravity", "水平对齐"));
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::Combo("##array-list-horizontal", &horizontal, Language("Left\0Right\0", "左\0右\0")))
                arrayListSettings.horizontalGravity =
                    static_cast<features::ArrayListHorizontalGravity>(horizontal);
            int vertical = static_cast<int>(arrayListSettings.verticalGravity);
            ImGui::TextUnformatted(Language("Vertical gravity", "垂直对齐"));
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::Combo("##array-list-vertical", &vertical, Language("Top\0Bottom\0", "顶部\0底部\0")))
                arrayListSettings.verticalGravity =
                    static_cast<features::ArrayListVerticalGravity>(vertical);
            ImGui::Checkbox(Language("Gravity changing animation", "对齐变化动画"),
                            &arrayListSettings.gravityChangingAnimationEnabled);
            ImGui::Checkbox(Language("Item closing animation", "项目关闭动画"),
                            &arrayListSettings.itemClosingAnimationEnabled);

            ImGui::SeparatorText(Language("Self information", "自身信息"));
            ImGui::Checkbox(Language("Self information HUD", "自身信息 HUD"), &arrayListSettings.selfInfoEnabled);
            if (arrayListSettings.selfInfoEnabled)
            {
                ImGui::Checkbox(Language("Frame rate", "帧率"), &arrayListSettings.selfInfoFps);
                ImGui::Checkbox(Language("Position", "位置"), &arrayListSettings.selfInfoPosition);
                ImGui::Checkbox(Language("Speed", "速度"), &arrayListSettings.selfInfoSpeed);
                ImGui::Checkbox(Language("Velocity", "速度向量"), &arrayListSettings.selfInfoVelocity);
                ImGui::Checkbox(Language("Movement mode", "移动模式"), &arrayListSettings.selfInfoMovementMode);
                FullWidthSliderFloat(Language("Info text size", "信息文字大小"), "##self-info-text-size",
                                     &arrayListSettings.selfInfoTextSize,
                                     8.0f, 32.0f, "%.1f px");
                FullWidthSliderFloat(Language("Info padding", "信息内边距"), "##self-info-padding",
                                     &arrayListSettings.selfInfoPadding,
                                     0.0f, 20.0f, "%.1f px");
                ImGui::ColorEdit4(Language("Info background", "信息背景"),
                                  arrayListSettings.selfInfoBackground.data());

                int infoHorizontal = static_cast<int>(
                    arrayListSettings.selfInfoHorizontalGravity);
                ImGui::TextUnformatted(Language("Info horizontal gravity", "信息水平对齐"));
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::Combo("##self-info-horizontal", &infoHorizontal, Language("Left\0Right\0", "左\0右\0")))
                {
                    arrayListSettings.selfInfoHorizontalGravity =
                        static_cast<features::ArrayListHorizontalGravity>(infoHorizontal);
                }
                int infoVertical = static_cast<int>(arrayListSettings.selfInfoVerticalGravity);
                ImGui::TextUnformatted(Language("Info vertical gravity", "信息垂直位置"));
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::Combo("##self-info-vertical", &infoVertical, Language("Top\0Lower middle\0", "顶部\0右中偏下\0")))
                {
                    arrayListSettings.selfInfoVerticalGravity =
                        static_cast<features::ArrayListVerticalGravity>(infoVertical);
                }
            }

            ImGui::SeparatorText("Notifications");
            ImGui::Checkbox(Language("Notifications", "通知"), &notificationSettings.enabled);
            if (notificationSettings.enabled)
            {
                ImGui::Checkbox(Language("Show on module toggle", "功能切换时显示"), &notificationSettings.showOnToggle);
                ImGui::Checkbox(Language("Scene scan summary", "场景扫描摘要"), &notificationSettings.sceneSummary);
                ImGui::Checkbox(Language("New discovery alerts", "新发现提醒"), &notificationSettings.discoveryAlerts);
                if (notificationSettings.discoveryAlerts)
                {
                    ImGui::Indent();
                    FullWidthSliderFloat(Language("Ignore discoveries within", "忽略此距离内的新发现"), "##discovery-ignore-radius",
                                         &notificationSettings.discoveryExclusionRadiusMeters,
                                         0.0f, 50.0f, "%.1f m");
                    ImGui::Unindent();
                }
                ImGui::Checkbox(Language("Control mode alerts", "控制模式提醒"), &notificationSettings.controlModeAlerts);
                ImGui::Checkbox(Language("Sanity stage warnings", "理智阶段警告"), &notificationSettings.sanityWarnings);
                if (notificationSettings.sanityWarnings)
                {
                    ImGui::Indent();
                    ImGui::TextUnformatted(Language("Stages: 50% / 30% / 10% (3 cards each)", "阶段：50% / 30% / 10%（每阶段 3 张卡片）"));
                    ImGui::Unindent();
                }
                FullWidthSliderFloat(Language("Size", "大小"), "##notification-size",
                                     &notificationSettings.sizePercent, 50.0f, 150.0f, "%.0f%%");
                FullWidthSliderFloat(Language("Duration", "持续时间"), "##notification-duration",
                                     &notificationSettings.durationSeconds, 0.5f, 10.0f, "%.1f s");
                FullWidthSliderFloat(Language("Duration multiplier", "持续时间倍率"), "##notification-duration-multiplier",
                                     &notificationSettings.durationMultiplier,
                                     0.5f, 3.0f, "%.2fx");
                ImGui::Checkbox(Language("Glow", "发光"), &notificationSettings.glow);
                if (notificationSettings.glow)
                {
                    FullWidthSliderFloat(Language("Glow strength", "发光强度"), "##notification-glow-strength",
                                         &notificationSettings.glowStrength,
                                         0.5f, 25.0f, "%.2f");
                }
                ImGui::Checkbox(Language("Color gradient", "颜色渐变"), &notificationSettings.colorGradient);
                ImGui::ColorEdit4(Language("Accent", "强调色"), notificationSettings.accentColor.data());
                if (notificationSettings.colorGradient)
                    ImGui::ColorEdit4(Language("Gradient", "渐变色"), notificationSettings.gradientColor.data());
                ImGui::Checkbox(Language("Acrylic background", "亚克力背景"),
                                &notificationSettings.acrylicBackground);
                ImGui::Checkbox(Language("Limit notifications", "限制通知数量"),
                                &notificationSettings.limitNotifications);
                if (notificationSettings.limitNotifications)
                {
                    FullWidthSliderInt(Language("Maximum notifications", "最大通知数量"), "##notification-maximum",
                                       &notificationSettings.maximumNotifications, 1, 25, "%d");
                }

            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Config"))
        {
            ImGui::SeparatorText(Language("Configuration", "配置管理"));
            if (ImGui::Checkbox(Language("Disable End key unload", "禁用 End 键卸载"),
                                &core::config::MenuSettings::disableEndUnload))
            {
                notifications.Push(
                    core::config::MenuSettings::disableEndUnload ?
                        Language("[Menu] End unload disabled", "[菜单] 已禁用 End 键卸载") :
                        Language("[Menu] End unload enabled", "[菜单] 已启用 End 键卸载"),
                    features::NotificationType::Info);
            }
            ImGui::Checkbox(Language("Draw mouse position circle", "绘制鼠标位置圆圈"),
                            &core::config::MenuSettings::mouseCircle);
            if (core::config::MenuSettings::mouseCircle)
            {
                ImGui::Indent();

                ImGui::SeparatorText(Language("Size & Thickness", "大小与线条"));
                {
                    ImGui::SliderFloat(Language("Radius", "半径"),
                                       &core::config::MenuSettings::mouseRadius, 2.0f, 24.0f, "%.1f");
                    ImGui::SliderFloat(Language("Thickness", "线条粗细"),
                                       &core::config::MenuSettings::mouseThickness, 0.5f, 6.0f, "%.1f");
                }

                ImGui::SeparatorText(Language("Colors", "颜色"));
                {
                    float mainCol[4] = {
                        static_cast<float>(core::config::MenuSettings::mouseColorR) / 255.0f,
                        static_cast<float>(core::config::MenuSettings::mouseColorG) / 255.0f,
                        static_cast<float>(core::config::MenuSettings::mouseColorB) / 255.0f,
                        static_cast<float>(core::config::MenuSettings::mouseColorA) / 255.0f,
                    };
                    float accentCol[3] = {
                        static_cast<float>(core::config::MenuSettings::mouseAccentR) / 255.0f,
                        static_cast<float>(core::config::MenuSettings::mouseAccentG) / 255.0f,
                        static_cast<float>(core::config::MenuSettings::mouseAccentB) / 255.0f,
                    };
                    ImGui::ColorEdit4(Language("Main color", "主色"), mainCol);
                    ImGui::ColorEdit3(Language("Accent color (shadow/outline)", "描边色"), accentCol);
                    core::config::MenuSettings::mouseColorR =
                        static_cast<int>(std::clamp(mainCol[0], 0.0f, 1.0f) * 255.0f + 0.5f);
                    core::config::MenuSettings::mouseColorG =
                        static_cast<int>(std::clamp(mainCol[1], 0.0f, 1.0f) * 255.0f + 0.5f);
                    core::config::MenuSettings::mouseColorB =
                        static_cast<int>(std::clamp(mainCol[2], 0.0f, 1.0f) * 255.0f + 0.5f);
                    core::config::MenuSettings::mouseColorA =
                        static_cast<int>(std::clamp(mainCol[3], 0.0f, 1.0f) * 255.0f + 0.5f);
                    core::config::MenuSettings::mouseAccentR =
                        static_cast<int>(std::clamp(accentCol[0], 0.0f, 1.0f) * 255.0f + 0.5f);
                    core::config::MenuSettings::mouseAccentG =
                        static_cast<int>(std::clamp(accentCol[1], 0.0f, 1.0f) * 255.0f + 0.5f);
                    core::config::MenuSettings::mouseAccentB =
                        static_cast<int>(std::clamp(accentCol[2], 0.0f, 1.0f) * 255.0f + 0.5f);
                }

                ImGui::SeparatorText(Language("Effects", "效果"));
                {
                    ImGui::Checkbox(Language("Glow halo", "发光光晕"), &core::config::MenuSettings::mouseGlowEnabled);
                    if (core::config::MenuSettings::mouseGlowEnabled)
                    {
                        ImGui::Indent();
                        ImGui::SliderFloat(Language("Glow radius", "光晕半径"),
                                           &core::config::MenuSettings::mouseGlowRadius, 0.5f, 8.0f, "%.1f");
                        ImGui::Unindent();
                    }
                }

                ImGui::SeparatorText(Language("Linear Follow Animation", "线性跟随动画"));
                {
                    ImGui::Checkbox(Language("Enable slow follow (smooth lag)", "启用慢速跟随（平滑拖拽感）"),
                                    &core::config::MenuSettings::mouseFollowEnabled);
                    if (core::config::MenuSettings::mouseFollowEnabled)
                    {
                        ImGui::Indent();
                        ImGui::SliderFloat(Language("Follow speed", "跟随速度"),
                                           &core::config::MenuSettings::mouseFollowSpeed, 1.0f, 60.0f, "%.1f");
                        ImGui::TextUnformatted(Language(
                            "Lower = slower smooth lag.  3 = very slow  |  14 = smooth  |  40 = tight",
                            "数值越小越慢、拖拽感越强。3=非常慢 | 14=顺滑 | 40=紧跟"));
                        ImGui::Unindent();
                    }
                }
                ImGui::Unindent();
            }
            ImGui::TextUnformatted(Language("Configuration name", "配置名称"));
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputText("##configuration-name", g_configName, std::size(g_configName));
            const std::string configName = g_configName[0] != '\0' ? g_configName : "default";
            const float spacing = ImGui::GetStyle().ItemSpacing.x;
            const float buttonWidth = (ImGui::GetContentRegionAvail().x - spacing) * 0.5f;
            if (ImGui::Button(Language("Save configuration", "保存配置"), ImVec2(buttonWidth, 0.0f)))
            {
                std::string message;
                g_configStatusSuccess = core::config::Save(configName, message);
                g_configStatus = g_configStatusSuccess ?
                    Language("Configuration saved", "配置已保存") : message;
                notifications.Push(
                    g_configStatusSuccess ?
                        std::string(Language("[Config] Saved ", "[配置] 已保存 ")) + configName :
                        std::string(Language("[Config] Save failed: ", "[配置] 保存失败：")) + message,
                    g_configStatusSuccess ? features::NotificationType::Success :
                                            features::NotificationType::Error);
            }
            ImGui::SameLine();
            if (ImGui::Button(Language("Load configuration", "加载配置"), ImVec2(-1.0f, 0.0f)))
            {
                std::string message;
                g_configStatusSuccess = core::config::Load(configName, message);
                if (g_configStatusSuccess)
                    g_windowPlacementDirty = true;
                g_configStatus = g_configStatusSuccess ?
                    Language("Configuration loaded", "配置已加载") : message;
                notifications.Push(
                    g_configStatusSuccess ?
                        std::string(Language("[Config] Loaded ", "[配置] 已加载 ")) + configName :
                        std::string(Language("[Config] Load failed: ", "[配置] 加载失败：")) + message,
                    g_configStatusSuccess ? features::NotificationType::Success :
                                            features::NotificationType::Error);
            }

            if (ImGui::Button(Language("Open configuration directory", "打开配置目录"), ImVec2(-1.0f, 0.0f)))
            {
                std::string message;
                g_configStatusSuccess = core::config::OpenDirectory(message);
                g_configStatus = g_configStatusSuccess ?
                    Language("Configuration directory opened", "配置目录已打开") : message;
                notifications.Push(
                    g_configStatusSuccess ?
                        Language("[Config] Directory opened", "[配置] 已打开配置目录") :
                        Language("[Config] Cannot open directory", "[配置] 无法打开配置目录"),
                    g_configStatusSuccess ? features::NotificationType::Success :
                                            features::NotificationType::Error);
            }

            ImGui::Spacing();
            ImGui::TextUnformatted(Language("Configuration path", "配置路径"));
            const std::string configPath = core::config::PathUtf8(configName);
            ImGui::TextWrapped("%s", configPath.c_str());
            if (!g_configStatus.empty())
            {
                const ImVec4 color = g_configStatusSuccess ?
                    ImVec4(0.35f, 0.90f, 0.45f, 1.0f) : ImVec4(1.0f, 0.38f, 0.32f, 1.0f);
                ImGui::TextColored(color, "%s", g_configStatus.c_str());
            }
            ImGui::TextDisabled(Language("The configuration is loaded automatically after injection.",
                                         "注入后会自动加载配置。"));
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Status"))
        {
            ImGui::SeparatorText(Language("Runtime", "运行状态"));
            if (ImGui::BeginTable("##runtime-status", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::TextUnformatted(Language("Renderer", "渲染器"));
                ImGui::TableNextColumn(); ImGui::TextUnformatted(renderer.BackendName());
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::TextUnformatted(Language("World", "世界"));
                ImGui::TableNextColumn(); ImGui::TextUnformatted(stats.worldReady ? Language("Ready", "就绪") : Language("Waiting", "等待中"));
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::TextUnformatted(Language("Frame rate", "帧率"));
                ImGui::TableNextColumn(); ImGui::Text("%.1f FPS", ImGui::GetIO().Framerate);
                ImGui::EndTable();
            }

            ImGui::SeparatorText(Language("ESP statistics", "ESP 统计"));
            ImGui::Text(Language("Drawn entities: %zu", "已绘制实体：%zu"), stats.drawnEntities);
            ImGui::Text(Language("Cached entities: %zu", "已缓存实体：%zu"), stats.cachedEntities);
            ImGui::Text(Language("Scanned actors: %zu", "已扫描 Actor：%zu"), stats.scannedActors);

            ImGui::SeparatorText(Language("Host runtime", "房主运行状态"));
            ImGui::Text(Language("Game thread: %s | Authority: %s", "游戏线程：%s | 权限：%s"),
                        exitStatus.gameThreadHookReady ? Language("Ready", "就绪") : Language("Unavailable", "不可用"),
                        exitStatus.hostAuthority ? Language("Host", "房主") : Language("Client", "客户端"));
            ImGui::Text(Language("Last teleported: %zu | Last attributes update: %zu", "上次传送：%zu | 上次属性更新：%zu"),
                        exitStatus.lastTeleportedPlayers, exitStatus.lastModifiedMembers);
            ImGui::Text(Language("Clown manager: %s | Roller manager: %s | Time: %d", "小丑管理器：%s | 过山车管理器：%s | 时间：%d"),
                        exitStatus.clownManagerDetected ? Language("Ready", "就绪") : Language("Waiting", "等待"),
                        exitStatus.rollercoasterManagerDetected ? Language("Ready", "就绪") : Language("Waiting", "等待"),
                        exitStatus.rollercoasterTime);

            ImGui::SeparatorText(Language("Session runtime", "房间运行状态"));
            ImGui::Text(Language("Resolver: %s", "解析状态：%s"),
                        sessionStatus.resolved ? Language("Ready", "就绪") :
                        (sessionStatus.resolving ? Language("Resolving", "解析中") : Language("Waiting", "等待中")));
            ImGui::Text(Language("Create screen: %s", "创建房间界面：%s"),
                        sessionStatus.createWidgetDetected ? Language("Detected", "已检测") : Language("Waiting", "等待中"));
            ImGui::Text(Language("Game UI sync: %s / %s", "游戏 UI 同步：%s / %s"),
                        sessionStatus.uiFunctionsReady ? Language("Ready", "就绪") : Language("Waiting", "等待中"),
                        sessionStatus.uiRefreshApplied ? Language("Applied", "已应用") : Language("Waiting", "等待中"));
            ImGui::Text(Language("Current room: %s | Previous slots: %d", "当前房间：%s | 原房间位：%d"),
                        sessionStatus.currentSessionDetected ? Language("Detected", "已检测") : Language("Waiting", "等待中"),
                        sessionStatus.currentSessionConnections);
            ImGui::Text(Language("Current room update: %s", "当前房间更新：%s"),
                        sessionStatus.currentSessionUpdateRequested ? Language("Requested", "已提交") : Language("Waiting", "等待中"));
            ImGui::Text(Language("Advertised slots: %d | Patched calls: %zu", "对外房间位：%d | 已修改调用：%zu"),
                        sessionStatus.advertisedConnections, sessionStatus.patchedSessionCalls);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::End();
    return open;
}

void DrawSpawnerWindow()
{
    if (!g_spawnWindowOpen)
        return;

    auto& spawner = features::Spawner::Instance();
    auto& settings = spawner.Settings();
    const auto& entries = spawner.Entries();

    if (ImGui::Begin(Language("Spawner", "召唤"), &g_spawnWindowOpen))
    {
        FullWidthSliderFloat(Language("Spawn distance", "生成距离"), "##spawner-window-distance",
                             &settings.spawnDistance, 0.0f, 2000.0f, "%.0f");
        FullWidthSliderInt(Language("Spawn count", "生成数量"), "##spawner-window-count",
                           &settings.spawnCount, 1, 20, "%d");
        ImGui::Text(Language("Classes: %zu  (double-click to spawn)", "类数量：%zu（双击即生成）"),
                    entries.size());
        ImGui::Separator();
        bool doubleClicked = false;
        if (ImGui::BeginChild("##spawner-entries", ImVec2(0.0f, 320.0f), true))
        {
            const features::SpawnCategory cats[4] = {
                features::SpawnCategory::Building, features::SpawnCategory::Item,
                features::SpawnCategory::Entity, features::SpawnCategory::Other};
            const char* catLabels[4] = {"建筑", "物品", "实体", "其他"};
            for (int ci = 0; ci < 4; ++ci)
            {
                int count = 0;
                for (const auto& e : entries)
                    if (e.category == cats[ci])
                        ++count;
                if (count == 0)
                    continue;
                char header[64];
                std::snprintf(header, sizeof(header), "%s (%d)", catLabels[ci], count);
                if (!ImGui::CollapsingHeader(header, ImGuiTreeNodeFlags_DefaultOpen))
                    continue;
                for (std::size_t index = 0; index < entries.size(); ++index)
                {
                    if (entries[index].category != cats[ci])
                        continue;
                    const std::string& name = entries[index].name;
                    const features::SpawnFav* meta = spawner.MetaFor(name);
                    const bool fav = meta != nullptr && meta->favorite;
                    ImGui::PushID(static_cast<int>(index));
                    bool wantToggle = false;
                    if (ImGui::SmallButton(fav ? "\u2605" : "\u2606"))
                        wantToggle = true;
                    ImGui::SameLine();
                    const bool selected = g_selectedSpawnEntry == static_cast<int>(index);
                    std::string label = name;
                    if (fav)
                        label = "\u2605 " + label;
                    if (ImGui::Selectable(label.c_str(), selected))
                        g_selectedSpawnEntry = static_cast<int>(index);
                    if (wantToggle)
                    {
                        spawner.ToggleFavorite(name);
                        spawner.SaveFavorites();
                    }
                    else if (ImGui::IsItemHovered() &&
                             ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    {
                        g_selectedSpawnEntry = static_cast<int>(index);
                        doubleClicked = true;
                    }
                    ImGui::PopID();
                }
            }
            ImGui::EndChild();
        }
        if (doubleClicked && g_selectedSpawnEntry >= 0 &&
            g_selectedSpawnEntry < static_cast<int>(entries.size()))
        {
            spawner.RequestSpawn(static_cast<std::size_t>(g_selectedSpawnEntry));
            g_spawnerStatus = Language("Spawn requested", "已请求生成");
        }
        if (g_selectedSpawnEntry >= 0 &&
            g_selectedSpawnEntry < static_cast<int>(entries.size()))
        {
            const std::string& selName = entries[static_cast<std::size_t>(g_selectedSpawnEntry)].name;
            ImGui::Separator();
            ImGui::TextUnformatted(selName.c_str());
            static char g_noteBuf[256] = {};
            static std::string g_noteFor;
            if (g_noteFor != selName)
            {
                g_noteFor = selName;
                const features::SpawnFav* m = spawner.MetaFor(selName);
                const std::string note = m != nullptr ? m->note : "";
                std::snprintf(g_noteBuf, sizeof(g_noteBuf), "%s", note.c_str());
            }
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputText("##spawner-note", g_noteBuf, sizeof(g_noteBuf));
            if (ImGui::Button(Language("Save note", "保存备注"), ImVec2(-1.0f, 0.0f)))
            {
                spawner.SetNote(selName, g_noteBuf);
                spawner.SaveFavorites();
                g_spawnerStatus = Language("Note saved", "备注已保存");
            }
            ImGui::TextUnformatted(Language("Double-click to spawn in front of you.",
                                            "双击即生成在你面前。"));
            if (ImGui::Button(Language("Spawn", "生成"), ImVec2(-1.0f, 0.0f)))
            {
                spawner.RequestSpawn(static_cast<std::size_t>(g_selectedSpawnEntry));
                g_spawnerStatus = Language("Spawn requested", "已请求生成");
            }
            if (!g_spawnerStatus.empty())
                ImGui::TextWrapped("%s", g_spawnerStatus.c_str());
        }
        if (ImGui::Button(Language("Clear list", "清空列表"), ImVec2(-1.0f, 0.0f)))
        {
            spawner.ClearList();
            g_selectedSpawnEntry = -1;
        }
    }
    ImGui::End();
}

void DrawModelBrowserWindow()
{
    if (!g_modelWindowOpen)
        return;

    auto& browser = features::ModelBrowser::Instance();
    const auto& entries = browser.Entries();

    if (ImGui::Begin(Language("Model browser", "模型浏览"), &g_modelWindowOpen))
    {
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputText("##model-window-filter", g_modelFilter, std::size(g_modelFilter));
        if (ImGui::Button(Language("Scan loaded meshes", "扫描已加载模型"), ImVec2(-1.0f, 0.0f)))
        {
            std::string message;
            browser.FetchModels(g_modelFilter, 4000, message);
            g_modelStatus = message;
            g_selectedModelEntry = -1;
        }
        ImGui::Text(Language("Meshes: %zu  (click to select)", "模型数量：%zu（点击选中）"),
                    entries.size());
        ImGui::Separator();
        if (ImGui::BeginChild("##model-entries", ImVec2(0.0f, 340.0f), true))
        {
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(entries.size()));
            while (clipper.Step())
            {
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
                {
                    const features::ModelEntry& entry = entries[static_cast<std::size_t>(i)];
                    ImGui::PushID(i);
                    std::string label = entry.skeletal ? "[S] " : "[T] ";
                    label += entry.name;
                    const bool selected = g_selectedModelEntry == i;
                    if (ImGui::Selectable(label.c_str(), selected))
                        g_selectedModelEntry = i;
                    if (ImGui::IsItemHovered() && !entry.fullName.empty())
                        ImGui::SetTooltip("%s", entry.fullName.c_str());
                    ImGui::PopID();
                }
            }
            ImGui::EndChild();
        }
        if (g_selectedModelEntry >= 0 &&
            g_selectedModelEntry < static_cast<int>(entries.size()))
        {
            const features::ModelEntry& entry = entries[static_cast<std::size_t>(g_selectedModelEntry)];
            ImGui::Separator();
            ImGui::TextUnformatted(entry.skeletal
                ? Language("Skeletal mesh", "骨骼网格")
                : Language("Static mesh", "静态网格"));
            ImGui::TextWrapped("%s", entry.fullName.empty() ? entry.name.c_str()
                                                            : entry.fullName.c_str());
        }
        if (!g_modelStatus.empty())
            ImGui::TextDisabled("%s", g_modelStatus.c_str());
        if (ImGui::Button(Language("Clear list", "清空列表"), ImVec2(-1.0f, 0.0f)))
        {
            browser.ClearList();
            g_selectedModelEntry = -1;
        }
    }
    ImGui::End();
}
}


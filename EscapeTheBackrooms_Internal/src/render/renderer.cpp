#include "render/renderer.hpp"

#include "core/config.hpp"
#include "core/logger.hpp"
#include "features/array_list.hpp"
#include "features/esp.hpp"
#include "features/exit_activator.hpp"
#include "features/movement.hpp"
#include "features/notifications.hpp"
#include "features/pickup.hpp"
#include "features/spectator.hpp"
#include "features/spawner.hpp"
#include "features/visuals.hpp"
#include "features/vehicle_flight.hpp"
#include "fonts/MiSans_Heavy.h"
#include "hooks/dxgi_hooks.hpp"
#include "render/d3d11_backend.hpp"
#include "render/d3d12_backend.hpp"
#include "ui/menu.hpp"

#include <backends/imgui_impl_win32.h>
#include <d3d11.h>
#include <d3d12.h>
#include <imgui.h>
#include <wrl/client.h>

#include <atomic>

using Microsoft::WRL::ComPtr;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace
{
etb::render::Renderer* g_renderer = nullptr;
std::atomic_bool g_menuInputActive{false};
std::atomic_bool g_mouseCircleActive{false};

bool g_cursorManaged = false;
std::atomic_bool g_visualFaultLogged{false};
std::atomic_bool g_espFaultLogged{false};
std::atomic_bool g_menuOpenedNotificationPending{false};
int g_cursorDisplayAdjustments = 0;

struct MouseCursorRenderState
{
    bool initialized = false;
    ImVec2 rawMousePos{-1.0f, -1.0f};
    ImVec2 smoothedPos{-1.0f, -1.0f};
    double timeSeconds = 0.0;
    LARGE_INTEGER perfFrequency{};
    LARGE_INTEGER lastFrameTick{};
};
MouseCursorRenderState g_cursorState{};

float GetSaneDeltaSeconds(MouseCursorRenderState& s)
{
    LARGE_INTEGER now{};
    if (!QueryPerformanceCounter(&now))
        return 1.0f / 60.0f;
    if (s.perfFrequency.QuadPart == 0)
    {
        QueryPerformanceFrequency(&s.perfFrequency);
        s.lastFrameTick = now;
        return 1.0f / 60.0f;
    }
    const float dt = static_cast<float>(
        static_cast<double>(now.QuadPart - s.lastFrameTick.QuadPart) /
        static_cast<double>(s.perfFrequency.QuadPart));
    s.lastFrameTick = now;
    s.timeSeconds += static_cast<double>(dt);
    if (dt <= 0.0f || dt > 0.1f)
        return 1.0f / 60.0f;
    return dt;
}

ImU32 ClampColor(int r, int g, int b, int a)
{
    auto clamp8 = [](int v) -> ImU8
    { return static_cast<ImU8>(v < 0 ? 0 : (v > 255 ? 255 : v)); };
    return IM_COL32(clamp8(r), clamp8(g), clamp8(b), clamp8(a));
}

inline ImVec2 LerpV2(const ImVec2& a, const ImVec2& b, float t)
{
    return ImVec2{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
}

void UpdateSmoothedCursor(MouseCursorRenderState& s, const ImVec2& target)
{
    const float dt = GetSaneDeltaSeconds(s);
    if (!s.initialized || s.rawMousePos.x < 0.0f || s.smoothedPos.x < 0.0f)
    {
        s.rawMousePos = target;
        s.smoothedPos = target;
        s.initialized = true;
        return;
    }
    s.rawMousePos = target;
    const bool follow = etb::core::config::MenuSettings::mouseFollowEnabled;
    if (!follow)
    {
        s.smoothedPos = target;
        return;
    }
    const float dx = target.x - s.smoothedPos.x;
    const float dy = target.y - s.smoothedPos.y;
    const float dist2 = dx * dx + dy * dy;
    const float distance = std::sqrt(dist2);
    float speed = std::max(1.0f, etb::core::config::MenuSettings::mouseFollowSpeed);
    float t = 1.0f - std::exp(-speed * dt);
    if (distance > 300.0f)
        t = std::min(1.0f, t * 2.0f);
    s.smoothedPos = LerpV2(s.smoothedPos, target, t);
}

void DrawCursorStyle(ImDrawList* drawList, const ImVec2& raw, const ImVec2& smoothed,
                     double timeSeconds)
{
    (void)raw;
    (void)timeSeconds;
    using namespace etb::core::config;
    const float radiusBase = std::max(2.0f, MenuSettings::mouseRadius);
    const float thickness = std::max(0.5f, MenuSettings::mouseThickness);
    const ImU32 mainColor = ClampColor(MenuSettings::mouseColorR, MenuSettings::mouseColorG,
                                        MenuSettings::mouseColorB, MenuSettings::mouseColorA);
    const ImU32 accentColor = ClampColor(MenuSettings::mouseAccentR, MenuSettings::mouseAccentG,
                                          MenuSettings::mouseAccentB, 240);

    const float radius = radiusBase;
    const float alphaBoost = 1.0f;

    auto applyAlpha = [alphaBoost](ImU32 color, float extra = 1.0f) -> ImU32
    {
        const ImU8 aRaw = static_cast<ImU8>((color >> IM_COL32_A_SHIFT) & 0xFF);
        const float aF = static_cast<float>(aRaw) * alphaBoost * extra / 255.0f;
        const ImU8 a = static_cast<ImU8>(std::clamp(aF, 0.0f, 1.0f) * 255.0f);
        return (color & ~IM_COL32_A_MASK) | (static_cast<ImU32>(a) << IM_COL32_A_SHIFT);
    };


    const int style = 0;
    const bool glow = MenuSettings::mouseGlowEnabled;
    const float glowRadius = std::max(0.5f, MenuSettings::mouseGlowRadius);

    if (glow)
    {
        const ImU32 glowColor = applyAlpha(mainColor, 0.12f);
        for (int layer = 4; layer >= 1; --layer)
        {
            const float r = radius + glowRadius * static_cast<float>(layer);
            const ImU32 lc = applyAlpha(mainColor, 0.08f + 0.06f * (1.0f - static_cast<float>(layer - 1) / 4.0f));
            drawList->AddCircle(smoothed, r, lc, 48, thickness + 0.5f * layer);
        }
        (void)glowColor;
    }

    switch (style)
    {
    case 0:
    {
        drawList->AddCircle(smoothed, radius, accentColor, 32, thickness + 2.0f);
        drawList->AddCircle(smoothed, radius, applyAlpha(mainColor), 32, thickness);
        break;
    }
    case 1:
    {
        drawList->AddCircle(smoothed, radius * 0.75f, accentColor, 32, thickness + 1.5f);
        drawList->AddCircle(smoothed, radius * 0.75f, applyAlpha(mainColor), 32, thickness);
        drawList->AddCircle(smoothed, radius, accentColor, 32, thickness + 1.2f);
        drawList->AddCircle(smoothed, radius, applyAlpha(mainColor, 0.8f), 32, thickness);
        const float cross = radius + radius * 0.5f;
        drawList->AddLine(ImVec2(smoothed.x - cross, smoothed.y), ImVec2(smoothed.x - radius * 1.2f, smoothed.y),
                          accentColor, thickness + 1.5f);
        drawList->AddLine(ImVec2(smoothed.x + radius * 1.2f, smoothed.y), ImVec2(smoothed.x + cross, smoothed.y),
                          accentColor, thickness + 1.5f);
        drawList->AddLine(ImVec2(smoothed.x, smoothed.y - cross), ImVec2(smoothed.x, smoothed.y - radius * 1.2f),
                          accentColor, thickness + 1.5f);
        drawList->AddLine(ImVec2(smoothed.x, smoothed.y + radius * 1.2f), ImVec2(smoothed.x, smoothed.y + cross),
                          accentColor, thickness + 1.5f);
        drawList->AddLine(ImVec2(smoothed.x - cross, smoothed.y), ImVec2(smoothed.x - radius * 1.2f, smoothed.y),
                          applyAlpha(mainColor), thickness);
        drawList->AddLine(ImVec2(smoothed.x + radius * 1.2f, smoothed.y), ImVec2(smoothed.x + cross, smoothed.y),
                          applyAlpha(mainColor), thickness);
        drawList->AddLine(ImVec2(smoothed.x, smoothed.y - cross), ImVec2(smoothed.x, smoothed.y - radius * 1.2f),
                          applyAlpha(mainColor), thickness);
        drawList->AddLine(ImVec2(smoothed.x, smoothed.y + radius * 1.2f), ImVec2(smoothed.x, smoothed.y + cross),
                          applyAlpha(mainColor), thickness);
        break;
    }
    case 2:
    {
        drawList->AddCircle(smoothed, radius, accentColor, 48, thickness + 2.5f);
        drawList->AddCircle(smoothed, radius, applyAlpha(mainColor), 48, thickness);
        drawList->AddLine(ImVec2(smoothed.x, smoothed.y - radius), ImVec2(smoothed.x, smoothed.y + radius),
                          accentColor, thickness + 1.0f);
        drawList->AddLine(ImVec2(smoothed.x - radius, smoothed.y), ImVec2(smoothed.x + radius, smoothed.y),
                          accentColor, thickness + 1.0f);
        drawList->AddLine(ImVec2(smoothed.x, smoothed.y - radius), ImVec2(smoothed.x, smoothed.y + radius),
                          applyAlpha(mainColor, 0.9f), thickness);
        drawList->AddLine(ImVec2(smoothed.x - radius, smoothed.y), ImVec2(smoothed.x + radius, smoothed.y),
                          applyAlpha(mainColor, 0.9f), thickness);
        break;
    }
    case 3:
    {
        drawList->AddCircleFilled(smoothed, 1.5f, accentColor);
        drawList->AddCircleFilled(smoothed, 1.5f, applyAlpha(mainColor));
        const float ring2 = radius * 0.55f;
        const float ring3 = radius * 1.15f;
        drawList->AddCircle(smoothed, ring2, accentColor, 32, thickness + 0.8f);
        drawList->AddCircle(smoothed, radius, accentColor, 32, thickness + 1.0f);
        drawList->AddCircle(smoothed, ring3, accentColor, 40, thickness + 1.2f);
        drawList->AddCircle(smoothed, ring2, applyAlpha(mainColor), 32, thickness);
        drawList->AddCircle(smoothed, radius, applyAlpha(mainColor, 0.85f), 32, thickness);
        drawList->AddCircle(smoothed, ring3, applyAlpha(mainColor, 0.65f), 40, thickness);
        break;
    }
    case 4:
    {
        const float hx = radius * 1.05f;
        const ImVec2 tl{smoothed.x - hx, smoothed.y - hx};
        const ImVec2 br{smoothed.x + hx, smoothed.y + hx};
        drawList->AddRect(tl, br, accentColor, 0.0f, 0, thickness + 2.0f);
        drawList->AddRect(tl, br, applyAlpha(mainColor), 0.0f, 0, thickness);
        drawList->AddLine(ImVec2(smoothed.x - hx * 0.6f, smoothed.y), ImVec2(smoothed.x + hx * 0.6f, smoothed.y),
                          accentColor, thickness + 0.8f);
        drawList->AddLine(ImVec2(smoothed.x, smoothed.y - hx * 0.6f), ImVec2(smoothed.x, smoothed.y + hx * 0.6f),
                          accentColor, thickness + 0.8f);
        drawList->AddLine(ImVec2(smoothed.x - hx * 0.6f, smoothed.y), ImVec2(smoothed.x + hx * 0.6f, smoothed.y),
                          applyAlpha(mainColor, 0.9f), thickness);
        drawList->AddLine(ImVec2(smoothed.x, smoothed.y - hx * 0.6f), ImVec2(smoothed.x, smoothed.y + hx * 0.6f),
                          applyAlpha(mainColor, 0.9f), thickness);
        break;
    }
    case 5:
    default:
    {
        ImVec2 arrowTip = raw;
        if (MenuSettings::mouseFollowEnabled)
            arrowTip = smoothed;
        const ImVec2 p0 = arrowTip;
        const ImVec2 p1{arrowTip.x + radius * 1.6f, arrowTip.y + radius * 1.8f};
        const ImVec2 p2{arrowTip.x + radius * 0.35f, arrowTip.y + radius * 1.15f};
        const ImVec2 p3{arrowTip.x + radius * 1.15f, arrowTip.y + radius * 1.95f};
        drawList->AddTriangleFilled(p0, p1, p2, accentColor);
        drawList->AddTriangleFilled(p1, p3, p2, accentColor);
        drawList->AddTriangleFilled(p0, p1, p2, applyAlpha(mainColor, 0.92f));
        drawList->AddTriangleFilled(p1, p3, p2, applyAlpha(mainColor, 0.7f));
        drawList->AddTriangle(p0, p1, p2, applyAlpha(mainColor), thickness);
        break;
    }
    }
}

__declspec(noinline) bool SafeUpdateVisuals(const bool inputBlocked)
{
    __try
    {
        etb::features::Visuals::Instance().Update(inputBlocked);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

__declspec(noinline) bool SafeUpdateEsp()
{
    __try
    {
        etb::features::Esp::Instance().UpdateAndDraw();
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool IsGameInputMessage(const UINT message)
{
    const bool mouseMessage = message >= WM_MOUSEFIRST && message <= WM_MOUSELAST;
    const bool keyboardMessage = message >= WM_KEYFIRST && message <= WM_KEYLAST;
    return mouseMessage || keyboardMessage || message == WM_INPUT ||
           message == WM_APPCOMMAND || message == WM_HOTKEY || message == WM_SETCURSOR;
}

void EnsureSystemCursorVisible()
{
    CURSORINFO cursorInfo{};
    cursorInfo.cbSize = sizeof(cursorInfo);
    if (!GetCursorInfo(&cursorInfo) || (cursorInfo.flags & CURSOR_SHOWING) == 0)
    {

        do
        {
            ++g_cursorDisplayAdjustments;
        }
        while (g_cursorDisplayAdjustments < 8 && ShowCursor(TRUE) < 0);

        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
    }
}

void RestoreSystemCursorState()
{
    int guard = 0;
    while (g_cursorDisplayAdjustments > 0 && guard < 16)
    {
        ShowCursor(FALSE);
        --g_cursorDisplayAdjustments;
        ++guard;
    }
    g_cursorDisplayAdjustments = 0;
}

void RepairSystemCursorIfBroken()
{
    __try
    {
        CURSORINFO cursorInfo{};
        cursorInfo.cbSize = sizeof(cursorInfo);
        const bool infoOk = GetCursorInfo(&cursorInfo) != FALSE;
        const bool visible = infoOk && (cursorInfo.flags & CURSOR_SHOWING) != 0;
        const bool handleIsNull = !infoOk || cursorInfo.hCursor == nullptr;
        if (handleIsNull || !visible)
        {
            if (handleIsNull)
                SetCursor(LoadCursorW(nullptr, IDC_ARROW));
            int showCursorResult = ShowCursor(TRUE);
            ++g_cursorDisplayAdjustments;
            while (showCursorResult < 0)
            {
                ++g_cursorDisplayAdjustments;
                showCursorResult = ShowCursor(TRUE);
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
}

void ApplyMenuCursorState(HWND window, const bool menuVisible)
{
    using namespace etb::core::config;
    const bool drawCursorFeatureEnabled = MenuSettings::mouseCircle;
    const bool drawActive = drawCursorFeatureEnabled && menuVisible;
    const bool hideSystem = drawActive;
    g_mouseCircleActive.store(drawActive, std::memory_order_release);
    if (ImGui::GetCurrentContext() != nullptr)
        ImGui::GetIO().MouseDrawCursor = false;

    if (!menuVisible)
    {

        if (g_cursorManaged)
        {
            g_cursorManaged = false;
            RestoreSystemCursorState();
        }
        return;
    }
    g_cursorManaged = true;

    if (menuVisible)
    {
        ClipCursor(nullptr);
        if (GetCapture() == window)
            ReleaseCapture();
    }

    if (hideSystem)
    {
        SetCursor(nullptr);
    }
    else
    {
        EnsureSystemCursorVisible();
    }
}

void DrawMousePositionCircle()
{
    if (!g_mouseCircleActive.load(std::memory_order_acquire))
    {
        g_cursorState.initialized = false;
        return;
    }

    const ImVec2 rawPos = ImGui::GetIO().MousePos;
    if (!ImGui::IsMousePosValid(&rawPos))
    {
        g_cursorState.initialized = false;
        return;
    }

    UpdateSmoothedCursor(g_cursorState, rawPos);
    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    DrawCursorStyle(drawList, rawPos, g_cursorState.smoothedPos, g_cursorState.timeSeconds);
}
}

namespace etb::render
{
bool IsMenuInputActive() noexcept
{
    return g_menuInputActive.load(std::memory_order_acquire);
}

bool IsMouseCircleActive() noexcept
{
    return g_mouseCircleActive.load(std::memory_order_acquire);
}

Renderer& Renderer::Instance()
{
    static Renderer instance;
    return instance;
}

bool Renderer::TryInitialize(IDXGISwapChain* swapChain)
{
    DXGI_SWAP_CHAIN_DESC desc{};
    if (swapChain == nullptr || FAILED(swapChain->GetDesc(&desc)) || desc.OutputWindow == nullptr)
        return false;

    std::unique_ptr<IRenderBackend> candidate;

    ComPtr<ID3D11Device> d3d11Device;
    if (SUCCEEDED(swapChain->GetDevice(IID_PPV_ARGS(&d3d11Device))))
    {
        candidate = std::make_unique<D3D11Backend>();
    }
    else
    {
        ComPtr<ID3D12Device> d3d12Device;
        if (FAILED(swapChain->GetDevice(IID_PPV_ARGS(&d3d12Device))))
            return false;

        ComPtr<ID3D12CommandQueue> queue = hooks::GetDirectCommandQueue();
        if (queue == nullptr)
            return false;

        candidate = std::make_unique<D3D12Backend>(std::move(queue));
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    imguiContextCreated_ = true;

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

    ImFontConfig fontConfig{};
    fontConfig.FontDataOwnedByAtlas = false;
    fontConfig.OversampleH = 2;
    fontConfig.OversampleV = 1;
    fontConfig.PixelSnapH = true;
    if (io.Fonts->AddFontFromMemoryTTF(
            const_cast<unsigned char*>(MiSans_Heavy),
            static_cast<int>(sizeof(MiSans_Heavy)),
            16.0f,
            &fontConfig,
            io.Fonts->GetGlyphRangesChineseSimplifiedCommon()) == nullptr)
    {
        io.Fonts->AddFontDefault();
        core::Log("failed to load embedded MiSans Heavy font; using ImGui default");
    }

    ImGui::StyleColorsDark();
    const UINT windowDpi = GetDpiForWindow(desc.OutputWindow);
    const float dpiScale = windowDpi == 0 ? 1.0f : static_cast<float>(windowDpi) / 96.0f;
    if (!features::Notifications::Instance().InitializeRenderResources(dpiScale))
        core::Log("failed to initialize Nolstice shadow atlas; notification glow is unavailable");

    const bool win32Initialized = ImGui_ImplWin32_Init(desc.OutputWindow);
    if (!win32Initialized || !candidate->Initialize(swapChain))
    {
        features::Notifications::Instance().ShutdownRenderResources();
        candidate->Shutdown();
        if (win32Initialized)
            ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        imguiContextCreated_ = false;
        return false;
    }

    swapChain_ = swapChain;
    backend_ = std::move(candidate);
    window_ = desc.OutputWindow;
    g_menuInputActive.store(menuVisible_, std::memory_order_release);
    InstallWindowProcedure();

    core::Logf("renderer attached to HWND=%p using %s", window_, BackendName());
    return true;
}

void Renderer::OnPresent(IDXGISwapChain* swapChain)
{
    std::lock_guard lock(mutex_);

    if (backend_ == nullptr)
    {
        TryInitialize(swapChain);
        if (backend_ == nullptr)
            return;
    }

    if (swapChain_.Get() != swapChain)
        return;

    ApplyMenuCursorState(window_, menuVisible_);

    backend_->NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    const bool gameplayInputBlocked = menuVisible_ || !IsGameForeground();
    const bool movementInputBlocked = gameplayInputBlocked ||
        features::Spectator::Instance().Status().isSpectating;
    features::Movement::Instance().Update(movementInputBlocked);
    features::VehicleFlight::Instance().UpdateInput(gameplayInputBlocked);
    features::Pickup::Instance().UpdateInput(gameplayInputBlocked);
    features::ExitActivator::Instance().Update();
    features::Spectator::Instance().Update(gameplayInputBlocked);
    if (!SafeUpdateVisuals(gameplayInputBlocked))
    {
        if (!g_visualFaultLogged.exchange(true, std::memory_order_relaxed))
            core::Log("visual update skipped after stale-object exception");
    }
    else
    {
        g_visualFaultLogged.store(false, std::memory_order_relaxed);
    }
    if (!SafeUpdateEsp())
    {
        if (!g_espFaultLogged.exchange(true, std::memory_order_relaxed))
            core::Log("ESP frame skipped after stale-object exception");
        features::Esp::Instance().Reset();
    }
    else
    {
        g_espFaultLogged.store(false, std::memory_order_relaxed);
    }
    features::ArrayListHud::Instance().Draw(menuVisible_);
    if (g_menuOpenedNotificationPending.exchange(false, std::memory_order_acq_rel))
    {
        features::Notifications::Instance().Push(
            "[Menu] Menu opened",
            features::NotificationType::Info);
    }
    features::Notifications::Instance().Draw();

    if (menuVisible_)
    {
        const bool wasOpen = menuVisible_;
        menuVisible_ = ui::DrawMenu(*this);
        if (wasOpen && !menuVisible_)
        {
            g_menuInputActive.store(false, std::memory_order_release);
            ApplyMenuCursorState(window_, false);
        }
    }

    DrawMousePositionCircle();

    if (menuVisible_)
    {
        ui::DrawSpawnerWindow();
        ui::DrawModelBrowserWindow();
    }

    ImGui::Render();
    backend_->Render(ImGui::GetDrawData());
}

void Renderer::BeforeResize(IDXGISwapChain* swapChain)
{
    std::lock_guard lock(mutex_);
    if (backend_ != nullptr && swapChain_.Get() == swapChain)
        backend_->BeforeResize();
}

void Renderer::AfterResize(IDXGISwapChain* swapChain)
{
    std::lock_guard lock(mutex_);
    if (backend_ != nullptr && swapChain_.Get() == swapChain && !backend_->AfterResize())
    {
        core::Log("renderer failed to recreate resources after ResizeBuffers");
        Shutdown();
    }
}

void Renderer::Shutdown()
{
    std::lock_guard lock(mutex_);

    RestoreWindowProcedure();

    features::Notifications::Instance().ShutdownRenderResources();

    if (backend_ != nullptr)
    {
        backend_->Shutdown();
        backend_.reset();
    }

    features::ArrayListHud::Instance().Reset();
    features::Notifications::Instance().Reset();
    features::Movement::Instance().Reset();
    features::VehicleFlight::Instance().Reset();
    features::Pickup::Instance().Reset();
    features::ExitActivator::Instance().Reset();
    features::Spectator::Instance().Reset();
    features::Visuals::Instance().Reset();
    features::Esp::Instance().Reset();
    features::Spawner::Instance().ClearList();
    g_menuInputActive.store(false, std::memory_order_release);
    g_mouseCircleActive.store(false, std::memory_order_release);
    RestoreSystemCursorState();

    if (imguiContextCreated_)
    {
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        imguiContextCreated_ = false;
    }

    swapChain_.Reset();
    window_ = nullptr;
}

void Renderer::InstallWindowProcedure()
{
    if (window_ == nullptr || originalWindowProcedure_ != nullptr)
        return;

    g_renderer = this;
    SetLastError(ERROR_SUCCESS);
    const LONG_PTR previous = SetWindowLongPtrW(window_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(WindowProcedure));
    if (previous == 0 && GetLastError() != ERROR_SUCCESS)
    {
        core::Logf("SetWindowLongPtrW failed: %lu", GetLastError());
        g_renderer = nullptr;
        return;
    }

    originalWindowProcedure_ = reinterpret_cast<WNDPROC>(previous);
}

void Renderer::RestoreWindowProcedure()
{
    if (window_ != nullptr && originalWindowProcedure_ != nullptr && IsWindow(window_))
        SetWindowLongPtrW(window_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(originalWindowProcedure_));

    originalWindowProcedure_ = nullptr;
    g_renderer = nullptr;
}

LRESULT CALLBACK Renderer::WindowProcedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    Renderer* renderer = g_renderer;
    if (renderer == nullptr || renderer->originalWindowProcedure_ == nullptr)
        return DefWindowProcW(window, message, wparam, lparam);

    std::lock_guard lock(renderer->mutex_);

    if (message == WM_KEYUP && wparam == VK_INSERT)
    {
        renderer->menuVisible_ = !renderer->menuVisible_;
        if (renderer->menuVisible_)
            g_menuOpenedNotificationPending.store(true, std::memory_order_release);
        g_menuInputActive.store(renderer->menuVisible_, std::memory_order_release);
        ApplyMenuCursorState(window, renderer->menuVisible_);
        return 0;
    }

    if (renderer->menuVisible_ && ImGui::GetCurrentContext() != nullptr)
    {
        ImGui_ImplWin32_WndProcHandler(window, message, wparam, lparam);

        if (message == WM_INPUT)
        {
            DefWindowProcW(window, message, wparam, lparam);
            return 0;
        }

        if (IsGameInputMessage(message))
            return 1;
    }

    return CallWindowProcW(renderer->originalWindowProcedure_, window, message, wparam, lparam);
}

bool Renderer::MenuVisible() const
{
    std::lock_guard lock(mutex_);
    return menuVisible_;
}

bool Renderer::IsGameForeground() const
{
    std::lock_guard lock(mutex_);
    if (window_ == nullptr)
        return false;

    const HWND foregroundWindow = GetForegroundWindow();
    return foregroundWindow == window_ ||
        (foregroundWindow != nullptr && GetAncestor(foregroundWindow, GA_ROOT) == window_);
}

void Renderer::ToggleMenu()
{
    std::lock_guard lock(mutex_);
    menuVisible_ = !menuVisible_;
    g_menuInputActive.store(menuVisible_, std::memory_order_release);
}

const char* Renderer::BackendName() const
{
    if (backend_ == nullptr)
        return "Pending";

    switch (backend_->Api())
    {
    case GraphicsApi::D3D11:
        return "Direct3D 11";
    case GraphicsApi::D3D12:
        return "Direct3D 12";
    default:
        return "Unknown";
    }
}
}

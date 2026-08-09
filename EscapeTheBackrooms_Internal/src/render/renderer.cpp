#include "render/renderer.hpp"

#include "core/config.hpp"
#include "core/logger.hpp"
#include "features/array_list.hpp"
#include "features/esp.hpp"
#include "features/exit_activator.hpp"
#include "features/movement.hpp"
#include "features/notifications.hpp"
#include "features/pickup.hpp"
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
std::atomic_bool g_visualFaultLogged{false};
std::atomic_bool g_espFaultLogged{false};
std::atomic_bool g_menuOpenedNotificationPending{false};
int g_cursorDisplayAdjustments = 0;

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
        while (ShowCursor(TRUE) < 0);

        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
    }
}

void RestoreSystemCursorState()
{
    while (g_cursorDisplayAdjustments > 0)
    {
        ShowCursor(FALSE);
        --g_cursorDisplayAdjustments;
    }
}
}

namespace etb::render
{
bool IsMenuInputActive() noexcept
{
    return g_menuInputActive.load(std::memory_order_acquire);
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

    ImGuiIO& io = ImGui::GetIO();
    io.MouseDrawCursor = false;
    if (menuVisible_)
    {
        ClipCursor(nullptr);
        if (GetCapture() == window_)
            ReleaseCapture();
        EnsureSystemCursorVisible();
    }

    backend_->NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    const bool gameplayInputBlocked = menuVisible_ || !IsGameForeground();
    features::Movement::Instance().Update(gameplayInputBlocked);
    features::VehicleFlight::Instance().UpdateInput(gameplayInputBlocked);
    features::Pickup::Instance().UpdateInput(gameplayInputBlocked);
    features::ExitActivator::Instance().Update();
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
            RestoreSystemCursorState();
        }
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
    features::Visuals::Instance().Reset();
    features::Esp::Instance().Reset();
    g_menuInputActive.store(false, std::memory_order_release);
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
        if (ImGui::GetCurrentContext() != nullptr)
            ImGui::GetIO().MouseDrawCursor = false;

        if (renderer->menuVisible_)
        {
            ClipCursor(nullptr);
            if (GetCapture() == window)
                ReleaseCapture();
            EnsureSystemCursorVisible();
        }
        else
        {
            RestoreSystemCursorState();
        }
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

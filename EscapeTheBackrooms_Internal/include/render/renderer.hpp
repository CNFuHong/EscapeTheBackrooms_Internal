#pragma once

#include <Windows.h>

#include "render/backend.hpp"

#include <dxgi.h>
#include <wrl/client.h>

#include <memory>
#include <mutex>

namespace etb::render
{
bool IsMenuInputActive() noexcept;

class Renderer
{
public:
    static Renderer& Instance();

    void OnPresent(IDXGISwapChain* swapChain);
    void BeforeResize(IDXGISwapChain* swapChain);
    void AfterResize(IDXGISwapChain* swapChain);
    void Shutdown();

    bool MenuVisible() const;
    bool IsGameForeground() const;
    void ToggleMenu();
    const char* BackendName() const;

private:
    Renderer() = default;
    ~Renderer() = default;
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    bool TryInitialize(IDXGISwapChain* swapChain);
    void InstallWindowProcedure();
    void RestoreWindowProcedure();
    static LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam);

    mutable std::recursive_mutex mutex_;
    Microsoft::WRL::ComPtr<IDXGISwapChain> swapChain_;
    std::unique_ptr<IRenderBackend> backend_;
    HWND window_ = nullptr;
    WNDPROC originalWindowProcedure_ = nullptr;
    bool imguiContextCreated_ = false;
    bool menuVisible_ = true;
};
}

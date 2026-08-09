#pragma once

#include <dxgi.h>
#include <imgui.h>

namespace etb::render
{
enum class GraphicsApi
{
    Unknown,
    D3D11,
    D3D12
};

class IRenderBackend
{
public:
    virtual ~IRenderBackend() = default;

    virtual GraphicsApi Api() const = 0;
    virtual bool Initialize(IDXGISwapChain* swapChain) = 0;
    virtual void NewFrame() = 0;
    virtual void Render(ImDrawData* drawData) = 0;
    virtual void BeforeResize() = 0;
    virtual bool AfterResize() = 0;
    virtual void Shutdown() = 0;
};
}


#pragma once

#include "render/backend.hpp"

#include <d3d11.h>
#include <wrl/client.h>

namespace etb::render
{
class D3D11Backend final : public IRenderBackend
{
public:
    GraphicsApi Api() const override;
    bool Initialize(IDXGISwapChain* swapChain) override;
    void NewFrame() override;
    void Render(ImDrawData* drawData) override;
    void BeforeResize() override;
    bool AfterResize() override;
    void Shutdown() override;

private:
    bool CreateRenderTarget();

    Microsoft::WRL::ComPtr<IDXGISwapChain> swapChain_;
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> renderTarget_;
    bool imguiInitialized_ = false;
};
}


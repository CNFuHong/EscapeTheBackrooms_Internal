#include "render/d3d11_backend.hpp"

#include "core/logger.hpp"

#include <backends/imgui_impl_dx11.h>

using Microsoft::WRL::ComPtr;

namespace etb::render
{
GraphicsApi D3D11Backend::Api() const
{
    return GraphicsApi::D3D11;
}

bool D3D11Backend::Initialize(IDXGISwapChain* swapChain)
{
    if (swapChain == nullptr || FAILED(swapChain->GetDevice(IID_PPV_ARGS(&device_))))
        return false;

    swapChain_ = swapChain;
    device_->GetImmediateContext(&context_);

    if (context_ == nullptr || !CreateRenderTarget())
    {
        Shutdown();
        return false;
    }

    if (!ImGui_ImplDX11_Init(device_.Get(), context_.Get()))
    {
        Shutdown();
        return false;
    }

    imguiInitialized_ = true;
    core::Log("D3D11 renderer initialized");
    return true;
}

bool D3D11Backend::CreateRenderTarget()
{
    ComPtr<ID3D11Texture2D> backBuffer;
    if (swapChain_ == nullptr || FAILED(swapChain_->GetBuffer(0, IID_PPV_ARGS(&backBuffer))))
        return false;

    return SUCCEEDED(device_->CreateRenderTargetView(backBuffer.Get(), nullptr, &renderTarget_));
}

void D3D11Backend::NewFrame()
{
    ImGui_ImplDX11_NewFrame();
}

void D3D11Backend::Render(ImDrawData* drawData)
{
    if (context_ == nullptr || renderTarget_ == nullptr)
        return;

    ComPtr<ID3D11RenderTargetView> previousTarget;
    ComPtr<ID3D11DepthStencilView> previousDepth;
    context_->OMGetRenderTargets(1, &previousTarget, &previousDepth);

    ID3D11RenderTargetView* target = renderTarget_.Get();
    context_->OMSetRenderTargets(1, &target, nullptr);
    ImGui_ImplDX11_RenderDrawData(drawData);

    ID3D11RenderTargetView* oldTarget = previousTarget.Get();
    context_->OMSetRenderTargets(1, &oldTarget, previousDepth.Get());
}

void D3D11Backend::BeforeResize()
{
    renderTarget_.Reset();
}

bool D3D11Backend::AfterResize()
{
    return CreateRenderTarget();
}

void D3D11Backend::Shutdown()
{
    if (imguiInitialized_)
    {
        ImGui_ImplDX11_Shutdown();
        imguiInitialized_ = false;
    }

    renderTarget_.Reset();
    context_.Reset();
    device_.Reset();
    swapChain_.Reset();
}
}


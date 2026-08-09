#pragma once

#include "render/backend.hpp"

#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include <cstdint>
#include <mutex>
#include <vector>

struct ImGui_ImplDX12_InitInfo;

namespace etb::render
{
class D3D12Backend final : public IRenderBackend
{
public:
    explicit D3D12Backend(Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue);
    ~D3D12Backend() override;

    GraphicsApi Api() const override;
    bool Initialize(IDXGISwapChain* swapChain) override;
    void NewFrame() override;
    void Render(ImDrawData* drawData) override;
    void BeforeResize() override;
    bool AfterResize() override;
    void Shutdown() override;

private:
    struct FrameContext
    {
        Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator;
        Microsoft::WRL::ComPtr<ID3D12Resource> backBuffer;
        D3D12_CPU_DESCRIPTOR_HANDLE renderTarget{};
        std::uint64_t fenceValue = 0;
    };

    static void AllocateSrv(
        ImGui_ImplDX12_InitInfo* info,
        D3D12_CPU_DESCRIPTOR_HANDLE* cpuHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE* gpuHandle);
    static void FreeSrv(
        ImGui_ImplDX12_InitInfo* info,
        D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle);

    bool CreateDescriptorHeaps();
    bool CreateFrameResources();
    bool InitializeImGuiBackend();
    void DestroyFrameResources();
    void WaitForFrame(FrameContext& frame);
    void WaitForGpu();

    Microsoft::WRL::ComPtr<IDXGISwapChain3> swapChain_;
    Microsoft::WRL::ComPtr<ID3D12Device> device_;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> renderTargetHeap_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvHeap_;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList_;
    Microsoft::WRL::ComPtr<ID3D12Fence> fence_;

    std::vector<FrameContext> frames_;
    std::vector<bool> srvSlots_;
    std::mutex srvMutex_;

    HANDLE fenceEvent_ = nullptr;
    DXGI_FORMAT renderTargetFormat_ = DXGI_FORMAT_UNKNOWN;
    UINT renderTargetDescriptorSize_ = 0;
    UINT srvDescriptorSize_ = 0;
    std::uint64_t nextFenceValue_ = 0;
    bool imguiInitialized_ = false;
};
}

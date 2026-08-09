#include "render/d3d12_backend.hpp"

#include "core/logger.hpp"

#include <backends/imgui_impl_dx12.h>

#include <algorithm>

using Microsoft::WRL::ComPtr;

namespace
{
constexpr UINT kSrvDescriptorCount = 64;
}

namespace etb::render
{
D3D12Backend::D3D12Backend(ComPtr<ID3D12CommandQueue> queue) : queue_(std::move(queue))
{
}

D3D12Backend::~D3D12Backend()
{
    Shutdown();
}

GraphicsApi D3D12Backend::Api() const
{
    return GraphicsApi::D3D12;
}

bool D3D12Backend::Initialize(IDXGISwapChain* swapChain)
{
    if (swapChain == nullptr || queue_ == nullptr)
        return false;

    if (FAILED(swapChain->QueryInterface(IID_PPV_ARGS(&swapChain_))) ||
        FAILED(swapChain_->GetDevice(IID_PPV_ARGS(&device_))))
    {
        Shutdown();
        return false;
    }

    DXGI_SWAP_CHAIN_DESC desc{};
    if (FAILED(swapChain_->GetDesc(&desc)) || desc.BufferCount == 0)
    {
        Shutdown();
        return false;
    }

    renderTargetFormat_ = desc.BufferDesc.Format;
    frames_.resize(desc.BufferCount);

    fenceEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (fenceEvent_ == nullptr ||
        FAILED(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_))) ||
        !CreateDescriptorHeaps() ||
        !CreateFrameResources())
    {
        Shutdown();
        return false;
    }

    if (FAILED(device_->CreateCommandList(
            0,
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            frames_[0].allocator.Get(),
            nullptr,
            IID_PPV_ARGS(&commandList_))))
    {
        Shutdown();
        return false;
    }
    commandList_->Close();

    if (!InitializeImGuiBackend())
    {
        Shutdown();
        return false;
    }

    core::Log("D3D12 renderer initialized");
    return true;
}

bool D3D12Backend::CreateDescriptorHeaps()
{
    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{};
    rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvDesc.NumDescriptors = static_cast<UINT>(frames_.size());
    if (FAILED(device_->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&renderTargetHeap_))))
        return false;

    D3D12_DESCRIPTOR_HEAP_DESC srvDesc{};
    srvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvDesc.NumDescriptors = kSrvDescriptorCount;
    srvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(device_->CreateDescriptorHeap(&srvDesc, IID_PPV_ARGS(&srvHeap_))))
        return false;

    renderTargetDescriptorSize_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    srvDescriptorSize_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    srvSlots_.assign(kSrvDescriptorCount, false);
    return true;
}

bool D3D12Backend::CreateFrameResources()
{
    D3D12_CPU_DESCRIPTOR_HANDLE handle = renderTargetHeap_->GetCPUDescriptorHandleForHeapStart();

    for (UINT index = 0; index < frames_.size(); ++index)
    {
        FrameContext& frame = frames_[index];
        if (FAILED(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&frame.allocator))) ||
            FAILED(swapChain_->GetBuffer(index, IID_PPV_ARGS(&frame.backBuffer))))
        {
            return false;
        }

        frame.renderTarget = handle;
        device_->CreateRenderTargetView(frame.backBuffer.Get(), nullptr, frame.renderTarget);
        handle.ptr += renderTargetDescriptorSize_;
    }

    return true;
}

bool D3D12Backend::InitializeImGuiBackend()
{
    ImGui_ImplDX12_InitInfo info{};
    info.Device = device_.Get();
    info.CommandQueue = queue_.Get();
    info.NumFramesInFlight = static_cast<int>(frames_.size());
    info.RTVFormat = renderTargetFormat_;
    info.DSVFormat = DXGI_FORMAT_UNKNOWN;
    info.UserData = this;
    info.SrvDescriptorHeap = srvHeap_.Get();
    info.SrvDescriptorAllocFn = AllocateSrv;
    info.SrvDescriptorFreeFn = FreeSrv;

    imguiInitialized_ = ImGui_ImplDX12_Init(&info);
    return imguiInitialized_;
}

void D3D12Backend::AllocateSrv(
    ImGui_ImplDX12_InitInfo* info,
    D3D12_CPU_DESCRIPTOR_HANDLE* cpuHandle,
    D3D12_GPU_DESCRIPTOR_HANDLE* gpuHandle)
{
    auto* self = static_cast<D3D12Backend*>(info->UserData);
    std::lock_guard lock(self->srvMutex_);

    const auto freeSlot = std::find(self->srvSlots_.begin(), self->srvSlots_.end(), false);
    if (freeSlot == self->srvSlots_.end())
    {
        cpuHandle->ptr = 0;
        gpuHandle->ptr = 0;
        return;
    }

    const std::size_t index = static_cast<std::size_t>(std::distance(self->srvSlots_.begin(), freeSlot));
    self->srvSlots_[index] = true;

    *cpuHandle = self->srvHeap_->GetCPUDescriptorHandleForHeapStart();
    *gpuHandle = self->srvHeap_->GetGPUDescriptorHandleForHeapStart();
    cpuHandle->ptr += index * self->srvDescriptorSize_;
    gpuHandle->ptr += index * self->srvDescriptorSize_;
}

void D3D12Backend::FreeSrv(
    ImGui_ImplDX12_InitInfo* info,
    const D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
    D3D12_GPU_DESCRIPTOR_HANDLE)
{
    auto* self = static_cast<D3D12Backend*>(info->UserData);
    std::lock_guard lock(self->srvMutex_);

    const auto start = self->srvHeap_->GetCPUDescriptorHandleForHeapStart();
    if (cpuHandle.ptr < start.ptr || self->srvDescriptorSize_ == 0)
        return;

    const std::size_t index = (cpuHandle.ptr - start.ptr) / self->srvDescriptorSize_;
    if (index < self->srvSlots_.size())
        self->srvSlots_[index] = false;
}

void D3D12Backend::NewFrame()
{
    ImGui_ImplDX12_NewFrame();
}

void D3D12Backend::WaitForFrame(FrameContext& frame)
{
    if (frame.fenceValue == 0 || fence_->GetCompletedValue() >= frame.fenceValue)
        return;

    if (SUCCEEDED(fence_->SetEventOnCompletion(frame.fenceValue, fenceEvent_)))
        WaitForSingleObject(fenceEvent_, INFINITE);
}

void D3D12Backend::WaitForGpu()
{
    if (queue_ == nullptr || fence_ == nullptr || fenceEvent_ == nullptr)
        return;

    const std::uint64_t value = ++nextFenceValue_;
    if (SUCCEEDED(queue_->Signal(fence_.Get(), value)) &&
        fence_->GetCompletedValue() < value &&
        SUCCEEDED(fence_->SetEventOnCompletion(value, fenceEvent_)))
    {
        WaitForSingleObject(fenceEvent_, INFINITE);
    }
}

void D3D12Backend::Render(ImDrawData* drawData)
{
    if (swapChain_ == nullptr || commandList_ == nullptr || frames_.empty())
        return;

    const UINT index = swapChain_->GetCurrentBackBufferIndex();
    if (index >= frames_.size())
        return;

    FrameContext& frame = frames_[index];
    WaitForFrame(frame);

    if (FAILED(frame.allocator->Reset()) || FAILED(commandList_->Reset(frame.allocator.Get(), nullptr)))
        return;

    D3D12_RESOURCE_BARRIER toRenderTarget{};
    toRenderTarget.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toRenderTarget.Transition.pResource = frame.backBuffer.Get();
    toRenderTarget.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    toRenderTarget.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    toRenderTarget.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    commandList_->ResourceBarrier(1, &toRenderTarget);

    commandList_->OMSetRenderTargets(1, &frame.renderTarget, FALSE, nullptr);
    ID3D12DescriptorHeap* heaps[] = {srvHeap_.Get()};
    commandList_->SetDescriptorHeaps(1, heaps);
    ImGui_ImplDX12_RenderDrawData(drawData, commandList_.Get());

    D3D12_RESOURCE_BARRIER toPresent = toRenderTarget;
    std::swap(toPresent.Transition.StateBefore, toPresent.Transition.StateAfter);
    commandList_->ResourceBarrier(1, &toPresent);

    if (FAILED(commandList_->Close()))
        return;

    ID3D12CommandList* lists[] = {commandList_.Get()};
    queue_->ExecuteCommandLists(1, lists);

    const std::uint64_t fenceValue = ++nextFenceValue_;
    if (SUCCEEDED(queue_->Signal(fence_.Get(), fenceValue)))
        frame.fenceValue = fenceValue;
}

void D3D12Backend::DestroyFrameResources()
{
    for (FrameContext& frame : frames_)
    {
        frame.backBuffer.Reset();
        frame.allocator.Reset();
        frame.fenceValue = 0;
    }
    frames_.clear();
}

void D3D12Backend::BeforeResize()
{
    WaitForGpu();

    if (imguiInitialized_)
    {
        ImGui_ImplDX12_Shutdown();
        imguiInitialized_ = false;
    }

    commandList_.Reset();
    DestroyFrameResources();
    renderTargetHeap_.Reset();
}

bool D3D12Backend::AfterResize()
{
    DXGI_SWAP_CHAIN_DESC desc{};
    if (swapChain_ == nullptr || FAILED(swapChain_->GetDesc(&desc)) || desc.BufferCount == 0)
        return false;

    renderTargetFormat_ = desc.BufferDesc.Format;
    frames_.resize(desc.BufferCount);

    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{};
    rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvDesc.NumDescriptors = desc.BufferCount;
    if (FAILED(device_->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&renderTargetHeap_))))
        return false;

    if (!CreateFrameResources() ||
        FAILED(device_->CreateCommandList(
            0,
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            frames_[0].allocator.Get(),
            nullptr,
            IID_PPV_ARGS(&commandList_))))
    {
        return false;
    }

    commandList_->Close();
    return InitializeImGuiBackend();
}

void D3D12Backend::Shutdown()
{
    WaitForGpu();

    if (imguiInitialized_)
    {
        ImGui_ImplDX12_Shutdown();
        imguiInitialized_ = false;
    }

    commandList_.Reset();
    DestroyFrameResources();
    renderTargetHeap_.Reset();
    srvHeap_.Reset();
    fence_.Reset();
    queue_.Reset();
    device_.Reset();
    swapChain_.Reset();
    srvSlots_.clear();

    if (fenceEvent_ != nullptr)
    {
        CloseHandle(fenceEvent_);
        fenceEvent_ = nullptr;
    }
}
}

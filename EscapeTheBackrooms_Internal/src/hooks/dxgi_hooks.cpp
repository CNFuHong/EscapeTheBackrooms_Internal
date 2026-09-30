#include "hooks/dxgi_hooks.hpp"

#include "core/logger.hpp"
#include "core/runtime.hpp"
#include "features/exit_activator.hpp"
#include "features/movement.hpp"
#include "features/pickup.hpp"
#include "features/session_limit.hpp"
#include "features/spectator.hpp"
#include "features/spawner.hpp"
#include "features/vehicle_flight.hpp"
#include "features/visuals.hpp"
#include "game/unreal_safety.hpp"
#include "render/renderer.hpp"

#include <Windows.h>

#include <MinHook.h>
#include <SDK/CoreUObject_classes.hpp>
#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <mutex>
#include <string>

using Microsoft::WRL::ComPtr;

namespace
{
using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using ResizeBuffersFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
using ExecuteCommandListsFn = void(STDMETHODCALLTYPE*)(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);
using SetCursorFn = HCURSOR(WINAPI*)(HCURSOR);
using ShowCursorFn = int(WINAPI*)(BOOL);
using ClipCursorFn = BOOL(WINAPI*)(const RECT*);
using SetCursorPosFn = BOOL(WINAPI*)(int, int);
using ProcessEventFn = void(*)(const SDK::UObject*, SDK::UFunction*, void*);

std::array<PresentFn, 2> g_originalPresent{};
std::array<ResizeBuffersFn, 2> g_originalResizeBuffers{};
ExecuteCommandListsFn g_originalExecuteCommandLists = nullptr;
SetCursorFn g_originalSetCursor = nullptr;
ShowCursorFn g_originalShowCursor = nullptr;
ClipCursorFn g_originalClipCursor = nullptr;
SetCursorPosFn g_originalSetCursorPos = nullptr;
ProcessEventFn g_originalProcessEvent = nullptr;
std::atomic_int32_t g_receiveTickNameIndex{-1};
std::atomic_int32_t g_pickupServerNameIndex{-1};
std::atomic_int32_t g_derpServerNameIndex{-1};

std::array<void*, 2> g_presentTargets{};
std::array<void*, 2> g_resizeTargets{};
std::size_t g_presentTargetCount = 0;
std::size_t g_resizeTargetCount = 0;
void* g_executeTarget = nullptr;
void* g_setCursorTarget = nullptr;
void* g_showCursorTarget = nullptr;
void* g_clipCursorTarget = nullptr;
void* g_setCursorPosTarget = nullptr;
void* g_processEventTarget = nullptr;

std::mutex g_queueMutex;
ComPtr<ID3D12CommandQueue> g_directQueue;
bool g_minhookInitialized = false;

LRESULT CALLBACK DummyWindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    return DefWindowProcW(window, message, wparam, lparam);
}

HCURSOR WINAPI HookSetCursor(HCURSOR cursor)
{
    if (etb::render::IsMenuInputActive())
    {
        if (etb::render::IsMouseCircleActive())
            cursor = nullptr;
        else if (cursor == nullptr)
            cursor = LoadCursorW(nullptr, IDC_ARROW);
    }
    return g_originalSetCursor(cursor);
}

int WINAPI HookShowCursor(BOOL show)
{
    return g_originalShowCursor(show);
}

BOOL WINAPI HookClipCursor(const RECT* rectangle)
{
    if (etb::render::IsMenuInputActive() && rectangle != nullptr)
        return g_originalClipCursor(nullptr);
    return g_originalClipCursor(rectangle);
}

BOOL WINAPI HookSetCursorPos(int x, int y)
{
    if (etb::render::IsMenuInputActive())
        return TRUE;
    return g_originalSetCursorPos(x, y);
}

__declspec(noinline) float ReadTickDeltaSeconds(void* parameters)
{
    if (parameters == nullptr)
        return 1.0f / 60.0f;
    __try
    {
        const float value = *static_cast<const float*>(parameters);
        return std::isfinite(value) && value > 0.0f && value <= 0.25f
            ? value : 1.0f / 60.0f;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 1.0f / 60.0f;
    }
}

__declspec(noinline) bool ReadFunctionMetadata(
    const SDK::UFunction* function, std::int32_t& nameIndex, std::uint32_t& flags)
{
    if (function == nullptr)
        return false;
    __try
    {
        nameIndex = function->Name.ComparisonIndex;
        flags = static_cast<std::uint32_t>(function->FunctionFlags);
        return nameIndex >= 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

void HookProcessEvent(const SDK::UObject* object, SDK::UFunction* function, void* parameters)
{
    auto& pickup = etb::features::Pickup::Instance();
    auto& movement = etb::features::Movement::Instance();
    auto& vehicleFlight = etb::features::VehicleFlight::Instance();
    auto& exitActivator = etb::features::ExitActivator::Instance();
    auto& visuals = etb::features::Visuals::Instance();
    auto& sessionLimit = etb::features::SessionLimit::Instance();
    auto& spectator = etb::features::Spectator::Instance();
    auto& spawner = etb::features::Spawner::Instance();
    const bool inspectPickup = pickup.HasPendingRequest() || pickup.HostAuthorizationEnabled();
    const bool inspectMovement = movement.NeedsGameThreadTick();
    const bool inspectVehicle = vehicleFlight.NeedsGameThreadTick();
    const bool inspectExits = exitActivator.NeedsGameThreadTick();
    const bool inspectVisualTick = visuals.NeedsGameThreadTick();
    const bool inspectSession = sessionLimit.NeedsInspection();
    const bool inspectSpectator = spectator.NeedsGameThreadTick();
    const bool inspectSpawner = spawner.NeedsGameThreadTick();
    const bool acceptRemoteDerp = movement.Settings().hostAcceptClientMovement;
    const bool inspectDerpNetwork = inspectVisualTick || acceptRemoteDerp;
    // The movement bypass must keep this hook awake even when no other feature is on.
    const bool bypassServerMove = movement.ServerMoveBypassEnabled();
    if (!inspectPickup && !inspectMovement && !inspectVehicle && !inspectExits && !inspectVisualTick &&
        !inspectDerpNetwork && !inspectSession && !inspectSpectator && !inspectSpawner &&
        !bypassServerMove)
    {
        g_originalProcessEvent(object, function, parameters);
        return;
    }

    std::int32_t functionNameIndex = -1;
    std::uint32_t functionFlags = 0;
    if (!ReadFunctionMetadata(function, functionNameIndex, functionFlags))
    {
        g_originalProcessEvent(object, function, parameters);
        return;
    }

    std::int32_t receiveTickIndex = g_receiveTickNameIndex.load(std::memory_order_relaxed);
    std::int32_t pickupServerIndex = g_pickupServerNameIndex.load(std::memory_order_relaxed);
    std::int32_t derpServerIndex = g_derpServerNameIndex.load(std::memory_order_relaxed);
    bool isReceiveTick = receiveTickIndex >= 0 && functionNameIndex == receiveTickIndex;
    bool isPickupServer = pickupServerIndex >= 0 && functionNameIndex == pickupServerIndex;
    bool isDerpServer = derpServerIndex >= 0 && functionNameIndex == derpServerIndex;

    const bool resolveReceiveTick = receiveTickIndex < 0 &&
                                    (inspectMovement || inspectVehicle || inspectExits || inspectVisualTick || inspectSession ||
                                     inspectSpectator || inspectSpawner ||
                                     acceptRemoteDerp || pickup.HasPendingRequest());
    const bool resolvePickupServer = pickupServerIndex < 0 && inspectPickup &&
        (functionFlags & static_cast<std::uint32_t>(SDK::EFunctionFlags::NetServer)) != 0;
    const bool resolveDerpServer = derpServerIndex < 0 && inspectDerpNetwork &&
        (functionFlags & static_cast<std::uint32_t>(SDK::EFunctionFlags::NetServer)) != 0;
    if (!isReceiveTick && !isPickupServer && !isDerpServer &&
        (resolveReceiveTick || resolvePickupServer || resolveDerpServer))
    {
        std::string functionName;
        if (etb::game::TryFNameToString(function->Name, functionName))
        {
            if (functionName == "ReceiveTick")
            {
                g_receiveTickNameIndex.store(functionNameIndex, std::memory_order_relaxed);
                isReceiveTick = true;
            }
            else if (functionName == "PickUp_SERVER")
            {
                g_pickupServerNameIndex.store(functionNameIndex, std::memory_order_relaxed);
                isPickupServer = true;
            }
            else if (functionName == "StartPushingActor_SERVER")
            {
                g_derpServerNameIndex.store(functionNameIndex, std::memory_order_relaxed);
                isDerpServer = true;
            }
        }
    }

    if (isDerpServer && inspectDerpNetwork &&
        visuals.OnBeforeNetworkDerpEvent(object, function, parameters))
    {
        return;
    }

    if (isPickupServer)
    {
        try
        {
            pickup.OnBeforeServerPickup(object, parameters);
        }
        catch (...)
        {
        }
    }

    if (inspectSession)
    {
        try
        {
            sessionLimit.OnBeforeProcessEvent(object, function, parameters);
        }
        catch (...)
        {
        }
    }

    // Multiplayer movement bypass: swallow the server's position correction RPCs so our
    // own (client-authoritative) position is never snapped back. The pointer compare keeps
    // this cheap — the name check only runs for our own movement component.
    if (bypassServerMove && movement.IsLocalMovementObject(object) &&
        etb::features::Movement::IsMovementCorrectionRpc(function))
    {
        return; // correction dropped before it can reach the movement component
    }

    g_originalProcessEvent(object, function, parameters);

    if (inspectSession)
    {
        try
        {
            sessionLimit.OnAfterProcessEvent(object, function, parameters);
        }
        catch (...)
        {
        }
    }

    if (isReceiveTick && pickup.HasPendingRequest())
    {
        try
        {
            pickup.OnGameThreadTick(object);
        }
        catch (...)
        {
        }
    }
    if (isReceiveTick && inspectMovement)
    {
        try
        {
            movement.OnGameThreadTick(object);
        }
        catch (...)
        {
        }
    }
    if (isReceiveTick && inspectVehicle)
    {
        try
        {
            vehicleFlight.OnGameThreadTick(object, ReadTickDeltaSeconds(parameters));
        }
        catch (...)
        {
        }
    }
    if (isReceiveTick && inspectExits)
    {
        try
        {
            exitActivator.OnGameThreadTick(object);
        }
        catch (...)
        {
        }
    }
    if (isReceiveTick && (inspectVisualTick || acceptRemoteDerp))
    {
        try
        {
            visuals.OnGameThreadTick(object, ReadTickDeltaSeconds(parameters));
        }
        catch (...)
        {
        }
    }
    if (isReceiveTick && inspectSession)
    {
        try
        {
            sessionLimit.OnGameThreadTick();
        }
        catch (...)
        {
        }
    }
    if (isReceiveTick && inspectSpectator)
    {
        try
        {
            spectator.OnGameThreadTick(object, ReadTickDeltaSeconds(parameters));
        }
        catch (...)
        {
        }
    }
    if (isReceiveTick && inspectSpawner)
    {
        try
        {
            spawner.OnGameThreadTick();
        }
        catch (...)
        {
        }
    }
}

struct DummyWindow
{
    HINSTANCE instance = GetModuleHandleW(nullptr);
    const wchar_t* className = L"ETB_Internal_DXGI_Dummy";
    HWND handle = nullptr;

    bool Create()
    {
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = DummyWindowProc;
        windowClass.hInstance = instance;
        windowClass.lpszClassName = className;

        if (RegisterClassExW(&windowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            return false;

        handle = CreateWindowExW(
            0,
            className,
            L"ETB dummy",
            WS_OVERLAPPEDWINDOW,
            0,
            0,
            100,
            100,
            nullptr,
            nullptr,
            instance,
            nullptr);

        return handle != nullptr;
    }

    ~DummyWindow()
    {
        if (handle != nullptr)
            DestroyWindow(handle);
        UnregisterClassW(className, instance);
    }
};

bool ResolveD3D11DxgiTargets(void*& present, void*& resizeBuffers)
{
    DummyWindow window;
    if (!window.Create())
        return false;

    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferCount = 2;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.OutputWindow = window.handle;
    desc.SampleDesc.Count = 1;
    desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    constexpr D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0};

    ComPtr<IDXGISwapChain> swapChain;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    D3D_FEATURE_LEVEL selected{};

    HRESULT result = D3D11CreateDeviceAndSwapChain(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        0,
        levels,
        static_cast<UINT>(std::size(levels)),
        D3D11_SDK_VERSION,
        &desc,
        &swapChain,
        &device,
        &selected,
        &context);

    if (FAILED(result))
    {
        result = D3D11CreateDeviceAndSwapChain(
            nullptr,
            D3D_DRIVER_TYPE_WARP,
            nullptr,
            0,
            levels,
            static_cast<UINT>(std::size(levels)),
            D3D11_SDK_VERSION,
            &desc,
            &swapChain,
            &device,
            &selected,
            &context);
    }

    if (FAILED(result) || swapChain == nullptr)
        return false;

    void** table = *reinterpret_cast<void***>(swapChain.Get());
    present = table[8];
    resizeBuffers = table[13];
    return present != nullptr && resizeBuffers != nullptr;
}

bool ResolveD3D12Targets(void*& present, void*& resizeBuffers, void*& executeCommandLists)
{
    DummyWindow window;
    if (!window.Create())
        return false;

    ComPtr<ID3D12Device> device;
    if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))))
        return false;

    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

    ComPtr<ID3D12CommandQueue> queue;
    if (FAILED(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue))))
        return false;

    ComPtr<IDXGIFactory4> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
        return false;

    DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
    swapChainDesc.Width = 100;
    swapChainDesc.Height = 100;
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.BufferCount = 2;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    ComPtr<IDXGISwapChain1> swapChain1;
    if (FAILED(factory->CreateSwapChainForHwnd(
            queue.Get(),
            window.handle,
            &swapChainDesc,
            nullptr,
            nullptr,
            &swapChain1)))
    {
        return false;
    }

    ComPtr<IDXGISwapChain> swapChain;
    if (FAILED(swapChain1.As(&swapChain)))
        return false;

    void** swapChainTable = *reinterpret_cast<void***>(swapChain.Get());
    void** queueTable = *reinterpret_cast<void***>(queue.Get());
    present = swapChainTable[8];
    resizeBuffers = swapChainTable[13];
    executeCommandLists = queueTable[10];
    return present != nullptr && resizeBuffers != nullptr && executeCommandLists != nullptr;
}

template <typename T, std::size_t Size>
void AddUniqueTarget(std::array<T, Size>& targets, std::size_t& count, T target)
{
    if (target == nullptr || count >= targets.size())
        return;

    for (std::size_t index = 0; index < count; ++index)
    {
        if (targets[index] == target)
            return;
    }

    targets[count++] = target;
}

HRESULT HookPresentCommon(
    const std::size_t targetIndex,
    IDXGISwapChain* swapChain,
    const UINT syncInterval,
    const UINT flags)
{
    if (!etb::core::IsStopping())
        etb::render::Renderer::Instance().OnPresent(swapChain);

    return g_originalPresent[targetIndex](swapChain, syncInterval, flags);
}

HRESULT STDMETHODCALLTYPE HookPresent0(IDXGISwapChain* swapChain, const UINT syncInterval, const UINT flags)
{
    return HookPresentCommon(0, swapChain, syncInterval, flags);
}

HRESULT STDMETHODCALLTYPE HookPresent1(IDXGISwapChain* swapChain, const UINT syncInterval, const UINT flags)
{
    return HookPresentCommon(1, swapChain, syncInterval, flags);
}

HRESULT HookResizeBuffersCommon(
    const std::size_t targetIndex,
    IDXGISwapChain* swapChain,
    const UINT bufferCount,
    const UINT width,
    const UINT height,
    const DXGI_FORMAT format,
    const UINT flags)
{
    etb::render::Renderer::Instance().BeforeResize(swapChain);
    const HRESULT result = g_originalResizeBuffers[targetIndex](swapChain, bufferCount, width, height, format, flags);
    if (SUCCEEDED(result))
        etb::render::Renderer::Instance().AfterResize(swapChain);
    return result;
}

HRESULT STDMETHODCALLTYPE HookResizeBuffers0(
    IDXGISwapChain* swapChain,
    const UINT bufferCount,
    const UINT width,
    const UINT height,
    const DXGI_FORMAT format,
    const UINT flags)
{
    return HookResizeBuffersCommon(0, swapChain, bufferCount, width, height, format, flags);
}

HRESULT STDMETHODCALLTYPE HookResizeBuffers1(
    IDXGISwapChain* swapChain,
    const UINT bufferCount,
    const UINT width,
    const UINT height,
    const DXGI_FORMAT format,
    const UINT flags)
{
    return HookResizeBuffersCommon(1, swapChain, bufferCount, width, height, format, flags);
}

void STDMETHODCALLTYPE HookExecuteCommandLists(
    ID3D12CommandQueue* queue,
    const UINT commandListCount,
    ID3D12CommandList* const* commandLists)
{
    if (queue != nullptr && queue->GetDesc().Type == D3D12_COMMAND_LIST_TYPE_DIRECT)
    {
        std::lock_guard lock(g_queueMutex);
        if (g_directQueue.Get() != queue)
        {
            g_directQueue = queue;
            etb::core::Logf("captured D3D12 direct command queue: %p", queue);
        }
    }

    g_originalExecuteCommandLists(queue, commandListCount, commandLists);
}

bool CreateAndEnableHook(void* target, void* detour, void** original)
{
    if (target == nullptr)
        return false;

    const MH_STATUS createResult = MH_CreateHook(target, detour, original);
    if (createResult != MH_OK)
    {
        etb::core::Logf("MH_CreateHook failed: %s", MH_StatusToString(createResult));
        return false;
    }

    const MH_STATUS enableResult = MH_EnableHook(target);
    if (enableResult != MH_OK)
    {
        etb::core::Logf("MH_EnableHook failed: %s", MH_StatusToString(enableResult));
        MH_RemoveHook(target);
        return false;
    }

    return true;
}

bool InstallCursorHooks()
{
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32 == nullptr)
        return false;

    g_setCursorTarget = reinterpret_cast<void*>(GetProcAddress(user32, "SetCursor"));
    g_showCursorTarget = reinterpret_cast<void*>(GetProcAddress(user32, "ShowCursor"));
    g_clipCursorTarget = reinterpret_cast<void*>(GetProcAddress(user32, "ClipCursor"));
    g_setCursorPosTarget = reinterpret_cast<void*>(GetProcAddress(user32, "SetCursorPos"));

    return CreateAndEnableHook(g_setCursorTarget, HookSetCursor, reinterpret_cast<void**>(&g_originalSetCursor)) &&
           CreateAndEnableHook(g_showCursorTarget, HookShowCursor, reinterpret_cast<void**>(&g_originalShowCursor)) &&
           CreateAndEnableHook(g_clipCursorTarget, HookClipCursor, reinterpret_cast<void**>(&g_originalClipCursor)) &&
           CreateAndEnableHook(g_setCursorPosTarget, HookSetCursorPos, reinterpret_cast<void**>(&g_originalSetCursorPos));
}
}

namespace etb::hooks
{
bool Install()
{
    void* d3d11Present = nullptr;
    void* d3d11Resize = nullptr;
    void* d3d12Present = nullptr;
    void* d3d12Resize = nullptr;

    const bool d3d11Available = ResolveD3D11DxgiTargets(d3d11Present, d3d11Resize);
    const bool d3d12Available = ResolveD3D12Targets(d3d12Present, d3d12Resize, g_executeTarget);

    AddUniqueTarget(g_presentTargets, g_presentTargetCount, d3d11Present);
    AddUniqueTarget(g_presentTargets, g_presentTargetCount, d3d12Present);
    AddUniqueTarget(g_resizeTargets, g_resizeTargetCount, d3d11Resize);
    AddUniqueTarget(g_resizeTargets, g_resizeTargetCount, d3d12Resize);

    if (g_presentTargetCount == 0 || g_resizeTargetCount == 0)
    {
        core::Log("failed to resolve DXGI swap-chain methods");
        return false;
    }

    core::Logf("dummy backends: D3D11=%s D3D12=%s", d3d11Available ? "yes" : "no", d3d12Available ? "yes" : "no");

    const MH_STATUS initResult = MH_Initialize();
    if (initResult != MH_OK && initResult != MH_ERROR_ALREADY_INITIALIZED)
    {
        core::Logf("MH_Initialize failed: %s", MH_StatusToString(initResult));
        return false;
    }
    g_minhookInitialized = true;

    g_processEventTarget = reinterpret_cast<void*>(
        SDK::InSDKUtils::GetImageBase() + SDK::Offsets::ProcessEvent);
    if (!etb::game::IsExecutable(g_processEventTarget) ||
        !CreateAndEnableHook(
            g_processEventTarget,
            HookProcessEvent,
            reinterpret_cast<void**>(&g_originalProcessEvent)))
    {
        core::Log("ProcessEvent hook unavailable; range pickup disabled");
        g_processEventTarget = nullptr;
    }
    features::Pickup::Instance().SetGameThreadHookReady(g_processEventTarget != nullptr);
    features::Movement::Instance().SetGameThreadHookReady(g_processEventTarget != nullptr);
    features::VehicleFlight::Instance().SetGameThreadHookReady(g_processEventTarget != nullptr);
    features::ExitActivator::Instance().SetGameThreadHookReady(g_processEventTarget != nullptr);
    features::Spectator::Instance().SetGameThreadHookReady(g_processEventTarget != nullptr);

    if (!InstallCursorHooks())
    {
        core::Log("failed to install Windows cursor hooks");
        Shutdown();
        return false;
    }

    void* presentDetours[] = {
        reinterpret_cast<void*>(HookPresent0),
        reinterpret_cast<void*>(HookPresent1)};
    void* resizeDetours[] = {
        reinterpret_cast<void*>(HookResizeBuffers0),
        reinterpret_cast<void*>(HookResizeBuffers1)};

    for (std::size_t index = 0; index < g_presentTargetCount; ++index)
    {
        if (!CreateAndEnableHook(
                g_presentTargets[index],
                presentDetours[index],
                reinterpret_cast<void**>(&g_originalPresent[index])))
        {
            Shutdown();
            return false;
        }
    }

    for (std::size_t index = 0; index < g_resizeTargetCount; ++index)
    {
        if (!CreateAndEnableHook(
                g_resizeTargets[index],
                resizeDetours[index],
                reinterpret_cast<void**>(&g_originalResizeBuffers[index])))
        {
            Shutdown();
            return false;
        }
    }

    if (g_executeTarget != nullptr)
    {
        if (!CreateAndEnableHook(
                g_executeTarget,
                HookExecuteCommandLists,
                reinterpret_cast<void**>(&g_originalExecuteCommandLists)))
        {
            core::Log("D3D12 queue hook unavailable; D3D11 remains usable");
            g_executeTarget = nullptr;
        }
    }
    else
    {
        core::Log("D3D12 is unavailable; D3D11 remains usable");
    }

    core::Logf(
        "DXGI hooks: Present=%zu ResizeBuffers=%zu ExecuteCommandLists=%p",
        g_presentTargetCount,
        g_resizeTargetCount,
        g_executeTarget);
    return true;
}

void Shutdown()
{
    if (!g_minhookInitialized)
        return;

    for (std::size_t index = 0; index < g_presentTargetCount; ++index)
        MH_DisableHook(g_presentTargets[index]);
    for (std::size_t index = 0; index < g_resizeTargetCount; ++index)
        MH_DisableHook(g_resizeTargets[index]);
    if (g_executeTarget != nullptr)
        MH_DisableHook(g_executeTarget);
    if (g_setCursorTarget != nullptr)
        MH_DisableHook(g_setCursorTarget);
    if (g_showCursorTarget != nullptr)
        MH_DisableHook(g_showCursorTarget);
    if (g_clipCursorTarget != nullptr)
        MH_DisableHook(g_clipCursorTarget);
    if (g_setCursorPosTarget != nullptr)
        MH_DisableHook(g_setCursorPosTarget);
    if (g_processEventTarget != nullptr)
        MH_DisableHook(g_processEventTarget);

    for (std::size_t index = 0; index < g_presentTargetCount; ++index)
        MH_RemoveHook(g_presentTargets[index]);
    for (std::size_t index = 0; index < g_resizeTargetCount; ++index)
        MH_RemoveHook(g_resizeTargets[index]);
    if (g_executeTarget != nullptr)
        MH_RemoveHook(g_executeTarget);
    if (g_setCursorTarget != nullptr)
        MH_RemoveHook(g_setCursorTarget);
    if (g_showCursorTarget != nullptr)
        MH_RemoveHook(g_showCursorTarget);
    if (g_clipCursorTarget != nullptr)
        MH_RemoveHook(g_clipCursorTarget);
    if (g_setCursorPosTarget != nullptr)
        MH_RemoveHook(g_setCursorPosTarget);
    if (g_processEventTarget != nullptr)
        MH_RemoveHook(g_processEventTarget);

    MH_Uninitialize();
    g_minhookInitialized = false;
    g_presentTargets = {};
    g_resizeTargets = {};
    g_originalPresent = {};
    g_originalResizeBuffers = {};
    g_presentTargetCount = 0;
    g_resizeTargetCount = 0;
    g_executeTarget = nullptr;
    g_originalExecuteCommandLists = nullptr;
    g_setCursorTarget = nullptr;
    g_showCursorTarget = nullptr;
    g_clipCursorTarget = nullptr;
    g_setCursorPosTarget = nullptr;
    g_processEventTarget = nullptr;
    g_originalSetCursor = nullptr;
    g_originalShowCursor = nullptr;
    g_originalClipCursor = nullptr;
    g_originalSetCursorPos = nullptr;
    g_originalProcessEvent = nullptr;
    g_receiveTickNameIndex.store(-1, std::memory_order_relaxed);
    g_pickupServerNameIndex.store(-1, std::memory_order_relaxed);
    features::Pickup::Instance().SetGameThreadHookReady(false);
    features::Movement::Instance().SetGameThreadHookReady(false);
    features::VehicleFlight::Instance().SetGameThreadHookReady(false);
    features::ExitActivator::Instance().SetGameThreadHookReady(false);
    features::Spectator::Instance().SetGameThreadHookReady(false);

    std::lock_guard lock(g_queueMutex);
    g_directQueue.Reset();
}

ComPtr<ID3D12CommandQueue> GetDirectCommandQueue()
{
    std::lock_guard lock(g_queueMutex);
    return g_directQueue;
}
}

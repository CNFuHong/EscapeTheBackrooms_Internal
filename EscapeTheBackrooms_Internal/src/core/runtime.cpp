#include "core/runtime.hpp"

#include "core/config.hpp"
#include "core/logger.hpp"
#include "hooks/dxgi_hooks.hpp"
#include "features/vehicle_flight.hpp"
#include "render/renderer.hpp"

#include <atomic>

namespace
{
std::atomic_bool g_stopping{false};
}

namespace etb::core
{
void RequestStop()
{
    g_stopping.store(true, std::memory_order_release);
}

bool IsStopping()
{
    return g_stopping.load(std::memory_order_acquire);
}

DWORD WINAPI RuntimeThread(void* module)
{
    const auto self = static_cast<HMODULE>(module);
    Log("runtime thread started");

    if (!hooks::Install())
    {
        Log("hook installation failed; unloading");
        FreeLibraryAndExitThread(self, 1);
    }

    Log("hooks installed; Insert toggles the menu, End unload is configurable");

    bool endWasDown = false;
    while (!IsStopping())
    {
        const bool endIsDown = (GetAsyncKeyState(VK_END) & 0x8000) != 0;
        if (!config::MenuSettings::disableEndUnload &&
            render::Renderer::Instance().IsGameForeground() && endIsDown && !endWasDown)
            RequestStop();
        endWasDown = endIsDown;

        Sleep(50);
    }

    Log("shutdown requested");
    features::VehicleFlight::Instance().RequestRestore();
    for (int attempt = 0;
         attempt < 20 && features::VehicleFlight::Instance().HasActiveVehicle(); ++attempt)
        Sleep(10);
    hooks::Shutdown();
    render::Renderer::Instance().Shutdown();
    Log("shutdown complete");

    FreeLibraryAndExitThread(self, 0);
}
}

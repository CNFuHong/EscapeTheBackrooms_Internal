#include <Windows.h>

#include "core/runtime.hpp"

BOOL APIENTRY DllMain(HMODULE module, const DWORD reason, void*)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(module);

        HANDLE thread = CreateThread(nullptr, 0, etb::core::RuntimeThread, module, 0, nullptr);
        if (thread == nullptr)
            return FALSE;

        CloseHandle(thread);
    }

    return TRUE;
}

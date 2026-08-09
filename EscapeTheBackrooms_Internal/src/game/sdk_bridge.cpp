#include <Windows.h>

#include <SDK/Basic.hpp>

namespace SDK::InSDKUtils
{
uintptr_t GetImageBase()
{
    return reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
}
}

#pragma once

#include <Windows.h>

#include <SDK/CoreUObject_classes.hpp>

#include <cstddef>
#include <cstdint>
#include <string>

namespace etb::game
{
inline bool IsReadable(const void* address, std::size_t size = 1)
{
    if (address == nullptr || size == 0)
        return false;

    auto current = reinterpret_cast<std::uintptr_t>(address);
    const std::uintptr_t end = current + size;
    if (end < current)
        return false;

    while (current < end)
    {
        MEMORY_BASIC_INFORMATION info{};
        if (VirtualQuery(reinterpret_cast<const void*>(current), &info, sizeof(info)) != sizeof(info))
            return false;
        if (info.State != MEM_COMMIT || (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0)
            return false;

        const auto regionEnd = reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
        if (regionEnd <= current)
            return false;
        current = regionEnd;
    }
    return true;
}

inline bool IsExecutable(const void* address)
{
    if (address == nullptr)
        return false;

    MEMORY_BASIC_INFORMATION info{};
    if (VirtualQuery(address, &info, sizeof(info)) != sizeof(info) || info.State != MEM_COMMIT ||
        (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0)
        return false;

    switch (info.Protect & 0xFF)
    {
    case PAGE_EXECUTE:
    case PAGE_EXECUTE_READ:
    case PAGE_EXECUTE_READWRITE:
    case PAGE_EXECUTE_WRITECOPY:
        return true;
    default:
        return false;
    }
}

inline bool IsLiveUObject(const SDK::UObject* object)
{
    if (!IsReadable(object, sizeof(SDK::UObject)))
        return false;

    __try
    {
        const auto flags = static_cast<std::uint32_t>(object->Flags);
        constexpr std::uint32_t destroyedFlags =
            static_cast<std::uint32_t>(SDK::EObjectFlags::BeginDestroyed) |
            static_cast<std::uint32_t>(SDK::EObjectFlags::FinishDestroyed) |
            static_cast<std::uint32_t>(SDK::EObjectFlags::MirroredGarbage);
        if ((flags & destroyedFlags) != 0 || object->Index < 0 ||
            !IsReadable(object->Class, sizeof(SDK::UClass)))
            return false;

        auto** vtable = reinterpret_cast<void**>(object->VTable);
        if (!IsReadable(vtable, sizeof(void*) * (SDK::Offsets::ProcessEventIdx + 1)))
            return false;

        return IsExecutable(vtable[SDK::Offsets::ProcessEventIdx]);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

__declspec(noinline) inline bool FNameToStringUnsafe(
    const SDK::FName* name, std::string* output)
{
    *output = name->ToString();
    return true;
}

__declspec(noinline) inline bool TryFNameToString(
    const SDK::FName& name, std::string& output)
{
    __try
    {
        return FNameToStringUnsafe(&name, &output);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        output.clear();
        return false;
    }
}

__declspec(noinline) inline bool FStringToStringUnsafe(
    const SDK::FString* value, std::string* output)
{
    *output = value->ToString();
    return true;
}

__declspec(noinline) inline bool TryFStringToString(
    const SDK::FString& value, std::string& output)
{
    __try
    {
        if (!value.IsValid() || value.Num() < 0 || value.Num() > 256 ||
            (value.Num() > 0 && !IsReadable(value.GetDataPtr(),
                                             sizeof(wchar_t) * value.Num())))
            return false;
        return FStringToStringUnsafe(&value, &output);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        output.clear();
        return false;
    }
}

inline bool CanProcessEvent(const SDK::UObject* object, const SDK::UFunction* function)
{
    return IsLiveUObject(object) && IsLiveUObject(function) &&
           IsReadable(function, sizeof(SDK::UFunction));
}

inline bool ProcessEventSafe(const SDK::UObject* object, SDK::UFunction* function, void* parameters)
{
    if (!CanProcessEvent(object, function))
        return false;

    __try
    {
        object->ProcessEvent(function, parameters);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}
}

#include "core/logger.hpp"

#include <Windows.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>

namespace
{
std::mutex g_logMutex;

std::wstring LogPath()
{
    wchar_t tempPath[MAX_PATH]{};
    const DWORD length = GetTempPathW(MAX_PATH, tempPath);
    if (length == 0 || length >= MAX_PATH)
        return L"EscapeTheBackrooms_Internal.log";

    return std::wstring(tempPath, length) + L"EscapeTheBackrooms_Internal.log";
}
}

namespace etb::core
{
void Log(const std::string_view message)
{
    std::lock_guard lock(g_logMutex);

    SYSTEMTIME time{};
    GetLocalTime(&time);

    char line[2304]{};
    const int prefixLength = std::snprintf(
        line,
        sizeof(line),
        "[%02u:%02u:%02u.%03u] ",
        time.wHour,
        time.wMinute,
        time.wSecond,
        time.wMilliseconds);

    const std::size_t prefix = prefixLength > 0 ? static_cast<std::size_t>(prefixLength) : 0;
    const std::size_t available = sizeof(line) - prefix - 3;
    const std::size_t copyLength = (std::min)(message.size(), available);
    std::memcpy(line + prefix, message.data(), copyLength);
    line[prefix + copyLength] = '\r';
    line[prefix + copyLength + 1] = '\n';
    line[prefix + copyLength + 2] = '\0';

    OutputDebugStringA(line);

    const std::wstring path = LogPath();
    HANDLE file = CreateFileW(
        path.c_str(),
        FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);

    if (file != INVALID_HANDLE_VALUE)
    {
        DWORD written = 0;
        WriteFile(file, line, static_cast<DWORD>(prefix + copyLength + 2), &written, nullptr);
        CloseHandle(file);
    }
}

void Logf(const char* format, ...)
{
    char buffer[2048]{};

    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    Log(buffer);
}
}

#pragma once

#include <Windows.h>

namespace etb::core
{
DWORD WINAPI RuntimeThread(void* module);
void RequestStop();
bool IsStopping();
}

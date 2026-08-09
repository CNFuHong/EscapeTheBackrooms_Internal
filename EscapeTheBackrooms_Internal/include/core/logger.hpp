#pragma once

#include <string_view>

namespace etb::core
{
void Log(std::string_view message);
void Logf(const char* format, ...);
}


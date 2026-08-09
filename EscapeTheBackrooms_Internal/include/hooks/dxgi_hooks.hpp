#pragma once

#include <d3d12.h>
#include <wrl/client.h>

namespace etb::hooks
{
bool Install();
void Shutdown();
Microsoft::WRL::ComPtr<ID3D12CommandQueue> GetDirectCommandQueue();
}


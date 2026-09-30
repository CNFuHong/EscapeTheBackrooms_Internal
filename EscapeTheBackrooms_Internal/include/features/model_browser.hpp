#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace SDK
{
class UObject;
}

namespace etb::features
{
struct ModelEntry
{
    std::string name;
    std::string fullName;
    SDK::UObject* mesh = nullptr;
    bool skeletal = false;
};

class ModelBrowser final
{
public:
    static ModelBrowser& Instance();

    bool FetchModels(const char* filter, std::size_t limit, std::string& message);
    void ClearList();
    void Release();

    const std::vector<ModelEntry>& Entries() const noexcept { return entries_; }
    bool HasList() const noexcept { return !entries_.empty(); }

private:
    ModelBrowser() = default;

    std::vector<ModelEntry> entries_;
};
}

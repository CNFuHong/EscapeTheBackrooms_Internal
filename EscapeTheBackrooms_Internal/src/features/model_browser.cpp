#include "features/model_browser.hpp"

#include "game/unreal_safety.hpp"

#include <Windows.h>
#include <SDK/Basic.hpp>
#include <SDK/CoreUObject_classes.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>
#include <vector>

namespace etb::features
{
namespace
{
using game::IsLiveUObject;
using game::IsReadable;

std::string ToLower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch)
    {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

// True when `object` is a mesh asset; `skeletal` reports which kind.
// The check walks the class chain with exact name matches, so mesh components
// (SkeletalMeshComponent / StaticMeshComponent) are NOT picked up.
bool ClassifyMesh(const SDK::UObject* object, bool& skeletal)
{
    if (!IsLiveUObject(object))
        return false;
    try
    {
        for (SDK::UStruct* type = object->Class; type != nullptr; type = type->SuperStruct)
        {
            if (!IsLiveUObject(type))
                return false;
            std::string name;
            if (!game::TryFNameToString(type->Name, name))
                return false;
            if (name == "SkeletalMesh")
            {
                skeletal = true;
                return true;
            }
            if (name == "StaticMesh")
            {
                skeletal = false;
                return true;
            }
        }
    }
    catch (...)
    {
    }
    return false;
}
    
// Rebuilds "Class Outer.Object" like UObject::GetFullName(), but without calling it:
// the SDK only declares GetFullName in the header, its definition lives in
// CoreUObject_functions.cpp which this project does not compile, so the call would
// fail to link. Every pointer is guarded by IsLiveUObject / TryFNameToString.
bool TryGetFullName(const SDK::UObject* object, std::string& output)
{
    output.clear();
    if (!IsLiveUObject(object))
        return false;

    std::string path;
    const SDK::UObject* outer = object;
    for (int depth = 0; outer != nullptr && depth < 64; ++depth)
    {
        if (!IsLiveUObject(outer))
            return false;
        std::string part;
        if (!game::TryFNameToString(outer->Name, part))
            return false;
        if (!path.empty())
            path.insert(0, ".");
        path.insert(0, part);
        outer = outer->Outer;
    }

    std::string cls;
    if (IsLiveUObject(object->Class) && game::TryFNameToString(object->Class->Name, cls))
        output = cls + " " + path;
    else
        output = path;
    return !output.empty();
}
}

ModelBrowser& ModelBrowser::Instance()
{
    static ModelBrowser instance;
    return instance;
}

bool ModelBrowser::FetchModels(const char* filter, std::size_t limit, std::string& message)
{
    entries_.clear();

    // Same OR-split as the spawner: keywords separated by ',' ' ' or '|'.
    std::vector<std::string> keywords;
    if (filter != nullptr)
    {
        std::string current;
        for (const char* p = filter; *p; ++p)
        {
            if (*p == ',' || *p == ' ' || *p == '|')
            {
                if (!current.empty())
                {
                    keywords.push_back(ToLower(current));
                    current.clear();
                }
            }
            else
            {
                current += *p;
            }
        }
        if (!current.empty())
            keywords.push_back(ToLower(current));
    }
    const bool acceptAll = keywords.empty();

    auto* objects = SDK::UObject::GObjects.GetTypedPtr();
    if (!objects || !IsReadable(objects, sizeof(*objects)))
    {
        message = "GObjects unavailable";
        return false;
    }
    const int count = objects->Num();
    if (count <= 0 || count > 4000000)
    {
        message = "Invalid object count";
        return false;
    }

    const std::size_t max = std::max<std::size_t>(1, limit);
    for (int index = 0; index < count && entries_.size() < max; ++index)
    {
        SDK::UObject* object = objects->GetByIndex(index);
        if (object == nullptr || !IsLiveUObject(object))
            continue;

        bool skeletal = false;
        if (!ClassifyMesh(object, skeletal))
            continue;

        std::string name;
        if (!game::TryFNameToString(object->Name, name) || name.empty())
            continue;
        const std::string lower = ToLower(name);
        // Class default objects are templates, not usable assets.
        if (lower.rfind("default__", 0) == 0)
            continue;

        if (!acceptAll)
        {
            bool match = false;
            for (const std::string& keyword : keywords)
            {
                if (lower.find(keyword) != std::string::npos)
                {
                    match = true;
                    break;
                }
            }
            if (!match)
                continue;
        }

        std::string fullName;
        TryGetFullName(object, fullName);
        entries_.push_back({std::move(name), std::move(fullName), object, skeletal});
    }

    if (entries_.empty())
    {
        message = acceptAll
            ? "No mesh assets found"
            : std::string("No meshes matching \"") + filter + "\"";
        return false;
    }
    message = "Found " + std::to_string(entries_.size()) + " meshes";
    return true;
}

void ModelBrowser::ClearList()
{
    entries_.clear();
}

void ModelBrowser::Release()
{
    entries_.clear();
}
}

#include "features/spawner.hpp"

#include "core/logger.hpp"
#include "features/notifications.hpp"
#include "game/unreal_safety.hpp"

#include <Windows.h>
#include <SDK/Basic.hpp>
#include <SDK/CoreUObject_classes.hpp>
#include <SDK/Engine_classes.hpp>
#include <SDK/Engine_parameters.hpp>
#include <SDK/NavigationSystem_classes.hpp>
#include <SDK/NavigationSystem_parameters.hpp>
#include <SDK/BPCharacter_Demo_classes.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace etb::features
{
namespace
{
using game::IsLiveUObject;
using game::IsReadable;
using Json = etb::core::config::Json;

std::wstring Utf8ToWide(const std::string& value)
{
    if (value.empty())
        return {};
    const int length = MultiByteToWideChar(CP_UTF8, 0, value.c_str(),
                                           static_cast<int>(value.size()), nullptr, 0);
    if (length <= 0)
        return {};
    std::wstring wide(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()),
                        wide.data(), length);
    return wide;
}

std::string GetFavoritesPath()
{
    static std::string path;
    if (!path.empty())
        return path;
    wchar_t localAppData[32768]{};
    const DWORD len = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, 32768);
    if (len == 0 || len >= 32768)
        return {};
    const std::wstring dir = std::wstring(localAppData) +
        L"\\EscapeTheBackrooms_Internal\\spawner_favorites.json";
    const int needed = WideCharToMultiByte(CP_UTF8, 0, dir.c_str(),
                                           static_cast<int>(dir.size()), nullptr, 0, nullptr, nullptr);
    if (needed <= 0)
        return {};
    path.resize(static_cast<std::size_t>(needed));
    WideCharToMultiByte(CP_UTF8, 0, dir.c_str(), static_cast<int>(dir.size()),
                        path.data(), needed, nullptr, nullptr);
    return path;
}

SDK::UWorld* ResolveWorld()
{
    const auto base = SDK::InSDKUtils::GetImageBase();
    auto** address = reinterpret_cast<SDK::UWorld**>(base + SDK::Offsets::GWorld);
    if (!IsReadable(address, sizeof(*address)))
        return nullptr;
    SDK::UWorld* world = *address;
    return IsLiveUObject(world) ? world : nullptr;
}

SDK::UObject* FindNamedObject(const char* wantedName)
{
    if (wantedName == nullptr)
        return nullptr;
    auto* objects = SDK::UObject::GObjects.GetTypedPtr();
    if (!IsReadable(objects, sizeof(*objects)))
        return nullptr;
    const int count = objects->Num();
    if (count <= 0 || count > 4000000)
        return nullptr;
    for (int index = 0; index < count; ++index)
    {
        SDK::UObject* object = objects->GetByIndex(index);
        if (object == nullptr)
            continue;
        std::string name;
        if (game::TryFNameToString(object->Name, name) && name == wantedName &&
            IsLiveUObject(object))
            return object;
    }
    return nullptr;
}

bool IsObjectNamed(const SDK::UObject* object, const char* className)
{
    if (!IsLiveUObject(object) || className == nullptr)
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
            if (name == className)
                return true;
        }
    }
    catch (...)
    {
    }
    return false;
}

// True if `cls` is (derived from) an AActor, so it can actually be spawned.
bool IsActorClass(SDK::UClass* cls)
{
    if (!IsLiveUObject(cls))
        return false;
    try
    {
        for (SDK::UStruct* type = cls->SuperStruct; type != nullptr; type = type->SuperStruct)
        {
            if (!IsLiveUObject(type))
                return false;
            std::string name;
            if (!game::TryFNameToString(type->Name, name))
                return false;
            if (name == "Actor")
                return true;
        }
    }
    catch (...)
    {
    }
    return false;
}


SDK::UFunction* FindFunction(SDK::UObject* object, const char* functionName)
{
    if (!IsLiveUObject(object))
        return nullptr;
    try
    {
        for (SDK::UStruct* type = object->Class; type != nullptr; type = type->SuperStruct)
        {
            if (!IsReadable(type, sizeof(SDK::UStruct)))
                break;
            for (SDK::UField* field = type->Children; field != nullptr; field = field->Next)
            {
                if (!IsReadable(field, sizeof(SDK::UField)))
                    break;
                std::string name;
                if (!game::TryFNameToString(field->Name, name))
                    return nullptr;
                if (name == functionName)
                {
                    auto* function = reinterpret_cast<SDK::UFunction*>(field);
                    return IsLiveUObject(function) ? function : nullptr;
                }
            }
        }
    }
    catch (...)
    {
        return nullptr;
    }
    return nullptr;
}

bool Invoke(SDK::UObject* object, const char* functionName, void* parameters)
{
    SDK::UFunction* function = FindFunction(object, functionName);
    if (!game::CanProcessEvent(object, function))
        return false;
    const auto flags = function->FunctionFlags;
    function->FunctionFlags |= 0x400;
    const bool invoked = game::ProcessEventSafe(object, function, parameters);
    if (IsLiveUObject(function))
        function->FunctionFlags = flags;
    return invoked;
}

// Resolves world, local controller, pawn and returns the pawn's location + the
// controller's yaw (used to spawn in front of the player).
bool ResolveSpawnFrame(SDK::UWorld*& world, SDK::FVector& location, float& yaw)
{
    world = ResolveWorld();
    if (!IsLiveUObject(world))
        return false;
    SDK::UGameInstance* instance = world->OwningGameInstance;
    if (!IsLiveUObject(instance))
        return false;
    const auto players = instance->LocalPlayers;
    if (!players.IsValid() || players.Num() <= 0 || players.Num() > 8 ||
        !IsReadable(players.GetDataPtr(), sizeof(SDK::ULocalPlayer*) * players.Num()))
        return false;
    SDK::ULocalPlayer* local = players.GetDataPtr()[0];
    if (!IsLiveUObject(local))
        return false;
    SDK::APlayerController* controller = local->PlayerController;
    if (!IsLiveUObject(controller))
        return false;
    SDK::APawn* pawn = controller->AcknowledgedPawn;
    if (!IsLiveUObject(pawn))
        return false;

    SDK::Params::Actor_K2_GetActorLocation locParams{};
    if (!Invoke(pawn, "K2_GetActorLocation", &locParams) || !std::isfinite(locParams.ReturnValue.X))
        return false;
    location = locParams.ReturnValue;

    SDK::Params::Controller_GetControlRotation rotParams{};
    if (!Invoke(controller, "GetControlRotation", &rotParams))
        yaw = 0.0f;
    else
        yaw = std::isfinite(rotParams.ReturnValue.Yaw) ? rotParams.ReturnValue.Yaw : 0.0f;
    return true;
}

bool SpawnActorAt(SDK::UWorld* world, SDK::UClass* actorClass, const SDK::FVector& location,
                  const SDK::FRotator& rotation, SDK::AActor* owner)
{
    static SDK::UObject* gameplayStatics = nullptr;
    if (!IsLiveUObject(gameplayStatics))
        gameplayStatics = FindNamedObject("Default__GameplayStatics");
    if (!IsLiveUObject(gameplayStatics) || !IsLiveUObject(actorClass))
        return false;

    // EnableCollision/transform identity, then use the deferred path so Blueprint
    // construction scripts run.
    SDK::FTransform transform{};
    transform.Rotation.W = 1.0f;
    transform.Translation = location;
    transform.Scale3D = {1.0f, 1.0f, 1.0f};

    SDK::AActor* spawned = nullptr;
    SDK::Params::GameplayStatics_BeginDeferredActorSpawnFromClass begin{};
    begin.WorldContextObject = world;
    begin.ActorClass = actorClass;
    begin.SpawnTransform = transform;
    begin.CollisionHandlingOverride = SDK::ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    begin.Owner = owner;
    if (Invoke(gameplayStatics, "BeginDeferredActorSpawnFromClass", &begin) &&
        IsLiveUObject(begin.ReturnValue))
    {
        SDK::Params::GameplayStatics_FinishSpawningActor finish{};
        finish.Actor = begin.ReturnValue;
        finish.SpawnTransform = transform;
        if (Invoke(gameplayStatics, "FinishSpawningActor", &finish))
            spawned = IsLiveUObject(finish.ReturnValue) ? finish.ReturnValue : begin.ReturnValue;
    }
    if (spawned != nullptr && !IsLiveUObject(spawned))
        spawned = nullptr;
    return spawned != nullptr;
}

std::string ToLower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch)
    {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool EndsWith(const std::string& value, const std::string& suffix)
{
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

SpawnCategory ClassifySpawnCategory(const std::string& lower)
{
    if (lower.find("item") != std::string::npos ||
        lower.find("flashlight") != std::string::npos ||
        lower.find("glowstick") != std::string::npos ||
        lower.find("battery") != std::string::npos ||
        lower.find("bandage") != std::string::npos ||
        lower.find("key") != std::string::npos ||
        lower.find("ticket") != std::string::npos ||
        lower.find("med") != std::string::npos) return SpawnCategory::Item;

    if (lower.find("moth") != std::string::npos ||
        lower.find("hound") != std::string::npos ||
        lower.find("smiler") != std::string::npos ||
        lower.find("skinstealer") != std::string::npos ||
        lower.find("bone") != std::string::npos ||
        lower.find("bacteria") != std::string::npos ||
        lower.find("clown") != std::string::npos ||
        lower.find("jester") != std::string::npos ||
        lower.find("monster") != std::string::npos ||
        lower.find("entity") != std::string::npos ||
        lower.find("character") != std::string::npos ||
        lower.find("pawn") != std::string::npos ||
        lower.find("ai_") != std::string::npos ||
        lower.find("enemy") != std::string::npos) return SpawnCategory::Entity;

    if (lower.find("wall") != std::string::npos ||
        lower.find("table") != std::string::npos ||
        lower.find("door") != std::string::npos ||
        lower.find("lock") != std::string::npos ||
        lower.find("crate") != std::string::npos ||
        lower.find("box") != std::string::npos ||
        lower.find("shelf") != std::string::npos ||
        lower.find("cabinet") != std::string::npos ||
        lower.find("furniture") != std::string::npos ||
        lower.find("barricade") != std::string::npos ||
        lower.find("pillar") != std::string::npos ||
        lower.find("lamp") != std::string::npos ||
        lower.find("ceiling") != std::string::npos ||
        lower.find("floor") != std::string::npos ||
        lower.find("computer") != std::string::npos ||
        lower.find("generator") != std::string::npos ||
        lower.find("valve") != std::string::npos ||
        lower.find("lever") != std::string::npos ||
        lower.find("ladder") != std::string::npos ||
        lower.find("tank") != std::string::npos ||
        lower.find("card") != std::string::npos ||
        lower.find("chair") != std::string::npos) return SpawnCategory::Building;

    return SpawnCategory::Other;
}

std::string CategoryKeywords(SpawnCategory category)
{
    switch (category)
    {
    case SpawnCategory::Building:
        return "wall,table,door,lock,crate,box,shelf,cabinet,furniture,barricade,pillar,lamp,ceiling,floor,computer,generator,valve,lever,ladder";
    case SpawnCategory::Item:
        return "item,flashlight,glowstick,battery,bandage,key,ticket";
    case SpawnCategory::Entity:
        return "moth,hound,smiler,skinstealer,bone,bacteria,clown,monster,entity,character,pawn,enemy";
    default:
        return "bp_";
    }
}

const char* CategoryLabel(SpawnCategory category)
{
    switch (category)
    {
    case SpawnCategory::Building: return "Building";
    case SpawnCategory::Item: return "Item";
    case SpawnCategory::Entity: return "Entity";
    default: return "Other";
    }
}
enum class ScanResult
{
    Ok,
    NoWorld,
    NoObjects,
    InvalidCount
};

// Scans every object once and keeps the actor classes accepted by `accept`.
// This is the single shared pass behind both filter-based and category-based listing.
template <typename AcceptFn>
ScanResult ScanActorClasses(const std::size_t limit, const AcceptFn& accept,
                            std::vector<SpawnEntry>& out)
{
    out.clear();
    if (ResolveWorld() == nullptr)
        return ScanResult::NoWorld;

    auto* objects = SDK::UObject::GObjects.GetTypedPtr();
    if (!objects || !IsReadable(objects, sizeof(*objects)))
        return ScanResult::NoObjects;
    const int count = objects->Num();
    if (count <= 0 || count > 4000000)
        return ScanResult::InvalidCount;

    const std::size_t max = std::max<std::size_t>(1, limit);
    for (int index = 0; index < count && out.size() < max; ++index)
    {
        SDK::UObject* object = objects->GetByIndex(index);
        if (object == nullptr || !IsLiveUObject(object))
            continue;
        std::string name;
        if (!game::TryFNameToString(object->Name, name))
            continue;
        const std::string lower = ToLower(name);
        // Class default objects are not spawnable instances.
        if (lower.rfind("default__", 0) == 0)
            continue;
        // Manager/singleton actors are placed in levels, not meant to be spawned.
        if (lower.find("manager") != std::string::npos)
            continue;

        auto* cls = reinterpret_cast<SDK::UClass*>(object);
        if (!IsLiveUObject(cls) || !IsActorClass(cls))
            continue;

        const SpawnCategory category = ClassifySpawnCategory(lower);
        if (!accept(lower, category))
            continue;
        out.push_back({std::move(name), cls, category});
    }
    return ScanResult::Ok;
}

const char* ScanResultMessage(ScanResult result)
{
    switch (result)
    {
    case ScanResult::NoWorld: return "Not in a game scene (world not ready)";
    case ScanResult::NoObjects: return "GObjects unavailable";
    case ScanResult::InvalidCount: return "Invalid object count";
    case ScanResult::Ok:
    default: return "";
    }
}
}

Spawner& Spawner::Instance()
{
    static Spawner instance;
    return instance;
}

bool Spawner::FetchList(const char* filter, std::size_t limit, std::string& message)
{
    // Parse filter into keywords (OR-match): split on ',' ' ' '|'.
    // An EMPTY filter now lists every spawnable actor class, which is what the original,
    // comprehensive list did — previously it errored out and the list stayed tiny.
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
    const ScanResult result = ScanActorClasses(
        limit,
        [&keywords, acceptAll](const std::string& lower, SpawnCategory)
        {
            if (acceptAll)
                return true;
            for (const std::string& keyword : keywords)
            {
                if (lower.find(keyword) != std::string::npos)
                    return true;
            }
            return false;
        },
        entries_);

    if (result != ScanResult::Ok)
    {
        message = ScanResultMessage(result);
        return false;
    }
    if (entries_.empty())
    {
        message = acceptAll
            ? "No spawnable actor classes found"
            : std::string("No matching actor classes (\"") + filter + "\")";
        return false;
    }
    message = "Found " + std::to_string(entries_.size()) + " classes";
    return true;
}

bool Spawner::FetchByCategory(SpawnCategory category, std::size_t limit, std::string& message)
{
    // List every class that belongs to the category, instead of matching a short keyword
    // list, so each category button shows everything it contains.
    const ScanResult result = ScanActorClasses(
        limit,
        [category](const std::string&, SpawnCategory entryCategory)
        { return entryCategory == category; },
        entries_);

    if (result != ScanResult::Ok)
    {
        message = ScanResultMessage(result);
        return false;
    }
    if (entries_.empty())
    {
        message = std::string("No ") + CategoryLabel(category) + " classes found";
        return false;
    }
    message = std::string("Found ") + std::to_string(entries_.size()) + " " +
              CategoryLabel(category) + " classes";
    return true;
}

bool Spawner::SpawnByIndex(std::size_t index, std::string& message, bool notify)
{
    if (index >= entries_.size())
    {
        message = "Invalid entry index";
        return false;
    }
    SDK::UWorld* world = nullptr;
    SDK::FVector location{};
    float yaw = 0.0f;
    if (!ResolveSpawnFrame(world, location, yaw))
    {
        message = "No spawn context (world/pawn)";
        return false;
    }
    const float distance = std::clamp(settings_.spawnDistance, 0.0f, 10000.0f);
    const float yawRad = yaw * 3.14159265f / 180.0f;
    const SDK::FVector forward{std::cos(yawRad), std::sin(yawRad), 0.0f};

    SDK::FRotator rot{};
    rot.Yaw = yaw;
    rot.Roll = 0.0f;
    rot.Pitch = 0.0f;

    const int count = std::clamp(settings_.spawnCount, 1, 50);
    std::size_t spawned = 0;
    for (int i = 0; i < count; ++i)
    {
        const float step = static_cast<float>(i) * 120.0f;
        const SDK::FVector spawnLoc{location.X + forward.X * (distance + step),
                                    location.Y + forward.Y * (distance + step), location.Z};
        if (SpawnActorAt(world, entries_[index].cls, spawnLoc, rot, nullptr))
            ++spawned;
    }
    if (spawned == 0)
    {
        message = "Spawn failed for \"" + entries_[index].name + "\"";
        if (notify)
            Notifications::Instance().Push("[Spawner] " + message, NotificationType::Error);
        return false;
    }
    message = "Spawned " + std::to_string(spawned) + "x " + entries_[index].name;
    if (notify)
        Notifications::Instance().Push("[Spawner] " + message, NotificationType::Success);
    return true;
}

void Spawner::ClearList()
{
    entries_.clear();
}

void Spawner::Release()
{
    entries_.clear();
    favorites_.clear();
}

void Spawner::RequestSpawn(std::size_t index)
{
    if (index >= entries_.size())
        return;
    spawnRequestIndex_.store(static_cast<int>(index), std::memory_order_relaxed);
    spawnRequestPending_.store(true, std::memory_order_release);
}

bool Spawner::NeedsGameThreadTick() const noexcept
{
    // Lock-free: this is called on EVERY ProcessEvent, so it must never touch a mutex.
    return spawnRequestPending_.load(std::memory_order_acquire) ||
           exitPathActive_.load(std::memory_order_acquire);
}

void Spawner::OnGameThreadTick()
{
    if (spawnRequestPending_.exchange(false, std::memory_order_acq_rel))
    {
        const int index = spawnRequestIndex_.load(std::memory_order_relaxed);
        if (index >= 0 && index < static_cast<int>(entries_.size()))
        {
            std::string message;
            SpawnByIndex(static_cast<std::size_t>(index), message, true);
        }
    }

    // Exit-path lookup is game-thread bound: the cheap render-thread request is
    // consumed here and the nav queries run on this (game) thread, never on Present.
    // One target per tick keeps a multi-exit batch from spiking the game thread.
    {
        SDK::UWorld* world = nullptr;
        SDK::FVector start{};
        SDK::FVector target{};
        std::size_t slot = 0;
        bool have = false;
        {
            std::lock_guard lock(exitPathMutex_);
            if (exitPathEnabled_ && exitPathRequested_ && game::IsLiveUObject(exitPathWorld_) &&
                exitPathTargetIndex_ < exitPathTargets_.size())
            {
                world = exitPathWorld_;
                start = exitPathStart_;
                target = exitPathTargets_[exitPathTargetIndex_];
                slot = exitPathTargetIndex_;
                have = true;
            }
        }
        if (have)
        {
            std::vector<SDK::FVector> points;
            const bool ok = ComputeExitPath(world, start, target, points);
            std::lock_guard lock(exitPathMutex_);
            if (slot < exitPathStaging_.size())
                exitPathStaging_[slot] = ok ? std::move(points) : std::vector<SDK::FVector>{};
            ++exitPathTargetIndex_;
            if (exitPathTargetIndex_ >= exitPathTargets_.size())
            {
                exitPathRequested_ = false;
                exitPathActive_.store(false, std::memory_order_release);
                exitPathTargetIndex_ = 0;
                exitPathTargets_.clear();
                // Publish finished paths and carry over any entry that failed this round,
                // so a transient nav miss doesn't erase a route that was just working.
                for (std::size_t index = 0; index < exitPathStaging_.size(); ++index)
                {
                    if (!exitPathStaging_[index].empty())
                        exitPaths_[index] = std::move(exitPathStaging_[index]);
                }
                exitPathStaging_.clear();
            }
        }
    }
}

void Spawner::SetExitPathEnabled(bool enabled) noexcept
{
    std::lock_guard lock(exitPathMutex_);
    exitPathEnabled_ = enabled;
    // NOTE: only clear the busy flag when turning OFF. The ESP calls this with `true` every
    // frame, and clearing it here wiped pending requests before the game thread tick could
    // consume them — which silently killed the path-to-exit feature.
    if (!enabled)
    {
        exitPathActive_.store(false, std::memory_order_release);
        exitPaths_.clear();
        exitPathStaging_.clear();
        exitPathTargets_.clear();
        exitPathTargetIndex_ = 0;
        exitPathRequested_ = false;
    }
}

void Spawner::RequestExitPaths(SDK::UWorld* world, const SDK::FVector& start,
                               const std::vector<SDK::FVector>& ends)
{
    std::lock_guard lock(exitPathMutex_);
    if (!exitPathEnabled_ || ends.empty())
        return;
    const ULONGLONG now = GetTickCount64();
    if (exitPathRequested_ || now - exitPathLastMs_ < 600)
        return; // a batch is already in flight, or we recomputed too recently

    exitPathWorld_ = world;
    exitPathStart_ = start;
    exitPathTargets_ = ends;
    exitPathStaging_.assign(ends.size(), std::vector<SDK::FVector>{});
    if (exitPaths_.size() != ends.size())
        exitPaths_.assign(ends.size(), std::vector<SDK::FVector>{});
    exitPathTargetIndex_ = 0;
    exitPathRequested_ = true;
    exitPathActive_.store(true, std::memory_order_release);
    exitPathLastMs_ = now;
}

bool Spawner::SnapshotExitPaths(std::vector<std::vector<SDK::FVector>>& out) const
{
    std::lock_guard lock(exitPathMutex_);
    out = exitPaths_;
    for (const auto& path : out)
    {
        if (!path.empty())
            return true;
    }
    return false;
}

bool Spawner::ComputeExitPath(SDK::UWorld* world, const SDK::FVector& start,
                              const SDK::FVector& end, std::vector<SDK::FVector>& outPoints)
{
    if (!game::IsLiveUObject(world))
        return false;
    SDK::UObject* navCdo = FindNamedObject("Default__NavigationSystemV1");
    if (!game::IsLiveUObject(navCdo))
        return false;

    SDK::Params::NavigationSystemV1_FindPathToLocationSynchronously params{};
    params.WorldContextObject = world;
    params.PathStart = start;
    params.PathEnd = end;
    params.PathfindingContext = nullptr;
    params.FilterClass = nullptr;
    if (!Invoke(navCdo, "FindPathToLocationSynchronously", &params) ||
        !game::IsLiveUObject(params.ReturnValue))
        return false;
    auto* path = static_cast<SDK::UNavigationPath*>(params.ReturnValue);
    const auto& points = path->PathPoints;
    if (!points.IsValid() || points.Num() <= 0 || points.Num() > 512)
        return false;
    outPoints.clear();
    for (int index = 0; index < points.Num(); ++index)
        outPoints.push_back(points[index]);
    return !outPoints.empty();
}

void Spawner::LoadFavorites()
{
    favorites_.clear();
    favoritesPath_ = GetFavoritesPath();
    if (favoritesPath_.empty())
        return;
    try
    {
        std::ifstream input(Utf8ToWide(favoritesPath_), std::ios::binary);
        if (!input)
            return;
        const Json json = Json::parse(input, nullptr, true, true);
        if (!json.is_object())
            return;
        for (auto it = json.begin(); it != json.end(); ++it)
        {
            if (!it.value().is_object())
                continue;
            SpawnFav fav;
            if (it.value().contains("favorite") && it.value().at("favorite").is_boolean())
                fav.favorite = it.value().at("favorite").get<bool>();
            if (it.value().contains("note") && it.value().at("note").is_string())
                fav.note = it.value().at("note").get<std::string>();
            favorites_[it.key()] = std::move(fav);
        }
    }
    catch (...)
    {
    }
}

void Spawner::SaveFavorites() const
{
    if (favoritesPath_.empty())
        return;
    try
    {
        Json json = Json::object();
        for (const auto& [name, fav] : favorites_)
        {
            json[name] = Json{{"favorite", fav.favorite}, {"note", fav.note}};
        }
        const std::wstring wide = Utf8ToWide(favoritesPath_);
        std::filesystem::create_directories(std::filesystem::path(wide).parent_path());
        std::ofstream output(wide, std::ios::binary | std::ios::trunc);
        if (!output)
            return;
        output << json.dump(4);
    }
    catch (...)
    {
    }
}

void Spawner::ToggleFavorite(const std::string& name)
{
    SpawnFav& fav = favorites_[name];
    fav.favorite = !fav.favorite;
}

void Spawner::SetNote(const std::string& name, const std::string& note)
{
    favorites_[name].note = note;
}

const SpawnFav* Spawner::MetaFor(const std::string& name) const
{
    const auto it = favorites_.find(name);
    return it != favorites_.end() ? &it->second : nullptr;
}
}

#include "features/esp.hpp"

#include "core/logger.hpp"
#include "features/exit_activator.hpp"
#include "features/notifications.hpp"
#include "game/unreal_safety.hpp"
#include "render/esp_draw.hpp"

#include <Windows.h>
#include <SDK/Backrooms_classes.hpp>
#include <SDK/Engine_classes.hpp>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace etb::features
{
namespace
{
using Clock = std::chrono::steady_clock;
using Category = render::EspCategory;

struct CameraState
{
    SDK::FVector location{};
    SDK::FRotator rotation{};
    float fov = 90.0f;
};

struct CachedEntity
{
    SDK::AActor* actor = nullptr;
    SDK::USceneComponent* root = nullptr;
    std::string name;
    std::string lowerActorName;
    std::string className;
    std::string lowerClassName;
    Category category = Category::Item;
    bool categorized = false;
    bool lever = false;
    bool valve = false;
    float sanity = -1.0f;
    float maxSanity = -1.0f;
    bool dynamicEligible = false;
    int32_t comparisonIndex = 0;
    uint32_t number = 0;
    uint64_t generation = 0;
    Clock::time_point spawnedUntil{};
    bool suppressDiscovery = false;
    SDK::FVector lastMotionLocation{};
    Clock::time_point lastMotionSample{};
    Clock::time_point movingUntil{};
    float speedMetersPerSecond = 0.0f;
    std::array<Clock::time_point, 3> movementEvents{};
    int movementEventCount = 0;
    bool motionHistoryValid = false;
};

struct FrameContext
{
    SDK::UWorld* world = nullptr;
    SDK::APawn* localPawn = nullptr;
    SDK::APlayerController* localController = nullptr;
    SDK::APlayerCameraManager* cameraManager = nullptr;
};

bool IsReadable(const void* address, std::size_t size = 1)
{
    if (address == nullptr || size == 0)
        return false;

    auto current = reinterpret_cast<uintptr_t>(address);
    const uintptr_t end = current + size;
    if (end < current)
        return false;

    while (current < end)
    {
        MEMORY_BASIC_INFORMATION info{};
        if (VirtualQuery(reinterpret_cast<const void*>(current), &info, sizeof(info)) != sizeof(info))
            return false;

        if (info.State != MEM_COMMIT || (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0)
            return false;

        const uintptr_t regionEnd = reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
        if (regionEnd <= current)
            return false;
        current = regionEnd;
    }
    return true;
}

template <typename T>
bool IsSaneArray(const UC::TArray<T>& array, int maximum)
{
    const int count = array.Num();
    return count >= 0 && count <= maximum && array.Max() >= count && array.Max() <= maximum * 2 &&
           (count == 0 || IsReadable(array.GetDataPtr(), sizeof(T) * static_cast<std::size_t>(count)));
}

bool ContainsAny(const std::string& value, std::initializer_list<const char*> words)
{
    for (const char* word : words)
    {
        if (value.find(word) != std::string::npos)
            return true;
    }
    return false;
}

bool IsExcludedActorClass(const std::string& lowerActorName,
                          const std::string& lowerClassName)
{

    const bool nonInteractiveTestHelper = ContainsAny(lowerActorName, {
               "player lp test tapebox", "player_lp_test_tapebox",
               "player lp test controller", "player_lp_test_controller",
               "sm furniture gallery exit sign", "sm_furniture_gallery_exit_sign",
               "rollerplayer", "roller_player"
           }) ||
           ContainsAny(lowerClassName, {
               "player lp test tapebox", "player_lp_test_tapebox",
               "player lp test controller", "player_lp_test_controller",
               "sm furniture gallery exit sign", "sm_furniture_gallery_exit_sign",
               "rollerplayer", "roller_player"
           });
    if (nonInteractiveTestHelper)
        return true;

    return ContainsAny(lowerClassName, {
        "staticmeshactor",
        "instancedfoliageactor",
        "landscapeproxy",
        "landscape",
        "reflectioncapture",
        "decalactor",
        "bp_ceiling_",
        "bp_wall_"
    });
}

std::string ToLower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch)
    {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::optional<Category> Classify(const std::string& lowerName)
{
    if (ContainsAny(lowerName, {
            "roompoint",
            "bacteria_room", "bacteriaroom",
            "entityspawn_zone",
            "playercontroller",
            "playerstart",
            "flashlightvolume", "flashlight_volume",
            "exitsign",
            "spawner",
            "playeritemgrab",
            "aic_"
        }))
        return std::nullopt;

    if (ContainsAny(lowerName, {
            "wall_ceiling_items", "wall ceiling items",
            "smokedetector", "smoke_detector",
            "plugsocket", "plug_socket",
            "stepladder", "step_ladder"
        }))
        return std::nullopt;

    if (ContainsAny(lowerName, {
            "transition",
            "teleport",
            "nextlevel",
            "level_exit",
            "exitportal",
            "elevator_exit",
            "elevatortransition",
            "exit", 
            "elevator"
        }))
        return Category::Exit;

    if (ContainsAny(lowerName, {
            "bacteria", // 细菌吧
            "monster",
            "wretch", // 
            "faceling", // 
            "smiler", // 笑魇
            "hound", // 大狗大狗
            "killerclown", "killer_clown", "killer clown", // 小丑
            "moth", "cave_moth", "cavemoth", // 扑腾蛾子
            "skinstealer", "skin_stealer", // 窃皮
            "partygoer", // 派对客
            //"balloon", // 气球
            "npc_", //
            "membri", //
            "entity" // 通用匹配 有些区带这个
        }))
        return Category::Monster;

    if (ContainsAny(lowerName, {
            "bp_dropped", "droppeditem",
            "almondwater", "almond_water", // 杏仁水
            "flashlight", // 手电筒
            "battery",
            "cassette", // 录音带
            "tape", //
            "document",
            "pushable",
            "keyitem",
            "bp_key", "key_", "_key",
            "note", // 你妈你纯来混的纸条一点不看 不看你玩级吧
            "energybar", "energy_bar",
            "radio",
            "collectible",
            "inventoryitem"
        }))
        return Category::Item;

    if (ContainsAny(lowerName, {
            "bpcharacter_demo",
            "playercharacter", "player_character",
            "mp_player",
            "character_demo",
            "survivor",
            "player",
            "character"
        }))
        return Category::Player;

    return std::nullopt;
}

void RemoveCaseInsensitivePrefix(std::string& value, const char* prefix)
{
    const std::string lower = ToLower(value);
    const std::string prefixLower = ToLower(prefix);
    if (lower.starts_with(prefixLower))
        value.erase(0, prefixLower.size());
}

std::string FriendlyName(std::string raw, Category category)
{
    if (const std::size_t slash = raw.find_last_of("/."); slash != std::string::npos)
        raw.erase(0, slash + 1);

    while (!raw.empty() && std::isdigit(static_cast<unsigned char>(raw.back())) != 0)
        raw.pop_back();
    while (!raw.empty() && raw.back() == '_')
        raw.pop_back();

    if (raw.size() >= 2 && ToLower(raw.substr(raw.size() - 2)) == "_c")
        raw.resize(raw.size() - 2);

    //WalkieTalkie2_C_17
    while (!raw.empty() && std::isdigit(static_cast<unsigned char>(raw.back())) != 0)
        raw.pop_back();
    while (!raw.empty() && raw.back() == '_')
        raw.pop_back();

    RemoveCaseInsensitivePrefix(raw, "BP_DroppedItem_");
    RemoveCaseInsensitivePrefix(raw, "BPCharacter_Demo_");
    RemoveCaseInsensitivePrefix(raw, "BP_");
    RemoveCaseInsensitivePrefix(raw, "NPC_");

    std::replace(raw.begin(), raw.end(), '_', ' ');
    if (raw.empty())
    {
        switch (category)
        {
        case Category::Monster: return "Unknown threat";
        case Category::Player:  return "Player";
        case Category::Item:    return "Item";
        case Category::Exit:    return "Exit";
        }
    }

    constexpr std::size_t maxLength = 42;
    if (raw.size() > maxLength)
        raw = raw.substr(0, maxLength - 3) + "...";
    return raw;
}

bool ResolveFrameContext(FrameContext& context)
{
    const uintptr_t base = SDK::InSDKUtils::GetImageBase();
    auto** worldAddress = reinterpret_cast<SDK::UWorld**>(base + SDK::Offsets::GWorld);
    if (!IsReadable(worldAddress, sizeof(*worldAddress)))
        return false;

    SDK::UWorld* world = *worldAddress;
    if (!game::IsLiveUObject(world) || !IsReadable(world, 0x188))
        return false;

    SDK::UGameInstance* gameInstance = world->OwningGameInstance;
    if (!game::IsLiveUObject(gameInstance) || !IsReadable(gameInstance, 0x48))
        return false;

    const auto localPlayers = gameInstance->LocalPlayers;
    if (!IsSaneArray(localPlayers, 8) || localPlayers.Num() == 0)
        return false;

    SDK::ULocalPlayer* localPlayer = localPlayers.GetDataPtr()[0];
    if (!game::IsLiveUObject(localPlayer) || !IsReadable(localPlayer, 0x38))
        return false;

    SDK::APlayerController* controller = localPlayer->PlayerController;
    if (!game::IsLiveUObject(controller) || !IsReadable(controller, 0x2C0))
        return false;

    SDK::APlayerCameraManager* cameraManager = controller->PlayerCameraManager;
    if (!game::IsLiveUObject(cameraManager) || !IsReadable(cameraManager, 0x1B00))
        return false;

    context.world = world;
    context.localPawn = controller->AcknowledgedPawn;
    context.localController = controller;
    context.cameraManager = cameraManager;
    return true;
}

bool ReadCamera(const FrameContext& context, CameraState& camera)
{
    if (!IsReadable(context.cameraManager, 0x1AE0 + sizeof(SDK::FCameraCacheEntry)))
        return false;

    const SDK::FMinimalViewInfo& pov = context.cameraManager->CameraCachePrivate.POV;
    if (!std::isfinite(pov.FOV) || pov.FOV < 20.0f || pov.FOV > 179.0f)
        return false;

    camera.location = pov.Location;
    camera.rotation = pov.Rotation;
    camera.fov = pov.FOV;
    return std::isfinite(camera.location.X) && std::isfinite(camera.location.Y) &&
           std::isfinite(camera.location.Z);
}

float DistanceMeters(const SDK::FVector& first, const SDK::FVector& second)
{
    const float x = first.X - second.X;
    const float y = first.Y - second.Y;
    const float z = first.Z - second.Z;
    return std::sqrt(x * x + y * y + z * z) * 0.01f;
}

bool WorldToScreen(const SDK::FVector& world, const CameraState& camera, const ImVec2& display,
                   ImVec2& screen)
{
    constexpr float degreesToRadians = 3.14159265358979323846f / 180.0f;
    const float pitch = camera.rotation.Pitch * degreesToRadians;
    const float yaw = camera.rotation.Yaw * degreesToRadians;
    const float roll = camera.rotation.Roll * degreesToRadians;

    const float sp = std::sin(pitch), cp = std::cos(pitch);
    const float sy = std::sin(yaw), cy = std::cos(yaw);
    const float sr = std::sin(roll), cr = std::cos(roll);

    const SDK::FVector forward(cp * cy, cp * sy, sp);
    const SDK::FVector right(sr * sp * cy - cr * sy, sr * sp * sy + cr * cy, -sr * cp);
    const SDK::FVector up(-(cr * sp * cy + sr * sy), cy * sr - cr * sp * sy, cr * cp);
    const SDK::FVector delta(world.X - camera.location.X, world.Y - camera.location.Y,
                             world.Z - camera.location.Z);

    const float transformedX = delta.X * right.X + delta.Y * right.Y + delta.Z * right.Z;
    const float transformedY = delta.X * up.X + delta.Y * up.Y + delta.Z * up.Z;
    const float transformedZ = delta.X * forward.X + delta.Y * forward.Y + delta.Z * forward.Z;
    if (transformedZ <= 1.0f)
        return false;

    const float focal = (display.x * 0.5f) /
                        std::tan(std::clamp(camera.fov, 20.0f, 179.0f) * degreesToRadians * 0.5f);
    screen.x = display.x * 0.5f + transformedX * focal / transformedZ;
    screen.y = display.y * 0.5f - transformedY * focal / transformedZ;
    return std::isfinite(screen.x) && std::isfinite(screen.y) &&
           screen.x > -display.x * 0.35f && screen.x < display.x * 1.35f &&
           screen.y > -display.y * 0.35f && screen.y < display.y * 1.35f;
}

bool IsOwnedByLocalActor(SDK::AActor* actor, const FrameContext& context)
{
    SDK::AActor* owner = actor;
    for (int depth = 0; depth < 6 && IsReadable(owner, 0xE8); ++depth)
    {
        owner = owner->Owner;
        if (owner == nullptr)
            return false;
        if (owner == context.localPawn || owner == context.localController ||
            owner == context.cameraManager)
            return true;
    }
    return false;
}

bool IsDynamicHelperClass(const std::string& lowerClassName)
{
    return ContainsAny(lowerClassName,
    {
        "playercontroller", "player_controller", "controller", "playerstate", "player_state",
        "cameramanager", "camera_manager", "gamemode", "game_mode", "gamestate", "game_state",
        "worldsettings", "levelscript", "spectator", "springarm", "hud", "debugcamera"
    });
}

bool IsDerivedFromClass(const SDK::UObject* object, const char* expected)
{
    if (!game::IsLiveUObject(object))
        return false;
    for (SDK::UStruct* type = object->Class; type != nullptr; type = type->SuperStruct)
    {
        if (!game::IsLiveUObject(type))
            return false;
        std::string name;
        if (!game::TryFNameToString(type->Name, name))
            return false;
        if (name == expected)
            return true;
    }
    return false;
}

std::string ResolvePlayerInfo(SDK::AActor* actor, const std::string& fallback,
                              float& sanity, float& maxSanity)
{
    sanity = -1.0f;
    maxSanity = -1.0f;
    if (!game::IsLiveUObject(actor) || !IsReadable(actor, sizeof(SDK::APawn)))
        return fallback;
    auto* pawn = reinterpret_cast<SDK::APawn*>(actor);
    SDK::APlayerState* playerState = pawn->PlayerState;
    if (!game::IsLiveUObject(playerState) ||
        !IsReadable(playerState, sizeof(SDK::APlayerState)))
        return fallback;

    if (IsDerivedFromClass(playerState, "FancyPlayerState") &&
        IsReadable(playerState, sizeof(SDK::AFancyPlayerState)))
    {
        auto* fancyState = reinterpret_cast<SDK::AFancyPlayerState*>(playerState);
        if (std::isfinite(fancyState->Sanity) && std::isfinite(fancyState->MaxSanity) &&
            fancyState->MaxSanity > 0.0f && fancyState->MaxSanity <= 100000.0f)
        {
            sanity = std::clamp(fancyState->Sanity, 0.0f, fancyState->MaxSanity);
            maxSanity = fancyState->MaxSanity;
        }
    }

    std::string playerName;
    if (!game::TryFStringToString(playerState->PlayerNamePrivate, playerName) || playerName.empty())
        return fallback;
    if (playerName.size() > 48)
        playerName = playerName.substr(0, 45) + "...";
    return playerName;
}

void ResolveLocalSanity(const FrameContext& context, float& sanity, float& maxSanity)
{
    sanity = -1.0f;
    maxSanity = -1.0f;
    if (!game::IsLiveUObject(context.localController))
        return;

    SDK::APlayerState* playerState = context.localController->PlayerState;
    if (!game::IsLiveUObject(playerState) ||
        !IsReadable(playerState, sizeof(SDK::AFancyPlayerState)))
        return;

    auto* fancyState = reinterpret_cast<SDK::AFancyPlayerState*>(playerState);
    if (!std::isfinite(fancyState->Sanity) || !std::isfinite(fancyState->MaxSanity) ||
        fancyState->MaxSanity <= 0.0f || fancyState->MaxSanity > 100000.0f)
        return;

    sanity = std::clamp(fancyState->Sanity, 0.0f, fancyState->MaxSanity);
    maxSanity = fancyState->MaxSanity;
}

std::uint64_t BuildSceneSignature(const FrameContext& context)
{
    std::uint64_t signature = static_cast<std::uint64_t>(
        reinterpret_cast<std::uintptr_t>(context.world));
    const auto mix = [&signature](const std::uintptr_t value)
    {
        signature ^= static_cast<std::uint64_t>(value) + 0x9E3779B97F4A7C15ull +
                     (signature << 6u) + (signature >> 2u);
    };

    mix(reinterpret_cast<std::uintptr_t>(context.world->PersistentLevel));
    const auto levels = context.world->Levels;
    if (IsSaneArray(levels, 512))
    {
        mix(static_cast<std::uintptr_t>(levels.Num()));
        for (int index = 0; index < levels.Num(); ++index)
            mix(reinterpret_cast<std::uintptr_t>(levels.GetDataPtr()[index]));
    }
    return signature != 0 ? signature : 1;
}

class EntityCache final
{
public:
    void Reset()
    {
        entities_.clear();
        world_ = nullptr;
        generation_ = 0;
        nextRefresh_ = Clock::time_point{};
        nextMotionSample_ = Clock::time_point{};
        scannedActors_ = 0;
        baselineComplete_ = false;
    }

    bool RefreshIfNeeded(const FrameContext& context, int intervalMs)
    {
        const auto now = Clock::now();
        if (context.world != world_)
        {
            Reset();
            world_ = context.world;
            core::Logf("ESP attached to GWorld=%p", world_);
        }

        if (now < nextRefresh_)
            return false;

        nextRefresh_ = now + std::chrono::milliseconds(std::clamp(intervalMs, 100, 2000));
        ++generation_;
        scannedActors_ = 0;

        const auto levels = context.world->Levels;
        if (!IsSaneArray(levels, 512))
            return true;

        std::unordered_set<SDK::AActor*> seen;
        seen.reserve(2048);

        for (int levelIndex = 0; levelIndex < levels.Num(); ++levelIndex)
        {
            SDK::ULevel* level = levels.GetDataPtr()[levelIndex];
            if (!IsReadable(level, 0xA8))
                continue;

            const auto actors = level->Actors;
            if (!IsSaneArray(actors, 100000))
                continue;

            scannedActors_ += static_cast<std::size_t>(actors.Num());
            for (int actorIndex = 0; actorIndex < actors.Num(); ++actorIndex)
            {
                SDK::AActor* actor = actors.GetDataPtr()[actorIndex];
                if (actor == nullptr || actor == context.localPawn || actor == context.localController ||
                    !seen.emplace(actor).second || !IsReadable(actor, 0x138))
                    continue;

                SDK::USceneComponent* root = actor->RootComponent;
                if (!IsReadable(root, 0x128))
                    continue;

                const SDK::FName actorFName = actor->Name;
                auto existing = entities_.find(actor);
                if (existing != entities_.end() && existing->second.comparisonIndex == actorFName.ComparisonIndex &&
                    existing->second.number == actorFName.Number)
                {
                    if (IsExcludedActorClass(existing->second.lowerActorName,
                                             existing->second.lowerClassName))
                    {
                        entities_.erase(existing);
                        continue;
                    }
                    existing->second.root = root;
                    if (existing->second.categorized &&
                        existing->second.category == Category::Player)
                        existing->second.name = ResolvePlayerInfo(
                            actor, existing->second.name,
                            existing->second.sanity, existing->second.maxSanity);
                    existing->second.dynamicEligible = !existing->second.categorized &&
                        !existing->second.lever &&
                        !existing->second.valve &&
                        !IsDynamicHelperClass(existing->second.lowerClassName) &&
                        !IsDynamicHelperClass(existing->second.lowerActorName) &&
                        !IsOwnedByLocalActor(actor, context);
                    existing->second.generation = generation_;
                    continue;
                }

                std::string rawName;
                std::string className;
                try
                {
                    if (!game::TryFNameToString(actorFName, rawName) ||
                        !IsReadable(actor->Class, sizeof(SDK::UClass)))
                        continue;
                    if (!game::TryFNameToString(actor->Class->Name, className))
                        continue;
                }
                catch (...)
                {
                    continue;
                }

                const std::string lowerActorName = ToLower(rawName);
                const std::string lowerClassName = ToLower(className);
                if (IsExcludedActorClass(lowerActorName, lowerClassName))
                    continue;
                auto category = Classify(lowerActorName);
                if (!category.has_value())
                    category = Classify(lowerClassName);
                if (category.has_value() && *category == Category::Player &&
                    !IsDerivedFromClass(actor, "Pawn"))
                    category.reset();
                const bool isLever = lowerClassName == "bp_lever_c" ||
                                     lowerClassName.find("bp_lever") != std::string::npos;
                const bool isValve = lowerClassName == "bp_dark_valve_c" ||
                                     lowerClassName.find("dark_valve") != std::string::npos;
                std::string displayName = category.has_value()
                    ? FriendlyName(rawName, *category) : std::string{};
                float sanity = -1.0f;
                float maxSanity = -1.0f;
                if (category.has_value() && *category == Category::Player)
                    displayName = ResolvePlayerInfo(actor, displayName, sanity, maxSanity);
                const bool dynamicEligible = !category.has_value() && !isLever && !isValve &&
                                             !IsDynamicHelperClass(lowerClassName) &&
                                             !IsDynamicHelperClass(lowerActorName) &&
                                             !IsOwnedByLocalActor(actor, context);
                // new spawn
                const bool meaningfulSpawn = category.has_value() || isLever || isValve;
                const Clock::time_point spawnedUntil = baselineComplete_ && meaningfulSpawn
                    ? now + std::chrono::seconds(10) : Clock::time_point{};
                bool suppressDiscovery = false;
                if (baselineComplete_)
                {
                    const float exclusionRadius = std::clamp(
                        Notifications::Instance().Settings().discoveryExclusionRadiusMeters,
                        0.0f, 100.0f);
                    SDK::USceneComponent* localRoot = context.localPawn != nullptr
                        ? context.localPawn->RootComponent : nullptr;
                    if (exclusionRadius > 0.0f && IsReadable(localRoot, 0x128))
                    {
                        const SDK::FVector localLocation = localRoot->RelativeLocation;
                        const SDK::FVector actorLocation = root->RelativeLocation;
                        if (std::isfinite(localLocation.X) && std::isfinite(localLocation.Y) &&
                            std::isfinite(localLocation.Z) && std::isfinite(actorLocation.X) &&
                            std::isfinite(actorLocation.Y) && std::isfinite(actorLocation.Z))
                        {
                            suppressDiscovery = DistanceMeters(localLocation, actorLocation) <=
                                                exclusionRadius;
                        }
                    }
                }

                entities_[actor] = CachedEntity{
                    actor,
                    root,
                    std::move(displayName),
                    lowerActorName,
                    std::move(className),
                    lowerClassName,
                    category.value_or(Category::Item),
                    category.has_value(),
                    isLever,
                    isValve,
                    sanity,
                    maxSanity,
                    dynamicEligible,
                    actorFName.ComparisonIndex,
                    actorFName.Number,
                    generation_,
                    spawnedUntil,
                    suppressDiscovery
                };
            }
        }

        std::erase_if(entities_, [this](const auto& pair)
        {
            return pair.second.generation != generation_;
        });
        baselineComplete_ = true;
        return true;
    }

    const std::unordered_map<SDK::AActor*, CachedEntity>& Entities() const noexcept { return entities_; }
    std::size_t ScannedActors() const noexcept { return scannedActors_; }

    void UpdateMotion(const EspSettings& settings)
    {
        if (!settings.dynamicMarkersEnabled && !settings.entitySpeed)
            return;
        const auto now = Clock::now();
        if (now < nextMotionSample_)
            return;
        nextMotionSample_ = now + std::chrono::milliseconds(100);

        const float minimumSpeed = std::clamp(settings.dynamicMinimumSpeed, 0.02f, 10.0f);
        const float detectionWindow = std::clamp(settings.dynamicDetectionWindowSeconds, 0.3f, 10.0f);
        const auto linger = std::chrono::duration_cast<Clock::duration>(
            std::chrono::duration<float>(std::clamp(settings.dynamicLingerSeconds, 1.0f, 30.0f)));

        for (auto& [actor, entity] : entities_)
        {
            (void)actor;
            const bool speedEligible = settings.entitySpeed && entity.categorized &&
                (entity.category == Category::Monster || entity.category == Category::Player);
            const bool dynamicEligible = settings.dynamicMarkersEnabled && entity.dynamicEligible;
            if ((!speedEligible && !dynamicEligible) || entity.root == nullptr)
                continue;

            const SDK::FVector location = entity.root->RelativeLocation;
            if (!std::isfinite(location.X) || !std::isfinite(location.Y) || !std::isfinite(location.Z))
            {
                entity.motionHistoryValid = false;
                continue;
            }

            if (!entity.motionHistoryValid)
            {
                entity.lastMotionLocation = location;
                entity.lastMotionSample = now;
                entity.motionHistoryValid = true;
                continue;
            }

            const float elapsed = std::chrono::duration<float>(now - entity.lastMotionSample).count();
            if (elapsed <= 0.001f)
                continue;

            const float distance = DistanceMeters(location, entity.lastMotionLocation);
            const float instantaneousSpeed = distance / elapsed;
            entity.speedMetersPerSecond = entity.speedMetersPerSecond * 0.35f + instantaneousSpeed * 0.65f;

            if (dynamicEligible && instantaneousSpeed >= minimumSpeed)
            {
                if (entity.movementEventCount < 3)
                {
                    entity.movementEvents[entity.movementEventCount++] = now;
                }
                else
                {
                    entity.movementEvents[0] = entity.movementEvents[1];
                    entity.movementEvents[1] = entity.movementEvents[2];
                    entity.movementEvents[2] = now;
                }

                if (entity.movementEventCount == 3 &&
                    std::chrono::duration<float>(now - entity.movementEvents[0]).count() <= detectionWindow)
                {
                    entity.movingUntil = now + linger;
                }
            }
            else if (entity.speedMetersPerSecond < minimumSpeed * 0.35f)
            {
                entity.speedMetersPerSecond = 0.0f;
            }

            entity.lastMotionLocation = location;
            entity.lastMotionSample = now;
        }
    }

    bool IsDynamic(const CachedEntity& entity, const EspSettings& settings) const
    {
        if (!settings.dynamicMarkersEnabled || !entity.dynamicEligible)
            return false;
        if (Clock::now() <= entity.movingUntil)
            return true;
        return false;
    }

    bool IsSpawned(const CachedEntity& entity, const EspSettings& settings) const
    {
        return settings.spawnMarkersEnabled && entity.spawnedUntil.time_since_epoch().count() != 0 &&
               Clock::now() <= entity.spawnedUntil;
    }

private:
    std::unordered_map<SDK::AActor*, CachedEntity> entities_;
    SDK::UWorld* world_ = nullptr;
    uint64_t generation_ = 0;
    Clock::time_point nextRefresh_{};
    Clock::time_point nextMotionSample_{};
    std::size_t scannedActors_ = 0;
    bool baselineComplete_ = false;
};

EntityCache g_cache;

bool CategoryEnabled(Category category, const EspSettings& settings)
{
    switch (category)
    {
    case Category::Monster: return settings.monsters;
    case Category::Player:  return settings.players;
    case Category::Item:    return settings.items;
    case Category::Exit:    return settings.exits;
    default:                return false;
    }
}

uint64_t LabelCellAt(const int32_t column, const int32_t row)
{
    return (static_cast<uint64_t>(static_cast<uint32_t>(column)) << 32u) |
           static_cast<uint32_t>(row);
}

int ReserveLabelSlot(std::unordered_map<uint64_t, int>& occupied, const ImVec2& anchor,
                     const float cellWidth, const float cellHeight)
{
    const int32_t column = static_cast<int32_t>(std::floor(anchor.x / cellWidth));
    const int32_t row = static_cast<int32_t>(std::floor(anchor.y / cellHeight));

    int slot = 0;
    for (int32_t y = -1; y <= 1; ++y)
    {
        for (int32_t x = -1; x <= 1; ++x)
        {
            if (const auto found = occupied.find(LabelCellAt(column + x, row + y));
                found != occupied.end())
            {
                slot = std::max(slot, found->second);
            }
        }
    }

    const int nextSlot = slot + 1;
    occupied[LabelCellAt(column, row)] = nextSlot;
    return slot;
}

bool MergeNearbyItemVisual(std::vector<render::EspVisual>& visuals,
                           render::EspVisual candidate,
                           const float maximumScreenSeparation,
                           const float maximumDistanceSeparationMeters)
{
    const auto normalizedName = [](const std::string& name)
    {
        std::string normalized;
        normalized.reserve(name.size());
        bool inNumber = false;
        for (const unsigned char character : name)
        {
            if (std::isdigit(character) != 0)
            {
                if (!inNumber)
                    normalized.push_back('#');
                inNumber = true;
                continue;
            }
            inNumber = false;
            normalized.push_back(static_cast<char>(std::tolower(character)));
        }
        return normalized;
    };
    const std::string candidateKey = normalizedName(candidate.name);
    const float candidateX = (candidate.head.x + candidate.foot.x) * 0.5f;
    const float candidateY = (candidate.head.y + candidate.foot.y) * 0.5f;

    for (auto iterator = visuals.rbegin(); iterator != visuals.rend(); ++iterator)
    {
        render::EspVisual& existing = *iterator;
        if (existing.category != render::EspCategory::Item ||
            normalizedName(existing.name) != candidateKey ||
            std::abs(existing.distanceMeters - candidate.distanceMeters) >
                maximumDistanceSeparationMeters)
            continue;

        const float existingX = (existing.head.x + existing.foot.x) * 0.5f;
        const float existingY = (existing.head.y + existing.foot.y) * 0.5f;
        const float deltaX = existingX - candidateX;
        const float deltaY = existingY - candidateY;
        if (deltaX * deltaX + deltaY * deltaY >
            maximumScreenSeparation * maximumScreenSeparation)
            continue;

        const int combinedCount = existing.stackCount + 1;
        if (candidate.distanceMeters < existing.distanceMeters)
        {
            candidate.stackCount = combinedCount;
            existing = std::move(candidate);
        }
        else
        {
            existing.stackCount = combinedCount;
        }
        return true;
    }
    return false;
}
}

Esp& Esp::Instance()
{
    static Esp instance;
    return instance;
}

void Esp::UpdateAndDraw()
{
    stats_.drawnEntities = 0;
    stats_.worldReady = false;

    const NotificationSettings& notificationSettings = Notifications::Instance().Settings();
    const bool backgroundMonitoring = notificationSettings.enabled &&
        (notificationSettings.sceneSummary || notificationSettings.sanityWarnings ||
         notificationSettings.discoveryAlerts);
    if ((!settings_.enabled && !settings_.classNamesEnabled && !settings_.leverHighlight &&
         !settings_.valveHighlight &&
         !settings_.dynamicMarkersEnabled && !backgroundMonitoring) ||
        ImGui::GetCurrentContext() == nullptr)
        return;

    FrameContext context{};
    CameraState camera{};
    if (!ResolveFrameContext(context) || !ReadCamera(context, camera))
    {
        stats_.cachedEntities = g_cache.Entities().size();
        return;
    }

    stats_.worldReady = true;
    const bool cacheRefreshed = g_cache.RefreshIfNeeded(context, settings_.refreshIntervalMs);
    g_cache.UpdateMotion(settings_);
    stats_.scannedActors = g_cache.ScannedActors();
    stats_.cachedEntities = g_cache.Entities().size();
    ResolveLocalSanity(context, stats_.localSanity, stats_.localMaxSanity);

    if (cacheRefreshed)
    {
        stats_.sceneSignature = BuildSceneSignature(context);
        ++stats_.scanRevision;
        stats_.monsters = 0;
        stats_.players = 0;
        stats_.items = 0;
        stats_.exits = 0;
        stats_.levers = 0;
        stats_.valves = 0;
        stats_.dynamicEntities = 0;
        stats_.alertMonsters = 0;
        stats_.alertPlayers = 0;
        stats_.alertItems = 0;
        stats_.alertExits = 0;
        stats_.alertObjectives = 0;
        stats_.alertDynamicEntities = 0;
        for (const auto& [actor, entity] : g_cache.Entities())
        {
            (void)actor;
            if (entity.categorized)
            {
                switch (entity.category)
                {
                case Category::Monster: ++stats_.monsters; break;
                case Category::Player:  ++stats_.players; break;
                case Category::Item:    ++stats_.items; break;
                case Category::Exit:    ++stats_.exits; break;
                default: break;
                }
            }
            if (entity.lever)
                ++stats_.levers;
            if (entity.valve)
                ++stats_.valves;
            if (g_cache.IsDynamic(entity, settings_))
                ++stats_.dynamicEntities;

            if (!entity.suppressDiscovery)
            {
                if (entity.categorized)
                {
                    switch (entity.category)
                    {
                    case Category::Monster: ++stats_.alertMonsters; break;
                    case Category::Player:  ++stats_.alertPlayers; break;
                    case Category::Item:    ++stats_.alertItems; break;
                    case Category::Exit:    ++stats_.alertExits; break;
                    default: break;
                    }
                }
                if (entity.lever || entity.valve)
                    ++stats_.alertObjectives;
                if (g_cache.IsDynamic(entity, settings_))
                    ++stats_.alertDynamicEntities;
            }
        }
    }

    const ImVec2 display = ImGui::GetIO().DisplaySize;
    if (display.x < 64.0f || display.y < 64.0f)
        return;

    std::vector<render::EspVisual> visuals;
    visuals.reserve(g_cache.Entities().size());

    for (const auto& [actor, entity] : g_cache.Entities())
    {
        (void)actor;
        const bool categorizedVisible = settings_.enabled && entity.categorized &&
            CategoryEnabled(entity.category, settings_);
        const bool leverVisible = entity.lever && settings_.leverHighlight;
        const bool valveVisible = entity.valve && settings_.valveHighlight;
        const bool dynamic = g_cache.IsDynamic(entity, settings_);
        const bool spawned = g_cache.IsSpawned(entity, settings_);
        if (!categorizedVisible && !leverVisible && !valveVisible && !dynamic && !spawned)
            continue;

        if (!IsReadable(entity.root, 0x128))
            continue;

        const SDK::FVector location = entity.root->RelativeLocation;
        if (!std::isfinite(location.X) || !std::isfinite(location.Y) || !std::isfinite(location.Z))
            continue;

        const float distance = DistanceMeters(location, camera.location);
        if (distance < 0.25f)
            continue;

        if (spawned)
        {
            const render::EspCategory spawnVisualCategory = entity.categorized
                ? entity.category
                : (entity.lever ? render::EspCategory::Lever
                                : (entity.valve ? render::EspCategory::Valve
                                                : render::EspCategory::Spawn));
            const bool compactSpawn = spawnVisualCategory == render::EspCategory::Item ||
                                      spawnVisualCategory == render::EspCategory::Exit ||
                                      spawnVisualCategory == render::EspCategory::Lever ||
                                      spawnVisualCategory == render::EspCategory::Valve;
            SDK::FVector head = location;
            SDK::FVector foot = location;
            const float halfHeight = compactSpawn ? 32.0f : 88.0f;
            head.Z += halfHeight;
            foot.Z -= halfHeight;
            ImVec2 headScreen{}, footScreen{};
            if (WorldToScreen(head, camera, display, headScreen) &&
                WorldToScreen(foot, camera, display, footScreen))
            {
                render::EspVisual visual{headScreen, footScreen, entity.className,
                                         distance, spawnVisualCategory, compactSpawn};
                visual.newlySpawned = true;
                visuals.push_back(std::move(visual));
                if (settings_.entitySpeed && entity.categorized &&
                    (entity.category == Category::Monster || entity.category == Category::Player) &&
                    entity.speedMetersPerSecond >= 1.0f)
                    visuals.back().speedMetersPerSecond = entity.speedMetersPerSecond;
            }
            continue;
        }

        if (categorizedVisible &&
            distance <= settings_.maxDistanceMeters)
        {
            const bool compact = entity.category == Category::Item || entity.category == Category::Exit;
            const float halfHeight = compact ? 18.0f : 88.0f;
            SDK::FVector head = location;
            SDK::FVector foot = location;
            head.Z += halfHeight;
            foot.Z -= halfHeight;

            ImVec2 headScreen{}, footScreen{};
            if (WorldToScreen(head, camera, display, headScreen) &&
                WorldToScreen(foot, camera, display, footScreen))
            {
                render::EspVisual visual{headScreen, footScreen, entity.name, distance,
                                         entity.category, compact};
                if (entity.category == Category::Player)
                {
                    visual.sanity = entity.sanity;
                    visual.maxSanity = entity.maxSanity;
                }
                if (settings_.entitySpeed &&
                    (entity.category == Category::Monster || entity.category == Category::Player) &&
                    entity.speedMetersPerSecond >= 1.0f)
                    visual.speedMetersPerSecond = entity.speedMetersPerSecond;
                if (entity.category != Category::Item ||
                    !MergeNearbyItemVisual(visuals, render::EspVisual(visual),
                                           settings_.itemMergeScreenDistancePixels,
                                           settings_.itemMergeDepthMeters))
                    visuals.push_back(std::move(visual));
            }
        }

        if (entity.lever && settings_.leverHighlight && distance <= settings_.leverMaxDistanceMeters)
        {
            SDK::FVector head = location;
            SDK::FVector foot = location;
            head.Z += 36.0f;
            foot.Z -= 36.0f;

            ImVec2 headScreen{}, footScreen{};
            if (WorldToScreen(head, camera, display, headScreen) &&
                WorldToScreen(foot, camera, display, footScreen))
            {
                visuals.push_back(render::EspVisual{headScreen, footScreen, entity.className, distance,
                                                    render::EspCategory::Lever, true});
            }
        }

        if (entity.valve && settings_.valveHighlight && distance <= settings_.valveMaxDistanceMeters)
        {
            SDK::FVector head = location;
            SDK::FVector foot = location;
            head.Z += 32.0f;
            foot.Z -= 32.0f;

            ImVec2 headScreen{}, footScreen{};
            if (WorldToScreen(head, camera, display, headScreen) &&
                WorldToScreen(foot, camera, display, footScreen))
            {
                visuals.push_back(render::EspVisual{headScreen, footScreen, entity.className, distance,
                                                    render::EspCategory::Valve, true});
            }
        }

        else if (!entity.lever && dynamic && distance <= settings_.dynamicMaxDistanceMeters)
        {
            SDK::FVector head = location;
            SDK::FVector foot = location;
            head.Z += 40.0f;
            foot.Z -= 40.0f;

            ImVec2 headScreen{}, footScreen{};
            if (WorldToScreen(head, camera, display, headScreen) &&
                WorldToScreen(foot, camera, display, footScreen))
            {
                std::string markerName = entity.className;
                if (entity.speedMetersPerSecond > 0.01f)
                {
                    char speed[32]{};
                    std::snprintf(speed, sizeof(speed), " [%.1f m/s]", entity.speedMetersPerSecond);
                    markerName += speed;
                }
                visuals.push_back(render::EspVisual{headScreen, footScreen, std::move(markerName), distance,
                                                    render::EspCategory::Dynamic, true});
            }
        }
    }

    std::vector<ActivatedExitMarker> activatedExits;
    ExitActivator::Instance().SnapshotMarkers(activatedExits);
    for (const ActivatedExitMarker& marker : activatedExits)
    {
        const SDK::FVector location(marker.x, marker.y, marker.z);
        const float distance = DistanceMeters(location, camera.location);
        SDK::FVector head = location;
        SDK::FVector foot = location;
        head.Z += 82.0f;
        foot.Z -= 82.0f;
        ImVec2 headScreen{};
        ImVec2 footScreen{};
        if (!WorldToScreen(head, camera, display, headScreen) ||
            !WorldToScreen(foot, camera, display, footScreen))
            continue;

        const float markerX = (headScreen.x + footScreen.x) * 0.5f;
        const float markerY = (headScreen.y + footScreen.y) * 0.5f;
        std::erase_if(visuals, [markerX, markerY](const render::EspVisual& visual)
        {
            if (visual.category != render::EspCategory::Exit)
                return false;
            const float visualX = (visual.head.x + visual.foot.x) * 0.5f;
            const float visualY = (visual.head.y + visual.foot.y) * 0.5f;
            const float x = visualX - markerX;
            const float y = visualY - markerY;
            return x * x + y * y <= 40.0f * 40.0f;
        });
        visuals.push_back(render::EspVisual{headScreen, footScreen, marker.name, distance,
                                            render::EspCategory::ForcedExit, false});
    }

    std::sort(visuals.begin(), visuals.end(), [](const render::EspVisual& left, const render::EspVisual& right)
    {
        return left.distanceMeters > right.distanceMeters;
    });

    render::EspDrawStyle drawStyle{};
    drawStyle.boxes = settings_.boxes;
    drawStyle.filledBoxes = settings_.filledBoxes;
    drawStyle.labels = settings_.labels;
    drawStyle.distance = settings_.distance;
    drawStyle.tracers = settings_.tracers;
    drawStyle.monsterTracersOnly = settings_.monsterTracersOnly;
    drawStyle.fillOpacity = settings_.fillOpacity;
    drawStyle.cardScale = settings_.cardScale;

    std::unordered_map<uint64_t, int> labelStacks;
    labelStacks.reserve(visuals.size());
    ImDrawList* drawList = ImGui::GetBackgroundDrawList();
    for (const render::EspVisual& visual : visuals)
    {
        int stackIndex = 0;
        const bool forcedHighlightLabel = visual.newlySpawned ||
                                          visual.category == render::EspCategory::Lever ||
                                          visual.category == render::EspCategory::Valve ||
                                          visual.category == render::EspCategory::Dynamic ||
                                          visual.category == render::EspCategory::Spawn ||
                                          visual.category == render::EspCategory::ForcedExit;
        const bool playerNameTag = visual.category == render::EspCategory::Player &&
                                   settings_.playerNameTags;
        if (settings_.labels || forcedHighlightLabel || playerNameTag)
            stackIndex = ReserveLabelSlot(labelStacks, visual.head, 180.0f, 58.0f);
        if (forcedHighlightLabel)
        {
            render::EspDrawStyle highlightStyle{};
            highlightStyle.boxes = true;
            highlightStyle.filledBoxes = true;
            highlightStyle.labels = true;
            highlightStyle.distance = true;
            highlightStyle.tracers = !visual.newlySpawned;
            highlightStyle.monsterTracersOnly = false;
            highlightStyle.fillOpacity = (visual.category == render::EspCategory::Lever ||
                                          visual.category == render::EspCategory::Valve) ? 0.28f : 0.22f;
            highlightStyle.cardScale = std::max(settings_.cardScale,
                                                (visual.category == render::EspCategory::Lever ||
                                                 visual.category == render::EspCategory::Valve) ? 1.15f :
                                                ((visual.newlySpawned ||
                                                  visual.category == render::EspCategory::Spawn)
                                                     ? 1.25f : 1.0f));
            if (visual.newlySpawned || visual.category == render::EspCategory::Spawn)
                highlightStyle.fillOpacity = 0.36f;
            if (visual.category == render::EspCategory::ForcedExit)
            {
                highlightStyle.fillOpacity = 0.42f;
                highlightStyle.cardScale = std::max(highlightStyle.cardScale, 1.30f);
            }
            render::DrawEspVisual(drawList, visual, highlightStyle, stackIndex, display);
        }
        else
        {
            render::EspDrawStyle entityStyle = drawStyle;
            if (playerNameTag)
                entityStyle.labels = false;
            render::DrawEspVisual(drawList, visual, entityStyle, stackIndex, display);
            if (playerNameTag)
            {
                render::DrawPlayerNameTag(drawList, visual, settings_.distance,
                                          settings_.playerSanity, settings_.cardScale,
                                          stackIndex, display);
            }
        }
    }

    if (settings_.classNamesEnabled)
    {
        for (const auto& [actor, entity] : g_cache.Entities())
        {
            (void)actor;
            if ((entity.lever && settings_.leverHighlight) ||
                (entity.valve && settings_.valveHighlight) ||
                (entity.categorized && entity.category == Category::Player &&
                 settings_.playerNameTags) ||
                g_cache.IsSpawned(entity, settings_) ||
                g_cache.IsDynamic(entity, settings_) ||
                !IsReadable(entity.root, 0x128))
                continue;

            const SDK::FVector location = entity.root->RelativeLocation;
            if (!std::isfinite(location.X) || !std::isfinite(location.Y) || !std::isfinite(location.Z))
                continue;

            const float distance = DistanceMeters(location, camera.location);
            if (distance < 0.25f || distance > settings_.classNameMaxDistanceMeters)
                continue;

            ImVec2 position{};
            if (!WorldToScreen(location, camera, display, position))
                continue;

            const int stackIndex = ReserveLabelSlot(labelStacks, position, 180.0f, 58.0f);
            render::DrawClassNameLabel(drawList, position, entity.className, distance,
                                       settings_.classNameDistance, settings_.classNameScale,
                                       stackIndex);
        }
    }

    stats_.drawnEntities = visuals.size();
}

void Esp::Reset()
{
    g_cache.Reset();
    stats_ = {};
}
}

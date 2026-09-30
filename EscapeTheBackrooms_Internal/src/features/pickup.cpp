#include "features/pickup.hpp"

#include "core/logger.hpp"
#include "game/unreal_safety.hpp"

#include <SDK/BPCharacter_Demo_classes.hpp>
#include <SDK/BPCharacter_Demo_parameters.hpp>
#include <SDK/BP_DroppedItem_classes.hpp>
#include <SDK/Backrooms_classes.hpp>
#include <SDK/Backrooms_parameters.hpp>
#include <SDK/Engine_parameters.hpp>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace etb::features
{
namespace
{
using game::IsLiveUObject;
using game::IsReadable;

SDK::UWorld* ResolveWorld()
{
    const std::uintptr_t base = SDK::InSDKUtils::GetImageBase();
    auto** worldAddress = reinterpret_cast<SDK::UWorld**>(base + SDK::Offsets::GWorld);
    if (!IsReadable(worldAddress, sizeof(*worldAddress)))
        return nullptr;

    SDK::UWorld* world = *worldAddress;
    return IsReadable(world, 0x188) && IsLiveUObject(world) ? world : nullptr;
}

SDK::ABPCharacter_Demo_C* ResolveLocalCharacter(SDK::UWorld* world)
{
    if (world == nullptr)
        return nullptr;

    SDK::UGameInstance* gameInstance = world->OwningGameInstance;
    if (!IsReadable(gameInstance, 0x48) || !IsLiveUObject(gameInstance))
        return nullptr;

    const auto localPlayers = gameInstance->LocalPlayers;
    if (!localPlayers.IsValid() || localPlayers.Num() <= 0 || localPlayers.Num() > 8 ||
        !IsReadable(localPlayers.GetDataPtr(), sizeof(SDK::ULocalPlayer*) * localPlayers.Num()))
        return nullptr;

    SDK::ULocalPlayer* localPlayer = localPlayers.GetDataPtr()[0];
    if (!IsReadable(localPlayer, 0x38) || !IsLiveUObject(localPlayer))
        return nullptr;

    SDK::APlayerController* controller = localPlayer->PlayerController;
    if (!IsReadable(controller, 0x2A8) || !IsLiveUObject(controller))
        return nullptr;

    SDK::APawn* pawn = controller->AcknowledgedPawn;
    if (!IsReadable(pawn, sizeof(SDK::ABPCharacter_Demo_C)) || !IsLiveUObject(pawn))
        return nullptr;
    return reinterpret_cast<SDK::ABPCharacter_Demo_C*>(pawn);
}

bool IsClassDerivedFrom(const SDK::UObject* object, const char* className)
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
            if (name == className)
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
            if (!IsLiveUObject(type))
                return nullptr;

            for (SDK::UField* field = type->Children; field != nullptr; field = field->Next)
            {
                if (!IsLiveUObject(field))
                    return nullptr;
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
    }
    return nullptr;
}

bool ReadActorLocation(const SDK::AActor* actor, SDK::FVector& location)
{
    if (!IsLiveUObject(actor) || !IsReadable(actor, sizeof(SDK::AActor)))
        return false;

    SDK::USceneComponent* root = actor->RootComponent;
    if (!IsReadable(root, 0x128) || !IsLiveUObject(root))
        return false;

    location = root->RelativeLocation;
    return std::isfinite(location.X) && std::isfinite(location.Y) && std::isfinite(location.Z);
}

float DistanceSquared(const SDK::FVector& left, const SDK::FVector& right)
{
    const float x = left.X - right.X;
    const float y = left.Y - right.Y;
    const float z = left.Z - right.Z;
    return x * x + y * y + z * z;
}

bool RequestServerPickup(SDK::ABPCharacter_Demo_C* character, SDK::ADroppedItem* item)
{
    static SDK::UClass* cachedClass = nullptr;
    static SDK::UFunction* function = nullptr;

    if (!IsLiveUObject(character) || !IsLiveUObject(item))
        return false;

    if (character->Class != cachedClass || !IsLiveUObject(function))
    {
        cachedClass = character->Class;
        function = FindFunction(character, "PickUp_SERVER");
    }
    if (!game::CanProcessEvent(character, function))
        return false;

    SDK::Params::BPCharacter_Demo_C_PickUp_SERVER parameters{};
    parameters.Item = item;
    return game::ProcessEventSafe(character, function, &parameters);
}

bool RequestPickupThroughGame(SDK::ABPCharacter_Demo_C* character, SDK::ADroppedItem* item)
{
    static SDK::UClass* cachedClass = nullptr;
    static SDK::UFunction* function = nullptr;

    if (!IsLiveUObject(character) || !IsLiveUObject(item))
        return false;

    if (character->Class != cachedClass || !IsLiveUObject(function))
    {
        cachedClass = character->Class;
        function = FindFunction(character, "TryPickup");
    }

    bool dispatched = false;
    if (game::CanProcessEvent(character, function))
    {
        SDK::ADroppedItem* previous = character->CurrentFocusedItem;
        character->CurrentFocusedItem = item;
        const bool called = game::ProcessEventSafe(character, function, nullptr);

        if (IsLiveUObject(character) && character->CurrentFocusedItem == item)
            character->CurrentFocusedItem = previous;
        dispatched = called;
    }

    dispatched = RequestServerPickup(character, item) || dispatched;
    return dispatched;
}

bool RequestInteractThroughGame(SDK::ABPCharacter_Demo_C* character, SDK::AActor* actor)
{
    static SDK::UClass* cachedClass = nullptr;
    static SDK::UFunction* functions[2] = {nullptr, nullptr};
    if (!IsLiveUObject(character) || !IsLiveUObject(actor))
        return false;

    if (character->Class != cachedClass)
    {
        cachedClass = character->Class;
        functions[0] = FindFunction(character, "Interact");
        functions[1] = FindFunction(character, "InteractCallBackVR");
    }

    SDK::AActor* previous = character->CurrentInteractableActor;
    character->CurrentInteractableActor = actor;

    bool dispatched = false;
    for (SDK::UFunction* function : functions)
    {
        if (dispatched)
            break;
        if (!game::CanProcessEvent(character, function))
            continue;
        SDK::Params::FancyCharacter_Interact parameters{};
        parameters.Actor = actor;
        dispatched = game::ProcessEventSafe(character, function, &parameters);
    }

    if (IsLiveUObject(character) && character->CurrentInteractableActor == actor)
        character->CurrentInteractableActor = previous;
    return dispatched;
}

bool SetActorLocation(SDK::AActor* actor, const SDK::FVector& location)
{
    if (!IsLiveUObject(actor))
        return false;

    SDK::UFunction* function = FindFunction(actor, "K2_SetActorLocation");
    if (!game::CanProcessEvent(actor, function))
        return false;

    SDK::Params::Actor_K2_SetActorLocation parameters{};
    parameters.NewLocation = location;
    parameters.bSweep = false;
    parameters.bTeleport = true;

    const auto originalFlags = function->FunctionFlags;
    function->FunctionFlags |= 0x400;
    const bool called = game::ProcessEventSafe(actor, function, &parameters);
    if (IsLiveUObject(function))
        function->FunctionFlags = originalFlags;
    return called && parameters.ReturnValue;
}

std::string PickupDisplayName(SDK::ADroppedItem* item)
{
    std::string name;
    if (!IsLiveUObject(item) ||
        !game::TryFNameToString(item->ID, name) || name.empty() || name == "None")
    {
        if (!IsLiveUObject(item) || !IsLiveUObject(item->Class) ||
            !game::TryFNameToString(item->Class->Name, name))
            return "item";
    }

    const auto removePrefix = [&name](const char* prefix)
    {
        const std::size_t length = std::char_traits<char>::length(prefix);
        if (name.size() >= length && name.compare(0, length, prefix) == 0)
            name.erase(0, length);
    };
    removePrefix("BP_DroppedItem_");
    removePrefix("BP_");
    if (name.size() >= 2 && name.compare(name.size() - 2, 2, "_C") == 0)
        name.resize(name.size() - 2);
    while (!name.empty() && std::isdigit(static_cast<unsigned char>(name.back())) != 0)
        name.pop_back();
    while (!name.empty() && name.back() == '_')
        name.pop_back();
    std::replace(name.begin(), name.end(), '_', ' ');
    return name.empty() ? "item" : name;
}

std::string ActorDisplayName(SDK::AActor* actor)
{
    std::string name;
    if (!IsLiveUObject(actor) || !IsLiveUObject(actor->Class) ||
        !game::TryFNameToString(actor->Class->Name, name))
        return "item";

    if (name.rfind("BP_", 0) == 0)
        name.erase(0, 3);
    if (name.size() >= 2 && name.compare(name.size() - 2, 2, "_C") == 0)
        name.resize(name.size() - 2);
    while (!name.empty() && std::isdigit(static_cast<unsigned char>(name.back())) != 0)
        name.pop_back();
    while (!name.empty() && name.back() == '_')
        name.pop_back();
    std::replace(name.begin(), name.end(), '_', ' ');
    return name.empty() ? "item" : name;
}

bool NameContainsAny(const std::string& lower, const std::initializer_list<const char*> words)
{
    for (const char* word : words)
    {
        if (word != nullptr && lower.find(word) != std::string::npos)
            return true;
    }
    return false;
}

// 0 = not a pickup, 1 = ADroppedItem (TryPickup/PickUp_SERVER), 2 = interactable actor.
std::uint8_t ClassifyPickupActor(SDK::AActor* actor)
{
    if (IsClassDerivedFrom(actor, "DroppedItem"))
        return 1;

    std::string className;
    if (!IsLiveUObject(actor) || !IsLiveUObject(actor->Class) ||
        !game::TryFNameToString(actor->Class->Name, className))
        return 0;
    std::transform(className.begin(), className.end(), className.begin(),
        [](const unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

    if (NameContainsAny(className, {"box", "test", "exitsign", "exit_sign", "staticmesh"}))
        return 0;

    if (NameContainsAny(className, {"note", "document", "radio", "paper", "clipboard"}))
        return 0;

    if (NameContainsAny(className, { "bp_keytu", "bp_keymanager"  }))
        return 0;

    if (NameContainsAny(className, {
            "tape_bp", "bp_tape", "tape", "cassette",
            "almondwater", "almond_water",
            "battery", "flashlight",
            "pushable",
            "keyitem", "bp_key",
            //"key_", "_key",
            //"key",
            "energybar", "energy_bar",
            "collectible",
            "inventoryitem",
            "bp_dropped", "droppeditem"
        }))
        return 2;

    return 0;
}

bool IsFlashlightName(const std::string& name)
{
    static constexpr char needle[] = "flashlight";
    return std::search(name.begin(), name.end(), std::begin(needle), std::end(needle) - 1,
                       [](const char left, const char right)
                       {
                           return std::tolower(static_cast<unsigned char>(left)) == right;
                       }) != name.end();
}
}

Pickup& Pickup::Instance()
{
    static Pickup instance;
    return instance;
}

void Pickup::UpdateInput(bool inputBlocked)
{
    hostAuthorization_.store(settings_.hostAcceptMemberPickup, std::memory_order_release);
    hostMaximumRadiusMeters_.store(
        std::clamp(settings_.hostMaximumRadiusMeters, 1.0f, 800.0f),
        std::memory_order_relaxed);

    const int key = settings_.hotkey;
    const bool down = key > 0 && key < 256 && (GetAsyncKeyState(key) & 0x8000) != 0;
    if (settings_.enabled && !inputBlocked && down && !hotkeyDown_)
        Request();
    hotkeyDown_ = down;

    const auto now = std::chrono::steady_clock::now();
    if (!settings_.enabled || !settings_.autoPickup)
    {
        nextAutoRequest_ = {};
        return;
    }

    if (nextAutoRequest_ == std::chrono::steady_clock::time_point{})
        nextAutoRequest_ = now;
    if (!HasPendingRequest() && now >= nextAutoRequest_)
    {
        Request();
        const float seconds = std::clamp(settings_.autoPickupIntervalSeconds, 0.25f, 5.0f);
        nextAutoRequest_ = now + std::chrono::milliseconds(
            static_cast<int>(seconds * 1000.0f));
    }
}

void Pickup::Request()
{
    if (!settings_.enabled)
        return;
    requestedRadiusMeters_.store(std::clamp(settings_.radiusMeters, 1.0f, 800.0f),
                                 std::memory_order_relaxed);
    requestedMaximum_.store(std::clamp(settings_.maxItemsPerActivation, 1, 32),
                            std::memory_order_relaxed);
    pending_.store(true, std::memory_order_release);
}

void Pickup::OnGameThreadTick(const SDK::UObject* tickObject)
{
    if (!HasPendingRequest())
        return;

    SDK::UWorld* world = ResolveWorld();
    SDK::ABPCharacter_Demo_C* character = ResolveLocalCharacter(world);
    if (character == nullptr || tickObject != character)
        return;

    if (pending_.exchange(false, std::memory_order_acq_rel))
    {
        lastCandidates_.store(0, std::memory_order_relaxed);
        lastRequested_.store(0, std::memory_order_relaxed);
        queue_.clear();
        completedBatchNames_.clear();
        nextQueueIndex_ = 0;
        queuedCount_.store(0, std::memory_order_release);
        scanCandidates_.clear();
        scanClassCache_.clear();
        scanLevelIndex_ = 0;
        scanActorIndex_ = 0;

        SDK::FVector playerLocation{};
        if (!ReadActorLocation(character, playerLocation))
        {
            scanActive_.store(false, std::memory_order_release);
            return;
        }

        const float radiusCm = requestedRadiusMeters_.load(std::memory_order_relaxed) * 100.0f;
        scanWorld_ = world;
        scanWorldIndex_ = world->Index;
        scanOriginX_ = playerLocation.X;
        scanOriginY_ = playerLocation.Y;
        scanOriginZ_ = playerLocation.Z;
        scanRadiusSquared_ = radiusCm * radiusCm;
        scanMaximum_ = requestedMaximum_.load(std::memory_order_relaxed);
        scanActive_.store(true, std::memory_order_release);
    }

    if (scanActive_.load(std::memory_order_acquire))
    {
        if (world != scanWorld_ || !IsLiveUObject(world) || world->Index != scanWorldIndex_)
        {
            scanActive_.store(false, std::memory_order_release);
            scanCandidates_.clear();
            scanClassCache_.clear();
            return;
        }

        if (std::chrono::steady_clock::now() < nextScanBatch_)
            return;
        nextScanBatch_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(60);

        const auto levels = world->Levels;
        if (!levels.IsValid() || levels.Num() <= 0 || levels.Num() > 512 ||
            !IsReadable(levels.GetDataPtr(), sizeof(SDK::ULevel*) * levels.Num()))
        {
            scanActive_.store(false, std::memory_order_release);
            return;
        }

        int budget = std::clamp(settings_.scanActorsPerTick, 50, 2000);
        const SDK::FVector playerLocation(scanOriginX_, scanOriginY_, scanOriginZ_);
        while (budget > 0 && scanLevelIndex_ < levels.Num())
        {
            SDK::ULevel* level = levels.GetDataPtr()[scanLevelIndex_];
            if (!IsReadable(level, 0xA8) || !IsLiveUObject(level))
            {
                ++scanLevelIndex_;
                scanActorIndex_ = 0;
                continue;
            }

            const auto actors = level->Actors;
            if (!actors.IsValid() || actors.Num() < 0 || actors.Num() > 100000 ||
                (actors.Num() > 0 &&
                 !IsReadable(actors.GetDataPtr(), sizeof(SDK::AActor*) * actors.Num())))
            {
                ++scanLevelIndex_;
                scanActorIndex_ = 0;
                continue;
            }

            while (budget > 0 && scanActorIndex_ < actors.Num())
            {
                SDK::AActor* actor = actors.GetDataPtr()[scanActorIndex_++];
                --budget;
                if (!IsLiveUObject(actor))
                    continue;

                auto [classIterator, inserted] = scanClassCache_.try_emplace(
                    actor->Class, static_cast<std::uint8_t>(0));
                if (inserted)
                    classIterator->second = ClassifyPickupActor(actor);
                const std::uint8_t pickupKind = classIterator->second;
                if (pickupKind == 0)
                    continue;

                auto* item = pickupKind == 1
                    ? reinterpret_cast<SDK::ADroppedItem*>(actor) : nullptr;
                if (item != nullptr && !item->CanPickup)
                    continue;

                SDK::FVector itemLocation{};
                if (!ReadActorLocation(actor, itemLocation))
                    continue;

                const float distanceSquared = DistanceSquared(playerLocation, itemLocation);
                if (distanceSquared <= scanRadiusSquared_)
                {
                    std::string displayName = item != nullptr
                        ? PickupDisplayName(item) : ActorDisplayName(actor);
                    if (settings_.excludeFlashlights && IsFlashlightName(displayName))
                        continue;
                    scanCandidates_.push_back({distanceSquared,
                        {actor, actor->Index, std::move(displayName),
                         pickupKind == 1 ? PickupKind::DroppedItem : PickupKind::Interactable}});
                }
            }

            if (scanActorIndex_ >= actors.Num())
            {
                ++scanLevelIndex_;
                scanActorIndex_ = 0;
            }
        }

        lastCandidates_.store(scanCandidates_.size(), std::memory_order_relaxed);
        if (scanLevelIndex_ < levels.Num())
            return;

        std::sort(scanCandidates_.begin(), scanCandidates_.end(),
                  [](const ScanCandidate& left, const ScanCandidate& right)
                  {
                      return left.distanceSquared < right.distanceSquared;
                  });
        const std::size_t queueCount = std::min(
            scanCandidates_.size(), static_cast<std::size_t>(scanMaximum_));
        queue_.reserve(queueCount);
        for (std::size_t index = 0; index < queueCount; ++index)
            queue_.push_back(std::move(scanCandidates_[index].item));

        const std::size_t candidateCount = scanCandidates_.size();
        scanCandidates_.clear();
        scanClassCache_.clear();
        scanActive_.store(false, std::memory_order_release);
        queuedCount_.store(queue_.size(), std::memory_order_release);
        nextDispatch_ = std::chrono::steady_clock::now();
        core::Logf("range pickup incremental scan: candidates=%zu queued=%zu",
                   candidateCount, queue_.size());
    }

    if (nextQueueIndex_ >= queue_.size() || std::chrono::steady_clock::now() < nextDispatch_)
        return;

    const QueuedItem queued = queue_[nextQueueIndex_++];
    queuedCount_.store(queue_.size() - nextQueueIndex_, std::memory_order_release);
    nextDispatch_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(180);

    bool dispatched = false;
    if (IsLiveUObject(queued.actor) && queued.actor->Index == queued.objectIndex)
    {
        if (settings_.teleportItemsBeforePickup)
        {
            SDK::FVector playerLocation{};
            if (ReadActorLocation(character, playerLocation))
            {
                playerLocation.Z += 25.0f;
                SetActorLocation(queued.actor, playerLocation);
            }
        }

        if (queued.kind == PickupKind::DroppedItem)
        {
            auto* item = reinterpret_cast<SDK::ADroppedItem*>(queued.actor);
            dispatched = item->CanPickup && RequestPickupThroughGame(character, item);
        }
        else
        {
            dispatched = RequestInteractThroughGame(character, queued.actor);
        }
    }
    if (dispatched)
    {
        completedBatchNames_.push_back(queued.displayName);
        const std::size_t total = lastRequested_.fetch_add(1, std::memory_order_acq_rel) + 1;
        core::Logf("range pickup dispatched: %zu/%zu", total, queue_.size());
    }

    if (nextQueueIndex_ >= queue_.size())
    {
        PublishCompletedBatch();
        queue_.clear();
        nextQueueIndex_ = 0;
        queuedCount_.store(0, std::memory_order_release);
    }
}

void Pickup::PublishCompletedBatch()
{
    if (completedBatchNames_.empty())
        return;

    std::sort(completedBatchNames_.begin(), completedBatchNames_.end());
    std::string message = "[Pickup] Picked up ";
    if (completedBatchNames_.size() > 1)
        message += std::to_string(completedBatchNames_.size()) + " items: ";

    for (std::size_t index = 0; index < completedBatchNames_.size();)
    {
        const std::size_t begin = index++;
        while (index < completedBatchNames_.size() &&
               completedBatchNames_[index] == completedBatchNames_[begin])
            ++index;
        if (begin != 0)
            message += ", ";
        message += completedBatchNames_[begin];
        const std::size_t count = index - begin;
        if (count > 1)
            message += " x" + std::to_string(count);
        if (message.size() > 210)
        {
            message += "...";
            break;
        }
    }

    const auto publishedAt = std::chrono::steady_clock::now();
    if (message == lastBatchMessage_ &&
        publishedAt - lastBatchTime_ < std::chrono::seconds(4))
    {
        completedBatchNames_.clear();
        return;
    }
    lastBatchMessage_ = message;
    lastBatchTime_ = publishedAt;

    {
        std::lock_guard lock(notificationMutex_);
        notificationQueue_.push_back(std::move(message));
    }
    completedBatchNames_.clear();
}

void Pickup::DrainNotifications(std::vector<std::string>& destination)
{
    std::lock_guard lock(notificationMutex_);
    for (std::string& message : notificationQueue_)
        destination.push_back(std::move(message));
    notificationQueue_.clear();
}

bool Pickup::HasPendingRequest() const noexcept
{
    return pending_.load(std::memory_order_acquire) ||
           scanActive_.load(std::memory_order_acquire) ||
           queuedCount_.load(std::memory_order_acquire) != 0;
}

bool Pickup::HostAuthorizationEnabled() const noexcept
{
    return hostAuthorization_.load(std::memory_order_acquire);
}

void Pickup::OnBeforeServerPickup(const SDK::UObject* object, void* parameters)
{
    if (!HostAuthorizationEnabled() || parameters == nullptr || !IsLiveUObject(object) ||
        !IsClassDerivedFrom(object, "BPCharacter_Demo_C"))
        return;

    auto* character = reinterpret_cast<SDK::ABPCharacter_Demo_C*>(const_cast<SDK::UObject*>(object));
    if (character->Role != SDK::ENetRole::ROLE_Authority)
        return;

    auto* pickupParameters = reinterpret_cast<SDK::Params::BPCharacter_Demo_C_PickUp_SERVER*>(parameters);
    SDK::ADroppedItem* item = pickupParameters->Item;
    if (!IsLiveUObject(item) || !item->CanPickup || !IsClassDerivedFrom(item, "DroppedItem"))
        return;

    SDK::FVector playerLocation{};
    SDK::FVector itemLocation{};
    if (!ReadActorLocation(character, playerLocation) || !ReadActorLocation(item, itemLocation))
        return;

    const float maximumCm = hostMaximumRadiusMeters_.load(std::memory_order_relaxed) * 100.0f;
    if (DistanceSquared(playerLocation, itemLocation) > maximumCm * maximumCm)
        return;

    SDK::FVector pickupLocation = playerLocation;
    pickupLocation.Z += 25.0f;
    if (SetActorLocation(item, pickupLocation))
    {
        const std::size_t total = hostAuthorizedCount_.fetch_add(1, std::memory_order_acq_rel) + 1;
        core::Logf("host authorized range pickup: total=%zu distance=%.1fm", total,
                   std::sqrt(DistanceSquared(playerLocation, itemLocation)) / 100.0f);
    }
}

void Pickup::SetGameThreadHookReady(bool ready) noexcept
{
    hookReady_.store(ready, std::memory_order_release);
}

PickupStatus Pickup::Status() const noexcept
{
    PickupStatus status{};
    status.gameThreadHookReady = hookReady_.load(std::memory_order_acquire);
    status.requestPending = HasPendingRequest();
    status.candidates = lastCandidates_.load(std::memory_order_relaxed);
    status.requested = lastRequested_.load(std::memory_order_acquire);
    status.queued = queuedCount_.load(std::memory_order_acquire);
    status.hostAuthorized = hostAuthorizedCount_.load(std::memory_order_acquire);
    return status;
}

void Pickup::Reset()
{
    pending_.store(false, std::memory_order_release);
    scanActive_.store(false, std::memory_order_release);
    hostAuthorization_.store(false, std::memory_order_release);
    lastCandidates_.store(0, std::memory_order_relaxed);
    lastRequested_.store(0, std::memory_order_relaxed);
    queuedCount_.store(0, std::memory_order_relaxed);
    hostAuthorizedCount_.store(0, std::memory_order_relaxed);
    queue_.clear();
    scanCandidates_.clear();
    scanClassCache_.clear();
    scanWorld_ = nullptr;
    scanWorldIndex_ = -1;
    scanLevelIndex_ = 0;
    scanActorIndex_ = 0;
    completedBatchNames_.clear();
    {
        std::lock_guard lock(notificationMutex_);
        notificationQueue_.clear();
    }
    nextQueueIndex_ = 0;
    nextAutoRequest_ = {};
    hotkeyDown_ = false;
}
}

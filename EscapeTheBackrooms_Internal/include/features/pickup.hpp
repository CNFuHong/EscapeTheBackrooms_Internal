#pragma once

#include "core/config.hpp"

#include <Windows.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <utility>
#include <unordered_map>
#include <vector>

namespace SDK
{
class ADroppedItem;
class AActor;
class UObject;
class UWorld;
class UClass;
}

namespace etb::features
{
struct PickupSettings
{
    CFG_VAR("pickup_enabled", bool, enabled, true, false, "Range Pickup", "", "Pickup");
    CFG_VAR("pickup_auto", bool, autoPickup, false, true, "Range Pickup", "Automatic", "Pickup");
    CFG_VAR("pickup_auto_interval", float, autoPickupIntervalSeconds, 1.0f, true, "Range Pickup", "Interval", "Pickup");
    CFG_VAR("pickup_scan_budget", int, scanActorsPerTick, 400, true, "Range Pickup", "Scan Budget", "Pickup");
    CFG_VAR("pickup_radius", float, radiusMeters, 15.0f, true, "Range Pickup", "Radius", "Pickup");
    CFG_VAR("pickup_maximum", int, maxItemsPerActivation, 3, true, "Range Pickup", "Maximum", "Pickup");
    CFG_VAR("pickup_hotkey", int, hotkey, VK_XBUTTON2, true, "Range Pickup", "Hotkey", "Pickup");
    CFG_VAR("pickup_exclude_flashlights", bool, excludeFlashlights, true, true, "Range Pickup", "Exclude Flashlights", "Pickup");
    CFG_VAR("pickup_host_accept", bool, hostAcceptMemberPickup, false, true, "Range Pickup", "Host Accept", "Pickup");
    CFG_VAR("pickup_host_radius", float, hostMaximumRadiusMeters, 100.0f, true, "Range Pickup", "Host Radius", "Pickup");
};

struct PickupStatus
{
    bool gameThreadHookReady = false;
    bool requestPending = false;
    std::size_t candidates = 0;
    std::size_t requested = 0;
    std::size_t queued = 0;
    std::size_t hostAuthorized = 0;
};

class Pickup final
{
public:
    static Pickup& Instance();

    void UpdateInput(bool inputBlocked);
    void Request();
    void DrainNotifications(std::vector<std::string>& destination);
    void Reset();

    bool HasPendingRequest() const noexcept;
    bool HostAuthorizationEnabled() const noexcept;
    void OnBeforeServerPickup(const SDK::UObject* object, void* parameters);
    void OnGameThreadTick(const SDK::UObject* tickObject);
    void SetGameThreadHookReady(bool ready) noexcept;

    PickupSettings& Settings() noexcept { return settings_; }
    const PickupSettings& Settings() const noexcept { return settings_; }
    PickupStatus Status() const noexcept;

private:
    Pickup() = default;

    enum class PickupKind : std::uint8_t
    {
        DroppedItem,
        Interactable
    };

    struct QueuedItem
    {
        SDK::AActor* actor = nullptr;
        std::int32_t objectIndex = -1;
        std::string displayName;
        PickupKind kind = PickupKind::DroppedItem;
    };

    void PublishCompletedBatch();

    PickupSettings settings_{};
    bool hotkeyDown_ = false;
    std::atomic_bool pending_{false};
    std::atomic_bool scanActive_{false};
    std::atomic_bool hookReady_{false};
    std::atomic_bool hostAuthorization_{false};
    std::atomic<float> hostMaximumRadiusMeters_{100.0f};
    std::atomic<float> requestedRadiusMeters_{15.0f};
    std::atomic<int> requestedMaximum_{3};
    std::atomic_size_t lastCandidates_{0};
    std::atomic_size_t lastRequested_{0};
    std::atomic_size_t queuedCount_{0};
    std::atomic_size_t hostAuthorizedCount_{0};
    std::vector<QueuedItem> queue_{};
    struct ScanCandidate
    {
        float distanceSquared = 0.0f;
        QueuedItem item{};
    };
    SDK::UWorld* scanWorld_ = nullptr;
    std::int32_t scanWorldIndex_ = -1;
    int scanLevelIndex_ = 0;
    int scanActorIndex_ = 0;
    float scanOriginX_ = 0.0f;
    float scanOriginY_ = 0.0f;
    float scanOriginZ_ = 0.0f;
    float scanRadiusSquared_ = 0.0f;
    int scanMaximum_ = 0;
    std::vector<ScanCandidate> scanCandidates_{};
    // 0ignored 1ADroppedItem 2generic
    std::unordered_map<SDK::UClass*, std::uint8_t> scanClassCache_{};
    std::vector<std::string> completedBatchNames_{};
    std::vector<std::string> notificationQueue_{};
    std::mutex notificationMutex_{};
    std::size_t nextQueueIndex_ = 0;
    std::chrono::steady_clock::time_point nextDispatch_{};
    std::chrono::steady_clock::time_point nextAutoRequest_{};
};
}

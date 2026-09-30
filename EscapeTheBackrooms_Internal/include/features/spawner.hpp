#pragma once

#include "core/config.hpp"

#include <SDK/CoreUObject_structs.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace SDK
{
class UObject;
class UClass;
class UWorld;
struct FVector;
}

namespace etb::features
{
enum class SpawnCategory : std::uint8_t
{
    Building,
    Item,
    Entity,
    Other
};

struct SpawnerSettings
{
    CFG_VAR("spawner_distance", float, spawnDistance, 300.0f, true, "Spawner", "Distance", "Host");
    CFG_VAR("spawner_count", int, spawnCount, 1, true, "Spawner", "Count", "Host");
};

struct SpawnEntry
{
    std::string name;
    SDK::UClass* cls = nullptr;
    SpawnCategory category = SpawnCategory::Other;
};

struct SpawnFav
{
    bool favorite = false;
    std::string note;
};

class Spawner final
{
public:
    static Spawner& Instance();

    // Grabs blueprint actor classes whose name contains `filter`, up to `limit`.
    // CDO objects ("Default__...") are skipped so only real, spawnable classes remain.
    bool FetchList(const char* filter, std::size_t limit, std::string& message);
    bool FetchByCategory(SpawnCategory category, std::size_t limit, std::string& message);
    bool SpawnByIndex(std::size_t index, std::string& message, bool notify);
    void ClearList();
    void Release();

    // Deferred (game-thread) spawn: the menu only sets a request; OnGameThreadTick
    // performs the actual actor spawn so we never call game functions on the UI thread.
    void RequestSpawn(std::size_t index);
    bool NeedsGameThreadTick() const noexcept;
    void OnGameThreadTick();

    // Favorites / notes (persisted to a JSON file in the config directory).
    void LoadFavorites();
    void SaveFavorites() const;
    void ToggleFavorite(const std::string& name);
    void SetNote(const std::string& name, const std::string& note);
    const SpawnFav* MetaFor(const std::string& name) const;

    // Exit pathfinding lives on the game thread (nav queries are game-thread bound);
    // the ESP requests recomputes for several exit points and snapshots the results.
    void SetExitPathEnabled(bool enabled) noexcept;
    void RequestExitPaths(SDK::UWorld* world, const SDK::FVector& start,
                          const std::vector<SDK::FVector>& ends);
    bool SnapshotExitPaths(std::vector<std::vector<SDK::FVector>>& out) const;

    SpawnerSettings& Settings() noexcept { return settings_; }
    const SpawnerSettings& Settings() const noexcept { return settings_; }
    const std::vector<SpawnEntry>& Entries() const noexcept { return entries_; }
    bool HasList() const noexcept { return !entries_.empty(); }

private:
    Spawner() = default;

    SpawnerSettings settings_{};
    std::vector<SpawnEntry> entries_;
    std::map<std::string, SpawnFav> favorites_;
    std::string favoritesPath_;
    std::atomic_bool spawnRequestPending_{false};
    std::atomic<int> spawnRequestIndex_{-1};

    mutable std::mutex exitPathMutex_;
    std::vector<std::vector<SDK::FVector>> exitPaths_;
    std::vector<std::vector<SDK::FVector>> exitPathStaging_;
    std::vector<SDK::FVector> exitPathTargets_;
    std::size_t exitPathTargetIndex_ = 0;
    bool exitPathEnabled_ = false;
    bool exitPathRequested_ = false;
    std::atomic_bool exitPathActive_{false};
    SDK::UWorld* exitPathWorld_ = nullptr;
    SDK::FVector exitPathStart_{};
    std::uint64_t exitPathLastMs_ = 0;

    bool ComputeExitPath(SDK::UWorld* world, const SDK::FVector& start,
                         const SDK::FVector& end, std::vector<SDK::FVector>& out);
};
}

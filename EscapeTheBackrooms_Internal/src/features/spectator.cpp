#include "features/spectator.hpp"

#include "core/logger.hpp"
#include "features/notifications.hpp"
#include "game/unreal_safety.hpp"

#include <SDK/Basic.hpp>
#include <SDK/CoreUObject_classes.hpp>
#include <SDK/Engine_classes.hpp>
#include <SDK/Engine_parameters.hpp>
#include <SDK/BPCharacter_Demo_classes.hpp>
#include <SDK/MP_GameState_classes.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace etb::features
{
namespace
{
using SDK::AActor;
using SDK::ACameraActor;
using SDK::APawn;
using SDK::APlayerCameraManager;
using SDK::APlayerController;
using SDK::ABPCharacter_Demo_C;
using SDK::AMP_GameState_C;
using SDK::UWorld;

constexpr float kPi = 3.14159265358979323846f;

void LogSpectatorFailure(const char* message)
{
    static std::string lastMessage;
    static ULONGLONG nextLog = 0;
    const ULONGLONG now = GetTickCount64();
    if (lastMessage == message && now < nextLog)
        return;
    lastMessage = message;
    nextLog = now + 5000;
    core::Logf("spectator: %s", message);
}

bool IsKeyJustPressed(const int virtualKey, bool& wasDown)
{
    if (virtualKey == 0)
    {
        wasDown = false;
        return false;
    }
    const bool down = (GetAsyncKeyState(virtualKey) & 0x8000) != 0;
    const bool pressed = down && !wasDown;
    wasDown = down;
    return pressed;
}

UWorld* ResolveWorld()
{
    const auto base = SDK::InSDKUtils::GetImageBase();
    auto** address = reinterpret_cast<UWorld**>(base + SDK::Offsets::GWorld);
    if (!game::IsReadable(address, sizeof(*address)))
        return nullptr;
    UWorld* world = *address;
    return game::IsLiveUObject(world) ? world : nullptr;
}

APlayerController* ResolveController(UWorld* world)
{
    if (!game::IsLiveUObject(world))
        return nullptr;
    SDK::UGameInstance* instance = world->OwningGameInstance;
    if (!game::IsLiveUObject(instance))
        return nullptr;
    const auto players = instance->LocalPlayers;
    if (!players.IsValid() || players.Num() <= 0 || players.Num() > 8 ||
        !game::IsReadable(players.GetDataPtr(), sizeof(SDK::ULocalPlayer*) * players.Num()))
        return nullptr;
    SDK::ULocalPlayer* player = players.GetDataPtr()[0];
    if (!game::IsLiveUObject(player))
        return nullptr;
    APlayerController* controller = player->PlayerController;
    return game::IsLiveUObject(controller) ? controller : nullptr;
}

__declspec(noinline) APawn* GetPawnUnsafe(APlayerController* controller)
{
    return controller->AcknowledgedPawn;
}

APawn* GetPawn(APlayerController* controller)
{
    if (!game::IsLiveUObject(controller))
        return nullptr;
    __try
    {
        APawn* pawn = GetPawnUnsafe(controller);
        return game::IsLiveUObject(pawn) ? pawn : nullptr;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

SDK::UFunction* FindFunction(const SDK::UObject* object, const char* wantedName)
{
    if (!game::IsLiveUObject(object) || wantedName == nullptr)
        return nullptr;

    struct CacheEntry
    {
        SDK::UClass* ownerClass;
        std::string name;
        SDK::UFunction* function;
    };
    static std::vector<CacheEntry> cache;
    SDK::UClass* ownerClass = object->Class;
    for (const CacheEntry& entry : cache)
    {
        if (entry.ownerClass == ownerClass && entry.name == wantedName &&
            game::IsLiveUObject(entry.function))
            return entry.function;
    }

    int typeGuard = 0;
    for (SDK::UStruct* type = ownerClass;
         game::IsLiveUObject(type) && typeGuard++ < 64; type = type->SuperStruct)
    {
        int fieldGuard = 0;
        for (SDK::UField* field = type->Children;
             game::IsLiveUObject(field) && fieldGuard++ < 4096; field = field->Next)
        {
            std::string name;
            if (game::TryFNameToString(field->Name, name) && name == wantedName)
            {
                auto* function = reinterpret_cast<SDK::UFunction*>(field);
                cache.push_back({ownerClass, wantedName, function});
                return function;
            }
        }
    }
    return nullptr;
}

bool Invoke(const SDK::UObject* object, const char* functionName, void* parameters)
{
    SDK::UFunction* function = FindFunction(object, functionName);
    if (!game::CanProcessEvent(object, function))
        return false;

    const auto flags = function->FunctionFlags;
    function->FunctionFlags |= 0x400;
    const bool invoked = game::ProcessEventSafe(object, function, parameters);
    if (game::IsLiveUObject(function))
        function->FunctionFlags = flags;
    return invoked;
}

bool IsObjectA(const SDK::UObject* object, const char* className)
{
    if (!game::IsLiveUObject(object) || className == nullptr)
        return false;
    int guard = 0;
    for (SDK::UStruct* type = object->Class;
         game::IsLiveUObject(type) && guard++ < 64; type = type->SuperStruct)
    {
        std::string name;
        if (game::TryFNameToString(type->Name, name) && name == className)
            return true;
    }
    return false;
}

__declspec(noinline) SDK::FVector GetLocationUnsafe(AActor* actor)
{
    SDK::Params::Actor_K2_GetActorLocation parameters{};
    Invoke(actor, "K2_GetActorLocation", &parameters);
    return parameters.ReturnValue;
}

bool GetLocation(AActor* actor, SDK::FVector& location)
{
    if (!game::IsLiveUObject(actor))
        return false;
    __try
    {
        location = GetLocationUnsafe(actor);
        return std::isfinite(location.X) && std::isfinite(location.Y) &&
               std::isfinite(location.Z);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

__declspec(noinline) bool SetLocationUnsafe(AActor* actor, const SDK::FVector& location,
                                             const bool sweep)
{
    SDK::Params::Actor_K2_SetActorLocation parameters{};
    parameters.NewLocation = location;
    parameters.bSweep = sweep;
    parameters.bTeleport = true;
    return Invoke(actor, "K2_SetActorLocation", &parameters) && parameters.ReturnValue;
}

bool SetLocation(AActor* actor, const SDK::FVector& location, const bool sweep)
{
    if (!game::IsLiveUObject(actor))
        return false;
    __try { return SetLocationUnsafe(actor, location, sweep); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

__declspec(noinline) bool SetRotationUnsafe(AActor* actor, const SDK::FRotator& rotation)
{
    SDK::Params::Actor_K2_SetActorRotation parameters{};
    parameters.NewRotation = rotation;
    parameters.bTeleportPhysics = true;
    return Invoke(actor, "K2_SetActorRotation", &parameters) && parameters.ReturnValue;
}

bool SetRotation(AActor* actor, const SDK::FRotator& rotation)
{
    if (!game::IsLiveUObject(actor))
        return false;
    __try { return SetRotationUnsafe(actor, rotation); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

__declspec(noinline) SDK::FRotator GetControlRotationUnsafe(APlayerController* controller)
{
    SDK::Params::Controller_GetControlRotation parameters{};
    Invoke(controller, "GetControlRotation", &parameters);
    return parameters.ReturnValue;
}

SDK::FRotator GetControlRotation(APlayerController* controller)
{
    if (!game::IsLiveUObject(controller))
        return {};
    __try { return GetControlRotationUnsafe(controller); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return {}; }
}

__declspec(noinline) void SetControlRotationUnsafe(APlayerController* controller,
                                                    const SDK::FRotator& rotation)
{
    SDK::Params::Controller_SetControlRotation parameters{};
    parameters.NewRotation = rotation;
    Invoke(controller, "SetControlRotation", &parameters);
}

void SetControlRotation(APlayerController* controller, const SDK::FRotator& rotation)
{
    if (!game::IsLiveUObject(controller))
        return;
    __try { SetControlRotationUnsafe(controller, rotation); }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}

float UnwrapPitch(const float pitch)
{
    if (!std::isfinite(pitch))
        return 0.0f;
    if (pitch > 180.0f)
        return pitch - 360.0f;
    if (pitch < -180.0f)
        return pitch + 360.0f;
    return pitch;
}

float UnwrapYaw(const float yaw)
{
    if (!std::isfinite(yaw))
        return 0.0f;
    if (yaw > 180.0f)
        return yaw - 360.0f;
    if (yaw < -180.0f)
        return yaw + 360.0f;
    return yaw;
}

__declspec(noinline) bool GetCameraPoseUnsafe(APlayerController* controller,
                                              SDK::FVector& location,
                                              SDK::FRotator& rotation)
{
    APlayerCameraManager* manager = controller->PlayerCameraManager;
    if (!game::IsLiveUObject(manager))
        return false;
    SDK::Params::PlayerCameraManager_GetCameraLocation locationParameters{};
    SDK::Params::PlayerCameraManager_GetCameraRotation rotationParameters{};
    if (!Invoke(manager, "GetCameraLocation", &locationParameters) ||
        !Invoke(manager, "GetCameraRotation", &rotationParameters))
        return false;
    location = locationParameters.ReturnValue;
    rotation = rotationParameters.ReturnValue;
    return true;
}

bool GetCameraPose(APlayerController* controller, SDK::FVector& location,
                   SDK::FRotator& rotation)
{
    if (!game::IsLiveUObject(controller))
        return false;
    __try
    {
        if (!GetCameraPoseUnsafe(controller, location, rotation))
            return false;
        return std::isfinite(location.X) && std::isfinite(location.Y) &&
               std::isfinite(location.Z) && std::isfinite(rotation.Pitch) &&
               std::isfinite(rotation.Yaw) && std::isfinite(rotation.Roll);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

__declspec(noinline) AActor* GetViewTargetUnsafe(APlayerController* controller)
{
    SDK::Params::Controller_GetViewTarget parameters{};
    Invoke(controller, "GetViewTarget", &parameters);
    return parameters.ReturnValue;
}

AActor* GetViewTarget(APlayerController* controller)
{
    if (!game::IsLiveUObject(controller))
        return nullptr;
    __try
    {
        AActor* target = GetViewTargetUnsafe(controller);
        return game::IsLiveUObject(target) ? target : nullptr;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

__declspec(noinline) bool SetViewTargetUnsafe(APlayerController* controller, AActor* target)
{
    SDK::Params::PlayerController_SetViewTargetWithBlend parameters{};
    parameters.NewViewTarget = target;
    parameters.BlendTime = 0.0f;
    parameters.BlendFunc = SDK::EViewTargetBlendFunction::VTBlend_Linear;
    parameters.BlendExp = 0.0f;
    parameters.bLockOutgoing = false;
    return Invoke(controller, "SetViewTargetWithBlend", &parameters);
}

bool SetViewTarget(APlayerController* controller, AActor* target)
{
    if (!game::IsLiveUObject(controller) || !game::IsLiveUObject(target))
        return false;
    __try
    {
        return SetViewTargetUnsafe(controller, target);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

__declspec(noinline) bool IsMoveIgnoredUnsafe(APlayerController* controller)
{
    SDK::Params::Controller_IsMoveInputIgnored parameters{};
    Invoke(controller, "IsMoveInputIgnored", &parameters);
    return parameters.ReturnValue;
}

bool IsMoveIgnored(APlayerController* controller)
{
    if (!game::IsLiveUObject(controller))
        return false;
    __try { return IsMoveIgnoredUnsafe(controller); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

__declspec(noinline) bool SetMoveIgnoredUnsafe(APlayerController* controller, const bool ignored)
{
    SDK::Params::Controller_SetIgnoreMoveInput parameters{};
    parameters.bNewMoveInput = ignored;
    return Invoke(controller, "SetIgnoreMoveInput", &parameters);
}

bool SetMoveIgnored(APlayerController* controller, const bool ignored)
{
    if (!game::IsLiveUObject(controller))
        return false;
    __try
    {
        return SetMoveIgnoredUnsafe(controller, ignored);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

__declspec(noinline) bool GetAutoCameraUnsafe(APlayerController* controller)
{
    return controller->bAutoManageActiveCameraTarget;
}

bool GetAutoCamera(APlayerController* controller, bool& value)
{
    if (!game::IsLiveUObject(controller))
        return false;
    __try
    {
        value = GetAutoCameraUnsafe(controller);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

__declspec(noinline) void SetAutoCameraUnsafe(APlayerController* controller, const bool value)
{
    controller->bAutoManageActiveCameraTarget = value;
}

bool SetAutoCamera(APlayerController* controller, const bool value)
{
    if (!game::IsLiveUObject(controller))
        return false;
    __try
    {
        SetAutoCameraUnsafe(controller, value);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool TryReadObjectName(SDK::UObject* object, std::string& name)
{
    if (object == nullptr)
        return false;
    __try
    {
        if (object->Index < 0)
            return false;
        return game::TryFNameToString(object->Name, name);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

SDK::UObject* FindNamedObject(const char* wantedName)
{
    if (wantedName == nullptr)
        return nullptr;
    SDK::TUObjectArray* objects = SDK::UObject::GObjects.GetTypedPtr();
    if (!game::IsReadable(objects, sizeof(*objects)))
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
        if (TryReadObjectName(object, name) && name == wantedName &&
            game::IsLiveUObject(object))
            return object;
    }
    return nullptr;
}

__declspec(noinline) ACameraActor* SpawnCameraUnsafe(UWorld* world, AActor* owner,
                                                     const SDK::FTransform& transform)
{
    static SDK::UObject* gameplayStatics = nullptr;
    static SDK::UObject* cameraDefault = nullptr;
    if (!game::IsLiveUObject(gameplayStatics))
        gameplayStatics = FindNamedObject("Default__GameplayStatics");
    if (!game::IsLiveUObject(cameraDefault))
        cameraDefault = FindNamedObject("Default__CameraActor");
    if (!game::IsLiveUObject(gameplayStatics) || !game::IsLiveUObject(cameraDefault) ||
        !game::IsLiveUObject(cameraDefault->Class))
    {
        LogSpectatorFailure("camera spawn objects are unavailable");
        return nullptr;
    }

    AActor* pendingActor = nullptr;
    SDK::Params::GameplayStatics_BeginDeferredActorSpawnFromClass begin{};
    begin.WorldContextObject = world;
    begin.ActorClass = cameraDefault->Class;
    begin.SpawnTransform = transform;
    begin.CollisionHandlingOverride = SDK::ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    begin.Owner = owner;
    if (Invoke(gameplayStatics, "BeginDeferredActorSpawnFromClass", &begin) &&
        game::IsLiveUObject(begin.ReturnValue))
    {
        pendingActor = begin.ReturnValue;
    }
    else
    {
        SDK::Params::GameplayStatics_BeginSpawningActorFromClass legacy{};
        legacy.WorldContextObject = world;
        legacy.ActorClass = cameraDefault->Class;
        legacy.SpawnTransform = transform;
        legacy.bNoCollisionFail = true;
        legacy.Owner = owner;
        if (Invoke(gameplayStatics, "BeginSpawningActorFromClass", &legacy) &&
            game::IsLiveUObject(legacy.ReturnValue))
            pendingActor = legacy.ReturnValue;
    }
    if (!game::IsLiveUObject(pendingActor))
    {
        LogSpectatorFailure("camera actor creation failed");
        return nullptr;
    }

    SDK::Params::GameplayStatics_FinishSpawningActor finish{};
    finish.Actor = pendingActor;
    finish.SpawnTransform = transform;
    if (!Invoke(gameplayStatics, "FinishSpawningActor", &finish))
    {
        Invoke(pendingActor, "K2_DestroyActor", nullptr);
        LogSpectatorFailure("camera actor finalization failed");
        return nullptr;
    }
    AActor* result = game::IsLiveUObject(finish.ReturnValue) ? finish.ReturnValue : pendingActor;
    return static_cast<ACameraActor*>(result);
}

// The spawned free camera is owned by the local pawn (see SpawnCamera), so its
// UCameraComponent would normally inherit the pawn's / controller's view rotation
// and ignore the actor rotation we assign below. That keeps the camera stuck
// looking in whichever direction the pawn's control rotation points (e.g. straight
// up). Clearing bUsePawnControlRotation forces the camera to render at the actor
// rotation we set each tick, so mouse look (via ControlRotation) drives the view.
void UseCameraOwnerRotation(SDK::ACameraActor* camera)
{
    if (!game::IsLiveUObject(camera))
        return;
    __try
    {
        SDK::UCameraComponent* component = camera->CameraComponent;
        if (game::IsLiveUObject(component))
            component->bUsePawnControlRotation = false;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}

ACameraActor* SpawnCamera(UWorld* world, AActor* owner, const SDK::FVector& location,
                          const SDK::FRotator& rotation)
{
    if (!game::IsLiveUObject(world))
        return nullptr;
    SDK::FTransform transform{};
    transform.Rotation.W = 1.0f;
    transform.Translation = location;
    transform.Scale3D = {1.0f, 1.0f, 1.0f};
    __try
    {
        ACameraActor* camera = SpawnCameraUnsafe(world, owner, transform);
        if (!game::IsLiveUObject(camera) || !IsObjectA(camera, "CameraActor"))
        {
            LogSpectatorFailure("spawned camera is invalid");
            return nullptr;
        }
        if (!SetLocation(camera, location, false) || !SetRotation(camera, rotation))
            LogSpectatorFailure("camera transform initialization was partially rejected");
        UseCameraOwnerRotation(camera);
        return camera;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

__declspec(noinline) void DestroyActorUnsafe(AActor* actor)
{
    Invoke(actor, "K2_DestroyActor", nullptr);
}

void DestroyActor(AActor* actor)
{
    if (!game::IsLiveUObject(actor))
        return;
    __try { DestroyActorUnsafe(actor); }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}

__declspec(noinline) SDK::ENetRole GetRoleUnsafe(APlayerController* controller)
{
    return controller->Role;
}

bool IsAuthority(APlayerController* controller)
{
    if (!game::IsLiveUObject(controller))
        return false;
    __try { return GetRoleUnsafe(controller) == SDK::ENetRole::ROLE_Authority; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

__declspec(noinline) SDK::APlayerState* GetPlayerStateUnsafe(ABPCharacter_Demo_C* player)
{
    return player->PlayerState;
}

SDK::APlayerState* GetPlayerState(ABPCharacter_Demo_C* player)
{
    if (!game::IsLiveUObject(player))
        return nullptr;
    __try
    {
        SDK::APlayerState* state = GetPlayerStateUnsafe(player);
        return game::IsLiveUObject(state) ? state : nullptr;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

__declspec(noinline) SDK::FString GetPlayerNameUnsafe(SDK::APlayerState* state)
{
    SDK::Params::PlayerState_GetPlayerName parameters{};
    Invoke(state, "GetPlayerName", &parameters);
    return parameters.ReturnValue;
}

bool TryGetPlayerNameValue(SDK::APlayerState* state, SDK::FString& output)
{
    if (!game::IsLiveUObject(state))
        return false;
    __try { output = GetPlayerNameUnsafe(state); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

std::string GetPlayerName(ABPCharacter_Demo_C* player)
{
    SDK::APlayerState* playerState = GetPlayerState(player);
    if (!playerState)
        return {};
    SDK::FString value{};
    if (!TryGetPlayerNameValue(playerState, value))
        return {};
    std::string result;
    game::TryFStringToString(value, result);
    return result;
}

struct RuntimeState
{
    std::mutex mutex;
    UWorld* world = nullptr;
    APlayerController* controller = nullptr;
    APawn* body = nullptr;
    AActor* savedViewTarget = nullptr;
    ACameraActor* camera = nullptr;
    ABPCharacter_Demo_C* watchedPlayer = nullptr;
    SDK::FVector cameraLocation{};
    SDK::FRotator cameraRotation{};
    SDK::FRotator savedControlRotation{};
    bool active = false;
    bool freeCam = false;
    bool savedAutoCamera = true;
    bool autoCameraCaptured = false;
    bool moveIgnoreApplied = false;
    bool controlRotationCaptured = false;
    bool controllerReady = false;
    bool authority = false;
    int watchedIndex = -1;
    std::vector<ABPCharacter_Demo_C*> players;
    std::string watchedName;
    std::vector<std::pair<std::string, NotificationType>> notifications;
    ULONGLONG nextEnterAttempt = 0;
    ULONGLONG nextPlayerRefresh = 0;
    ULONGLONG readySince = 0;
    std::uintptr_t readyWorldKey = 0;
};

RuntimeState& State()
{
    static RuntimeState state;
    return state;
}

void QueueNotification(RuntimeState& state, std::string message, const NotificationType type)
{
    if (state.notifications.size() >= 16)
        state.notifications.erase(state.notifications.begin());
    state.notifications.emplace_back(std::move(message), type);
}

void ClearRuntimeReferences(RuntimeState& state)
{
    state.world = nullptr;
    state.controller = nullptr;
    state.body = nullptr;
    state.savedViewTarget = nullptr;
    state.camera = nullptr;
    state.watchedPlayer = nullptr;
    state.cameraLocation = {};
    state.cameraRotation = {};
    state.savedControlRotation = {};
    state.active = false;
    state.freeCam = false;
    state.savedAutoCamera = true;
    state.autoCameraCaptured = false;
    state.moveIgnoreApplied = false;
    state.controlRotationCaptured = false;
    state.controllerReady = false;
    state.authority = false;
    state.watchedIndex = -1;
    state.players.clear();
    state.watchedName.clear();
    state.nextEnterAttempt = 0;
    state.nextPlayerRefresh = 0;
    state.readySince = 0;
    state.readyWorldKey = 0;
}

__declspec(noinline) bool RefreshPlayersUnsafe(RuntimeState& state, UWorld* world,
                                                APawn* localBody)
{
    if (!game::IsLiveUObject(world->GameState) ||
        !IsObjectA(world->GameState, "MP_GameState_C"))
        return false;

    auto* gameState = static_cast<AMP_GameState_C*>(world->GameState);
    const auto& alive = gameState->PlayersAlive;
    const int count = alive.Num();
    if (count < 0 || count > 64)
        return false;

    state.players.clear();
    state.players.reserve(static_cast<std::size_t>(count));
    for (int index = 0; index < count; ++index)
    {
        ABPCharacter_Demo_C* player = alive[index];
        if (!game::IsLiveUObject(player) || player == localBody)
            continue;
        if (IsObjectA(player, "BPCharacter_Demo_C"))
            state.players.push_back(player);
    }
    return true;
}

bool TryRefreshPlayers(RuntimeState& state, UWorld* world, APawn* localBody)
{
    if (!game::IsLiveUObject(world))
        return false;
    __try { return RefreshPlayersUnsafe(state, world, localBody); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool RefreshPlayers(RuntimeState& state, UWorld* world, APawn* localBody)
{
    ABPCharacter_Demo_C* previous = state.watchedPlayer;
    if (!TryRefreshPlayers(state, world, localBody))
    {
        state.players.clear();
        state.watchedPlayer = nullptr;
        state.watchedName.clear();
        state.watchedIndex = -1;
        return false;
    }

    state.watchedIndex = -1;
    for (std::size_t index = 0; index < state.players.size(); ++index)
    {
        if (state.players[index] == previous)
        {
            state.watchedIndex = static_cast<int>(index);
            break;
        }
    }
    if (state.watchedIndex < 0)
    {
        state.watchedPlayer = nullptr;
        state.watchedName.clear();
    }
    return !state.players.empty();
}
bool SelectPlayer(RuntimeState& state, APlayerController* controller, int index)
{
    if (state.players.empty() || !game::IsLiveUObject(controller))
        return false;

    const int count = static_cast<int>(state.players.size());
    index %= count;
    if (index < 0)
        index += count;

    ABPCharacter_Demo_C* target = state.players[static_cast<std::size_t>(index)];
    if (!game::IsLiveUObject(target) || !SetViewTarget(controller, target))
        return false;

    state.watchedIndex = index;
    state.watchedPlayer = target;
    state.watchedName = GetPlayerName(target);
    return true;
}

void CaptureControllerState(RuntimeState& state, APlayerController* controller, APawn* body)
{
    state.controller = controller;
    state.body = body;
    state.savedViewTarget = GetViewTarget(controller);
    state.savedControlRotation = GetControlRotation(controller);
    state.controlRotationCaptured = true;
    state.autoCameraCaptured = GetAutoCamera(controller, state.savedAutoCamera);
    if (state.autoCameraCaptured)
        SetAutoCamera(controller, false);
    if (!IsMoveIgnored(controller))
        state.moveIgnoreApplied = SetMoveIgnored(controller, true);
}

void RestoreControllerState(RuntimeState& state)
{
    APlayerController* controller = state.controller;
    if (game::IsLiveUObject(controller))
    {
        if (state.moveIgnoreApplied)
            SetMoveIgnored(controller, false);
        if (state.autoCameraCaptured)
            SetAutoCamera(controller, state.savedAutoCamera);

        AActor* restoreTarget = state.savedViewTarget;
        if (!game::IsLiveUObject(restoreTarget) && game::IsLiveUObject(state.body))
            restoreTarget = state.body;
        if (game::IsLiveUObject(restoreTarget))
            SetViewTarget(controller, restoreTarget);
        if (state.controlRotationCaptured)
            SetControlRotation(controller, state.savedControlRotation);
    }

    if (game::IsLiveUObject(state.camera))
        DestroyActor(state.camera);

    state.camera = nullptr;
    state.watchedPlayer = nullptr;
    state.players.clear();
    state.watchedName.clear();
    state.watchedIndex = -1;
    state.active = false;
    state.freeCam = false;
    state.savedViewTarget = nullptr;
    state.moveIgnoreApplied = false;
    state.autoCameraCaptured = false;
    state.controlRotationCaptured = false;
}

bool EnterFreeCamera(RuntimeState& state, UWorld* world, APlayerController* controller,
                     APawn* body)
{
    SDK::FVector location{};
    SDK::FRotator rotation{};
    if (!GetCameraPose(controller, location, rotation))
    {
        if (!GetLocation(body, location))
        {
            LogSpectatorFailure("initial camera pose is unavailable");
            return false;
        }
        rotation = GetControlRotation(controller);
    }

    // The controller exposes its pitch in a wrapped space, so unwrap it before
    // clamping or the camera spawns pinned to the top of its pitch range.
    rotation.Pitch = std::clamp(UnwrapPitch(rotation.Pitch), -89.0f, 89.0f);
    rotation.Roll = 0.0f;

    ACameraActor* camera = SpawnCamera(world, body, location, rotation);
    if (!camera)
        return false;

    CaptureControllerState(state, controller, body);
    state.camera = camera;
    state.cameraLocation = location;
    state.cameraRotation = rotation;
    SetControlRotation(controller, rotation);
    if (!SetViewTarget(controller, camera))
    {
        LogSpectatorFailure("SetViewTargetWithBlend rejected the free camera");
        RestoreControllerState(state);
        return false;
    }

    state.active = true;
    state.freeCam = true;
    core::Logf("spectator free camera attached: Camera=%p Body=%p", camera, body);
    QueueNotification(state, "[Spectator] Free camera active", NotificationType::Success);
    return true;
}

bool EnterPlayerWatch(RuntimeState& state, UWorld* world, APlayerController* controller,
                      APawn* body)
{
    state.watchedPlayer = nullptr;
    state.watchedIndex = -1;
    if (!RefreshPlayers(state, world, body))
        return false;

    CaptureControllerState(state, controller, body);
    if (!SelectPlayer(state, controller, 0))
    {
        RestoreControllerState(state);
        return false;
    }

    state.active = true;
    state.freeCam = false;
    state.nextPlayerRefresh = GetTickCount64() + 500;
    QueueNotification(state,
        state.watchedName.empty() ? "[Spectator] Player watch active" :
                                    "[Spectator] Watching " + state.watchedName,
        NotificationType::Success);
    return true;
}

SDK::FVector Add(const SDK::FVector& left, const SDK::FVector& right)
{
    return {left.X + right.X, left.Y + right.Y, left.Z + right.Z};
}

SDK::FVector Scale(const SDK::FVector& value, const float scale)
{
    return {value.X * scale, value.Y * scale, value.Z * scale};
}

void UpdateFreeCamera(RuntimeState& state, APlayerController* controller,
                      const float configuredSpeed, const bool noClip,
                      const bool inputBlocked, const float deltaSeconds)
{
    if (!game::IsLiveUObject(state.camera))
        return;

    SDK::FRotator rotation = GetControlRotation(controller);
    if (!std::isfinite(rotation.Pitch) || !std::isfinite(rotation.Yaw))
        rotation = state.cameraRotation;
    // PlayerController stores its control rotation in FRotator::ClampAxis space
    // (0..360), so a downward pitch arrives as 270..360 and would otherwise be
    // clamped to the top of the range. Unwrap it into -180..180 before clamping so
    // the camera can pitch both up and down.
    rotation.Pitch = std::clamp(UnwrapPitch(rotation.Pitch), -89.0f, 89.0f);
    rotation.Yaw = UnwrapYaw(rotation.Yaw);
    rotation.Roll = 0.0f;
    state.cameraRotation = rotation;

    if (!inputBlocked)
    {
        const float yaw = rotation.Yaw * kPi / 180.0f;
        const float pitch = rotation.Pitch * kPi / 180.0f;
        const SDK::FVector forward{
            std::cos(pitch) * std::cos(yaw),
            std::cos(pitch) * std::sin(yaw),
            std::sin(pitch)};
        const SDK::FVector right{-std::sin(yaw), std::cos(yaw), 0.0f};
        const SDK::FVector up{0.0f, 0.0f, 1.0f};

        SDK::FVector direction{};
        if ((GetAsyncKeyState('W') & 0x8000) != 0) direction = Add(direction, forward);
        if ((GetAsyncKeyState('S') & 0x8000) != 0) direction = Add(direction, Scale(forward, -1.0f));
        if ((GetAsyncKeyState('D') & 0x8000) != 0) direction = Add(direction, right);
        if ((GetAsyncKeyState('A') & 0x8000) != 0) direction = Add(direction, Scale(right, -1.0f));
        if ((GetAsyncKeyState(VK_SPACE) & 0x8000) != 0) direction = Add(direction, up);
        if ((GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0) direction = Add(direction, Scale(up, -1.0f));

        const float length = std::sqrt(direction.X * direction.X + direction.Y * direction.Y +
                                       direction.Z * direction.Z);
        if (length > 0.001f)
        {
            const float boost = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0 ? 2.5f : 1.0f;
            const float dt = std::clamp(deltaSeconds, 0.001f, 0.1f);
            const float distance = std::clamp(configuredSpeed, 100.0f, 10000.0f) * boost * dt;
            const SDK::FVector wanted = Add(state.cameraLocation,
                                            Scale(direction, distance / length));
            if (SetLocation(state.camera, wanted, !noClip))
            {
                SDK::FVector actual{};
                state.cameraLocation = GetLocation(state.camera, actual) ? actual : wanted;
            }
        }
    }

    SetRotation(state.camera, state.cameraRotation);
    if (GetViewTarget(controller) != state.camera)
        SetViewTarget(controller, state.camera);
}

void UpdatePlayerWatch(RuntimeState& state, UWorld* world, APlayerController* controller,
                       APawn* body, const int cycleRequest, const int indexRequest)
{
    const ULONGLONG now = GetTickCount64();
    if (!game::IsLiveUObject(state.watchedPlayer) || now >= state.nextPlayerRefresh ||
        cycleRequest != 0 || indexRequest >= 0)
    {
        RefreshPlayers(state, world, body);
        state.nextPlayerRefresh = now + 500;
    }

    if (state.players.empty())
    {
        state.watchedPlayer = nullptr;
        state.watchedIndex = -1;
        state.watchedName.clear();
        return;
    }

    int wanted = state.watchedIndex;
    if (indexRequest >= 0)
        wanted = indexRequest;
    else if (cycleRequest != 0)
        wanted = (wanted < 0 ? 0 : wanted) + cycleRequest;
    else if (wanted < 0)
        wanted = 0;

    if (wanted != state.watchedIndex || !game::IsLiveUObject(state.watchedPlayer))
    {
        if (SelectPlayer(state, controller, wanted) && cycleRequest != 0)
        {
            QueueNotification(state,
                state.watchedName.empty() ? "[Spectator] Switched player" :
                                            "[Spectator] Watching " + state.watchedName,
                NotificationType::Info);
        }
    }
    else if (GetViewTarget(controller) != state.watchedPlayer)
    {
        SetViewTarget(controller, state.watchedPlayer);
    }
}
}

class SpectatorRuntime
{
public:
    static void Tick(Spectator& feature, const SDK::UObject* tickObject,
                     const float deltaSeconds)
    {
        RuntimeState& state = State();
        std::lock_guard lock(state.mutex);

        UWorld* world = ResolveWorld();
        APlayerController* controller = ResolveController(world);
        if (!world || !controller)
        {
            state.controllerReady = false;
            state.authority = false;
            if (state.world != world)
            {
                ClearRuntimeReferences(state);
                feature.runtimeActive_.store(false, std::memory_order_release);
                feature.cleanupPending_.store(false, std::memory_order_release);
            }
            return;
        }

        if (state.world != nullptr && state.world != world)
        {
            ClearRuntimeReferences(state);
            feature.runtimeActive_.store(false, std::memory_order_release);
            feature.cleanupPending_.store(false, std::memory_order_release);
        }
        state.world = world;
        state.controllerReady = true;
        state.authority = IsAuthority(controller);

        APawn* body = GetPawn(controller);
        if (tickObject != controller && tickObject != body)
            return;

        if (state.controller != nullptr && state.controller != controller && state.active)
        {
            RestoreControllerState(state);
            feature.runtimeActive_.store(false, std::memory_order_release);
        }

        const bool spectateWanted = feature.spectateDesired_.load(std::memory_order_acquire);
        const bool freeCamWanted = feature.freeCamDesired_.load(std::memory_order_acquire);
        const bool wanted = spectateWanted || freeCamWanted;
        if (state.active && (!wanted || state.freeCam != freeCamWanted))
        {
            RestoreControllerState(state);
            feature.runtimeActive_.store(false, std::memory_order_release);
            feature.cleanupPending_.store(false, std::memory_order_release);
            if (!wanted)
                QueueNotification(state, "[Spectator] Returned to body", NotificationType::Success);
        }

        // Spawning a camera or retargeting the view while a map is still streaming
        // in can corrupt the engine's async level-loading state (the "Retry was NOT
        // successful" fatal). Only enter once the current world has been fully
        // available for a moment and the local gameplay character is spawned. The
        // timer is tracked continuously on the render thread (see Spectator::Update)
        // so a hotkey press during normal play is not forced to wait it out.
        const ULONGLONG now = GetTickCount64();
        const bool worldStable = state.readySince != 0 &&
            state.readyWorldKey == reinterpret_cast<std::uintptr_t>(world) &&
            (now - state.readySince) >= 2000 &&
            game::IsLiveUObject(world->PersistentLevel) &&
            body != nullptr && IsObjectA(body, "BPCharacter_Demo_C");

        if (wanted && !state.active && worldStable)
        {
            if (now >= state.nextEnterAttempt)
            {
                const bool entered = freeCamWanted
                    ? EnterFreeCamera(state, world, controller, body)
                    : EnterPlayerWatch(state, world, controller, body);
                if (entered)
                {
                    state.nextEnterAttempt = 0;
                    feature.runtimeActive_.store(true, std::memory_order_release);
                }
                else
                {
                    state.nextEnterAttempt = now + 500;
                }
            }
        }

        const int cycle = feature.cycleRequest_.exchange(0, std::memory_order_acq_rel);
        const int index = feature.indexRequest_.exchange(-1, std::memory_order_acq_rel);
        if (!state.active)
            return;

        if (state.freeCam)
        {
            if (!game::IsLiveUObject(state.camera))
            {
                RestoreControllerState(state);
                feature.runtimeActive_.store(false, std::memory_order_release);
                state.nextEnterAttempt = GetTickCount64() + 250;
                return;
            }
            UpdateFreeCamera(
                state, controller,
                feature.configuredSpeed_.load(std::memory_order_relaxed),
                feature.configuredNoClip_.load(std::memory_order_relaxed),
                feature.gameplayInputBlocked_.load(std::memory_order_relaxed),
                deltaSeconds);
        }
        else
        {
            UpdatePlayerWatch(state, world, controller, body, cycle, index);
        }
    }
};

Spectator& Spectator::Instance()
{
    static Spectator instance;
    return instance;
}

void Spectator::Update(const bool inputBlocked)
{
    UWorld* world = ResolveWorld();
    APlayerController* controller = world != nullptr ? ResolveController(world) : nullptr;
    APawn* pawn = controller != nullptr ? GetPawn(controller) : nullptr;
    const SDK::UObject* tickObject = pawn != nullptr ? static_cast<SDK::UObject*>(pawn) :
                                     (controller != nullptr ? static_cast<SDK::UObject*>(controller) : nullptr);
    gameThreadTickObject_.store(tickObject, std::memory_order_release);

    // This runs every rendered frame, unlike SpectatorRuntime::Tick which only runs
    // once a mode has been requested. Track world stability here so the delay that
    // guards against retargeting the view mid-load has already elapsed by the time
    // the player presses F2/F3 during normal play.
    const bool worldReady = world != nullptr && game::IsLiveUObject(world->PersistentLevel);

    configuredNoClip_.store(settings_.freeCamNoClip, std::memory_order_relaxed);
    configuredSpeed_.store(std::clamp(settings_.freeCamSpeed, 100.0f, 10000.0f),
                           std::memory_order_relaxed);
    gameplayInputBlocked_.store(inputBlocked, std::memory_order_relaxed);

    const bool interact = !inputBlocked;

    // F2 toggles spectating teammates.
    if (interact && IsKeyJustPressed(settings_.spectateToggleKey, spectateToggleKeyDown_))
    {
        settings_.spectateEnabled = !settings_.spectateEnabled;
        if (settings_.spectateEnabled)
            settings_.freeCamEnabled = false;
    }
    else if (!interact)
    {
        IsKeyJustPressed(0, spectateToggleKeyDown_);
    }

    // F3 toggles the detached free camera.
    if (interact && IsKeyJustPressed(settings_.freeCamToggleKey, freeCamToggleKeyDown_))
    {
        settings_.freeCamEnabled = !settings_.freeCamEnabled;
        if (settings_.freeCamEnabled)
            settings_.spectateEnabled = false;
    }
    else if (!interact)
    {
        IsKeyJustPressed(0, freeCamToggleKeyDown_);
    }

    // The two modes are mutually exclusive; whichever was enabled last wins.
    if (settings_.spectateEnabled && settings_.freeCamEnabled)
        settings_.freeCamEnabled = false;

    const bool wasEnabled = spectateDesired_.load(std::memory_order_acquire) ||
                            freeCamDesired_.load(std::memory_order_acquire);
    spectateDesired_.store(settings_.spectateEnabled, std::memory_order_release);
    freeCamDesired_.store(settings_.freeCamEnabled, std::memory_order_release);
    const bool nowEnabled = settings_.spectateEnabled || settings_.freeCamEnabled;
    if (wasEnabled && !nowEnabled)
        cleanupPending_.store(runtimeActive_.load(std::memory_order_acquire),
                              std::memory_order_release);

    // Next / previous player only applies while spectating teammates, not the free camera.
    if (interact && settings_.spectateEnabled && !settings_.freeCamEnabled)
    {
        if (IsKeyJustPressed(settings_.spectateNextKey, nextKeyDown_))
            cycleRequest_.fetch_add(1, std::memory_order_release);
        if (IsKeyJustPressed(settings_.spectatePrevKey, prevKeyDown_))
            cycleRequest_.fetch_sub(1, std::memory_order_release);
    }
    else
    {
        IsKeyJustPressed(0, nextKeyDown_);
        IsKeyJustPressed(0, prevKeyDown_);
    }

    std::vector<std::pair<std::string, NotificationType>> pending;
    {
        RuntimeState& state = State();
        std::lock_guard lock(state.mutex);
        if (worldReady)
        {
            const auto worldKey = reinterpret_cast<std::uintptr_t>(world);
            if (state.readyWorldKey != worldKey)
            {
                state.readyWorldKey = worldKey;
                state.readySince = GetTickCount64();
            }
        }
        else
        {
            state.readyWorldKey = 0;
            state.readySince = 0;
        }
        pending.swap(state.notifications);
    }
    for (auto& [message, type] : pending)
        Notifications::Instance().Push(std::move(message), type);
}

void Spectator::OnGameThreadTick(const SDK::UObject* tickObject, const float deltaSeconds)
{
    if (tickObject == nullptr ||
        tickObject != gameThreadTickObject_.load(std::memory_order_acquire))
        return;
    SpectatorRuntime::Tick(*this, tickObject, deltaSeconds);
}

void Spectator::SetGameThreadHookReady(const bool ready) noexcept
{
    gameThreadHookReady_.store(ready, std::memory_order_release);
}

bool Spectator::NeedsGameThreadTick() const noexcept
{
    return gameThreadTickObject_.load(std::memory_order_acquire) != nullptr &&
           (spectateDesired_.load(std::memory_order_acquire) ||
            freeCamDesired_.load(std::memory_order_acquire) ||
            runtimeActive_.load(std::memory_order_acquire) ||
            cleanupPending_.load(std::memory_order_acquire));
}

SpectatorStatus Spectator::Status() const noexcept
{
    SpectatorStatus result{};
    result.gameThreadHookReady = gameThreadHookReady_.load(std::memory_order_acquire);
    RuntimeState& state = State();
    std::lock_guard lock(state.mutex);
    result.controllerReady = state.controllerReady;
    result.spectatorPawnReady = state.freeCam ? state.camera != nullptr :
                                                state.watchedPlayer != nullptr;
    result.isSpectating = state.active;
    result.isFreeCam = state.active && state.freeCam;
    result.authority = state.authority;
    result.playerCount = state.players.size();
    result.currentSpectateIndex = state.watchedIndex;
    result.currentPlayerName = state.watchedName;
    return result;
}

bool Spectator::EnterSpectator()
{
    settings_.spectateEnabled = true;
    settings_.freeCamEnabled = false;
    spectateDesired_.store(true, std::memory_order_release);
    freeCamDesired_.store(false, std::memory_order_release);
    return gameThreadHookReady_.load(std::memory_order_acquire);
}

bool Spectator::ExitSpectator()
{
    settings_.spectateEnabled = false;
    settings_.freeCamEnabled = false;
    spectateDesired_.store(false, std::memory_order_release);
    freeCamDesired_.store(false, std::memory_order_release);
    cleanupPending_.store(runtimeActive_.load(std::memory_order_acquire),
                          std::memory_order_release);
    return true;
}

bool Spectator::ToggleSpectator()
{
    return (spectateDesired_.load(std::memory_order_acquire) ||
            freeCamDesired_.load(std::memory_order_acquire))
        ? ExitSpectator()
        : EnterSpectator();
}

bool Spectator::SpectateNext()
{
    if (!runtimeActive_.load(std::memory_order_acquire) ||
        freeCamDesired_.load(std::memory_order_acquire))
        return false;
    cycleRequest_.fetch_add(1, std::memory_order_release);
    return true;
}

bool Spectator::SpectatePrevious()
{
    if (!runtimeActive_.load(std::memory_order_acquire) ||
        freeCamDesired_.load(std::memory_order_acquire))
        return false;
    cycleRequest_.fetch_sub(1, std::memory_order_release);
    return true;
}

bool Spectator::SpectateIndex(const int index)
{
    if (index < 0 || !runtimeActive_.load(std::memory_order_acquire) ||
        freeCamDesired_.load(std::memory_order_acquire))
        return false;
    indexRequest_.store(index, std::memory_order_release);
    return true;
}

bool Spectator::EnterFreeCam()
{
    settings_.freeCamEnabled = true;
    settings_.spectateEnabled = false;
    freeCamDesired_.store(true, std::memory_order_release);
    spectateDesired_.store(false, std::memory_order_release);
    return gameThreadHookReady_.load(std::memory_order_acquire);
}

bool Spectator::ExitFreeCam()
{
    if (!runtimeActive_.load(std::memory_order_acquire) ||
        !freeCamDesired_.load(std::memory_order_acquire))
        return false;
    return ExitSpectator();
}

void Spectator::Reset()
{
    settings_.spectateEnabled = false;
    settings_.freeCamEnabled = false;
    spectateDesired_.store(false, std::memory_order_release);
    freeCamDesired_.store(false, std::memory_order_release);
    runtimeActive_.store(false, std::memory_order_release);
    cleanupPending_.store(false, std::memory_order_release);
    gameThreadTickObject_.store(nullptr, std::memory_order_release);
    cycleRequest_.store(0, std::memory_order_release);
    indexRequest_.store(-1, std::memory_order_release);
    spectateToggleKeyDown_ = false;
    freeCamToggleKeyDown_ = false;
    nextKeyDown_ = false;
    prevKeyDown_ = false;

    RuntimeState& state = State();
    std::lock_guard lock(state.mutex);
    ClearRuntimeReferences(state);
    state.notifications.clear();
}
}


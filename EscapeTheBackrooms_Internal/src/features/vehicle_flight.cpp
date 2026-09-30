#include "features/vehicle_flight.hpp"

#include "core/logger.hpp"
#include "game/unreal_safety.hpp"

#include <SDK/BP_RowBoat_classes.hpp>
#include <SDK/Backrooms_parameters.hpp>
#include <SDK/Engine_parameters.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace etb::features
{
namespace
{
using Clock = std::chrono::steady_clock;
using game::IsLiveUObject;
using game::IsReadable;

enum InputBit : std::uint32_t
{
    InputForward = 1u << 0,
    InputBackward = 1u << 1,
    InputLeft = 1u << 2,
    InputRight = 1u << 3,
    InputUp = 1u << 4,
    InputDown = 1u << 5,
    InputTurnLeft = 1u << 6,
    InputTurnRight = 1u << 7,
    InputBoost = 1u << 8,
};

struct FunctionCache
{
    SDK::UClass* objectClass = nullptr;
    SDK::UFunction* function = nullptr;
};

struct BoatOriginal
{
    bool gravityEnabled = true;
    SDK::ECollisionEnabled collision = SDK::ECollisionEnabled::QueryAndPhysics;
    bool syncMovement = true;
    bool forceReplication = false;
    std::vector<bool> floaters;
    std::vector<bool> engines;
};

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

bool Invoke(SDK::UObject* object, FunctionCache& cache, const char* functionName, void* parameters)
{
    if (!IsLiveUObject(object))
        return false;
    if (cache.objectClass != object->Class || !IsLiveUObject(cache.function))
    {
        cache.objectClass = object->Class;
        cache.function = FindFunction(object, functionName);
    }
    if (!game::CanProcessEvent(object, cache.function))
        return false;

    const auto originalFlags = cache.function->FunctionFlags;
    cache.function->FunctionFlags |= 0x400;
    const bool invoked = game::ProcessEventSafe(object, cache.function, parameters);
    if (IsLiveUObject(cache.function))
        cache.function->FunctionFlags = originalFlags;
    return invoked;
}

bool IsBoatClass(const SDK::UObject* object)
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
            if (name == "BoatPawn" || name == "BP_RowBoat_C")
                return true;
        }
    }
    catch (...)
    {
    }
    return false;
}

SDK::UWorld* ResolveWorld()
{
    const std::uintptr_t base = SDK::InSDKUtils::GetImageBase();
    auto** worldAddress = reinterpret_cast<SDK::UWorld**>(base + SDK::Offsets::GWorld);
    if (!IsReadable(worldAddress, sizeof(*worldAddress)))
        return nullptr;
    SDK::UWorld* world = *worldAddress;
    return IsReadable(world, 0x188) && IsLiveUObject(world) ? world : nullptr;
}

SDK::APlayerController* ResolveLocalController()
{
    SDK::UWorld* world = ResolveWorld();
    if (world == nullptr)
        return nullptr;
    SDK::UGameInstance* gameInstance = world->OwningGameInstance;
    if (!IsReadable(gameInstance, 0x48) || !IsLiveUObject(gameInstance))
        return nullptr;
    const auto players = gameInstance->LocalPlayers;
    if (!players.IsValid() || players.Num() <= 0 || players.Num() > 8 ||
        !IsReadable(players.GetDataPtr(), sizeof(SDK::ULocalPlayer*) * players.Num()))
        return nullptr;
    SDK::ULocalPlayer* player = players.GetDataPtr()[0];
    if (!IsReadable(player, 0x38) || !IsLiveUObject(player))
        return nullptr;
    SDK::APlayerController* controller = player->PlayerController;
    return IsReadable(controller, sizeof(SDK::APlayerController)) && IsLiveUObject(controller)
        ? controller : nullptr;
}

SDK::ABoatPawn* ResolveControlledBoat(SDK::APlayerController*& controller)
{
    controller = ResolveLocalController();
    if (controller == nullptr)
        return nullptr;
    SDK::APawn* pawn = controller->AcknowledgedPawn;
    if (!IsLiveUObject(pawn) || !IsBoatClass(pawn))
        pawn = controller->Pawn;
    if (!IsReadable(pawn, sizeof(SDK::ABoatPawn)) || !IsLiveUObject(pawn) || !IsBoatClass(pawn))
        return nullptr;
    return reinterpret_cast<SDK::ABoatPawn*>(pawn);
}

bool ReadTransform(SDK::ABoatPawn* boat, SDK::FVector& location, SDK::FRotator& rotation)
{
    static FunctionCache locationFunction;
    static FunctionCache rotationFunction;
    SDK::Params::Actor_K2_GetActorLocation locationParameters{};
    SDK::Params::Actor_K2_GetActorRotation rotationParameters{};
    if (!Invoke(boat, locationFunction, "K2_GetActorLocation", &locationParameters) ||
        !Invoke(boat, rotationFunction, "K2_GetActorRotation", &rotationParameters))
        return false;
    location = locationParameters.ReturnValue;
    rotation = rotationParameters.ReturnValue;
    return std::isfinite(location.X) && std::isfinite(location.Y) && std::isfinite(location.Z) &&
           std::isfinite(rotation.Pitch) && std::isfinite(rotation.Yaw) && std::isfinite(rotation.Roll);
}

__declspec(noinline) bool TryReadViewRotation(SDK::APlayerController* controller,
                                               SDK::FRotator* rotation) noexcept
{
    if (controller == nullptr || rotation == nullptr)
        return false;

    __try
    {
        SDK::APlayerCameraManager* cameraManager = controller->PlayerCameraManager;
        if (cameraManager == nullptr)
            return false;

        const SDK::FRotator value = cameraManager->CameraCachePrivate.POV.Rotation;
        if (!std::isfinite(value.Pitch) || !std::isfinite(value.Yaw) ||
            !std::isfinite(value.Roll))
            return false;

        *rotation = value;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool WriteTransform(SDK::ABoatPawn* boat, const SDK::FVector& location,
                    const SDK::FRotator& rotation, bool noClip)
{
    static FunctionCache function;
    SDK::Params::Actor_K2_SetActorLocationAndRotation parameters{};
    parameters.NewLocation = location;
    parameters.NewRotation = rotation;
    parameters.bSweep = !noClip;
    parameters.bTeleport = true;
    return Invoke(boat, function, "K2_SetActorLocationAndRotation", &parameters) &&
           parameters.ReturnValue;
}

bool ReadGravity(SDK::UPrimitiveComponent* root, bool& value)
{
    static FunctionCache function;
    SDK::Params::PrimitiveComponent_IsGravityEnabled parameters{};
    if (!Invoke(root, function, "IsGravityEnabled", &parameters))
        return false;
    value = parameters.ReturnValue;
    return true;
}

bool ReadCollision(SDK::UPrimitiveComponent* root, SDK::ECollisionEnabled& value)
{
    static FunctionCache function;
    SDK::Params::PrimitiveComponent_GetCollisionEnabled parameters{};
    if (!Invoke(root, function, "GetCollisionEnabled", &parameters))
        return false;
    value = parameters.ReturnValue;
    return true;
}

bool SetGravity(SDK::UPrimitiveComponent* root, bool value)
{
    static FunctionCache function;
    SDK::Params::PrimitiveComponent_SetEnableGravity parameters{};
    parameters.bGravityEnabled = value;
    return Invoke(root, function, "SetEnableGravity", &parameters);
}

bool SetCollision(SDK::UPrimitiveComponent* root, SDK::ECollisionEnabled value)
{
    static FunctionCache function;
    SDK::Params::PrimitiveComponent_SetCollisionEnabled parameters{};
    parameters.NewType = value;
    return Invoke(root, function, "SetCollisionEnabled", &parameters);
}

bool SetLinearVelocity(SDK::UPrimitiveComponent* root, const SDK::FVector& value)
{
    static FunctionCache function;
    SDK::Params::PrimitiveComponent_SetPhysicsLinearVelocity parameters{};
    parameters.NewVel = value;
    parameters.bAddToCurrent = false;
    return Invoke(root, function, "SetPhysicsLinearVelocity", &parameters);
}

bool SetAngularVelocity(SDK::UPrimitiveComponent* root, const SDK::FVector& value)
{
    static FunctionCache function;
    SDK::Params::PrimitiveComponent_SetPhysicsAngularVelocityInDegrees parameters{};
    parameters.NewAngVel = value;
    parameters.bAddToCurrent = false;
    return Invoke(root, function, "SetPhysicsAngularVelocityInDegrees", &parameters);
}

bool SendMovement(SDK::UBoatComponent* component, const SDK::FRepXShipMovement& movement)
{
    static FunctionCache function;
    SDK::Params::BoatComponent_Server_PassMovementInfo parameters{};
    parameters.NewRepXShipMovement = movement;
    return Invoke(component, function, "Server_PassMovementInfo", &parameters);
}

void AssignNetVector(SDK::FVector_NetQuantize& destination, const SDK::FVector& source)
{
    destination.X = source.X;
    destination.Y = source.Y;
    destination.Z = source.Z;
}

bool ValidArrayCount(int count)
{
    return count >= 0 && count <= 64;
}

}

class VehicleRuntime final
{
public:
    void Tick(VehicleFlight& feature, const SDK::UObject* tickObject, float deltaSeconds)
    {
        const auto now = Clock::now();
        if (nextControllerResolve_.time_since_epoch().count() == 0 ||
            now >= nextControllerResolve_)
        {
            cachedControlledBoat_ = ResolveControlledBoat(cachedController_);
            nextControllerResolve_ = now + std::chrono::milliseconds(50);
            feature.vehicleDetected_.store(cachedControlledBoat_ != nullptr,
                                           std::memory_order_release);
        }

        SDK::ABoatPawn* controlledBoat = cachedControlledBoat_;
        SDK::APlayerController* controller = cachedController_;

        if (activeBoat_ != nullptr &&
            (!IsLiveUObject(activeBoat_) || activeBoat_->Index != activeBoatIndex_))
        {
            Abandon(feature);
        }

        if (tickObject != controlledBoat && tickObject != activeBoat_)
            return;

        if (activeBoat_ != nullptr && activeBoat_ != controlledBoat && tickObject == activeBoat_)
        {
            Restore(feature);
            return;
        }

        if (controlledBoat == nullptr || tickObject != controlledBoat)
            return;

        const bool enabled = feature.configuredEnabled_.load(std::memory_order_acquire) ||
                             feature.configuredNoClip_.load(std::memory_order_acquire);
        if (!enabled)
        {
            if (activeBoat_ == controlledBoat)
                Restore(feature);
            return;
        }

        if (activeBoat_ != controlledBoat)
        {
            if (!Attach(feature, controlledBoat))
                return;
        }

        if (!ValidateAttached())
        {
            Abandon(feature);
            return;
        }

        const float dt = std::clamp(std::isfinite(deltaSeconds) ? deltaSeconds : 0.0f,
                                    1.0f / 240.0f, 0.050f);
        Apply(feature, controller, dt);
    }

private:
    bool Attach(VehicleFlight& feature, SDK::ABoatPawn* boat)
    {
        if (!IsReadable(boat, sizeof(SDK::ABoatPawn)) || !IsLiveUObject(boat))
            return false;
        SDK::UBoxComponent* root = boat->RootBoxComponent;
        SDK::UBoatComponent* component = boat->BoatComponent;
        if (!IsReadable(root, sizeof(SDK::UBoxComponent)) || !IsLiveUObject(root) ||
            !IsReadable(component, sizeof(SDK::UBoatComponent)) || !IsLiveUObject(component))
            return false;

        BoatOriginal original{};
        if (!ReadGravity(root, original.gravityEnabled) || !ReadCollision(root, original.collision))
            return false;
        original.syncMovement = component->bSyncMovement;
        original.forceReplication = component->bForceReplication;

        const auto floaters = component->XShipFloaters;
        const auto engines = component->XShipEngines;
        if (!floaters.IsValid() || !engines.IsValid() ||
            !ValidArrayCount(floaters.Num()) || !ValidArrayCount(engines.Num()) ||
            (floaters.Num() > 0 &&
             !IsReadable(floaters.GetDataPtr(), sizeof(SDK::FXShipFloater) * floaters.Num())) ||
            (engines.Num() > 0 &&
             !IsReadable(engines.GetDataPtr(), sizeof(SDK::FXShipEngine) * engines.Num())))
            return false;

        original.floaters.reserve(static_cast<std::size_t>(floaters.Num()));
        for (int index = 0; index < floaters.Num(); ++index)
            original.floaters.push_back(floaters.GetDataPtr()[index].bEnabled);
        original.engines.reserve(static_cast<std::size_t>(engines.Num()));
        for (int index = 0; index < engines.Num(); ++index)
            original.engines.push_back(engines.GetDataPtr()[index].bEnabled);

        activeBoat_ = boat;
        activeBoatIndex_ = boat->Index;
        root_ = root;
        rootIndex_ = root->Index;
        component_ = component;
        componentIndex_ = component->Index;
        original_ = std::move(original);
        collisionDisabled_ = false;
        lastNetworkSend_ = {};

        SetGravity(root_, false);
        DisableForces();
        component_->bSyncMovement = true;
        component_->bForceReplication = true;
        feature.runtimeActive_.store(true, std::memory_order_release);
        feature.authority_.store(boat->Role == SDK::ENetRole::ROLE_Authority,
                                 std::memory_order_release);
        core::Logf("vehicle flight attached to Boat=%p Root=%p Component=%p", boat, root, component);
        return true;
    }

    bool ValidateAttached() const
    {
        return IsReadable(activeBoat_, sizeof(SDK::ABoatPawn)) && IsLiveUObject(activeBoat_) &&
               activeBoat_->Index == activeBoatIndex_ &&
               IsReadable(root_, sizeof(SDK::UBoxComponent)) && IsLiveUObject(root_) &&
               root_->Index == rootIndex_ && activeBoat_->RootBoxComponent == root_ &&
               IsReadable(component_, sizeof(SDK::UBoatComponent)) && IsLiveUObject(component_) &&
               component_->Index == componentIndex_ && activeBoat_->BoatComponent == component_;
    }

    void DisableForces()
    {
        auto floaters = component_->XShipFloaters;
        if (floaters.IsValid() && ValidArrayCount(floaters.Num()) &&
            (floaters.Num() == 0 ||
             IsReadable(floaters.GetDataPtr(), sizeof(SDK::FXShipFloater) * floaters.Num())))
        {
            auto* data = const_cast<SDK::FXShipFloater*>(floaters.GetDataPtr());
            for (int index = 0; index < floaters.Num(); ++index)
                data[index].bEnabled = false;
        }
        auto engines = component_->XShipEngines;
        if (engines.IsValid() && ValidArrayCount(engines.Num()) &&
            (engines.Num() == 0 ||
             IsReadable(engines.GetDataPtr(), sizeof(SDK::FXShipEngine) * engines.Num())))
        {
            auto* data = const_cast<SDK::FXShipEngine*>(engines.GetDataPtr());
            for (int index = 0; index < engines.Num(); ++index)
                data[index].bEnabled = false;
        }
        component_->MovementInput = {};
        component_->RotationInput = 0.0f;
    }

    void Apply(VehicleFlight& feature, SDK::APlayerController* controller, float dt)
    {
        const bool noClip = feature.configuredNoClip_.load(std::memory_order_acquire);
        if (noClip != collisionDisabled_)
        {
            SetCollision(root_, noClip ? SDK::ECollisionEnabled::NoCollision : original_.collision);
            collisionDisabled_ = noClip;
        }

        SetGravity(root_, false);
        DisableForces();

        SDK::FVector location{};
        SDK::FRotator rotation{};
        if (!ReadTransform(activeBoat_, location, rotation))
            return;

        const std::uint32_t input = feature.inputMask_.load(std::memory_order_acquire);
        const float forwardInput = ((input & InputForward) != 0 ? 1.0f : 0.0f) -
                                   ((input & InputBackward) != 0 ? 1.0f : 0.0f);
        const float rightInput = ((input & InputRight) != 0 ? 1.0f : 0.0f) -
                                 ((input & InputLeft) != 0 ? 1.0f : 0.0f);
        const float upInput = ((input & InputUp) != 0 ? 1.0f : 0.0f) -
                              ((input & InputDown) != 0 ? 1.0f : 0.0f);
        const float turnInput = ((input & InputTurnRight) != 0 ? 1.0f : 0.0f) -
                                ((input & InputTurnLeft) != 0 ? 1.0f : 0.0f);
        const float boost = (input & InputBoost) != 0
            ? feature.configuredBoostMultiplier_.load(std::memory_order_relaxed) : 1.0f;

        SDK::FRotator viewRotation = rotation;
        if (!TryReadViewRotation(controller, &viewRotation) &&
            controller != nullptr && IsLiveUObject(controller))
        {
            viewRotation = controller->ControlRotation;
        }

        constexpr float DegreesToRadians = 3.14159265358979323846f / 180.0f;
        const float pitch = viewRotation.Pitch * DegreesToRadians;
        const float yaw = viewRotation.Yaw * DegreesToRadians;
        const float cosPitch = std::cos(pitch);
        const float sinPitch = std::sin(pitch);
        const float cosYaw = std::cos(yaw);
        const float sinYaw = std::sin(yaw);
        const SDK::FVector forward{cosPitch * cosYaw, cosPitch * sinYaw, sinPitch};
        const SDK::FVector right{-sinYaw, cosYaw, 0.0f};

        float planarLength = std::sqrt(forwardInput * forwardInput + rightInput * rightInput);
        const float normalization = planarLength > 1.0f ? 1.0f / planarLength : 1.0f;
        const float horizontalSpeed = feature.configuredHorizontalSpeed_.load(std::memory_order_relaxed) * boost;
        const float verticalSpeed = feature.configuredVerticalSpeed_.load(std::memory_order_relaxed) * boost;
        SDK::FVector velocity{};
        velocity.X = (forward.X * forwardInput + right.X * rightInput) * normalization * horizontalSpeed;
        velocity.Y = (forward.Y * forwardInput + right.Y * rightInput) * normalization * horizontalSpeed;
        velocity.Z = forward.Z * forwardInput * normalization * horizontalSpeed +
                     upInput * verticalSpeed;

        location.X += velocity.X * dt;
        location.Y += velocity.Y * dt;
        location.Z += velocity.Z * dt;
        rotation.Pitch = 0.0f;
        rotation.Roll = 0.0f;
        rotation.Yaw += turnInput * feature.configuredTurnSpeed_.load(std::memory_order_relaxed) * dt;

        if (!WriteTransform(activeBoat_, location, rotation, noClip))
            return;
        SetLinearVelocity(root_, velocity);
        SetAngularVelocity(root_, {});

        SDK::FRepXShipMovement replicated{};
        AssignNetVector(replicated.LinearVelocity, velocity);
        AssignNetVector(replicated.AngularVelocity, {});
        AssignNetVector(replicated.Location, location);
        replicated.Rotation = rotation;
        component_->RepXShipMovement = replicated;

        const bool authority = activeBoat_->Role == SDK::ENetRole::ROLE_Authority;
        feature.authority_.store(authority, std::memory_order_release);
        bool sent = authority;
        const float networkRate = feature.configuredNetworkRateHz_.load(std::memory_order_relaxed);
        const auto now = Clock::now();
        const float interval = 1.0f / std::max(networkRate, 1.0f);
        if (!authority &&
            (lastNetworkSend_.time_since_epoch().count() == 0 ||
             std::chrono::duration<float>(now - lastNetworkSend_).count() >= interval))
        {
            sent = SendMovement(component_, replicated);
            if (sent)
                lastNetworkSend_ = now;
        }
        feature.networkSync_.store(sent, std::memory_order_release);
        const float speed = std::sqrt(velocity.X * velocity.X + velocity.Y * velocity.Y +
                                      velocity.Z * velocity.Z) / 100.0f;
        feature.speedMetersPerSecond_.store(speed, std::memory_order_release);
    }

    void Restore(VehicleFlight& feature)
    {
        if (ValidateAttached())
        {
            SetLinearVelocity(root_, {});
            SetAngularVelocity(root_, {});
            SetGravity(root_, original_.gravityEnabled);
            SetCollision(root_, original_.collision);
            component_->bSyncMovement = original_.syncMovement;
            component_->bForceReplication = original_.forceReplication;

            auto floaters = component_->XShipFloaters;
            if (floaters.IsValid() && ValidArrayCount(floaters.Num()) &&
                static_cast<std::size_t>(floaters.Num()) == original_.floaters.size() &&
                (floaters.Num() == 0 ||
                 IsReadable(floaters.GetDataPtr(), sizeof(SDK::FXShipFloater) * floaters.Num())))
            {
                auto* data = const_cast<SDK::FXShipFloater*>(floaters.GetDataPtr());
                for (int index = 0; index < floaters.Num(); ++index)
                    data[index].bEnabled = original_.floaters[index];
            }
            auto engines = component_->XShipEngines;
            if (engines.IsValid() && ValidArrayCount(engines.Num()) &&
                static_cast<std::size_t>(engines.Num()) == original_.engines.size() &&
                (engines.Num() == 0 ||
                 IsReadable(engines.GetDataPtr(), sizeof(SDK::FXShipEngine) * engines.Num())))
            {
                auto* data = const_cast<SDK::FXShipEngine*>(engines.GetDataPtr());
                for (int index = 0; index < engines.Num(); ++index)
                    data[index].bEnabled = original_.engines[index];
            }
        }
        core::Log("vehicle flight detached");
        Clear(feature);
    }

    void Abandon(VehicleFlight& feature)
    {
        core::Log("vehicle flight abandoned stale boat during travel");
        Clear(feature);
    }

    void Clear(VehicleFlight& feature)
    {
        activeBoat_ = nullptr;
        activeBoatIndex_ = -1;
        root_ = nullptr;
        rootIndex_ = -1;
        component_ = nullptr;
        componentIndex_ = -1;
        original_ = {};
        collisionDisabled_ = false;
        lastNetworkSend_ = {};
        feature.runtimeActive_.store(false, std::memory_order_release);
        feature.authority_.store(false, std::memory_order_release);
        feature.networkSync_.store(false, std::memory_order_release);
        feature.speedMetersPerSecond_.store(0.0f, std::memory_order_release);
    }

    SDK::ABoatPawn* activeBoat_ = nullptr;
    std::int32_t activeBoatIndex_ = -1;
    SDK::UBoxComponent* root_ = nullptr;
    std::int32_t rootIndex_ = -1;
    SDK::UBoatComponent* component_ = nullptr;
    std::int32_t componentIndex_ = -1;
    BoatOriginal original_{};
    bool collisionDisabled_ = false;
    Clock::time_point lastNetworkSend_{};
    Clock::time_point nextControllerResolve_{};
    SDK::APlayerController* cachedController_ = nullptr;
    SDK::ABoatPawn* cachedControlledBoat_ = nullptr;
};

namespace
{
VehicleRuntime g_runtime;
}

VehicleFlight& VehicleFlight::Instance()
{
    static VehicleFlight instance;
    return instance;
}

void VehicleFlight::UpdateInput(const bool inputBlocked)
{
    const auto keyDown = [](const int key)
    {
        return key > 0 && key < 256 && (GetAsyncKeyState(key) & 0x8000) != 0;
    };

    const bool toggleDown = keyDown(settings_.toggleKey);
    const bool noClipDown = keyDown(settings_.noClipToggleKey);
    if (!inputBlocked)
    {
        if (toggleDown && !toggleKeyDown_)
            settings_.enabled = !settings_.enabled;
        if (noClipDown && !noClipKeyDown_)
            settings_.noClip = !settings_.noClip;
    }
    toggleKeyDown_ = toggleDown;
    noClipKeyDown_ = noClipDown;

    configuredEnabled_.store(settings_.enabled, std::memory_order_release);
    configuredNoClip_.store(settings_.noClip, std::memory_order_release);
    configuredHorizontalSpeed_.store(std::clamp(settings_.horizontalSpeed, 50.0f, 10000.0f),
                                     std::memory_order_relaxed);
    configuredVerticalSpeed_.store(std::clamp(settings_.verticalSpeed, 50.0f, 10000.0f),
                                   std::memory_order_relaxed);
    configuredTurnSpeed_.store(std::clamp(settings_.turnSpeedDegrees, 0.0f, 360.0f),
                               std::memory_order_relaxed);
    configuredBoostMultiplier_.store(std::clamp(settings_.boostMultiplier, 1.0f, 5.0f),
                                     std::memory_order_relaxed);
    configuredNetworkRateHz_.store(std::clamp(settings_.networkRateHz, 5.0f, 60.0f),
                                   std::memory_order_relaxed);

    std::uint32_t mask = 0;
    if (!inputBlocked)
    {
        if (keyDown('W')) mask |= InputForward;
        if (keyDown('S')) mask |= InputBackward;
        if (keyDown('A')) mask |= InputLeft;
        if (keyDown('D')) mask |= InputRight;
        if (keyDown(VK_SPACE)) mask |= InputUp;
        if (keyDown(VK_CONTROL)) mask |= InputDown;
        if (keyDown('Q')) mask |= InputTurnLeft;
        if (keyDown('E')) mask |= InputTurnRight;
        if (keyDown(VK_SHIFT)) mask |= InputBoost;
    }
    inputMask_.store(mask, std::memory_order_release);
}

void VehicleFlight::OnGameThreadTick(const SDK::UObject* tickObject, const float deltaSeconds)
{
    g_runtime.Tick(*this, tickObject, deltaSeconds);
}

void VehicleFlight::RequestRestore() noexcept
{
    settings_.enabled = false;
    settings_.noClip = false;
    configuredEnabled_.store(false, std::memory_order_release);
    configuredNoClip_.store(false, std::memory_order_release);
    inputMask_.store(0, std::memory_order_release);
}

void VehicleFlight::Reset() noexcept
{
    RequestRestore();
    settings_.noClip = false;
    configuredNoClip_.store(false, std::memory_order_release);
    vehicleDetected_.store(false, std::memory_order_release);
    hookReady_.store(false, std::memory_order_release);
}

void VehicleFlight::SetGameThreadHookReady(const bool ready) noexcept
{
    hookReady_.store(ready, std::memory_order_release);
}

bool VehicleFlight::NeedsGameThreadTick() const noexcept
{
    return configuredEnabled_.load(std::memory_order_acquire) ||
           configuredNoClip_.load(std::memory_order_acquire) ||
           runtimeActive_.load(std::memory_order_acquire);
}

bool VehicleFlight::HasActiveVehicle() const noexcept
{
    return runtimeActive_.load(std::memory_order_acquire);
}

VehicleFlightStatus VehicleFlight::Status() const noexcept
{
    return {
        hookReady_.load(std::memory_order_acquire),
        vehicleDetected_.load(std::memory_order_acquire),
        runtimeActive_.load(std::memory_order_acquire),
        authority_.load(std::memory_order_acquire),
        networkSync_.load(std::memory_order_acquire),
        speedMetersPerSecond_.load(std::memory_order_acquire),
    };
}
}

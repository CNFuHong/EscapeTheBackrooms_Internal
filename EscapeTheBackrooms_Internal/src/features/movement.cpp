#include "features/movement.hpp"

#include "core/logger.hpp"
#include "game/unreal_safety.hpp"

#include <Windows.h>
#include <SDK/BPCharacter_Demo_classes.hpp>
#include <SDK/Backrooms_classes.hpp>
#include <SDK/Backrooms_parameters.hpp>
#include <SDK/Engine_parameters.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace etb::features
{
namespace
{
struct OriginalValues
{
    float componentWalkSpeed = 0.0f;
    float componentSprintSpeed = 0.0f;
    float componentCrouchSpeed = 0.0f;
    float jumpVelocity = 0.0f;
    float gravityScale = 0.0f;
    float flySpeed = 0.0f;
    float flightGravityScale = 0.0f;
    float blueprintWalkSpeed = 0.0f;
    float blueprintSprintSpeed = 0.0f;
    float blueprintCrouchSpeed = 0.0f;
    float airControl = 0.0f;
    float airControlBoostMultiplier = 0.0f;
    float airControlBoostVelocityThreshold = 0.0f;
    float maxAcceleration = 0.0f;
    float networkMaxSmoothUpdateDistance = 0.0f;
    float networkNoSmoothUpdateDistance = 0.0f;
    std::uint8_t ignoreClientMovementErrors = 0;
    std::uint8_t serverAcceptClientPosition = 0;
    SDK::EMovementMode movementMode = SDK::EMovementMode::MOVE_Walking;
    std::uint8_t customMovementMode = 0;
    std::array<SDK::ECollisionResponse, 5> noClipResponses{};
    std::array<bool, 5> noClipResponseValid{};
    SDK::FName collisionProfileName{};
    bool collisionProfileValid = false;
};

constexpr std::array<SDK::ECollisionChannel, 5> NoClipChannels{
    SDK::ECollisionChannel::ECC_WorldStatic,
    SDK::ECollisionChannel::ECC_WorldDynamic,
    SDK::ECollisionChannel::ECC_PhysicsBody,
    SDK::ECollisionChannel::ECC_Vehicle,
    SDK::ECollisionChannel::ECC_Destructible,
};

constexpr std::array<SDK::ECollisionResponse, 5> NoClipResponses{
    SDK::ECollisionResponse::ECR_Ignore,  // 静态墙体和阻挡体
    SDK::ECollisionResponse::ECR_Overlap, // 动态出口/交互触发器
    SDK::ECollisionResponse::ECR_Overlap,
    SDK::ECollisionResponse::ECR_Overlap,
    SDK::ECollisionResponse::ECR_Overlap,
};

using game::IsLiveUObject;
using game::IsReadable;

SDK::UWorld* ResolveWorld()
{
    const uintptr_t base = SDK::InSDKUtils::GetImageBase();
    auto** worldAddress = reinterpret_cast<SDK::UWorld**>(base + SDK::Offsets::GWorld);
    if (!IsReadable(worldAddress, sizeof(*worldAddress)))
        return nullptr;

    SDK::UWorld* world = *worldAddress;
    if (!IsReadable(world, 0x188) || !IsLiveUObject(world))
        return nullptr;

    return world;
}

SDK::APawn* ResolveLocalPawn(SDK::UWorld* world)
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
    return IsReadable(pawn, sizeof(SDK::ABPCharacter_Demo_C)) && IsLiveUObject(pawn) ? pawn : nullptr;
}

SDK::APawn* ResolveLocalPawn()
{
    return ResolveLocalPawn(ResolveWorld());
}

bool IsCharacterClass(const SDK::UObject* object)
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
            if (name == "BPCharacter_Demo_C" || name == "Character")
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

std::optional<SDK::ECollisionResponse> GetCollisionResponse(
    SDK::UPrimitiveComponent* component,
    SDK::ECollisionChannel channel)
{
    static SDK::UClass* cachedClass = nullptr;
    static SDK::UFunction* function = nullptr;
    if (!IsLiveUObject(component))
        return std::nullopt;

    if (component->Class != cachedClass)
    {
        cachedClass = component->Class;
        function = FindFunction(component, "GetCollisionResponseToChannel");
    }
    if (!game::CanProcessEvent(component, function))
        return std::nullopt;

    SDK::Params::PrimitiveComponent_GetCollisionResponseToChannel parameters{};
    parameters.Channel = channel;
    const auto flags = function->FunctionFlags;
    function->FunctionFlags |= 0x400;
    const bool invoked = game::ProcessEventSafe(component, function, &parameters);
    if (IsLiveUObject(function))
        function->FunctionFlags = flags;
    return invoked ? std::optional{parameters.ReturnValue} : std::nullopt;
}

bool SetCollisionResponse(
    SDK::UPrimitiveComponent* component,
    SDK::ECollisionChannel channel,
    SDK::ECollisionResponse response)
{
    static SDK::UClass* cachedClass = nullptr;
    static SDK::UFunction* function = nullptr;
    if (!IsLiveUObject(component))
        return false;

    if (component->Class != cachedClass)
    {
        cachedClass = component->Class;
        function = FindFunction(component, "SetCollisionResponseToChannel");
    }
    if (!game::CanProcessEvent(component, function))
        return false;

    SDK::Params::PrimitiveComponent_SetCollisionResponseToChannel parameters{};
    parameters.Channel = channel;
    parameters.NewResponse = response;
    const auto flags = function->FunctionFlags;
    function->FunctionFlags |= 0x400;
    const bool invoked = game::ProcessEventSafe(component, function, &parameters);
    if (IsLiveUObject(function))
        function->FunctionFlags = flags;
    return invoked;
}

bool SetCollisionProfile(SDK::UPrimitiveComponent* component, const SDK::FName& profile)
{
    static SDK::UClass* cachedClass = nullptr;
    static SDK::UFunction* function = nullptr;
    if (!IsLiveUObject(component) || profile.IsNone())
        return false;

    if (component->Class != cachedClass)
    {
        cachedClass = component->Class;
        function = FindFunction(component, "SetCollisionProfileName");
    }
    if (!game::CanProcessEvent(component, function))
        return false;

    SDK::Params::PrimitiveComponent_SetCollisionProfileName parameters{};
    parameters.InCollisionProfileName = profile;
    parameters.bUpdateOverlaps = true;
    const auto flags = function->FunctionFlags;
    function->FunctionFlags |= 0x400;
    const bool invoked = game::ProcessEventSafe(component, function, &parameters);
    if (IsLiveUObject(function))
        function->FunctionFlags = flags;
    return invoked;
}

bool HasBlockingPawnResponses(const SDK::UCapsuleComponent* capsule)
{
    if (!IsReadable(capsule, sizeof(SDK::UCapsuleComponent)))
        return false;
    const auto& channels = capsule->BodyInstance.CollisionResponses.ResponseToChannels;
    return channels.WorldStatic == SDK::ECollisionResponse::ECR_Block &&
           channels.WorldDynamic == SDK::ECollisionResponse::ECR_Block &&
           channels.PhysicsBody == SDK::ECollisionResponse::ECR_Block &&
           channels.Vehicle == SDK::ECollisionResponse::ECR_Block &&
           channels.Destructible == SDK::ECollisionResponse::ECR_Block;
}

class MovementRuntime final
{
public:
    void Update(MovementSettings& settings, MovementStatus& status, bool inputBlocked)
    {
        ProcessHotkeys(settings, inputBlocked);

        SDK::UWorld* world = ResolveWorld();
        SDK::APawn* pawn = ResolveLocalPawn(world);
        status.pawnAttached = pawn != nullptr;
        UpdateHostAuthorization(settings, status, world, pawn);

        if (pawn != pawn_ || (pawn != nullptr && pawn->Index != pawnIndex_))
        {
            Detach(false);
            if (pawn != nullptr)
                Attach(pawn);
        }

        status.movementComponentReady = movement_ != nullptr;
        if (character_ == nullptr || movement_ == nullptr ||
            !IsReadable(character_, sizeof(SDK::ABPCharacter_Demo_C)) || !IsLiveUObject(character_) ||
            !IsReadable(movement_, sizeof(SDK::UCharacterMovementComponent)) || !IsLiveUObject(movement_) ||
            character_->CharacterMovement != movement_ ||
            movement_->CharacterOwner != static_cast<SDK::ACharacter*>(character_))
        {
            Detach(false);
            status.movementComponentReady = false;
            status.telemetryReady = false;
            status.positionReady = false;
            return;
        }

        ApplyInfiniteStamina(settings);
        // Flight and auto-sprint conflict: while flying, suppress sprinting (the
        // two fight for the movement component). Remember that auto-sprint was
        // enabled so it resumes automatically once flight is turned back off.
        const bool flightActive = settings.flightEnabled || settings.noClipEnabled;
        if (flightActive)
        {
            if (settings.autoSprint && !autoSprintSuspendedByFlight_)
                autoSprintSuspendedByFlight_ = true;
            if (autoSprintSuspendedByFlight_)
                RestoreAutoSprint();
        }
        else
        {
            if (autoSprintSuspendedByFlight_)
                autoSprintSuspendedByFlight_ = false;
            ApplyAutoSprint(settings, inputBlocked);
        }
        ApplyCrouchSpeed(settings);
        ApplyJump(settings);
        ApplyGravity(settings);
        ApplyAirTurn(settings);
        ApplyServerMoveBypass(settings);
        TrackNoClipRequest(settings);
        ApplyNoClip(settings);
        ApplyFlight(settings, inputBlocked);
        RecoverOrphanedFlight(settings);
        RecoverOrphanedNoClip(settings);
        // ApplySpeed runs last so the custom walk/sprint speed is the final word
        // (it also sets MaxSprintSpeed so auto-sprint can't override it).
        ApplySpeed(settings);
        ApplyQuickStop(settings);
        UpdateTelemetry(status);
    }

    void Reset()
    {
        SDK::UWorld* currentWorld = ResolveWorld();
        SDK::APawn* currentPawn = ResolveLocalPawn(currentWorld);
        Detach(currentPawn != nullptr && currentPawn == pawn_ && currentPawn->Index == pawnIndex_);

        if (currentWorld != nullptr && currentWorld == serverWorld_ &&
            currentWorld->Index == serverWorldIndex_)
            RestoreHostAuthorization();
        else
            serverOriginals_.clear();
        serverWorld_ = nullptr;
        serverWorldIndex_ = -1;
        hostAuthSuspendUntil_ = 0;
    }

    const SDK::UObject* TickObject() const noexcept
    {
        return pawn_;
    }

    const SDK::UObject* MovementObject() const noexcept
    {
        return movement_;
    }

    // Immediately drop the client-movement override so the server's own correction wins.
    void ClearHostAuthorization()
    {
        RestoreHostAuthorization();
    }

    // Keep the override off for a while so the engine can push a teleport to the clients.
    void SuspendHostAuthorization(const std::uint64_t milliseconds)
    {
        const std::uint64_t span = milliseconds > 60000 ? 60000 : milliseconds;
        hostAuthSuspendUntil_ = GetTickCount64() + span;
        RestoreHostAuthorization();
    }

private:
    struct ServerMovementOriginal
    {
        std::int32_t objectIndex = -1;
        bool ignoreClientErrors = false;
        bool acceptClientPosition = false;
    };

    static SDK::UCharacterMovementComponent* ResolvePlayerMovement(SDK::APlayerState* playerState)
    {
        if (!IsLiveUObject(playerState))
            return nullptr;

        SDK::APawn* pawn = playerState->PawnPrivate;
        if (!IsLiveUObject(pawn) || !IsReadable(pawn, sizeof(SDK::ABPCharacter_Demo_C)) ||
            pawn->Role != SDK::ENetRole::ROLE_Authority || !IsCharacterClass(pawn))
            return nullptr;

        auto* character = reinterpret_cast<SDK::ACharacter*>(pawn);
        SDK::UCharacterMovementComponent* movement = character->CharacterMovement;
        if (!IsLiveUObject(movement) ||
            !IsReadable(movement, sizeof(SDK::UCharacterMovementComponent)) ||
            movement->CharacterOwner != character)
            return nullptr;

        return movement;
    }

    void UpdateHostAuthorization(
        const MovementSettings& settings,
        MovementStatus& status,
        SDK::UWorld* world,
        SDK::APawn* localPawn)
    {
        status.hostAuthority = localPawn != nullptr && localPawn->Role == SDK::ENetRole::ROLE_Authority;
        status.hostAuthorizedPawns = 0;

        // While suspended (e.g. right after a server-side teleport of a member) leave the
        // engine's own correction in place so the teleport actually reaches the clients.
        if (GetTickCount64() < hostAuthSuspendUntil_)
        {
            RestoreHostAuthorization();
            return;
        }

        if (world == nullptr)
        {
            serverOriginals_.clear();
            serverWorld_ = nullptr;
            serverWorldIndex_ = -1;
            return;
        }

        if (world != serverWorld_ || world->Index != serverWorldIndex_)
        {
            serverOriginals_.clear();
            serverWorld_ = world;
            serverWorldIndex_ = world->Index;
        }

        if (!settings.hostAcceptClientMovement || !status.hostAuthority)
        {
            RestoreHostAuthorization();
            return;
        }

        SDK::AGameStateBase* gameState = world->GameState;
        if (!IsLiveUObject(gameState))
            return;

        const auto players = gameState->PlayerArray;
        if (!players.IsValid() || players.Num() < 0 || players.Num() > 64 ||
            (players.Num() > 0 &&
             !IsReadable(players.GetDataPtr(), sizeof(SDK::APlayerState*) * players.Num())))
            return;

        std::vector<SDK::UCharacterMovementComponent*> activeComponents;
        activeComponents.reserve(static_cast<std::size_t>(players.Num()));

        for (int index = 0; index < players.Num(); ++index)
        {
            SDK::UCharacterMovementComponent* movement = ResolvePlayerMovement(players.GetDataPtr()[index]);
            if (movement == nullptr)
                continue;

            activeComponents.push_back(movement);
            auto iterator = serverOriginals_.find(movement);
            if (iterator == serverOriginals_.end() || iterator->second.objectIndex != movement->Index)
            {
                ServerMovementOriginal original{};
                original.objectIndex = movement->Index;
                original.ignoreClientErrors = movement->bIgnoreClientMovementErrorChecksAndCorrection != 0;
                original.acceptClientPosition = movement->bServerAcceptClientAuthoritativePosition != 0;
                serverOriginals_[movement] = original;
            }

            movement->bIgnoreClientMovementErrorChecksAndCorrection = 1;
            movement->bServerAcceptClientAuthoritativePosition = 1;
            ++status.hostAuthorizedPawns;
        }

        for (auto iterator = serverOriginals_.begin(); iterator != serverOriginals_.end();)
        {
            if (std::find(activeComponents.begin(), activeComponents.end(), iterator->first) !=
                activeComponents.end())
            {
                ++iterator;
                continue;
            }

            RestoreHostMovement(iterator->first, iterator->second);
            iterator = serverOriginals_.erase(iterator);
        }
    }

    static void RestoreHostMovement(
        SDK::UCharacterMovementComponent* movement,
        const ServerMovementOriginal& original)
    {
        if (!IsLiveUObject(movement) ||
            !IsReadable(movement, sizeof(SDK::UCharacterMovementComponent)) ||
            movement->Index != original.objectIndex)
            return;

        movement->bIgnoreClientMovementErrorChecksAndCorrection = original.ignoreClientErrors ? 1 : 0;
        movement->bServerAcceptClientAuthoritativePosition = original.acceptClientPosition ? 1 : 0;
    }

    void RestoreHostAuthorization()
    {
        for (const auto& [movement, original] : serverOriginals_)
            RestoreHostMovement(movement, original);
        serverOriginals_.clear();
    }

    static bool IsVirtualKeyDown(int key)
    {
        return key > 0 && key < 256 && (GetAsyncKeyState(key) & 0x8000) != 0;
    }

    void ProcessHotkeys(MovementSettings& settings, bool inputBlocked)
    {
        const bool flightDown = IsVirtualKeyDown(settings.flightToggleKey);
        const bool noClipDown = IsVirtualKeyDown(settings.noClipToggleKey);

        if (settings.flightToggleKey != 0 &&
            settings.flightToggleKey == settings.noClipToggleKey)
        {
            if (!inputBlocked && flightDown && !flightKeyDown_)
            {
                const bool enabled = !(settings.flightEnabled || settings.noClipEnabled);
                settings.flightEnabled = enabled;
                settings.noClipEnabled = enabled;
            }
            flightKeyDown_ = flightDown;
            noClipKeyDown_ = flightDown;
            return;
        }

        if (!inputBlocked)
        {
            if (flightDown && !flightKeyDown_)
                settings.flightEnabled = !settings.flightEnabled;
            if (noClipDown && !noClipKeyDown_)
                settings.noClipEnabled = !settings.noClipEnabled;
        }

        flightKeyDown_ = flightDown;
        noClipKeyDown_ = noClipDown;
    }

    void UpdateTelemetry(MovementStatus& status) const
    {
        constexpr float unrealUnitsPerMeter = 100.0f;
        const SDK::FVector velocity = movement_->Velocity;
        if (!std::isfinite(velocity.X) || !std::isfinite(velocity.Y) ||
            !std::isfinite(velocity.Z))
        {
            status.telemetryReady = false;
            status.positionReady = false;
            return;
        }

        status.velocityX = velocity.X / unrealUnitsPerMeter;
        status.velocityY = velocity.Y / unrealUnitsPerMeter;
        status.velocityZ = velocity.Z / unrealUnitsPerMeter;
        status.horizontalSpeed = std::sqrt(velocity.X * velocity.X + velocity.Y * velocity.Y) /
                                 unrealUnitsPerMeter;
        status.totalSpeed = std::sqrt(velocity.X * velocity.X + velocity.Y * velocity.Y +
                                      velocity.Z * velocity.Z) / unrealUnitsPerMeter;
        status.movementMode = static_cast<int>(movement_->MovementMode);
        status.telemetryReady = true;
        status.positionReady = false;

        SDK::USceneComponent* root = character_->RootComponent;
        if (!IsReadable(root, sizeof(SDK::USceneComponent)) || !IsLiveUObject(root))
            return;

        const SDK::FVector position = root->RelativeLocation;
        if (!std::isfinite(position.X) || !std::isfinite(position.Y) ||
            !std::isfinite(position.Z))
            return;

        status.positionX = position.X / unrealUnitsPerMeter;
        status.positionY = position.Y / unrealUnitsPerMeter;
        status.positionZ = position.Z / unrealUnitsPerMeter;
        status.positionReady = true;
    }

    void Attach(SDK::APawn* pawn)
    {
        auto* character = reinterpret_cast<SDK::ABPCharacter_Demo_C*>(pawn);
        SDK::UCharacterMovementComponent* movement = character->CharacterMovement;
        if (!IsReadable(movement, sizeof(SDK::UCharacterMovementComponent)) || !IsLiveUObject(character) ||
            !IsLiveUObject(movement) ||
            movement->CharacterOwner != static_cast<SDK::ACharacter*>(character))
            return;

        pawn_ = pawn;
        pawnIndex_ = pawn->Index;
        character_ = character;
        movement_ = movement;
        SDK::UCapsuleComponent* capsule = character_->CapsuleComponent;
        if (IsLiveUObject(capsule) && !HasBlockingPawnResponses(capsule))
            collisionRecoveryFrames_ = 180;
        core::Logf("movement attached to Pawn=%p CharacterMovement=%p", pawn_, movement_);
    }

    void Detach(bool restore)
    {
        if (restore && character_ != nullptr && movement_ != nullptr &&
            IsReadable(character_, sizeof(SDK::ABPCharacter_Demo_C)) &&
            IsReadable(movement_, sizeof(SDK::UCharacterMovementComponent)) &&
            IsLiveUObject(character_) && IsLiveUObject(movement_) &&
            movement_->CharacterOwner == static_cast<SDK::ACharacter*>(character_))
        {
            RestoreSpeed();
            RestoreCrouchSpeed();
            RestoreJump();
            RestoreAirTurn();
            RestoreServerMoveBypass();
            RestoreNoClip();
            RestoreFlight(nullptr);
            RestoreGravity();
            RestoreAutoSprint();
        }

        pawn_ = nullptr;
        pawnIndex_ = -1;
        character_ = nullptr;
        movement_ = nullptr;
        speedApplied_ = false;
        crouchApplied_ = false;
        jumpApplied_ = false;
        airTurnApplied_ = false;
        gravityApplied_ = false;
        flightApplied_ = false;
        noClipApplied_ = false;
        autoSprintApplied_ = false;
        autoSprintSuspendedByFlight_ = false;
        original_ = {};
        noClipRequestedLast_ = false;
        collisionRecoveryFrames_ = 0;
    }

    void ApplyInfiniteStamina(const MovementSettings& settings)
    {
        if (!settings.infiniteStamina)
            return;

        const float maximum = character_->MaxStamina;
        if (std::isfinite(maximum) && maximum > 0.0f)
            character_->Stamina = maximum;
    }

    bool SetSprintingNative(bool sprint)
    {
        if (movement_ == nullptr ||
            !IsReadable(movement_, sizeof(SDK::UFancyMovementComponent)) ||
            !IsLiveUObject(movement_))
            return false;

        static SDK::UClass* cachedClass = nullptr;
        static SDK::UFunction* function = nullptr;
        if (movement_->Class != cachedClass)
        {
            cachedClass = movement_->Class;
            function = FindFunction(movement_, "SetSprinting");
        }
        if (!game::CanProcessEvent(movement_, function))
            return false;

        SDK::Params::FancyMovementComponent_SetSprinting parameters{};
        parameters.Sprint = sprint;
        const auto flags = function->FunctionFlags;
        function->FunctionFlags |= 0x400;
        const bool invoked = game::ProcessEventSafe(movement_, function, &parameters);
        if (IsLiveUObject(function))
            function->FunctionFlags = flags;
        return invoked;
    }

    void ApplyAutoSprint(const MovementSettings& settings, bool inputBlocked)
    {
        if (!settings.autoSprint)
        {
            RestoreAutoSprint();
            return;
        }

        if (inputBlocked || character_ == nullptr || movement_ == nullptr)
            return;

        const bool wantsForward = (GetAsyncKeyState('W') & 0x8000) != 0;
        const bool canSprint = !character_->IsBurnedOut &&
            (!character_->ShouldUseStamina || character_->Stamina > 0.0f);
        const bool shouldSprint = wantsForward && canSprint;

        character_->IsSprinting = shouldSprint;
        if (shouldSprint != autoSprintApplied_)
        {
            SetSprintingNative(shouldSprint);
            autoSprintApplied_ = shouldSprint;
        }
    }

    void RestoreAutoSprint()
    {
        if (!autoSprintApplied_)
            return;

        if (character_ != nullptr &&
            IsReadable(character_, sizeof(SDK::ABPCharacter_Demo_C)) &&
            IsLiveUObject(character_))
        {
            character_->IsSprinting = false;
        }
        SetSprintingNative(false);
        autoSprintApplied_ = false;
    }

    void ApplySpeed(const MovementSettings& settings)
    {
        if (!settings.speedEnabled)
        {
            RestoreSpeed();
            return;
        }

        if (!speedApplied_)
        {
            original_.componentWalkSpeed = movement_->MaxWalkSpeed;
            if (auto* fancy = static_cast<SDK::UFancyMovementComponent*>(movement_); IsLiveUObject(fancy))
                original_.componentSprintSpeed = fancy->MaxSprintSpeed;
            original_.blueprintWalkSpeed = character_->WalkSpeed;
            original_.blueprintSprintSpeed = character_->SprintSpeed;
            speedApplied_ = true;
        }

        const float walk = std::clamp(settings.walkSpeed, 50.0f, 10000.0f);
        const float sprint = std::clamp(settings.sprintSpeed, 50.0f, 10000.0f);
        character_->WalkSpeed = walk;
        character_->SprintSpeed = sprint;
        if (auto* fancy = static_cast<SDK::UFancyMovementComponent*>(movement_); IsLiveUObject(fancy))
            fancy->MaxSprintSpeed = sprint;
        movement_->MaxWalkSpeed = character_->IsSprinting ? sprint : walk;
    }

    void ApplyCrouchSpeed(const MovementSettings& settings)
    {
        if (!settings.crouchSpeedEnabled)
        {
            RestoreCrouchSpeed();
            return;
        }

        if (!crouchApplied_)
        {
            original_.componentCrouchSpeed = movement_->MaxWalkSpeedCrouched;
            original_.blueprintCrouchSpeed = character_->CrouchWalkSpeed;
            crouchApplied_ = true;
        }

        const float value = std::clamp(settings.crouchSpeed, 25.0f, 10000.0f);
        character_->CrouchWalkSpeed = value;
        movement_->MaxWalkSpeedCrouched = value;
    }

    void ApplyJump(const MovementSettings& settings)
    {
        if (!settings.jumpEnabled)
        {
            RestoreJump();
            return;
        }

        if (!jumpApplied_)
        {
            original_.jumpVelocity = movement_->JumpZVelocity;
            jumpApplied_ = true;
        }
        movement_->JumpZVelocity = std::clamp(settings.jumpVelocity, 0.0f, 5000.0f);
    }

    void ApplyGravity(const MovementSettings& settings)
    {
        if (!settings.gravityEnabled)
        {
            RestoreGravity();
            return;
        }

        if (!gravityApplied_)
        {
            original_.gravityScale = movement_->GravityScale;
            gravityApplied_ = true;
        }
        movement_->GravityScale = std::clamp(settings.gravityScale, 0.0f, 5.0f);
    }

    void ApplyFlight(const MovementSettings& settings, bool inputBlocked)
    {
        const bool enabled = settings.flightEnabled || settings.noClipEnabled;
        if (!enabled)
        {
            RestoreFlight(&settings);
            return;
        }

        if (!flightApplied_)
        {
            original_.flySpeed = movement_->MaxFlySpeed;
            original_.flightGravityScale = movement_->GravityScale;
            original_.movementMode = movement_->MovementMode;
            original_.customMovementMode = movement_->CustomMovementMode;
            flightApplied_ = true;
        }

        const float speed = std::clamp(settings.flySpeed, 50.0f, 10000.0f);
        movement_->MovementMode = SDK::EMovementMode::MOVE_Flying;
        movement_->CustomMovementMode = 0;
        movement_->GravityScale = 0.0f;
        movement_->MaxFlySpeed = speed;

        float verticalVelocity = 0.0f;
        if (!inputBlocked)
        {
            if ((GetAsyncKeyState(VK_SPACE) & 0x8000) != 0)
                verticalVelocity += speed;
            if ((GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0)
                verticalVelocity -= speed;
        }
        movement_->Velocity.Z = verticalVelocity;
    }

    void ApplyNoClip(const MovementSettings& settings)
    {
        if (!settings.noClipEnabled)
        {
            RestoreNoClip();
            return;
        }

        if (!noClipApplied_)
        {
            SDK::UCapsuleComponent* capsule = character_->CapsuleComponent;
            if (!IsReadable(capsule, sizeof(SDK::UCapsuleComponent)) || !IsLiveUObject(capsule))
                return;

            original_.noClipResponseValid.fill(false);
            original_.collisionProfileName = capsule->BodyInstance.CollisionProfileName;
            original_.collisionProfileValid = !original_.collisionProfileName.IsNone();
            for (std::size_t index = 0; index < NoClipChannels.size(); ++index)
            {
                const auto response = GetCollisionResponse(capsule, NoClipChannels[index]);
                if (!response.has_value() ||
                    !SetCollisionResponse(capsule, NoClipChannels[index],
                                          NoClipResponses[index]))
                {
                    //修改失败时立即回滚
                    for (std::size_t restore = 0; restore < index; ++restore)
                    {
                        if (original_.noClipResponseValid[restore])
                            SetCollisionResponse(capsule, NoClipChannels[restore],
                                                 original_.noClipResponses[restore]);
                    }
                    return;
                }
                original_.noClipResponses[index] = *response;
                original_.noClipResponseValid[index] = true;
            }
            noClipApplied_ = true;
        }
    }

    void RestoreSpeed()
    {
        if (!speedApplied_)
            return;
        movement_->MaxWalkSpeed = original_.componentWalkSpeed;
        if (auto* fancy = static_cast<SDK::UFancyMovementComponent*>(movement_); IsLiveUObject(fancy))
            fancy->MaxSprintSpeed = original_.componentSprintSpeed;
        character_->WalkSpeed = original_.blueprintWalkSpeed;
        character_->SprintSpeed = original_.blueprintSprintSpeed;
        speedApplied_ = false;
    }

    void RestoreCrouchSpeed()
    {
        if (!crouchApplied_)
            return;
        movement_->MaxWalkSpeedCrouched = original_.componentCrouchSpeed;
        character_->CrouchWalkSpeed = original_.blueprintCrouchSpeed;
        crouchApplied_ = false;
    }

    void RestoreJump()
    {
        if (!jumpApplied_)
            return;
        movement_->JumpZVelocity = original_.jumpVelocity;
        jumpApplied_ = false;
    }

    void RestoreGravity()
    {
        if (!gravityApplied_)
            return;
        movement_->GravityScale = original_.gravityScale;
        gravityApplied_ = false;
    }

    // Air turn / mid-air steering: raises AirControl (and the airborne acceleration)
    // so the character can steer while falling/jumping instead of drifting straight.
    void ApplyAirTurn(const MovementSettings& settings)
    {
        if (!settings.airTurnEnabled)
        {
            RestoreAirTurn();
            return;
        }

        if (!airTurnApplied_)
        {
            original_.airControl = movement_->AirControl;
            original_.airControlBoostMultiplier = movement_->AirControlBoostMultiplier;
            original_.airControlBoostVelocityThreshold = movement_->AirControlBoostVelocityThreshold;
            original_.maxAcceleration = movement_->MaxAcceleration;
            airTurnApplied_ = true;
        }

        movement_->AirControl = std::clamp(settings.airTurnControl, 0.0f, 1.0f);
        movement_->AirControlBoostMultiplier = 1.0f;
        movement_->AirControlBoostVelocityThreshold = 0.0f;
        const bool airborne = movement_->MovementMode == SDK::EMovementMode::MOVE_Falling ||
                              movement_->MovementMode == SDK::EMovementMode::MOVE_Swimming;
        movement_->MaxAcceleration = airborne
            ? std::clamp(settings.airTurnAcceleration, 0.0f, 100000.0f)
            : original_.maxAcceleration;
    }

    void RestoreAirTurn()
    {
        if (!airTurnApplied_)
            return;
        if (IsReadable(movement_, sizeof(SDK::UCharacterMovementComponent)) && IsLiveUObject(movement_))
        {
            movement_->AirControl = original_.airControl;
            movement_->AirControlBoostMultiplier = original_.airControlBoostMultiplier;
            movement_->AirControlBoostVelocityThreshold = original_.airControlBoostVelocityThreshold;
            movement_->MaxAcceleration = original_.maxAcceleration;
        }
        airTurnApplied_ = false;
    }

    // Multiplayer movement bypass (client side).
    // The server re-simulates our moves and answers divergence with a ClientAdjust* RPC,
    // which snaps us back. Two things make our own position stick:
    //   1. widen the client's smooth/no-smooth network distances so it never eases toward
    //      the server's copy;
    //   2. mark the component as ignoring client movement errors;
    // and, in the ProcessEvent hook, drop the ClientAdjust* RPCs entirely (dxgi_hooks.cpp).
    void ApplyServerMoveBypass(const MovementSettings& settings)
    {
        if (!settings.serverMoveBypass)
        {
            RestoreServerMoveBypass();
            return;
        }

        if (!bypassApplied_)
        {
            original_.networkMaxSmoothUpdateDistance = movement_->NetworkMaxSmoothUpdateDistance;
            original_.networkNoSmoothUpdateDistance = movement_->NetworkNoSmoothUpdateDistance;
            original_.ignoreClientMovementErrors = movement_->bIgnoreClientMovementErrorChecksAndCorrection;
            original_.serverAcceptClientPosition = movement_->bServerAcceptClientAuthoritativePosition;
            bypassApplied_ = true;
        }

        // Effectively "never smooth, never snap".
        movement_->NetworkMaxSmoothUpdateDistance = 1.0e7f;
        movement_->NetworkNoSmoothUpdateDistance = 1.0e7f;
        movement_->bIgnoreClientMovementErrorChecksAndCorrection = 1;
        movement_->bServerAcceptClientAuthoritativePosition = 1;
    }

    void RestoreServerMoveBypass()
    {
        if (!bypassApplied_)
            return;
        if (IsReadable(movement_, sizeof(SDK::UCharacterMovementComponent)) && IsLiveUObject(movement_))
        {
            movement_->NetworkMaxSmoothUpdateDistance = original_.networkMaxSmoothUpdateDistance;
            movement_->NetworkNoSmoothUpdateDistance = original_.networkNoSmoothUpdateDistance;
            movement_->bIgnoreClientMovementErrorChecksAndCorrection =
                original_.ignoreClientMovementErrors;
            movement_->bServerAcceptClientAuthoritativePosition = original_.serverAcceptClientPosition;
        }
        bypassApplied_ = false;
    }

    void ClearFlightMotion()
    {
        movement_->Velocity = {};
        movement_->Acceleration = {};
        movement_->LastUpdateVelocity = {};
        movement_->PendingImpulseToApply = {};
        movement_->PendingForceToApply = {};
        movement_->AnalogInputModifier = 0.0f;
        movement_->bForceNextFloorCheck = 1;
        movement_->bJustTeleported = 1;
        character_->ControlInputVector = {};
        character_->LastControlInputVector = {};
    }

    // Quick stop: releasing WASD kills horizontal velocity instantly instead of
    // letting the character slide to a halt. Grounded and airborne are both handled.
    void ApplyQuickStop(const MovementSettings& settings)
    {
        if (!settings.quickStop)
            return;

        static const int movementKeys[] = {'W', 'A', 'S', 'D'};
        for (const int key : movementKeys)
        {
            if ((GetAsyncKeyState(key) & 0x8000) != 0)
                return;
        }

        const SDK::EMovementMode mode = movement_->MovementMode;
        if (mode != SDK::EMovementMode::MOVE_Walking &&
            mode != SDK::EMovementMode::MOVE_NavWalking &&
            mode != SDK::EMovementMode::MOVE_Falling)
            return;

        movement_->Velocity.X = 0.0f;
        movement_->Velocity.Y = 0.0f;
        movement_->Acceleration.X = 0.0f;
        movement_->Acceleration.Y = 0.0f;
        movement_->LastUpdateVelocity.X = 0.0f;
        movement_->LastUpdateVelocity.Y = 0.0f;
    }

    void RestoreFlight(const MovementSettings* settings)
    {
        if (!flightApplied_)
            return;

        if (IsReadable(movement_, sizeof(SDK::UCharacterMovementComponent)) && IsLiveUObject(movement_))
        {
            ClearFlightMotion();
            movement_->MaxFlySpeed = original_.flySpeed;
            if (settings != nullptr && settings->gravityEnabled)
                movement_->GravityScale = std::clamp(settings->gravityScale, 0.0f, 5.0f);
            else
            {
                const float savedGravity = original_.flightGravityScale;
                movement_->GravityScale = std::isfinite(savedGravity) && savedGravity > 0.01f
                    ? savedGravity : 1.0f;
            }
            SDK::EMovementMode restoreMode = original_.movementMode;
            if (restoreMode != SDK::EMovementMode::MOVE_Swimming)
                restoreMode = SDK::EMovementMode::MOVE_Falling;
            movement_->MovementMode = restoreMode;
            movement_->CustomMovementMode = restoreMode == original_.movementMode
                ? original_.customMovementMode : 0;
        }
        flightApplied_ = false;
    }

    void RecoverOrphanedFlight(const MovementSettings& settings)
    {
        if (flightApplied_ || settings.flightEnabled || settings.noClipEnabled)
            return;

        const bool orphanedMode = movement_->MovementMode == SDK::EMovementMode::MOVE_Flying;
        const bool orphanedGravity = !settings.gravityEnabled &&
            (!std::isfinite(movement_->GravityScale) || movement_->GravityScale <= 0.01f);
        if (!orphanedMode && !orphanedGravity)
            return;

        //fix残留
        ClearFlightMotion();
        if (settings.gravityEnabled)
            movement_->GravityScale = std::clamp(settings.gravityScale, 0.0f, 5.0f);
        else if (!std::isfinite(movement_->GravityScale) || movement_->GravityScale <= 0.01f)
            movement_->GravityScale = 1.0f;
        if (orphanedMode)
        {
            movement_->MovementMode = SDK::EMovementMode::MOVE_Falling;
            movement_->CustomMovementMode = 0;
        }
        core::Log("recovered orphaned character flight state");
    }

    void TrackNoClipRequest(const MovementSettings& settings)
    {
        if (noClipRequestedLast_ && !settings.noClipEnabled)
            collisionRecoveryFrames_ = 180;
        noClipRequestedLast_ = settings.noClipEnabled;
    }

    void RecoverOrphanedNoClip(const MovementSettings& settings)
    {
        if (settings.noClipEnabled || collisionRecoveryFrames_ <= 0)
            return;

        SDK::UCapsuleComponent* capsule = IsLiveUObject(character_)
            ? character_->CapsuleComponent : nullptr;
        if (!IsLiveUObject(capsule) || !IsReadable(capsule, sizeof(SDK::UCapsuleComponent)))
            return;

        if (HasBlockingPawnResponses(capsule))
        {
            collisionRecoveryFrames_ = 0;
            return;
        }

        const SDK::FName profile = original_.collisionProfileValid
            ? original_.collisionProfileName : capsule->BodyInstance.CollisionProfileName;
        if (!profile.IsNone())
            SetCollisionProfile(capsule, profile);
        for (const SDK::ECollisionChannel channel : NoClipChannels)
            SetCollisionResponse(capsule, channel, SDK::ECollisionResponse::ECR_Block);
        --collisionRecoveryFrames_;
    }

    void RestoreNoClip()
    {
        if (!noClipApplied_)
            return;

        SDK::UCapsuleComponent* capsule = IsLiveUObject(character_) ? character_->CapsuleComponent : nullptr;
        bool restoredAll = false;
        if (IsReadable(capsule, sizeof(SDK::UCapsuleComponent)) && IsLiveUObject(capsule))
        {
            if (original_.collisionProfileValid)
                SetCollisionProfile(capsule, original_.collisionProfileName);

            for (std::size_t index = 0; index < NoClipChannels.size(); ++index)
            {
                SetCollisionResponse(capsule, NoClipChannels[index],
                                     SDK::ECollisionResponse::ECR_Block);
            }
            restoredAll = HasBlockingPawnResponses(capsule);
        }

        if (restoredAll)
        {
            original_.noClipResponseValid.fill(false);
            noClipApplied_ = false;
        }
        else
        {
            collisionRecoveryFrames_ = std::max(collisionRecoveryFrames_, 180);
        }
    }

    SDK::APawn* pawn_ = nullptr;
    std::int32_t pawnIndex_ = -1;
    SDK::ABPCharacter_Demo_C* character_ = nullptr;
    SDK::UCharacterMovementComponent* movement_ = nullptr;
    OriginalValues original_{};
    bool speedApplied_ = false;
    bool crouchApplied_ = false;
    bool jumpApplied_ = false;
    bool gravityApplied_ = false;
    bool airTurnApplied_ = false;
    bool bypassApplied_ = false;
    bool flightApplied_ = false;
    bool noClipApplied_ = false;
    bool autoSprintApplied_ = false;
    bool autoSprintSuspendedByFlight_ = false;
    bool noClipRequestedLast_ = false;
    int collisionRecoveryFrames_ = 0;
    bool flightKeyDown_ = false;
    bool noClipKeyDown_ = false;
    SDK::UWorld* serverWorld_ = nullptr;
    std::int32_t serverWorldIndex_ = -1;
    std::uint64_t hostAuthSuspendUntil_ = 0;
    std::unordered_map<SDK::UCharacterMovementComponent*, ServerMovementOriginal> serverOriginals_;
};

MovementRuntime g_runtime;
}

Movement& Movement::Instance()
{
    static Movement instance;
    return instance;
}

void Movement::Update(bool inputBlocked)
{
    bunnyHopRequested_.store(settings_.bunnyHop, std::memory_order_release);
    autoJumpRequested_.store(settings_.bhopAutoJump, std::memory_order_release);
    bunnyHopMovementBlocked_.store(settings_.flightEnabled || settings_.noClipEnabled,
                                   std::memory_order_release);
    gameplayInputBlocked_.store(inputBlocked, std::memory_order_release);
    g_runtime.Update(settings_, status_, inputBlocked);
    gameThreadTickObject_.store(g_runtime.TickObject(), std::memory_order_release);
    // Published for the ProcessEvent hook: dropping ClientAdjust* needs to be decided on
    // the game thread without touching member state.
    serverMoveBypass_.store(settings_.serverMoveBypass, std::memory_order_release);
    localMovementObject_.store(g_runtime.MovementObject(), std::memory_order_release);
}

bool Movement::ServerMoveBypassEnabled() const noexcept
{
    return serverMoveBypass_.load(std::memory_order_acquire);
}

bool Movement::IsLocalMovementObject(const SDK::UObject* object) const noexcept
{
    const auto* local = localMovementObject_.load(std::memory_order_acquire);
    return object != nullptr && object == local;
}

bool Movement::IsMovementCorrectionRpc(SDK::UFunction* function)
{
    if (!IsLiveUObject(function))
        return false;
    std::string name;
    if (!game::TryFNameToString(function->Name, name))
        return false;
    // Every server -> client correction entry point in UCharacterMovementComponent.
    return name == "ClientAdjustPosition" ||
           name == "ClientVeryShortAdjustPosition" ||
           name == "ClientAdjustRootMotionPosition" ||
           name == "ClientAdjustRootMotionSourcePosition";
}

void Movement::OnGameThreadTick(const SDK::UObject* tickObject)
{
    if (!bunnyHopRequested_.load(std::memory_order_acquire) ||
        gameplayInputBlocked_.load(std::memory_order_acquire) ||
        bunnyHopMovementBlocked_.load(std::memory_order_acquire))
        return;

    const auto* anchor = gameThreadTickObject_.load(std::memory_order_acquire);
    if (anchor == nullptr || tickObject != anchor || !IsCharacterClass(anchor))
        return;

    auto* character = reinterpret_cast<SDK::ACharacter*>(const_cast<SDK::UObject*>(anchor));
    SDK::UCharacterMovementComponent* movement = character->CharacterMovement;
    if (!IsLiveUObject(movement) ||
        !IsReadable(movement, sizeof(SDK::UCharacterMovementComponent)) ||
        movement->CharacterOwner != character ||
        (movement->MovementMode != SDK::EMovementMode::MOVE_Walking &&
         movement->MovementMode != SDK::EMovementMode::MOVE_NavWalking))
        return;

    // Auto-jump mode: keep jumping while the player is actually moving, with no key
    // needed. Standing still (horizontal speed under ~0.5 m/s) stops the jumping.
    // Otherwise fall back to the classic "hold Space to hop" behaviour.
    if (autoJumpRequested_.load(std::memory_order_acquire))
    {
        const float horizontalSq = movement->Velocity.X * movement->Velocity.X +
                                   movement->Velocity.Y * movement->Velocity.Y;
        const float threshold = std::clamp(settings_.bhopAutoJumpSpeed, 0.0f, 50.0f) * 100.0f;
        if (!(horizontalSq > threshold * threshold))
            return;
    }
    else if ((GetAsyncKeyState(VK_SPACE) & 0x8000) == 0)
    {
        return;
    }

    static SDK::UClass* cachedClass = nullptr;
    static SDK::UFunction* jumpFunction = nullptr;
    if (cachedClass != character->Class || !IsLiveUObject(jumpFunction))
    {
        cachedClass = character->Class;
        jumpFunction = FindFunction(character, "Jump");
    }

    if (game::CanProcessEvent(character, jumpFunction))
    {
        const auto flags = jumpFunction->FunctionFlags;
        jumpFunction->FunctionFlags |= 0x400;
        game::ProcessEventSafe(character, jumpFunction, nullptr);
        if (IsLiveUObject(jumpFunction))
            jumpFunction->FunctionFlags = flags;
    }
}

void Movement::SetGameThreadHookReady(const bool ready) noexcept
{
    gameThreadHookReady_.store(ready, std::memory_order_release);
}

void Movement::SuspendHostAuthorization(const std::uint64_t milliseconds) noexcept
{
    g_runtime.SuspendHostAuthorization(milliseconds);
}

bool Movement::NeedsGameThreadTick() const noexcept
{
    return bunnyHopRequested_.load(std::memory_order_acquire) &&
           gameThreadTickObject_.load(std::memory_order_acquire) != nullptr;
}

void Movement::Reset()
{
    bunnyHopRequested_.store(false, std::memory_order_release);
    autoJumpRequested_.store(false, std::memory_order_release);
    bunnyHopMovementBlocked_.store(false, std::memory_order_release);
    gameplayInputBlocked_.store(true, std::memory_order_release);
    gameThreadTickObject_.store(nullptr, std::memory_order_release);
    serverMoveBypass_.store(false, std::memory_order_release);
    localMovementObject_.store(nullptr, std::memory_order_release);
    g_runtime.Reset();
    status_ = {};
}
}

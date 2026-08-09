#include "features/exit_activator.hpp"

#include "core/logger.hpp"
#include "game/unreal_safety.hpp"

#include <Windows.h>
#include <SDK/BPCharacter_Demo_classes.hpp>
#include <SDK/Engine_parameters.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
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

struct ActorRecord
{
    std::int32_t objectIndex = -1;
    bool forcedOnce = false;
};

struct RuntimeState
{
    SDK::UWorld* world = nullptr;
    std::int32_t worldIndex = -1;
    int levelIndex = 0;
    int actorIndex = 0;
    std::size_t currentCandidates = 0;
    std::unordered_map<SDK::AActor*, ActorRecord> records;
};

RuntimeState g_runtime{};

std::string ToLower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char character)
    {
        return static_cast<char>(std::tolower(character));
    });
    return value;
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

SDK::UWorld* ResolveWorld()
{
    const std::uintptr_t base = SDK::InSDKUtils::GetImageBase();
    auto** address = reinterpret_cast<SDK::UWorld**>(base + SDK::Offsets::GWorld);
    if (!IsReadable(address, sizeof(*address)))
        return nullptr;
    SDK::UWorld* world = *address;
    return IsReadable(world, 0x188) && IsLiveUObject(world) ? world : nullptr;
}

SDK::ABPCharacter_Demo_C* ResolveLocalCharacter(SDK::UWorld* world)
{
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
    if (!IsReadable(player, 0x38) || !IsLiveUObject(player) ||
        !IsLiveUObject(player->PlayerController))
        return nullptr;
    SDK::APawn* pawn = player->PlayerController->AcknowledgedPawn;
    return IsReadable(pawn, sizeof(SDK::ABPCharacter_Demo_C)) && IsLiveUObject(pawn)
        ? reinterpret_cast<SDK::ABPCharacter_Demo_C*>(pawn) : nullptr;
}

SDK::UFunction* FindFunction(SDK::UObject* object, const char* functionName)
{
    if (!IsLiveUObject(object))
        return nullptr;
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
                return reinterpret_cast<SDK::UFunction*>(field);
        }
    }
    return nullptr;
}

bool Invoke(SDK::UObject* object, const char* name, void* parameters)
{
    SDK::UFunction* function = FindFunction(object, name);
    if (!game::CanProcessEvent(object, function))
        return false;
    const auto flags = function->FunctionFlags;
    function->FunctionFlags |= 0x400;
    const bool result = game::ProcessEventSafe(object, function, parameters);
    if (IsLiveUObject(function))
        function->FunctionFlags = flags;
    return result;
}

bool InvokeNoParameters(SDK::UObject* object, const char* name)
{
    SDK::UFunction* function = FindFunction(object, name);
    std::uint16_t parameterSize = 0;
    const auto* parameterSizeAddress = reinterpret_cast<const std::uint16_t*>(
        reinterpret_cast<const std::uint8_t*>(function) + 0xB6);
    if (!game::CanProcessEvent(object, function) ||
        !IsReadable(parameterSizeAddress, sizeof(parameterSize)))
        return false;
    parameterSize = *parameterSizeAddress;
    if (parameterSize != 0)
        return false;
    const auto flags = function->FunctionFlags;
    function->FunctionFlags |= 0x400;
    const bool result = game::ProcessEventSafe(object, function, nullptr);
    if (IsLiveUObject(function))
        function->FunctionFlags = flags;
    return result;
}

bool IsDerivedFrom(const SDK::UObject* object, const char* expected)
{
    if (!IsLiveUObject(object))
        return false;
    for (SDK::UStruct* type = object->Class; type != nullptr; type = type->SuperStruct)
    {
        if (!IsLiveUObject(type))
            return false;
        std::string name;
        if (!game::TryFNameToString(type->Name, name))
            return false;
        if (name == expected)
            return true;
    }
    return false;
}

bool IsExitCandidate(SDK::AActor* actor, std::string& displayName)
{
    if (!IsLiveUObject(actor) || !IsLiveUObject(actor->Class))
        return false;
    std::string actorName;
    std::string className;
    if (!game::TryFNameToString(actor->Name, actorName) ||
        !game::TryFNameToString(actor->Class->Name, className))
        return false;

    const std::string lowerActor = ToLower(actorName);
    const std::string lowerClass = ToLower(className);
    if (ContainsAny(lowerActor, {"exit sign", "exit_sign", "exitsign",
                                 "furniture gallery exit"}) ||
        ContainsAny(lowerClass, {"staticmeshactor", "exitconfirmationflow",
                                 "exit sign", "exit_sign", "exitsign"}))
        return false;

    const bool exitNamed = ContainsAny(lowerActor, {
        "level_exit", "exitportal", "exit_portal", "exit_zone", "exitzone",
        "elevator_exit", "elevatortransition", "transition", "teleport", "exit"
    }) || ContainsAny(lowerClass, {
        "level_exit", "exitportal", "exit_portal", "exit_zone", "exitzone",
        "elevator_exit", "elevatortransition", "transition", "teleport", "exit"
    });
    if (!exitNamed)
        return false;

    displayName = (ToLower(className) == "actor") ? actorName : className;
    if (displayName.size() > 64)
        displayName.resize(64);
    return true;
}

bool ForceActivateExit(SDK::AActor* actor, const bool forceTriggerCollision)
{
    if (!IsReadable(actor, sizeof(SDK::AActor)) || !IsLiveUObject(actor))
        return false;

    bool invoked = false;
    SDK::Params::Actor_SetActorHiddenInGame hidden{};
    hidden.bNewHidden = false;
    invoked |= Invoke(actor, "SetActorHiddenInGame", &hidden);

    SDK::Params::Actor_SetActorEnableCollision collision{};
    collision.bNewActorEnableCollision = true;
    invoked |= Invoke(actor, "SetActorEnableCollision", &collision);

    actor->bHidden = 0;
    actor->bActorEnableCollision = 1;

    SDK::USceneComponent* root = actor->RootComponent;
    if (IsReadable(root, sizeof(SDK::USceneComponent)) && IsLiveUObject(root))
    {
        SDK::Params::SceneComponent_SetVisibility visibility{};
        visibility.bNewVisibility = true;
        visibility.bPropagateToChildren = true;
        invoked |= Invoke(root, "SetVisibility", &visibility);

        SDK::Params::SceneComponent_SetHiddenInGame componentHidden{};
        componentHidden.NewHidden = false;
        componentHidden.bPropagateToChildren = true;
        invoked |= Invoke(root, "SetHiddenInGame", &componentHidden);

        SDK::Params::ActorComponent_Activate activate{};
        activate.bReset = true;
        invoked |= Invoke(root, "Activate", &activate);
        root->bVisible = 1;
        root->bHiddenInGame = 0;

        if (forceTriggerCollision)
        {
            std::vector<SDK::USceneComponent*> pending{root};
            std::size_t cursor = 0;
            while (cursor < pending.size() && pending.size() <= 256)
            {
                SDK::USceneComponent* component = pending[cursor++];
                if (!IsReadable(component, sizeof(SDK::USceneComponent)) ||
                    !IsLiveUObject(component))
                    continue;

                const auto children = component->AttachChildren;
                if (children.IsValid() && children.Num() >= 0 && children.Num() <= 128 &&
                    (children.Num() == 0 || IsReadable(children.GetDataPtr(),
                        sizeof(SDK::USceneComponent*) * children.Num())))
                {
                    for (int index = 0; index < children.Num(); ++index)
                        pending.push_back(children.GetDataPtr()[index]);
                }

                if (!IsDerivedFrom(component, "ShapeComponent"))
                    continue;
                SDK::Params::PrimitiveComponent_SetGenerateOverlapEvents overlap{};
                overlap.bInGenerateOverlapEvents = true;
                invoked |= Invoke(component, "SetGenerateOverlapEvents", &overlap);

                SDK::Params::PrimitiveComponent_SetCollisionEnabled enabled{};
                enabled.NewType = SDK::ECollisionEnabled::QueryOnly;
                invoked |= Invoke(component, "SetCollisionEnabled", &enabled);
            }
        }
    }

    static constexpr const char* activationEvents[] = {
        "EnableExit", "ActivateExit", "OpenExit", "UnlockExit",
        "Enable", "Activate", "Open", "Unlock"
    };
    for (const char* eventName : activationEvents)
        invoked |= InvokeNoParameters(actor, eventName);

    const auto childActors = actor->Children;
    if (childActors.IsValid() && childActors.Num() > 0 && childActors.Num() <= 64 &&
        IsReadable(childActors.GetDataPtr(), sizeof(SDK::AActor*) * childActors.Num()))
    {
        for (int childIndex = 0; childIndex < childActors.Num(); ++childIndex)
        {
            SDK::AActor* child = childActors.GetDataPtr()[childIndex];
            if (!IsLiveUObject(child) || !IsReadable(child, sizeof(SDK::AActor)))
                continue;
            SDK::Params::Actor_SetActorHiddenInGame childHidden{};
            childHidden.bNewHidden = false;
            invoked |= Invoke(child, "SetActorHiddenInGame", &childHidden);
            SDK::Params::Actor_SetActorEnableCollision childCollision{};
            childCollision.bNewActorEnableCollision = true;
            invoked |= Invoke(child, "SetActorEnableCollision", &childCollision);
            child->bHidden = 0;
            child->bActorEnableCollision = 1;
            if (IsLiveUObject(child->RootComponent))
            {
                SDK::Params::SceneComponent_SetVisibility visibility{};
                visibility.bNewVisibility = true;
                visibility.bPropagateToChildren = true;
                invoked |= Invoke(child->RootComponent, "SetVisibility", &visibility);
                SDK::Params::ActorComponent_Activate activate{};
                activate.bReset = true;
                invoked |= Invoke(child->RootComponent, "Activate", &activate);
            }
            for (const char* eventName : activationEvents)
                invoked |= InvokeNoParameters(child, eventName);
        }
    }

    invoked |= Invoke(actor, "ForceNetUpdate", nullptr);
    return invoked;
}

bool HasClassName(const SDK::UObject* object, const char* expected)
{
    if (!IsLiveUObject(object) || !IsLiveUObject(object->Class))
        return false;
    const std::string expectedName = ToLower(expected);
    for (SDK::UStruct* type = object->Class; type != nullptr; type = type->SuperStruct)
    {
        if (!IsLiveUObject(type))
            return false;
        std::string className;
        if (game::TryFNameToString(type->Name, className) &&
            ToLower(className).find(expectedName) != std::string::npos)
            return true;
    }
    return false;
}

bool BelongsToWorld(const SDK::UObject* object, const SDK::UWorld* world)
{
    const SDK::UObject* current = object;
    for (int guard = 0; current != nullptr && guard < 32; ++guard)
    {
        if (current == world)
            return true;
        if (!IsLiveUObject(current))
            return false;
        current = current->Outer;
    }
    return false;
}

SDK::AActor* FindActorByClassName(SDK::UWorld* world, const char* expected)
{
    if (!IsLiveUObject(world))
        return nullptr;
    const auto levels = world->Levels;
    if (!levels.IsValid() || levels.Num() <= 0 || levels.Num() > 512 ||
        !IsReadable(levels.GetDataPtr(), sizeof(SDK::ULevel*) * levels.Num()))
        return nullptr;
    for (int levelIndex = 0; levelIndex < levels.Num(); ++levelIndex)
    {
        SDK::ULevel* level = levels.GetDataPtr()[levelIndex];
        if (!IsReadable(level, 0xA8) || !IsLiveUObject(level))
            continue;
        const auto actors = level->Actors;
        if (!actors.IsValid() || actors.Num() < 0 || actors.Num() > 100000 ||
            (actors.Num() > 0 && !IsReadable(actors.GetDataPtr(), sizeof(SDK::AActor*) * actors.Num())))
            continue;
        for (int actorIndex = 0; actorIndex < actors.Num(); ++actorIndex)
        {
            SDK::AActor* actor = actors.GetDataPtr()[actorIndex];
            if (HasClassName(actor, expected))
                return actor;
        }
    }

    if (!IsReadable(SDK::UObject::GObjects, sizeof(*SDK::UObject::GObjects)))
        return nullptr;
    const int objectCount = SDK::UObject::GObjects->Num();
    if (objectCount <= 0 || objectCount > 4000000)
        return nullptr;
    SDK::AActor* fallback = nullptr;
    for (int index = 0; index < objectCount; ++index)
    {
        SDK::UObject* object = SDK::UObject::GObjects->GetByIndex(index);
        if (!IsLiveUObject(object) || !IsReadable(object, sizeof(SDK::AActor)) ||
            !HasClassName(object, expected) || !IsDerivedFrom(object, "Actor"))
            continue;
        std::string objectName;
        if (!game::TryFNameToString(object->Name, objectName) ||
            ToLower(objectName).find("default__") == 0)
            continue;
        auto* actor = reinterpret_cast<SDK::AActor*>(object);
        if (BelongsToWorld(object, world))
            return actor;
        if (fallback == nullptr)
            fallback = actor;
    }
    if (fallback != nullptr)
        return fallback;
    return nullptr;
}

SDK::FProperty* FindProperty(SDK::UObject* object, const char* expected)
{
    if (!IsLiveUObject(object))
        return nullptr;
    for (SDK::UStruct* type = object->Class; type != nullptr; type = type->SuperStruct)
    {
        if (!IsLiveUObject(type))
            return nullptr;
        SDK::FField* field = type->ChildProperties;
        for (int guard = 0; field != nullptr && guard < 4096; ++guard)
        {
            if (!IsReadable(field, sizeof(SDK::FProperty)))
                return nullptr;
            std::string name;
            if (game::TryFNameToString(field->Name, name) && name == expected)
                return reinterpret_cast<SDK::FProperty*>(field);
            field = field->Next;
        }
    }
    return nullptr;
}

bool IsPropertyType(const SDK::FProperty* property, const char* expected)
{
    if (!IsReadable(property, sizeof(SDK::FProperty)) ||
        !IsReadable(property->ClassPrivate, sizeof(SDK::FFieldClass)))
        return false;
    std::string name;
    return game::TryFNameToString(property->ClassPrivate->Name, name) && name == expected;
}

bool WriteIntProperty(SDK::UObject* object, const char* name, const int value)
{
    SDK::FProperty* property = FindProperty(object, name);
    if (!IsPropertyType(property, "IntProperty") || property->ElementSize != sizeof(int) ||
        property->Offset < 0 || property->Offset > 0x100000)
        return false;
    auto* address = reinterpret_cast<int*>(reinterpret_cast<std::uint8_t*>(object) + property->Offset);
    if (!IsReadable(address, sizeof(*address)))
        return false;
    *address = value;
    return true;
}

bool WriteBoolProperty(SDK::UObject* object, const char* name, const bool value)
{
    SDK::FProperty* property = FindProperty(object, name);
    if (!IsPropertyType(property, "BoolProperty") || property->ElementSize != sizeof(bool) ||
        property->Offset < 0 || property->Offset > 0x100000 ||
        !IsReadable(property, sizeof(SDK::FBoolProperty)))
        return false;
    const auto* boolProperty = reinterpret_cast<const SDK::FBoolProperty*>(property);
    auto* address = reinterpret_cast<std::uint8_t*>(object) + property->Offset + boolProperty->ByteOffset;
    if (!IsReadable(address, sizeof(*address)))
        return false;
    if (boolProperty->FieldMask == 0xFF)
        *address = value ? 1 : 0;
    else if (value)
        *address |= boolProperty->ByteMask;
    else
        *address &= static_cast<std::uint8_t>(~boolProperty->ByteMask);
    return true;
}

SDK::UObject* ReadObjectProperty(SDK::UObject* object, const char* name)
{
    SDK::FProperty* property = FindProperty(object, name);
    if (!IsPropertyType(property, "ObjectProperty") || property->ElementSize != sizeof(SDK::UObject*) ||
        property->Offset < 0 || property->Offset > 0x100000)
        return nullptr;
    auto** address = reinterpret_cast<SDK::UObject**>(reinterpret_cast<std::uint8_t*>(object) + property->Offset);
    if (!IsReadable(address, sizeof(*address)) || !IsLiveUObject(*address))
        return nullptr;
    return *address;
}

SDK::AActor* FindRollercoasterManager(SDK::UWorld* world)
{
    SDK::AActor* manager = FindActorByClassName(world, "BP_RollercoasterManager_C");
    if (manager != nullptr)
        return manager;
    if (!IsLiveUObject(world))
        return nullptr;
    const auto levels = world->Levels;
    if (!levels.IsValid() || levels.Num() <= 0 || levels.Num() > 512 ||
        !IsReadable(levels.GetDataPtr(), sizeof(SDK::ULevel*) * levels.Num()))
        return nullptr;
    for (int levelIndex = 0; levelIndex < levels.Num(); ++levelIndex)
    {
        SDK::ULevel* level = levels.GetDataPtr()[levelIndex];
        if (!IsReadable(level, 0xA8) || !IsLiveUObject(level))
            continue;
        const auto actors = level->Actors;
        if (!actors.IsValid() || actors.Num() < 0 || actors.Num() > 100000 ||
            (actors.Num() > 0 && !IsReadable(actors.GetDataPtr(), sizeof(SDK::AActor*) * actors.Num())))
            continue;
        for (int actorIndex = 0; actorIndex < actors.Num(); ++actorIndex)
        {
            SDK::AActor* actor = actors.GetDataPtr()[actorIndex];
            if (!HasClassName(actor, "BP_RollerCoaster_C"))
                continue;
            SDK::UObject* linked = ReadObjectProperty(actor, "Manager");
            if (linked != nullptr && HasClassName(linked, "BP_RollercoasterManager_C"))
                return reinterpret_cast<SDK::AActor*>(linked);
        }
    }
    return nullptr;
}

std::size_t SetRollercoastersPowered(SDK::UWorld* world)
{
    if (!IsLiveUObject(world))
        return 0;
    const auto levels = world->Levels;
    if (!levels.IsValid() || levels.Num() <= 0 || levels.Num() > 512 ||
        !IsReadable(levels.GetDataPtr(), sizeof(SDK::ULevel*) * levels.Num()))
        return 0;
    std::size_t powered = 0;
    for (int levelIndex = 0; levelIndex < levels.Num(); ++levelIndex)
    {
        SDK::ULevel* level = levels.GetDataPtr()[levelIndex];
        if (!IsReadable(level, 0xA8) || !IsLiveUObject(level))
            continue;
        const auto actors = level->Actors;
        if (!actors.IsValid() || actors.Num() < 0 || actors.Num() > 100000 ||
            (actors.Num() > 0 && !IsReadable(actors.GetDataPtr(), sizeof(SDK::AActor*) * actors.Num())))
            continue;
        for (int actorIndex = 0; actorIndex < actors.Num(); ++actorIndex)
        {
            SDK::AActor* actor = actors.GetDataPtr()[actorIndex];
            if (!HasClassName(actor, "BP_RollerCoaster_C"))
                continue;
            if (WriteBoolProperty(actor, "IsPoweredOn", true))
            {
                InvokeNoParameters(actor, "FlushNetDormancy");
                InvokeNoParameters(actor, "ForceNetUpdate");
                ++powered;
            }
        }
    }
    return powered;
}

int ReadIntProperty(SDK::UObject* object, const char* name)
{
    SDK::FProperty* property = FindProperty(object, name);
    if (!IsPropertyType(property, "IntProperty") || property->ElementSize != sizeof(int) ||
        property->Offset < 0 || property->Offset > 0x100000)
        return -1;
    const auto* address = reinterpret_cast<const int*>(reinterpret_cast<const std::uint8_t*>(object) + property->Offset);
    return IsReadable(address, sizeof(*address)) ? *address : -1;
}

std::size_t TeleportPlayersToHost(SDK::UWorld* world, SDK::ABPCharacter_Demo_C* host)
{
    if (!IsLiveUObject(world) || !IsLiveUObject(host) || !IsLiveUObject(world->GameState))
        return 0;
    SDK::Params::Actor_K2_GetActorLocation getLocation{};
    if (!Invoke(host, "K2_GetActorLocation", &getLocation))
        return 0;
    const auto players = world->GameState->PlayerArray;
    if (!players.IsValid() || players.Num() <= 0 || players.Num() > 128 ||
        !IsReadable(players.GetDataPtr(), sizeof(SDK::APlayerState*) * players.Num()))
        return 0;
    std::size_t teleported = 0;
    int remoteIndex = 0;
    for (int index = 0; index < players.Num(); ++index)
    {
        SDK::APlayerState* playerState = players.GetDataPtr()[index];
        if (!IsReadable(playerState, sizeof(SDK::APlayerState)) || !IsLiveUObject(playerState))
            continue;
        SDK::APawn* pawn = playerState->PawnPrivate;
        if (!IsLiveUObject(pawn) || pawn == host || !IsDerivedFrom(pawn, "BPCharacter_Demo_C"))
            continue;
        const float angle = static_cast<float>(remoteIndex++) * 2.39996323f;
        const float radius = 90.0f + static_cast<float>(remoteIndex / 8) * 75.0f;
        SDK::Params::Actor_K2_SetActorLocation move{};
        move.NewLocation = getLocation.ReturnValue;
        move.NewLocation.X += std::cos(angle) * radius;
        move.NewLocation.Y += std::sin(angle) * radius;
        move.NewLocation.Z += 15.0f;
        move.bSweep = false;
        move.bTeleport = true;
        if (Invoke(pawn, "K2_SetActorLocation", &move))
        {
            InvokeNoParameters(pawn, "ForceNetUpdate");
            ++teleported;
        }
    }
    return teleported;
}

std::size_t ApplyAttributesToMembers(SDK::UWorld* world, SDK::ABPCharacter_Demo_C* host,
                                     const float walkSpeed, const float sprintSpeed,
                                     const float crouchSpeed, const float maximumStamina,
                                     const bool infiniteStamina)
{
    if (!IsLiveUObject(world) || !IsLiveUObject(host) || !IsLiveUObject(world->GameState))
        return 0;
    const auto players = world->GameState->PlayerArray;
    if (!players.IsValid() || players.Num() <= 0 || players.Num() > 128 ||
        !IsReadable(players.GetDataPtr(), sizeof(SDK::APlayerState*) * players.Num()))
        return 0;

    struct FloatParameter
    {
        float value;
    };

    std::size_t modified = 0;
    for (int index = 0; index < players.Num(); ++index)
    {
        SDK::APlayerState* playerState = players.GetDataPtr()[index];
        if (!IsReadable(playerState, sizeof(SDK::APlayerState)) || !IsLiveUObject(playerState))
            continue;
        SDK::APawn* pawn = playerState->PawnPrivate;
        if (!IsLiveUObject(pawn) || pawn == host || !IsDerivedFrom(pawn, "BPCharacter_Demo_C") ||
            !IsReadable(pawn, sizeof(SDK::ABPCharacter_Demo_C)))
            continue;

        auto* member = reinterpret_cast<SDK::ABPCharacter_Demo_C*>(pawn);
        FloatParameter walk{walkSpeed};
        FloatParameter sprint{sprintSpeed};
        FloatParameter crouch{crouchSpeed};
        Invoke(member, "SetWalkSpeedServer", &walk);
        Invoke(member, "SetSprintSpeedServer", &sprint);
        Invoke(member, "SetCrouchWalkSpeedServer", &crouch);

        member->WalkSpeed = walkSpeed;
        member->SprintSpeed = sprintSpeed;
        member->CrouchWalkSpeed = crouchSpeed;
        member->MaxStamina = maximumStamina;
        member->Stamina = maximumStamina;
        member->ShouldUseStamina = !infiniteStamina;

        SDK::UCharacterMovementComponent* movement = member->CharacterMovement;
        if (IsReadable(movement, sizeof(SDK::UCharacterMovementComponent)) && IsLiveUObject(movement))
        {
            movement->MaxWalkSpeed = member->IsSprinting ? sprintSpeed : walkSpeed;
            movement->MaxWalkSpeedCrouched = crouchSpeed;
        }

        InvokeNoParameters(member, "OnRep_WalkSpeed");
        InvokeNoParameters(member, "OnRep_SprintSpeed");
        InvokeNoParameters(member, "OnRep_CrouchWalkSpeed");
        InvokeNoParameters(member, "OnRep_ShouldUseStamina");
        InvokeNoParameters(member, "FlushNetDormancy");
        InvokeNoParameters(member, "ForceNetUpdate");
        ++modified;
    }
    return modified;
}
}

ExitActivator& ExitActivator::Instance()
{
    static ExitActivator instance;
    return instance;
}

void ExitActivator::Update()
{
    forceTriggerCollision_.store(settings_.forceTriggerCollision, std::memory_order_relaxed);
    scanActorsPerTick_.store(std::clamp(settings_.scanActorsPerTick, 50, 2000),
                             std::memory_order_relaxed);
    maintainMemberAttributes_.store(settings_.maintainMemberAttributes, std::memory_order_relaxed);
    memberWalkSpeed_.store(std::clamp(settings_.memberWalkSpeed, 50.0f, 5000.0f), std::memory_order_relaxed);
    memberSprintSpeed_.store(std::clamp(settings_.memberSprintSpeed, 50.0f, 8000.0f), std::memory_order_relaxed);
    memberCrouchSpeed_.store(std::clamp(settings_.memberCrouchSpeed, 25.0f, 5000.0f), std::memory_order_relaxed);
    memberMaxStamina_.store(std::clamp(settings_.memberMaxStamina, 1.0f, 10000.0f), std::memory_order_relaxed);
    memberInfiniteStamina_.store(settings_.memberInfiniteStamina, std::memory_order_relaxed);

    if (settings_.enabled && !previousUiEnabled_)
    {
        restartRequested_.store(true, std::memory_order_release);
        runActive_.store(true, std::memory_order_release);
        completed_.store(false, std::memory_order_relaxed);
    }
    else if (!settings_.enabled && previousUiEnabled_)
    {
        runActive_.store(false, std::memory_order_release);
        scanning_.store(false, std::memory_order_release);
        completed_.store(false, std::memory_order_relaxed);
        std::lock_guard lock(notificationMutex_);
        markers_.clear();
    }
    previousUiEnabled_ = settings_.enabled;
}

bool ExitActivator::NeedsGameThreadTick() const noexcept
{
    return runActive_.load(std::memory_order_acquire) ||
        teleportAllRequested_.load(std::memory_order_acquire) ||
        applyMemberAttributesRequested_.load(std::memory_order_acquire) ||
        maintainMemberAttributes_.load(std::memory_order_acquire) ||
        startClownRequested_.load(std::memory_order_acquire) ||
        completeClownRequested_.load(std::memory_order_acquire) ||
        startRollercoasterRequested_.load(std::memory_order_acquire);
}

void ExitActivator::OnGameThreadTick(const SDK::UObject* tickObject)
{
    if (!NeedsGameThreadTick())
        return;
    SDK::UWorld* world = ResolveWorld();
    SDK::ABPCharacter_Demo_C* character = ResolveLocalCharacter(world);
    if (world == nullptr || character == nullptr || tickObject != character)
        return;

    const bool authority = character->Role == SDK::ENetRole::ROLE_Authority;
    hostAuthority_.store(authority, std::memory_order_relaxed);

    const bool teleportRequested = teleportAllRequested_.exchange(false, std::memory_order_acq_rel);
    const bool applyMemberAttributes = applyMemberAttributesRequested_.exchange(false, std::memory_order_acq_rel);
    const bool startClown = startClownRequested_.exchange(false, std::memory_order_acq_rel);
    const bool completeClown = completeClownRequested_.exchange(false, std::memory_order_acq_rel);
    const bool startCoaster = startRollercoasterRequested_.exchange(false, std::memory_order_acq_rel);
    if (teleportRequested || applyMemberAttributes || startClown || completeClown || startCoaster)
    {
        if (!authority)
        {
            std::lock_guard lock(notificationMutex_);
            notificationQueue_.push_back("[Host] This action requires room-host authority");
        }
        else
        {
            if (teleportRequested)
            {
                const std::size_t count = TeleportPlayersToHost(world, character);
                lastTeleportedPlayers_.store(count, std::memory_order_relaxed);
                std::lock_guard lock(notificationMutex_);
                notificationQueue_.push_back("[Host] Teleported members: " + std::to_string(count));
            }

            if (applyMemberAttributes)
            {
                const std::size_t count = ApplyAttributesToMembers(
                    world, character,
                    memberWalkSpeed_.load(std::memory_order_relaxed),
                    memberSprintSpeed_.load(std::memory_order_relaxed),
                    memberCrouchSpeed_.load(std::memory_order_relaxed),
                    memberMaxStamina_.load(std::memory_order_relaxed),
                    memberInfiniteStamina_.load(std::memory_order_relaxed));
                lastModifiedMembers_.store(count, std::memory_order_relaxed);
                nextMemberAttributesApplyTick_.store(GetTickCount64() + 1000, std::memory_order_relaxed);
                std::lock_guard lock(notificationMutex_);
                notificationQueue_.push_back("[Host] Updated member attributes: " + std::to_string(count));
            }

            SDK::AActor* clownManager = nullptr;
            SDK::AActor* coasterManager = nullptr;
            if (startClown)
            {
                clownManager = FindActorByClassName(world, "BP_ClownManager_C");
                clownManagerDetected_.store(clownManager != nullptr, std::memory_order_relaxed);
                bool applied = false;
                if (clownManager != nullptr)
                {
                    WriteBoolProperty(clownManager, "ShouldSpawn", true);
                    applied |= InvokeNoParameters(clownManager, "StartSpawning");
                    applied |= InvokeNoParameters(clownManager, "SpawnTrick");
                    InvokeNoParameters(clownManager, "ForceNetUpdate");
                }
                std::lock_guard lock(notificationMutex_);
                notificationQueue_.push_back(clownManager == nullptr ?
                    "[Level 94] Clown manager is not loaded" :
                    (applied ? "[Level 94] Clown challenge started" :
                               "[Level 94] Clown manager event failed"));
            }

            if (completeClown || startCoaster)
            {
                coasterManager = FindRollercoasterManager(world);
                rollercoasterManagerDetected_.store(coasterManager != nullptr, std::memory_order_relaxed);
            }
            if (completeClown)
            {
                bool applied = false;
                const std::size_t poweredCoasters = SetRollercoastersPowered(world);
                applied |= poweredCoasters > 0;
                if (coasterManager != nullptr)
                {
                    applied |= WriteIntProperty(coasterManager, "CurrentTime", 100);
                    applied |= WriteBoolProperty(coasterManager, "DidFinish", true);
                    applied |= InvokeNoParameters(coasterManager, "OnRep_CurrentTime");
                    applied |= InvokeNoParameters(coasterManager, "MC_Finish");
                    InvokeNoParameters(coasterManager, "FlushNetDormancy");
                    InvokeNoParameters(coasterManager, "ForceNetUpdate");
                    rollercoasterTime_.store(ReadIntProperty(coasterManager, "CurrentTime"),
                                             std::memory_order_relaxed);
                }
                std::lock_guard lock(notificationMutex_);
                notificationQueue_.push_back(applied ?
                    "[Level 94] Challenge completed; powered coasters: " +
                        std::to_string(poweredCoasters) :
                    "[Level 94] Roller-coaster manager is not loaded");
            }
            if (startCoaster)
            {
                bool applied = false;
                const std::size_t poweredCoasters = SetRollercoastersPowered(world);
                applied |= poweredCoasters > 0;
                if (coasterManager != nullptr)
                {
                    applied |= WriteBoolProperty(coasterManager, "DidFinish", true);
                    applied |= InvokeNoParameters(coasterManager, "MC_Finish");
                    applied |= InvokeNoParameters(coasterManager, "CheckPlayers");
                    applied |= InvokeNoParameters(coasterManager, "StartCoaster");
                    InvokeNoParameters(coasterManager, "FlushNetDormancy");
                    InvokeNoParameters(coasterManager, "ForceNetUpdate");
                }
                std::lock_guard lock(notificationMutex_);
                notificationQueue_.push_back(coasterManager == nullptr && poweredCoasters == 0 ?
                    "[Level 94] Roller-coaster manager is not loaded" :
                    (applied ? "[Level 94] Roller coaster started" :
                               "[Level 94] Roller-coaster event failed"));
            }
        }
    }

    if (authority && maintainMemberAttributes_.load(std::memory_order_relaxed) &&
        GetTickCount64() >= nextMemberAttributesApplyTick_.load(std::memory_order_relaxed))
    {
        const std::size_t count = ApplyAttributesToMembers(
            world, character,
            memberWalkSpeed_.load(std::memory_order_relaxed),
            memberSprintSpeed_.load(std::memory_order_relaxed),
            memberCrouchSpeed_.load(std::memory_order_relaxed),
            memberMaxStamina_.load(std::memory_order_relaxed),
            memberInfiniteStamina_.load(std::memory_order_relaxed));
        lastModifiedMembers_.store(count, std::memory_order_relaxed);
        nextMemberAttributesApplyTick_.store(GetTickCount64() + 1000, std::memory_order_relaxed);
    }

    if (!runActive_.load(std::memory_order_acquire))
        return;
    if (restartRequested_.exchange(false, std::memory_order_acq_rel))
    {
        g_runtime = {};
        candidates_.store(0, std::memory_order_relaxed);
        activated_.store(0, std::memory_order_relaxed);
        scanning_.store(false, std::memory_order_release);
        std::lock_guard lock(notificationMutex_);
        markers_.clear();
    }

    if (world != g_runtime.world || world->Index != g_runtime.worldIndex)
    {
        g_runtime = {};
        g_runtime.world = world;
        g_runtime.worldIndex = world->Index;
        candidates_.store(0, std::memory_order_relaxed);
        activated_.store(0, std::memory_order_relaxed);
        std::lock_guard lock(notificationMutex_);
        markers_.clear();
    }

    if (!scanning_.load(std::memory_order_acquire))
    {
        g_runtime.levelIndex = 0;
        g_runtime.actorIndex = 0;
        g_runtime.currentCandidates = 0;
        scanning_.store(true, std::memory_order_release);
    }

    const auto levels = world->Levels;
    if (!levels.IsValid() || levels.Num() <= 0 || levels.Num() > 512 ||
        !IsReadable(levels.GetDataPtr(), sizeof(SDK::ULevel*) * levels.Num()))
    {
        scanning_.store(false, std::memory_order_release);
        return;
    }

    int budget = scanActorsPerTick_.load(std::memory_order_relaxed);
    while (budget > 0 && g_runtime.levelIndex < levels.Num())
    {
        SDK::ULevel* level = levels.GetDataPtr()[g_runtime.levelIndex];
        if (!IsReadable(level, 0xA8) || !IsLiveUObject(level))
        {
            ++g_runtime.levelIndex;
            g_runtime.actorIndex = 0;
            continue;
        }
        const auto actors = level->Actors;
        if (!actors.IsValid() || actors.Num() < 0 || actors.Num() > 100000 ||
            (actors.Num() > 0 && !IsReadable(actors.GetDataPtr(),
                                             sizeof(SDK::AActor*) * actors.Num())))
        {
            ++g_runtime.levelIndex;
            g_runtime.actorIndex = 0;
            continue;
        }

        while (budget > 0 && g_runtime.actorIndex < actors.Num())
        {
            SDK::AActor* actor = actors.GetDataPtr()[g_runtime.actorIndex++];
            --budget;
            std::string className;
            if (!IsExitCandidate(actor, className))
                continue;
            ++g_runtime.currentCandidates;

            ActorRecord& record = g_runtime.records[actor];
            if (record.objectIndex != actor->Index)
                record = {actor->Index, false};

            const bool hidden = actor->bHidden != 0 || actor->bActorEnableCollision == 0 ||
                (IsReadable(actor->RootComponent, sizeof(SDK::USceneComponent)) &&
                 (actor->RootComponent->bVisible == 0 || actor->RootComponent->bHiddenInGame != 0));
            const bool shouldForce = !record.forcedOnce;
            if (!shouldForce)
                continue;

            if (ForceActivateExit(actor,
                    forceTriggerCollision_.load(std::memory_order_relaxed)))
            {
                const bool wasForced = record.forcedOnce;
                const bool firstActivation = !wasForced || hidden;
                record.forcedOnce = true;
                if (!wasForced)
                {
                    activated_.fetch_add(1, std::memory_order_relaxed);
                    SDK::USceneComponent* root = actor->RootComponent;
                    if (IsReadable(root, sizeof(SDK::USceneComponent)) && IsLiveUObject(root))
                    {
                        const SDK::FVector location = root->RelativeLocation;
                        std::lock_guard lock(notificationMutex_);
                        markers_.push_back({location.X, location.Y, location.Z, className});
                    }
                }
                if (firstActivation)
                {
                    std::lock_guard lock(notificationMutex_);
                    notificationQueue_.push_back("[Exit] Forced active: " + className);
                    core::Logf("forced exit active: %s actor=%p authority=%d",
                               className.c_str(), actor, authority ? 1 : 0);
                }
            }
        }

        if (g_runtime.actorIndex >= actors.Num())
        {
            ++g_runtime.levelIndex;
            g_runtime.actorIndex = 0;
        }
    }

    if (g_runtime.levelIndex >= levels.Num())
    {
        candidates_.store(g_runtime.currentCandidates, std::memory_order_relaxed);
        scanning_.store(false, std::memory_order_release);
        completed_.store(true, std::memory_order_release);
        runActive_.store(false, std::memory_order_release);
    }
}

void ExitActivator::SetGameThreadHookReady(const bool ready) noexcept
{
    hookReady_.store(ready, std::memory_order_release);
}

ExitActivatorStatus ExitActivator::Status() const noexcept
{
    return {
        hookReady_.load(std::memory_order_acquire),
        hostAuthority_.load(std::memory_order_relaxed),
        scanning_.load(std::memory_order_acquire),
        completed_.load(std::memory_order_acquire),
        candidates_.load(std::memory_order_relaxed),
        activated_.load(std::memory_order_relaxed),
        lastTeleportedPlayers_.load(std::memory_order_relaxed),
        lastModifiedMembers_.load(std::memory_order_relaxed),
        clownManagerDetected_.load(std::memory_order_relaxed),
        rollercoasterManagerDetected_.load(std::memory_order_relaxed),
        rollercoasterTime_.load(std::memory_order_relaxed)
    };
}

void ExitActivator::RequestTeleportAllPlayers() noexcept
{
    teleportAllRequested_.store(true, std::memory_order_release);
}

void ExitActivator::RequestApplyMemberAttributes() noexcept
{
    memberWalkSpeed_.store(std::clamp(settings_.memberWalkSpeed, 50.0f, 5000.0f), std::memory_order_relaxed);
    memberSprintSpeed_.store(std::clamp(settings_.memberSprintSpeed, 50.0f, 8000.0f), std::memory_order_relaxed);
    memberCrouchSpeed_.store(std::clamp(settings_.memberCrouchSpeed, 25.0f, 5000.0f), std::memory_order_relaxed);
    memberMaxStamina_.store(std::clamp(settings_.memberMaxStamina, 1.0f, 10000.0f), std::memory_order_relaxed);
    memberInfiniteStamina_.store(settings_.memberInfiniteStamina, std::memory_order_relaxed);
    applyMemberAttributesRequested_.store(true, std::memory_order_release);
}

void ExitActivator::RequestStartClownChallenge() noexcept
{
    startClownRequested_.store(true, std::memory_order_release);
}

void ExitActivator::RequestCompleteClownChallenge() noexcept
{
    completeClownRequested_.store(true, std::memory_order_release);
}

void ExitActivator::RequestStartRollercoaster() noexcept
{
    startRollercoasterRequested_.store(true, std::memory_order_release);
}

void ExitActivator::SnapshotMarkers(std::vector<ActivatedExitMarker>& destination)
{
    std::lock_guard lock(notificationMutex_);
    destination = markers_;
}

void ExitActivator::DrainNotifications(std::vector<std::string>& destination)
{
    std::lock_guard lock(notificationMutex_);
    for (std::string& message : notificationQueue_)
        destination.push_back(std::move(message));
    notificationQueue_.clear();
}

void ExitActivator::Reset()
{
    runActive_.store(false, std::memory_order_release);
    restartRequested_.store(false, std::memory_order_release);
    scanning_.store(false, std::memory_order_release);
    completed_.store(false, std::memory_order_relaxed);
    hostAuthority_.store(false, std::memory_order_relaxed);
    candidates_.store(0, std::memory_order_relaxed);
    activated_.store(0, std::memory_order_relaxed);
    lastTeleportedPlayers_.store(0, std::memory_order_relaxed);
    lastModifiedMembers_.store(0, std::memory_order_relaxed);
    teleportAllRequested_.store(false, std::memory_order_release);
    applyMemberAttributesRequested_.store(false, std::memory_order_release);
    maintainMemberAttributes_.store(false, std::memory_order_release);
    nextMemberAttributesApplyTick_.store(0, std::memory_order_relaxed);
    startClownRequested_.store(false, std::memory_order_release);
    completeClownRequested_.store(false, std::memory_order_release);
    startRollercoasterRequested_.store(false, std::memory_order_release);
    clownManagerDetected_.store(false, std::memory_order_relaxed);
    rollercoasterManagerDetected_.store(false, std::memory_order_relaxed);
    rollercoasterTime_.store(-1, std::memory_order_relaxed);
    g_runtime = {};
    std::lock_guard lock(notificationMutex_);
    notificationQueue_.clear();
    markers_.clear();
    previousUiEnabled_ = false;
}
}

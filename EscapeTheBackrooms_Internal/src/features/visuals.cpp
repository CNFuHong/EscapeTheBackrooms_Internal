#include "features/visuals.hpp"

#include "core/logger.hpp"
#include "game/unreal_safety.hpp"

#include <Windows.h>
#include <SDK/BPCharacter_Demo_classes.hpp>
#include <SDK/BPCharacter_Demo_parameters.hpp>
#include <SDK/BP_Item_Flashlight_classes.hpp>
#include <SDK/BP_RowBoat_classes.hpp>
#include <SDK/Engine_parameters.hpp>
#include <SDK/Player_AnimBP_classes.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <unordered_map>

namespace etb::features
{
namespace
{
using game::IsLiveUObject;
using game::IsReadable;

SDK::UWorld* ResolveWorld()
{
    const uintptr_t base = SDK::InSDKUtils::GetImageBase();
    auto** worldAddress = reinterpret_cast<SDK::UWorld**>(base + SDK::Offsets::GWorld);
    if (!IsReadable(worldAddress, sizeof(*worldAddress)))
        return nullptr;

    SDK::UWorld* world = *worldAddress;
    return IsReadable(world, sizeof(SDK::UWorld)) && IsLiveUObject(world) ? world : nullptr;
}

SDK::APawn* ResolveLocalPawn()
{
    SDK::UWorld* world = ResolveWorld();
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
    if (!IsLiveUObject(pawn))
        pawn = controller->Pawn;
    return IsReadable(pawn, sizeof(SDK::APawn)) && IsLiveUObject(pawn) ? pawn : nullptr;
}

bool IsPawnClass(const SDK::UObject* object, const char* first, const char* second = nullptr)
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
            if (name == first || (second != nullptr && name == second))
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


bool SetOwnerNoSee(SDK::UPrimitiveComponent* component, bool ownerNoSee)
{
    if (!IsLiveUObject(component))
        return false;

    SDK::UFunction* function = FindFunction(component, "SetOwnerNoSee");
    if (!game::CanProcessEvent(component, function))
        return false;

    SDK::Params::PrimitiveComponent_SetOwnerNoSee parameters{};
    parameters.bNewOwnerNoSee = ownerNoSee;
    const auto flags = function->FunctionFlags;
    function->FunctionFlags |= 0x400;
    const bool invoked = game::ProcessEventSafe(component, function, &parameters);
    if (IsLiveUObject(function))
        function->FunctionFlags = flags;
    return invoked;
}

bool SetVisibility(SDK::USceneComponent* component, bool visible)
{
    if (!IsLiveUObject(component))
        return false;

    SDK::UFunction* function = FindFunction(component, "SetVisibility");
    if (!game::CanProcessEvent(component, function))
        return false;

    SDK::Params::SceneComponent_SetVisibility parameters{};
    parameters.bNewVisibility = visible;
    parameters.bPropagateToChildren = false;
    const auto flags = function->FunctionFlags;
    function->FunctionFlags |= 0x400;
    const bool invoked = game::ProcessEventSafe(component, function, &parameters);
    if (IsLiveUObject(function))
        function->FunctionFlags = flags;
    return invoked;
}

bool SetRelativeLocation(SDK::USceneComponent* component, const SDK::FVector& location)
{
    static SDK::UClass* cachedClass = nullptr;
    static SDK::UFunction* function = nullptr;
    if (!IsLiveUObject(component))
        return false;

    if (component->Class != cachedClass)
    {
        cachedClass = component->Class;
        function = FindFunction(component, "K2_SetRelativeLocation");
    }
    if (!game::CanProcessEvent(component, function))
        return false;

    SDK::Params::SceneComponent_K2_SetRelativeLocation parameters{};
    parameters.NewLocation = location;
    parameters.bSweep = false;
    parameters.bTeleport = true;
    const auto flags = function->FunctionFlags;
    function->FunctionFlags |= 0x400;
    const bool invoked = game::ProcessEventSafe(component, function, &parameters);
    if (IsLiveUObject(function))
        function->FunctionFlags = flags;
    return invoked;
}

bool SetRelativeRotation(SDK::USceneComponent* component, const SDK::FRotator& rotation)
{
    static SDK::UClass* cachedClass = nullptr;
    static SDK::UFunction* function = nullptr;
    if (!IsLiveUObject(component))
        return false;

    if (component->Class != cachedClass)
    {
        cachedClass = component->Class;
        function = FindFunction(component, "K2_SetRelativeRotation");
    }
    if (!game::CanProcessEvent(component, function))
        return false;

    SDK::Params::SceneComponent_K2_SetRelativeRotation parameters{};
    parameters.NewRotation = rotation;
    parameters.bSweep = false;
    parameters.bTeleport = true;
    const auto flags = function->FunctionFlags;
    function->FunctionFlags |= 0x400;
    const bool invoked = game::ProcessEventSafe(component, function, &parameters);
    if (IsLiveUObject(function))
        function->FunctionFlags = flags;
    return invoked;
}

bool SetComponentReplicated(SDK::UActorComponent* component, const bool replicated)
{
    static SDK::UClass* cachedClass = nullptr;
    static SDK::UFunction* function = nullptr;
    if (!IsLiveUObject(component))
        return false;

    if (component->Class != cachedClass)
    {
        cachedClass = component->Class;
        function = FindFunction(component, "SetIsReplicated");
    }
    if (!game::CanProcessEvent(component, function))
        return false;

    SDK::Params::ActorComponent_SetIsReplicated parameters{};
    parameters.ShouldReplicate = replicated;
    const auto flags = function->FunctionFlags;
    function->FunctionFlags |= 0x400;
    const bool invoked = game::ProcessEventSafe(component, function, &parameters);
    if (IsLiveUObject(function))
        function->FunctionFlags = flags;
    return invoked;
}

bool SetFogDensity(SDK::UExponentialHeightFogComponent* component, const float density)
{
    static SDK::UFunction* function = nullptr;
    if (!IsLiveUObject(component))
        return false;
    if (!IsLiveUObject(function))
        function = FindFunction(component, "SetFogDensity");
    if (!game::CanProcessEvent(component, function))
        return false;

    SDK::Params::ExponentialHeightFogComponent_SetFogDensity parameters{};
    parameters.Value = density;
    const auto flags = function->FunctionFlags;
    function->FunctionFlags |= 0x400;
    const bool invoked = game::ProcessEventSafe(component, function, &parameters);
    if (IsLiveUObject(function))
        function->FunctionFlags = flags;
    return invoked;
}

bool SetVolumetricFog(SDK::UExponentialHeightFogComponent* component, const bool enabled)
{
    static SDK::UFunction* function = nullptr;
    if (!IsLiveUObject(component))
        return false;
    if (!IsLiveUObject(function))
        function = FindFunction(component, "SetVolumetricFog");
    if (!game::CanProcessEvent(component, function))
        return false;

    SDK::Params::ExponentialHeightFogComponent_SetVolumetricFog parameters{};
    parameters.bNewValue = enabled;
    const auto flags = function->FunctionFlags;
    function->FunctionFlags |= 0x400;
    const bool invoked = game::ProcessEventSafe(component, function, &parameters);
    if (IsLiveUObject(function))
        function->FunctionFlags = flags;
    return invoked;
}

bool SetSkyLightIntensity(SDK::USkyLightComponent* component, const float intensity)
{
    static SDK::UFunction* function = nullptr;
    if (!IsLiveUObject(component))
        return false;
    if (!IsLiveUObject(function))
        function = FindFunction(component, "SetIntensity");
    if (!game::CanProcessEvent(component, function))
        return false;

    SDK::Params::SkyLightComponent_SetIntensity parameters{};
    parameters.NewIntensity = intensity;
    const auto flags = function->FunctionFlags;
    function->FunctionFlags |= 0x400;
    const bool invoked = game::ProcessEventSafe(component, function, &parameters);
    if (IsLiveUObject(function))
        function->FunctionFlags = flags;
    return invoked;
}

bool SetSkyLightIndirectIntensity(SDK::USkyLightComponent* component, const float intensity)
{
    static SDK::UFunction* function = nullptr;
    if (!IsLiveUObject(component))
        return false;
    if (!IsLiveUObject(function))
        function = FindFunction(component, "SetIndirectLightingIntensity");
    if (!game::CanProcessEvent(component, function))
        return false;

    SDK::Params::SkyLightComponent_SetIndirectLightingIntensity parameters{};
    parameters.NewIntensity = intensity;
    const auto flags = function->FunctionFlags;
    function->FunctionFlags |= 0x400;
    const bool invoked = game::ProcessEventSafe(component, function, &parameters);
    if (IsLiveUObject(function))
        function->FunctionFlags = flags;
    return invoked;
}

bool SetLocalLightIntensity(SDK::ULightComponent* component, const float intensity)
{
    static SDK::UFunction* function = nullptr;
    if (!IsLiveUObject(component))
        return false;
    if (!IsLiveUObject(function))
        function = FindFunction(component, "SetIntensity");
    if (!game::CanProcessEvent(component, function))
        return false;

    SDK::Params::LightComponent_SetIntensity parameters{};
    parameters.NewIntensity = intensity;
    const auto flags = function->FunctionFlags;
    function->FunctionFlags |= 0x400;
    const bool invoked = game::ProcessEventSafe(component, function, &parameters);
    if (IsLiveUObject(function))
        function->FunctionFlags = flags;
    return invoked;
}

bool SetLocalLightIndirectIntensity(SDK::ULightComponent* component, const float intensity)
{
    static SDK::UFunction* function = nullptr;
    if (!IsLiveUObject(component))
        return false;
    if (!IsLiveUObject(function))
        function = FindFunction(component, "SetIndirectLightingIntensity");
    if (!game::CanProcessEvent(component, function))
        return false;

    SDK::Params::LightComponent_SetIndirectLightingIntensity parameters{};
    parameters.NewIntensity = intensity;
    const auto flags = function->FunctionFlags;
    function->FunctionFlags |= 0x400;
    const bool invoked = game::ProcessEventSafe(component, function, &parameters);
    if (IsLiveUObject(function))
        function->FunctionFlags = flags;
    return invoked;
}

bool SetLocalLightRadius(SDK::ULocalLightComponent* component, const float radius)
{
    static SDK::UFunction* function = nullptr;
    if (!IsLiveUObject(component))
        return false;
    if (!IsLiveUObject(function))
        function = FindFunction(component, "SetAttenuationRadius");
    if (!game::CanProcessEvent(component, function))
        return false;

    SDK::Params::LocalLightComponent_SetAttenuationRadius parameters{};
    parameters.NewRadius = radius;
    const auto flags = function->FunctionFlags;
    function->FunctionFlags |= 0x400;
    const bool invoked = game::ProcessEventSafe(component, function, &parameters);
    if (IsLiveUObject(function))
        function->FunctionFlags = flags;
    return invoked;
}

bool SetSpotLightOuterCone(SDK::USpotLightComponent* component, const float angle)
{
    static SDK::UFunction* function = nullptr;
    if (!IsLiveUObject(component))
        return false;
    if (!IsLiveUObject(function))
        function = FindFunction(component, "SetOuterConeAngle");
    if (!game::CanProcessEvent(component, function))
        return false;

    SDK::Params::SpotLightComponent_SetOuterConeAngle parameters{};
    parameters.NewOuterConeAngle = angle;
    const auto flags = function->FunctionFlags;
    function->FunctionFlags |= 0x400;
    const bool invoked = game::ProcessEventSafe(component, function, &parameters);
    if (IsLiveUObject(function))
        function->FunctionFlags = flags;
    return invoked;
}

bool SetPointLightFalloff(SDK::UPointLightComponent* component, const float exponent)
{
    static SDK::UFunction* function = nullptr;
    if (!IsLiveUObject(component))
        return false;
    if (!IsLiveUObject(function))
        function = FindFunction(component, "SetLightFalloffExponent");
    if (!game::CanProcessEvent(component, function))
        return false;

    SDK::Params::PointLightComponent_SetLightFalloffExponent parameters{};
    parameters.NewLightFalloffExponent = exponent;
    const auto flags = function->FunctionFlags;
    function->FunctionFlags |= 0x400;
    const bool invoked = game::ProcessEventSafe(component, function, &parameters);
    if (IsLiveUObject(function))
        function->FunctionFlags = flags;
    return invoked;
}

bool SetComponentHidden(SDK::USceneComponent* component, const bool hidden)
{
    static SDK::UFunction* function = nullptr;
    if (!IsLiveUObject(component))
        return false;
    if (!IsLiveUObject(function))
        function = FindFunction(component, "SetHiddenInGame");
    if (!game::CanProcessEvent(component, function))
        return false;

    SDK::Params::SceneComponent_SetHiddenInGame parameters{};
    parameters.NewHidden = hidden;
    parameters.bPropagateToChildren = false;
    const auto flags = function->FunctionFlags;
    function->FunctionFlags |= 0x400;
    const bool invoked = game::ProcessEventSafe(component, function, &parameters);
    if (IsLiveUObject(function))
        function->FunctionFlags = flags;
    return invoked;
}

bool SetComponentActive(SDK::UActorComponent* component, const bool active)
{
    static SDK::UFunction* function = nullptr;
    if (!IsLiveUObject(component))
        return false;
    if (!IsLiveUObject(function))
        function = FindFunction(component, "SetActive");
    if (!game::CanProcessEvent(component, function))
        return false;

    SDK::Params::ActorComponent_SetActive parameters{};
    parameters.bNewActive = active;
    parameters.bReset = true;
    const auto flags = function->FunctionFlags;
    function->FunctionFlags |= 0x400;
    const bool invoked = game::ProcessEventSafe(component, function, &parameters);
    if (IsLiveUObject(function))
        function->FunctionFlags = flags;
    return invoked;
}

bool SetLightCastShadows(SDK::ULightComponentBase* component, const bool castShadows)
{
    static SDK::UFunction* function = nullptr;
    if (!IsLiveUObject(component))
        return false;
    if (!IsLiveUObject(function))
        function = FindFunction(component, "SetCastShadows");
    if (!game::CanProcessEvent(component, function))
        return false;

    SDK::Params::LightComponentBase_SetCastShadows parameters{};
    parameters.bNewValue = castShadows;
    const auto flags = function->FunctionFlags;
    function->FunctionFlags |= 0x400;
    const bool invoked = game::ProcessEventSafe(component, function, &parameters);
    if (IsLiveUObject(function))
        function->FunctionFlags = flags;
    return invoked;
}

bool SetSceneMobility(SDK::USceneComponent* component, const SDK::EComponentMobility mobility)
{
    static SDK::UFunction* function = nullptr;
    if (!IsLiveUObject(component))
        return false;
    if (!IsLiveUObject(function))
        function = FindFunction(component, "SetMobility");
    if (!game::CanProcessEvent(component, function))
        return false;

    SDK::Params::SceneComponent_SetMobility parameters{};
    parameters.NewMobility = mobility;
    const auto flags = function->FunctionFlags;
    function->FunctionFlags |= 0x400;
    const bool invoked = game::ProcessEventSafe(component, function, &parameters);
    if (IsLiveUObject(function))
        function->FunctionFlags = flags;
    return invoked;
}

__declspec(noinline) bool IsClassObject(const SDK::UObject* object)
{
    if (object == nullptr || object->Class == nullptr)
        return false;
    __try
    {
        return static_cast<std::uint64_t>(
            object->Class->CastFlags & SDK::EClassCastFlags::Class) != 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

SDK::UPointLightComponent* AddPointLightComponent(SDK::AActor* actor)
{
    static SDK::UClass* cachedClass = nullptr;
    static SDK::UFunction* function = nullptr;
    static SDK::UClass* lightClass = nullptr;
    if (!IsLiveUObject(actor))
        return nullptr;
    if (actor->Class != cachedClass)
    {
        cachedClass = actor->Class;
        function = FindFunction(actor, "AddComponentByClass");
    }
    if (!IsLiveUObject(lightClass))
    {
        const int count = SDK::UObject::GObjects->Num();
        for (int index = 0; index < count; ++index)
        {
            SDK::UObject* object = SDK::UObject::GObjects->GetByIndex(index);
            if (!IsLiveUObject(object) || object->Class == nullptr)
                continue;
            if (!IsClassObject(object))
                continue;
            std::string name;
            if (game::TryFNameToString(object->Name, name) && name == "PointLightComponent")
            {
                lightClass = static_cast<SDK::UClass*>(object);
                break;
            }
        }
    }
    if (!game::CanProcessEvent(actor, function) || !IsLiveUObject(lightClass))
        return nullptr;

    SDK::Params::Actor_AddComponentByClass parameters{};
    parameters.Class_0 = lightClass;
    parameters.bManualAttachment = false;
    parameters.RelativeTransform.Rotation.W = 1.0f;
    parameters.RelativeTransform.Translation = {0.0f, 0.0f, 55.0f};
    parameters.RelativeTransform.Scale3D = {1.0f, 1.0f, 1.0f};
    parameters.bDeferredFinish = false;
    const auto flags = function->FunctionFlags;
    function->FunctionFlags |= 0x400;
    const bool invoked = game::ProcessEventSafe(actor, function, &parameters);
    if (IsLiveUObject(function))
        function->FunctionFlags = flags;
    auto* component = reinterpret_cast<SDK::UPointLightComponent*>(parameters.ReturnValue);
    return invoked && IsLiveUObject(component) ? component : nullptr;
}

bool DestroyActorComponent(SDK::AActor* actor, SDK::UActorComponent* component)
{
    static SDK::UClass* cachedClass = nullptr;
    static SDK::UFunction* function = nullptr;
    if (!IsLiveUObject(actor) || !IsLiveUObject(component))
        return false;
    if (actor->Class != cachedClass)
    {
        cachedClass = actor->Class;
        function = FindFunction(actor, "K2_DestroyComponent");
    }
    if (!game::CanProcessEvent(actor, function))
        return false;

    SDK::Params::Actor_K2_DestroyComponent parameters{};
    parameters.Component = component;
    const auto flags = function->FunctionFlags;
    function->FunctionFlags |= 0x400;
    const bool invoked = game::ProcessEventSafe(actor, function, &parameters);
    if (IsLiveUObject(function))
        function->FunctionFlags = flags;
    return invoked;
}

constexpr float NetworkDerpMagicX = 712345.25f;
constexpr float NetworkDerpMagicY = -83456.75f;
constexpr float NetworkDerpMagicZ = 1942.5f;

// 全关卡光源扫描每次 tick 处理的 actor 上限。
// 分帧执行以避免一次性遍历全部关卡造成的游戏卡顿；数值越小越稳，扫描周期越长。
constexpr int WorldLightScanActorsPerCall = 256;

bool IsNetworkDerpPacket(const SDK::Params::BPCharacter_Demo_C_StartPushingActor_SERVER& packet)
{
    return packet.PushableActor == nullptr && packet.B.X == NetworkDerpMagicX &&
           packet.B.Y == NetworkDerpMagicY && packet.B.Z == NetworkDerpMagicZ;
}

__declspec(noinline) bool ReadNetworkDerpPacket(void* parameters, SDK::FRotator& rotation)
{
    if (parameters == nullptr)
        return false;
    __try
    {
        const auto* packet =
            static_cast<const SDK::Params::BPCharacter_Demo_C_StartPushingActor_SERVER*>(parameters);
        if (!IsNetworkDerpPacket(*packet))
            return false;
        rotation = packet->NewRotation;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool SendNetworkDerp(SDK::ABPCharacter_Demo_C* character, const SDK::FRotator& rotation)
{
    static SDK::UClass* cachedClass = nullptr;
    static SDK::UFunction* function = nullptr;
    if (!IsLiveUObject(character))
        return false;

    if (character->Class != cachedClass)
    {
        cachedClass = character->Class;
        function = FindFunction(character, "StartPushingActor_SERVER");
    }
    if (!game::CanProcessEvent(character, function))
        return false;

    SDK::Params::BPCharacter_Demo_C_StartPushingActor_SERVER packet{};
    packet.PushableActor = nullptr;
    packet.B = {NetworkDerpMagicX, NetworkDerpMagicY, NetworkDerpMagicZ};
    packet.NewRotation = rotation;
    const auto flags = function->FunctionFlags;
    function->FunctionFlags |= 0x400;
    const bool invoked = game::ProcessEventSafe(character, function, &packet);
    if (IsLiveUObject(function))
        function->FunctionFlags = flags;
    return invoked;
}

struct NightVisionOriginal
{
    float blendWeight = 0.0f;
    float exposureBias = 0.0f;
    bool overrideExposureBias = false;
    SDK::FVector4 colorGamma{};
    SDK::FVector4 colorGain{};
    SDK::FVector4 colorOffset{};
    SDK::FVector4 shadowGamma{};
    SDK::FVector4 shadowGain{};
    SDK::FVector4 shadowOffset{};
    float shadowMaximum = 0.0f;
    SDK::FLinearColor indirectLightingColor{};
    float indirectLightingIntensity = 0.0f;
    float ambientOcclusionIntensity = 0.0f;
    bool overrideColorGamma = false;
    bool overrideColorGain = false;
    bool overrideColorOffset = false;
    bool overrideShadowGamma = false;
    bool overrideShadowGain = false;
    bool overrideShadowOffset = false;
    bool overrideShadowMaximum = false;
    bool overrideIndirectLightingColor = false;
    bool overrideIndirectLightingIntensity = false;
    bool overrideAmbientOcclusionIntensity = false;
};

struct FogOriginal
{
    SDK::UExponentialHeightFogComponent* component = nullptr;
    std::int32_t componentIndex = -1;
    float density = 0.0f;
    bool volumetric = false;
};

struct SkyLightOriginal
{
    SDK::USkyLightComponent* component = nullptr;
    std::int32_t componentIndex = -1;
    float intensity = 0.0f;
    float indirectIntensity = 0.0f;
    SDK::EComponentMobility mobility = SDK::EComponentMobility::Static;
};

struct DirectionalLightOriginal
{
    SDK::ULightComponent* component = nullptr;
    std::int32_t componentIndex = -1;
    float intensity = 0.0f;
    float indirectIntensity = 0.0f;
    SDK::EComponentMobility mobility = SDK::EComponentMobility::Static;
};

struct FlashlightComponentOriginal
{
    SDK::UPointLightComponent* component = nullptr;
    std::int32_t componentIndex = -1;
    float intensity = 0.0f;
    float radius = 0.0f;
    float outerCone = 0.0f;
    bool spot = false;
    bool inverseSquared = true;
    float falloffExponent = 0.0f;
    bool visible = true;
    bool hidden = false;
    bool active = true;
    bool affectsWorld = true;
    bool castShadows = true;
};

struct ThirdPersonOriginal
{
    float armLength = 0.0f;
    bool collisionTest = true;
    bool usePawnControlRotation = false;
    bool inheritPitch = true;
    bool inheritYaw = true;
    bool inheritRoll = true;
    SDK::FVector cameraRelativeLocation{};
    bool meshOwnerNoSee = false;
    bool armsVisible = true;
    bool legsVisible = true;
    float animationPitch = 0.0f;
};

struct DerpOriginal
{
    SDK::FRotator meshRelativeRotation{};
    float pitchOffset = 0.0f;
    float yawOffset = 0.0f;
    float rollOffset = 0.0f;
    float networkSendAccumulator = 0.0f;
    bool networkVisible = false;
    bool springSettingsValid = false;
    bool springUsePawnControlRotation = false;
    bool springInheritPitch = true;
    bool springInheritYaw = true;
    bool springInheritRoll = true;
};

struct RemoteDerpState
{
    SDK::AActor* actor = nullptr;
    std::int32_t actorIndex = -1;
    SDK::FRotator rotation{};
    ULONGLONG lastPacketMs = 0;
};

class VisualRuntime final
{
public:
    bool Update(const VisualSettings& settings, VisualStatus& status)
    {
        SDK::APawn* pawn = ResolveLocalPawn();
        const bool targetChanged =
            pawn != pawn_ || (pawn != nullptr && pawn->Index != pawnIndex_);
        if (targetChanged)
        {
            const bool oldTargetLive = pawn_ != nullptr && IsLiveUObject(pawn_) &&
                                       pawn_->Index == pawnIndex_;
            Detach(oldTargetLive);
            pawn_ = pawn;
            if (pawn_ != nullptr)
            {
                pawnIndex_ = pawn_->Index;
                if (IsPawnClass(pawn_, "BPCharacter_Demo_C"))
                {
                    character_ = reinterpret_cast<SDK::ABPCharacter_Demo_C*>(pawn_);
                    camera_ = character_->CameraComponent;
                    springArm_ = character_->SpringArm;
                    mesh_ = character_->Mesh;
                    arms_ = character_->Arms;
                    legs_ = character_->Legs;
                    if (IsReadable(mesh_, sizeof(SDK::USkeletalMeshComponent)) &&
                        IsLiveUObject(mesh_->AnimScriptInstance) &&
                        IsPawnClass(mesh_->AnimScriptInstance, "Player_AnimBP_C"))
                    {
                        playerAnim_ = reinterpret_cast<SDK::UPlayer_AnimBP_C*>(
                            mesh_->AnimScriptInstance);
                    }
                    core::Logf("visuals attached to Character=%p Camera=%p SpringArm=%p",
                               character_, camera_, springArm_);
                }
                else if (IsPawnClass(pawn_, "BP_RowBoat_C", "BoatPawn"))
                {
                    boat_ = reinterpret_cast<SDK::ABoatPawn*>(pawn_);
                    camera_ = boat_->InteractableCameraComponent;
                    core::Logf("visuals attached to Boat=%p Camera=%p", boat_, camera_);
                }
            }
        }

        if (pawn_ == nullptr || (!character_ && !boat_) ||
            !IsReadable(pawn_, sizeof(SDK::APawn)) || !IsLiveUObject(pawn_))
        {
            Detach(false);
            status = {};
            return targetChanged;
        }

        status.cameraReady = IsReadable(camera_, sizeof(SDK::UCameraComponent)) && IsLiveUObject(camera_);
        status.springArmReady = IsReadable(springArm_, sizeof(SDK::USpringArmComponent)) &&
                                IsLiveUObject(springArm_);
        status.usingVehicle = boat_ != nullptr;
        status.modelReady = IsReadable(mesh_, sizeof(SDK::USkeletalMeshComponent)) &&
                            IsLiveUObject(mesh_);
        status.suppressedFogComponents =
            suppressedFogCount_.load(std::memory_order_acquire);
        status.boostedSkyLights = boostedSkyLightCount_.load(std::memory_order_acquire);
        status.boostedDirectionalLights =
            boostedDirectionalLightCount_.load(std::memory_order_acquire);

        ApplyThirdPerson(settings, status.cameraReady &&
                         (status.usingVehicle || status.springArmReady));

        SDK::UObject* currentItem = character_ != nullptr ? character_->CurrentItem_Rep : nullptr;
        status.darknessLightReady = IsLiveUObject(persistentLight_) &&
                                    persistentLight_->Index == persistentLightIndex_;
        const std::int32_t currentItemIndex =
            IsLiveUObject(currentItem) ? currentItem->Index : -1;
        bool currentFlashlightOn = false;
        const bool sameObservedItem = currentItem == observedCurrentItem_ &&
                                      currentItemIndex == observedCurrentItemIndex_;
        const bool currentItemIsFlashlight = IsLiveUObject(currentItem) &&
            (sameObservedItem ? observedCurrentItemIsFlashlight_ :
                                IsPawnClass(currentItem, "BP_Item_Flashlight_C"));
        if (currentItemIsFlashlight)
        {
            auto* flashlight = reinterpret_cast<SDK::ABP_Item_Flashlight_C*>(currentItem);
            currentFlashlightOn = flashlight->IsFlashlightOn;
            status.darknessLightReady = status.darknessLightReady ||
                                        IsLiveUObject(flashlight->PointLight);
        }
        const bool itemChanged = currentItem != observedCurrentItem_ ||
                                 currentItemIndex != observedCurrentItemIndex_ ||
                                 currentFlashlightOn != observedFlashlightOn_;
        observedCurrentItem_ = currentItem;
        observedCurrentItemIndex_ = currentItemIndex;
        observedCurrentItemIsFlashlight_ = currentItemIsFlashlight;
        observedFlashlightOn_ = currentFlashlightOn;
        return targetChanged || itemChanged;
    }

    bool GameThreadTick(const VisualSettings& settings, const SDK::UObject* tickObject,
                        const float deltaSeconds, const bool updateNightVision,
                        const bool nightVisionActive)
    {
        ApplyRemoteDerp(tickObject);
        // Re-assert the exposure boost but throttle it: run every frame on the pawn
        // tick (one call per frame) plus a ~40ms fallback on other ticks (e.g. while
        // interacting, when the pawn is paused). Avoids hitting the game thread with
        // a call for every ticking actor every frame.
        if (nightVisionActive || updateNightVision)
        {
            const bool isPawnTick = (tickObject == pawn_);
            const ULONGLONG now = GetTickCount64();
            if (isPawnTick || updateNightVision || now >= nextNightVisionApplyMs_)
            {
                if (!isPawnTick && !updateNightVision)
                    nextNightVisionApplyMs_ = now + 40;
                const bool cameraReady =
                    IsReadable(camera_, sizeof(SDK::UCameraComponent)) && IsLiveUObject(camera_);
                ApplyNightVision(settings, cameraReady);
            }
        }
        if (tickObject != pawn_ || !IsLiveUObject(pawn_))
            return false;

        // Environment brightness stays on for long stretches, so the world must be
        // re-checked periodically: fogs/lights spawned after the initial map load
        // are not covered by the one-shot config change, which is what used to force
        // a manual refresh. The cheap re-assert path inside ApplyNightVisionFog only
        // writes values that actually changed, and the expensive full world scan is
        // still gated by nextWorldLightScanMs_, so this stays cheap per frame.
        if (updateNightVision)
            nextNightVisionFogMs_ = 0;
        if (settings.nightVisionEnabled || updateNightVision)
        {
            const ULONGLONG now = GetTickCount64();
            if (updateNightVision || now >= nextNightVisionFogMs_)
            {
                nextNightVisionFogMs_ = now + 250;
                ApplyNightVisionFog(settings);
            }
        }
        if (character_ == nullptr || !IsLiveUObject(character_))
            return true;
        const bool modelReady = IsReadable(mesh_, sizeof(SDK::USkeletalMeshComponent)) &&
                                IsLiveUObject(mesh_);
        ApplyDerp(settings, modelReady, std::clamp(deltaSeconds, 0.0f, 0.1f));
        return true;
    }

    bool HandleNetworkPacket(const SDK::UObject* object, SDK::UFunction* function,
                             void* parameters)
    {
        if (!IsLiveUObject(object) || !IsLiveUObject(function) || parameters == nullptr)
            return false;

        std::string functionName;
        if (!game::TryFNameToString(function->Name, functionName) ||
            functionName != "StartPushingActor_SERVER")
            return false;

        auto* character = reinterpret_cast<SDK::ABPCharacter_Demo_C*>(
            const_cast<SDK::UObject*>(object));
        if (!IsReadable(character, sizeof(SDK::ABPCharacter_Demo_C)) ||
            character->Role != SDK::ENetRole::ROLE_Authority)
            return false;

        SDK::FRotator rotation{};
        if (!ReadNetworkDerpPacket(parameters, rotation) ||
            !std::isfinite(rotation.Pitch) || !std::isfinite(rotation.Yaw) ||
            !std::isfinite(rotation.Roll))
            return false;

        SDK::USkeletalMeshComponent* mesh = character->Mesh;
        if (!IsReadable(mesh, sizeof(SDK::USkeletalMeshComponent)) || !IsLiveUObject(mesh))
            return true;

        if (mesh->bReplicates == 0)
            SetComponentReplicated(mesh, true);
        SetRelativeRotation(mesh, rotation);
        RememberRemoteDerp(character, rotation);
        return true;
    }

    void Reset()
    {
        SDK::APawn* currentPawn = ResolveLocalPawn();
        Detach(currentPawn != nullptr && currentPawn == pawn_ &&
               currentPawn->Index == pawnIndex_);
    }

private:
    void RememberRemoteDerp(SDK::AActor* actor, const SDK::FRotator& rotation)
    {
        RemoteDerpState* freeSlot = nullptr;
        RemoteDerpState* oldest = &remoteDerp_[0];
        for (auto& state : remoteDerp_)
        {
            if (state.actor == actor && state.actorIndex == actor->Index)
            {
                state.rotation = rotation;
                state.lastPacketMs = GetTickCount64();
                return;
            }
            if (state.actor == nullptr && freeSlot == nullptr)
                freeSlot = &state;
            if (state.lastPacketMs < oldest->lastPacketMs)
                oldest = &state;
        }

        RemoteDerpState& state = freeSlot != nullptr ? *freeSlot : *oldest;
        state.actor = actor;
        state.actorIndex = actor->Index;
        state.rotation = rotation;
        state.lastPacketMs = GetTickCount64();
    }

    void ApplyRemoteDerp(const SDK::UObject* tickObject)
    {
        const ULONGLONG now = GetTickCount64();
        for (auto& state : remoteDerp_)
        {
            if (state.actor == nullptr)
                continue;
            if (now - state.lastPacketMs > 750)
            {
                state = {};
                continue;
            }
            if (tickObject == state.actor && IsLiveUObject(state.actor) &&
                state.actor->Index == state.actorIndex)
            {
                auto* character = reinterpret_cast<SDK::ABPCharacter_Demo_C*>(state.actor);
                SDK::USkeletalMeshComponent* mesh = character->Mesh;
                if (IsLiveUObject(mesh))
                    SetRelativeRotation(mesh, state.rotation);
                return;
            }
        }
    }

    void ApplyNightVision(const VisualSettings& settings, const bool cameraReady)
    {
        if (!settings.nightVisionEnabled || !settings.nightVisionExposureBoost)
        {
            RestoreNightVision();
            return;
        }
        if (!IsLiveUObject(camera_))
        {
            RestoreNightVision();
            return;
        }
        // Transient view/target change (interacting, switching items, spectating):
        // keep the effect applied instead of toggling it off, then re-apply the
        // moment the camera is ready again.
        if (!cameraReady)
            return;

        auto& pp = camera_->PostProcessSettings;
        if (camera_->Index != appliedCameraIndex_)
        {
            nightVisionApplied_ = false;
            nightVisionOriginal_ = {};
            appliedCameraIndex_ = camera_->Index;
        }
        if (!nightVisionApplied_)
        {
            nightVisionOriginal_.blendWeight = camera_->PostProcessBlendWeight;
            nightVisionOriginal_.exposureBias = pp.AutoExposureBias;
            nightVisionOriginal_.overrideExposureBias = pp.bOverride_AutoExposureBias != 0;
            nightVisionOriginal_.colorGamma = pp.ColorGamma;
            nightVisionOriginal_.colorGain = pp.ColorGain;
            nightVisionOriginal_.colorOffset = pp.ColorOffset;
            nightVisionOriginal_.shadowGamma = pp.ColorGammaShadows;
            nightVisionOriginal_.shadowGain = pp.ColorGainShadows;
            nightVisionOriginal_.shadowOffset = pp.ColorOffsetShadows;
            nightVisionOriginal_.shadowMaximum = pp.ColorCorrectionShadowsMax;
            nightVisionOriginal_.indirectLightingColor = pp.IndirectLightingColor;
            nightVisionOriginal_.indirectLightingIntensity = pp.IndirectLightingIntensity;
            nightVisionOriginal_.ambientOcclusionIntensity = pp.AmbientOcclusionIntensity;
            nightVisionOriginal_.overrideColorGamma = pp.bOverride_ColorGamma != 0;
            nightVisionOriginal_.overrideColorGain = pp.bOverride_ColorGain != 0;
            nightVisionOriginal_.overrideColorOffset = pp.bOverride_ColorOffset != 0;
            nightVisionOriginal_.overrideShadowGamma = pp.bOverride_ColorGammaShadows != 0;
            nightVisionOriginal_.overrideShadowGain = pp.bOverride_ColorGainShadows != 0;
            nightVisionOriginal_.overrideShadowOffset = pp.bOverride_ColorOffsetShadows != 0;
            nightVisionOriginal_.overrideShadowMaximum =
                pp.bOverride_ColorCorrectionShadowsMax != 0;
            nightVisionOriginal_.overrideIndirectLightingColor =
                pp.bOverride_IndirectLightingColor != 0;
            nightVisionOriginal_.overrideIndirectLightingIntensity =
                pp.bOverride_IndirectLightingIntensity != 0;
            nightVisionOriginal_.overrideAmbientOcclusionIntensity =
                pp.bOverride_AmbientOcclusionIntensity != 0;
            nightVisionApplied_ = true;
        }

        // 曝光补偿
        camera_->PostProcessBlendWeight = 1.0f;
        pp.bOverride_AutoExposureBias = 1;
        pp.AutoExposureBias = nightVisionOriginal_.exposureBias +
            std::clamp(settings.nightVisionExposureBias, 0.0f, 8.0f);
    }
    void ApplyNightVisionFog(const VisualSettings& settings)
    {
        if (!settings.nightVisionEnabled)
        {
            RestoreNightVisionWorld();
            return;
        }

        ApplyNightVisionFlashlight(settings);

        if (!settings.nightVisionRemoveFog)
            RestoreNightVisionFog();

        const float strength = std::clamp(settings.nightVisionStrength, 1.0f, 20.0f);
        const float skyIntensity = 5.0f * strength;
        const float indirectIntensity = 4.0f * strength;

        unsigned int liveFogCount = 0;
        if (settings.nightVisionRemoveFog)
        {
            for (auto& original : fogOriginals_)
            {
                if (original.component == nullptr)
                    continue;
                if (!IsLiveUObject(original.component) ||
                    original.component->Index != original.componentIndex)
                {
                    original = {};
                    continue;
                }
                ++liveFogCount;
                if (original.component->FogDensity != 0.0f)
                    SetFogDensity(original.component, 0.0f);
                if (original.component->bEnableVolumetricFog)
                    SetVolumetricFog(original.component, false);
            }
        }

        unsigned int liveSkyCount = 0;
        for (auto& original : skyLightOriginals_)
        {
            if (original.component == nullptr)
                continue;
            if (!IsLiveUObject(original.component) ||
                original.component->Index != original.componentIndex)
            {
                original = {};
                continue;
            }
            ++liveSkyCount;
            const float wantedSky = std::max(original.intensity, skyIntensity);
            const float wantedIndirect = std::max(original.indirectIntensity, indirectIntensity);
            if (std::fabs(original.component->Intensity - wantedSky) > 0.01f)
                SetSkyLightIntensity(original.component, wantedSky);
            if (std::fabs(original.component->IndirectLightingIntensity - wantedIndirect) > 0.01f)
                SetSkyLightIndirectIntensity(original.component, wantedIndirect);
        }

        unsigned int liveDirectionalCount = 0;
        for (auto& original : directionalLightOriginals_)
        {
            if (original.component == nullptr)
                continue;
            if (!IsLiveUObject(original.component) ||
                original.component->Index != original.componentIndex)
            {
                original = {};
                continue;
            }
            ++liveDirectionalCount;
            const float wantedIntensity =
                std::max(original.intensity * strength, 2.0f * strength);
            const float wantedIndirect =
                std::max(original.indirectIntensity, 2.0f * strength);
            if (std::fabs(original.component->Intensity - wantedIntensity) > 0.01f)
                SetLocalLightIntensity(original.component, wantedIntensity);
            if (std::fabs(original.component->IndirectLightingIntensity - wantedIndirect) > 0.01f)
                SetLocalLightIndirectIntensity(original.component, wantedIndirect);
        }

        const ULONGLONG now = GetTickCount64();
        if (!worldScanInProgress_ && now < nextWorldLightScanMs_)
        {
            suppressedFogCount_.store(liveFogCount, std::memory_order_release);
            boostedSkyLightCount_.store(liveSkyCount, std::memory_order_release);
            boostedDirectionalLightCount_.store(liveDirectionalCount,
                                                std::memory_order_release);
            return;
        }

        SDK::UWorld* world = ResolveWorld();
        if (world == nullptr)
        {
            worldScanInProgress_ = false;
            worldScanLevelIndex_ = 0;
            worldScanActorIndex_ = 0;
            nextWorldLightScanMs_ = now + 5000;
            suppressedFogCount_.store(liveFogCount, std::memory_order_release);
            boostedSkyLightCount_.store(liveSkyCount, std::memory_order_release);
            boostedDirectionalLightCount_.store(liveDirectionalCount,
                                                std::memory_order_release);
            return;
        }
        const auto levels = world->Levels;
        if (!levels.IsValid() || levels.Num() <= 0 || levels.Num() > 4096 ||
            !IsReadable(levels.GetDataPtr(), sizeof(SDK::ULevel*) * levels.Num()))
        {
            worldScanInProgress_ = false;
            worldScanLevelIndex_ = 0;
            worldScanActorIndex_ = 0;
            nextWorldLightScanMs_ = now + 5000;
            suppressedFogCount_.store(liveFogCount, std::memory_order_release);
            boostedSkyLightCount_.store(liveSkyCount, std::memory_order_release);
            boostedDirectionalLightCount_.store(liveDirectionalCount,
                                                std::memory_order_release);
            return;
        }

        if (!worldScanInProgress_)
        {
            worldScanInProgress_ = true;
            worldScanLevelIndex_ = 0;
            worldScanActorIndex_ = 0;
        }

        const auto captureSkyLight = [&](SDK::USkyLightComponent* component)
        {
            if (!IsLiveUObject(component) ||
                !IsReadable(component, sizeof(SDK::USkyLightComponent)))
                return false;

            SkyLightOriginal* freeSlot = nullptr;
            for (auto& original : skyLightOriginals_)
            {
                if (original.component == component &&
                    original.componentIndex == component->Index)
                    return false;
                if (original.component == nullptr && freeSlot == nullptr)
                    freeSlot = &original;
            }
            if (freeSlot == nullptr)
                return false;

            freeSlot->component = component;
            freeSlot->componentIndex = component->Index;
            freeSlot->intensity = component->Intensity;
            freeSlot->indirectIntensity = component->IndirectLightingIntensity;
            freeSlot->mobility = component->Mobility;
            SetSceneMobility(component, SDK::EComponentMobility::Movable);
            SetSkyLightIntensity(component, std::max(component->Intensity, skyIntensity));
            SetSkyLightIndirectIntensity(
                component, std::max(component->IndirectLightingIntensity, indirectIntensity));
            return true;
        };

        const auto captureWorldLight = [&](SDK::ULightComponent* component)
        {
            if (!IsLiveUObject(component) ||
                !IsReadable(component, sizeof(SDK::ULightComponent)))
                return false;

            DirectionalLightOriginal* freeSlot = nullptr;
            for (auto& original : directionalLightOriginals_)
            {
                if (original.component == component &&
                    original.componentIndex == component->Index)
                    return false;
                if (original.component == nullptr && freeSlot == nullptr)
                    freeSlot = &original;
            }
            if (freeSlot == nullptr)
                return false;

            freeSlot->component = component;
            freeSlot->componentIndex = component->Index;
            freeSlot->intensity = component->Intensity;
            freeSlot->indirectIntensity = component->IndirectLightingIntensity;
            freeSlot->mobility = component->Mobility;
            SetSceneMobility(component, SDK::EComponentMobility::Movable);
            SetLocalLightIntensity(component,
                std::max(component->Intensity * strength, 2.0f * strength));
            SetLocalLightIndirectIntensity(component,
                std::max(component->IndirectLightingIntensity, 2.0f * strength));
            return true;
        };

        // 分帧增量扫描：每次 tick 只处理有限个 actor，未完成则保存游标、下次继续，
        // 避免一次性遍历全部关卡造成的卡顿。
        const int levelCount = levels.Num();
        int scannedActors = 0;
        while (worldScanLevelIndex_ < levelCount &&
               scannedActors < WorldLightScanActorsPerCall)
        {
            SDK::ULevel* level = levels.GetDataPtr()[worldScanLevelIndex_];
            if (!IsLiveUObject(level) || !IsReadable(level, sizeof(SDK::ULevel)))
            {
                ++worldScanLevelIndex_;
                worldScanActorIndex_ = 0;
                continue;
            }
            const auto actors = level->Actors;
            if (!actors.IsValid() || actors.Num() <= 0 || actors.Num() > 1000000 ||
                !IsReadable(actors.GetDataPtr(), sizeof(SDK::AActor*) * actors.Num()))
            {
                ++worldScanLevelIndex_;
                worldScanActorIndex_ = 0;
                continue;
            }

            const int actorCount = actors.Num();
            while (worldScanActorIndex_ < actorCount &&
                   scannedActors < WorldLightScanActorsPerCall)
            {
                SDK::AActor* actor = actors.GetDataPtr()[worldScanActorIndex_];
                ++worldScanActorIndex_;
                ++scannedActors;
                if (!IsLiveUObject(actor))
                    continue;

                auto [kindIterator, inserted] = worldActorClassCache_.try_emplace(
                    actor->Class, static_cast<std::uint8_t>(0));
                if (inserted)
                {
                    if (IsPawnClass(actor, "ExponentialHeightFog"))
                        kindIterator->second = 1;
                    else if (IsPawnClass(actor, "SkyLight"))
                        kindIterator->second = 2;
                    else if (IsPawnClass(actor, "DirectionalLight"))
                        kindIterator->second = 3;
                }
                const std::uint8_t actorKind = kindIterator->second;

                if (settings.nightVisionRemoveFog && actorKind == 1)
                {
                    auto* fogActor = reinterpret_cast<SDK::AExponentialHeightFog*>(actor);
                    auto* component = fogActor->Component;
                    if (!IsLiveUObject(component) ||
                        !IsReadable(component, sizeof(SDK::UExponentialHeightFogComponent)))
                        continue;

                    bool known = false;
                    FogOriginal* freeSlot = nullptr;
                    for (auto& original : fogOriginals_)
                    {
                        if (original.component == component &&
                            original.componentIndex == component->Index)
                        {
                            known = true;
                            break;
                        }
                        if (original.component == nullptr && freeSlot == nullptr)
                            freeSlot = &original;
                    }
                    if (!known && freeSlot != nullptr)
                    {
                        freeSlot->component = component;
                        freeSlot->componentIndex = component->Index;
                        freeSlot->density = component->FogDensity;
                        freeSlot->volumetric = component->bEnableVolumetricFog;
                        SetFogDensity(component, 0.0f);
                        SetVolumetricFog(component, false);
                    }
                    continue;
                }

                if (actorKind == 2)
                {
                    auto* skyActor = reinterpret_cast<SDK::ASkyLight*>(actor);
                    auto* component = skyActor->LightComponent;
                    if (!IsLiveUObject(component) ||
                        !IsReadable(component, sizeof(SDK::USkyLightComponent)))
                        continue;

                    captureSkyLight(component);
                    continue;
                }

                if (actorKind == 3)
                {
                    auto* lightActor = reinterpret_cast<SDK::ALight*>(actor);
                    auto* component = lightActor->LightComponent;
                    if (!IsLiveUObject(component) ||
                        !IsReadable(component, sizeof(SDK::ULightComponent)))
                        continue;

                    captureWorldLight(component);
                }

            }

            if (worldScanActorIndex_ >= actorCount)
            {
                ++worldScanLevelIndex_;
                worldScanActorIndex_ = 0;
            }
        }

        if (worldScanLevelIndex_ < levelCount)
        {
            // 扫描未完成：保存进度，本次 tick 到此为止（不阻塞游戏线程）
            suppressedFogCount_.store(liveFogCount, std::memory_order_release);
            boostedSkyLightCount_.store(liveSkyCount, std::memory_order_release);
            boostedDirectionalLightCount_.store(liveDirectionalCount,
                                                std::memory_order_release);
            return;
        }

        // 扫描完成：复位游标并重新统计（本轮新捕获的组件未计入上方快速重施加的计数）
        worldScanInProgress_ = false;
        worldScanLevelIndex_ = 0;
        worldScanActorIndex_ = 0;
        nextWorldLightScanMs_ = now + 5000;

        liveFogCount = 0;
        for (const auto& original : fogOriginals_)
        {
            if (IsLiveUObject(original.component) &&
                original.component->Index == original.componentIndex)
                ++liveFogCount;
        }
        liveSkyCount = 0;
        for (const auto& original : skyLightOriginals_)
        {
            if (IsLiveUObject(original.component) &&
                original.component->Index == original.componentIndex)
                ++liveSkyCount;
        }
        liveDirectionalCount = 0;
        for (const auto& original : directionalLightOriginals_)
        {
            if (IsLiveUObject(original.component) &&
                original.component->Index == original.componentIndex)
                ++liveDirectionalCount;
        }

        bool flashlightFallbackReady = IsLiveUObject(persistentLight_) &&
                                       persistentLight_->Index == persistentLightIndex_;
        for (const auto& original : flashlightOriginals_)
        {
            if (IsLiveUObject(original.component) &&
                original.component->Index == original.componentIndex)
            {
                flashlightFallbackReady = true;
                break;
            }
        }
        if (settings.nightVisionRemoveFog && liveSkyCount == 0 &&
            liveDirectionalCount == 0 && !flashlightFallbackReady)
        {
            RestoreNightVisionFog();
            liveFogCount = 0;
        }
        suppressedFogCount_.store(liveFogCount, std::memory_order_release);
        boostedSkyLightCount_.store(liveSkyCount, std::memory_order_release);
        boostedDirectionalLightCount_.store(liveDirectionalCount,
                                            std::memory_order_release);
    }

    void RestoreNightVisionFog()
    {
        for (auto& original : fogOriginals_)
        {
            if (original.component == nullptr)
                continue;
            if (IsLiveUObject(original.component) &&
                original.component->Index == original.componentIndex)
            {
                SetFogDensity(original.component, original.density);
                SetVolumetricFog(original.component, original.volumetric);
            }
            original = {};
        }
        nextWorldLightScanMs_ = 0;
        worldScanInProgress_ = false;
        worldScanLevelIndex_ = 0;
        worldScanActorIndex_ = 0;
        suppressedFogCount_.store(0, std::memory_order_release);
    }

    void ApplyPersistentLight(const VisualSettings& settings)
    {
        if (!settings.nightVisionPersistentLight || character_ == nullptr ||
            !IsLiveUObject(character_))
        {
            RestorePersistentLight();
            return;
        }

        if (!IsLiveUObject(persistentLight_) ||
            persistentLight_->Index != persistentLightIndex_ ||
            persistentLightOwner_ != character_ ||
            persistentLightOwnerIndex_ != character_->Index)
        {
            RestorePersistentLight();
            persistentLight_ = AddPointLightComponent(character_);
            if (!IsLiveUObject(persistentLight_))
                return;
            persistentLightIndex_ = persistentLight_->Index;
            persistentLightOwner_ = character_;
            persistentLightOwnerIndex_ = character_->Index;
            SetSceneMobility(persistentLight_, SDK::EComponentMobility::Movable);
        }

        const float radius = std::clamp(settings.nightVisionLightRangeMeters,
                                        20.0f, 800.0f) * 100.0f;
        const float intensity = std::clamp(settings.nightVisionLightIntensity,
                                           1.0f, 20.0f);
        persistentLight_->bUseInverseSquaredFalloff = 0;
        persistentLight_->bAffectsWorld = 1;
        SetPointLightFalloff(persistentLight_, 1.0f);
        SetLocalLightIntensity(persistentLight_, intensity);
        SetLocalLightRadius(persistentLight_, radius);
        SetLightCastShadows(persistentLight_, false);
        SetComponentActive(persistentLight_, true);
        SetComponentHidden(persistentLight_, false);
        SetVisibility(persistentLight_, true);
    }

    void RestorePersistentLight()
    {
        if (IsLiveUObject(persistentLight_) &&
            persistentLight_->Index == persistentLightIndex_)
        {
            SetLocalLightIntensity(persistentLight_, 0.0f);
            SetComponentActive(persistentLight_, false);
            SetComponentHidden(persistentLight_, true);
            SetVisibility(persistentLight_, false);
            if (IsLiveUObject(persistentLightOwner_) &&
                persistentLightOwner_->Index == persistentLightOwnerIndex_)
                DestroyActorComponent(persistentLightOwner_, persistentLight_);
        }
        persistentLight_ = nullptr;
        persistentLightIndex_ = -1;
        persistentLightOwner_ = nullptr;
        persistentLightOwnerIndex_ = -1;
    }

    void ApplyNightVisionFlashlight(const VisualSettings& settings)
    {
        ApplyPersistentLight(settings);
        if (character_ == nullptr || !IsLiveUObject(character_))
        {
            RestoreNightVisionFlashlight();
            return;
        }

        if (!settings.nightVisionExtendFlashlight)
        {
            RestoreNightVisionFlashlight();
            return;
        }

        SDK::ABP_Item_C* currentItem = character_->CurrentItem_Rep;
        if (!IsLiveUObject(currentItem) ||
            !IsPawnClass(currentItem, "BP_Item_Flashlight_C"))
        {
            RestoreNightVisionFlashlight();
            return;
        }

        auto* flashlight = reinterpret_cast<SDK::ABP_Item_Flashlight_C*>(currentItem);
        if (!IsReadable(flashlight, sizeof(SDK::ABP_Item_Flashlight_C)))
            return;
        if (flashlightItem_ != flashlight || flashlightItemIndex_ != flashlight->Index)
        {
            RestoreNightVisionFlashlight();
            flashlightItem_ = flashlight;
            flashlightItemIndex_ = flashlight->Index;

            const auto capture = [this](SDK::UPointLightComponent* component,
                                        const bool spot, const std::size_t slot)
            {
                if (!IsLiveUObject(component) || slot >= flashlightOriginals_.size())
                    return;
                auto& original = flashlightOriginals_[slot];
                original.component = component;
                original.componentIndex = component->Index;
                original.intensity = component->Intensity;
                original.radius = component->AttenuationRadius;
                original.spot = spot;
                original.inverseSquared = component->bUseInverseSquaredFalloff != 0;
                original.falloffExponent = component->LightFalloffExponent;
                original.visible = component->bVisible != 0;
                original.hidden = component->bHiddenInGame != 0;
                original.active = component->bIsActive != 0;
                original.affectsWorld = component->bAffectsWorld != 0;
                original.castShadows = component->CastShadows != 0;
                if (spot)
                    original.outerCone =
                        reinterpret_cast<SDK::USpotLightComponent*>(component)->OuterConeAngle;
            };
            capture(flashlight->Flashlight, true, 0);
            capture(flashlight->FlashlightFakeGI, true, 1);
        }

        const float strength = std::clamp(settings.nightVisionStrength, 0.25f, 3.0f);
        const float radius = std::clamp(settings.nightVisionLightRangeMeters,
                                        20.0f, 300.0f) * 100.0f;
        for (std::size_t index = 0; index < flashlightOriginals_.size(); ++index)
        {
            auto& original = flashlightOriginals_[index];
            if (!IsLiveUObject(original.component) ||
                original.component->Index != original.componentIndex)
                continue;
            if (settings.nightVisionExtendFlashlight && original.spot)
            {
                SetLocalLightIntensity(original.component,
                                       std::max(original.intensity,
                                                original.intensity * (1.5f + strength)));
                SetLocalLightRadius(original.component, std::max(original.radius, radius));
                SetSpotLightOuterCone(
                    reinterpret_cast<SDK::USpotLightComponent*>(original.component),
                    index == 0 ? 82.0f : 88.0f);
            }
        }
    }

    void RestoreNightVisionFlashlight()
    {
        for (auto& original : flashlightOriginals_)
        {
            if (IsLiveUObject(original.component) &&
                original.component->Index == original.componentIndex)
            {
                SetLocalLightIntensity(original.component, original.intensity);
                SetLocalLightRadius(original.component, original.radius);
                original.component->bUseInverseSquaredFalloff =
                    original.inverseSquared ? 1 : 0;
                original.component->bAffectsWorld = original.affectsWorld ? 1 : 0;
                SetPointLightFalloff(original.component, original.falloffExponent);
                SetLightCastShadows(original.component, original.castShadows);
                SetComponentActive(original.component, original.active);
                SetComponentHidden(original.component, original.hidden);
                SetVisibility(original.component, original.visible);
                if (original.spot)
                {
                    SetSpotLightOuterCone(
                        reinterpret_cast<SDK::USpotLightComponent*>(original.component),
                        original.outerCone);
                }
            }
            original = {};
        }
        flashlightItem_ = nullptr;
        flashlightItemIndex_ = -1;
    }

    void RestoreNightVisionWorld()
    {
        RestorePersistentLight();
        RestoreNightVisionFlashlight();
        RestoreNightVisionFog();
        for (auto& original : skyLightOriginals_)
        {
            if (original.component == nullptr)
                continue;
            if (IsLiveUObject(original.component) &&
                original.component->Index == original.componentIndex)
            {
                SetSkyLightIntensity(original.component, original.intensity);
                SetSkyLightIndirectIntensity(original.component, original.indirectIntensity);
                SetSceneMobility(original.component, original.mobility);
            }
            original = {};
        }
        for (auto& original : directionalLightOriginals_)
        {
            if (original.component == nullptr)
                continue;
            if (IsLiveUObject(original.component) &&
                original.component->Index == original.componentIndex)
            {
                SetLocalLightIntensity(original.component, original.intensity);
                SetLocalLightIndirectIntensity(original.component,
                                               original.indirectIntensity);
                SetSceneMobility(original.component, original.mobility);
            }
            original = {};
        }
        nextWorldLightScanMs_ = 0;
        worldScanInProgress_ = false;
        worldScanLevelIndex_ = 0;
        worldScanActorIndex_ = 0;
        boostedSkyLightCount_.store(0, std::memory_order_release);
        boostedDirectionalLightCount_.store(0, std::memory_order_release);
    }
    void ApplyThirdPerson(const VisualSettings& settings, bool targetReady)
    {
        if (!settings.thirdPersonEnabled || !targetReady)
        {
            RestoreThirdPerson();
            return;
        }

        if (!thirdPersonApplied_)
        {
            if (springArm_ != nullptr)
            {
                thirdPersonOriginal_.armLength = springArm_->TargetArmLength;
                thirdPersonOriginal_.collisionTest = springArm_->bDoCollisionTest != 0;
                thirdPersonOriginal_.usePawnControlRotation =
                    springArm_->bUsePawnControlRotation != 0;
                thirdPersonOriginal_.inheritPitch = springArm_->bInheritPitch != 0;
                thirdPersonOriginal_.inheritYaw = springArm_->bInheritYaw != 0;
                thirdPersonOriginal_.inheritRoll = springArm_->bInheritRoll != 0;
            }
            thirdPersonOriginal_.cameraRelativeLocation = camera_->RelativeLocation;
            if (IsReadable(mesh_, sizeof(SDK::USkeletalMeshComponent)))
                thirdPersonOriginal_.meshOwnerNoSee = mesh_->bOwnerNoSee != 0;
            if (IsReadable(arms_, sizeof(SDK::USkeletalMeshComponent)))
                thirdPersonOriginal_.armsVisible = arms_->bVisible != 0;
            if (IsReadable(legs_, sizeof(SDK::USkeletalMeshComponent)))
                thirdPersonOriginal_.legsVisible = legs_->bVisible != 0;
            if (IsReadable(playerAnim_, sizeof(SDK::UPlayer_AnimBP_C)))
                thirdPersonOriginal_.animationPitch = playerAnim_->Pitch;
            thirdPersonApplied_ = true;
        }

        const float distance = std::clamp(settings.thirdPersonDistance, 50.0f, 1000.0f);
        const float height = std::clamp(settings.thirdPersonHeight, -500.0f, 500.0f);
        if (springArm_ != nullptr)
        {
            springArm_->TargetArmLength = 0.0f;
            springArm_->bDoCollisionTest = 0;
            // 必须用Controller Rotation 用Actor Rotation你等着自转吧
            springArm_->bUsePawnControlRotation = 1;
            springArm_->bInheritPitch = 1;
            springArm_->bInheritYaw = 1;
            springArm_->bInheritRoll = 0;
        }

        SDK::FVector cameraLocation = thirdPersonOriginal_.cameraRelativeLocation;
        cameraLocation.X -= distance;
        cameraLocation.Z += height;
        const SDK::FVector& currentLocation = camera_->RelativeLocation;
        if (currentLocation.X != cameraLocation.X || currentLocation.Y != cameraLocation.Y ||
            currentLocation.Z != cameraLocation.Z)
        {
            SetRelativeLocation(camera_, cameraLocation);
        }

        if (IsReadable(mesh_, sizeof(SDK::USkeletalMeshComponent)) && mesh_->bOwnerNoSee != 0)
            SetOwnerNoSee(mesh_, false);
        if (IsReadable(arms_, sizeof(SDK::USkeletalMeshComponent)) && arms_->bVisible != 0)
            SetVisibility(arms_, false);
        if (IsReadable(legs_, sizeof(SDK::USkeletalMeshComponent)) && legs_->bVisible != 0)
            SetVisibility(legs_, false);
        if (IsReadable(playerAnim_, sizeof(SDK::UPlayer_AnimBP_C)) && IsLiveUObject(playerAnim_))
            playerAnim_->Pitch = 0.0f;
        // 处理第三视角模型异常
    }

    void ApplyDerp(const VisualSettings& settings, bool modelReady, const float deltaSeconds)
    {
        if (!settings.derpEnabled || !modelReady)
        {
            RestoreDerp(true);
            return;
        }

        if (derpApplied_ && derpOriginal_.networkVisible != settings.derpNetworkVisible)
            RestoreDerp(true);

        if (!derpApplied_)
        {
            derpOriginal_.meshRelativeRotation = mesh_->RelativeRotation;
            derpOriginal_.networkVisible = settings.derpNetworkVisible;
            if (IsReadable(springArm_, sizeof(SDK::USpringArmComponent)) &&
                IsLiveUObject(springArm_))
            {
                derpOriginal_.springSettingsValid = true;
                derpOriginal_.springUsePawnControlRotation =
                    springArm_->bUsePawnControlRotation != 0;
                derpOriginal_.springInheritPitch = springArm_->bInheritPitch != 0;
                derpOriginal_.springInheritYaw = springArm_->bInheritYaw != 0;
                derpOriginal_.springInheritRoll = springArm_->bInheritRoll != 0;
            }
            derpApplied_ = true;
        }

        derpOriginal_.pitchOffset = std::remainder(
            derpOriginal_.pitchOffset + settings.derpPitchSpeed * deltaSeconds, 360.0f);
        derpOriginal_.yawOffset = std::remainder(
            derpOriginal_.yawOffset + settings.derpYawSpeed * deltaSeconds, 360.0f);
        derpOriginal_.rollOffset = std::remainder(
            derpOriginal_.rollOffset + settings.derpRollSpeed * deltaSeconds, 360.0f);

        SDK::FRotator rotation = derpOriginal_.meshRelativeRotation;
        rotation.Pitch += derpOriginal_.pitchOffset;
        rotation.Yaw += derpOriginal_.yawOffset;
        rotation.Roll += derpOriginal_.rollOffset;
        SetRelativeRotation(mesh_, rotation);

        if (settings.derpNetworkVisible)
        {
            const bool hasAuthority = character_->Role == SDK::ENetRole::ROLE_Authority;
            if (hasAuthority && derpOriginal_.springSettingsValid && IsLiveUObject(springArm_))
            {
                springArm_->bUsePawnControlRotation = 1;
                springArm_->bInheritPitch = 1;
                springArm_->bInheritYaw = 1;
                springArm_->bInheritRoll = 0;
            }

            if (hasAuthority && mesh_->bReplicates == 0)
                SetComponentReplicated(mesh_, true);
            // 非房主不再借用 StartPushingActor_SERVER 发送旋转。若服务器没有安装本 DLL，
            // 原游戏会把该包当作推物体请求处理，其中的标记坐标会破坏玩家位置，表现为
            // Z 持续下降、掉出地图以及周围完全变黑。客户端现在始终退回本地模型旋转。
        }
    }

    void RestoreNightVision()
    {
        if (!nightVisionApplied_)
            return;

        if (IsReadable(camera_, sizeof(SDK::UCameraComponent)))
        {
            auto& pp = camera_->PostProcessSettings;
            camera_->PostProcessBlendWeight = nightVisionOriginal_.blendWeight;
            pp.AutoExposureBias = nightVisionOriginal_.exposureBias;
            pp.bOverride_AutoExposureBias =
                nightVisionOriginal_.overrideExposureBias ? 1 : 0;
            pp.ColorGamma = nightVisionOriginal_.colorGamma;
            pp.ColorGain = nightVisionOriginal_.colorGain;
            pp.ColorOffset = nightVisionOriginal_.colorOffset;
            pp.ColorGammaShadows = nightVisionOriginal_.shadowGamma;
            pp.ColorGainShadows = nightVisionOriginal_.shadowGain;
            pp.ColorOffsetShadows = nightVisionOriginal_.shadowOffset;
            pp.ColorCorrectionShadowsMax = nightVisionOriginal_.shadowMaximum;
            pp.IndirectLightingColor = nightVisionOriginal_.indirectLightingColor;
            pp.IndirectLightingIntensity = nightVisionOriginal_.indirectLightingIntensity;
            pp.AmbientOcclusionIntensity = nightVisionOriginal_.ambientOcclusionIntensity;
            pp.bOverride_ColorGamma = nightVisionOriginal_.overrideColorGamma ? 1 : 0;
            pp.bOverride_ColorGain = nightVisionOriginal_.overrideColorGain ? 1 : 0;
            pp.bOverride_ColorOffset = nightVisionOriginal_.overrideColorOffset ? 1 : 0;
            pp.bOverride_ColorGammaShadows = nightVisionOriginal_.overrideShadowGamma ? 1 : 0;
            pp.bOverride_ColorGainShadows = nightVisionOriginal_.overrideShadowGain ? 1 : 0;
            pp.bOverride_ColorOffsetShadows = nightVisionOriginal_.overrideShadowOffset ? 1 : 0;
            pp.bOverride_ColorCorrectionShadowsMax =
                nightVisionOriginal_.overrideShadowMaximum ? 1 : 0;
            pp.bOverride_IndirectLightingColor =
                nightVisionOriginal_.overrideIndirectLightingColor ? 1 : 0;
            pp.bOverride_IndirectLightingIntensity =
                nightVisionOriginal_.overrideIndirectLightingIntensity ? 1 : 0;
            pp.bOverride_AmbientOcclusionIntensity =
                nightVisionOriginal_.overrideAmbientOcclusionIntensity ? 1 : 0;
        }
        nightVisionApplied_ = false;
        nightVisionOriginal_ = {};
    }

    void RestoreThirdPerson()
    {
        if (!thirdPersonApplied_)
            return;

        if (springArm_ != nullptr && IsReadable(springArm_, sizeof(SDK::USpringArmComponent)))
        {
            springArm_->TargetArmLength = thirdPersonOriginal_.armLength;
            springArm_->bDoCollisionTest = thirdPersonOriginal_.collisionTest ? 1 : 0;
            springArm_->bUsePawnControlRotation =
                thirdPersonOriginal_.usePawnControlRotation ? 1 : 0;
            springArm_->bInheritPitch = thirdPersonOriginal_.inheritPitch ? 1 : 0;
            springArm_->bInheritYaw = thirdPersonOriginal_.inheritYaw ? 1 : 0;
            springArm_->bInheritRoll = thirdPersonOriginal_.inheritRoll ? 1 : 0;
        }
        if (IsReadable(camera_, sizeof(SDK::UCameraComponent)))
            SetRelativeLocation(camera_, thirdPersonOriginal_.cameraRelativeLocation);
        if (IsReadable(mesh_, sizeof(SDK::USkeletalMeshComponent)))
            SetOwnerNoSee(mesh_, thirdPersonOriginal_.meshOwnerNoSee);
        if (IsReadable(arms_, sizeof(SDK::USkeletalMeshComponent)))
            SetVisibility(arms_, thirdPersonOriginal_.armsVisible);
        if (IsReadable(legs_, sizeof(SDK::USkeletalMeshComponent)))
            SetVisibility(legs_, thirdPersonOriginal_.legsVisible);
        if (IsReadable(playerAnim_, sizeof(SDK::UPlayer_AnimBP_C)) && IsLiveUObject(playerAnim_))
            playerAnim_->Pitch = thirdPersonOriginal_.animationPitch;

        thirdPersonApplied_ = false;
        thirdPersonOriginal_ = {};
    }

    void RestoreDerp(const bool notifyServer)
    {
        if (!derpApplied_)
            return;

        if (IsReadable(mesh_, sizeof(SDK::USkeletalMeshComponent)) && IsLiveUObject(mesh_))
            SetRelativeRotation(mesh_, derpOriginal_.meshRelativeRotation);
        // 房主的组件复制会同步恢复值；客户端禁止再发送伪装成推物体请求的恢复包。
        (void)notifyServer;
        if (derpOriginal_.springSettingsValid && IsLiveUObject(springArm_))
        {
            springArm_->bUsePawnControlRotation =
                derpOriginal_.springUsePawnControlRotation ? 1 : 0;
            springArm_->bInheritPitch = derpOriginal_.springInheritPitch ? 1 : 0;
            springArm_->bInheritYaw = derpOriginal_.springInheritYaw ? 1 : 0;
            springArm_->bInheritRoll = derpOriginal_.springInheritRoll ? 1 : 0;
        }
        derpApplied_ = false;
        derpOriginal_ = {};
    }

    void Detach(bool restore)
    {
        if (restore && IsLiveUObject(pawn_))
        {
            RestoreDerp(false);
            RestoreThirdPerson();
            RestoreNightVision();
            RestoreNightVisionWorld();
        }
        else
        {
            thirdPersonApplied_ = false;
            thirdPersonOriginal_ = {};
            derpApplied_ = false;
            derpOriginal_ = {};
            nightVisionApplied_ = false;
            nightVisionOriginal_ = {};
            for (auto& original : fogOriginals_)
                original = {};
            for (auto& original : skyLightOriginals_)
                original = {};
            for (auto& original : directionalLightOriginals_)
                original = {};
            for (auto& original : flashlightOriginals_)
                original = {};
            flashlightItem_ = nullptr;
            flashlightItemIndex_ = -1;
            persistentLight_ = nullptr;
            persistentLightIndex_ = -1;
            persistentLightOwner_ = nullptr;
            persistentLightOwnerIndex_ = -1;
            worldActorClassCache_.clear();
            nextWorldLightScanMs_ = 0;
            worldScanInProgress_ = false;
            worldScanLevelIndex_ = 0;
            worldScanActorIndex_ = 0;
            suppressedFogCount_.store(0, std::memory_order_release);
            boostedSkyLightCount_.store(0, std::memory_order_release);
            boostedDirectionalLightCount_.store(0, std::memory_order_release);
        }
        pawn_ = nullptr;
        pawnIndex_ = -1;
        character_ = nullptr;
        boat_ = nullptr;
        camera_ = nullptr;
        springArm_ = nullptr;
        mesh_ = nullptr;
        arms_ = nullptr;
        legs_ = nullptr;
        playerAnim_ = nullptr;
        observedCurrentItem_ = nullptr;
        observedCurrentItemIndex_ = -1;
        observedCurrentItemIsFlashlight_ = false;
        observedFlashlightOn_ = false;
    }

    SDK::APawn* pawn_ = nullptr;
    std::int32_t pawnIndex_ = -1;
    SDK::ABPCharacter_Demo_C* character_ = nullptr;
    SDK::ABoatPawn* boat_ = nullptr;
    SDK::UCameraComponent* camera_ = nullptr;
    SDK::UFancySpringArmComponent* springArm_ = nullptr;
    SDK::USkeletalMeshComponent* mesh_ = nullptr;
    SDK::USkeletalMeshComponent* arms_ = nullptr;
    SDK::USkeletalMeshComponent* legs_ = nullptr;
    SDK::UPlayer_AnimBP_C* playerAnim_ = nullptr;
    NightVisionOriginal nightVisionOriginal_{};
    ThirdPersonOriginal thirdPersonOriginal_{};
    DerpOriginal derpOriginal_{};
    bool nightVisionApplied_ = false;
    std::int32_t appliedCameraIndex_ = -1;
    ULONGLONG nextNightVisionApplyMs_ = 0;
    bool thirdPersonApplied_ = false;
    bool derpApplied_ = false;
    std::array<RemoteDerpState, 16> remoteDerp_{};
    std::array<FogOriginal, 64> fogOriginals_{};
    std::array<SkyLightOriginal, 16> skyLightOriginals_{};
    std::array<DirectionalLightOriginal, 16> directionalLightOriginals_{};
    std::array<FlashlightComponentOriginal, 2> flashlightOriginals_{};
    SDK::ABP_Item_Flashlight_C* flashlightItem_ = nullptr;
    std::int32_t flashlightItemIndex_ = -1;
    SDK::UPointLightComponent* persistentLight_ = nullptr;
    std::int32_t persistentLightIndex_ = -1;
    SDK::AActor* persistentLightOwner_ = nullptr;
    std::int32_t persistentLightOwnerIndex_ = -1;
    SDK::UObject* observedCurrentItem_ = nullptr;
    std::int32_t observedCurrentItemIndex_ = -1;
    bool observedCurrentItemIsFlashlight_ = false;
    bool observedFlashlightOn_ = false;
    std::unordered_map<SDK::UClass*, std::uint8_t> worldActorClassCache_{};
    ULONGLONG nextWorldLightScanMs_ = 0;
    bool worldScanInProgress_ = false;
    int worldScanLevelIndex_ = 0;
    int worldScanActorIndex_ = 0;
    ULONGLONG nextNightVisionFogMs_ = 0;
    std::atomic_uint suppressedFogCount_{0};
    std::atomic_uint boostedSkyLightCount_{0};
    std::atomic_uint boostedDirectionalLightCount_{0};
};

VisualRuntime g_runtime;
}

Visuals& Visuals::Instance()
{
    static Visuals instance;
    return instance;
}

void Visuals::Update(const bool inputBlocked)
{
    const int key = settings_.thirdPersonToggleKey;
    const bool keyDown = key > 0 && key < 256 && (GetAsyncKeyState(key) & 0x8000) != 0;
    if (!inputBlocked && keyDown && !thirdPersonKeyDown_)
        settings_.thirdPersonEnabled = !settings_.thirdPersonEnabled;
    thirdPersonKeyDown_ = keyDown;

    if (derpConfiguredLastFrame_ && !settings_.derpEnabled)
        derpRestorePending_.store(true, std::memory_order_release);
    derpConfiguredLastFrame_ = settings_.derpEnabled;
    derpConfigured_.store(settings_.derpEnabled, std::memory_order_release);
    nightVisionActive_.store(settings_.nightVisionEnabled, std::memory_order_release);

    const bool nightVisionChanged =
        nightVisionConfiguredLastFrame_ != settings_.nightVisionEnabled ||
        std::fabs(nightVisionStrengthLastFrame_ - settings_.nightVisionStrength) > 0.001f ||
        nightVisionRemoveFogLastFrame_ != settings_.nightVisionRemoveFog ||
        nightVisionPersistentLightLastFrame_ != settings_.nightVisionPersistentLight ||
        nightVisionExtendFlashlightLastFrame_ != settings_.nightVisionExtendFlashlight ||
        std::fabs(nightVisionLightRangeLastFrame_ -
                  settings_.nightVisionLightRangeMeters) > 0.01f ||
        std::fabs(nightVisionLightIntensityLastFrame_ -
                  settings_.nightVisionLightIntensity) > 0.01f;
    if (nightVisionConfiguredLastFrame_ && !settings_.nightVisionEnabled)
        nightVisionRestorePending_.store(true, std::memory_order_release);
    if (nightVisionChanged)
        nightVisionConfigured_.store(true, std::memory_order_release);
    nightVisionConfiguredLastFrame_ = settings_.nightVisionEnabled;
    nightVisionStrengthLastFrame_ = settings_.nightVisionStrength;
    nightVisionRemoveFogLastFrame_ = settings_.nightVisionRemoveFog;
    nightVisionPersistentLightLastFrame_ = settings_.nightVisionPersistentLight;
    nightVisionExtendFlashlightLastFrame_ = settings_.nightVisionExtendFlashlight;
    nightVisionLightRangeLastFrame_ = settings_.nightVisionLightRangeMeters;
    nightVisionLightIntensityLastFrame_ = settings_.nightVisionLightIntensity;

    const bool targetChanged = g_runtime.Update(settings_, status_);
    if (targetChanged && settings_.nightVisionEnabled)
        nightVisionConfigured_.store(true, std::memory_order_release);
}

void Visuals::OnGameThreadTick(const SDK::UObject* tickObject, const float deltaSeconds)
{
    const bool updateNightVision =
        nightVisionConfigured_.load(std::memory_order_acquire) ||
        nightVisionRestorePending_.load(std::memory_order_acquire);
    if (g_runtime.GameThreadTick(settings_, tickObject, deltaSeconds, updateNightVision,
                                 nightVisionActive_.load(std::memory_order_acquire)))
    {
        if (!settings_.derpEnabled)
            derpRestorePending_.store(false, std::memory_order_release);
        nightVisionConfigured_.store(false, std::memory_order_release);
        if (!settings_.nightVisionEnabled)
            nightVisionRestorePending_.store(false, std::memory_order_release);
    }
}

bool Visuals::OnBeforeNetworkDerpEvent(const SDK::UObject* object, SDK::UFunction* function,
                                       void* parameters)
{
    return g_runtime.HandleNetworkPacket(object, function, parameters);
}

bool Visuals::NeedsGameThreadTick() const noexcept
{
    return derpConfigured_.load(std::memory_order_acquire) ||
           derpRestorePending_.load(std::memory_order_acquire) ||
           nightVisionConfigured_.load(std::memory_order_acquire) ||
           nightVisionRestorePending_.load(std::memory_order_acquire) ||
           nightVisionActive_.load(std::memory_order_acquire);
}

void Visuals::Reset()
{
    g_runtime.Reset();
    status_ = {};
    thirdPersonKeyDown_ = false;
    derpConfiguredLastFrame_ = false;
    derpConfigured_.store(false, std::memory_order_release);
    derpRestorePending_.store(false, std::memory_order_release);
    nightVisionConfiguredLastFrame_ = false;
    nightVisionStrengthLastFrame_ = 1.0f;
    nightVisionRemoveFogLastFrame_ = false;
    nightVisionPersistentLightLastFrame_ = true;
    nightVisionExtendFlashlightLastFrame_ = false;
    nightVisionLightRangeLastFrame_ = 150.0f;
    nightVisionLightIntensityLastFrame_ = 1.0f;
    nightVisionConfigured_.store(false, std::memory_order_release);
    nightVisionRestorePending_.store(false, std::memory_order_release);
    nightVisionActive_.store(false, std::memory_order_release);
}
}

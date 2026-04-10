#include "Unreal/CoreUObject/UObject/Class.hpp"
#include "Unreal/UObject.hpp"
#include "Unreal/UEnum.hpp"
#include "Unreal/Engine/UDataTable.hpp"
#include "Unreal/Transform.hpp"
#include "Unreal/World.hpp"
#include "Unreal/Hooks/Hooks.hpp"
#include "Unreal/UObjectGlobals.hpp"
#include "SDK/Classes/AMonoNPCSpawner.h"
#include "SDK/Classes/APalSpawnerStandard.h"
#include "SDK/Classes/UWorldPartition.h"
#include "SDK/Classes/UWorldPartitionRuntimeLevelStreamingCell.h"
#include "SDK/Classes/ULevelStreaming.h"
#include "SDK/Classes/KismetGuidLibrary.h"
#include "SDK/Classes/KismetSystemLibrary.h"
#include "SDK/Classes/Custom/UObjectGlobals.h"
#include "SDK/Classes/Custom/DataTable/TableSerializer.h"
#include "SDK/Helper/PropertyHelper.h"
#include "Utility/Logging.h"
#include "Utility/EnumHelpers.h"
#include "Utility/JsonHelpers.h"
#include "Loader/PalSpawnLoader.h"
#include "SDK/PalSignatures.h"
#include <cctype>

using namespace RC;
using namespace RC::Unreal;

namespace fs = std::filesystem;

namespace Palworld {
    namespace
    {
        constexpr RC::Unreal::uint32 kMonoNpcResolveWarmupPasses = 20;
        constexpr RC::Unreal::uint32 kMonoNpcResolveCooldownPasses = 4;
        constexpr RC::Unreal::uint32 kMonoNpcResolvePumpTickInterval = 15;
    }

    PalSpawnLoader::PalSpawnLoader() : PalModLoaderBase("spawns") {
        SetDisplayName(TEXT("Custom Spawn Loader"));
    }

    PalSpawnLoader::~PalSpawnLoader() {
        auto expected = WorldCleanupHook.disable();
        WorldCleanupHook = {};
        if (m_onLevelShownFunction)
        {
            m_onLevelShownFunction->UnregisterHook(m_onLevelShownCallbackId);
        }
        if (m_onLevelHiddenFunction)
        {
            m_onLevelHiddenFunction->UnregisterHook(m_onLevelHiddenCallbackId);
        }
        if (m_engineTickCallbackId != RC::Unreal::Hook::ERROR_ID)
        {
            RC::Unreal::Hook::UnregisterCallback(m_engineTickCallbackId);
            m_engineTickCallbackId = RC::Unreal::Hook::ERROR_ID;
        }
    }

    void PalSpawnLoader::Reload(const std::filesystem::path::string_type& modName, const nlohmann::json& data)
    {
        UnloadMod(modName);
        LoadSpawns(modName, data);

        for (auto loadedCell : m_loadedCells)
        {
            ProcessCellSpawners(loadedCell);
        }
    }

    void PalSpawnLoader::OnLoad(const std::filesystem::path& loaderPath, const RC::StringType& modName, const EEngineLifecyclePhase& engineLifecyclePhase)
    {
        if (engineLifecyclePhase != EEngineLifecyclePhase::GameInstanceInit)
        {
            return;
        }

        PS::JsonHelpers::ParseJsonFilesInPath(loaderPath, [&](const nlohmann::json& data) {
            LoadSpawns(modName, data);
        });
    }

    void PalSpawnLoader::OnAutoReload(const std::filesystem::path::string_type& modName, const std::filesystem::path& modFilePath)
    {
        PS::JsonHelpers::ParseJsonFileInPath(modFilePath, [&](const nlohmann::json& data) {
            Reload(modName, data);
        });
    }

    bool PalSpawnLoader::CanInitialize(const EEngineLifecyclePhase& engineLifecyclePhase)
    {
        if (engineLifecyclePhase == EEngineLifecyclePhase::GameInstanceInit)
        {
            return true;
        }

        return false;
    }

    bool PalSpawnLoader::OnInitialize()
    {
        try
        {
            m_bossSpawnerLocationData = GetDatatableByName("DT_BossSpawnerLoactionData");

            auto CleanupWorld_FuncPtr = Palworld::SignatureManager::GetSignature("UWorld::CleanupWorld");
            if (!CleanupWorld_FuncPtr)
            {
                PS::Log<LogLevel::Error>(STR("Unable to hook UWorld::CleanupWorld, signature is outdated. Custom spawns will not work.\n"));
                return false;
            }

            WorldCleanupCallback = [&](RC::Unreal::UWorld* selfWorld, bool bSessionEnded, bool bCleanupResources, RC::Unreal::UWorld* newWorld) {
                this->CleanupSpawns();
            };

            WorldCleanupHook = safetyhook::create_inline(reinterpret_cast<void*>(CleanupWorld_FuncPtr),
                OnWorldCleanup);

            SetupWorldPartitionHooks();
        }
        catch (const std::exception& e)
        {
            PS::Log<LogLevel::Error>(STR("Unable to initialize {}, {}\n"), GetDisplayName(), RC::to_generic_string(e.what()));
            return false;
        }

        return true;
    }

    void PalSpawnLoader::OnCellLoaded(UECustom::UWorldPartitionRuntimeLevelStreamingCell* cell)
    {
        // This isn't perfect, but it should hopefully do the job for now until a better solution is found.
        // Seems like cells can overlap each other (?) which means the spawner code can get called multiple times.

        if (cell->GetExtent() > 51200.0)
        {
            // Skip massive cells. Hopefully it doesn't cause issues with some locations not working for spawners.
            return;
        }

        if (cell->GetIsHLOD())
        {
            // Skip HLOD. From what I noticed, if a spawner is created inside a HLOD, it more than likely will not despawn ever which is not ideal.
            return;
        }

        m_loadedCells.Add(cell);
        ProcessCellSpawners(cell);
        TryResolvePendingMonoNpcSpawners();
    }

    void PalSpawnLoader::OnCellUnloaded(UECustom::UWorldPartitionRuntimeLevelStreamingCell* cell)
    {
        DestroySpawnersInCell(cell);
        m_loadedCells.Remove(cell);
    }

    void PalSpawnLoader::SetupWorldPartitionHooks()
    {
        m_onLevelShownFunction = UECustom::UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/Engine.WorldPartitionRuntimeLevelStreamingCell:OnLevelShown"));
        if (!m_onLevelShownFunction)
        {
            PS::Log<LogLevel::Warning>(STR("Failed to hook WorldPartitionRuntimeLevelStreamingCell:OnLevelShown. Custom spawn streaming may not initialize correctly.\n"));
            return;
        }

        m_onLevelShownCallbackId = m_onLevelShownFunction->RegisterPostHook([&](UnrealScriptFunctionCallableContext& Context, void* CustomData) {
            auto cell = static_cast<UECustom::UWorldPartitionRuntimeLevelStreamingCell*>(Context.Context);
            OnCellLoaded(cell);
        });

        m_onLevelHiddenFunction = UECustom::UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/Engine.WorldPartitionRuntimeLevelStreamingCell:OnLevelHidden"));
        if (!m_onLevelHiddenFunction)
        {
            PS::Log<LogLevel::Warning>(STR("Failed to hook WorldPartitionRuntimeLevelStreamingCell:OnLevelHidden. Custom spawn cleanup may not run correctly.\n"));
            return;
        }

        m_onLevelHiddenCallbackId = m_onLevelHiddenFunction->RegisterPostHook([&](UnrealScriptFunctionCallableContext& Context, void* CustomData) {
            auto cell = static_cast<UECustom::UWorldPartitionRuntimeLevelStreamingCell*>(Context.Context);
            OnCellUnloaded(cell);
        });

        RC::Unreal::Hook::FCallbackOptions engineTickOptions{};
        engineTickOptions.OwnerModName = STR("PalSchema");
        engineTickOptions.HookName = STR("PalSpawnLoader_MonoNpcResolvePump");
        m_engineTickCallbackId = RC::Unreal::Hook::RegisterEngineTickPostCallback(
            [&](RC::Unreal::Hook::TCallbackIterationData<void>&, RC::Unreal::UEngine*, float, bool) {
                if (m_pendingMonoNpcResolves.empty())
                {
                    return;
                }

                m_engineTickCounter += 1;
                if ((m_engineTickCounter % kMonoNpcResolvePumpTickInterval) != 0)
                {
                    return;
                }

                TryResolvePendingMonoNpcSpawners();
            },
            engineTickOptions);

        if (m_engineTickCallbackId == RC::Unreal::Hook::ERROR_ID)
        {
            PS::Log<LogLevel::Warning>(STR("Failed to hook UEngine::Tick. Deferred MonoNPC resolve retries will rely on cell events only.\n"));
        }
    }

    void PalSpawnLoader::LoadSpawns(const RC::StringType& modName, const nlohmann::json& data)
    {
        if (!data.is_array())
        {
            throw std::runtime_error("Spawn JSON must start as an array rather than as an object.");
        }

        // The json file itself starts as an array, rather than as an object
        for (auto& value : data)
        {
            RegisterSpawn(modName, value);
        }
    }

    void PalSpawnLoader::RegisterSpawn(const std::filesystem::path::string_type& modName, const nlohmann::json& value)
    {
        PS::JsonHelpers::ValidateFieldExists(value, "Type");
        PS::JsonHelpers::ValidateFieldExists(value, "Location");
        PS::JsonHelpers::ValidateFieldExists(value, "Rotation");

        PS::SpawnerInfo spawnerInfo{};

        auto Guid = UECustom::UKismetGuidLibrary::NewGuid();
        spawnerInfo.Guid = Guid;
        spawnerInfo.ModName = modName;

        std::string type;
        PS::JsonHelpers::ParseString(value, "Type", type);

        if (type == "MonoNPC")
        {
            spawnerInfo.Type = PS::SpawnerType::MonoNPC;
        }
        else if (type == "Sheet")
        {
            spawnerInfo.Type = PS::SpawnerType::Sheet;
        }
        else
        {
            throw std::runtime_error("Type must be 'MonoNPC' or 'Sheet'.");
        }

        // Location is required for all spawner types
        PS::JsonHelpers::ParseVector(value, "Location", spawnerInfo.Location);

        // Rotation is required for all spawner types
        PS::JsonHelpers::ParseRotator(value, "Rotation", spawnerInfo.Rotation);

        if (spawnerInfo.Type == PS::SpawnerType::Sheet)
        {
            RegisterSheet(modName, spawnerInfo, value);
        }
        else if (spawnerInfo.Type == PS::SpawnerType::MonoNPC)
        {
            RegisterMonoNPC(modName, spawnerInfo, value);
        }

        m_spawns.push_back(spawnerInfo);

        Output::send<LogLevel::Normal>(STR("Added new spawn: {}\n"), spawnerInfo.ToString());
    }

    void PalSpawnLoader::RegisterSheet(const std::filesystem::path::string_type& modName, PS::SpawnerInfo& spawnerInfo, const nlohmann::json& value)
    {
        PS::JsonHelpers::ValidateFieldExists(value, "SpawnGroupList");
        auto& spawnGroupList = value.at("SpawnGroupList");

        if (!spawnGroupList.is_array())
        {
            throw std::runtime_error("SpawnGroupList must be an array of objects.");
        }

        for (auto& spawnGroupListItem : spawnGroupList)
        {
            spawnerInfo.AddSpawnGroupList(spawnGroupListItem);
        }

        if (PS::JsonHelpers::FieldExists(value, "SpawnerName"))
        {
            std::string spawnerName;
            PS::JsonHelpers::ParseString(value, "SpawnerName", spawnerName);

            auto spawnerNameWide = RC::to_generic_string(spawnerName);
            spawnerNameWide = std::format(TEXT("{}_{}"), spawnerInfo.ModName, spawnerNameWide);

            spawnerInfo.SpawnerName = FName(spawnerNameWide, FNAME_Add);
        }

        if (PS::JsonHelpers::FieldExists(value, "SpawnerType"))
        {
            static auto ENUM_EPalSpawnedCharacterType = UECustom::UObjectGlobals::StaticFindObject<UEnum*>(nullptr, nullptr,
                STR("/Script/Pal.EPalSpawnedCharacterType"));

            std::string spawnerType;
            PS::JsonHelpers::ParseString(value, "SpawnerType", spawnerType);
            spawnerInfo.SpawnerType = PS::EnumHelpers::GetEnumValueByName(ENUM_EPalSpawnedCharacterType, spawnerType);

            if (spawnerType.ends_with("FieldBoss"))
            {
                AddBossSpawnLocationToMap(spawnerInfo);
            }
        }
    }

    void PalSpawnLoader::RegisterMonoNPC(const std::filesystem::path::string_type& modName, PS::SpawnerInfo& spawnerInfo, const nlohmann::json& value)
    {
        PS::JsonHelpers::ValidateFieldExists(value, "NPCID");
        PS::JsonHelpers::ValidateFieldExists(value, "Level");

        PS::JsonHelpers::ParseFName(value, "NPCID", spawnerInfo.NPCID);
        PS::JsonHelpers::ParseInteger(value, "Level", spawnerInfo.Level);

        if (spawnerInfo.Type == PS::SpawnerType::MonoNPC && spawnerInfo.NPCID == NAME_None)
        {
            throw std::runtime_error("NPCID can not be 'None' when type is set to 'MonoNPC'");
        }

        // This will be the Pal that is summoned by the NPC when it is attacked. Optional field
        if (PS::JsonHelpers::FieldExists(value, "OtomoId"))
        {
            PS::JsonHelpers::ParseFName(value, "OtomoId", spawnerInfo.OtomoName);
        }

        if (PS::JsonHelpers::FieldExists(value, "AddComponents"))
        {
            auto& addComponents = value.at("AddComponents");
            if (!addComponents.is_array())
            {
                throw std::runtime_error("AddComponents must be an array.");
            }

            for (auto& item : addComponents)
            {
                std::string componentName;
                nlohmann::json componentProperties = nlohmann::json::object();

                if (item.is_string())
                {
                    componentName = item.get<std::string>();
                }
                else if (item.is_object())
                {
                    if (!item.contains("ComponentPath") || !item.at("ComponentPath").is_string())
                    {
                        throw std::runtime_error("AddComponents object entries must include a string field named ComponentPath.");
                    }

                    componentName = item.at("ComponentPath").get<std::string>();

                    if (item.contains("Properties"))
                    {
                        if (!item.at("Properties").is_object())
                        {
                            throw std::runtime_error("AddComponents.Properties must be an object.");
                        }

                        componentProperties = item.at("Properties");
                    }
                }
                else
                {
                    throw std::runtime_error("AddComponents entries must be either strings or objects.");
                }

                // Be tolerant of accidental whitespace in JSON values.
                const auto firstNonSpace = componentName.find_first_not_of(" \t\r\n");
                if (firstNonSpace == std::string::npos)
                {
                    continue;
                }

                const auto lastNonSpace = componentName.find_last_not_of(" \t\r\n");
                componentName = componentName.substr(firstNonSpace, lastNonSpace - firstNonSpace + 1);

                if (componentName.empty())
                {
                    continue;
                }

                if (!componentName.starts_with('/'))
                {
                    throw std::runtime_error("AddComponents entries must be full asset paths, e.g. /Game/.../BP_ComponentName or /Game/.../BP_ComponentName.BP_ComponentName_C");
                }

                spawnerInfo.AddComponents.push_back({ RC::to_generic_string(componentName), std::move(componentProperties) });
            }
        }
    }

    void PalSpawnLoader::ProcessCellSpawners(UECustom::UWorldPartitionRuntimeLevelStreamingCell* cell)
    {
        auto& Box = cell->GetContentBounds();
        for (auto& spawn : m_spawns)
        {
            if (spawn.bExistsInWorld) continue;

            if (Box.IsInside(spawn.Location))
            {
                CreateSpawner(cell, spawn);
            }
        }
    }

    void PalSpawnLoader::CreateSpawner(UECustom::UWorldPartitionRuntimeLevelStreamingCell* cell, PS::SpawnerInfo& spawnerInfo)
    {
        switch (spawnerInfo.Type)
        {
        case PS::SpawnerType::MonoNPC:
            SpawnMonoNPC(cell, spawnerInfo);
            break;
        case PS::SpawnerType::Sheet:
            SpawnSheet(cell, spawnerInfo);
            break;
        }
    }

    void PalSpawnLoader::TryResolvePendingMonoNpcSpawners()
    {
        if (m_pendingMonoNpcResolves.empty())
        {
            return;
        }

        auto iterator = m_pendingMonoNpcResolves.begin();
        while (iterator != m_pendingMonoNpcResolves.end())
        {
            auto* monoSpawner = iterator->Spawner;
            if (!monoSpawner || !RC::Unreal::UObject::IsReal(monoSpawner))
            {
                iterator = m_pendingMonoNpcResolves.erase(iterator);
                continue;
            }

            if (iterator->WarmupPassesRemaining > 0)
            {
                iterator->WarmupPassesRemaining -= 1;
                ++iterator;
                continue;
            }

            if (iterator->CooldownPassesRemaining > 0)
            {
                iterator->CooldownPassesRemaining -= 1;
                ++iterator;
                continue;
            }

            if (auto* spawnedNpc = monoSpawner->TryGetSpawnedCharacter())
            {
                if (auto* spawnerInfo = FindSpawnerInfoBySpawnerActor(monoSpawner))
                {
                    AttachConfiguredComponents(spawnedNpc, *spawnerInfo);
                }

                PS::Log<LogLevel::Normal>(STR("Resolved spawned NPC {} from MonoNPC spawner {} after {} retry tick(s).\n"),
                    spawnedNpc->GetName(), monoSpawner->GetName(), iterator->Attempts + 1);
                iterator = m_pendingMonoNpcResolves.erase(iterator);
                continue;
            }

            iterator->Attempts += 1;
            iterator->CooldownPassesRemaining = kMonoNpcResolveCooldownPasses;
            if (iterator->Attempts % 50 == 0)
            {
                PS::Log<LogLevel::Normal>(STR("MonoNPC spawner {} still unresolved after {} retries; continuing to wait.\n"),
                    monoSpawner->GetName(), iterator->Attempts);
            }

            ++iterator;
        }
    }

    PS::SpawnerInfo* PalSpawnLoader::FindSpawnerInfoBySpawnerActor(RC::Unreal::AActor* spawnerActor)
    {
        for (auto& spawnerInfo : m_spawns)
        {
            if (spawnerInfo.SpawnerActor == spawnerActor)
            {
                return &spawnerInfo;
            }
        }

        return nullptr;
    }

    RC::Unreal::UClass* PalSpawnLoader::ResolveComponentClass(const RC::StringType& configuredName)
    {
        // Full class object path may already be provided.
        auto* resolvedClass = UECustom::UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, configuredName.c_str());
        if (resolvedClass)
        {
            return resolvedClass;
        }

        // Enforce full package paths for AddComponents to keep the schema future-proof.
        if (configuredName.find(TEXT('/')) == RC::StringType::npos)
        {
            return nullptr;
        }

        RC::StringType packagePath = configuredName;
        const auto dotPos = packagePath.find_last_of(TEXT('.'));
        if (dotPos != RC::StringType::npos)
        {
            // Supports paths like /Game/.../BP_Component.BP_Component_C.
            const auto objectName = packagePath.substr(dotPos + 1);
            if (!objectName.ends_with(TEXT("_C")))
            {
                const auto classObjectPath = std::format(TEXT("{}_C"), packagePath);
                resolvedClass = UECustom::UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, classObjectPath.c_str());
                if (resolvedClass)
                {
                    return resolvedClass;
                }
            }

            packagePath = packagePath.substr(0, dotPos);
        }

        const auto slashPos = packagePath.find_last_of(TEXT('/'));
        if (slashPos == RC::StringType::npos || slashPos + 1 >= packagePath.size())
        {
            return nullptr;
        }

        auto assetName = packagePath.substr(slashPos + 1);
        if (assetName.ends_with(TEXT("_C")))
        {
            assetName = assetName.substr(0, assetName.size() - 2);
        }

        if (assetName.empty())
        {
            return nullptr;
        }

        const auto classObjectPath = std::format(TEXT("{}.{}_C"), packagePath, assetName);
        resolvedClass = UECustom::UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, classObjectPath.c_str());
        if (resolvedClass)
        {
            return resolvedClass;
        }

        // Preload step: try loading the blueprint asset by package path, then re-resolve class object.
        auto loadedAsset = UECustom::UKismetSystemLibrary::LoadAsset_Blocking(
            UECustom::TSoftObjectPtr<UObject>(UECustom::FSoftObjectPath(packagePath)));

        if (loadedAsset)
        {
            resolvedClass = UECustom::UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, classObjectPath.c_str());
            if (resolvedClass)
            {
                return resolvedClass;
            }
        }

        // Fallback: search already-loaded classes by short object name.
        const auto shortClassName = std::format(TEXT("{}_C"), assetName);
        if (auto* loadedBpClass = RC::Unreal::UObjectGlobals::FindObject(
                STR("BlueprintGeneratedClass"), shortClassName.c_str()))
        {
            return static_cast<UClass*>(loadedBpClass);
        }

        if (auto* loadedNativeClass = RC::Unreal::UObjectGlobals::FindObject(
                STR("Class"), shortClassName.c_str()))
        {
            return static_cast<UClass*>(loadedNativeClass);
        }

        return nullptr;
    }

    void PalSpawnLoader::AttachConfiguredComponents(RC::Unreal::AActor* resolvedNpc, const PS::SpawnerInfo& spawnerInfo)
    {
        if (!resolvedNpc)
        {
            return;
        }

        if (spawnerInfo.AddComponents.empty())
        {
            return;
        }

        auto* addComponentFunction = resolvedNpc->GetFunctionByNameInChain(TEXT("AddComponentByClass"));
        if (!addComponentFunction)
        {
            PS::Log<LogLevel::Warning>(STR("Failed to attach configured components for {} because AddComponentByClass is unavailable.\n"), resolvedNpc->GetName());
            return;
        }

        auto* finishAddComponentFunction = resolvedNpc->GetFunctionByNameInChain(TEXT("FinishAddComponent"));
        static auto actorComponentBaseClass = UECustom::UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/Engine.ActorComponent"));

        for (auto& configuredComponent : spawnerInfo.AddComponents)
        {
            auto* componentClass = ResolveComponentClass(configuredComponent.ComponentPath);
            if (!componentClass)
            {
                PS::Log<LogLevel::Warning>(STR("Failed to resolve component class from '{}'. Tried path lookup and blocking preload; skipping component attachment.\n"), configuredComponent.ComponentPath);
                continue;
            }

            if (actorComponentBaseClass && !componentClass->IsChildOf(actorComponentBaseClass))
            {
                PS::Log<LogLevel::Warning>(STR("Resolved class {} from '{}' is not an ActorComponent, skipping attachment to {}.\n"), componentClass->GetName(), configuredComponent.ComponentPath, resolvedNpc->GetName());
                continue;
            }

            auto existingComponents = resolvedNpc->K2_GetComponentsByClass(componentClass);
            if (existingComponents.Num() > 0)
            {
                PS::Log<LogLevel::Verbose>(STR("Skipping component {} on {} because an instance already exists.\n"), configuredComponent.ComponentPath, resolvedNpc->GetName());
                continue;
            }

            struct
            {
                RC::Unreal::UClass* ComponentClass = nullptr;
                bool bManualAttachment = false;
                RC::Unreal::FTransform RelativeTransform;
                bool bDeferredFinish = false;
                RC::Unreal::UObject* ReturnValue = nullptr;
            } params{};

            params.ComponentClass = componentClass;
            params.RelativeTransform = RC::Unreal::FTransform(RC::Unreal::FRotator{ 0.0, 0.0, 0.0 }, RC::Unreal::FVector{ 0.0, 0.0, 0.0 }, RC::Unreal::FVector{ 1.0, 1.0, 1.0 });
            params.bDeferredFinish = finishAddComponentFunction != nullptr;

            resolvedNpc->ProcessEvent(addComponentFunction, &params);

            if (params.ReturnValue)
            {
                if (finishAddComponentFunction)
                {
                    struct
                    {
                        RC::Unreal::UObject* Component = nullptr;
                        bool bManualAttachment = false;
                        RC::Unreal::FTransform RelativeTransform;
                    } finishParams{};

                    finishParams.Component = params.ReturnValue;
                    finishParams.RelativeTransform = params.RelativeTransform;
                    resolvedNpc->ProcessEvent(finishAddComponentFunction, &finishParams);
                }

                ApplyConfiguredComponentProperties(params.ReturnValue, configuredComponent.Properties);

                PS::Log<LogLevel::Normal>(STR("Attached component {} to resolved NPC {}.\n"), configuredComponent.ComponentPath, resolvedNpc->GetName());
            }
            else
            {
                PS::Log<LogLevel::Warning>(STR("AddComponentByClass returned null while attaching {} to {}.\n"), configuredComponent.ComponentPath, resolvedNpc->GetName());
            }
        }
    }

    void PalSpawnLoader::ApplyConfiguredComponentProperties(RC::Unreal::UObject* componentInstance, const nlohmann::json& properties)
    {
        if (!componentInstance || !properties.is_object() || properties.empty())
        {
            return;
        }

        auto findPropertyByNameLoose = [](RC::Unreal::UClass* classType, const std::string& requestedName) -> RC::Unreal::FProperty*
        {
            if (!classType)
            {
                return nullptr;
            }

            auto normalize = [](const std::string& value)
            {
                std::string lowered;
                lowered.reserve(value.size());
                for (unsigned char c : value)
                {
                    lowered.push_back(static_cast<char>(std::tolower(c)));
                }
                return lowered;
            };

            const auto requestedLower = normalize(requestedName);
            RC::Unreal::FProperty* bestMatch = nullptr;

            for (auto* property = classType->GetPropertyLink(); property != nullptr; property = property->GetPropertyLinkNext())
            {
                const auto propertyNameUtf8 = RC::to_string(property->GetName());
                if (propertyNameUtf8 == requestedName)
                {
                    return property;
                }

                if (normalize(propertyNameUtf8) == requestedLower)
                {
                    bestMatch = property;
                }
            }

            return bestMatch;
        };

        int successfulChanges = 0;
        for (auto& [propertyName, propertyValue] : properties.items())
        {
            auto* property = findPropertyByNameLoose(componentInstance->GetClassPrivate(), propertyName);
            if (!property)
            {
                PS::Log<LogLevel::Warning>(STR("Property '{}' does not exist on component {}.\n"), RC::to_generic_string(propertyName), componentInstance->GetName());
                continue;
            }

            try
            {
                Palworld::PropertyHelper::CopyJsonValueToContainer(componentInstance, property, propertyValue);
                ++successfulChanges;
            }
            catch (const std::exception& ex)
            {
                PS::Log<LogLevel::Warning>(STR("Failed to set component property '{}' on {}: {}\n"),
                    RC::to_generic_string(propertyName), componentInstance->GetName(), RC::to_generic_string(ex.what()));
            }
        }

        if (successfulChanges > 0)
        {
            PS::Log<LogLevel::Normal>(STR("Applied {} configured property change(s) to component {}.\n"), successfulChanges, componentInstance->GetName());
        }
    }

    void PalSpawnLoader::SpawnMonoNPC(UECustom::UWorldPartitionRuntimeLevelStreamingCell* cell, PS::SpawnerInfo& spawnerInfo)
    {
        if (spawnerInfo.SpawnerActor && RC::Unreal::UObject::IsReal(spawnerInfo.SpawnerActor) && !spawnerInfo.SpawnerActor->IsUnreachable())
        {
            spawnerInfo.bExistsInWorld = true;
            spawnerInfo.Cell = cell;
            PS::Log<LogLevel::Normal>(STR("Skipping MonoNPC spawn for {} because spawner actor {} is still alive.\n"),
                spawnerInfo.NPCID.ToString(), spawnerInfo.SpawnerActor->GetName());
            return;
        }

        static auto bpClass = UECustom::UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Game/Pal/Blueprint/Spawner/BP_MonoNPCSpawner.BP_MonoNPCSpawner_C"));
        if (!bpClass)
        {
            PS::Log<LogLevel::Error>(STR("Unable to get class BP_MonoNPCSpawner, failed to spawn {}\n"), spawnerInfo.NPCID.ToString());
            return;
        }

        auto world = cell->GetWorld();
        if (!world)
        {
            PS::Log<LogLevel::Error>(STR("Unable to get world from cell, failed to spawn {}\n"), spawnerInfo.NPCID.ToString());
            return;
        }

        auto transform = FTransform(spawnerInfo.Rotation, spawnerInfo.Location, { 1.0, 1.0, 1.0 });
        auto spawnedActor = world->SpawnActor(bpClass, &transform);
        if (!spawnedActor)
        {
            PS::Log<LogLevel::Error>(STR("SpawnActor failed for BP_MonoNPCSpawner, failed to spawn {}\n"), spawnerInfo.NPCID.ToString());
            return;
        }

        auto monoSpawner = static_cast<AMonoNPCSpawner*>(spawnedActor);
        if (!monoSpawner)
        {
            PS::Log<LogLevel::Error>(STR("Failed to cast spawned actor to AMonoNPCSpawner for {}\n"), spawnerInfo.NPCID.ToString());
            return;
        }

        monoSpawner->GetHumanName() = spawnerInfo.NPCID;
        monoSpawner->GetCharaName() = spawnerInfo.NPCID;
        monoSpawner->GetLevel() = spawnerInfo.Level;
        monoSpawner->GetOtomoName() = spawnerInfo.OtomoName;

        // Queue deferred resolution with a warmup so world streaming can finish before first lookup.
        m_pendingMonoNpcResolves.push_back({ monoSpawner, 0, kMonoNpcResolveWarmupPasses, 0 });
        PS::Log<LogLevel::Normal>(STR("Queued MonoNPC spawner {} for deferred spawned-actor resolution after warmup.\n"), monoSpawner->GetName());

        spawnerInfo.bExistsInWorld = true;
        spawnerInfo.Cell = cell;
        spawnerInfo.SpawnerActor = monoSpawner;

        PS::Log<LogLevel::Verbose>(STR("Spawned {} in {}\n"), spawnerInfo.ToString(), cell->GetName());
    }

    void PalSpawnLoader::SpawnSheet(UECustom::UWorldPartitionRuntimeLevelStreamingCell* cell, PS::SpawnerInfo& spawnerInfo)
    {
        static auto sheetBPClass = UECustom::UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Game/Pal/Blueprint/Spawner/BP_PalSpawner_Standard.BP_PalSpawner_Standard_C"));
        if (!sheetBPClass)
        {
            PS::Log<LogLevel::Error>(STR("Unable to get class BP_PalSpawner_Standard, failed to spawn sheet\n"));
            return;
        }

        auto world = cell->GetWorld();
        if (!world)
        {
            PS::Log<LogLevel::Error>(STR("Unable to get world from cell, failed to spawn sheet\n"));
            return;
        }

        auto transform = FTransform(spawnerInfo.Rotation, spawnerInfo.Location, { 1.0, 1.0, 1.0 });
        auto spawnedActor = world->SpawnActor(sheetBPClass, &transform);
        auto palSheet = static_cast<APalSpawnerStandard*>(spawnedActor);

        PalSpawnerGroup spawnerGroup;
        for (auto& spawnGroupListItem : spawnerInfo.SpawnGroupList)
        {
            spawnerGroup.Weight = spawnGroupListItem.Weight;
            spawnerGroup.OnlyTime = spawnGroupListItem.OnlyTime;
            spawnerGroup.OnlyWeather = spawnGroupListItem.OnlyWeather;

            for (auto& palListInfo : spawnGroupListItem.PalList)
            {
                spawnerGroup.PalList.push_back(palListInfo);
            }
        }

        palSheet->SetSpawnerName(spawnerInfo.SpawnerName);
        palSheet->SetSpawnerType(spawnerInfo.SpawnerType);
        palSheet->AddSpawnerGroup(spawnerGroup);

        spawnerInfo.bExistsInWorld = true;
        spawnerInfo.Cell = cell;
        spawnerInfo.SpawnerActor = palSheet;

        PS::Log<LogLevel::Verbose>(STR("Spawned {} in {}\n"), spawnerInfo.ToString(), cell->GetName());
    }

    void PalSpawnLoader::AddBossSpawnLocationToMap(PS::SpawnerInfo& spawnerInfo)
    {
        if (!m_bossSpawnerLocationData) {
            PS::Log<LogLevel::Error>(STR("Failed to add boss icon to map, DT_BossSpawnerLoactionData was not initialized.\n"));
            return;
        }

        if (spawnerInfo.SpawnGroupList.size() == 0)
        {
            PS::Log<LogLevel::Error>(STR("Failed to add boss icon to map, SpawnGroupList has no entries.\n"));
            return;
        }

        auto& firstGroup = spawnerInfo.SpawnGroupList.at(0);
        if (firstGroup.PalList.size() == 0)
        {
            PS::Log<LogLevel::Error>(STR("Failed to add boss icon to map, PalList has no entries.\n"));
            return;
        }

        auto& firstPal = firstGroup.PalList.at(0);

        auto characterId = firstPal.PalId;
        if (characterId == NAME_None)
        {
            characterId = firstPal.NPCID;
        }

        TableSerializer serializer(m_bossSpawnerLocationData);

        auto newRow = serializer.Add(spawnerInfo.SpawnerName);
        newRow->SetValue<FName>(STR("SpawnerID"), spawnerInfo.SpawnerName);
        newRow->SetValue<FName>(STR("CharacterID"), characterId);
        newRow->SetValue<FVector>(STR("Location"), spawnerInfo.Location);
        newRow->SetValue<int>(STR("Level"), firstPal.Level);

        spawnerInfo.bHasMapIcon = true;
    }

    void PalSpawnLoader::RemoveBossSpawnLocationFromMap(PS::SpawnerInfo& spawnerInfo)
    {
        if (!m_bossSpawnerLocationData) {
            PS::Log<LogLevel::Error>(STR("Failed to remove boss icon from map, DT_BossSpawnerLoactionData was not initialized.\n"));
            return;
        }

        // This doesn't seem to actually help.
        // WBP_Map_Base saves boss icons permanently to a variable called BossIcons.
        // The logic goes something like this: 
        //  (WBP_Map_Base):"Setup Boss Icon" -> 
        //  (WBP_Map_Base):"Add Boss Icon" -> 
        //  (WBP_Map_Base).(WBP_Map_Body): Add Icon By Location
        // Ideally we'd want to clear the icons manually from WBP_Map_Body and then run "Setup Boss Icon" in WBP_Map_Base again to refresh them.
        m_bossSpawnerLocationData->RemoveRow(spawnerInfo.SpawnerName);
    }

    void PalSpawnLoader::DestroySpawnersInCell(UECustom::UWorldPartitionRuntimeLevelStreamingCell* cell)
    {
        for (auto& spawn : m_spawns)
        {
            if (spawn.Cell == cell)
            {
                spawn.Unload();
                PS::Log<LogLevel::Verbose>(STR("Unloaded spawn in Cell {}\n"), cell->GetName());
            }
        }
    }

    void PalSpawnLoader::UnloadMod(const std::filesystem::path::string_type& modName)
    {
        std::erase_if(m_spawns, [&](PS::SpawnerInfo& spawn) {
            if (spawn.ModName == modName)
            {
                spawn.Unload();

                if (spawn.bHasMapIcon)
                {
                    RemoveBossSpawnLocationFromMap(spawn);
                }

                return true;
            }

            return false;
        });
    }

    void PalSpawnLoader::CleanupSpawns()
    {
        // Main world is unloading (returning to title).
        // We want to reset all the containers so that our spawners can be spawned again when we re-enter the world.
        for (auto& spawnInfo : m_spawns)
        {
            spawnInfo.Cell = nullptr;
            spawnInfo.SpawnerActor = nullptr;
            spawnInfo.bExistsInWorld = false;
        }

        m_loadedCells.Empty();
        m_pendingMonoNpcResolves.clear();
        m_engineTickCounter = 0;

        PS::Log<LogLevel::Verbose>(STR("Session ending, spawners have been cleaned up.\n"));
    }

    void PalSpawnLoader::OnWorldCleanup(RC::Unreal::UWorld* thisWorld, bool bSessionEnded, bool bCleanupResources, RC::Unreal::UWorld* newWorld)
    {
        WorldCleanupHook.call(thisWorld, bSessionEnded, bCleanupResources, newWorld);

        static auto NAME_MainWorld5 = FName(STR("PL_MainWorld5"), FNAME_Add);
        if (NAME_MainWorld5 != thisWorld->GetNamePrivate())
        {
            return;
        }

        if (WorldCleanupCallback)
        {
            WorldCleanupCallback(thisWorld, bSessionEnded, bCleanupResources, newWorld);
        }
    }
}
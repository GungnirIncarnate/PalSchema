#pragma once

#include "Loader/PalModLoaderBase.h"
#include "Loader/Spawner/MonoNpcComponentProfileApplicator.h"
#include "Loader/Spawner/SpawnerInfo.h"
#include "Unreal/Hooks/GlobalCallbackId.hpp"
#include "Unreal/NameTypes.hpp"
#include "Unreal/Rotator.hpp"
#include "Unreal/UnrealCoreStructs.hpp"
#include "nlohmann/json.hpp"
#include "safetyhook.hpp"

namespace RC::Unreal
{
    class UDataTable;
    class UFunction;
    class UWorld;
}

namespace UECustom
{
    class UWorldPartitionRuntimeLevelStreamingCell;
}

namespace Palworld
{
    class AMonoNPCSpawner;

    class PalSpawnLoader : public PalModLoaderBase
    {
    public:
        PalSpawnLoader();
        ~PalSpawnLoader();

        void Reload(const std::filesystem::path::string_type& modName, const nlohmann::json& data);

        // This is called whenever a world partition is loaded within the main world.
        void OnCellLoaded(UECustom::UWorldPartitionRuntimeLevelStreamingCell* cell);

        // This is called whenever a world partition is unloaded within the main world.
        void OnCellUnloaded(UECustom::UWorldPartitionRuntimeLevelStreamingCell* cell);

    protected:
        virtual void OnLoad(const std::filesystem::path& loaderPath, const RC::StringType& modName, const EEngineLifecyclePhase& engineLifecyclePhase) override final;
        virtual void OnAutoReload(const std::filesystem::path::string_type& modName, const std::filesystem::path& modFilePath) override final;
        virtual bool CanInitialize(const EEngineLifecyclePhase& engineLifecyclePhase) override final;
        virtual bool OnInitialize() override final;

    private:
        struct PendingMonoNpcResolve
        {
            AMonoNPCSpawner* Spawner = nullptr;
            RC::Unreal::uint32 Attempts = 0;
            RC::Unreal::uint32 WarmupPassesRemaining = 0;
            RC::Unreal::uint32 CooldownPassesRemaining = 0;
        };

        RC::Unreal::UDataTable* m_bossSpawnerLocationData = nullptr;

        // Storing loaded cells here for mod reloading purposes.
        RC::Unreal::TArray<UECustom::UWorldPartitionRuntimeLevelStreamingCell*> m_loadedCells;
        std::vector<PS::SpawnerInfo> m_spawns;

        void SetupWorldPartitionHooks();
        void LoadSpawns(const RC::StringType& modName, const nlohmann::json& data);
        void RegisterSpawn(const std::filesystem::path::string_type& modName, const nlohmann::json& value);
        void RegisterSheet(const std::filesystem::path::string_type& modName, PS::SpawnerInfo& spawnerInfo, const nlohmann::json& value);
        void RegisterMonoNPC(const std::filesystem::path::string_type& modName, PS::SpawnerInfo& spawnerInfo, const nlohmann::json& value);

        void ProcessCellSpawners(UECustom::UWorldPartitionRuntimeLevelStreamingCell* cell);
        void CreateSpawner(UECustom::UWorldPartitionRuntimeLevelStreamingCell* cell, PS::SpawnerInfo& spawnerInfo);
        void SpawnMonoNPC(UECustom::UWorldPartitionRuntimeLevelStreamingCell* cell, PS::SpawnerInfo& spawnerInfo);
        void SpawnSheet(UECustom::UWorldPartitionRuntimeLevelStreamingCell* cell, PS::SpawnerInfo& spawnerInfo);

        void AddBossSpawnLocationToMap(PS::SpawnerInfo& spawnerInfo);
        void RemoveBossSpawnLocationFromMap(PS::SpawnerInfo& spawnerInfo);
        void DestroySpawnersInCell(UECustom::UWorldPartitionRuntimeLevelStreamingCell* cell);
        void TryResolvePendingMonoNpcSpawners();
        PS::SpawnerInfo* FindSpawnerInfoBySpawnerActor(AMonoNPCSpawner* spawnerActor);

        void UnloadMod(const std::filesystem::path::string_type& modName);
        void CleanupSpawns();

    private:
        RC::Unreal::CallbackId m_onLevelShownCallbackId{};
        RC::Unreal::CallbackId m_onLevelHiddenCallbackId{};
        RC::Unreal::UFunction* m_onLevelShownFunction = nullptr;
        RC::Unreal::UFunction* m_onLevelHiddenFunction = nullptr;
        RC::Unreal::Hook::GlobalCallbackId m_engineTickCallbackId{};
        std::vector<PendingMonoNpcResolve> m_pendingMonoNpcResolves;
        int m_engineTickCounter = 0;
        PS::MonoNpcComponentProfileApplicator m_monoNpcComponentProfileApplicator;

    private:
        static inline std::function<void(RC::Unreal::UWorld*, bool, bool, RC::Unreal::UWorld*)> WorldCleanupCallback = nullptr;
        static inline SafetyHookInline WorldCleanupHook;

        static void OnWorldCleanup(RC::Unreal::UWorld* thisWorld, bool bSessionEnded, bool bCleanupResources, RC::Unreal::UWorld* newWorld);
    };
}
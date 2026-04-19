#pragma once

#include <Loader/Spawner/Components/MonoNpcMerchantComponentHandler.h>
#include <Loader/Spawner/SpawnerInfo.h>

namespace RC::Unreal
{
    class AActor;
}

namespace PS
{
    class MonoNpcComponentProfileApplicator
    {
    public:
        void ParseProfileComponents(const nlohmann::json& spawnNode, PS::SpawnerInfo& spawnerInfo) const;
        void ApplyProfileComponents(RC::Unreal::AActor* resolvedNpc, const PS::SpawnerInfo& spawnerInfo) const;

    private:
        PS::MonoNpcMerchantComponentHandler m_merchantHandler;
    };
}

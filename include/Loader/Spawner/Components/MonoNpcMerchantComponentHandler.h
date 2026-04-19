#pragma once

#include <Loader/Spawner/Components/MonoNpcComponentProfiles.h>
#include <nlohmann/json.hpp>
#include <Unreal/Core/Containers/Array.hpp>
#include <Unreal/UObject.hpp>

namespace RC::Unreal
{
    class AActor;
    class UClass;
}

namespace PS
{
    class MonoNpcMerchantComponentHandler
    {
    public:
        void ParseConfig(const nlohmann::json& merchantNode, PS::MerchantProfile& outProfile) const;
        void Apply(RC::Unreal::AActor* resolvedNpc, const PS::MerchantProfile& merchantProfile) const;

    private:
        RC::Unreal::UClass* ResolveComponentClass() const;
        RC::Unreal::UObject* FindOrAddComponent(RC::Unreal::AActor* resolvedNpc, RC::Unreal::UClass* componentClass) const;
        void ApplyMerchantProperties(RC::Unreal::UObject* componentInstance, const PS::MerchantProfile& merchantProfile) const;
    };
}

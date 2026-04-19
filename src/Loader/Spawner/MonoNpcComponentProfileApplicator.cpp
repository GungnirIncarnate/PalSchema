#include <Loader/Spawner/MonoNpcComponentProfileApplicator.h>
#include <Utility/JsonHelpers.h>

namespace PS
{
    void MonoNpcComponentProfileApplicator::ParseProfileComponents(const nlohmann::json& spawnNode, PS::SpawnerInfo& spawnerInfo) const
    {
        spawnerInfo.MonoNpcProfiles.RequiredComponents.clear();
        spawnerInfo.MonoNpcProfiles.Payloads.clear();

        if (PS::JsonHelpers::FieldExists(spawnNode, "Merchant"))
        {
            PS::MerchantProfile merchantProfile{};
            m_merchantHandler.ParseConfig(spawnNode.at("Merchant"), merchantProfile);

            spawnerInfo.MonoNpcProfiles.RequiredComponents.push_back(PS::MonoNpcProfileComponentType::Merchant);
            spawnerInfo.MonoNpcProfiles.Payloads.emplace(
                PS::MonoNpcProfileComponentType::Merchant, PS::MonoNpcComponentPayload{ std::move(merchantProfile) });
        }
    }

    void MonoNpcComponentProfileApplicator::ApplyProfileComponents(RC::Unreal::AActor* resolvedNpc, const PS::SpawnerInfo& spawnerInfo) const
    {
        for (auto componentType : spawnerInfo.MonoNpcProfiles.RequiredComponents)
        {
            switch (componentType)
            {
            case PS::MonoNpcProfileComponentType::Merchant:
                if (auto payloadIterator = spawnerInfo.MonoNpcProfiles.Payloads.find(componentType);
                    payloadIterator != spawnerInfo.MonoNpcProfiles.Payloads.end())
                {
                    if (auto* merchantProfile = std::get_if<PS::MerchantProfile>(&payloadIterator->second))
                    {
                        m_merchantHandler.Apply(resolvedNpc, *merchantProfile);
                    }
                }
                break;
            }
        }
    }
}

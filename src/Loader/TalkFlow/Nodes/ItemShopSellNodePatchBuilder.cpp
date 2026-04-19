#include "Loader/TalkFlow/Nodes/ItemShopSellNodePatchBuilder.h"

namespace Palworld::TalkFlow::Nodes
{
    void ItemShopSellNodePatchBuilder::Build(const std::vector<std::string>& mappedNodeNames, nlohmann::json& nodePatches)
    {
        for (const auto& nodeName : mappedNodeNames)
        {
            nodePatches[nodeName] = {
                { "OpenItemShopTabType", "E_PalItemShopTabType::NewEnumerator1" }
            };
        }
    }
}

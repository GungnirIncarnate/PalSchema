#include "Loader/TalkFlow/Nodes/PalShopSellNodePatchBuilder.h"

namespace Palworld::TalkFlow::Nodes
{
    void PalShopSellNodePatchBuilder::Build(const std::vector<std::string>& mappedNodeNames, nlohmann::json& nodePatches)
    {
        for (const auto& nodeName : mappedNodeNames)
        {
            nodePatches[nodeName] = {
                { "OpenPalShopTabType", "E_PalItemShopTabType::NewEnumerator1" }
            };
        }
    }
}

#include "Loader/TalkFlow/Nodes/PalShopBuyNodePatchBuilder.h"

namespace Palworld::TalkFlow::Nodes
{
    void PalShopBuyNodePatchBuilder::Build(const std::vector<std::string>& mappedNodeNames, nlohmann::json& nodePatches)
    {
        for (const auto& nodeName : mappedNodeNames)
        {
            nodePatches[nodeName] = {
                { "OpenPalShopTabType", "E_PalItemShopTabType::NewEnumerator0" }
            };
        }
    }
}

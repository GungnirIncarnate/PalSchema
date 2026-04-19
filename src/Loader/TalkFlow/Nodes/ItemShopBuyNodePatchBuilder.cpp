#include "Loader/TalkFlow/Nodes/ItemShopBuyNodePatchBuilder.h"

namespace Palworld::TalkFlow::Nodes
{
    void ItemShopBuyNodePatchBuilder::Build(const std::vector<std::string>& mappedNodeNames, nlohmann::json& nodePatches)
    {
        for (const auto& nodeName : mappedNodeNames)
        {
            nodePatches[nodeName] = {
                { "OpenItemShopTabType", "E_PalItemShopTabType::NewEnumerator0" }
            };
        }
    }
}

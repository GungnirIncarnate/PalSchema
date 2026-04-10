#include "Loader/TalkFlow/Nodes/ShopNodePatchBuilder.h"

namespace Palworld::TalkFlow::Nodes
{
    void ShopNodePatchBuilder::Build(
        const std::vector<std::string>& mappedBuyShopNodes,
        const std::vector<std::string>& mappedSellShopNodes,
        const std::vector<std::string>& mappedBuyPalShopNodes,
        const std::vector<std::string>& mappedSellPalShopNodes,
        nlohmann::json& nodePatches)
    {
        for (const auto& nodeName : mappedBuyShopNodes)
        {
            nodePatches[nodeName] = {
                { "OpenItemShopTabType", "E_PalItemShopTabType::NewEnumerator0" }
            };
        }

        for (const auto& nodeName : mappedSellShopNodes)
        {
            nodePatches[nodeName] = {
                { "OpenItemShopTabType", "E_PalItemShopTabType::NewEnumerator1" }
            };
        }

        for (const auto& nodeName : mappedBuyPalShopNodes)
        {
            nodePatches[nodeName] = {
                { "OpenPalShopTabType", "E_PalItemShopTabType::NewEnumerator0" }
            };
        }

        for (const auto& nodeName : mappedSellPalShopNodes)
        {
            nodePatches[nodeName] = {
                { "OpenPalShopTabType", "E_PalItemShopTabType::NewEnumerator1" }
            };
        }
    }
}

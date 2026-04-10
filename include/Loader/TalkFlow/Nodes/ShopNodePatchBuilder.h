#pragma once

#include "nlohmann/json.hpp"
#include <string>
#include <vector>

namespace Palworld::TalkFlow::Nodes
{
    class ShopNodePatchBuilder
    {
    public:
        static void Build(
            const std::vector<std::string>& mappedBuyShopNodes,
            const std::vector<std::string>& mappedSellShopNodes,
            const std::vector<std::string>& mappedBuyPalShopNodes,
            const std::vector<std::string>& mappedSellPalShopNodes,
            nlohmann::json& nodePatches);
    };
}

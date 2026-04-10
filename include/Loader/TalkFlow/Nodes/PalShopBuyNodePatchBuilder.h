#pragma once

#include "nlohmann/json.hpp"
#include <string>
#include <vector>

namespace Palworld::TalkFlow::Nodes
{
    class PalShopBuyNodePatchBuilder
    {
    public:
        static void Build(const std::vector<std::string>& mappedNodeNames, nlohmann::json& nodePatches);
    };
}

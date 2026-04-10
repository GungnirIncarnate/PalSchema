#pragma once

#include "Unreal/UObject.hpp"
#include <string>
#include <unordered_map>
#include <vector>

namespace Palworld::TalkFlow
{
    struct FlowNodeCatalog
    {
        std::unordered_map<std::string, RC::Unreal::UObject*> NodesByName;
        std::vector<std::string> StartNodes;
        std::vector<std::string> MessageNodes;
        std::vector<std::string> ChoiceNodes;
        std::vector<std::string> OpenItemShopNodes;
        std::vector<std::string> OpenItemShopBuyNodes;
        std::vector<std::string> OpenItemShopSellNodes;
        std::vector<std::string> OpenPalShopNodes;
        std::vector<std::string> OpenPalShopBuyNodes;
        std::vector<std::string> OpenPalShopSellNodes;
        std::vector<std::string> GetItemNodes;
        std::vector<std::string> NPCTalkBranchCountNodes;
    };
}

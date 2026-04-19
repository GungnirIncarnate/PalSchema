#pragma once

#include "nlohmann/json.hpp"
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Palworld::TalkFlow::Nodes
{
    class NPCTalkBranchCountNodePatchBuilder
    {
    public:
        static void Build(
            const std::string& ownerId,
            const nlohmann::json& conversationNodes,
            const std::vector<std::pair<std::string, std::string>>& mappedNPCTalkBranchCountNodes,
            const std::unordered_map<std::string, std::string>& logicalToRuntimeNode,
            nlohmann::json& nodePatches,
            int& requiredFlowMaxTalkCount);
    };
}

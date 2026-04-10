#pragma once

#include "String/StringType.hpp"
#include "nlohmann/json.hpp"
#include <string>
#include <unordered_map>
#include <vector>

namespace Palworld::TalkFlow::Nodes
{
    class ConversationNodePatchBuilder
    {
    public:
        static void Build(
            const std::string& ownerId,
            const std::string& ownerToken,
            const RC::StringType& assetPath,
            const nlohmann::json& conversationNodes,
            const std::vector<std::string>& logicalOrder,
            const std::unordered_map<std::string, std::string>& logicalNodeType,
            const std::unordered_map<std::string, bool>& logicalNeedsChoiceNode,
            const std::unordered_map<std::string, std::string>& logicalToRuntimeNode,
            const std::unordered_map<std::string, std::string>& logicalToMessageNode,
            const std::unordered_map<std::string, std::string>& logicalToChoiceNode,
            const std::vector<std::string>& messageNodes,
            const std::vector<std::string>& startNodes,
            bool hasStartNode,
            std::unordered_map<std::string, std::string>& registeredTalkTextDefaults,
            nlohmann::json& nodePatches,
            nlohmann::json& textEntries,
            nlohmann::json& buttonEntries);
    };
}

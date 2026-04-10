#pragma once

#include "nlohmann/json.hpp"
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>

namespace Palworld::TalkFlow::Nodes
{
    class FixedMessageNodePatchBuilder
    {
    public:
        static void Build(
            const std::string& ownerId,
            const std::string& logicalId,
            const nlohmann::json& nodeDef,
            const std::string& messageNodeName,
            const std::unordered_map<std::string, std::string>& logicalToRuntimeNode,
            const std::function<std::optional<std::string>(const std::string&, const std::string&, const nlohmann::json&)>& resolveButtonTargetNode,
            nlohmann::json& nodePatches);
    };
}

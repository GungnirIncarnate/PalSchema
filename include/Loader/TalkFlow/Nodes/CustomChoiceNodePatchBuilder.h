#pragma once

#include "nlohmann/json.hpp"
#include <functional>
#include <optional>
#include <string>

namespace Palworld::TalkFlow::Nodes
{
    class CustomChoiceNodePatchBuilder
    {
    public:
        static void Build(
            const std::string& logicalId,
            const nlohmann::json& nodeDef,
            const std::string& choiceNodeName,
            const std::function<std::string(const std::string&, const std::string&, const nlohmann::json&)>& resolveButtonTextId,
            const std::function<std::string(const std::string&, const std::string&, const nlohmann::json&)>& resolveButtonDefaultText,
            const std::function<void(const std::string&, const std::string&)>& registerButtonTextEntry,
            const std::function<std::optional<std::string>(const std::string&, const std::string&, const nlohmann::json&)>& resolveButtonTargetNode,
            nlohmann::json& nodePatches);
    };
}

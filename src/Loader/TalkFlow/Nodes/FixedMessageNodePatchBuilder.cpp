#include "Loader/TalkFlow/Nodes/FixedMessageNodePatchBuilder.h"

#include <format>
#include <stdexcept>

namespace Palworld::TalkFlow::Nodes
{
    void FixedMessageNodePatchBuilder::Build(
        const std::string& ownerId,
        const std::string& logicalId,
        const nlohmann::json& nodeDef,
        const std::string& messageNodeName,
        const std::unordered_map<std::string, std::string>& logicalToRuntimeNode,
        const std::function<std::optional<std::string>(const std::string&, const std::string&, const nlohmann::json&)>& resolveButtonTargetNode,
        nlohmann::json& nodePatches)
    {
        auto messagePatch = nlohmann::json::object();

        std::optional<std::string> targetNodeName;
        if (nodeDef.contains("LinkID") && nodeDef.at("LinkID").is_string())
        {
            const auto targetLogicalId = nodeDef.at("LinkID").get<std::string>();
            auto targetIt = logicalToRuntimeNode.find(targetLogicalId);
            if (targetIt == logicalToRuntimeNode.end())
            {
                throw std::runtime_error(std::format(
                    "Conversation '{}': node-level LinkID '{}' in node '{}' does not exist.",
                    ownerId,
                    targetLogicalId,
                    logicalId));
            }
            targetNodeName = targetIt->second;
        }
        else if (nodeDef.contains("Buttons") && nodeDef.at("Buttons").is_object() && !nodeDef.at("Buttons").empty())
        {
            auto buttonIt = nodeDef.at("Buttons").items().begin();
            targetNodeName = resolveButtonTargetNode(logicalId, buttonIt.key(), buttonIt.value());
        }

        if (targetNodeName.has_value())
        {
            messagePatch["Connections"] = nlohmann::json::array({
                {
                    { "Key", "Out" },
                    { "Value", {
                        { "NodeName", targetNodeName.value() },
                        { "PinName", "In" }
                    } }
                }
            });
        }

        nodePatches[messageNodeName].update(messagePatch);
    }
}

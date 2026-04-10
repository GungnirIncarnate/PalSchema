#include "Loader/TalkFlow/Nodes/TalkCountBranchNodePatchBuilder.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <stdexcept>

namespace Palworld::TalkFlow::Nodes
{
    void TalkCountBranchNodePatchBuilder::Build(
        const std::string& ownerId,
        const nlohmann::json& conversationNodes,
        const std::vector<std::pair<std::string, std::string>>& mappedTalkCountBranchNodes,
        const std::unordered_map<std::string, std::string>& logicalToRuntimeNode,
        nlohmann::json& nodePatches,
        int& requiredFlowMaxTalkCount)
    {
        for (const auto& [logicalId, nodeName] : mappedTalkCountBranchNodes)
        {
            const auto& nodeDef = conversationNodes.at(logicalId);
            auto countBranchPatch = nlohmann::json::object();
            auto branchConnections = nlohmann::json::array();
            std::vector<std::string> configuredBranchPins;
            int maxTalkCount = 0;

            auto addBranchLink = [&](const char* pinName, const std::string& targetLogicalId)
            {
                auto targetIt = logicalToRuntimeNode.find(targetLogicalId);
                if (targetIt == logicalToRuntimeNode.end())
                {
                    throw std::runtime_error(std::format(
                        "Conversation '{}': TalkCountBranch node '{}' link '{}' references unknown node '{}'.",
                        ownerId,
                        logicalId,
                        pinName,
                        targetLogicalId
                    ));
                }

                configuredBranchPins.emplace_back(pinName);

                const auto pinAsString = std::string(pinName);
                const auto isNumericPin = !pinAsString.empty() &&
                    std::all_of(pinAsString.begin(), pinAsString.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
                if (isNumericPin)
                {
                    maxTalkCount = std::max(maxTalkCount, std::stoi(pinAsString));
                }

                branchConnections.push_back({
                    { "Key", pinName },
                    { "Value", {
                        { "NodeName", targetIt->second },
                        { "PinName", "In" }
                    } }
                });
            };

            if (nodeDef.contains("Routes"))
            {
                if (!nodeDef.at("Routes").is_object())
                {
                    throw std::runtime_error(std::format(
                        "Conversation '{}': TalkCountBranch node '{}' has non-object Routes.",
                        ownerId,
                        logicalId
                    ));
                }

                for (const auto& [pinName, targetLogicalIdValue] : nodeDef.at("Routes").items())
                {
                    if (!targetLogicalIdValue.is_string())
                    {
                        throw std::runtime_error(std::format(
                            "Conversation '{}': TalkCountBranch node '{}' route '{}' must be a string.",
                            ownerId,
                            logicalId,
                            pinName
                        ));
                    }

                    addBranchLink(pinName.c_str(), targetLogicalIdValue.get<std::string>());
                }
            }
            else
            {
                if (!nodeDef.contains("FirstLinkID") || !nodeDef.at("FirstLinkID").is_string())
                {
                    throw std::runtime_error(std::format(
                        "Conversation '{}': TalkCountBranch node '{}' requires string FirstLinkID (or Routes object).",
                        ownerId,
                        logicalId
                    ));
                }

                if (!nodeDef.contains("RepeatLinkID") || !nodeDef.at("RepeatLinkID").is_string())
                {
                    throw std::runtime_error(std::format(
                        "Conversation '{}': TalkCountBranch node '{}' requires string RepeatLinkID (or Routes object).",
                        ownerId,
                        logicalId
                    ));
                }

                const auto firstLinkId = nodeDef.at("FirstLinkID").get<std::string>();
                const auto repeatLinkId = nodeDef.at("RepeatLinkID").get<std::string>();
                const auto secondLinkId = (nodeDef.contains("SecondLinkID") && nodeDef.at("SecondLinkID").is_string())
                    ? nodeDef.at("SecondLinkID").get<std::string>()
                    : repeatLinkId;

                addBranchLink("1", firstLinkId);
                addBranchLink("2", secondLinkId);
                addBranchLink("Loop", repeatLinkId);
            }

            if (!branchConnections.empty())
            {
                std::sort(configuredBranchPins.begin(), configuredBranchPins.end());
                configuredBranchPins.erase(std::unique(configuredBranchPins.begin(), configuredBranchPins.end()), configuredBranchPins.end());

                if (maxTalkCount > 0)
                {
                    requiredFlowMaxTalkCount = std::max(requiredFlowMaxTalkCount, maxTalkCount);
                }

                if (!configuredBranchPins.empty())
                {
                    countBranchPatch["OutputPins"] = nlohmann::json::array();
                    for (const auto& pin : configuredBranchPins)
                    {
                        countBranchPatch["OutputPins"].push_back({
                            { "PinName", pin },
                            { "PinFriendlyName", pin },
                            { "PinToolTip", "" }
                        });
                    }
                }

                countBranchPatch["Connections"] = std::move(branchConnections);
                nodePatches[nodeName] = std::move(countBranchPatch);
            }
        }
    }
}

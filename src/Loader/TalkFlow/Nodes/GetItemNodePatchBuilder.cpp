#include "Loader/TalkFlow/Nodes/GetItemNodePatchBuilder.h"

#include <format>
#include <stdexcept>

namespace Palworld::TalkFlow::Nodes
{
    void GetItemNodePatchBuilder::Build(
        const std::string& ownerId,
        const nlohmann::json& conversationNodes,
        const std::vector<std::pair<std::string, std::string>>& mappedGetItemNodes,
        const std::unordered_map<std::string, std::string>& logicalToRuntimeNode,
        nlohmann::json& nodePatches)
    {
        for (const auto& [logicalId, nodeName] : mappedGetItemNodes)
        {
            const auto& nodeDef = conversationNodes.at(logicalId);
            auto getItemPatch = nlohmann::json::object();

            if (nodeDef.contains("NetworkInvokeName"))
            {
                if (!nodeDef.at("NetworkInvokeName").is_string())
                {
                    throw std::runtime_error(std::format(
                        "Conversation '{}': GetItem node '{}' has non-string NetworkInvokeName.",
                        ownerId,
                        logicalId
                    ));
                }

                getItemPatch["NetworkInvokeName"] = nodeDef.at("NetworkInvokeName").get<std::string>();
            }

            if (nodeDef.contains("bSaveNetworkInvoke") || nodeDef.contains("SaveNetworkInvoke"))
            {
                const auto saveKey = nodeDef.contains("bSaveNetworkInvoke") ? "bSaveNetworkInvoke" : "SaveNetworkInvoke";
                if (!nodeDef.at(saveKey).is_boolean())
                {
                    throw std::runtime_error(std::format(
                        "Conversation '{}': GetItem node '{}' has non-boolean {}.",
                        ownerId,
                        logicalId,
                        saveKey
                    ));
                }

                getItemPatch["bSaveNetworkInvoke"] = nodeDef.at(saveKey).get<bool>();
            }

            if (nodeDef.contains("LotteryDataTable"))
            {
                if (!nodeDef.at("LotteryDataTable").is_string())
                {
                    throw std::runtime_error(std::format(
                        "Conversation '{}': GetItem node '{}' has non-string LotteryDataTable.",
                        ownerId,
                        logicalId
                    ));
                }

                getItemPatch["LotteryDataTable"] = nodeDef.at("LotteryDataTable").get<std::string>();
            }

            if (nodeDef.contains("GetItemList"))
            {
                if (!nodeDef.at("GetItemList").is_array())
                {
                    throw std::runtime_error(std::format(
                        "Conversation '{}': GetItem node '{}' has non-array GetItemList.",
                        ownerId,
                        logicalId
                    ));
                }

                auto normalizedGetItemList = nlohmann::json::array();
                for (const auto& itemEntry : nodeDef.at("GetItemList"))
                {
                    if (!itemEntry.is_object())
                    {
                        throw std::runtime_error(std::format(
                            "Conversation '{}': GetItem node '{}' contains non-object entry in GetItemList.",
                            ownerId,
                            logicalId
                        ));
                    }

                    if (itemEntry.contains("ItemId") || itemEntry.contains("Count"))
                    {
                        if (!itemEntry.contains("ItemId") || !itemEntry.at("ItemId").is_string())
                        {
                            throw std::runtime_error(std::format(
                                "Conversation '{}': GetItem node '{}' entry is missing string ItemId.",
                                ownerId,
                                logicalId
                            ));
                        }

                        if (!itemEntry.contains("Count") || !itemEntry.at("Count").is_number_integer())
                        {
                            throw std::runtime_error(std::format(
                                "Conversation '{}': GetItem node '{}' entry is missing integer Count.",
                                ownerId,
                                logicalId
                            ));
                        }

                        normalizedGetItemList.push_back({
                            { "Key", { { "Key", itemEntry.at("ItemId").get<std::string>() } } },
                            { "Value", itemEntry.at("Count").get<int>() }
                        });
                        continue;
                    }

                    if (!itemEntry.contains("Key") || !itemEntry.contains("Value"))
                    {
                        throw std::runtime_error(std::format(
                            "Conversation '{}': GetItem node '{}' entry must use either ItemId/Count or Key/Value format.",
                            ownerId,
                            logicalId
                        ));
                    }

                    normalizedGetItemList.push_back(itemEntry);
                }

                getItemPatch["GetItemList"] = std::move(normalizedGetItemList);
            }

            if (nodeDef.contains("LinkID"))
            {
                if (!nodeDef.at("LinkID").is_string())
                {
                    throw std::runtime_error(std::format(
                        "Conversation '{}': GetItem node '{}' has non-string LinkID.",
                        ownerId,
                        logicalId
                    ));
                }

                const auto targetLogicalId = nodeDef.at("LinkID").get<std::string>();
                auto targetIt = logicalToRuntimeNode.find(targetLogicalId);
                if (targetIt == logicalToRuntimeNode.end())
                {
                    throw std::runtime_error(std::format(
                        "Conversation '{}': GetItem node '{}' LinkID '{}' does not exist.",
                        ownerId,
                        logicalId,
                        targetLogicalId
                    ));
                }

                getItemPatch["Connections"] = nlohmann::json::array({
                    {
                        { "Key", "Out" },
                        { "Value", {
                            { "NodeName", targetIt->second },
                            { "PinName", "In" }
                        } }
                    }
                });
            }

            if (!getItemPatch.empty())
            {
                nodePatches[nodeName] = std::move(getItemPatch);
            }
        }
    }
}

#include "Loader/TalkFlow/Nodes/ConversationNodePatchBuilder.h"
#include "Loader/TalkFlow/Nodes/CustomChoiceNodePatchBuilder.h"
#include "Loader/TalkFlow/Nodes/FixedMessageNodePatchBuilder.h"

#include "Helpers/String.hpp"
#include "Utility/Logging.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <optional>
#include <stdexcept>

namespace Palworld::TalkFlow::Nodes
{
    namespace
    {
        std::string SanitizeToken(std::string value)
        {
            for (auto& c : value)
            {
                if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_')
                {
                    c = '_';
                }
            }
            return value;
        }

        void RegisterTalkTextEntry(
            std::unordered_map<std::string, std::string>& registeredTalkTextDefaults,
            nlohmann::json& targetEntries,
            const std::string& ownerId,
            const std::string& textId,
            const std::string& defaultText,
            const std::string& context)
        {
            auto existingDefaultIt = registeredTalkTextDefaults.find(textId);
            if (existingDefaultIt != registeredTalkTextDefaults.end())
            {
                if (existingDefaultIt->second != defaultText)
                {
                    throw std::runtime_error(std::format(
                        "Conversation '{}': TextID '{}' is used with conflicting default text while processing {}.",
                        ownerId,
                        textId,
                        context
                    ));
                }
            }
            else
            {
                registeredTalkTextDefaults.emplace(textId, defaultText);
            }

            targetEntries[textId] = defaultText;
        }
    }

    void ConversationNodePatchBuilder::Build(
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
        nlohmann::json& buttonEntries)
    {
        auto resolveNodeDefaultMsg = [&](const std::string& logicalId, const nlohmann::json& nodeDef) -> std::string
        {
            if (!nodeDef.contains("DefaultMsg"))
            {
                throw std::runtime_error(std::format(
                    "Conversation '{}': node '{}' must provide string DefaultMsg.",
                    ownerId,
                    logicalId
                ));
            }

            if (!nodeDef.at("DefaultMsg").is_string())
            {
                throw std::runtime_error(std::format(
                    "Conversation '{}': node '{}' has non-string DefaultMsg.",
                    ownerId,
                    logicalId
                ));
            }

            return nodeDef.at("DefaultMsg").get<std::string>();
        };

        auto resolveNodeTextId = [&](const std::string& logicalId, const nlohmann::json& nodeDef) -> std::string
        {
            if (nodeDef.contains("TextID"))
            {
                if (!nodeDef.at("TextID").is_string())
                {
                    throw std::runtime_error(std::format(
                        "Conversation '{}': node '{}' has non-string TextID.",
                        ownerId,
                        logicalId
                    ));
                }

                return nodeDef.at("TextID").get<std::string>();
            }

            return std::format("{}_{}_MSG", ownerToken, SanitizeToken(logicalId));
        };

        auto resolveButtonDefaultText = [&](const std::string& logicalId, const std::string& buttonId, const nlohmann::json& buttonDef) -> std::string
        {
            if (!buttonDef.contains("DefaultText"))
            {
                throw std::runtime_error(std::format(
                    "Conversation '{}': button '{}' in node '{}' must provide string DefaultText.",
                    ownerId,
                    buttonId,
                    logicalId
                ));
            }

            if (!buttonDef.at("DefaultText").is_string())
            {
                throw std::runtime_error(std::format(
                    "Conversation '{}': button '{}' in node '{}' has non-string DefaultText.",
                    ownerId,
                    buttonId,
                    logicalId
                ));
            }

            return buttonDef.at("DefaultText").get<std::string>();
        };

        auto resolveButtonTextId = [&](const std::string& logicalId, const std::string& buttonId, const nlohmann::json& buttonDef) -> std::string
        {
            if (buttonDef.contains("TextID"))
            {
                if (!buttonDef.at("TextID").is_string())
                {
                    throw std::runtime_error(std::format(
                        "Conversation '{}': button '{}' in node '{}' has non-string TextID.",
                        ownerId,
                        buttonId,
                        logicalId
                    ));
                }

                return buttonDef.at("TextID").get<std::string>();
            }

            return std::format(
                "{}_{}_BTN_{}",
                ownerToken,
                SanitizeToken(logicalId),
                SanitizeToken(buttonId)
            );
        };

        std::optional<std::string> exitNodeName;

        auto resolveButtonTargetNode = [&](const std::string& logicalId, const std::string& buttonId, const nlohmann::json& buttonDef) -> std::optional<std::string>
        {
            if (buttonDef.contains("LinkID"))
            {
                if (!buttonDef.at("LinkID").is_string())
                {
                    throw std::runtime_error(std::format("Conversation '{}': Button '{}' in node '{}' has non-string LinkID.", ownerId, buttonId, logicalId));
                }

                const auto targetLogicalId = buttonDef.at("LinkID").get<std::string>();
                auto targetIt = logicalToRuntimeNode.find(targetLogicalId);
                if (targetIt == logicalToRuntimeNode.end())
                {
                    throw std::runtime_error(std::format(
                        "Conversation '{}': LinkID '{}' from node '{}' does not exist.",
                        ownerId,
                        targetLogicalId,
                        logicalId
                    ));
                }

                return targetIt->second;
            }

            if (buttonDef.contains("Action"))
            {
                if (!buttonDef.at("Action").is_string())
                {
                    throw std::runtime_error(std::format("Conversation '{}': Button '{}' in node '{}' has non-string Action.", ownerId, buttonId, logicalId));
                }

                const auto action = buttonDef.at("Action").get<std::string>();
                if (action == "Shop")
                {
                    auto shopTargetIt = std::find_if(
                        logicalOrder.begin(),
                        logicalOrder.end(),
                        [&](const std::string& nodeId)
                        {
                            auto typeIt = logicalNodeType.find(nodeId);
                            if (typeIt == logicalNodeType.end())
                            {
                                return false;
                            }

                            return typeIt->second == "ItemShopBuy" || typeIt->second == "ItemShopSell";
                        });

                    if (shopTargetIt != logicalOrder.end())
                    {
                        auto runtimeIt = logicalToRuntimeNode.find(*shopTargetIt);
                        if (runtimeIt != logicalToRuntimeNode.end())
                        {
                            return runtimeIt->second;
                        }
                    }

                    throw std::runtime_error(std::format(
                        "Conversation '{}': Action=Shop requested but no ItemShop node was mapped in flow '{}'.",
                        ownerId,
                        RC::to_string(assetPath)
                    ));
                }

                if (action == "Exit")
                {
                    if (!exitNodeName.has_value())
                    {
                        for (const auto& candidate : messageNodes)
                        {
                            const auto isAssignedConversationNode = std::find_if(
                                logicalToMessageNode.begin(),
                                logicalToMessageNode.end(),
                                [&](const auto& entry) { return entry.second == candidate; }
                            ) != logicalToMessageNode.end();

                            if (!isAssignedConversationNode)
                            {
                                exitNodeName = candidate;
                                break;
                            }
                        }

                        if (!exitNodeName.has_value())
                        {
                            PS::Log<RC::LogLevel::Warning>(
                                STR("Conversation '{}': Action=Exit has no spare fixed-message node in '{}'. Leaving this exit path unconnected.\n"),
                                RC::to_generic_string(ownerId),
                                assetPath
                            );
                            return std::nullopt;
                        }

                        const auto exitMsgId = std::format("{}_EXIT", ownerToken);
                        RegisterTalkTextEntry(registeredTalkTextDefaults, textEntries, ownerId, exitMsgId, "See you.", std::format("exit node for '{}'", logicalId));
                        nodePatches[exitNodeName.value()] = {
                            { "MsgIdList", nlohmann::json::array({ exitMsgId }) }
                        };
                    }

                    return exitNodeName.value();
                }

                throw std::runtime_error(std::format(
                    "Conversation '{}': unsupported Action '{}' in node '{}'. Supported actions: Shop, Exit.",
                    ownerId,
                    action,
                    logicalId
                ));
            }

            if (!exitNodeName.has_value())
            {
                for (const auto& candidate : messageNodes)
                {
                    const auto isAssignedConversationNode = std::find_if(
                        logicalToMessageNode.begin(),
                        logicalToMessageNode.end(),
                        [&](const auto& entry) { return entry.second == candidate; }
                    ) != logicalToMessageNode.end();

                    if (!isAssignedConversationNode)
                    {
                        exitNodeName = candidate;
                        break;
                    }
                }

                if (!exitNodeName.has_value())
                {
                    PS::Log<RC::LogLevel::Warning>(
                        STR("Conversation '{}': implicit Exit for button '{}' in node '{}' has no spare fixed-message node in '{}'. Leaving this exit path unconnected.\n"),
                        RC::to_generic_string(ownerId),
                        RC::to_generic_string(buttonId),
                        RC::to_generic_string(logicalId),
                        assetPath
                    );
                    return std::nullopt;
                }

                const auto exitMsgId = std::format("{}_EXIT", ownerToken);
                RegisterTalkTextEntry(registeredTalkTextDefaults, textEntries, ownerId, exitMsgId, "See you.", std::format("implicit exit node for '{}'", logicalId));
                nodePatches[exitNodeName.value()] = {
                    { "MsgIdList", nlohmann::json::array({ exitMsgId }) }
                };
            }

            return exitNodeName.value();
        };

        for (size_t i = 0; i < logicalOrder.size(); ++i)
        {
            const auto& logicalId = logicalOrder[i];
            const auto& nodeDef = conversationNodes.at(logicalId);
            const auto& nodeType = logicalNodeType.at(logicalId);

            if (nodeType == "ItemShopBuy" || nodeType == "ItemShopSell" || nodeType == "PalShopBuy" || nodeType == "PalShopSell" || nodeType == "GetItem" || nodeType == "TalkCountBranch" || nodeType == "NPCTalkBranchCount")
            {
                continue;
            }

            const auto msgId = resolveNodeTextId(logicalId, nodeDef);
            RegisterTalkTextEntry(registeredTalkTextDefaults, textEntries, ownerId, msgId, resolveNodeDefaultMsg(logicalId, nodeDef), std::format("node '{}'", logicalId));

            auto messageNodeIt = logicalToMessageNode.find(logicalId);
            if (messageNodeIt == logicalToMessageNode.end())
            {
                PS::Log<RC::LogLevel::Warning>(
                    STR("Conversation '{}': no message node could be allocated for logical node '{}'.\n"),
                    RC::to_generic_string(ownerId),
                    RC::to_generic_string(logicalId)
                );
                continue;
            }

            const auto& messageNodeName = messageNodeIt->second;
            const auto needsChoiceNode = logicalNeedsChoiceNode.at(logicalId);

            auto messagePatch = nlohmann::json::object();
            messagePatch["MsgIdList"] = nlohmann::json::array({ msgId });
            nodePatches[messageNodeName] = messagePatch;

            if (!needsChoiceNode)
            {
                FixedMessageNodePatchBuilder::Build(
                    ownerId,
                    logicalId,
                    nodeDef,
                    messageNodeName,
                    logicalToRuntimeNode,
                    resolveButtonTargetNode,
                    nodePatches);
                continue;
            }

            auto choiceNodeIt = logicalToChoiceNode.find(logicalId);
            if (choiceNodeIt == logicalToChoiceNode.end())
            {
                PS::Log<RC::LogLevel::Warning>(
                    STR("Conversation '{}': no choice node could be allocated for logical node '{}'.\n"),
                    RC::to_generic_string(ownerId),
                    RC::to_generic_string(logicalId)
                );
                continue;
            }

            const auto& choiceNodeName = choiceNodeIt->second;
            nodePatches[messageNodeName]["Connections"] = nlohmann::json::array({
                {
                    { "Key", "Out" },
                    { "Value", {
                        { "NodeName", choiceNodeName },
                        { "PinName", "In" }
                    } }
                }
            });

            CustomChoiceNodePatchBuilder::Build(
                logicalId,
                nodeDef,
                choiceNodeName,
                resolveButtonTextId,
                resolveButtonDefaultText,
                [&](const std::string& buttonMsgId, const std::string& defaultText)
                {
                    RegisterTalkTextEntry(
                        registeredTalkTextDefaults,
                        buttonEntries,
                        ownerId,
                        buttonMsgId,
                        defaultText,
                        std::format("button text registration in node '{}'", logicalId));
                },
                resolveButtonTargetNode,
                nodePatches);
        }

        if (hasStartNode && !logicalOrder.empty())
        {
            auto entryIt = logicalToRuntimeNode.find(logicalOrder.front());
            if (entryIt != logicalToRuntimeNode.end())
            {
                const auto& entryMessageNode = entryIt->second;
                nodePatches[startNodes.front()] = {
                    { "Connections", nlohmann::json::array({
                        {
                            { "Key", "Out" },
                            { "Value", {
                                { "NodeName", entryMessageNode },
                                { "PinName", "In" }
                            } }
                        }
                    }) }
                };
            }
            else
            {
                PS::Log<RC::LogLevel::Warning>(
                    STR("Conversation '{}': unable to resolve runtime entry node for '{}'.\n"),
                    RC::to_generic_string(ownerId),
                    RC::to_generic_string(logicalOrder.front())
                );
            }
        }
    }
}

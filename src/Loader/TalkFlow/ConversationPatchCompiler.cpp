#include "Loader/TalkFlow/ConversationPatchCompiler.h"

#include "Loader/TalkFlow/FlowNodeCatalogBuilder.h"
#include "Loader/TalkFlow/Nodes/ConversationNodePatchBuilder.h"
#include "Loader/TalkFlow/Nodes/GetItemNodePatchBuilder.h"
#include "Loader/TalkFlow/Nodes/ShopNodePatchBuilder.h"
#include "Loader/TalkFlow/Nodes/TalkCountBranchNodePatchBuilder.h"
#include "Loader/TalkFlowCloneManager.h"
#include "Utility/Logging.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <vector>

using namespace RC;
using namespace RC::Unreal;

namespace Palworld::TalkFlow
{
    namespace
    {
        int ExtractTrailingNumber(const std::string& text)
        {
            if (text.empty() || !std::isdigit(static_cast<unsigned char>(text.back())))
            {
                return -1;
            }

            auto split = text.size();
            while (split > 0 && std::isdigit(static_cast<unsigned char>(text[split - 1])))
            {
                --split;
            }

            return std::stoi(text.substr(split));
        }

        void SortNodeNamesBySuffix(std::vector<std::string>& names)
        {
            std::sort(names.begin(), names.end(), [](const std::string& lhs, const std::string& rhs)
            {
                const auto lhsNum = ExtractTrailingNumber(lhs);
                const auto rhsNum = ExtractTrailingNumber(rhs);
                if (lhsNum != rhsNum)
                {
                    return lhsNum < rhsNum;
                }

                return lhs < rhs;
            });
        }

        std::string SanitizeToken(std::string value)
        {
            for (auto& c : value)
            {
                if (!std::isalnum(static_cast<unsigned char>(c)))
                {
                    c = '_';
                }
                else
                {
                    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                }
            }

            return value;
        }
    }

    nlohmann::json ConversationPatchCompiler::BuildConversationPatchFromSchema(
        const std::string& ownerId,
        const nlohmann::json& conversationNodes,
        const RC::StringType& assetPath,
        const std::optional<std::string>& preferredStartNode,
        const ConversationPatchCompilerContext& context)
    {
        if (!context.flowNodeClass)
        {
            throw std::runtime_error("Conversation patch compiler requires /Script/Flow.FlowNode class.");
        }

        if (!context.cloneManager)
        {
            throw std::runtime_error("Conversation patch compiler requires a TalkFlowCloneManager.");
        }

        if (!context.findTalkFlowAsset)
        {
            throw std::runtime_error("Conversation patch compiler requires FindTalkFlowAsset callback.");
        }

        auto* flowAsset = context.findTalkFlowAsset(assetPath);
        if (!flowAsset)
        {
            throw std::runtime_error(std::format(
                "Unable to compile conversation schema for '{}' because flow asset '{}' is not loaded.",
                ownerId,
                RC::to_string(assetPath)));
        }

        auto nodeCatalog = TalkFlow::FlowNodeCatalogBuilder::Build(context.flowNodeClass, flowAsset, assetPath);
        auto& nodesByName = nodeCatalog.NodesByName;
        auto& startNodes = nodeCatalog.StartNodes;
        auto& messageNodes = nodeCatalog.MessageNodes;
        auto& choiceNodes = nodeCatalog.ChoiceNodes;
        auto& openItemShopNodes = nodeCatalog.OpenItemShopNodes;
        auto& openItemShopBuyNodes = nodeCatalog.OpenItemShopBuyNodes;
        auto& openItemShopSellNodes = nodeCatalog.OpenItemShopSellNodes;
        auto& openPalShopNodes = nodeCatalog.OpenPalShopNodes;
        auto& openPalShopBuyNodes = nodeCatalog.OpenPalShopBuyNodes;
        auto& openPalShopSellNodes = nodeCatalog.OpenPalShopSellNodes;
        auto& getItemNodes = nodeCatalog.GetItemNodes;
        auto& talkCountBranchNodes = nodeCatalog.TalkCountBranchNodes;

        // Phase 1: measure required node capacity and spawn missing runtime nodes.
        {
            size_t neededMessageNodes = 0;
            size_t neededChoiceNodes = 0;
            size_t neededBuyShopNodes = 0;
            size_t neededSellShopNodes = 0;
            size_t neededBuyPalShopNodes = 0;
            size_t neededSellPalShopNodes = 0;
            size_t neededGetItemNodes = 0;
            size_t neededTalkCountBranchNodes = 0;
            bool willNeedExitSpare = false;

            for (const auto& [logicalId, nodeDef] : conversationNodes.items())
            {
                const auto nodeType = nodeDef.value("Type", std::string{});
                if (nodeType == "ItemShopBuy")
                {
                    ++neededBuyShopNodes;
                    continue;
                }

                if (nodeType == "ItemShopSell")
                {
                    ++neededSellShopNodes;
                    continue;
                }

                if (nodeType == "PalShopBuy")
                {
                    ++neededBuyPalShopNodes;
                    continue;
                }

                if (nodeType == "PalShopSell")
                {
                    ++neededSellPalShopNodes;
                    continue;
                }

                if (nodeType == "GetItem")
                {
                    ++neededGetItemNodes;
                    continue;
                }

                if (nodeType == "TalkCountBranch")
                {
                    ++neededTalkCountBranchNodes;
                    continue;
                }

                ++neededMessageNodes;

                const auto hasButtons = nodeDef.contains("Buttons") && nodeDef.at("Buttons").is_object();
                const auto buttonCount = hasButtons ? nodeDef.at("Buttons").size() : 0;
                bool needsChoice = buttonCount > 1;
                if (!nodeType.empty())
                {
                    if (nodeType == "CustomChoice") needsChoice = true;
                    else if (nodeType == "FixedMessage") needsChoice = false;
                    else throw std::runtime_error(std::format(
                        "Conversation '{}': node '{}' has unknown Type '{}'. Supported: FixedMessage, CustomChoice, ItemShopBuy, ItemShopSell, PalShopBuy, PalShopSell, GetItem, TalkCountBranch.",
                        ownerId, logicalId, nodeType));
                }
                if (needsChoice) ++neededChoiceNodes;
                if (hasButtons)
                {
                    for (const auto& [buttonId, buttonDef] : nodeDef.at("Buttons").items())
                    {
                        const auto explicitExit = buttonDef.contains("Action") && buttonDef.at("Action").is_string() && buttonDef.at("Action").get<std::string>() == "Exit";
                        const auto implicitExit = !buttonDef.contains("LinkID") && !buttonDef.contains("Action");
                        if (explicitExit || implicitExit)
                        {
                            willNeedExitSpare = true;
                        }
                    }
                }
            }

            if (willNeedExitSpare) ++neededMessageNodes;

            int spawnIndex = 0;
            while (messageNodes.size() < neededMessageNodes)
            {
                const auto newName = std::format("FNBP_NPCTalk_FixedMsdId_C_Spawned_{}", spawnIndex++);
                auto* newNode = context.cloneManager->SpawnNodeInClone(flowAsset, "FNBP_NPCTalk_FixedMsdId_C", newName);
                if (!newNode) break;
                const auto nodeNameNarrow = RC::to_string(newNode->GetName());
                messageNodes.push_back(nodeNameNarrow);
                nodesByName.emplace(nodeNameNarrow, newNode);
            }

            spawnIndex = 0;
            while (choiceNodes.size() < neededChoiceNodes)
            {
                const auto newName = std::format("FNBP_NPCTalk_CustomChoice_C_Spawned_{}", spawnIndex++);
                auto* newNode = context.cloneManager->SpawnNodeInClone(flowAsset, "FNBP_NPCTalk_CustomChoice_C", newName);
                if (!newNode) break;
                const auto nodeNameNarrow = RC::to_string(newNode->GetName());
                choiceNodes.push_back(nodeNameNarrow);
                nodesByName.emplace(nodeNameNarrow, newNode);
            }

            spawnIndex = 0;
            while (openItemShopBuyNodes.size() < neededBuyShopNodes)
            {
                const auto newName = std::format("FNBP_OpenItemShop_C_Buy_Spawned_{}", spawnIndex++);
                auto* newNode = context.cloneManager->SpawnNodeInClone(flowAsset, "FNBP_OpenItemShop_C", newName);
                if (!newNode) break;

                const auto nodeNameNarrow = RC::to_string(newNode->GetName());
                openItemShopNodes.push_back(nodeNameNarrow);
                openItemShopBuyNodes.push_back(nodeNameNarrow);
                nodesByName.emplace(nodeNameNarrow, newNode);
            }

            spawnIndex = 0;
            while (openItemShopSellNodes.size() < neededSellShopNodes)
            {
                const auto newName = std::format("FNBP_OpenItemShop_C_Sell_Spawned_{}", spawnIndex++);
                auto* newNode = context.cloneManager->SpawnNodeInClone(flowAsset, "FNBP_OpenItemShop_C", newName);
                if (!newNode) break;

                const auto nodeNameNarrow = RC::to_string(newNode->GetName());
                openItemShopNodes.push_back(nodeNameNarrow);
                openItemShopSellNodes.push_back(nodeNameNarrow);
                nodesByName.emplace(nodeNameNarrow, newNode);
            }

            spawnIndex = 0;
            while (openPalShopBuyNodes.size() < neededBuyPalShopNodes)
            {
                const auto newName = std::format("FNBP_OpenPalShop_C_Buy_Spawned_{}", spawnIndex++);
                auto* newNode = context.cloneManager->SpawnNodeInClone(flowAsset, "FNBP_OpenPalShop_C", newName);
                if (!newNode) break;

                const auto nodeNameNarrow = RC::to_string(newNode->GetName());
                openPalShopNodes.push_back(nodeNameNarrow);
                openPalShopBuyNodes.push_back(nodeNameNarrow);
                nodesByName.emplace(nodeNameNarrow, newNode);
            }

            spawnIndex = 0;
            while (openPalShopSellNodes.size() < neededSellPalShopNodes)
            {
                const auto newName = std::format("FNBP_OpenPalShop_C_Sell_Spawned_{}", spawnIndex++);
                auto* newNode = context.cloneManager->SpawnNodeInClone(flowAsset, "FNBP_OpenPalShop_C", newName);
                if (!newNode) break;

                const auto nodeNameNarrow = RC::to_string(newNode->GetName());
                openPalShopNodes.push_back(nodeNameNarrow);
                openPalShopSellNodes.push_back(nodeNameNarrow);
                nodesByName.emplace(nodeNameNarrow, newNode);
            }

            spawnIndex = 0;
            while (getItemNodes.size() < neededGetItemNodes)
            {
                const auto newName = std::format("FNBP_GetItem_C_Spawned_{}", spawnIndex++);
                auto* newNode = context.cloneManager->SpawnNodeInClone(flowAsset, "FNBP_GetItem_C", newName);
                if (!newNode) break;

                const auto nodeNameNarrow = RC::to_string(newNode->GetName());
                getItemNodes.push_back(nodeNameNarrow);
                nodesByName.emplace(nodeNameNarrow, newNode);
            }

            spawnIndex = 0;
            while (talkCountBranchNodes.size() < neededTalkCountBranchNodes)
            {
                const auto newName = std::format("FNBP_NPCTalkCountBranch_C_Spawned_{}", spawnIndex++);
                auto* newNode = context.cloneManager->SpawnNodeInClone(flowAsset, "FNBP_NPCTalkCountBranch_C", newName);
                if (!newNode) break;

                const auto nodeNameNarrow = RC::to_string(newNode->GetName());
                talkCountBranchNodes.push_back(nodeNameNarrow);
                nodesByName.emplace(nodeNameNarrow, newNode);
            }

            SortNodeNamesBySuffix(openItemShopNodes);
            SortNodeNamesBySuffix(openItemShopBuyNodes);
            SortNodeNamesBySuffix(openItemShopSellNodes);
            SortNodeNamesBySuffix(openPalShopNodes);
            SortNodeNamesBySuffix(openPalShopBuyNodes);
            SortNodeNamesBySuffix(openPalShopSellNodes);
            SortNodeNamesBySuffix(getItemNodes);
            SortNodeNamesBySuffix(talkCountBranchNodes);
        }

        // Phase 2: choose logical entry ordering and map logical nodes to runtime nodes.
        const auto hasStartNode = !startNodes.empty();
        if (!hasStartNode)
        {
            PS::Log<LogLevel::Warning>(
                STR("Conversation schema for '{}' did not find FlowNode_Start in '{}'. Keeping existing start routing.\n"),
                RC::to_generic_string(ownerId),
                assetPath);
        }

        size_t nonShopLogicalNodes = 0;
        for (const auto& [logicalId, nodeDef] : conversationNodes.items())
        {
            const auto nodeType = nodeDef.value("Type", std::string{});
            if (nodeType != "ItemShopBuy" && nodeType != "ItemShopSell" && nodeType != "PalShopBuy" && nodeType != "PalShopSell" && nodeType != "GetItem" && nodeType != "TalkCountBranch")
            {
                ++nonShopLogicalNodes;
            }
        }

        if (nonShopLogicalNodes > messageNodes.size())
        {
            PS::Log<LogLevel::Warning>(
                STR("Conversation '{}': not enough fixed-message nodes in '{}' even after spawn attempt ({} needed, {} available).\n"),
                RC::to_generic_string(ownerId),
                assetPath,
                nonShopLogicalNodes,
                messageNodes.size());
        }

        const auto ownerToken = SanitizeToken(ownerId);

        std::vector<std::string> logicalOrder;
        logicalOrder.reserve(conversationNodes.size());
        for (const auto& [logicalId, nodeDef] : conversationNodes.items())
        {
            logicalOrder.push_back(logicalId);
        }

        auto findNodeIdCaseInsensitive = [&](const std::string& requestedId) -> std::optional<std::string>
        {
            if (conversationNodes.contains(requestedId))
            {
                return requestedId;
            }

            auto loweredRequested = requestedId;
            std::transform(
                loweredRequested.begin(),
                loweredRequested.end(),
                loweredRequested.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

            for (const auto& [logicalId, nodeDef] : conversationNodes.items())
            {
                auto lowered = logicalId;
                std::transform(
                    lowered.begin(),
                    lowered.end(),
                    lowered.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

                if (lowered == loweredRequested)
                {
                    return logicalId;
                }
            }

            return std::nullopt;
        };

        auto chooseEntryNode = [&]() -> std::optional<std::string>
        {
            if (preferredStartNode.has_value())
            {
                auto explicitNode = findNodeIdCaseInsensitive(preferredStartNode.value());
                if (!explicitNode.has_value())
                {
                    throw std::runtime_error(std::format(
                        "Conversation '{}': StartNode '{}' does not exist in Nodes.",
                        ownerId,
                        preferredStartNode.value()));
                }

                return explicitNode;
            }

            return findNodeIdCaseInsensitive("Greeting");
        };

        if (auto entryNode = chooseEntryNode(); entryNode.has_value())
        {
            auto it = std::find(logicalOrder.begin(), logicalOrder.end(), entryNode.value());
            if (it != logicalOrder.end() && it != logicalOrder.begin())
            {
                std::rotate(logicalOrder.begin(), it, it + 1);
            }
        }

        std::unordered_map<std::string, bool> logicalNeedsChoiceNode;
        std::unordered_map<std::string, std::string> logicalNodeType;
        size_t requiredChoiceNodes = 0;
        for (const auto& logicalId : logicalOrder)
        {
            const auto& nodeDef = conversationNodes.at(logicalId);
            const auto nodeType = nodeDef.value("Type", std::string{});
            logicalNodeType[logicalId] = nodeType;

            if (nodeType == "ItemShopBuy" || nodeType == "ItemShopSell" || nodeType == "PalShopBuy" || nodeType == "PalShopSell" || nodeType == "GetItem" || nodeType == "TalkCountBranch")
            {
                logicalNeedsChoiceNode[logicalId] = false;
                continue;
            }

            const auto hasButtons = nodeDef.contains("Buttons") && nodeDef.at("Buttons").is_object();
            const auto buttonCount = hasButtons ? nodeDef.at("Buttons").size() : 0;
            bool needsChoice = buttonCount > 1;
            if (!nodeType.empty())
            {
                needsChoice = (nodeType == "CustomChoice");
            }
            logicalNeedsChoiceNode[logicalId] = needsChoice;
            if (needsChoice)
            {
                ++requiredChoiceNodes;
            }
        }

        if (requiredChoiceNodes > choiceNodes.size())
        {
            PS::Log<LogLevel::Warning>(
                STR("Conversation '{}': not enough choice nodes in '{}' even after spawn attempt ({} needed, {} available).\n"),
                RC::to_generic_string(ownerId),
                assetPath,
                requiredChoiceNodes,
                choiceNodes.size());
        }

        std::unordered_map<std::string, std::string> logicalToRuntimeNode;
        std::unordered_map<std::string, std::string> logicalToMessageNode;
        std::unordered_map<std::string, std::string> logicalToChoiceNode;
        std::vector<std::string> mappedBuyShopNodes;
        std::vector<std::string> mappedSellShopNodes;
        std::vector<std::string> mappedBuyPalShopNodes;
        std::vector<std::string> mappedSellPalShopNodes;
        std::vector<std::pair<std::string, std::string>> mappedGetItemNodes;
        std::vector<std::pair<std::string, std::string>> mappedTalkCountBranchNodes;
        size_t buyShopNodeIndex = 0;
        size_t sellShopNodeIndex = 0;
        size_t buyPalShopNodeIndex = 0;
        size_t sellPalShopNodeIndex = 0;
        size_t getItemNodeIndex = 0;
        size_t talkCountBranchNodeIndex = 0;
        size_t choiceNodeIndex = 0;
        size_t messageNodeIndex = 0;
        for (size_t i = 0; i < logicalOrder.size(); ++i)
        {
            const auto& logicalId = logicalOrder[i];
            const auto& nodeType = logicalNodeType.at(logicalId);

            if (nodeType == "ItemShopBuy")
            {
                if (buyShopNodeIndex < openItemShopBuyNodes.size())
                {
                    const auto& nodeName = openItemShopBuyNodes[buyShopNodeIndex++];
                    logicalToRuntimeNode[logicalId] = nodeName;
                    mappedBuyShopNodes.push_back(nodeName);
                }
                continue;
            }

            if (nodeType == "ItemShopSell")
            {
                if (sellShopNodeIndex < openItemShopSellNodes.size())
                {
                    const auto& nodeName = openItemShopSellNodes[sellShopNodeIndex++];
                    logicalToRuntimeNode[logicalId] = nodeName;
                    mappedSellShopNodes.push_back(nodeName);
                }
                continue;
            }

            if (nodeType == "PalShopBuy")
            {
                if (buyPalShopNodeIndex < openPalShopBuyNodes.size())
                {
                    const auto& nodeName = openPalShopBuyNodes[buyPalShopNodeIndex++];
                    logicalToRuntimeNode[logicalId] = nodeName;
                    mappedBuyPalShopNodes.push_back(nodeName);
                }
                continue;
            }

            if (nodeType == "PalShopSell")
            {
                if (sellPalShopNodeIndex < openPalShopSellNodes.size())
                {
                    const auto& nodeName = openPalShopSellNodes[sellPalShopNodeIndex++];
                    logicalToRuntimeNode[logicalId] = nodeName;
                    mappedSellPalShopNodes.push_back(nodeName);
                }
                continue;
            }

            if (nodeType == "GetItem")
            {
                if (getItemNodeIndex < getItemNodes.size())
                {
                    const auto& nodeName = getItemNodes[getItemNodeIndex++];
                    logicalToRuntimeNode[logicalId] = nodeName;
                    mappedGetItemNodes.emplace_back(logicalId, nodeName);
                }
                continue;
            }

            if (nodeType == "TalkCountBranch")
            {
                if (talkCountBranchNodeIndex < talkCountBranchNodes.size())
                {
                    const auto& nodeName = talkCountBranchNodes[talkCountBranchNodeIndex++];
                    logicalToRuntimeNode[logicalId] = nodeName;
                    mappedTalkCountBranchNodes.emplace_back(logicalId, nodeName);
                }
                continue;
            }

            if (messageNodeIndex >= messageNodes.size())
            {
                continue;
            }

            logicalToMessageNode[logicalId] = messageNodes[messageNodeIndex++];
            logicalToRuntimeNode[logicalId] = logicalToMessageNode[logicalId];
            if (logicalNeedsChoiceNode[logicalId])
            {
                if (choiceNodeIndex < choiceNodes.size())
                {
                    logicalToChoiceNode[logicalId] = choiceNodes[choiceNodeIndex++];
                }
            }
        }

        // Phase 3: emit final patch JSON for runtime patch application.
        auto patch = nlohmann::json::object();
        patch["AssetPath"] = RC::to_string(assetPath);
        patch["Nodes"] = nlohmann::json::object();
        patch["Text"] = nlohmann::json::object();
        patch["Buttons"] = nlohmann::json::object();

        auto& nodePatches = patch["Nodes"];
        auto& textEntries = patch["Text"];
        auto& buttonEntries = patch["Buttons"];
        int requiredFlowMaxTalkCount = 0;
        std::unordered_map<std::string, std::string> registeredTalkTextDefaults;

        TalkFlow::Nodes::ShopNodePatchBuilder::Build(
            mappedBuyShopNodes,
            mappedSellShopNodes,
            mappedBuyPalShopNodes,
            mappedSellPalShopNodes,
            nodePatches);

        TalkFlow::Nodes::GetItemNodePatchBuilder::Build(
            ownerId,
            conversationNodes,
            mappedGetItemNodes,
            logicalToRuntimeNode,
            nodePatches);

        TalkFlow::Nodes::TalkCountBranchNodePatchBuilder::Build(
            ownerId,
            conversationNodes,
            mappedTalkCountBranchNodes,
            logicalToRuntimeNode,
            nodePatches,
            requiredFlowMaxTalkCount);

        if (requiredFlowMaxTalkCount > 0)
        {
            patch["AssetProperties"] = {
                { "MaxTalkCount", requiredFlowMaxTalkCount }
            };
        }

        TalkFlow::Nodes::ConversationNodePatchBuilder::Build(
            ownerId,
            ownerToken,
            assetPath,
            conversationNodes,
            logicalOrder,
            logicalNodeType,
            logicalNeedsChoiceNode,
            logicalToRuntimeNode,
            logicalToMessageNode,
            logicalToChoiceNode,
            messageNodes,
            startNodes,
            hasStartNode,
            registeredTalkTextDefaults,
            nodePatches,
            textEntries,
            buttonEntries);

        return patch;
    }
}

#include "Loader/TalkFlow/FlowNodeCatalogBuilder.h"
#include "Loader/TalkFlow/FlowNodeResolver.h"

#include "Unreal/CoreUObject/UObject/Class.hpp"
#include "Helpers/String.hpp"
#include "SDK/Helper/PropertyHelper.h"

#include <algorithm>
#include <cctype>

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
    }

    FlowNodeCatalog FlowNodeCatalogBuilder::Build(UClass* flowNodeClass, UObject* flowAsset, const RC::StringType& assetPath)
    {
        FlowNodeCatalog catalog{};
        if (!flowNodeClass || !flowAsset)
        {
            return catalog;
        }

        auto nodeLookup = FlowNodeResolver::BuildNodeLookup(flowNodeClass, flowAsset, assetPath);

        auto classifyOpenShopNode = [&](UObject* nodeObject, const std::string& nodeName)
        {
            catalog.OpenItemShopNodes.push_back(nodeName);

            int32_t tabValue = 0;
            bool hasTabValue = false;

            if (auto* tabPtr8 = Palworld::PropertyHelper::GetValuePtrByPropertyNameInChain<uint8_t>(nodeObject, STR("OpenItemShopTabType")))
            {
                tabValue = static_cast<int32_t>(*tabPtr8);
                hasTabValue = true;
            }
            else if (auto* tabPtr32 = Palworld::PropertyHelper::GetValuePtrByPropertyNameInChain<int32_t>(nodeObject, STR("OpenItemShopTabType")))
            {
                tabValue = *tabPtr32;
                hasTabValue = true;
            }

            if (hasTabValue && tabValue == 1)
            {
                catalog.OpenItemShopSellNodes.push_back(nodeName);
            }
            else
            {
                catalog.OpenItemShopBuyNodes.push_back(nodeName);
            }
        };

        auto classifyOpenPalShopNode = [&](UObject* nodeObject, const std::string& nodeName)
        {
            catalog.OpenPalShopNodes.push_back(nodeName);

            int32_t tabValue = 0;
            bool hasTabValue = false;

            if (auto* tabPtr8 = Palworld::PropertyHelper::GetValuePtrByPropertyNameInChain<uint8_t>(nodeObject, STR("OpenPalShopTabType")))
            {
                tabValue = static_cast<int32_t>(*tabPtr8);
                hasTabValue = true;
            }
            else if (auto* tabPtr32 = Palworld::PropertyHelper::GetValuePtrByPropertyNameInChain<int32_t>(nodeObject, STR("OpenPalShopTabType")))
            {
                tabValue = *tabPtr32;
                hasTabValue = true;
            }

            if (hasTabValue && tabValue == 1)
            {
                catalog.OpenPalShopSellNodes.push_back(nodeName);
            }
            else
            {
                catalog.OpenPalShopBuyNodes.push_back(nodeName);
            }
        };

        auto classifyNode = [&](UObject* nodeObject)
        {
            const auto nodeName = RC::to_string(nodeObject->GetName());
            catalog.NodesByName.emplace(nodeName, nodeObject);

            const auto className = nodeObject->GetClassPrivate() ? RC::to_string(nodeObject->GetClassPrivate()->GetName()) : std::string{};
            const auto hasMsgIdList = nodeObject->GetPropertyByNameInChain(STR("MsgIdList")) != nullptr;
            const auto hasChoiceMsgIdList = nodeObject->GetPropertyByNameInChain(STR("ChoiceMsgIDList")) != nullptr;
            const auto hasOpenItemShopTabType = nodeObject->GetPropertyByNameInChain(STR("OpenItemShopTabType")) != nullptr;
            const auto hasOpenPalShopTabType = nodeObject->GetPropertyByNameInChain(STR("OpenPalShopTabType")) != nullptr;
            const auto hasGetItemList = nodeObject->GetPropertyByNameInChain(STR("GetItemList")) != nullptr;
            const auto hasLotteryDataTable = nodeObject->GetPropertyByNameInChain(STR("LotteryDataTable")) != nullptr;
            const auto hasOutputPins = nodeObject->GetPropertyByNameInChain(STR("OutputPins")) != nullptr;

            if (className.contains("FlowNode_Start") || nodeName.starts_with("FlowNode_Start"))
            {
                catalog.StartNodes.push_back(nodeName);
            }
            else if (className.contains("FNBP_NPCTalk_FixedMsdId_C") || hasMsgIdList)
            {
                catalog.MessageNodes.push_back(nodeName);
            }
            else if (className.contains("FNBP_NPCTalk_CustomChoice_C") || hasChoiceMsgIdList)
            {
                catalog.ChoiceNodes.push_back(nodeName);
            }
            else if (className.contains("FNBP_OpenItemShop_C") || hasOpenItemShopTabType)
            {
                classifyOpenShopNode(nodeObject, nodeName);
            }
            else if (className.contains("FNBP_OpenPalShop_C") || hasOpenPalShopTabType)
            {
                classifyOpenPalShopNode(nodeObject, nodeName);
            }
            else if (className.contains("FNBP_GetItem_C") || hasGetItemList || hasLotteryDataTable)
            {
                catalog.GetItemNodes.push_back(nodeName);
            }
            else if (className.contains("FNBP_NPCTalkCountBranch_C") || hasOutputPins)
            {
                catalog.TalkCountBranchNodes.push_back(nodeName);
            }
        };

        for (const auto& entry : nodeLookup.nodesByName)
        {
            classifyNode(entry.second);
        }

        SortNodeNamesBySuffix(catalog.StartNodes);
        SortNodeNamesBySuffix(catalog.MessageNodes);
        SortNodeNamesBySuffix(catalog.ChoiceNodes);
        SortNodeNamesBySuffix(catalog.OpenItemShopNodes);
        SortNodeNamesBySuffix(catalog.OpenItemShopBuyNodes);
        SortNodeNamesBySuffix(catalog.OpenItemShopSellNodes);
        SortNodeNamesBySuffix(catalog.OpenPalShopNodes);
        SortNodeNamesBySuffix(catalog.OpenPalShopBuyNodes);
        SortNodeNamesBySuffix(catalog.OpenPalShopSellNodes);
        SortNodeNamesBySuffix(catalog.GetItemNodes);
        SortNodeNamesBySuffix(catalog.TalkCountBranchNodes);

        return catalog;
    }
}

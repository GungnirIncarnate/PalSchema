#include "Loader/TalkFlow/FlowPatchApplier.h"

#include "Loader/TalkFlow/FlowNodeResolver.h"
#include "Loader/TalkFlow/TalkFlowAssetResolver.h"
#include "Unreal/UObject.hpp"
#include "SDK/Helper/PropertyHelper.h"
#include "Utility/Logging.h"

#include <cstdint>
#include <format>
#include <stdexcept>
#include <unordered_map>

using namespace RC;
using namespace RC::Unreal;

namespace Palworld::TalkFlow
{
    namespace
    {
        constexpr const TCHAR* kVanillaAssetPrefix = STR("/Game/Pal/");

        nlohmann::json BuildGuidJsonFromNode(UObject* nodeObject)
        {
            struct GuidMemory
            {
                uint32_t A;
                uint32_t B;
                uint32_t C;
                uint32_t D;
            };

            auto* guid = Palworld::PropertyHelper::GetValuePtrByPropertyNameInChain<GuidMemory>(nodeObject, STR("NodeGuid"));
            if (!guid)
            {
                throw std::runtime_error(std::format("Node '{}' does not expose NodeGuid.", RC::to_string(nodeObject->GetName())));
            }

            auto guidJson = nlohmann::json::object();
            guidJson["A"] = static_cast<uint64_t>(guid->A);
            guidJson["B"] = static_cast<uint64_t>(guid->B);
            guidJson["C"] = static_cast<uint64_t>(guid->C);
            guidJson["D"] = static_cast<uint64_t>(guid->D);
            return guidJson;
        }

        void ResolveConnectionNodeNames(
            nlohmann::json& nodePatch,
            const std::unordered_map<std::string, UObject*>& nodesByName)
        {
            if (!nodePatch.is_object() || !nodePatch.contains("Connections") || !nodePatch.at("Connections").is_array())
            {
                return;
            }

            for (auto& connectionEntry : nodePatch.at("Connections"))
            {
                if (!connectionEntry.is_object() || !connectionEntry.contains("Value") || !connectionEntry.at("Value").is_object())
                {
                    continue;
                }

                auto& value = connectionEntry["Value"];
                if (!value.contains("NodeName") || !value.at("NodeName").is_string())
                {
                    continue;
                }

                const auto nodeName = value.at("NodeName").get<std::string>();
                auto nodeIt = nodesByName.find(nodeName);
                if (nodeIt == nodesByName.end())
                {
                    throw std::runtime_error(std::format(
                        "Connection target NodeName '{}' was not found in flow asset.",
                        nodeName));
                }

                value["NodeGuid"] = BuildGuidJsonFromNode(nodeIt->second);
                value.erase("NodeName");
            }
        }
    }

    bool FlowPatchApplier::ApplySingleFlowPatch(
        const nlohmann::json& patch,
        bool skipVanillaGuard,
        const FlowPatchApplierContext& context)
    {
        if (!patch.is_object())
        {
            throw std::runtime_error("Each FlowPatches entry must be an object.");
        }

        if (!patch.contains("AssetPath") || !patch.at("AssetPath").is_string())
        {
            throw std::runtime_error("Each FlowPatches entry requires string field AssetPath.");
        }

        if (!patch.contains("Nodes") || !patch.at("Nodes").is_object())
        {
            throw std::runtime_error("Each FlowPatches entry requires object field Nodes.");
        }

        if (patch.contains("AssetProperties") && !patch.at("AssetProperties").is_object())
        {
            throw std::runtime_error("FlowPatches.AssetProperties must be an object when provided.");
        }

        if (!context.applyNodePatch)
        {
            throw std::runtime_error("Flow patch applier requires ApplyNodePatch callback.");
        }

        if (!context.addOrEditTalkText)
        {
            throw std::runtime_error("Flow patch applier requires AddOrEditTalkText callback.");
        }

        auto assetPath = RC::to_generic_string(patch.at("AssetPath").get<std::string>());

        bool allowGlobalPatch = false;
        if (patch.contains("AllowGlobalPatch"))
        {
            if (!patch.at("AllowGlobalPatch").is_boolean())
            {
                throw std::runtime_error("FlowPatches.AllowGlobalPatch must be a boolean.");
            }

            allowGlobalPatch = patch.at("AllowGlobalPatch").get<bool>();
        }

        if (!skipVanillaGuard && assetPath.rfind(kVanillaAssetPrefix, 0) == 0 && !allowGlobalPatch)
        {
            throw std::runtime_error(
                std::format(
                    "Refusing to patch vanilla flow asset '{}' without explicit opt-in. "
                    "Set AllowGlobalPatch=true only if you intentionally want to affect all NPCs using that asset.",
                    RC::to_string(assetPath)));
        }

        auto* flowAsset = TalkFlowAssetResolver::FindTalkFlowAsset(assetPath, context.flowNodeClass, context.npcTalkFlowTable);
        if (!flowAsset)
        {
            return false;
        }

        if (patch.contains("AssetProperties"))
        {
            for (auto& [propertyName, propertyValue] : patch.at("AssetProperties").items())
            {
                auto propertyNameWide = RC::to_generic_string(propertyName);
                auto* property = flowAsset->GetPropertyByNameInChain(propertyNameWide.c_str());
                if (!property)
                {
                    property = Palworld::PropertyHelper::GetPropertyByName(flowAsset->GetClassPrivate(), propertyNameWide);
                }

                if (!property)
                {
                    PS::Log<LogLevel::Warning>(STR("Flow patch skipped unknown asset property {} on {}\n"), propertyNameWide, flowAsset->GetName());
                    continue;
                }

                try
                {
                    Palworld::PropertyHelper::CopyJsonValueToContainer(flowAsset, property, propertyValue);
                }
                catch (const std::exception& e)
                {
                    PS::Log<LogLevel::Warning>(
                        STR("Flow patch skipped invalid asset property {} on {}: {}\n"),
                        propertyNameWide,
                        flowAsset->GetName(),
                        RC::to_generic_string(e.what()));
                }
            }
        }

        auto nodeLookup = FlowNodeResolver::BuildNodeLookup(context.flowNodeClass, flowAsset, assetPath);
        auto& nodesByName = nodeLookup.nodesByName;

        if (!nodeLookup.fallbackOuterName.empty())
        {
            PS::Log<LogLevel::Warning>(
                STR("Flow patch fallback: '{}' has no owned nodes; patching source graph nodes from '{}'.\n"),
                assetPath,
            RC::to_generic_string(nodeLookup.fallbackOuterName));
        }

        for (auto& [nodeName, nodePatch] : patch.at("Nodes").items())
        {
            auto resolvedNodePatch = nodePatch;
            ResolveConnectionNodeNames(resolvedNodePatch, nodesByName);

            std::string matchedBy;
            size_t candidateCount = 0;
            auto* selectedNode = FlowNodeResolver::ResolveNodeByPatchName(nodeName, nodeLookup, &matchedBy, &candidateCount);
            if (selectedNode)
            {
                if (matchedBy == "normalized-name" && candidateCount > 1)
                {
                    PS::Log<LogLevel::Warning>(
                        STR("Flow patch node {} matched {} runtime nodes in asset {} by normalized name. Applying to {}.\n"),
                        RC::to_generic_string(nodeName),
                        static_cast<int>(candidateCount),
                        assetPath,
                        selectedNode->GetName());
                }
                else if (matchedBy == "normalized-class" && candidateCount > 1)
                {
                    PS::Log<LogLevel::Warning>(
                        STR("Flow patch node {} matched {} runtime nodes in asset {} by normalized class name. Applying to {}.\n"),
                        RC::to_generic_string(nodeName),
                        static_cast<int>(candidateCount),
                        assetPath,
                        selectedNode->GetName());
                }

                context.applyNodePatch(selectedNode, resolvedNodePatch);
                continue;
            }

            PS::Log<LogLevel::Warning>(STR("Flow patch skipped node {} in asset {} (node not found by name).\n"), RC::to_generic_string(nodeName), assetPath);
        }

        if (patch.contains("Text"))
        {
            if (!patch.at("Text").is_object())
            {
                throw std::runtime_error("FlowPatches.Text must be an object mapping MsgId -> text.");
            }

            context.addOrEditTalkText(patch.at("Text"));
        }

        if (patch.contains("Buttons"))
        {
            if (!patch.at("Buttons").is_object())
            {
                throw std::runtime_error("FlowPatches.Buttons must be an object mapping MsgId -> text.");
            }

            context.addOrEditTalkText(patch.at("Buttons"));
        }

        return true;
    }
}

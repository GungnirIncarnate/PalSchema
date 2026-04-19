#include "Loader/TalkFlow/FlowNodeResolver.h"

#include "Unreal/CoreUObject/UObject/Class.hpp"
#include "Helpers/String.hpp"
#include "SDK/Classes/Custom/UObjectGlobals.h"

#include <cctype>

using namespace RC;
using namespace RC::Unreal;

namespace Palworld::TalkFlow
{
    FlowNodeLookup FlowNodeResolver::BuildNodeLookup(
        UClass* flowNodeClass,
        UObject* flowAsset,
        const RC::StringType& assetPath)
    {
        FlowNodeLookup lookup{};
        if (!flowNodeClass || !flowAsset)
        {
            return lookup;
        }

        TArray<UObject*> flowNodeObjects;
        UECustom::UObjectGlobals::GetObjectsOfClass(flowNodeClass, flowNodeObjects, true, EObjectFlags::RF_ClassDefaultObject, EInternalObjectFlags::None);

        auto collectNodesForOuter = [&](UObject* targetOuter)
        {
            for (auto* nodeObject : flowNodeObjects)
            {
                if (!nodeObject || nodeObject->GetOuterPrivate() != targetOuter)
                {
                    continue;
                }

                auto nodeName = nodeObject->GetName();
                auto nodeNameNarrow = RC::to_string(nodeName);
                lookup.nodesByName.emplace(nodeNameNarrow, nodeObject);

                auto normalizedNodeName = NormalizeNodeName(nodeName);
                lookup.nodesByNormalizedName[RC::to_string(normalizedNodeName)].push_back(nodeObject);

                auto className = nodeObject->GetClassPrivate() ? nodeObject->GetClassPrivate()->GetName() : RC::StringType{};
                if (!className.empty())
                {
                    auto normalizedClassName = NormalizeNodeName(className);
                    lookup.nodesByNormalizedClassName[RC::to_string(normalizedClassName)].push_back(nodeObject);
                }
            }
        };

        collectNodesForOuter(flowAsset);
        if (!lookup.nodesByName.empty())
        {
            return lookup;
        }

        const auto sourceOuterName = ExtractPackageAssetName(assetPath);
        for (auto* nodeObject : flowNodeObjects)
        {
            if (!nodeObject)
            {
                continue;
            }

            auto* outer = nodeObject->GetOuterPrivate();
            if (!outer)
            {
                continue;
            }

            if (RC::to_string(outer->GetName()) != sourceOuterName)
            {
                continue;
            }

            auto nodeName = nodeObject->GetName();
            auto nodeNameNarrow = RC::to_string(nodeName);
            lookup.nodesByName.emplace(nodeNameNarrow, nodeObject);

            auto normalizedNodeName = NormalizeNodeName(nodeName);
            lookup.nodesByNormalizedName[RC::to_string(normalizedNodeName)].push_back(nodeObject);

            auto className = nodeObject->GetClassPrivate() ? nodeObject->GetClassPrivate()->GetName() : RC::StringType{};
            if (!className.empty())
            {
                auto normalizedClassName = NormalizeNodeName(className);
                lookup.nodesByNormalizedClassName[RC::to_string(normalizedClassName)].push_back(nodeObject);
            }
        }

        if (!lookup.nodesByName.empty())
        {
            lookup.fallbackOuterName = sourceOuterName;
        }

        return lookup;
    }

    UObject* FlowNodeResolver::ResolveNodeByPatchName(
        const std::string& patchNodeName,
        const FlowNodeLookup& lookup,
        std::string* matchedBy,
        size_t* candidateCount)
    {
        if (matchedBy)
        {
            *matchedBy = "none";
        }

        if (candidateCount)
        {
            *candidateCount = 0;
        }

        auto exactIt = lookup.nodesByName.find(patchNodeName);
        if (exactIt != lookup.nodesByName.end())
        {
            if (matchedBy)
            {
                *matchedBy = "exact";
            }

            if (candidateCount)
            {
                *candidateCount = 1;
            }

            return exactIt->second;
        }

        auto normalizedPatchNodeName = RC::to_string(NormalizeNodeName(RC::to_generic_string(patchNodeName)));
        auto normalizedIt = lookup.nodesByNormalizedName.find(normalizedPatchNodeName);
        if (normalizedIt != lookup.nodesByNormalizedName.end() && !normalizedIt->second.empty())
        {
            if (matchedBy)
            {
                *matchedBy = "normalized-name";
            }

            if (candidateCount)
            {
                *candidateCount = normalizedIt->second.size();
            }

            return normalizedIt->second.front();
        }

        auto normalizedClassIt = lookup.nodesByNormalizedClassName.find(normalizedPatchNodeName);
        if (normalizedClassIt != lookup.nodesByNormalizedClassName.end() && !normalizedClassIt->second.empty())
        {
            if (matchedBy)
            {
                *matchedBy = "normalized-class";
            }

            if (candidateCount)
            {
                *candidateCount = normalizedClassIt->second.size();
            }

            return normalizedClassIt->second.front();
        }

        return nullptr;
    }

    RC::StringType FlowNodeResolver::NormalizeNodeName(const RC::StringType& nodeName)
    {
        auto normalized = nodeName;
        auto endsWithDigits = !normalized.empty() && std::isdigit(static_cast<unsigned char>(normalized.back()));
        if (!endsWithDigits)
        {
            return normalized;
        }

        auto trimPos = normalized.size();
        while (trimPos > 0 && std::isdigit(static_cast<unsigned char>(normalized[trimPos - 1])))
        {
            --trimPos;
        }

        if (trimPos > 0 && normalized[trimPos - 1] == '_')
        {
            normalized.resize(trimPos - 1);
        }

        return normalized;
    }

    std::string FlowNodeResolver::ExtractPackageAssetName(const RC::StringType& assetPath)
    {
        auto packagePart = assetPath;
        const auto dot = packagePart.find_last_of('.');
        if (dot != RC::StringType::npos)
        {
            packagePart = packagePart.substr(0, dot);
        }

        const auto slash = packagePart.find_last_of('/');
        if (slash != RC::StringType::npos && slash + 1 < packagePart.size())
        {
            packagePart = packagePart.substr(slash + 1);
        }

        return RC::to_string(packagePart);
    }
}

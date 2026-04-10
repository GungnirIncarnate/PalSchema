#pragma once

#include "String/StringType.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace RC::Unreal
{
    class UClass;
    class UObject;
}

namespace Palworld::TalkFlow
{
    struct FlowNodeLookup
    {
        std::unordered_map<std::string, RC::Unreal::UObject*> nodesByName;
        std::unordered_map<std::string, std::vector<RC::Unreal::UObject*>> nodesByNormalizedName;
        std::unordered_map<std::string, std::vector<RC::Unreal::UObject*>> nodesByNormalizedClassName;
        std::string fallbackOuterName;
    };

    class FlowNodeResolver
    {
    public:
        static FlowNodeLookup BuildNodeLookup(
            RC::Unreal::UClass* flowNodeClass,
            RC::Unreal::UObject* flowAsset,
            const RC::StringType& assetPath);

        static RC::Unreal::UObject* ResolveNodeByPatchName(
            const std::string& patchNodeName,
            const FlowNodeLookup& lookup,
            std::string* matchedBy,
            size_t* candidateCount);

    private:
        static RC::StringType NormalizeNodeName(const RC::StringType& nodeName);
        static std::string ExtractPackageAssetName(const RC::StringType& assetPath);
    };
}

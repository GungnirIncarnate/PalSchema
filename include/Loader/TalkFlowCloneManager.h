#pragma once

#include "Unreal/CoreUObject/UObject/Class.hpp"
#include "Unreal/UObject.hpp"
#include <unordered_map>
#include <unordered_set>

namespace Palworld {
    struct TalkFlowCloneRequest {
        RC::StringType CharacterId;
        RC::StringType SourceAssetPath;
        bool ForceRebuild = false;
    };

    class TalkFlowCloneManager {
    public:
        void Initialize(RC::Unreal::UClass* FlowNodeClass);

        RC::StringType ResolveTalkFlowAssetPath(const TalkFlowCloneRequest& Request);

        // Spawns a new node of the given Blueprint class name inside CloneAsset, assigns it a
        // fresh random GUID, and registers it in the asset's Nodes FMap. Returns the new node
        // or nullptr if the class or construction failed.
        RC::Unreal::UObject* SpawnNodeInClone(
            RC::Unreal::UObject* CloneAsset,
            const std::string& DesiredClassName,
            const std::string& DesiredNodeName);
    private:
        std::string BuildCloneKey(const TalkFlowCloneRequest& Request) const;

        RC::Unreal::UObject* FindAssetByPath(const RC::StringType& AssetPath) const;

        RC::Unreal::UObject* CreateCloneObject(RC::Unreal::UObject* SourceAsset, const TalkFlowCloneRequest& Request);

        RC::StringType BuildCloneObjectName(const TalkFlowCloneRequest& Request) const;

        RC::Unreal::UObject* FindTransientOuter() const;

        void CloneFlowNodesIntoAsset(RC::Unreal::UObject* SourceAsset, RC::Unreal::UObject* ClonedAsset);

        void RemapFlowNodeMapToClones(
            RC::Unreal::UObject* SourceAsset,
            RC::Unreal::UObject* ClonedAsset,
            const std::unordered_map<std::string, RC::Unreal::UObject*>& ClonedNodesByGuidKey,
            const std::unordered_map<RC::Unreal::UObject*, RC::Unreal::UObject*>& ClonedNodesBySourceNode);

        bool IsValidResolvedAssetPath(const RC::StringType& AssetPath) const;

        RC::Unreal::UClass* m_flowNodeClass = nullptr;
        std::unordered_map<std::string, RC::StringType> m_cachedResolvedPaths;
        std::unordered_map<std::string, RC::Unreal::UObject*> m_cachedCloneObjects;
        std::unordered_set<std::string> m_warnedCloneKeys;
    };
}
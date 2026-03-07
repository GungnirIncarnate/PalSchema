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
    private:
        std::string BuildCloneKey(const TalkFlowCloneRequest& Request) const;

        RC::StringType BuildCloneObjectName(const TalkFlowCloneRequest& Request) const;

        bool IsValidResolvedAssetPath(const RC::StringType& AssetPath) const;

        RC::Unreal::UObject* FindTransientOuter() const;

        RC::Unreal::UObject* FindAssetByPath(const RC::StringType& AssetPath) const;

        RC::Unreal::UObject* CreateCloneObject(RC::Unreal::UObject* SourceAsset, const TalkFlowCloneRequest& Request);

        RC::Unreal::UClass* m_flowNodeClass = nullptr;
        std::unordered_map<std::string, RC::StringType> m_cachedResolvedPaths;
        std::unordered_map<std::string, RC::Unreal::UObject*> m_cachedCloneObjects;
        std::unordered_set<std::string> m_warnedCloneKeys;
    };
}
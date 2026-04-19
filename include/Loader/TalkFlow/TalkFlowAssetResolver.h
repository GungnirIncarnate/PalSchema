#pragma once

#include "String/StringType.hpp"
#include <vector>

namespace RC::Unreal
{
    class UClass;
    class UDataTable;
    class UObject;
}

namespace Palworld::TalkFlow
{
    class TalkFlowAssetResolver
    {
    public:
        static RC::Unreal::UObject* FindTalkFlowAsset(
            const RC::StringType& assetPath,
            RC::Unreal::UClass* flowNodeClass,
            RC::Unreal::UDataTable* npcTalkFlowTable);

        static RC::StringType CanonicalizeTalkFlowPath(
            const RC::StringType& talkFlowPath,
            RC::Unreal::UClass* flowNodeClass,
            RC::Unreal::UDataTable* npcTalkFlowTable);

    private:
        static void AddTalkFlowPathCandidates(
            const RC::StringType& talkFlowPath,
            std::vector<RC::StringType>& candidates);

        static void AddTalkFlowCandidatesFromDataTable(
            const RC::StringType& talkFlowPath,
            RC::Unreal::UDataTable* npcTalkFlowTable,
            std::vector<RC::StringType>& candidates);
    };
}

#pragma once

#include "Loader/PalModLoaderBase.h"
#include "Loader/TalkFlowCloneManager.h"
#include "nlohmann/json.hpp"
#include <optional>
#include <vector>

namespace RC::Unreal {
    class UObject;
    class UClass;
    class UDataTable;
}

namespace Palworld {
    class PalTalkFlowModLoader : public PalModLoaderBase {
    public:
        PalTalkFlowModLoader();

        ~PalTalkFlowModLoader();

        void Initialize();

        virtual void Load(const nlohmann::json& Data) override final;
    private:
        struct PendingCloneAssignment {
            RC::StringType CharacterId;
            RC::StringType SourceAssetPath;
            bool ForceRebuild = false;
        };

        RC::StringType AssignTalkFlowToNpcWithCloneOption(const RC::StringType& CharacterIdString, const RC::StringType& TalkFlowPath, bool UseClone, bool ForceRebuildClone = false);

        void AssignTalkFlowToNpc(const RC::StringType& CharacterIdString, const RC::StringType& TalkFlowPath);

        bool IsConversationNodeSchema(const nlohmann::json& Nodes) const;

        nlohmann::json BuildConversationPatchFromSchema(const std::string& OwnerId, const nlohmann::json& ConversationNodes, const RC::StringType& AssetPath, const std::optional<std::string>& PreferredStartNode);

        void ApplyFlowPatches(const nlohmann::json& FlowPatches);

        bool ApplySingleFlowPatch(const nlohmann::json& Patch, bool SkipVanillaGuard = false);

        void ProcessPending();

        void QueueFlowPatchRetry(const nlohmann::json& Patch, bool SkipVanillaGuard);

        RC::StringType CanonicalizeTalkFlowPath(const RC::StringType& TalkFlowPath) const;

        void AddTalkFlowPathCandidates(const RC::StringType& TalkFlowPath, std::vector<RC::StringType>& Candidates) const;

        void AddTalkFlowCandidatesFromDataTable(const RC::StringType& TalkFlowPath, std::vector<RC::StringType>& Candidates) const;

        RC::Unreal::UObject* FindTalkFlowAsset(const RC::StringType& AssetPath) const;

        void ApplyNodePatch(RC::Unreal::UObject* NodeObject, const nlohmann::json& NodePatch);

        void AddOrEditTalkText(const nlohmann::json& TextEntries);

        RC::Unreal::UClass* m_flowNodeClass = nullptr;
        RC::Unreal::UDataTable* m_npcTalkFlowTable = nullptr;
        RC::Unreal::UDataTable* m_humanParamTable = nullptr;
        RC::Unreal::UDataTable* m_npcTalkTextTable = nullptr;
        TalkFlowCloneManager m_talkFlowCloneManager;
        std::vector<nlohmann::json> m_pendingFlowPatches;
        std::vector<PendingCloneAssignment> m_pendingCloneAssignments;
        bool m_isProcessingPending = false;
        bool m_processPendingRequested = false;
    };
}
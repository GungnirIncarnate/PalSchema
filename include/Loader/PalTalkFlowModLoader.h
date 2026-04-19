#pragma once

#include "Loader/PalModLoaderBase.h"
#include "Loader/TalkFlowCloneManager.h"
#include "Loader/TalkFlow/TalkFlowPendingWorkProcessor.h"
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
    protected:
        virtual std::filesystem::path ResolveLoaderPath(const std::filesystem::path& modPath) const override final;
        virtual void OnLoad(const std::filesystem::path& loaderPath, const RC::StringType& modName, const EEngineLifecyclePhase& engineLifecyclePhase) override final;

        virtual bool CanInitialize(const EEngineLifecyclePhase& engineLifecyclePhase) override final;

        virtual bool OnInitialize() override final;
    private:
        using PendingCloneAssignment = TalkFlow::PendingCloneAssignment;

        RC::StringType ResolveAndAssignClonedTalkFlow(const RC::StringType& CharacterIdString, const RC::StringType& TalkFlowPath, bool ForceRebuildClone = false);

        void AssignTalkFlowToNpc(const RC::StringType& CharacterIdString, const RC::StringType& TalkFlowPath);

        void AddOrEditTalkText(const nlohmann::json& TextEntries);

        bool IsConversationNodeSchema(const nlohmann::json& Nodes) const;

        nlohmann::json BuildConversationPatchFromSchema(const std::string& OwnerId, const nlohmann::json& ConversationNodes, const RC::StringType& AssetPath, const std::optional<std::string>& PreferredStartNode);

        void ApplyFlowPatches(const nlohmann::json& FlowPatches);

        bool ApplySingleFlowPatch(const nlohmann::json& Patch, bool SkipVanillaGuard = false);

        void QueueFlowPatchRetry(const nlohmann::json& Patch, bool SkipVanillaGuard);

        void ProcessPending();

        void ApplyNodePatch(RC::Unreal::UObject* NodeObject, const nlohmann::json& NodePatch);

        RC::Unreal::UClass* m_flowNodeClass = nullptr;
        RC::Unreal::UDataTable* m_npcTalkFlowTable = nullptr;
        RC::Unreal::UDataTable* m_humanParamTable = nullptr;
        RC::Unreal::UDataTable* m_npcTalkTextTable = nullptr;
        TalkFlowCloneManager m_talkFlowCloneManager;
        std::vector<nlohmann::json> m_pendingFlowPatches;
        std::vector<PendingCloneAssignment> m_pendingCloneAssignments;
        bool m_isProcessingPending = false;
        bool m_processPendingRequested = false;

        void LoadData(const nlohmann::json& Data);
    };
}
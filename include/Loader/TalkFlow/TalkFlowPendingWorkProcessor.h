#pragma once

#include "String/StringType.hpp"
#include "nlohmann/json.hpp"

#include <functional>
#include <vector>

namespace Palworld::TalkFlow
{
    struct PendingCloneAssignment
    {
        RC::StringType characterId;
        RC::StringType sourceAssetPath;
        bool forceRebuild = false;
    };

    struct TalkFlowPendingWorkProcessorContext
    {
        std::function<RC::StringType(const PendingCloneAssignment&)> resolveCloneAssignment;
        std::function<void(const RC::StringType&, const RC::StringType&)> assignTalkFlowToNpc;
        std::function<bool(const nlohmann::json&, bool)> applySingleFlowPatch;
    };

    class TalkFlowPendingWorkProcessor
    {
    public:
        static void ProcessPending(
            std::vector<PendingCloneAssignment>& pendingCloneAssignments,
            std::vector<nlohmann::json>& pendingFlowPatches,
            bool& isProcessingPending,
            bool& processPendingRequested,
            const TalkFlowPendingWorkProcessorContext& context);

        static void QueueFlowPatchRetry(
            std::vector<nlohmann::json>& pendingFlowPatches,
            const nlohmann::json& patch,
            bool skipVanillaGuard);
    };
}

#include "Loader/TalkFlow/TalkFlowPendingWorkProcessor.h"

#include "Helpers/String.hpp"
#include "Utility/Logging.h"

#include <algorithm>
#include <utility>

using namespace RC;

namespace Palworld::TalkFlow
{
    void TalkFlowPendingWorkProcessor::ProcessPending(
        std::vector<PendingCloneAssignment>& pendingCloneAssignments,
        std::vector<nlohmann::json>& pendingFlowPatches,
        bool& isProcessingPending,
        bool& processPendingRequested,
        const TalkFlowPendingWorkProcessorContext& context)
    {
        if (isProcessingPending)
        {
            processPendingRequested = true;
            return;
        }

        if (!context.resolveCloneAssignment || !context.assignTalkFlowToNpc || !context.applySingleFlowPatch)
        {
            return;
        }

        isProcessingPending = true;
        do
        {
            processPendingRequested = false;

            if (!pendingCloneAssignments.empty())
            {
                auto cloneIt = pendingCloneAssignments.begin();
                while (cloneIt != pendingCloneAssignments.end())
                {
                    auto resolvedTalkFlowPath = context.resolveCloneAssignment(*cloneIt);
                    if (resolvedTalkFlowPath != cloneIt->sourceAssetPath)
                    {
                        context.assignTalkFlowToNpc(cloneIt->characterId, resolvedTalkFlowPath);
                        cloneIt = pendingCloneAssignments.erase(cloneIt);
                        continue;
                    }

                    ++cloneIt;
                }
            }

            if (!pendingFlowPatches.empty())
            {
                auto patchIt = pendingFlowPatches.begin();
                while (patchIt != pendingFlowPatches.end())
                {
                    if (!patchIt->is_object() || !patchIt->contains("Patch") || !patchIt->at("Patch").is_object() || !patchIt->contains("SkipVanillaGuard") || !patchIt->at("SkipVanillaGuard").is_boolean())
                    {
                        patchIt = pendingFlowPatches.erase(patchIt);
                        continue;
                    }

                    auto& patch = patchIt->at("Patch");
                    auto skipVanillaGuard = patchIt->at("SkipVanillaGuard").get<bool>();
                    if (context.applySingleFlowPatch(patch, skipVanillaGuard))
                    {
                        patchIt = pendingFlowPatches.erase(patchIt);
                        continue;
                    }

                    ++patchIt;
                }
            }

        } while (processPendingRequested);
        isProcessingPending = false;
    }

    void TalkFlowPendingWorkProcessor::QueueFlowPatchRetry(
        std::vector<nlohmann::json>& pendingFlowPatches,
        const nlohmann::json& patch,
        bool skipVanillaGuard)
    {
        auto wrapper = nlohmann::json::object();
        wrapper["Patch"] = patch;
        wrapper["SkipVanillaGuard"] = skipVanillaGuard;

        auto wrapperDump = wrapper.dump();
        auto alreadyQueued = std::find_if(
            pendingFlowPatches.begin(),
            pendingFlowPatches.end(),
            [&](const nlohmann::json& pending)
            {
                return pending.dump() == wrapperDump;
            }
        ) != pendingFlowPatches.end();

        if (!alreadyQueued)
        {
            pendingFlowPatches.push_back(std::move(wrapper));
            if (patch.contains("AssetPath") && patch.at("AssetPath").is_string())
            {
                PS::Log<LogLevel::Warning>(
                    STR("Flow patch target not loaded yet for {}. Queued for retry.\n"),
                    RC::to_generic_string(patch.at("AssetPath").get<std::string>())
                );
            }
            else
            {
                PS::Log<LogLevel::Warning>(STR("Flow patch target not loaded yet. Queued for retry.\n"));
            }
        }
    }
}

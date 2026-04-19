#pragma once

#include "String/StringType.hpp"
#include "nlohmann/json.hpp"

#include <functional>

namespace RC::Unreal
{
    class UClass;
    class UDataTable;
    class UObject;
}

namespace Palworld::TalkFlow
{
    struct FlowPatchApplierContext
    {
        RC::Unreal::UClass* flowNodeClass = nullptr;
        RC::Unreal::UDataTable* npcTalkFlowTable = nullptr;
        std::function<void(RC::Unreal::UObject*, const nlohmann::json&)> applyNodePatch;
        std::function<void(const nlohmann::json&)> addOrEditTalkText;
    };

    class FlowPatchApplier
    {
    public:
        static bool ApplySingleFlowPatch(
            const nlohmann::json& patch,
            bool skipVanillaGuard,
            const FlowPatchApplierContext& context);
    };
}

#pragma once

#include "String/StringType.hpp"
#include "nlohmann/json.hpp"

#include <functional>
#include <optional>

namespace RC::Unreal
{
    class UClass;
    class UObject;
}

namespace Palworld
{
    class TalkFlowCloneManager;
}

namespace Palworld::TalkFlow
{
    struct ConversationPatchCompilerContext
    {
        RC::Unreal::UClass* flowNodeClass = nullptr;
        ::Palworld::TalkFlowCloneManager* cloneManager = nullptr;
        std::function<RC::Unreal::UObject*(const RC::StringType&)> findTalkFlowAsset;
    };

    class ConversationPatchCompiler
    {
    public:
        static nlohmann::json BuildConversationPatchFromSchema(
            const std::string& ownerId,
            const nlohmann::json& conversationNodes,
            const RC::StringType& assetPath,
            const std::optional<std::string>& preferredStartNode,
            const ConversationPatchCompilerContext& context);
    };
}

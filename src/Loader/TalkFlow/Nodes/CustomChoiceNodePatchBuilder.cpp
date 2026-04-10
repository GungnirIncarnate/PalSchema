#include "Loader/TalkFlow/Nodes/CustomChoiceNodePatchBuilder.h"

namespace Palworld::TalkFlow::Nodes
{
    void CustomChoiceNodePatchBuilder::Build(
        const std::string& logicalId,
        const nlohmann::json& nodeDef,
        const std::string& choiceNodeName,
        const std::function<std::string(const std::string&, const std::string&, const nlohmann::json&)>& resolveButtonTextId,
        const std::function<std::string(const std::string&, const std::string&, const nlohmann::json&)>& resolveButtonDefaultText,
        const std::function<void(const std::string&, const std::string&)>& registerButtonTextEntry,
        const std::function<std::optional<std::string>(const std::string&, const std::string&, const nlohmann::json&)>& resolveButtonTargetNode,
        nlohmann::json& nodePatches)
    {
        auto choicePatch = nlohmann::json::object();
        choicePatch["ChoiceMsgIDList"] = nlohmann::json::array();
        choicePatch["OutputPins"] = nlohmann::json::array();
        choicePatch["Connections"] = nlohmann::json::array();

        for (const auto& [buttonId, buttonDef] : nodeDef.at("Buttons").items())
        {
            const auto buttonMsgId = resolveButtonTextId(logicalId, buttonId, buttonDef);
            registerButtonTextEntry(buttonMsgId, resolveButtonDefaultText(logicalId, buttonId, buttonDef));
            choicePatch["ChoiceMsgIDList"].push_back(buttonMsgId);
            choicePatch["OutputPins"].push_back({
                { "PinName", buttonMsgId },
                { "PinFriendlyName", buttonMsgId },
                { "PinToolTip", "" }
            });

            const auto targetNodeName = resolveButtonTargetNode(logicalId, buttonId, buttonDef);
            if (targetNodeName.has_value())
            {
                choicePatch["Connections"].push_back({
                    { "Key", buttonMsgId },
                    { "Value", {
                        { "NodeName", targetNodeName.value() },
                        { "PinName", "In" }
                    } }
                });
            }
        }

        nodePatches[choiceNodeName] = std::move(choicePatch);
    }
}

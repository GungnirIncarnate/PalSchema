#include "Unreal/Engine/UDataTable.hpp"
#include "Unreal/UObjectGlobals.hpp"
#include "SDK/Classes/Custom/UObjectGlobals.h"
#include "SDK/Structs/FPalNPCTalkFlowClassDataRow.h"
#include "SDK/Structs/FPalLocalizedTextData.h"
#include "SDK/Helper/PropertyHelper.h"
#include "Loader/PalTalkFlowModLoader.h"
#include "Utility/Logging.h"
#include "Utility/JsonHelpers.h"
#include "Helpers/String.hpp"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <format>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <vector>

using namespace RC;
using namespace RC::Unreal;

namespace Palworld {

    namespace constants {
        constexpr const TCHAR* cloneSourceTalkFlowAssetPath = STR("/Game/Pal/Blueprint/FlowGraph/NPCTalkFlow/Graph/FABP_CommonItemShop.FABP_CommonItemShop");
        constexpr const TCHAR* vanillaAssetPrefix = STR("/Game/Pal/");
    }

    PalTalkFlowModLoader::PalTalkFlowModLoader() : PalModLoaderBase("talkflows")
    {
        SetDisplayName(TEXT("TalkFlow Mod Loader"));
    }

    PalTalkFlowModLoader::~PalTalkFlowModLoader() {}

    bool PalTalkFlowModLoader::CanInitialize(const EEngineLifecyclePhase& engineLifecyclePhase)
    {
        return engineLifecyclePhase == EEngineLifecyclePhase::GameInstanceInit;
    }

    bool PalTalkFlowModLoader::OnInitialize()
    {
        m_flowNodeClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/Flow.FlowNode"));
        m_talkFlowCloneManager.Initialize(m_flowNodeClass);

        try
        {
            m_npcTalkFlowTable = GetDatatableByName("DT_NPCTalkFlow");
            m_humanParamTable = TryGetDatatableByName("DT_PalHumanParameter");
            m_npcTalkTextTable = TryGetDatatableByName("DT_NpcTalkText");
        }
        catch (const std::exception& e)
        {
            PS::Log<LogLevel::Error>(STR("Unable to initialize {}, {}\n"), GetDisplayName(), RC::to_generic_string(e.what()));
            return false;
        }

        if (!m_flowNodeClass)
        {
            PS::Log<LogLevel::Error>(STR("TalkFlow loader failed to initialize: /Script/Flow.FlowNode not found.\n"));
            return false;
        }

        if (!m_npcTalkFlowTable)
        {
            PS::Log<LogLevel::Error>(STR("TalkFlow loader failed to initialize: DT_NPCTalkFlow not found.\n"));
            return false;
        }

        return true;
    }

    std::filesystem::path PalTalkFlowModLoader::ResolveLoaderPath(const std::filesystem::path& modPath) const
    {
        return modPath / "npcs" / "talkflows";
    }

    void PalTalkFlowModLoader::OnLoad(const std::filesystem::path& loaderPath, const RC::StringType& modName, const EEngineLifecyclePhase& engineLifecyclePhase)
    {
        if (engineLifecyclePhase != EEngineLifecyclePhase::GameInstanceInit)
        {
            return;
        }

        if (std::filesystem::is_directory(loaderPath))
        {
            PS::JsonHelpers::ParseJsonFilesInPath(loaderPath, [&](const nlohmann::json& data)
            {
                LoadData(data);
            });
        }
    }

    void PalTalkFlowModLoader::LoadData(const nlohmann::json& Data)
    {
        if (!m_npcTalkFlowTable)
        {
            throw std::runtime_error("Failed to load talkflows: DT_NPCTalkFlow could not be found.");
        }

        for (auto& [CharacterIdString, RowData] : Data.items())
        {
            if (CharacterIdString == "FlowPatches" || CharacterIdString == "$flowPatches")
            {
                continue;
            }

            bool didWork = false;
            RC::StringType resolvedTalkFlowPath{};
            const auto characterId = RC::to_generic_string(CharacterIdString);

            auto ensureResolvedTalkFlowPath = [&]()
            {
                if (resolvedTalkFlowPath.empty())
                {
                    resolvedTalkFlowPath = ResolveAndAssignClonedTalkFlow(characterId, constants::cloneSourceTalkFlowAssetPath);
                }
            };

            if (RowData.is_string())
            {
                ensureResolvedTalkFlowPath();
                didWork = true;
            }
            else if (RowData.is_object())
            {
                if (RowData.contains("TalkFlowAssetPath"))
                {
                    ensureResolvedTalkFlowPath();
                    didWork = true;
                }

                std::optional<std::string> preferredStartNode;
                if (RowData.contains("StartNode"))
                {
                    if (!RowData.at("StartNode").is_string())
                    {
                        throw std::runtime_error(std::format("StartNode for '{}' must be a string node id.", CharacterIdString));
                    }

                    preferredStartNode = RowData.at("StartNode").get<std::string>();
                }

                if (!RowData.contains("TalkFlowAssetPath") && (RowData.contains("Text") || RowData.contains("Buttons")))
                {
                    ensureResolvedTalkFlowPath();
                    didWork = true;
                }

                if (RowData.contains("Text"))
                {
                    if (!RowData.at("Text").is_object())
                    {
                        throw std::runtime_error(std::format("Text for '{}' must be an object mapping MsgId -> text.", CharacterIdString));
                    }

                    AddOrEditTalkText(RowData.at("Text"));
                    didWork = true;
                }

                if (RowData.contains("Buttons"))
                {
                    if (!RowData.at("Buttons").is_object())
                    {
                        throw std::runtime_error(std::format("Buttons for '{}' must be an object mapping ChoiceMsgId -> text.", CharacterIdString));
                    }

                    AddOrEditTalkText(RowData.at("Buttons"));
                    didWork = true;
                }

                if (RowData.contains("Nodes"))
                {
                    if (!RowData.at("Nodes").is_object())
                    {
                        throw std::runtime_error(std::format("Nodes for '{}' must be an object mapping NodeId -> schema node definition.", CharacterIdString));
                    }

                    if (resolvedTalkFlowPath.empty())
                    {
                        ensureResolvedTalkFlowPath();
                    }

                    const auto isVanillaNamespace = resolvedTalkFlowPath.rfind(constants::vanillaAssetPrefix, 0) == 0;
                    const auto looksLikeClonePath = resolvedTalkFlowPath.find(STR("TFClone_")) != RC::StringType::npos;
                    if (isVanillaNamespace && !looksLikeClonePath)
                    {
                        throw std::runtime_error(std::format(
                            "Failed to isolate talkflow for '{}'. Clone resolution returned vanilla path '{}'. "
                            "Refusing to patch nodes on shared asset.",
                            CharacterIdString,
                            RC::to_string(resolvedTalkFlowPath)
                        ));
                    }

                    const auto& nodesPayload = RowData.at("Nodes");

                    if (!IsConversationNodeSchema(nodesPayload))
                    {
                        throw std::runtime_error(std::format(
                            "Nodes for '{}' must use conversation schema format. Raw node patches are no longer supported.",
                            CharacterIdString
                        ));
                    }

                    nlohmann::json perNpcPatch = BuildConversationPatchFromSchema(CharacterIdString, nodesPayload, resolvedTalkFlowPath, preferredStartNode);
                    if (perNpcPatch.contains("Text"))
                    {
                        AddOrEditTalkText(perNpcPatch.at("Text"));
                    }

                    if (perNpcPatch.contains("Buttons"))
                    {
                        AddOrEditTalkText(perNpcPatch.at("Buttons"));
                    }

                    if (!ApplySingleFlowPatch(perNpcPatch, true))
                    {
                        QueueFlowPatchRetry(perNpcPatch, true);
                    }
                    else
                    {
                        PS::Log<LogLevel::Normal>(
                            STR("Applied talkflow patch for NPC {}\n"),
                            RC::to_generic_string(CharacterIdString)
                        );
                    }
                    didWork = true;
                }
            }
            else
            {
                throw std::runtime_error(std::format("TalkFlow value for '{}' must be a string path or an object.", CharacterIdString));
            }

            if (!didWork)
            {
                throw std::runtime_error(std::format("Talkflow entry '{}' did not contain supported fields.", CharacterIdString));
            }
        }

        ProcessPending();
    }

    void PalTalkFlowModLoader::ProcessPending()
    {
        if (m_isProcessingPending)
        {
            m_processPendingRequested = true;
            return;
        }

        if (!m_npcTalkFlowTable || !m_flowNodeClass)
        {
            return;
        }

        if (m_npcTalkFlowTable->IsUnreachable() || m_flowNodeClass->IsUnreachable())
        {
            return;
        }

        m_isProcessingPending = true;
        do
        {
            m_processPendingRequested = false;

            if (!m_pendingCloneAssignments.empty())
            {
                auto it = m_pendingCloneAssignments.begin();
                while (it != m_pendingCloneAssignments.end())
                {
                    TalkFlowCloneRequest cloneRequest{};
                    cloneRequest.CharacterId = it->CharacterId;
                    cloneRequest.SourceAssetPath = it->SourceAssetPath;
                    cloneRequest.ForceRebuild = it->ForceRebuild;

                    auto resolvedTalkFlowPath = m_talkFlowCloneManager.ResolveTalkFlowAssetPath(cloneRequest);
                    if (resolvedTalkFlowPath != it->SourceAssetPath)
                    {
                        AssignTalkFlowToNpc(it->CharacterId, resolvedTalkFlowPath);
                        it = m_pendingCloneAssignments.erase(it);
                        continue;
                    }

                    ++it;
                }
            }

            if (!m_pendingFlowPatches.empty())
            {
                auto it = m_pendingFlowPatches.begin();
                while (it != m_pendingFlowPatches.end())
                {
                    if (!it->is_object() || !it->contains("Patch") || !it->at("Patch").is_object() || !it->contains("SkipVanillaGuard") || !it->at("SkipVanillaGuard").is_boolean())
                    {
                        it = m_pendingFlowPatches.erase(it);
                        continue;
                    }

                    auto& patch = it->at("Patch");
                    auto skipVanillaGuard = it->at("SkipVanillaGuard").get<bool>();
                    if (ApplySingleFlowPatch(patch, skipVanillaGuard))
                    {
                        it = m_pendingFlowPatches.erase(it);
                        continue;
                    }

                    ++it;
                }
            }

        } while (m_processPendingRequested);
        m_isProcessingPending = false;
    }

    namespace {
        void NormalizeNodePatchForEngineTypes(nlohmann::json& NodePatch)
        {
            if (!NodePatch.is_object())
            {
                return;
            }

            // Compatibility: allow object-form PinFriendlyName and normalize to string.
            if (NodePatch.contains("OutputPins") && NodePatch.at("OutputPins").is_array())
            {
                for (auto& pinEntry : NodePatch.at("OutputPins"))
                {
                    if (!pinEntry.is_object() || !pinEntry.contains("PinFriendlyName"))
                    {
                        continue;
                    }

                    auto& pinFriendlyName = pinEntry["PinFriendlyName"];
                    if (pinFriendlyName.is_object())
                    {
                        std::string resolvedFriendlyName;
                        if (pinFriendlyName.contains("CultureInvariantString") && pinFriendlyName.at("CultureInvariantString").is_string())
                        {
                            resolvedFriendlyName = pinFriendlyName.at("CultureInvariantString").get<std::string>();
                        }
                        else if (pinEntry.contains("PinName") && pinEntry.at("PinName").is_string())
                        {
                            resolvedFriendlyName = pinEntry.at("PinName").get<std::string>();
                        }

                        pinFriendlyName = resolvedFriendlyName;
                    }
                }
            }

        }

        RC::StringType NormalizeNodeName(const RC::StringType& nodeName)
        {
            auto normalized = nodeName;
            auto endsWithDigits = !normalized.empty() && std::isdigit(static_cast<unsigned char>(normalized.back()));
            if (!endsWithDigits)
            {
                return normalized;
            }

            auto trimPos = normalized.size();
            while (trimPos > 0 && std::isdigit(static_cast<unsigned char>(normalized[trimPos - 1])))
            {
                --trimPos;
            }

            if (trimPos > 0 && normalized[trimPos - 1] == '_')
            {
                normalized.resize(trimPos - 1);
            }

            return normalized;
        }

        int ExtractTrailingNumber(const std::string& text)
        {
            if (text.empty() || !std::isdigit(static_cast<unsigned char>(text.back())))
            {
                return -1;
            }

            auto split = text.size();
            while (split > 0 && std::isdigit(static_cast<unsigned char>(text[split - 1])))
            {
                --split;
            }

            return std::stoi(text.substr(split));
        }

        void SortNodeNamesBySuffix(std::vector<std::string>& names)
        {
            std::sort(names.begin(), names.end(), [](const std::string& lhs, const std::string& rhs)
            {
                const auto lhsNum = ExtractTrailingNumber(lhs);
                const auto rhsNum = ExtractTrailingNumber(rhs);
                if (lhsNum != rhsNum)
                {
                    return lhsNum < rhsNum;
                }

                return lhs < rhs;
            });
        }

        std::string SanitizeToken(std::string value)
        {
            for (auto& c : value)
            {
                if (!std::isalnum(static_cast<unsigned char>(c)))
                {
                    c = '_';
                }
                else
                {
                    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                }
            }

            return value;
        }

        std::string ExtractPackageAssetName(const RC::StringType& assetPath)
        {
            auto packagePart = assetPath;
            const auto dot = packagePart.find_last_of('.');
            if (dot != RC::StringType::npos)
            {
                packagePart = packagePart.substr(0, dot);
            }

            const auto slash = packagePart.find_last_of('/');
            if (slash != RC::StringType::npos && slash + 1 < packagePart.size())
            {
                packagePart = packagePart.substr(slash + 1);
            }

            return RC::to_string(packagePart);
        }

        nlohmann::json BuildGuidJsonFromNode(RC::Unreal::UObject* nodeObject)
        {
            struct GuidMemory { uint32_t A; uint32_t B; uint32_t C; uint32_t D; };

            auto* guid = Palworld::PropertyHelper::GetValuePtrByPropertyNameInChain<GuidMemory>(nodeObject, STR("NodeGuid"));
            if (!guid)
            {
                throw std::runtime_error(std::format("Node '{}' does not expose NodeGuid.", RC::to_string(nodeObject->GetName())));
            }

            auto guidJson = nlohmann::json::object();
            guidJson["A"] = static_cast<uint64_t>(guid->A);
            guidJson["B"] = static_cast<uint64_t>(guid->B);
            guidJson["C"] = static_cast<uint64_t>(guid->C);
            guidJson["D"] = static_cast<uint64_t>(guid->D);
            return guidJson;
        }

        void ResolveConnectionNodeNames(
            nlohmann::json& nodePatch,
            const std::unordered_map<std::string, RC::Unreal::UObject*>& nodesByName)
        {
            if (!nodePatch.is_object() || !nodePatch.contains("Connections") || !nodePatch.at("Connections").is_array())
            {
                return;
            }

            for (auto& connectionEntry : nodePatch.at("Connections"))
            {
                if (!connectionEntry.is_object() || !connectionEntry.contains("Value") || !connectionEntry.at("Value").is_object())
                {
                    continue;
                }

                auto& value = connectionEntry["Value"];
                if (!value.contains("NodeName") || !value.at("NodeName").is_string())
                {
                    continue;
                }

                const auto nodeName = value.at("NodeName").get<std::string>();
                auto nodeIt = nodesByName.find(nodeName);
                if (nodeIt == nodesByName.end())
                {
                    throw std::runtime_error(std::format(
                        "Connection target NodeName '{}' was not found in flow asset.",
                        nodeName
                    ));
                }

                value["NodeGuid"] = BuildGuidJsonFromNode(nodeIt->second);
                value.erase("NodeName");
            }
        }
    }

    RC::StringType PalTalkFlowModLoader::ResolveAndAssignClonedTalkFlow(const RC::StringType& CharacterIdString, const RC::StringType& TalkFlowPath, bool ForceRebuildClone)
    {
        auto canonicalTalkFlowPath = CanonicalizeTalkFlowPath(TalkFlowPath);
        auto resolvedTalkFlowPath = canonicalTalkFlowPath;
        TalkFlowCloneRequest cloneRequest{};
        cloneRequest.CharacterId = CharacterIdString;
        cloneRequest.SourceAssetPath = canonicalTalkFlowPath;
        cloneRequest.ForceRebuild = ForceRebuildClone;
        resolvedTalkFlowPath = m_talkFlowCloneManager.ResolveTalkFlowAssetPath(cloneRequest);

        if (resolvedTalkFlowPath == canonicalTalkFlowPath)
        {
            auto alreadyQueued = std::find_if(
                m_pendingCloneAssignments.begin(),
                m_pendingCloneAssignments.end(),
                [&](const PendingCloneAssignment& pending)
                {
                    return pending.CharacterId == CharacterIdString && pending.SourceAssetPath == canonicalTalkFlowPath;
                }
            ) != m_pendingCloneAssignments.end();

            if (!alreadyQueued)
            {
                m_pendingCloneAssignments.push_back(PendingCloneAssignment{ CharacterIdString, canonicalTalkFlowPath, true });
                PS::Log<LogLevel::Warning>(
                    STR("TalkFlow clone source not loaded yet for {} ({}). Queued for retry.\n"),
                    CharacterIdString,
                    canonicalTalkFlowPath
                );
            }
        }

        AssignTalkFlowToNpc(CharacterIdString, resolvedTalkFlowPath);
        return resolvedTalkFlowPath;
    }

    void PalTalkFlowModLoader::AssignTalkFlowToNpc(const RC::StringType& CharacterIdString, const RC::StringType& TalkFlowPath)
    {
        auto CharacterId = FName(CharacterIdString, FNAME_Add);
        auto ExistingRow = std::bit_cast<FPalNPCTalkFlowClassDataRow*>(m_npcTalkFlowTable->FindRowUnchecked(CharacterId));
        if (ExistingRow)
        {
            ExistingRow->NPCTalkFlowClass = UECustom::TSoftClassPtr<UClass>(UECustom::FSoftObjectPath(TalkFlowPath));
        }
        else
        {
            FPalNPCTalkFlowClassDataRow NewRow{ TalkFlowPath };
            m_npcTalkFlowTable->AddRow(CharacterId, NewRow);
        }

        if (m_humanParamTable)
        {
            auto* humanRow = m_humanParamTable->FindRowUnchecked(CharacterId);
            if (humanRow)
            {
                auto* rowStruct = m_humanParamTable->GetRowStruct().Get();
                if (rowStruct)
                {
                    const std::array<RC::StringType, 4> candidateProps{
                        STR("SoftTalkFlowAsset"),
                        STR("TalkFlowAsset"),
                        STR("NPCTalkFlowClass"),
                        STR("TalkFlowClass")
                    };

                    for (const auto& propName : candidateProps)
                    {
                        auto* prop = rowStruct->GetPropertyByName(propName.c_str());
                        if (!prop)
                        {
                            continue;
                        }

                        Palworld::PropertyHelper::CopyJsonValueToContainer(humanRow, prop, RC::to_string(TalkFlowPath));
                        break;
                    }
                }
            }
        }
    }

    void PalTalkFlowModLoader::AddOrEditTalkText(const nlohmann::json& TextEntries)
    {
        if (!m_npcTalkTextTable)
        {
            PS::Log<LogLevel::Warning>(STR("DT_NpcTalkText not found, skipping talk text entries.\n"));
            return;
        }

        for (auto& [MessageIdString, MessageText] : TextEntries.items())
        {
            if (!MessageText.is_string())
            {
                throw std::runtime_error(std::format("Text for MsgId '{}' must be a string.", MessageIdString));
            }

            auto MessageId = FName(RC::to_generic_string(MessageIdString), FNAME_Add);
            auto ExistingTextRow = std::bit_cast<FPalLocalizedTextData*>(m_npcTalkTextTable->FindRowUnchecked(MessageId));
            auto LocalizedText = FText(RC::to_generic_string(MessageText.get<std::string>()));
            if (ExistingTextRow)
            {
                ExistingTextRow->TextData = LocalizedText;
            }
            else
            {
                FPalLocalizedTextData NewRow{};
                NewRow.TextData = LocalizedText;
                m_npcTalkTextTable->AddRow(MessageId, NewRow);
            }

        }
    }

    bool PalTalkFlowModLoader::IsConversationNodeSchema(const nlohmann::json& Nodes) const
    {
        if (!Nodes.is_object() || Nodes.empty())
        {
            return false;
        }

        for (const auto& [nodeId, nodeDef] : Nodes.items())
        {
            if (!nodeDef.is_object())
            {
                return false;
            }

            const auto nodeType = nodeDef.value("Type", std::string{});
            const auto isPureNodeType = (
                nodeType == "ItemShopBuy" || nodeType == "ItemShopSell" ||
                nodeType == "PalShopBuy" || nodeType == "PalShopSell" ||
                nodeType == "GetItem" ||
                nodeType == "TalkCountBranch");

            if (isPureNodeType)
            {
                // Pure nodes intentionally don't require Msg/Buttons.
                continue;
            }

            const auto hasDefaultMsg = nodeDef.contains("DefaultMsg") && nodeDef.at("DefaultMsg").is_string();
            if (!hasDefaultMsg)
            {
                return false;
            }

            // Valid forms for a conversation node's continuation:
            //   "Buttons": { ... }          — choice or single-button node (validated below)
            //   "LinkID": "OtherNode"       — node-level auto-chain (no button UI)
            //   (neither)                    — terminal node, conversation ends after message
            const auto hasButtons = nodeDef.contains("Buttons") && nodeDef.at("Buttons").is_object();

            if (hasButtons)
            {
                for (const auto& [buttonKey, buttonDef] : nodeDef.at("Buttons").items())
                {
                    if (!buttonDef.is_object())
                    {
                        return false;
                    }

                    const auto hasDefaultText = buttonDef.contains("DefaultText") && buttonDef.at("DefaultText").is_string();
                    if (!hasDefaultText)
                    {
                        return false;
                    }
                }
            }
        }

        return true;
    }

    nlohmann::json PalTalkFlowModLoader::BuildConversationPatchFromSchema(
        const std::string& OwnerId,
        const nlohmann::json& ConversationNodes,
        const RC::StringType& AssetPath,
        const std::optional<std::string>& PreferredStartNode)
    {
        auto* flowAsset = FindTalkFlowAsset(AssetPath);
        if (!flowAsset)
        {
            throw std::runtime_error(std::format(
                "Unable to compile conversation schema for '{}' because flow asset '{}' is not loaded.",
                OwnerId,
                RC::to_string(AssetPath)
            ));
        }

        TArray<UObject*> flowNodeObjects;
        UECustom::UObjectGlobals::GetObjectsOfClass(m_flowNodeClass, flowNodeObjects, true, EObjectFlags::RF_ClassDefaultObject, EInternalObjectFlags::None);

        std::unordered_map<std::string, UObject*> nodesByName;
        std::vector<std::string> startNodes;
        std::vector<std::string> messageNodes;
        std::vector<std::string> choiceNodes;
        std::vector<std::string> openItemShopNodes;
        std::vector<std::string> openItemShopBuyNodes;
        std::vector<std::string> openItemShopSellNodes;
        std::vector<std::string> openPalShopNodes;
        std::vector<std::string> openPalShopBuyNodes;
        std::vector<std::string> openPalShopSellNodes;
        std::vector<std::string> getItemNodes;
        std::vector<std::string> talkCountBranchNodes;

        auto classifyOpenShopNode = [&](UObject* nodeObject, const std::string& nodeName)
        {
            openItemShopNodes.push_back(nodeName);

            // Empirical mapping:
            // - default/0 tab => buy shop
            // - 1 tab         => sell shop
            int32_t tabValue = 0;
            bool hasTabValue = false;

            if (auto* tabPtr8 = Palworld::PropertyHelper::GetValuePtrByPropertyNameInChain<uint8_t>(nodeObject, STR("OpenItemShopTabType")))
            {
                tabValue = static_cast<int32_t>(*tabPtr8);
                hasTabValue = true;
            }
            else if (auto* tabPtr32 = Palworld::PropertyHelper::GetValuePtrByPropertyNameInChain<int32_t>(nodeObject, STR("OpenItemShopTabType")))
            {
                tabValue = *tabPtr32;
                hasTabValue = true;
            }

            if (hasTabValue && tabValue == 1)
            {
                openItemShopSellNodes.push_back(nodeName);
            }
            else
            {
                openItemShopBuyNodes.push_back(nodeName);
            }
        };

        auto classifyOpenPalShopNode = [&](UObject* nodeObject, const std::string& nodeName)
        {
            openPalShopNodes.push_back(nodeName);

            // Empirical mapping:
            // - default/0 tab => buy shop
            // - 1 tab         => sell shop
            int32_t tabValue = 0;
            bool hasTabValue = false;

            if (auto* tabPtr8 = Palworld::PropertyHelper::GetValuePtrByPropertyNameInChain<uint8_t>(nodeObject, STR("OpenPalShopTabType")))
            {
                tabValue = static_cast<int32_t>(*tabPtr8);
                hasTabValue = true;
            }
            else if (auto* tabPtr32 = Palworld::PropertyHelper::GetValuePtrByPropertyNameInChain<int32_t>(nodeObject, STR("OpenPalShopTabType")))
            {
                tabValue = *tabPtr32;
                hasTabValue = true;
            }

            if (hasTabValue && tabValue == 1)
            {
                openPalShopSellNodes.push_back(nodeName);
            }
            else
            {
                openPalShopBuyNodes.push_back(nodeName);
            }
        };

        auto classifyNode = [&](UObject* nodeObject)
        {
            const auto nodeName = RC::to_string(nodeObject->GetName());
            nodesByName.emplace(nodeName, nodeObject);

            const auto className = nodeObject->GetClassPrivate() ? RC::to_string(nodeObject->GetClassPrivate()->GetName()) : std::string{};
            const auto hasMsgIdList = nodeObject->GetPropertyByNameInChain(STR("MsgIdList")) != nullptr;
            const auto hasChoiceMsgIdList = nodeObject->GetPropertyByNameInChain(STR("ChoiceMsgIDList")) != nullptr;
            const auto hasOpenItemShopTabType = nodeObject->GetPropertyByNameInChain(STR("OpenItemShopTabType")) != nullptr;
            const auto hasOpenPalShopTabType = nodeObject->GetPropertyByNameInChain(STR("OpenPalShopTabType")) != nullptr;
            const auto hasGetItemList = nodeObject->GetPropertyByNameInChain(STR("GetItemList")) != nullptr;
            const auto hasLotteryDataTable = nodeObject->GetPropertyByNameInChain(STR("LotteryDataTable")) != nullptr;
            const auto hasOutputPins = nodeObject->GetPropertyByNameInChain(STR("OutputPins")) != nullptr;

            if (className.contains("FlowNode_Start") || nodeName.starts_with("FlowNode_Start"))
            {
                startNodes.push_back(nodeName);
            }
            else if (className.contains("FNBP_NPCTalk_FixedMsdId_C") || hasMsgIdList)
            {
                messageNodes.push_back(nodeName);
            }
            else if (className.contains("FNBP_NPCTalk_CustomChoice_C") || hasChoiceMsgIdList)
            {
                choiceNodes.push_back(nodeName);
            }
            else if (className.contains("FNBP_OpenItemShop_C") || hasOpenItemShopTabType)
            {
                classifyOpenShopNode(nodeObject, nodeName);
            }
            else if (className.contains("FNBP_OpenPalShop_C") || hasOpenPalShopTabType)
            {
                classifyOpenPalShopNode(nodeObject, nodeName);
            }
            else if (className.contains("FNBP_GetItem_C") || hasGetItemList || hasLotteryDataTable)
            {
                getItemNodes.push_back(nodeName);
            }
            else if (className.contains("FNBP_NPCTalkCountBranch_C") || hasOutputPins)
            {
                talkCountBranchNodes.push_back(nodeName);
            }
        };

        for (auto* nodeObject : flowNodeObjects)
        {
            if (!nodeObject || nodeObject->GetOuterPrivate() != flowAsset)
            {
                continue;
            }

            classifyNode(nodeObject);
        }

        if (nodesByName.empty())
        {
            const auto sourceOuterName = ExtractPackageAssetName(AssetPath);
            for (auto* nodeObject : flowNodeObjects)
            {
                if (!nodeObject)
                {
                    continue;
                }

                auto* outer = nodeObject->GetOuterPrivate();
                if (!outer)
                {
                    continue;
                }

                if (RC::to_string(outer->GetName()) != sourceOuterName)
                {
                    continue;
                }

                classifyNode(nodeObject);
            }
        }

        SortNodeNamesBySuffix(startNodes);
        SortNodeNamesBySuffix(messageNodes);
        SortNodeNamesBySuffix(choiceNodes);
        SortNodeNamesBySuffix(openItemShopNodes);
        SortNodeNamesBySuffix(openItemShopBuyNodes);
        SortNodeNamesBySuffix(openItemShopSellNodes);
        SortNodeNamesBySuffix(openPalShopNodes);
        SortNodeNamesBySuffix(openPalShopBuyNodes);
        SortNodeNamesBySuffix(openPalShopSellNodes);
        SortNodeNamesBySuffix(getItemNodes);
        SortNodeNamesBySuffix(talkCountBranchNodes);

        // Pre-analysis: determine required node counts from JSON (respecting optional "Type" field)
        // and spawn any shortfall directly into the clone before the capacity checks run.
        {
            size_t neededMessageNodes = 0;
            size_t neededChoiceNodes = 0;
            size_t neededBuyShopNodes = 0;
            size_t neededSellShopNodes = 0;
            size_t neededBuyPalShopNodes = 0;
            size_t neededSellPalShopNodes = 0;
            size_t neededGetItemNodes = 0;
            size_t neededTalkCountBranchNodes = 0;
            bool willNeedExitSpare = false;

            for (const auto& [logicalId, nodeDef] : ConversationNodes.items())
            {
                const auto typeStr = nodeDef.value("Type", std::string{});
                if (typeStr == "ItemShopBuy")
                {
                    ++neededBuyShopNodes;
                    continue;
                }

                if (typeStr == "ItemShopSell")
                {
                    ++neededSellShopNodes;
                    continue;
                }

                if (typeStr == "PalShopBuy")
                {
                    ++neededBuyPalShopNodes;
                    continue;
                }

                if (typeStr == "PalShopSell")
                {
                    ++neededSellPalShopNodes;
                    continue;
                }

                if (typeStr == "GetItem")
                {
                    ++neededGetItemNodes;
                    continue;
                }

                if (typeStr == "TalkCountBranch")
                {
                    ++neededTalkCountBranchNodes;
                    continue;
                }

                ++neededMessageNodes;

                const auto hasButtons = nodeDef.contains("Buttons") && nodeDef.at("Buttons").is_object();
                const auto buttonCount = hasButtons ? nodeDef.at("Buttons").size() : 0;
                bool needsChoice = buttonCount > 1;
                if (!typeStr.empty())
                {
                    if (typeStr == "CustomChoice") needsChoice = true;
                    else if (typeStr == "FixedMessage") needsChoice = false;
                    else throw std::runtime_error(std::format(
                        "Conversation '{}': node '{}' has unknown Type '{}'. Supported: FixedMessage, CustomChoice, ItemShopBuy, ItemShopSell, PalShopBuy, PalShopSell, GetItem, TalkCountBranch.",
                        OwnerId, logicalId, typeStr));
                }
                if (needsChoice) ++neededChoiceNodes;
                if (hasButtons)
                {
                    for (const auto& [btnId, btnDef] : nodeDef.at("Buttons").items())
                    {
                        const auto explicitExit = btnDef.contains("Action") && btnDef.at("Action").is_string() && btnDef.at("Action").get<std::string>() == "Exit";
                        const auto implicitExit = !btnDef.contains("LinkID") && !btnDef.contains("Action");
                        if (explicitExit || implicitExit)
                            willNeedExitSpare = true;
                    }
                }
            }

            if (willNeedExitSpare) ++neededMessageNodes;

            int spawnIdx = 0;
            while (messageNodes.size() < neededMessageNodes)
            {
                const auto newName = std::format("FNBP_NPCTalk_FixedMsdId_C_Spawned_{}", spawnIdx++);
                auto* newNode = m_talkFlowCloneManager.SpawnNodeInClone(flowAsset, "FNBP_NPCTalk_FixedMsdId_C", newName);
                if (!newNode) break;
                const auto nodeNameNarrow = RC::to_string(newNode->GetName());
                messageNodes.push_back(nodeNameNarrow);
                nodesByName.emplace(nodeNameNarrow, newNode);
            }

            spawnIdx = 0;
            while (choiceNodes.size() < neededChoiceNodes)
            {
                const auto newName = std::format("FNBP_NPCTalk_CustomChoice_C_Spawned_{}", spawnIdx++);
                auto* newNode = m_talkFlowCloneManager.SpawnNodeInClone(flowAsset, "FNBP_NPCTalk_CustomChoice_C", newName);
                if (!newNode) break;
                const auto nodeNameNarrow = RC::to_string(newNode->GetName());
                choiceNodes.push_back(nodeNameNarrow);
                nodesByName.emplace(nodeNameNarrow, newNode);
            }

            spawnIdx = 0;
            while (openItemShopBuyNodes.size() < neededBuyShopNodes)
            {
                const auto newName = std::format("FNBP_OpenItemShop_C_Buy_Spawned_{}", spawnIdx++);
                auto* newNode = m_talkFlowCloneManager.SpawnNodeInClone(flowAsset, "FNBP_OpenItemShop_C", newName);
                if (!newNode) break;

                const auto nodeNameNarrow = RC::to_string(newNode->GetName());
                openItemShopNodes.push_back(nodeNameNarrow);
                openItemShopBuyNodes.push_back(nodeNameNarrow);
                nodesByName.emplace(nodeNameNarrow, newNode);
            }

            spawnIdx = 0;
            while (openItemShopSellNodes.size() < neededSellShopNodes)
            {
                const auto newName = std::format("FNBP_OpenItemShop_C_Sell_Spawned_{}", spawnIdx++);
                auto* newNode = m_talkFlowCloneManager.SpawnNodeInClone(flowAsset, "FNBP_OpenItemShop_C", newName);
                if (!newNode) break;

                const auto nodeNameNarrow = RC::to_string(newNode->GetName());
                openItemShopNodes.push_back(nodeNameNarrow);
                openItemShopSellNodes.push_back(nodeNameNarrow);
                nodesByName.emplace(nodeNameNarrow, newNode);
            }

            spawnIdx = 0;
            while (openPalShopBuyNodes.size() < neededBuyPalShopNodes)
            {
                const auto newName = std::format("FNBP_OpenPalShop_C_Buy_Spawned_{}", spawnIdx++);
                auto* newNode = m_talkFlowCloneManager.SpawnNodeInClone(flowAsset, "FNBP_OpenPalShop_C", newName);
                if (!newNode) break;

                const auto nodeNameNarrow = RC::to_string(newNode->GetName());
                openPalShopNodes.push_back(nodeNameNarrow);
                openPalShopBuyNodes.push_back(nodeNameNarrow);
                nodesByName.emplace(nodeNameNarrow, newNode);
            }

            spawnIdx = 0;
            while (openPalShopSellNodes.size() < neededSellPalShopNodes)
            {
                const auto newName = std::format("FNBP_OpenPalShop_C_Sell_Spawned_{}", spawnIdx++);
                auto* newNode = m_talkFlowCloneManager.SpawnNodeInClone(flowAsset, "FNBP_OpenPalShop_C", newName);
                if (!newNode) break;

                const auto nodeNameNarrow = RC::to_string(newNode->GetName());
                openPalShopNodes.push_back(nodeNameNarrow);
                openPalShopSellNodes.push_back(nodeNameNarrow);
                nodesByName.emplace(nodeNameNarrow, newNode);
            }

            spawnIdx = 0;
            while (getItemNodes.size() < neededGetItemNodes)
            {
                const auto newName = std::format("FNBP_GetItem_C_Spawned_{}", spawnIdx++);
                auto* newNode = m_talkFlowCloneManager.SpawnNodeInClone(flowAsset, "FNBP_GetItem_C", newName);
                if (!newNode) break;

                const auto nodeNameNarrow = RC::to_string(newNode->GetName());
                getItemNodes.push_back(nodeNameNarrow);
                nodesByName.emplace(nodeNameNarrow, newNode);
            }

            spawnIdx = 0;
            while (talkCountBranchNodes.size() < neededTalkCountBranchNodes)
            {
                const auto newName = std::format("FNBP_NPCTalkCountBranch_C_Spawned_{}", spawnIdx++);
                auto* newNode = m_talkFlowCloneManager.SpawnNodeInClone(flowAsset, "FNBP_NPCTalkCountBranch_C", newName);
                if (!newNode) break;

                const auto nodeNameNarrow = RC::to_string(newNode->GetName());
                talkCountBranchNodes.push_back(nodeNameNarrow);
                nodesByName.emplace(nodeNameNarrow, newNode);
            }

            SortNodeNamesBySuffix(openItemShopNodes);
            SortNodeNamesBySuffix(openItemShopBuyNodes);
            SortNodeNamesBySuffix(openItemShopSellNodes);
            SortNodeNamesBySuffix(openPalShopNodes);
            SortNodeNamesBySuffix(openPalShopBuyNodes);
            SortNodeNamesBySuffix(openPalShopSellNodes);
            SortNodeNamesBySuffix(getItemNodes);
            SortNodeNamesBySuffix(talkCountBranchNodes);
        }

        const auto hasStartNode = !startNodes.empty();
        if (!hasStartNode)
        {
            PS::Log<LogLevel::Warning>(
                STR("Conversation schema for '{}' did not find FlowNode_Start in '{}'. Keeping existing start routing.\n"),
                RC::to_generic_string(OwnerId),
                AssetPath
            );
        }

        size_t nonShopLogicalNodes = 0;
        for (const auto& [logicalId, nodeDef] : ConversationNodes.items())
        {
            const auto typeStr = nodeDef.value("Type", std::string{});
            if (typeStr != "ItemShopBuy" && typeStr != "ItemShopSell" && typeStr != "PalShopBuy" && typeStr != "PalShopSell" && typeStr != "GetItem" && typeStr != "TalkCountBranch")
            {
                ++nonShopLogicalNodes;
            }
        }

        if (nonShopLogicalNodes > messageNodes.size())
        {
            PS::Log<LogLevel::Warning>(
                STR("Conversation '{}': not enough fixed-message nodes in '{}' even after spawn attempt ({} needed, {} available).\n"),
                RC::to_generic_string(OwnerId),
                AssetPath,
                nonShopLogicalNodes,
                messageNodes.size()
            );
        }

        const auto ownerToken = SanitizeToken(OwnerId);

        std::vector<std::string> logicalOrder;
        logicalOrder.reserve(ConversationNodes.size());
        for (const auto& [logicalId, nodeDef] : ConversationNodes.items())
        {
            logicalOrder.push_back(logicalId);
        }

        auto findNodeIdCaseInsensitive = [&](const std::string& requestedId) -> std::optional<std::string>
        {
            if (ConversationNodes.contains(requestedId))
            {
                return requestedId;
            }

            auto loweredRequested = requestedId;
            std::transform(
                loweredRequested.begin(),
                loweredRequested.end(),
                loweredRequested.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); }
            );

            for (const auto& [logicalId, nodeDef] : ConversationNodes.items())
            {
                auto lowered = logicalId;
                std::transform(
                    lowered.begin(),
                    lowered.end(),
                    lowered.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); }
                );

                if (lowered == loweredRequested)
                {
                    return logicalId;
                }
            }

            return std::nullopt;
        };

        // Entry node priority:
        // 1) explicit StartNode
        // 2) Greeting (case-insensitive)
        // 3) existing logical order fallback
        auto chooseEntryNode = [&]() -> std::optional<std::string>
        {
            if (PreferredStartNode.has_value())
            {
                auto explicitNode = findNodeIdCaseInsensitive(PreferredStartNode.value());
                if (!explicitNode.has_value())
                {
                    throw std::runtime_error(std::format(
                        "Conversation '{}': StartNode '{}' does not exist in Nodes.",
                        OwnerId,
                        PreferredStartNode.value()
                    ));
                }

                return explicitNode;
            }

            return findNodeIdCaseInsensitive("Greeting");
        };

        if (auto entryNode = chooseEntryNode(); entryNode.has_value())
        {
            auto it = std::find(logicalOrder.begin(), logicalOrder.end(), entryNode.value());
            if (it != logicalOrder.end() && it != logicalOrder.begin())
            {
                std::rotate(logicalOrder.begin(), it, it + 1);
            }
        }

        std::unordered_map<std::string, bool> logicalNeedsChoiceNode;
        std::unordered_map<std::string, std::string> logicalNodeType;
        size_t requiredChoiceNodes = 0;
        for (const auto& logicalId : logicalOrder)
        {
            const auto& nodeDef = ConversationNodes.at(logicalId);
            const auto typeStr = nodeDef.value("Type", std::string{});
            logicalNodeType[logicalId] = typeStr;

            if (typeStr == "ItemShopBuy" || typeStr == "ItemShopSell" || typeStr == "PalShopBuy" || typeStr == "PalShopSell" || typeStr == "GetItem" || typeStr == "TalkCountBranch")
            {
                logicalNeedsChoiceNode[logicalId] = false;
                continue;
            }

            const auto hasButtons = nodeDef.contains("Buttons") && nodeDef.at("Buttons").is_object();
            const auto buttonCount = hasButtons ? nodeDef.at("Buttons").size() : 0;
            bool needsChoice = buttonCount > 1;
            if (!typeStr.empty())
            {
                needsChoice = (typeStr == "CustomChoice");
            }
            logicalNeedsChoiceNode[logicalId] = needsChoice;
            if (needsChoice)
            {
                ++requiredChoiceNodes;
            }
        }

        if (requiredChoiceNodes > choiceNodes.size())
        {
            PS::Log<LogLevel::Warning>(
                STR("Conversation '{}': not enough choice nodes in '{}' even after spawn attempt ({} needed, {} available).\n"),
                RC::to_generic_string(OwnerId),
                AssetPath,
                requiredChoiceNodes,
                choiceNodes.size()
            );
        }

        std::unordered_map<std::string, std::string> logicalToRuntimeNode;
        std::unordered_map<std::string, std::string> logicalToMessageNode;
        std::unordered_map<std::string, std::string> logicalToChoiceNode;
        std::vector<std::string> mappedBuyShopNodes;
        std::vector<std::string> mappedSellShopNodes;
        std::vector<std::string> mappedBuyPalShopNodes;
        std::vector<std::string> mappedSellPalShopNodes;
        std::vector<std::pair<std::string, std::string>> mappedGetItemNodes;
        std::vector<std::pair<std::string, std::string>> mappedTalkCountBranchNodes;
        size_t buyShopNodeIndex = 0;
        size_t sellShopNodeIndex = 0;
        size_t buyPalShopNodeIndex = 0;
        size_t sellPalShopNodeIndex = 0;
        size_t getItemNodeIndex = 0;
        size_t talkCountBranchNodeIndex = 0;
        size_t choiceNodeIndex = 0;
        size_t messageNodeIndex = 0;
        for (size_t i = 0; i < logicalOrder.size(); ++i)
        {
            const auto& logicalId = logicalOrder[i];
            const auto& typeStr = logicalNodeType.at(logicalId);

            if (typeStr == "ItemShopBuy")
            {
                if (buyShopNodeIndex < openItemShopBuyNodes.size())
                {
                    const auto& nodeName = openItemShopBuyNodes[buyShopNodeIndex++];
                    logicalToRuntimeNode[logicalId] = nodeName;
                    mappedBuyShopNodes.push_back(nodeName);
                }
                continue;
            }

            if (typeStr == "ItemShopSell")
            {
                if (sellShopNodeIndex < openItemShopSellNodes.size())
                {
                    const auto& nodeName = openItemShopSellNodes[sellShopNodeIndex++];
                    logicalToRuntimeNode[logicalId] = nodeName;
                    mappedSellShopNodes.push_back(nodeName);
                }
                continue;
            }

            if (typeStr == "PalShopBuy")
            {
                if (buyPalShopNodeIndex < openPalShopBuyNodes.size())
                {
                    const auto& nodeName = openPalShopBuyNodes[buyPalShopNodeIndex++];
                    logicalToRuntimeNode[logicalId] = nodeName;
                    mappedBuyPalShopNodes.push_back(nodeName);
                }
                continue;
            }

            if (typeStr == "PalShopSell")
            {
                if (sellPalShopNodeIndex < openPalShopSellNodes.size())
                {
                    const auto& nodeName = openPalShopSellNodes[sellPalShopNodeIndex++];
                    logicalToRuntimeNode[logicalId] = nodeName;
                    mappedSellPalShopNodes.push_back(nodeName);
                }
                continue;
            }

            if (typeStr == "GetItem")
            {
                if (getItemNodeIndex < getItemNodes.size())
                {
                    const auto& nodeName = getItemNodes[getItemNodeIndex++];
                    logicalToRuntimeNode[logicalId] = nodeName;
                    mappedGetItemNodes.emplace_back(logicalId, nodeName);
                }
                continue;
            }

            if (typeStr == "TalkCountBranch")
            {
                if (talkCountBranchNodeIndex < talkCountBranchNodes.size())
                {
                    const auto& nodeName = talkCountBranchNodes[talkCountBranchNodeIndex++];
                    logicalToRuntimeNode[logicalId] = nodeName;
                    mappedTalkCountBranchNodes.emplace_back(logicalId, nodeName);
                }
                continue;
            }

            if (messageNodeIndex >= messageNodes.size())
            {
                continue;
            }

            logicalToMessageNode[logicalId] = messageNodes[messageNodeIndex++];
            logicalToRuntimeNode[logicalId] = logicalToMessageNode[logicalId];
            if (logicalNeedsChoiceNode[logicalId])
            {
                if (choiceNodeIndex < choiceNodes.size())
                {
                    logicalToChoiceNode[logicalId] = choiceNodes[choiceNodeIndex++];
                }
            }
        }

        auto patch = nlohmann::json::object();
        patch["AssetPath"] = RC::to_string(AssetPath);
        patch["Nodes"] = nlohmann::json::object();
        patch["Text"] = nlohmann::json::object();
        patch["Buttons"] = nlohmann::json::object();

        auto& nodePatches = patch["Nodes"];
        auto& textEntries = patch["Text"];
        auto& buttonEntries = patch["Buttons"];
        int requiredFlowMaxTalkCount = 0;
        std::unordered_map<std::string, std::string> registeredTalkTextDefaults;

        auto registerTalkTextEntry = [&](nlohmann::json& targetEntries, const std::string& textId, const std::string& defaultText, const std::string& context)
        {
            auto existingDefaultIt = registeredTalkTextDefaults.find(textId);
            if (existingDefaultIt != registeredTalkTextDefaults.end())
            {
                if (existingDefaultIt->second != defaultText)
                {
                    throw std::runtime_error(std::format(
                        "Conversation '{}': TextID '{}' is used with conflicting default text while processing {}.",
                        OwnerId,
                        textId,
                        context
                    ));
                }
            }
            else
            {
                registeredTalkTextDefaults.emplace(textId, defaultText);
            }

            targetEntries[textId] = defaultText;
        };

        auto resolveNodeDefaultMsg = [&](const std::string& logicalId, const nlohmann::json& nodeDef) -> std::string
        {
            if (!nodeDef.contains("DefaultMsg"))
            {
                throw std::runtime_error(std::format(
                    "Conversation '{}': node '{}' must provide string DefaultMsg.",
                    OwnerId,
                    logicalId
                ));
            }

            if (!nodeDef.at("DefaultMsg").is_string())
            {
                throw std::runtime_error(std::format(
                    "Conversation '{}': node '{}' has non-string DefaultMsg.",
                    OwnerId,
                    logicalId
                ));
            }

            return nodeDef.at("DefaultMsg").get<std::string>();
        };

        auto resolveNodeTextId = [&](const std::string& logicalId, const nlohmann::json& nodeDef) -> std::string
        {
            if (nodeDef.contains("TextID"))
            {
                if (!nodeDef.at("TextID").is_string())
                {
                    throw std::runtime_error(std::format(
                        "Conversation '{}': node '{}' has non-string TextID.",
                        OwnerId,
                        logicalId
                    ));
                }

                return nodeDef.at("TextID").get<std::string>();
            }

            return std::format("{}_{}_MSG", ownerToken, SanitizeToken(logicalId));
        };

        auto resolveButtonDefaultText = [&](const std::string& logicalId, const std::string& buttonId, const nlohmann::json& buttonDef) -> std::string
        {
            if (!buttonDef.contains("DefaultText"))
            {
                throw std::runtime_error(std::format(
                    "Conversation '{}': button '{}' in node '{}' must provide string DefaultText.",
                    OwnerId,
                    buttonId,
                    logicalId
                ));
            }

            if (!buttonDef.at("DefaultText").is_string())
            {
                throw std::runtime_error(std::format(
                    "Conversation '{}': button '{}' in node '{}' has non-string DefaultText.",
                    OwnerId,
                    buttonId,
                    logicalId
                ));
            }

            return buttonDef.at("DefaultText").get<std::string>();
        };

        auto resolveButtonTextId = [&](const std::string& logicalId, const std::string& buttonId, const nlohmann::json& buttonDef) -> std::string
        {
            if (buttonDef.contains("TextID"))
            {
                if (!buttonDef.at("TextID").is_string())
                {
                    throw std::runtime_error(std::format(
                        "Conversation '{}': button '{}' in node '{}' has non-string TextID.",
                        OwnerId,
                        buttonId,
                        logicalId
                    ));
                }

                return buttonDef.at("TextID").get<std::string>();
            }

            return std::format(
                "{}_{}_BTN_{}",
                ownerToken,
                SanitizeToken(logicalId),
                SanitizeToken(buttonId)
            );
        };

        for (const auto& nodeName : mappedBuyShopNodes)
        {
            nodePatches[nodeName] = {
                { "OpenItemShopTabType", "E_PalItemShopTabType::NewEnumerator0" }
            };
        }

        for (const auto& nodeName : mappedSellShopNodes)
        {
            nodePatches[nodeName] = {
                { "OpenItemShopTabType", "E_PalItemShopTabType::NewEnumerator1" }
            };
        }

        for (const auto& nodeName : mappedBuyPalShopNodes)
        {
            nodePatches[nodeName] = {
                { "OpenPalShopTabType", "E_PalItemShopTabType::NewEnumerator0" }
            };
        }

        for (const auto& nodeName : mappedSellPalShopNodes)
        {
            nodePatches[nodeName] = {
                { "OpenPalShopTabType", "E_PalItemShopTabType::NewEnumerator1" }
            };
        }

        for (const auto& [logicalId, nodeName] : mappedGetItemNodes)
        {
            const auto& nodeDef = ConversationNodes.at(logicalId);
            auto getItemPatch = nlohmann::json::object();

            if (nodeDef.contains("NetworkInvokeName"))
            {
                if (!nodeDef.at("NetworkInvokeName").is_string())
                {
                    throw std::runtime_error(std::format(
                        "Conversation '{}': GetItem node '{}' has non-string NetworkInvokeName.",
                        OwnerId,
                        logicalId
                    ));
                }

                getItemPatch["NetworkInvokeName"] = nodeDef.at("NetworkInvokeName").get<std::string>();
            }

            if (nodeDef.contains("bSaveNetworkInvoke") || nodeDef.contains("SaveNetworkInvoke"))
            {
                const auto saveKey = nodeDef.contains("bSaveNetworkInvoke") ? "bSaveNetworkInvoke" : "SaveNetworkInvoke";
                if (!nodeDef.at(saveKey).is_boolean())
                {
                    throw std::runtime_error(std::format(
                        "Conversation '{}': GetItem node '{}' has non-boolean {}.",
                        OwnerId,
                        logicalId,
                        saveKey
                    ));
                }

                getItemPatch["bSaveNetworkInvoke"] = nodeDef.at(saveKey).get<bool>();
            }

            if (nodeDef.contains("LotteryDataTable"))
            {
                if (!nodeDef.at("LotteryDataTable").is_string())
                {
                    throw std::runtime_error(std::format(
                        "Conversation '{}': GetItem node '{}' has non-string LotteryDataTable.",
                        OwnerId,
                        logicalId
                    ));
                }

                getItemPatch["LotteryDataTable"] = nodeDef.at("LotteryDataTable").get<std::string>();
            }

            if (nodeDef.contains("GetItemList"))
            {
                if (!nodeDef.at("GetItemList").is_array())
                {
                    throw std::runtime_error(std::format(
                        "Conversation '{}': GetItem node '{}' has non-array GetItemList.",
                        OwnerId,
                        logicalId
                    ));
                }

                auto normalizedGetItemList = nlohmann::json::array();
                for (const auto& itemEntry : nodeDef.at("GetItemList"))
                {
                    if (!itemEntry.is_object())
                    {
                        throw std::runtime_error(std::format(
                            "Conversation '{}': GetItem node '{}' contains non-object entry in GetItemList.",
                            OwnerId,
                            logicalId
                        ));
                    }

                    if (itemEntry.contains("ItemId") || itemEntry.contains("Count"))
                    {
                        if (!itemEntry.contains("ItemId") || !itemEntry.at("ItemId").is_string())
                        {
                            throw std::runtime_error(std::format(
                                "Conversation '{}': GetItem node '{}' entry is missing string ItemId.",
                                OwnerId,
                                logicalId
                            ));
                        }

                        if (!itemEntry.contains("Count") || !itemEntry.at("Count").is_number_integer())
                        {
                            throw std::runtime_error(std::format(
                                "Conversation '{}': GetItem node '{}' entry is missing integer Count.",
                                OwnerId,
                                logicalId
                            ));
                        }

                        normalizedGetItemList.push_back({
                            { "Key", { { "Key", itemEntry.at("ItemId").get<std::string>() } } },
                            { "Value", itemEntry.at("Count").get<int>() }
                        });
                        continue;
                    }

                    if (!itemEntry.contains("Key") || !itemEntry.contains("Value"))
                    {
                        throw std::runtime_error(std::format(
                            "Conversation '{}': GetItem node '{}' entry must use either ItemId/Count or Key/Value format.",
                            OwnerId,
                            logicalId
                        ));
                    }

                    normalizedGetItemList.push_back(itemEntry);
                }

                getItemPatch["GetItemList"] = std::move(normalizedGetItemList);
            }

            if (nodeDef.contains("LinkID"))
            {
                if (!nodeDef.at("LinkID").is_string())
                {
                    throw std::runtime_error(std::format(
                        "Conversation '{}': GetItem node '{}' has non-string LinkID.",
                        OwnerId,
                        logicalId
                    ));
                }

                const auto targetLogicalId = nodeDef.at("LinkID").get<std::string>();
                auto targetIt = logicalToRuntimeNode.find(targetLogicalId);
                if (targetIt == logicalToRuntimeNode.end())
                {
                    throw std::runtime_error(std::format(
                        "Conversation '{}': GetItem node '{}' LinkID '{}' does not exist.",
                        OwnerId,
                        logicalId,
                        targetLogicalId
                    ));
                }

                getItemPatch["Connections"] = nlohmann::json::array({
                    {
                        { "Key", "Out" },
                        { "Value", {
                            { "NodeName", targetIt->second },
                            { "PinName", "In" }
                        } }
                    }
                });
            }

            if (!getItemPatch.empty())
            {
                nodePatches[nodeName] = std::move(getItemPatch);
            }
        }

        for (const auto& [logicalId, nodeName] : mappedTalkCountBranchNodes)
        {
            const auto& nodeDef = ConversationNodes.at(logicalId);
            auto countBranchPatch = nlohmann::json::object();
            auto branchConnections = nlohmann::json::array();
            std::vector<std::string> configuredBranchPins;
            int maxTalkCount = 0;

            auto addBranchLink = [&](const char* pinName, const std::string& targetLogicalId)
            {
                auto targetIt = logicalToRuntimeNode.find(targetLogicalId);
                if (targetIt == logicalToRuntimeNode.end())
                {
                    throw std::runtime_error(std::format(
                        "Conversation '{}': TalkCountBranch node '{}' link '{}' references unknown node '{}'.",
                        OwnerId,
                        logicalId,
                        pinName,
                        targetLogicalId
                    ));
                }

                configuredBranchPins.emplace_back(pinName);

                const auto pinAsString = std::string(pinName);
                const auto isNumericPin = !pinAsString.empty() &&
                    std::all_of(pinAsString.begin(), pinAsString.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
                if (isNumericPin)
                {
                    maxTalkCount = std::max(maxTalkCount, std::stoi(pinAsString));
                }

                branchConnections.push_back({
                    { "Key", pinName },
                    { "Value", {
                        { "NodeName", targetIt->second },
                        { "PinName", "In" }
                    } }
                });
            };

            if (nodeDef.contains("Routes"))
            {
                if (!nodeDef.at("Routes").is_object())
                {
                    throw std::runtime_error(std::format(
                        "Conversation '{}': TalkCountBranch node '{}' has non-object Routes.",
                        OwnerId,
                        logicalId
                    ));
                }

                for (const auto& [pinName, targetLogicalIdValue] : nodeDef.at("Routes").items())
                {
                    if (!targetLogicalIdValue.is_string())
                    {
                        throw std::runtime_error(std::format(
                            "Conversation '{}': TalkCountBranch node '{}' route '{}' must be a string.",
                            OwnerId,
                            logicalId,
                            pinName
                        ));
                    }

                    addBranchLink(pinName.c_str(), targetLogicalIdValue.get<std::string>());
                }
            }
            else
            {
                if (!nodeDef.contains("FirstLinkID") || !nodeDef.at("FirstLinkID").is_string())
                {
                    throw std::runtime_error(std::format(
                        "Conversation '{}': TalkCountBranch node '{}' requires string FirstLinkID (or Routes object).",
                        OwnerId,
                        logicalId
                    ));
                }

                if (!nodeDef.contains("RepeatLinkID") || !nodeDef.at("RepeatLinkID").is_string())
                {
                    throw std::runtime_error(std::format(
                        "Conversation '{}': TalkCountBranch node '{}' requires string RepeatLinkID (or Routes object).",
                        OwnerId,
                        logicalId
                    ));
                }

                const auto firstLinkId = nodeDef.at("FirstLinkID").get<std::string>();
                const auto repeatLinkId = nodeDef.at("RepeatLinkID").get<std::string>();
                const auto secondLinkId = (nodeDef.contains("SecondLinkID") && nodeDef.at("SecondLinkID").is_string())
                    ? nodeDef.at("SecondLinkID").get<std::string>()
                    : repeatLinkId;

                addBranchLink("1", firstLinkId);
                addBranchLink("2", secondLinkId);
                addBranchLink("Loop", repeatLinkId);
            }

            if (!branchConnections.empty())
            {
                std::sort(configuredBranchPins.begin(), configuredBranchPins.end());
                configuredBranchPins.erase(std::unique(configuredBranchPins.begin(), configuredBranchPins.end()), configuredBranchPins.end());

                if (maxTalkCount > 0)
                {
                    requiredFlowMaxTalkCount = std::max(requiredFlowMaxTalkCount, maxTalkCount);
                }

                if (!configuredBranchPins.empty())
                {
                    countBranchPatch["OutputPins"] = nlohmann::json::array();
                    for (const auto& pin : configuredBranchPins)
                    {
                        countBranchPatch["OutputPins"].push_back({
                            { "PinName", pin },
                            { "PinFriendlyName", pin },
                            { "PinToolTip", "" }
                        });
                    }
                }

                countBranchPatch["Connections"] = std::move(branchConnections);
                nodePatches[nodeName] = std::move(countBranchPatch);
            }
        }

        if (requiredFlowMaxTalkCount > 0)
        {
            patch["AssetProperties"] = {
                { "MaxTalkCount", requiredFlowMaxTalkCount }
            };
        }

        std::optional<std::string> exitNodeName;

        auto resolveButtonTargetNode = [&](const std::string& logicalId, const std::string& buttonId, const nlohmann::json& buttonDef) -> std::optional<std::string>
        {
            if (buttonDef.contains("LinkID"))
            {
                if (!buttonDef.at("LinkID").is_string())
                {
                    throw std::runtime_error(std::format("Conversation '{}': Button '{}' in node '{}' has non-string LinkID.", OwnerId, buttonId, logicalId));
                }

                const auto targetLogicalId = buttonDef.at("LinkID").get<std::string>();
                auto targetIt = logicalToRuntimeNode.find(targetLogicalId);
                if (targetIt == logicalToRuntimeNode.end())
                {
                    throw std::runtime_error(std::format(
                        "Conversation '{}': LinkID '{}' from node '{}' does not exist.",
                        OwnerId,
                        targetLogicalId,
                        logicalId
                    ));
                }

                return targetIt->second;
            }

            if (buttonDef.contains("Action"))
            {
                if (!buttonDef.at("Action").is_string())
                {
                    throw std::runtime_error(std::format("Conversation '{}': Button '{}' in node '{}' has non-string Action.", OwnerId, buttonId, logicalId));
                }

                const auto action = buttonDef.at("Action").get<std::string>();
                if (action == "Shop")
                {
                    if (openItemShopBuyNodes.empty() && openItemShopNodes.empty())
                    {
                        throw std::runtime_error(std::format(
                            "Conversation '{}': Action=Shop requested but flow '{}' has no FNBP_OpenItemShop_C node.",
                            OwnerId,
                            RC::to_string(AssetPath)
                        ));
                    }

                    if (!openItemShopBuyNodes.empty())
                    {
                        return openItemShopBuyNodes.front();
                    }

                    return openItemShopNodes.front();
                }

                if (action == "Exit")
                {
                    if (!exitNodeName.has_value())
                    {
                        for (const auto& candidate : messageNodes)
                        {
                            const auto isAssignedConversationNode = std::find_if(
                                logicalToMessageNode.begin(),
                                logicalToMessageNode.end(),
                                [&](const auto& entry) { return entry.second == candidate; }
                            ) != logicalToMessageNode.end();

                            if (!isAssignedConversationNode)
                            {
                                exitNodeName = candidate;
                                break;
                            }
                        }

                        if (!exitNodeName.has_value())
                        {
                            PS::Log<LogLevel::Warning>(
                                STR("Conversation '{}': Action=Exit has no spare fixed-message node in '{}'. Leaving this exit path unconnected.\n"),
                                RC::to_generic_string(OwnerId),
                                AssetPath
                            );
                            return std::nullopt;
                        }

                        const auto exitMsgId = std::format("{}_EXIT", ownerToken);
                        registerTalkTextEntry(textEntries, exitMsgId, "See you.", std::format("exit node for '{}'", logicalId));
                        nodePatches[exitNodeName.value()] = {
                            { "MsgIdList", nlohmann::json::array({ exitMsgId }) }
                        };
                    }

                    return exitNodeName.value();
                }

                throw std::runtime_error(std::format(
                    "Conversation '{}': unsupported Action '{}' in node '{}'. Supported actions: Shop, Exit.",
                    OwnerId,
                    action,
                    logicalId
                ));
            }

            // No LinkID/Action explicitly provided: default to Exit behavior.
            if (!exitNodeName.has_value())
            {
                for (const auto& candidate : messageNodes)
                {
                    const auto isAssignedConversationNode = std::find_if(
                        logicalToMessageNode.begin(),
                        logicalToMessageNode.end(),
                        [&](const auto& entry) { return entry.second == candidate; }
                    ) != logicalToMessageNode.end();

                    if (!isAssignedConversationNode)
                    {
                        exitNodeName = candidate;
                        break;
                    }
                }

                if (!exitNodeName.has_value())
                {
                    PS::Log<LogLevel::Warning>(
                        STR("Conversation '{}': implicit Exit for button '{}' in node '{}' has no spare fixed-message node in '{}'. Leaving this exit path unconnected.\n"),
                        RC::to_generic_string(OwnerId),
                        RC::to_generic_string(buttonId),
                        RC::to_generic_string(logicalId),
                        AssetPath
                    );
                    return std::nullopt;
                }

                const auto exitMsgId = std::format("{}_EXIT", ownerToken);
                registerTalkTextEntry(textEntries, exitMsgId, "See you.", std::format("implicit exit node for '{}'", logicalId));
                nodePatches[exitNodeName.value()] = {
                    { "MsgIdList", nlohmann::json::array({ exitMsgId }) }
                };
            }

            return exitNodeName.value();
        };

        for (size_t i = 0; i < logicalOrder.size(); ++i)
        {
            const auto& logicalId = logicalOrder[i];
            const auto& nodeDef = ConversationNodes.at(logicalId);
            const auto& nodeType = logicalNodeType.at(logicalId);

            if (nodeType == "ItemShopBuy" || nodeType == "ItemShopSell" || nodeType == "PalShopBuy" || nodeType == "PalShopSell" || nodeType == "GetItem" || nodeType == "TalkCountBranch")
            {
                // Pure target nodes: their routing is provided by incoming LinkID edges.
                continue;
            }

            const auto msgId = resolveNodeTextId(logicalId, nodeDef);
            registerTalkTextEntry(textEntries, msgId, resolveNodeDefaultMsg(logicalId, nodeDef), std::format("node '{}'", logicalId));

            auto messageNodeIt = logicalToMessageNode.find(logicalId);
            if (messageNodeIt == logicalToMessageNode.end())
            {
                PS::Log<LogLevel::Warning>(
                    STR("Conversation '{}': no message node could be allocated for logical node '{}'.\n"),
                    RC::to_generic_string(OwnerId),
                    RC::to_generic_string(logicalId)
                );
                continue;
            }

            const auto& messageNodeName = messageNodeIt->second;
            const auto needsChoiceNode = logicalNeedsChoiceNode.at(logicalId);

            auto messagePatch = nlohmann::json::object();
            messagePatch["MsgIdList"] = nlohmann::json::array({ msgId });

            if (!needsChoiceNode)
            {
                // Determine the single outgoing connection for this fixed-message node.
                // Priority: node-level LinkID > single-button > terminal (no connection)
                std::optional<std::string> targetNodeName;
                if (nodeDef.contains("LinkID") && nodeDef.at("LinkID").is_string())
                {
                    const auto targetLogicalId = nodeDef.at("LinkID").get<std::string>();
                    auto targetIt = logicalToRuntimeNode.find(targetLogicalId);
                    if (targetIt == logicalToRuntimeNode.end())
                    {
                        throw std::runtime_error(std::format(
                            "Conversation '{}': node-level LinkID '{}' in node '{}' does not exist.",
                            OwnerId, targetLogicalId, logicalId));
                    }
                    targetNodeName = targetIt->second;
                }
                else if (nodeDef.contains("Buttons") && nodeDef.at("Buttons").is_object() && !nodeDef.at("Buttons").empty())
                {
                    auto buttonIt = nodeDef.at("Buttons").items().begin();
                    targetNodeName = resolveButtonTargetNode(logicalId, buttonIt.key(), buttonIt.value());
                }
                // else: terminal node — message plays and conversation ends, no connection patch

                if (targetNodeName.has_value())
                {
                    messagePatch["Connections"] = nlohmann::json::array({
                        {
                            { "Key", "Out" },
                            { "Value", {
                                { "NodeName", targetNodeName.value() },
                                { "PinName", "In" }
                            } }
                        }
                    });
                }
                nodePatches[messageNodeName] = std::move(messagePatch);
                continue;
            }

            auto choiceNodeIt = logicalToChoiceNode.find(logicalId);
            if (choiceNodeIt == logicalToChoiceNode.end())
            {
                PS::Log<LogLevel::Warning>(
                    STR("Conversation '{}': no choice node could be allocated for logical node '{}'.\n"),
                    RC::to_generic_string(OwnerId),
                    RC::to_generic_string(logicalId)
                );
                nodePatches[messageNodeName] = std::move(messagePatch);
                continue;
            }

            const auto& choiceNodeName = choiceNodeIt->second;
            messagePatch["Connections"] = nlohmann::json::array({
                {
                    { "Key", "Out" },
                    { "Value", {
                        { "NodeName", choiceNodeName },
                        { "PinName", "In" }
                    } }
                }
            });
            nodePatches[messageNodeName] = std::move(messagePatch);

            auto choicePatch = nlohmann::json::object();
            choicePatch["ChoiceMsgIDList"] = nlohmann::json::array();
            choicePatch["OutputPins"] = nlohmann::json::array();
            choicePatch["Connections"] = nlohmann::json::array();

            for (const auto& [buttonId, buttonDef] : nodeDef.at("Buttons").items())
            {
                const auto buttonMsgId = resolveButtonTextId(logicalId, buttonId, buttonDef);
                registerTalkTextEntry(buttonEntries, buttonMsgId, resolveButtonDefaultText(logicalId, buttonId, buttonDef), std::format("button '{}' in node '{}'", buttonId, logicalId));
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

        if (hasStartNode)
        {
            auto entryIt = logicalToRuntimeNode.find(logicalOrder.front());
            if (entryIt != logicalToRuntimeNode.end())
            {
                const auto& entryMessageNode = entryIt->second;
                nodePatches[startNodes.front()] = {
                    { "Connections", nlohmann::json::array({
                        {
                            { "Key", "Out" },
                            { "Value", {
                                { "NodeName", entryMessageNode },
                                { "PinName", "In" }
                            } }
                        }
                    }) }
                };
            }
            else
            {
                PS::Log<LogLevel::Warning>(
                    STR("Conversation '{}': unable to resolve runtime entry node for '{}'.\n"),
                    RC::to_generic_string(OwnerId),
                    RC::to_generic_string(logicalOrder.front())
                );
            }
        }

        return patch;
    }

    void PalTalkFlowModLoader::ApplyFlowPatches(const nlohmann::json& FlowPatches)
    {
        if (!FlowPatches.is_array())
        {
            throw std::runtime_error("FlowPatches must be an array.");
        }

        if (!m_flowNodeClass)
        {
            throw std::runtime_error("Flow patching requires /Script/Flow.FlowNode, but it could not be found.");
        }

        for (const auto& Patch : FlowPatches)
        {
            if (!ApplySingleFlowPatch(Patch, false))
            {
                QueueFlowPatchRetry(Patch, false);
            }
        }
    }

    bool PalTalkFlowModLoader::ApplySingleFlowPatch(const nlohmann::json& Patch, bool SkipVanillaGuard)
    {
        if (!Patch.is_object())
        {
            throw std::runtime_error("Each FlowPatches entry must be an object.");
        }

        if (!Patch.contains("AssetPath") || !Patch.at("AssetPath").is_string())
        {
            throw std::runtime_error("Each FlowPatches entry requires string field AssetPath.");
        }

        if (!Patch.contains("Nodes") || !Patch.at("Nodes").is_object())
        {
            throw std::runtime_error("Each FlowPatches entry requires object field Nodes.");
        }

        if (Patch.contains("AssetProperties") && !Patch.at("AssetProperties").is_object())
        {
            throw std::runtime_error("FlowPatches.AssetProperties must be an object when provided.");
        }

        auto AssetPath = RC::to_generic_string(Patch.at("AssetPath").get<std::string>());

        bool allowGlobalPatch = false;
        if (Patch.contains("AllowGlobalPatch"))
        {
            if (!Patch.at("AllowGlobalPatch").is_boolean())
            {
                throw std::runtime_error("FlowPatches.AllowGlobalPatch must be a boolean.");
            }

            allowGlobalPatch = Patch.at("AllowGlobalPatch").get<bool>();
        }

        if (!SkipVanillaGuard && AssetPath.rfind(constants::vanillaAssetPrefix, 0) == 0 && !allowGlobalPatch)
        {
            throw std::runtime_error(
                std::format(
                    "Refusing to patch vanilla flow asset '{}' without explicit opt-in. "
                    "Set AllowGlobalPatch=true only if you intentionally want to affect all NPCs using that asset.",
                    RC::to_string(AssetPath)
                )
            );
        }

        auto FlowAsset = FindTalkFlowAsset(AssetPath);
        if (!FlowAsset)
        {
            return false;
        }

        if (Patch.contains("AssetProperties"))
        {
            for (auto& [PropertyName, PropertyValue] : Patch.at("AssetProperties").items())
            {
                auto PropertyNameWide = RC::to_generic_string(PropertyName);
                auto* Property = FlowAsset->GetPropertyByNameInChain(PropertyNameWide.c_str());
                if (!Property)
                {
                    Property = Palworld::PropertyHelper::GetPropertyByName(FlowAsset->GetClassPrivate(), PropertyNameWide);
                }

                if (!Property)
                {
                    PS::Log<LogLevel::Warning>(STR("Flow patch skipped unknown asset property {} on {}\n"), PropertyNameWide, FlowAsset->GetName());
                    continue;
                }

                try
                {
                    Palworld::PropertyHelper::CopyJsonValueToContainer(FlowAsset, Property, PropertyValue);
                }
                catch (const std::exception& e)
                {
                    PS::Log<LogLevel::Warning>(
                        STR("Flow patch skipped invalid asset property {} on {}: {}\n"),
                        PropertyNameWide,
                        FlowAsset->GetName(),
                        RC::to_generic_string(e.what())
                    );
                }
            }
        }

        TArray<UObject*> FlowNodeObjects;
        UECustom::UObjectGlobals::GetObjectsOfClass(m_flowNodeClass, FlowNodeObjects, true, EObjectFlags::RF_ClassDefaultObject, EInternalObjectFlags::None);

        std::unordered_map<std::string, UObject*> NodesByName;
        std::unordered_map<std::string, std::vector<UObject*>> NodesByNormalizedName;
        std::unordered_map<std::string, std::vector<UObject*>> NodesByNormalizedClassName;

        auto collectNodesForOuter = [&](UObject* targetOuter)
        {
            for (auto* NodeObject : FlowNodeObjects)
            {
                if (!NodeObject) continue;
                if (NodeObject->GetOuterPrivate() != targetOuter) continue;

                auto nodeName = NodeObject->GetName();
                auto nodeNameNarrow = RC::to_string(nodeName);
                NodesByName.emplace(nodeNameNarrow, NodeObject);

                auto normalizedNodeName = NormalizeNodeName(nodeName);
                NodesByNormalizedName[RC::to_string(normalizedNodeName)].push_back(NodeObject);

                auto className = NodeObject->GetClassPrivate() ? NodeObject->GetClassPrivate()->GetName() : RC::StringType{};
                if (!className.empty())
                {
                    auto normalizedClassName = NormalizeNodeName(className);
                    NodesByNormalizedClassName[RC::to_string(normalizedClassName)].push_back(NodeObject);
                }
            }
        };

        collectNodesForOuter(FlowAsset);

        if (NodesByName.empty())
        {
            const auto sourceOuterName = ExtractPackageAssetName(AssetPath);
            for (auto* NodeObject : FlowNodeObjects)
            {
                if (!NodeObject)
                {
                    continue;
                }

                auto* outer = NodeObject->GetOuterPrivate();
                if (!outer)
                {
                    continue;
                }

                if (RC::to_string(outer->GetName()) != sourceOuterName)
                {
                    continue;
                }

                auto nodeName = NodeObject->GetName();
                auto nodeNameNarrow = RC::to_string(nodeName);
                NodesByName.emplace(nodeNameNarrow, NodeObject);

                auto normalizedNodeName = NormalizeNodeName(nodeName);
                NodesByNormalizedName[RC::to_string(normalizedNodeName)].push_back(NodeObject);

                auto className = NodeObject->GetClassPrivate() ? NodeObject->GetClassPrivate()->GetName() : RC::StringType{};
                if (!className.empty())
                {
                    auto normalizedClassName = NormalizeNodeName(className);
                    NodesByNormalizedClassName[RC::to_string(normalizedClassName)].push_back(NodeObject);
                }
            }

            if (!NodesByName.empty())
            {
                PS::Log<LogLevel::Warning>(
                    STR("Flow patch fallback: '{}' has no owned nodes; patching source graph nodes from '{}'.\n"),
                    AssetPath,
                    RC::to_generic_string(sourceOuterName)
                );
            }
        }

        for (auto& [NodeName, NodePatch] : Patch.at("Nodes").items())
        {
            auto resolvedNodePatch = NodePatch;
            ResolveConnectionNodeNames(resolvedNodePatch, NodesByName);

            auto NodeIt = NodesByName.find(NodeName);
            if (NodeIt != NodesByName.end())
            {
                ApplyNodePatch(NodeIt->second, resolvedNodePatch);
                continue;
            }

            auto normalizedPatchNodeName = RC::to_string(NormalizeNodeName(RC::to_generic_string(NodeName)));
            auto normalizedIt = NodesByNormalizedName.find(normalizedPatchNodeName);
            if (normalizedIt != NodesByNormalizedName.end() && !normalizedIt->second.empty())
            {
                auto* selectedNode = normalizedIt->second.front();
                if (normalizedIt->second.size() > 1)
                {
                    PS::Log<LogLevel::Warning>(
                        STR("Flow patch node {} matched {} runtime nodes in asset {} by normalized name. Applying to {}.\n"),
                        RC::to_generic_string(NodeName),
                        static_cast<int>(normalizedIt->second.size()),
                        AssetPath,
                        selectedNode->GetName()
                    );
                }

                ApplyNodePatch(selectedNode, resolvedNodePatch);
                continue;
            }

            auto normalizedClassIt = NodesByNormalizedClassName.find(normalizedPatchNodeName);
            if (normalizedClassIt != NodesByNormalizedClassName.end() && !normalizedClassIt->second.empty())
            {
                auto* selectedNode = normalizedClassIt->second.front();
                if (normalizedClassIt->second.size() > 1)
                {
                    PS::Log<LogLevel::Warning>(
                        STR("Flow patch node {} matched {} runtime nodes in asset {} by normalized class name. Applying to {}.\n"),
                        RC::to_generic_string(NodeName),
                        static_cast<int>(normalizedClassIt->second.size()),
                        AssetPath,
                        selectedNode->GetName()
                    );
                }

                ApplyNodePatch(selectedNode, resolvedNodePatch);
                continue;
            }

            PS::Log<LogLevel::Warning>(STR("Flow patch skipped node {} in asset {} (node not found by name).\n"), RC::to_generic_string(NodeName), AssetPath);
        }

        if (Patch.contains("Text"))
        {
            if (!Patch.at("Text").is_object())
            {
                throw std::runtime_error("FlowPatches.Text must be an object mapping MsgId -> text.");
            }

            AddOrEditTalkText(Patch.at("Text"));
        }

        if (Patch.contains("Buttons"))
        {
            if (!Patch.at("Buttons").is_object())
            {
                throw std::runtime_error("FlowPatches.Buttons must be an object mapping MsgId -> text.");
            }

            AddOrEditTalkText(Patch.at("Buttons"));
        }

        return true;
    }

    void PalTalkFlowModLoader::ApplyNodePatch(UObject* NodeObject, const nlohmann::json& NodePatch)
    {
        if (!NodePatch.is_object())
        {
            throw std::runtime_error("Node patch must be an object.");
        }

        auto normalizedNodePatch = NodePatch;
        NormalizeNodePatchForEngineTypes(normalizedNodePatch);

        const nlohmann::json* PropertiesToApply = &normalizedNodePatch;
        if (normalizedNodePatch.contains("Properties"))
        {
            if (!normalizedNodePatch.at("Properties").is_object())
            {
                throw std::runtime_error("Node patch field Properties must be an object.");
            }

            PropertiesToApply = &normalizedNodePatch.at("Properties");
        }

        for (auto& [PropertyName, PropertyValue] : PropertiesToApply->items())
        {
            auto PropertyNameWide = RC::to_generic_string(PropertyName);
            auto* Property = NodeObject->GetPropertyByNameInChain(PropertyNameWide.c_str());
            if (!Property)
            {
                Property = Palworld::PropertyHelper::GetPropertyByName(NodeObject->GetClassPrivate(), PropertyNameWide);
            }

            if (!Property)
            {
                PS::Log<LogLevel::Warning>(STR("Node patch skipped unknown property {} on {}\n"), PropertyNameWide, NodeObject->GetName());
                continue;
            }

            try
            {
                Palworld::PropertyHelper::CopyJsonValueToContainer(NodeObject, Property, PropertyValue);
            }
            catch (const std::exception& e)
            {
                PS::Log<LogLevel::Warning>(
                    STR("Node patch skipped invalid property {} on {}: {}\n"),
                    PropertyNameWide,
                    NodeObject->GetName(),
                    RC::to_generic_string(e.what())
                );
                continue;
            }
        }

    }

    void PalTalkFlowModLoader::QueueFlowPatchRetry(const nlohmann::json& Patch, bool SkipVanillaGuard)
    {
        auto wrapper = nlohmann::json::object();
        wrapper["Patch"] = Patch;
        wrapper["SkipVanillaGuard"] = SkipVanillaGuard;

        auto wrapperDump = wrapper.dump();
        auto alreadyQueued = std::find_if(
            m_pendingFlowPatches.begin(),
            m_pendingFlowPatches.end(),
            [&](const nlohmann::json& pending)
            {
                return pending.dump() == wrapperDump;
            }
        ) != m_pendingFlowPatches.end();

        if (!alreadyQueued)
        {
            m_pendingFlowPatches.push_back(std::move(wrapper));
            if (Patch.contains("AssetPath") && Patch.at("AssetPath").is_string())
            {
                PS::Log<LogLevel::Warning>(
                    STR("Flow patch target not loaded yet for {}. Queued for retry.\n"),
                    RC::to_generic_string(Patch.at("AssetPath").get<std::string>())
                );
            }
            else
            {
                PS::Log<LogLevel::Warning>(STR("Flow patch target not loaded yet. Queued for retry.\n"));
            }
        }
    }

    UObject* PalTalkFlowModLoader::FindTalkFlowAsset(const RC::StringType& AssetPath) const
    {
        std::vector<RC::StringType> candidates;
        AddTalkFlowPathCandidates(AssetPath, candidates);
        AddTalkFlowCandidatesFromDataTable(AssetPath, candidates);

        for (const auto& candidate : candidates)
        {
            auto foundAsset = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, candidate.c_str());
            if (foundAsset)
            {
                return foundAsset;
            }
        }

        if (m_flowNodeClass)
        {
            auto expectedShortName = AssetPath;
            auto expectedDotIndex = expectedShortName.find_last_of('.');
            if (expectedDotIndex != RC::StringType::npos)
            {
                expectedShortName = expectedShortName.substr(expectedDotIndex + 1);
            }
            else
            {
                auto expectedSlashIndex = expectedShortName.find_last_of('/');
                if (expectedSlashIndex != RC::StringType::npos)
                {
                    expectedShortName = expectedShortName.substr(expectedSlashIndex + 1);
                }
            }

            if (expectedShortName.size() > 2 && expectedShortName.ends_with(STR("_C")))
            {
                expectedShortName = expectedShortName.substr(0, expectedShortName.size() - 2);
            }

            TArray<UObject*> flowNodeObjects;
            UECustom::UObjectGlobals::GetObjectsOfClass(m_flowNodeClass, flowNodeObjects, true, EObjectFlags::RF_ClassDefaultObject, EInternalObjectFlags::None);

            for (auto* nodeObject : flowNodeObjects)
            {
                if (!nodeObject) continue;
                auto* outer = nodeObject->GetOuterPrivate();
                if (!outer) continue;

                auto outerName = outer->GetName();
                auto outerNameNoClass = outerName;
                if (outerNameNoClass.size() > 2 && outerNameNoClass.ends_with(STR("_C")))
                {
                    outerNameNoClass = outerNameNoClass.substr(0, outerNameNoClass.size() - 2);
                }

                if (outerNameNoClass == expectedShortName)
                {
                    return outer;
                }
            }
        }

        return nullptr;
    }

    RC::StringType PalTalkFlowModLoader::CanonicalizeTalkFlowPath(const RC::StringType& TalkFlowPath) const
    {
        std::vector<RC::StringType> candidates;
        AddTalkFlowPathCandidates(TalkFlowPath, candidates);
        AddTalkFlowCandidatesFromDataTable(TalkFlowPath, candidates);

        for (const auto& candidate : candidates)
        {
            auto foundAsset = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, candidate.c_str());
            if (foundAsset)
            {
                return candidate;
            }
        }

        if (!candidates.empty())
        {
            return candidates.front();
        }

        return TalkFlowPath;
    }

    void PalTalkFlowModLoader::AddTalkFlowPathCandidates(const RC::StringType& TalkFlowPath, std::vector<RC::StringType>& Candidates) const
    {
        auto addCandidate = [&Candidates](const RC::StringType& candidate)
        {
            if (candidate.empty()) return;
            if (std::find(Candidates.begin(), Candidates.end(), candidate) != Candidates.end()) return;
            Candidates.emplace_back(candidate);
        };

        addCandidate(TalkFlowPath);

        auto dotIndex = TalkFlowPath.find_last_of('.');
        if (dotIndex != RC::StringType::npos)
        {
            auto packagePath = TalkFlowPath.substr(0, dotIndex);
            auto objectName = TalkFlowPath.substr(dotIndex + 1);

            if (objectName.size() > 2 && objectName.ends_with(STR("_C")))
            {
                addCandidate(std::format(STR("{}.{}"), packagePath, objectName.substr(0, objectName.size() - 2)));
            }
            else
            {
                addCandidate(std::format(STR("{}.{}_C"), packagePath, objectName));
            }
        }
        else if (TalkFlowPath.rfind(STR("/"), 0) == 0)
        {
            auto slashIndex = TalkFlowPath.find_last_of('/');
            auto objectName = slashIndex == RC::StringType::npos ? TalkFlowPath : TalkFlowPath.substr(slashIndex + 1);
            if (!objectName.empty())
            {
                addCandidate(std::format(STR("{}.{}"), TalkFlowPath, objectName));
                addCandidate(std::format(STR("{}.{}_C"), TalkFlowPath, objectName));
            }
        }
    }

    void PalTalkFlowModLoader::AddTalkFlowCandidatesFromDataTable(const RC::StringType& TalkFlowPath, std::vector<RC::StringType>& Candidates) const
    {
        if (!m_npcTalkFlowTable)
        {
            return;
        }

        auto expectedShortName = TalkFlowPath;
        auto expectedDotIndex = expectedShortName.find_last_of('.');
        if (expectedDotIndex != RC::StringType::npos)
        {
            expectedShortName = expectedShortName.substr(expectedDotIndex + 1);
        }
        else
        {
            auto expectedSlashIndex = expectedShortName.find_last_of('/');
            if (expectedSlashIndex != RC::StringType::npos)
            {
                expectedShortName = expectedShortName.substr(expectedSlashIndex + 1);
            }
        }

        if (expectedShortName.size() > 2 && expectedShortName.ends_with(STR("_C")))
        {
            expectedShortName = expectedShortName.substr(0, expectedShortName.size() - 2);
        }

        auto& rowMap = m_npcTalkFlowTable->GetRowMap();
        for (auto& [rowName, rowData] : rowMap)
        {
            if (!rowData) continue;

            auto* row = std::bit_cast<FPalNPCTalkFlowClassDataRow*>(rowData);
            auto softPath = row->NPCTalkFlowClass.ToSoftObjectPath();

            auto topLevelPath = softPath.GetAssetPath();
            auto packageName = topLevelPath.GetPackageName().ToString();
            auto assetName = topLevelPath.GetAssetName().ToString();
            if (packageName.empty() || assetName.empty())
            {
                continue;
            }

            auto assetNameNoClass = assetName;
            if (assetNameNoClass.size() > 2 && assetNameNoClass.ends_with(STR("_C")))
            {
                assetNameNoClass = assetNameNoClass.substr(0, assetNameNoClass.size() - 2);
            }

            if (assetNameNoClass != expectedShortName)
            {
                continue;
            }

            AddTalkFlowPathCandidates(std::format(STR("{}.{}"), packageName, assetName), Candidates);
        }
    }
}
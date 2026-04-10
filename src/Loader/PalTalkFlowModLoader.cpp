#include "Unreal/Engine/UDataTable.hpp"
#include "Unreal/UObjectGlobals.hpp"
#include "SDK/Classes/Custom/UObjectGlobals.h"
#include "SDK/Structs/FPalNPCTalkFlowClassDataRow.h"
#include "SDK/Structs/FPalLocalizedTextData.h"
#include "SDK/Helper/PropertyHelper.h"
#include "Loader/PalTalkFlowModLoader.h"
#include "Loader/TalkFlow/ConversationPatchCompiler.h"
#include "Loader/TalkFlow/FlowPatchApplier.h"
#include "Loader/TalkFlow/TalkFlowPendingWorkProcessor.h"
#include "Loader/TalkFlow/TalkFlowAssetResolver.h"
#include "Utility/Logging.h"
#include "Utility/JsonHelpers.h"
#include "Helpers/String.hpp"
#include <algorithm>
#include <format>
#include <optional>
#include <stdexcept>
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
        if (!m_npcTalkFlowTable || !m_flowNodeClass)
        {
            return;
        }

        if (m_npcTalkFlowTable->IsUnreachable() || m_flowNodeClass->IsUnreachable())
        {
            return;
        }

        TalkFlow::TalkFlowPendingWorkProcessorContext context{};
        context.resolveCloneAssignment = [this](const TalkFlow::PendingCloneAssignment& pending)
        {
            TalkFlowCloneRequest cloneRequest{};
            cloneRequest.CharacterId = pending.characterId;
            cloneRequest.SourceAssetPath = pending.sourceAssetPath;
            cloneRequest.ForceRebuild = pending.forceRebuild;
            return m_talkFlowCloneManager.ResolveTalkFlowAssetPath(cloneRequest);
        };
        context.assignTalkFlowToNpc = [this](const RC::StringType& characterId, const RC::StringType& talkFlowPath)
        {
            AssignTalkFlowToNpc(characterId, talkFlowPath);
        };
        context.applySingleFlowPatch = [this](const nlohmann::json& patch, bool skipVanillaGuard)
        {
            return ApplySingleFlowPatch(patch, skipVanillaGuard);
        };

        TalkFlow::TalkFlowPendingWorkProcessor::ProcessPending(
            m_pendingCloneAssignments,
            m_pendingFlowPatches,
            m_isProcessingPending,
            m_processPendingRequested,
            context);
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

    }

    RC::StringType PalTalkFlowModLoader::ResolveAndAssignClonedTalkFlow(const RC::StringType& CharacterIdString, const RC::StringType& TalkFlowPath, bool ForceRebuildClone)
    {
        auto canonicalTalkFlowPath = TalkFlow::TalkFlowAssetResolver::CanonicalizeTalkFlowPath(TalkFlowPath, m_flowNodeClass, m_npcTalkFlowTable);
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
                    return pending.characterId == CharacterIdString && pending.sourceAssetPath == canonicalTalkFlowPath;
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
        TalkFlow::ConversationPatchCompilerContext context{};
        context.flowNodeClass = m_flowNodeClass;
        context.cloneManager = &m_talkFlowCloneManager;
        context.findTalkFlowAsset = [this](const RC::StringType& path)
        {
            return TalkFlow::TalkFlowAssetResolver::FindTalkFlowAsset(path, m_flowNodeClass, m_npcTalkFlowTable);
        };

        return TalkFlow::ConversationPatchCompiler::BuildConversationPatchFromSchema(
            OwnerId,
            ConversationNodes,
            AssetPath,
            PreferredStartNode,
            context);
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
        TalkFlow::FlowPatchApplierContext context{};
        context.flowNodeClass = m_flowNodeClass;
        context.npcTalkFlowTable = m_npcTalkFlowTable;
        context.applyNodePatch = [this](UObject* nodeObject, const nlohmann::json& nodePatch)
        {
            ApplyNodePatch(nodeObject, nodePatch);
        };
        context.addOrEditTalkText = [this](const nlohmann::json& textEntries)
        {
            AddOrEditTalkText(textEntries);
        };

        return TalkFlow::FlowPatchApplier::ApplySingleFlowPatch(Patch, SkipVanillaGuard, context);
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
        TalkFlow::TalkFlowPendingWorkProcessor::QueueFlowPatchRetry(m_pendingFlowPatches, Patch, SkipVanillaGuard);
    }
}
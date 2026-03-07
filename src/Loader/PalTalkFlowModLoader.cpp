#include "Unreal/Engine/UDataTable.hpp"
#include "Unreal/UObjectGlobals.hpp"
#include "SDK/Classes/Custom/UObjectGlobals.h"
#include "SDK/Structs/FPalNPCTalkFlowClassDataRow.h"
#include "SDK/Structs/FPalLocalizedTextData.h"
#include "SDK/Helper/PropertyHelper.h"
#include "Loader/PalTalkFlowModLoader.h"
#include "Utility/Logging.h"
#include "Helpers/String.hpp"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <format>
#include <unordered_map>
#include <vector>

using namespace RC;
using namespace RC::Unreal;

namespace Palworld {
    namespace {
        bool TryParseHexUint32(const std::string& text, uint32_t& outValue)
        {
            if (text.empty() || text.size() > 8)
            {
                return false;
            }

            uint32_t value = 0;
            for (char c : text)
            {
                value <<= 4;
                if (c >= '0' && c <= '9')
                {
                    value |= static_cast<uint32_t>(c - '0');
                }
                else if (c >= 'a' && c <= 'f')
                {
                    value |= static_cast<uint32_t>(c - 'a' + 10);
                }
                else if (c >= 'A' && c <= 'F')
                {
                    value |= static_cast<uint32_t>(c - 'A' + 10);
                }
                else
                {
                    return false;
                }
            }

            outValue = value;
            return true;
        }

        bool TryConvertGuidStringToObject(const std::string& guidText, nlohmann::json& outGuidObject)
        {
            std::array<std::string, 4> segments{};
            size_t start = 0;
            size_t segmentIndex = 0;
            while (segmentIndex < segments.size())
            {
                auto end = guidText.find('-', start);
                if (end == std::string::npos)
                {
                    end = guidText.size();
                }

                segments[segmentIndex] = guidText.substr(start, end - start);
                ++segmentIndex;

                if (end == guidText.size())
                {
                    break;
                }

                start = end + 1;
            }

            if (segmentIndex != 4 || start < guidText.size())
            {
                return false;
            }

            uint32_t values[4]{};
            for (size_t i = 0; i < 4; ++i)
            {
                if (!TryParseHexUint32(segments[i], values[i]))
                {
                    return false;
                }
            }

            outGuidObject = nlohmann::json::object();
            outGuidObject["A"] = static_cast<uint64_t>(values[0]);
            outGuidObject["B"] = static_cast<uint64_t>(values[1]);
            outGuidObject["C"] = static_cast<uint64_t>(values[2]);
            outGuidObject["D"] = static_cast<uint64_t>(values[3]);
            return true;
        }

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

            // Compatibility: allow compact GUID string in Connections.Value.NodeGuid.
            if (NodePatch.contains("Connections") && NodePatch.at("Connections").is_array())
            {
                for (auto& connectionEntry : NodePatch.at("Connections"))
                {
                    if (!connectionEntry.is_object() || !connectionEntry.contains("Value"))
                    {
                        continue;
                    }

                    auto& valueEntry = connectionEntry["Value"];
                    if (!valueEntry.is_object() || !valueEntry.contains("NodeGuid"))
                    {
                        continue;
                    }

                    auto& nodeGuidEntry = valueEntry["NodeGuid"];
                    if (!nodeGuidEntry.is_string())
                    {
                        continue;
                    }

                    nlohmann::json guidObject{};
                    if (TryConvertGuidStringToObject(nodeGuidEntry.get<std::string>(), guidObject))
                    {
                        nodeGuidEntry = std::move(guidObject);
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
    }

    namespace constants {
        constexpr const TCHAR* defaultTalkFlowAssetPath = STR("/Game/Pal/Blueprint/FlowGraph/NPCTalkFlow/Graph/FABP_CommonBountyTrader.FABP_CommonBountyTrader");
        constexpr const TCHAR* vanillaAssetPrefix = STR("/Game/Pal/");
        constexpr bool defaultUseClone = true;
    }

    PalTalkFlowModLoader::PalTalkFlowModLoader() : PalModLoaderBase("talkflows") {}

    PalTalkFlowModLoader::~PalTalkFlowModLoader() {}

    void PalTalkFlowModLoader::Initialize()
    {
        m_flowNodeClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/Flow.FlowNode"));
        m_talkFlowCloneManager.Initialize(m_flowNodeClass);

        m_npcTalkFlowTable = UObjectGlobals::StaticFindObject<UDataTable*>(nullptr, nullptr,
            STR("/Game/Pal/Blueprint/Component/NPCTalk/DT_NPCTalkFlow.DT_NPCTalkFlow"));

        m_npcTalkTextTable = UObjectGlobals::StaticFindObject<UDataTable*>(nullptr, nullptr,
            STR("/Game/Pal/DataTable/Text/DT_NpcTalkText.DT_NpcTalkText"));
    }

    void PalTalkFlowModLoader::Load(const nlohmann::json& Data)
    {
        if (Data.contains("FlowPatches"))
        {
            ApplyFlowPatches(Data.at("FlowPatches"));
        }

        if (Data.contains("$flowPatches"))
        {
            ApplyFlowPatches(Data.at("$flowPatches"));
        }

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
            auto useClone = constants::defaultUseClone;
            RC::StringType resolvedTalkFlowPath{};

            if (RowData.is_string())
            {
                auto TalkFlowPath = RC::to_generic_string(RowData.get<std::string>());
                resolvedTalkFlowPath = AssignTalkFlowToNpcWithCloneOption(RC::to_generic_string(CharacterIdString), TalkFlowPath, useClone);
                didWork = true;
            }
            else if (RowData.is_object())
            {
                if (RowData.contains("UseClone"))
                {
                    if (!RowData.at("UseClone").is_boolean())
                    {
                        throw std::runtime_error(std::format("UseClone for '{}' must be a boolean.", CharacterIdString));
                    }

                    useClone = RowData.at("UseClone").get<bool>();
                }

                if (RowData.contains("TalkFlowAssetPath"))
                {
                    if (!RowData.at("TalkFlowAssetPath").is_string())
                    {
                        throw std::runtime_error(std::format("TalkFlowAssetPath for '{}' must be a string.", CharacterIdString));
                    }

                    auto TalkFlowPath = RC::to_generic_string(RowData.at("TalkFlowAssetPath").get<std::string>());
                    resolvedTalkFlowPath = AssignTalkFlowToNpcWithCloneOption(RC::to_generic_string(CharacterIdString), TalkFlowPath, useClone);
                    didWork = true;
                }

                if (!RowData.contains("TalkFlowAssetPath") && (RowData.contains("Text") || RowData.contains("Buttons")))
                {
                    resolvedTalkFlowPath = AssignTalkFlowToNpcWithCloneOption(
                        RC::to_generic_string(CharacterIdString),
                        constants::defaultTalkFlowAssetPath,
                        useClone
                    );
                    PS::Log<LogLevel::Normal>(
                        STR("TalkFlowAssetPath not provided for {}. Using default base flow {} (UseClone={})\n"),
                        RC::to_generic_string(CharacterIdString),
                        constants::defaultTalkFlowAssetPath,
                        useClone
                    );
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
                        throw std::runtime_error(std::format("Nodes for '{}' must be an object mapping NodeName -> patch.", CharacterIdString));
                    }

                    if (resolvedTalkFlowPath.empty())
                    {
                        resolvedTalkFlowPath = AssignTalkFlowToNpcWithCloneOption(
                            RC::to_generic_string(CharacterIdString),
                            constants::defaultTalkFlowAssetPath,
                            useClone
                        );
                    }

                    if (useClone && resolvedTalkFlowPath.rfind(constants::vanillaAssetPrefix, 0) == 0)
                    {
                        throw std::runtime_error(std::format(
                            "Failed to isolate talkflow for '{}'. Clone resolution returned vanilla path '{}'. "
                            "Refusing to patch nodes on shared asset.",
                            CharacterIdString,
                            RC::to_string(resolvedTalkFlowPath)
                        ));
                    }

                    auto perNpcPatch = nlohmann::json::object();
                    perNpcPatch["AssetPath"] = RC::to_string(resolvedTalkFlowPath);
                    perNpcPatch["Nodes"] = RowData.at("Nodes");
                    if (!ApplySingleFlowPatch(perNpcPatch, true))
                    {
                        QueueFlowPatchRetry(perNpcPatch, true);
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
                throw std::runtime_error(std::format("Talkflow entry '{}' did not contain supported fields. Use TalkFlowAssetPath and/or Text/Buttons.", CharacterIdString));
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
                        PS::Log<LogLevel::Normal>(
                            STR("Resolved deferred TalkFlow clone for {} -> {}\n"),
                            it->CharacterId,
                            resolvedTalkFlowPath
                        );
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
                        PS::Log<LogLevel::Normal>(STR("Resolved deferred flow patch for {}\n"), RC::to_generic_string(patch.at("AssetPath").get<std::string>()));
                        it = m_pendingFlowPatches.erase(it);
                        continue;
                    }

                    ++it;
                }
            }

        } while (m_processPendingRequested);
        m_isProcessingPending = false;
    }

    RC::StringType PalTalkFlowModLoader::AssignTalkFlowToNpcWithCloneOption(const RC::StringType& CharacterIdString, const RC::StringType& TalkFlowPath, bool UseClone, bool ForceRebuildClone)
    {
        auto canonicalTalkFlowPath = CanonicalizeTalkFlowPath(TalkFlowPath);
        auto resolvedTalkFlowPath = canonicalTalkFlowPath;
        if (UseClone)
        {
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

        PS::Log<LogLevel::Normal>(STR("Assigned talkflow {} to NPC {}\n"), TalkFlowPath, CharacterId.ToString());
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

        TArray<UObject*> FlowNodeObjects;
        UECustom::UObjectGlobals::GetObjectsOfClass(m_flowNodeClass, FlowNodeObjects, true, EObjectFlags::RF_ClassDefaultObject, EInternalObjectFlags::None);

        std::unordered_map<std::string, UObject*> NodesByName;
        std::unordered_map<std::string, std::vector<UObject*>> NodesByNormalizedName;
        std::unordered_map<std::string, std::vector<UObject*>> NodesByNormalizedClassName;
        for (auto* NodeObject : FlowNodeObjects)
        {
            if (!NodeObject) continue;
            if (NodeObject->GetOuterPrivate() != FlowAsset) continue;

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

        for (auto& [NodeName, NodePatch] : Patch.at("Nodes").items())
        {
            auto NodeIt = NodesByName.find(NodeName);
            if (NodeIt != NodesByName.end())
            {
                ApplyNodePatch(NodeIt->second, NodePatch);
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

                ApplyNodePatch(selectedNode, NodePatch);
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

                ApplyNodePatch(selectedNode, NodePatch);
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

        PS::Log<LogLevel::Normal>(STR("Applied flow patch to {}\n"), AssetPath);
        return true;
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
                    PS::Log<LogLevel::Verbose>(
                        STR("Flow asset '{}' resolved via loaded FlowNode outer '{}'.\n"),
                        AssetPath,
                        outer->GetPathName()
                    );
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

        PS::Log<LogLevel::Normal>(STR("Patched node {}\n"), NodeObject->GetName());
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

            PS::Log<LogLevel::Normal>(STR("Set DT_NpcTalkText[{}]\n"), MessageId.ToString());
        }
    }
}
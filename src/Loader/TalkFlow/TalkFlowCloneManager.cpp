#include "Loader/TalkFlowCloneManager.h"
#include "SDK/Helper/PropertyHelper.h"
#include "SDK/Structs/Custom/FManagedValue.h"
#include "SDK/Structs/Custom/FScriptMapHelper.h"
#include "Unreal/UObjectGlobals.hpp"
#include "Unreal/FProperty.hpp"
#include "Unreal/Property/FMapProperty.hpp"
#include "Unreal/Property/FObjectProperty.hpp"
#include "SDK/Classes/Custom/UObjectGlobals.h"
#include "SDK/Classes/KismetSystemLibrary.h"
#include "Utility/Logging.h"
#include "Helpers/String.hpp"
#include <cctype>
#include <algorithm>
#include <cstring>
#include <format>
#include <random>
#include <vector>

using namespace RC;
using namespace RC::Unreal;

namespace Palworld {
    namespace constants {
        constexpr const TCHAR* vanillaAssetPrefix = STR("/Game/Pal/");
    }

    namespace {
        struct GuidMemory { uint32_t A; uint32_t B; uint32_t C; uint32_t D; };

        std::string BuildGuidKey(const GuidMemory& guid)
        {
            return std::format("{:08X}-{:08X}-{:08X}-{:08X}", guid.A, guid.B, guid.C, guid.D);
        }
    }

    void TalkFlowCloneManager::Initialize(UClass* FlowNodeClass)
    {
        m_flowNodeClass = FlowNodeClass;
    }

    RC::StringType TalkFlowCloneManager::ResolveTalkFlowAssetPath(const TalkFlowCloneRequest& Request)
    {
        auto cloneKey = BuildCloneKey(Request);

        if (!Request.ForceRebuild)
        {
            auto cachedResolvedPathIt = m_cachedResolvedPaths.find(cloneKey);
            if (cachedResolvedPathIt != m_cachedResolvedPaths.end())
            {
                return cachedResolvedPathIt->second;
            }
        }

        auto isVanillaAssetPath = Request.SourceAssetPath.rfind(constants::vanillaAssetPrefix, 0) == 0;
        if (!isVanillaAssetPath)
        {
            m_cachedResolvedPaths[cloneKey] = Request.SourceAssetPath;
            return Request.SourceAssetPath;
        }

        auto sourceAsset = FindAssetByPath(Request.SourceAssetPath);
        if (!sourceAsset)
        {
            if (!m_warnedCloneKeys.contains(cloneKey))
            {
                m_warnedCloneKeys.emplace(cloneKey);
                PS::Log<LogLevel::Warning>(
                    STR("TalkFlow clone source asset could not be found for '{}' (source '{}'). Falling back to source path.\n"),
                    Request.CharacterId,
                    Request.SourceAssetPath
                );
            }

            m_cachedResolvedPaths[cloneKey] = Request.SourceAssetPath;
            return Request.SourceAssetPath;
        }

        auto clonedAsset = CreateCloneObject(sourceAsset, Request);
        if (!clonedAsset)
        {
            if (!m_warnedCloneKeys.contains(cloneKey))
            {
                m_warnedCloneKeys.emplace(cloneKey);
                PS::Log<LogLevel::Warning>(
                    STR("TalkFlow clone failed for '{}' from '{}'. Falling back to source path.\n"),
                    Request.CharacterId,
                    Request.SourceAssetPath
                );
            }

            m_cachedResolvedPaths[cloneKey] = Request.SourceAssetPath;
            return Request.SourceAssetPath;
        }

        auto resolvedPath = clonedAsset->GetPathName();
        if (!IsValidResolvedAssetPath(resolvedPath))
        {
            if (!m_warnedCloneKeys.contains(cloneKey))
            {
                m_warnedCloneKeys.emplace(cloneKey);
                PS::Log<LogLevel::Warning>(
                    STR("TalkFlow clone produced invalid path '{}' for '{}' (source '{}'). Falling back to source path.\n"),
                    resolvedPath,
                    Request.CharacterId,
                    Request.SourceAssetPath
                );
            }

            m_cachedResolvedPaths[cloneKey] = Request.SourceAssetPath;
            return Request.SourceAssetPath;
        }

        m_cachedCloneObjects[cloneKey] = clonedAsset;
        m_cachedResolvedPaths[cloneKey] = resolvedPath;

        return resolvedPath;
    }

    std::string TalkFlowCloneManager::BuildCloneKey(const TalkFlowCloneRequest& Request) const
    {
        return std::format("{}::{}", RC::to_string(Request.CharacterId), RC::to_string(Request.SourceAssetPath));
    }

    bool TalkFlowCloneManager::IsValidResolvedAssetPath(const RC::StringType& AssetPath) const
    {
        if (AssetPath.empty())
        {
            return false;
        }

        if (AssetPath.rfind(STR("/"), 0) != 0)
        {
            return false;
        }

        if (AssetPath.find(STR(".")) == RC::StringType::npos)
        {
            return false;
        }

        return true;
    }

    UObject* TalkFlowCloneManager::FindAssetByPath(const RC::StringType& AssetPath) const
    {
        std::vector<RC::StringType> candidates;
        auto addCandidate = [&candidates](const RC::StringType& candidate)
        {
            if (candidate.empty()) return;
            if (std::find(candidates.begin(), candidates.end(), candidate) != candidates.end()) return;
            candidates.emplace_back(candidate);
        };

        addCandidate(AssetPath);

        auto dotIndex = AssetPath.find_last_of('.');
        if (dotIndex != RC::StringType::npos)
        {
            auto packagePath = AssetPath.substr(0, dotIndex);
            auto objectName = AssetPath.substr(dotIndex + 1);

            if (objectName.size() > 2 && objectName.ends_with(STR("_C")))
            {
                addCandidate(std::format(STR("{}.{}"), packagePath, objectName.substr(0, objectName.size() - 2)));
            }
            else
            {
                addCandidate(std::format(STR("{}.{}_C"), packagePath, objectName));
            }
        }
        else if (AssetPath.rfind(STR("/"), 0) == 0)
        {
            auto slashIndex = AssetPath.find_last_of('/');
            auto objectName = slashIndex == RC::StringType::npos ? AssetPath : AssetPath.substr(slashIndex + 1);
            if (!objectName.empty())
            {
                addCandidate(std::format(STR("{}.{}"), AssetPath, objectName));
                addCandidate(std::format(STR("{}.{}_C"), AssetPath, objectName));
            }
        }

        for (const auto& candidate : candidates)
        {
            if (auto found = UECustom::UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, candidate.c_str()))
            {
                return found;
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

        // If the asset is not resident yet, try an explicit blocking load so clone creation
        // can happen before the first interaction uses vanilla talkflow.
        for (const auto& candidate : candidates)
        {
            auto loadedAsset = UECustom::UKismetSystemLibrary::LoadAsset_Blocking(
                UECustom::TSoftObjectPtr<UObject>(UECustom::FSoftObjectPath(candidate))
            );
            if (!loadedAsset)
            {
                continue;
            }
            return loadedAsset;
        }

        return nullptr;
    }

    RC::StringType TalkFlowCloneManager::BuildCloneObjectName(const TalkFlowCloneRequest& Request) const
    {
        auto characterIdNarrow = RC::to_string(Request.CharacterId);
        auto sourcePathNarrow = RC::to_string(Request.SourceAssetPath);

        auto sourceNameStart = sourcePathNarrow.find_last_of('/');
        auto sourceName = sourceNameStart == std::string::npos ? sourcePathNarrow : sourcePathNarrow.substr(sourceNameStart + 1);

        for (auto& c : characterIdNarrow)
        {
            if (!std::isalnum(static_cast<unsigned char>(c))) c = '_';
        }

        for (auto& c : sourceName)
        {
            if (!std::isalnum(static_cast<unsigned char>(c))) c = '_';
        }

        return RC::to_generic_string(std::format("TFClone_{}_{}", characterIdNarrow, sourceName));
    }

    UObject* TalkFlowCloneManager::FindTransientOuter() const
    {
        auto transientOuter = UECustom::UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, STR("/Engine/Transient.Transient"));
        if (transientOuter)
        {
            return transientOuter;
        }

        return nullptr;
    }

    UObject* TalkFlowCloneManager::CreateCloneObject(UObject* SourceAsset, const TalkFlowCloneRequest& Request)
    {
        if (!SourceAsset)
        {
            return nullptr;
        }

        auto cloneOuter = SourceAsset->GetOuterPrivate();
        if (!cloneOuter)
        {
            cloneOuter = FindTransientOuter();
        }

        if (!cloneOuter)
        {
            return nullptr;
        }

        auto cloneObjectName = BuildCloneObjectName(Request);

        FStaticConstructObjectParameters constructParams{ SourceAsset->GetClassPrivate(), cloneOuter };
        constructParams.Name = FName(cloneObjectName, FNAME_Add);
        constructParams.SetFlags = EObjectFlags::RF_Transient;
        constructParams.Template = SourceAsset;
        constructParams.bCopyTransientsFromClassDefaults = true;

        auto clonedObject = RC::Unreal::UObjectGlobals::StaticConstructObject<UObject*>(constructParams);
        if (!clonedObject)
        {
            return nullptr;
        }

        CloneFlowNodesIntoAsset(SourceAsset, clonedObject);

        clonedObject->SetRootSet();

        return clonedObject;
    }

    void TalkFlowCloneManager::CloneFlowNodesIntoAsset(UObject* SourceAsset, UObject* ClonedAsset)
    {
        if (!SourceAsset || !ClonedAsset || !m_flowNodeClass)
        {
            return;
        }

        TArray<UObject*> flowNodeObjects;
        UECustom::UObjectGlobals::GetObjectsOfClass(m_flowNodeClass, flowNodeObjects, true, EObjectFlags::RF_ClassDefaultObject, EInternalObjectFlags::None);

        std::unordered_map<std::string, UObject*> clonedNodesByGuidKey;
        std::unordered_map<UObject*, UObject*> clonedNodesBySourceNode;

        for (auto* sourceNode : flowNodeObjects)
        {
            if (!sourceNode || sourceNode->GetOuterPrivate() != SourceAsset)
            {
                continue;
            }

            FStaticConstructObjectParameters constructParams{ sourceNode->GetClassPrivate(), ClonedAsset };
            constructParams.Name = FName(sourceNode->GetName(), FNAME_Add);
            constructParams.SetFlags = EObjectFlags::RF_Transient;
            constructParams.Template = sourceNode;
            constructParams.bCopyTransientsFromClassDefaults = true;

            auto* clonedNode = RC::Unreal::UObjectGlobals::StaticConstructObject<UObject*>(constructParams);
            if (!clonedNode)
            {
                continue;
            }

            clonedNode->SetRootSet();
            clonedNodesBySourceNode.emplace(sourceNode, clonedNode);

            // Index by SOURCE node's GUID so the key matches what is stored in the Nodes FMap,
            // which was copied from the source before we did any cloning.
            // Some node classes regenerate their NodeGuid in their constructor, so the
            // cloned node may carry a different GUID than the source.  Using the source GUID
            // guarantees a hit in RemapFlowNodeMapToClones.
            if (auto* sourceGuid = Palworld::PropertyHelper::GetValuePtrByPropertyNameInChain<GuidMemory>(sourceNode, STR("NodeGuid")))
            {
                clonedNodesByGuidKey[BuildGuidKey(*sourceGuid)] = clonedNode;
            }

            // Also register under the cloned node's own GUID as a secondary key (handles
            // nodes that DO preserve their GUID through construction).
            if (auto* clonedGuid = Palworld::PropertyHelper::GetValuePtrByPropertyNameInChain<GuidMemory>(clonedNode, STR("NodeGuid")))
            {
                auto clonedKey = BuildGuidKey(*clonedGuid);
                if (!clonedNodesByGuidKey.count(clonedKey))
                {
                    clonedNodesByGuidKey[clonedKey] = clonedNode;
                }
            }
        }

        if (!clonedNodesByGuidKey.empty() || !clonedNodesBySourceNode.empty())
        {
            RemapFlowNodeMapToClones(SourceAsset, ClonedAsset, clonedNodesByGuidKey, clonedNodesBySourceNode);
        }

        
    }

    void TalkFlowCloneManager::RemapFlowNodeMapToClones(
        UObject* SourceAsset,
        UObject* ClonedAsset,
        const std::unordered_map<std::string, UObject*>& ClonedNodesByGuidKey,
        const std::unordered_map<UObject*, UObject*>& ClonedNodesBySourceNode)
    {
        if (!SourceAsset || !ClonedAsset || (ClonedNodesByGuidKey.empty() && ClonedNodesBySourceNode.empty()))
        {
            return;
        }

        auto* sourceNodesProperty = SourceAsset->GetPropertyByNameInChain(STR("Nodes"));
        auto* sourceMapProperty = Palworld::PropertyHelper::CastProperty<FMapProperty>(sourceNodesProperty);
        if (!sourceMapProperty)
        {
            return;
        }

        auto* cloneNodesProperty = ClonedAsset->GetPropertyByNameInChain(STR("Nodes"));
        auto* cloneMapProperty = Palworld::PropertyHelper::CastProperty<FMapProperty>(cloneNodesProperty);
        if (!cloneMapProperty)
        {
            return;
        }

        auto* sourceValueObjectProperty = Palworld::PropertyHelper::CastProperty<FObjectProperty>(sourceMapProperty->GetValueProp());
        if (!sourceValueObjectProperty || !Palworld::PropertyHelper::CastProperty<FObjectProperty>(cloneMapProperty->GetValueProp()))
        {
            return;
        }

        auto* sourceMapData = sourceMapProperty->ContainerPtrToValuePtr<void>(SourceAsset);
        auto* cloneMapData = cloneMapProperty->ContainerPtrToValuePtr<void>(ClonedAsset);
        if (!sourceMapData || !cloneMapData)
        {
            return;
        }

        UECustom::FScriptMapHelper sourceMapHelper(sourceMapProperty, sourceMapData);
        UECustom::FScriptMapHelper cloneMapHelper(cloneMapProperty, cloneMapData);

        int32_t sourceEntries = 0;
        int32_t unresolvedEntries = 0;

        sourceMapHelper.ForEachPair([&](void* sourceKeyPtr, void* sourceValuePtr)
        {
            if (!sourceKeyPtr || !sourceValuePtr)
            {
                return;
            }

            ++sourceEntries;

            auto* sourceNodeSlot = sourceValueObjectProperty->ContainerPtrToValuePtr<UObject*>(sourceValuePtr);
            if (!sourceNodeSlot)
            {
                ++unresolvedEntries;
                return;
            }

            UObject* replacementNode = nullptr;

            GuidMemory keyGuid{};
            std::memcpy(&keyGuid, sourceKeyPtr, sizeof(GuidMemory));

            auto guidKey = BuildGuidKey(keyGuid);
            auto cloneIt = ClonedNodesByGuidKey.find(guidKey);
            if (cloneIt != ClonedNodesByGuidKey.end())
            {
                replacementNode = cloneIt->second;
            }
            else if (*sourceNodeSlot)
            {
                auto sourceIt = ClonedNodesBySourceNode.find(*sourceNodeSlot);
                if (sourceIt != ClonedNodesBySourceNode.end())
                {
                    replacementNode = sourceIt->second;
                }
            }

            if (!replacementNode)
            {
                ++unresolvedEntries;
                return;
            }

            UECustom::FManagedValue pair;
            cloneMapHelper.InitializePair(pair);

            auto* pairData = static_cast<uint8_t*>(pair.GetData());
            if (!pairData)
            {
                ++unresolvedEntries;
                return;
            }

            std::memcpy(pairData, sourceKeyPtr, sizeof(GuidMemory));
            auto* replacementSlot = reinterpret_cast<UObject**>(pairData + sizeof(GuidMemory));
            *replacementSlot = replacementNode;
            cloneMapHelper.Add(pair);
        });

        if (unresolvedEntries > 0)
        {
            PS::Log<LogLevel::Warning>(
                STR("TalkFlow clone node remap unresolved entries: asset={} unresolved={} sourceEntries={}\n"),
                ClonedAsset->GetPathName(),
                unresolvedEntries,
                sourceEntries
            );
        }

        
    }

    UObject* TalkFlowCloneManager::SpawnNodeInClone(
        UObject* CloneAsset,
        const std::string& DesiredClassName,
        const std::string& DesiredNodeName)
    {
        if (!CloneAsset || !m_flowNodeClass)
        {
            return nullptr;
        }

        // Find the UClass* for DesiredClassName by scanning existing flow node instances.
        UClass* targetClass = nullptr;
        TArray<UObject*> flowNodeObjects;
        UECustom::UObjectGlobals::GetObjectsOfClass(m_flowNodeClass, flowNodeObjects, true, EObjectFlags::RF_ClassDefaultObject, EInternalObjectFlags::None);

        for (auto* obj : flowNodeObjects)
        {
            if (!obj) continue;
            auto* cls = obj->GetClassPrivate();
            if (!cls) continue;
            if (RC::to_string(cls->GetName()) == DesiredClassName)
            {
                targetClass = static_cast<UClass*>(cls);
                break;
            }
        }

        if (!targetClass)
        {
            // Fallback: resolve class by convention path and force a blocking load.
            auto classNameWide = RC::to_generic_string(DesiredClassName);
            auto baseNameWide = classNameWide;
            if (baseNameWide.size() > 2 && baseNameWide.ends_with(STR("_C")))
            {
                baseNameWide = baseNameWide.substr(0, baseNameWide.size() - 2);
            }

            std::vector<RC::StringType> classPathCandidates;
            classPathCandidates.emplace_back(
                std::format(
                    STR("/Game/Pal/Blueprint/FlowGraph/NPCTalkFlow/CommonNode/{}.{}"),
                    baseNameWide,
                    classNameWide));
            classPathCandidates.emplace_back(
                std::format(
                    STR("/Game/Pal/Blueprint/FlowGraph/NPCTalkFlow/CommonNode/{}.{}_C"),
                    baseNameWide,
                    baseNameWide));
            classPathCandidates.emplace_back(
                std::format(
                    STR("/Game/Pal/Blueprint/FlowGraph/NPCTalkFlow/CustomNode/{}.{}"),
                    baseNameWide,
                    classNameWide));
            classPathCandidates.emplace_back(
                std::format(
                    STR("/Game/Pal/Blueprint/FlowGraph/NPCTalkFlow/CustomNode/{}.{}_C"),
                    baseNameWide,
                    baseNameWide));

            for (const auto& candidatePath : classPathCandidates)
            {
                auto* loadedObject = FindAssetByPath(candidatePath);
                if (!loadedObject)
                {
                    continue;
                }

                if (RC::to_string(loadedObject->GetName()) == DesiredClassName)
                {
                    targetClass = static_cast<UClass*>(loadedObject);
                    break;
                }

                auto* loadedClass = loadedObject->GetClassPrivate();
                if (loadedClass && RC::to_string(loadedClass->GetName()) == DesiredClassName)
                {
                    targetClass = static_cast<UClass*>(loadedClass);
                    break;
                }
            }
        }

        if (!targetClass)
        {
            PS::Log<LogLevel::Warning>(
                STR("SpawnNodeInClone: class '{}' not found among loaded flow nodes for clone '{}'\n"),
                RC::to_generic_string(DesiredClassName),
                CloneAsset->GetName()
            );
            return nullptr;
        }

        auto desiredNameW = RC::to_generic_string(DesiredNodeName);
        FStaticConstructObjectParameters params{ targetClass, CloneAsset };
        params.Name = FName(desiredNameW, FNAME_Add);
        params.SetFlags = EObjectFlags::RF_Transient;
        params.bCopyTransientsFromClassDefaults = true;

        auto* newNode = RC::Unreal::UObjectGlobals::StaticConstructObject<UObject*>(params);
        if (!newNode)
        {
            PS::Log<LogLevel::Warning>(
                STR("SpawnNodeInClone: StaticConstructObject failed for '{}' ({}) in clone '{}'\n"),
                RC::to_generic_string(DesiredNodeName),
                RC::to_generic_string(DesiredClassName),
                CloneAsset->GetName()
            );
            return nullptr;
        }

        newNode->SetRootSet();

        // Assign a fresh random GUID to this node so it has a unique Nodes map key.
        static std::mt19937 rng{ std::random_device{}() };
        static std::uniform_int_distribution<uint32_t> dist;
        GuidMemory freshGuid{ dist(rng), dist(rng), dist(rng), dist(rng) };

        if (auto* guidProp = Palworld::PropertyHelper::GetValuePtrByPropertyNameInChain<GuidMemory>(newNode, STR("NodeGuid")))
        {
            *guidProp = freshGuid;
        }

        // Register the new node in the clone asset's Nodes FMap using its GUID as key.
        auto* nodesProperty = CloneAsset->GetPropertyByNameInChain(STR("Nodes"));
        auto* mapProperty = Palworld::PropertyHelper::CastProperty<FMapProperty>(nodesProperty);
        if (mapProperty)
        {
            auto* mapData = mapProperty->ContainerPtrToValuePtr<void>(CloneAsset);
            if (mapData)
            {
                UECustom::FScriptMapHelper mapHelper(mapProperty, mapData);

                UECustom::FManagedValue pair;
                mapHelper.InitializePair(pair);
                auto* pairData = static_cast<uint8_t*>(pair.GetData());
                if (pairData)
                {
                    std::memcpy(pairData, &freshGuid, sizeof(GuidMemory));
                    auto* nodeSlot = reinterpret_cast<UObject**>(pairData + sizeof(GuidMemory));
                    *nodeSlot = newNode;
                    mapHelper.Add(pair);
                }
            }
        }

        return newNode;
    }
}
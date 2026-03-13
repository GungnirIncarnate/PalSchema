#include "Loader/TalkFlowCloneManager.h"
#include "Unreal/UObjectGlobals.hpp"
#include "SDK/Classes/Custom/UObjectGlobals.h"
#include "SDK/Classes/KismetSystemLibrary.h"
#include "Utility/Logging.h"
#include "Helpers/String.hpp"
#include <cctype>
#include <algorithm>
#include <vector>

using namespace RC;
using namespace RC::Unreal;

namespace Palworld {
    namespace constants {
        constexpr const TCHAR* vanillaAssetPrefix = STR("/Game/Pal/");
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

        // Groundwork behavior:
        // - Non-vanilla/custom asset paths are already isolated, no clone required.
        // - Vanilla paths should eventually be cloned into transient objects.
        // - Until cloning logic is implemented, we warn once and return source path.
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

        PS::Log<LogLevel::Normal>(
            STR("TalkFlow clone created for '{}' from '{}' -> '{}'\n"),
            Request.CharacterId,
            Request.SourceAssetPath,
            resolvedPath
        );

        return resolvedPath;
    }

    std::string TalkFlowCloneManager::BuildCloneKey(const TalkFlowCloneRequest& Request) const
    {
        return std::format("{}::{}", RC::to_string(Request.CharacterId), RC::to_string(Request.SourceAssetPath));
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

    UObject* TalkFlowCloneManager::FindTransientOuter() const
    {
        auto transientOuter = UECustom::UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, STR("/Engine/Transient.Transient"));
        if (transientOuter)
        {
            return transientOuter;
        }

        return nullptr;
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
                    PS::Log<LogLevel::Verbose>(
                        STR("TalkFlow clone source '{}' resolved via loaded FlowNode outer '{}'.\n"),
                        AssetPath,
                        outer->GetPathName()
                    );
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

            PS::Log<LogLevel::Verbose>(
                STR("TalkFlow source '{}' force-loaded via LoadAsset_Blocking as '{}'.\n"),
                candidate,
                loadedAsset->GetPathName()
            );
            return loadedAsset;
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

        clonedObject->SetRootSet();

        return clonedObject;
    }
}
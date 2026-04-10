#include "Loader/TalkFlow/TalkFlowAssetResolver.h"

#include "Unreal/UObjectGlobals.hpp"
#include "SDK/Classes/Custom/UObjectGlobals.h"
#include "SDK/Structs/FPalNPCTalkFlowClassDataRow.h"

#include <algorithm>
#include <bit>
#include <format>

using namespace RC;
using namespace RC::Unreal;

namespace Palworld::TalkFlow
{
    UObject* TalkFlowAssetResolver::FindTalkFlowAsset(
        const RC::StringType& assetPath,
        UClass* flowNodeClass,
        UDataTable* npcTalkFlowTable)
    {
        std::vector<RC::StringType> candidates;
        AddTalkFlowPathCandidates(assetPath, candidates);
        AddTalkFlowCandidatesFromDataTable(assetPath, npcTalkFlowTable, candidates);

        for (const auto& candidate : candidates)
        {
            auto foundAsset = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, candidate.c_str());
            if (foundAsset)
            {
                return foundAsset;
            }
        }

        if (flowNodeClass)
        {
            auto expectedShortName = assetPath;
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
            UECustom::UObjectGlobals::GetObjectsOfClass(flowNodeClass, flowNodeObjects, true, EObjectFlags::RF_ClassDefaultObject, EInternalObjectFlags::None);

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

    RC::StringType TalkFlowAssetResolver::CanonicalizeTalkFlowPath(
        const RC::StringType& talkFlowPath,
        UClass* flowNodeClass,
        UDataTable* npcTalkFlowTable)
    {
        std::vector<RC::StringType> candidates;
        AddTalkFlowPathCandidates(talkFlowPath, candidates);
        AddTalkFlowCandidatesFromDataTable(talkFlowPath, npcTalkFlowTable, candidates);

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

        return talkFlowPath;
    }

    void TalkFlowAssetResolver::AddTalkFlowPathCandidates(
        const RC::StringType& talkFlowPath,
        std::vector<RC::StringType>& candidates)
    {
        auto addCandidate = [&candidates](const RC::StringType& candidate)
        {
            if (candidate.empty())
            {
                return;
            }

            if (std::find(candidates.begin(), candidates.end(), candidate) != candidates.end())
            {
                return;
            }

            candidates.emplace_back(candidate);
        };

        addCandidate(talkFlowPath);

        auto dotIndex = talkFlowPath.find_last_of('.');
        if (dotIndex != RC::StringType::npos)
        {
            auto packagePath = talkFlowPath.substr(0, dotIndex);
            auto objectName = talkFlowPath.substr(dotIndex + 1);

            if (objectName.size() > 2 && objectName.ends_with(STR("_C")))
            {
                addCandidate(std::format(STR("{}.{}"), packagePath, objectName.substr(0, objectName.size() - 2)));
            }
            else
            {
                addCandidate(std::format(STR("{}.{}_C"), packagePath, objectName));
            }
        }
        else if (talkFlowPath.rfind(STR("/"), 0) == 0)
        {
            auto slashIndex = talkFlowPath.find_last_of('/');
            auto objectName = slashIndex == RC::StringType::npos ? talkFlowPath : talkFlowPath.substr(slashIndex + 1);
            if (!objectName.empty())
            {
                addCandidate(std::format(STR("{}.{}"), talkFlowPath, objectName));
                addCandidate(std::format(STR("{}.{}_C"), talkFlowPath, objectName));
            }
        }
    }

    void TalkFlowAssetResolver::AddTalkFlowCandidatesFromDataTable(
        const RC::StringType& talkFlowPath,
        UDataTable* npcTalkFlowTable,
        std::vector<RC::StringType>& candidates)
    {
        if (!npcTalkFlowTable)
        {
            return;
        }

        auto expectedShortName = talkFlowPath;
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

        auto& rowMap = npcTalkFlowTable->GetRowMap();
        for (auto& [rowName, rowData] : rowMap)
        {
            if (!rowData)
            {
                continue;
            }

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

            AddTalkFlowPathCandidates(std::format(STR("{}.{}"), packageName, assetName), candidates);
        }
    }
}

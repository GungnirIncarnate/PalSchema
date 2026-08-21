#include "Utility/LoadOrderSystem/ModLoadOrderSorter.h"
#include "Utility/Logging.h"
#include <algorithm>
#include <limits>
#include <queue>
#include <unordered_set>

namespace PS {
    std::unordered_map<std::string, size_t> ModLoadOrderSorter::BuildModIndexMap(const std::vector<ModLoadOrderEntry>& mods) const
    {
        std::unordered_map<std::string, size_t> modIndexById;
        for (size_t index = 0; index < mods.size(); ++index)
        {
            modIndexById.emplace(mods[index].modId, index);
        }

        return modIndexById;
    }

    std::unordered_map<std::string, size_t> ModLoadOrderSorter::BuildExplicitRankMap(const ModLoadOrderSettings& settings) const
    {
        std::unordered_map<std::string, size_t> explicitRank;
        for (size_t index = 0; index < settings.explicitOrder.size(); ++index)
        {
            explicitRank.emplace(settings.explicitOrder[index], index);
        }

        return explicitRank;
    }

    void ModLoadOrderSorter::AddDependencyEdges(const std::vector<ModLoadOrderEntry>& mods,
        const std::unordered_map<std::string, size_t>& modIndexById,
        std::vector<std::unordered_set<size_t>>& edges,
        std::vector<size_t>& indegree,
        std::unordered_set<uint64_t>& metadataEdgeKeys) const
    {
        auto makeEdgeKey = [](size_t fromIndex, size_t toIndex) {
            return (static_cast<uint64_t>(fromIndex) << 32) | static_cast<uint64_t>(toIndex);
        };

        auto addEdge = [&](size_t fromIndex, size_t toIndex) {
            if (fromIndex == toIndex)
            {
                return;
            }

            if (edges[fromIndex].insert(toIndex).second)
            {
                indegree[toIndex] += 1;
            }
        };

        for (size_t index = 0; index < mods.size(); ++index)
        {
            const auto& mod = mods[index];

            for (const auto& dependency : mod.dependencies)
            {
                if (auto it = modIndexById.find(dependency); it != modIndexById.end())
                {
                    addEdge(it->second, index);
                    metadataEdgeKeys.insert(makeEdgeKey(it->second, index));
                }
            }
        }
    }

    void ModLoadOrderSorter::AddExplicitOrderEdges(const std::vector<ModLoadOrderEntry>& mods,
        const ModLoadOrderSettings& settings,
        const std::unordered_map<std::string, size_t>& modIndexById,
        const std::unordered_set<uint64_t>& metadataEdgeKeys,
        std::vector<std::unordered_set<size_t>>& edges,
        std::vector<size_t>& indegree) const
    {
        auto makeEdgeKey = [](size_t fromIndex, size_t toIndex) {
            return (static_cast<uint64_t>(fromIndex) << 32) | static_cast<uint64_t>(toIndex);
        };

        auto addEdge = [&](size_t fromIndex, size_t toIndex) {
            if (fromIndex == toIndex)
            {
                return;
            }

            if (edges[fromIndex].insert(toIndex).second)
            {
                indegree[toIndex] += 1;
            }
        };

        for (size_t index = 1; index < settings.explicitOrder.size(); ++index)
        {
            auto previous = modIndexById.find(settings.explicitOrder[index - 1]);
            auto current = modIndexById.find(settings.explicitOrder[index]);
            if (previous == modIndexById.end() || current == modIndexById.end())
            {
                continue;
            }

            const auto fromIndex = previous->second;
            const auto toIndex = current->second;
            if (metadataEdgeKeys.contains(makeEdgeKey(toIndex, fromIndex)))
            {
                PS::Log<RC::LogLevel::Warning>(
                    STR("Skipping 'explicit_order' ordering edge because it conflicts with metadata dependency/order rules: '{}' -> '{}'.\n"),
                    RC::to_generic_string(mods[fromIndex].modId),
                    RC::to_generic_string(mods[toIndex].modId)
                );
                continue;
            }

            addEdge(fromIndex, toIndex);
        }
    }

    bool ModLoadOrderSorter::CompareModIndices(const std::vector<ModLoadOrderEntry>& mods,
        const std::unordered_map<std::string, size_t>& explicitRank,
        size_t leftIndex,
        size_t rightIndex) const
    {
        const auto getRank = [](const auto& ranks, const std::string& modId) {
            auto it = ranks.find(modId);
            return it == ranks.end() ? std::numeric_limits<size_t>::max() : it->second;
        };

        const auto& left = mods[leftIndex];
        const auto& right = mods[rightIndex];

        auto leftExplicitRank = getRank(explicitRank, left.modId);
        auto rightExplicitRank = getRank(explicitRank, right.modId);
        if (leftExplicitRank != rightExplicitRank)
        {
            return leftExplicitRank > rightExplicitRank;
        }

        return left.folderName > right.folderName;
    }

    std::vector<ModLoadOrderEntry> ModLoadOrderSorter::SortMods(const std::vector<ModLoadOrderEntry>& mods, const ModLoadOrderSettings& settings) const
    {
        std::vector<ModLoadOrderEntry> sortedMods;
        if (mods.empty())
        {
            return sortedMods;
        }

        const auto modIndexById = BuildModIndexMap(mods);
        const auto explicitRank = BuildExplicitRankMap(settings);

        for (const auto& modId : settings.explicitOrder)
        {
            if (!modIndexById.contains(modId))
            {
                PS::Log<RC::LogLevel::Verbose>(STR("explicit_order references unknown mod id '{}'.\n"), RC::to_generic_string(modId));
            }
        }

        std::vector<std::unordered_set<size_t>> edges(mods.size());
        std::vector<size_t> indegree(mods.size(), 0);
        std::unordered_set<uint64_t> metadataEdgeKeys;

        AddDependencyEdges(mods, modIndexById, edges, indegree, metadataEdgeKeys);
        AddExplicitOrderEdges(mods, settings, modIndexById, metadataEdgeKeys, edges, indegree);

        auto compareIndices = [&](size_t leftIndex, size_t rightIndex) {
            return CompareModIndices(mods, explicitRank, leftIndex, rightIndex);
        };

        std::priority_queue<size_t, std::vector<size_t>, decltype(compareIndices)> available(compareIndices);
        for (size_t index = 0; index < indegree.size(); ++index)
        {
            if (indegree[index] == 0)
            {
                available.push(index);
            }
        }

        std::vector<bool> processed(mods.size(), false);
        while (!available.empty())
        {
            auto currentIndex = available.top();
            available.pop();

            if (processed[currentIndex])
            {
                continue;
            }

            processed[currentIndex] = true;
            sortedMods.push_back(mods[currentIndex]);

            for (auto nextIndex : edges[currentIndex])
            {
                indegree[nextIndex] -= 1;
                if (indegree[nextIndex] == 0)
                {
                    available.push(nextIndex);
                }
            }
        }

        if (sortedMods.size() != mods.size())
        {
            PS::Log<RC::LogLevel::Error>(STR("Load order graph contains a cycle or conflicting constraints. Falling back for unresolved mods.\n"));

            std::vector<size_t> unresolvedIndices;
            for (size_t index = 0; index < mods.size(); ++index)
            {
                if (!processed[index])
                {
                    unresolvedIndices.push_back(index);
                }
            }

            std::sort(unresolvedIndices.begin(), unresolvedIndices.end(), [&](size_t leftIndex, size_t rightIndex) {
                return !compareIndices(leftIndex, rightIndex);
            });

            for (auto unresolvedIndex : unresolvedIndices)
            {
                sortedMods.push_back(mods[unresolvedIndex]);
            }
        }

        return sortedMods;
    }
}

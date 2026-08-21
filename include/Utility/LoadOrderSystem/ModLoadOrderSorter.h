#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "Utility/LoadOrderSystem/ModLoadOrderHelper.h"

namespace PS {
    class ModLoadOrderSorter {
    public:
        std::vector<ModLoadOrderEntry> SortMods(const std::vector<ModLoadOrderEntry>& mods, const ModLoadOrderSettings& settings) const;

    private:
        std::unordered_map<std::string, size_t> BuildModIndexMap(const std::vector<ModLoadOrderEntry>& mods) const;
        std::unordered_map<std::string, size_t> BuildExplicitRankMap(const ModLoadOrderSettings& settings) const;
        void AddDependencyEdges(const std::vector<ModLoadOrderEntry>& mods,
            const std::unordered_map<std::string, size_t>& modIndexById,
            std::vector<std::unordered_set<size_t>>& edges,
            std::vector<size_t>& indegree,
            std::unordered_set<uint64_t>& metadataEdgeKeys) const;
        void AddExplicitOrderEdges(const std::vector<ModLoadOrderEntry>& mods,
            const ModLoadOrderSettings& settings,
            const std::unordered_map<std::string, size_t>& modIndexById,
            const std::unordered_set<uint64_t>& metadataEdgeKeys,
            std::vector<std::unordered_set<size_t>>& edges,
            std::vector<size_t>& indegree) const;
        bool CompareModIndices(const std::vector<ModLoadOrderEntry>& mods,
            const std::unordered_map<std::string, size_t>& explicitRank,
            size_t leftIndex,
            size_t rightIndex) const;
    };
}

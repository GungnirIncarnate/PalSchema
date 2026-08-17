#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "glaze/glaze.hpp"

namespace PS {
    struct ModMetadata {
        std::string mod_id{};
        std::string name{};
        std::string version{};
        std::vector<std::string> authors{};
        std::vector<std::string> dependencies{};
    };

    struct SchemaModsConfig {
        std::vector<std::string> explicit_order{};
        std::vector<std::string> disabled_mods{};
    };

    struct ModLoadOrderEntry {
        std::filesystem::path modPath{};
        std::string folderName = "";
        std::string modId = "";
        std::string name = "";
        std::string version = "";
        std::vector<std::string> authors{};
        bool usesFallbackModId = false;
        std::vector<std::string> dependencies{};
    };

    struct ModLoadOrderSettings {
        std::vector<std::string> explicitOrder{};
        std::vector<std::string> disabledMods{};
    };

    class ModLoadOrderHelper {
    public:
        std::vector<ModLoadOrderEntry> Resolve(const std::filesystem::path& modsPath);

    private:
        template<typename T>
        bool TryReadJsonFile(const std::filesystem::path& path, T& outValue) const;

        std::filesystem::path FindMetadataPath(const std::filesystem::path& modPath) const;
        std::filesystem::path FindLoadOrderPath(const std::filesystem::path& modsPath) const;

        ModLoadOrderEntry BuildModEntry(const std::filesystem::path& modPath, const ModMetadata& metadata) const;
        ModLoadOrderSettings BuildSettings(const SchemaModsConfig& config) const;

        ModLoadOrderEntry LoadModMetadata(const std::filesystem::path& modPath) const;
        ModLoadOrderSettings LoadSettings(const std::filesystem::path& modsPath) const;

        std::vector<ModLoadOrderEntry> DiscoverMods(const std::filesystem::path& modsPath) const;
        std::vector<ModLoadOrderEntry> FilterDisabledMods(const std::vector<ModLoadOrderEntry>& mods, const ModLoadOrderSettings& settings) const;
        std::vector<ModLoadOrderEntry> FilterMissingDependencies(const std::vector<ModLoadOrderEntry>& mods) const;

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

        std::vector<ModLoadOrderEntry> SortMods(const std::vector<ModLoadOrderEntry>& mods, const ModLoadOrderSettings& settings) const;
    };
}
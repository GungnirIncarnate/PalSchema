#include "Utility/ModLoadOrderHelper.h"
#include "Utility/Logging.h"
#include <algorithm>
#include <fstream>
#include <queue>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace fs = std::filesystem;

namespace PS {
    template<typename T>
    bool ModLoadOrderHelper::TryReadJsonFile(const std::filesystem::path& path, T& outValue) const
    {
        auto readJson = [&](auto& value) {
            if (path.extension() == ".jsonc")
            {
                return glz::read_file_jsonc(value, path.string(), std::string{});
            }

            return glz::read_file_json(value, path.string(), std::string{});
        };

        auto ec = readJson(outValue);
        if (ec)
        {
            std::string errorMessage = glz::format_error(ec, std::string{});
            PS::Log<RC::LogLevel::Error>(STR("Failed to parse JSON file '{}' - {}\n"), RC::to_generic_string(path.string()), RC::to_generic_string(errorMessage));
            return false;
        }

        return true;
    }

    ModLoadOrderEntry ModLoadOrderHelper::BuildModEntry(const std::filesystem::path& modPath, const ModMetadata& metadata) const
    {
        ModLoadOrderEntry mod;
        mod.modPath = modPath;
        mod.folderName = modPath.filename().string();
        mod.modId = mod.folderName;
        mod.usesFallbackModId = true;

        if (!metadata.mod_id.empty())
        {
            mod.modId = metadata.mod_id;
            mod.usesFallbackModId = false;
        }

        mod.name = metadata.name;
        mod.version = metadata.version;
        mod.authors = metadata.authors;
        mod.dependencies = metadata.dependencies;

        if (mod.usesFallbackModId)
        {
            PS::Log<RC::LogLevel::Warning>(STR("Mod '{}' does not declare 'mod_id' in metadata. Falling back to folder name.\n"), RC::to_generic_string(mod.folderName));
        }

        return mod;
    }

    ModLoadOrderSettings ModLoadOrderHelper::BuildSettings(const SchemaModsConfig& config) const
    {
        ModLoadOrderSettings settings;
        settings.explicitOrder = config.explicit_order;
        settings.disabledMods = config.disabled_mods;
        return settings;
    }

    std::vector<ModLoadOrderEntry> ModLoadOrderHelper::Resolve(const std::filesystem::path& modsPath)
    {
        auto mods = DiscoverMods(modsPath);
        auto settings = LoadSettings(modsPath);
        auto filteredMods = FilterDisabledMods(mods, settings);
        auto dependencyValidMods = FilterMissingDependencies(filteredMods);
        return SortMods(dependencyValidMods, settings);
    }

    std::vector<ModLoadOrderEntry> ModLoadOrderHelper::DiscoverMods(const std::filesystem::path& modsPath) const
    {
        std::vector<ModLoadOrderEntry> mods;
        std::unordered_set<std::string> seenModIds;

        if (!fs::exists(modsPath))
        {
            return mods;
        }

        for (const auto& entry : fs::directory_iterator(modsPath))
        {
            if (!entry.is_directory())
            {
                continue;
            }

            auto mod = LoadModMetadata(entry.path());
            if (mod.modId.empty())
            {
                continue;
            }

            if (!seenModIds.insert(mod.modId).second)
            {
                PS::Log<RC::LogLevel::Error>(STR("Duplicate mod id '{}' found. Skipping folder '{}'.\n"), RC::to_generic_string(mod.modId), RC::to_generic_string(mod.folderName));
                continue;
            }

            mods.push_back(std::move(mod));
        }

        return mods;
    }

    ModLoadOrderEntry ModLoadOrderHelper::LoadModMetadata(const std::filesystem::path& modPath) const
    {
        auto metadataPath = FindMetadataPath(modPath);
        if (metadataPath.empty())
        {
            return BuildModEntry(modPath, ModMetadata{});
        }

        ModMetadata metadata{};
        if (!TryReadJsonFile(metadataPath, metadata))
        {
            return BuildModEntry(modPath, ModMetadata{});
        }

        return BuildModEntry(modPath, metadata);
    }

    ModLoadOrderSettings ModLoadOrderHelper::LoadSettings(const std::filesystem::path& modsPath) const
    {
        auto loadOrderPath = FindLoadOrderPath(modsPath);
        if (loadOrderPath.empty())
        {
            PS::Log<RC::LogLevel::Warning>(STR("No schema_mods.json or schema_mods.jsonc found in '{}'.\n"), RC::to_generic_string(modsPath.string()));
            return {};
        }

        PS::Log<RC::LogLevel::Verbose>(STR("Using schema mods settings from '{}'.\n"), RC::to_generic_string(loadOrderPath.string()));

        SchemaModsConfig config{};
        if (!TryReadJsonFile(loadOrderPath, config))
        {
            return {};
        }

        auto settings = BuildSettings(config);
        PS::Log<RC::LogLevel::Verbose>(STR("Schema mods settings loaded: explicit_order={}, disabled_mods={}.\n"), settings.explicitOrder.size(), settings.disabledMods.size());
        return settings;
    }

    std::filesystem::path ModLoadOrderHelper::FindMetadataPath(const std::filesystem::path& modPath) const
    {
        auto jsonPath = modPath / "metadata.json";
        if (fs::exists(jsonPath) && fs::is_regular_file(jsonPath))
        {
            return jsonPath;
        }

        auto jsoncPath = modPath / "metadata.jsonc";
        if (fs::exists(jsoncPath) && fs::is_regular_file(jsoncPath))
        {
            return jsoncPath;
        }

        return {};
    }

    std::filesystem::path ModLoadOrderHelper::FindLoadOrderPath(const std::filesystem::path& modsPath) const
    {
        auto jsonPath = modsPath / "schema_mods.json";
        if (fs::exists(jsonPath) && fs::is_regular_file(jsonPath))
        {
            return jsonPath;
        }

        auto jsoncPath = modsPath / "schema_mods.jsonc";
        if (fs::exists(jsoncPath) && fs::is_regular_file(jsoncPath))
        {
            return jsoncPath;
        }

        return {};
    }

    std::vector<ModLoadOrderEntry> ModLoadOrderHelper::FilterDisabledMods(const std::vector<ModLoadOrderEntry>& mods, const ModLoadOrderSettings& settings) const
    {
        std::unordered_set<std::string> disabledIds(settings.disabledMods.begin(), settings.disabledMods.end());
        std::vector<ModLoadOrderEntry> filteredMods;
        filteredMods.reserve(mods.size());

        for (const auto& mod : mods)
        {
            if (disabledIds.contains(mod.modId))
            {
                PS::Log<RC::LogLevel::Verbose>(STR("Skipping disabled mod '{}'.\n"), RC::to_generic_string(mod.modId));
                continue;
            }

            filteredMods.push_back(mod);
        }

        return filteredMods;
    }

    std::vector<ModLoadOrderEntry> ModLoadOrderHelper::FilterMissingDependencies(const std::vector<ModLoadOrderEntry>& mods) const
    {
        std::vector<ModLoadOrderEntry> activeMods = mods;
        bool hasChanges = true;

        while (hasChanges)
        {
            hasChanges = false;

            std::unordered_set<std::string> activeModIds;
            activeModIds.reserve(activeMods.size());
            for (const auto& mod : activeMods)
            {
                activeModIds.insert(mod.modId);
            }

            std::vector<ModLoadOrderEntry> nextPassMods;
            nextPassMods.reserve(activeMods.size());

            for (const auto& mod : activeMods)
            {
                bool hasMissingDependency = false;

                for (const auto& dependency : mod.dependencies)
                {
                    if (!activeModIds.contains(dependency))
                    {
                        const auto& displayName = mod.name.empty() ? mod.modId : mod.name;
                        PS::Log<RC::LogLevel::Warning>(STR("Skipping mod '{}' because required dependency '{}' is missing or disabled.\n"), RC::to_generic_string(displayName), RC::to_generic_string(dependency));
                        hasMissingDependency = true;
                        hasChanges = true;
                        break;
                    }
                }

                if (!hasMissingDependency)
                {
                    nextPassMods.push_back(mod);
                }
            }

            activeMods = std::move(nextPassMods);
        }

        return activeMods;
    }

    std::unordered_map<std::string, size_t> ModLoadOrderHelper::BuildModIndexMap(const std::vector<ModLoadOrderEntry>& mods) const
    {
        std::unordered_map<std::string, size_t> modIndexById;
        for (size_t index = 0; index < mods.size(); ++index)
        {
            modIndexById.emplace(mods[index].modId, index);
        }

        return modIndexById;
    }

    std::unordered_map<std::string, size_t> ModLoadOrderHelper::BuildExplicitRankMap(const ModLoadOrderSettings& settings) const
    {
        std::unordered_map<std::string, size_t> explicitRank;
        for (size_t index = 0; index < settings.explicitOrder.size(); ++index)
        {
            explicitRank.emplace(settings.explicitOrder[index], index);
        }

        return explicitRank;
    }

    void ModLoadOrderHelper::AddDependencyEdges(const std::vector<ModLoadOrderEntry>& mods,
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
                else
                {
                    PS::Log<RC::LogLevel::Warning>(STR("Mod '{}' depends on unknown mod id '{}'.\n"), RC::to_generic_string(mod.modId), RC::to_generic_string(dependency));
                }
            }
        }
    }

    void ModLoadOrderHelper::AddExplicitOrderEdges(const std::vector<ModLoadOrderEntry>& mods,
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

    bool ModLoadOrderHelper::CompareModIndices(const std::vector<ModLoadOrderEntry>& mods,
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

    std::vector<ModLoadOrderEntry> ModLoadOrderHelper::SortMods(const std::vector<ModLoadOrderEntry>& mods, const ModLoadOrderSettings& settings) const
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
                PS::Log<RC::LogLevel::Warning>(STR("explicit_order references unknown mod id '{}'.\n"), RC::to_generic_string(modId));
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

        for (size_t index = 0; index < sortedMods.size(); ++index)
        {
            PS::Log<RC::LogLevel::Verbose>(STR("Resolved load order [{}]: '{}' (folder '{}')\n"), index, RC::to_generic_string(sortedMods[index].modId), RC::to_generic_string(sortedMods[index].folderName));
        }

        return sortedMods;
    }
}
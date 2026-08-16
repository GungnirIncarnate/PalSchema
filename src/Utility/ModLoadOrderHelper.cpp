#include "Utility/ModLoadOrderHelper.h"
#include "Utility/Logging.h"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <fstream>
#include <queue>
#include <unordered_map>
#include <unordered_set>

namespace fs = std::filesystem;

namespace PS {
    std::vector<ModLoadOrderEntry> ModLoadOrderHelper::Resolve(const std::filesystem::path& modsPath)
    {
        auto mods = DiscoverMods(modsPath);
        auto settings = LoadSettings(modsPath);
        auto filteredMods = FilterDisabledMods(mods, settings);
        return SortMods(filteredMods, settings);
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
        ModLoadOrderEntry mod;
        mod.modPath = modPath;
        mod.folderName = modPath.filename().string();
        mod.modId = mod.folderName;
        mod.usesFallbackModId = true;

        auto metadataPath = FindMetadataPath(modPath);
        if (metadataPath.empty())
        {
            return mod;
        }

        auto metadata = LoadJsonFile(metadataPath);
        if (!metadata.is_object())
        {
            return mod;
        }

        if (metadata.contains("mod_id") && metadata.at("mod_id").is_string())
        {
            mod.modId = metadata.at("mod_id").get<std::string>();
            mod.usesFallbackModId = false;
        }

        if (metadata.contains("name") && metadata.at("name").is_string())
        {
            mod.name = metadata.at("name").get<std::string>();
        }

        if (metadata.contains("version") && metadata.at("version").is_string())
        {
            mod.version = metadata.at("version").get<std::string>();
        }

        mod.authors = ReadStringArray(metadata, "authors");

        if (metadata.contains("load_priority") && metadata.at("load_priority").is_number_integer())
        {
            mod.loadPriority = metadata.at("load_priority").get<int>();
            mod.hasLoadPriority = true;
        }

        mod.dependencies = ReadStringArray(metadata, "dependencies");
        mod.loadAfter = ReadStringArray(metadata, "load_after");
        mod.loadBefore = ReadStringArray(metadata, "load_before");

        if (mod.usesFallbackModId)
        {
            PS::Log<RC::LogLevel::Warning>(STR("Mod '{}' does not declare 'mod_id' in metadata. Falling back to folder name.\n"), RC::to_generic_string(mod.folderName));
        }

        return mod;
    }

    ModLoadOrderSettings ModLoadOrderHelper::LoadSettings(const std::filesystem::path& modsPath) const
    {
        ModLoadOrderSettings settings;

        auto loadOrderPath = FindLoadOrderPath(modsPath);
        if (loadOrderPath.empty())
        {
            PS::Log<RC::LogLevel::Warning>(STR("No load_order.json or load_order.jsonc found in '{}'.\n"), RC::to_generic_string(modsPath.string()));
            return settings;
        }

        PS::Log<RC::LogLevel::Verbose>(STR("Using load order settings from '{}'.\n"), RC::to_generic_string(loadOrderPath.string()));

        auto data = LoadJsonFile(loadOrderPath);
        if (!data.is_object())
        {
            return settings;
        }

        settings.loadFirst = ReadStringArray(data, "load_first");
        settings.explicitOrder = ReadStringArray(data, "explicit_order");
        settings.loadLast = ReadStringArray(data, "load_last");
        settings.disabledMods = ReadStringArray(data, "disabled_mods");

        PS::Log<RC::LogLevel::Verbose>(STR("Load order settings loaded: load_first={}, explicit_order={}, load_last={}, disabled_mods={}.\n"), settings.loadFirst.size(), settings.explicitOrder.size(), settings.loadLast.size(), settings.disabledMods.size());

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
        auto jsonPath = modsPath / "load_order.json";
        if (fs::exists(jsonPath) && fs::is_regular_file(jsonPath))
        {
            return jsonPath;
        }

        auto jsoncPath = modsPath / "load_order.jsonc";
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

    std::vector<ModLoadOrderEntry> ModLoadOrderHelper::SortMods(const std::vector<ModLoadOrderEntry>& mods, const ModLoadOrderSettings& settings) const
    {
        std::vector<ModLoadOrderEntry> sortedMods;
        if (mods.empty())
        {
            return sortedMods;
        }

        std::unordered_map<std::string, size_t> modIndexById;
        std::unordered_map<std::string, size_t> loadFirstRank;
        std::unordered_map<std::string, size_t> explicitRank;
        std::unordered_map<std::string, size_t> loadLastRank;
        std::unordered_set<std::string> loadFirstIds(settings.loadFirst.begin(), settings.loadFirst.end());
        std::unordered_set<std::string> explicitIds(settings.explicitOrder.begin(), settings.explicitOrder.end());
        std::unordered_set<std::string> loadLastIds(settings.loadLast.begin(), settings.loadLast.end());

        for (size_t index = 0; index < mods.size(); ++index)
        {
            modIndexById.emplace(mods[index].modId, index);
        }

        for (size_t index = 0; index < settings.loadFirst.size(); ++index)
        {
            loadFirstRank.emplace(settings.loadFirst[index], index);
        }

        for (size_t index = 0; index < settings.explicitOrder.size(); ++index)
        {
            explicitRank.emplace(settings.explicitOrder[index], index);
        }

        for (size_t index = 0; index < settings.loadLast.size(); ++index)
        {
            loadLastRank.emplace(settings.loadLast[index], index);
        }

        for (const auto& modId : settings.loadFirst)
        {
            if (!modIndexById.contains(modId))
            {
                PS::Log<RC::LogLevel::Warning>(STR("load_first references unknown mod id '{}'.\n"), RC::to_generic_string(modId));
            }
        }

        for (const auto& modId : settings.explicitOrder)
        {
            if (!modIndexById.contains(modId))
            {
                PS::Log<RC::LogLevel::Warning>(STR("explicit_order references unknown mod id '{}'.\n"), RC::to_generic_string(modId));
            }
        }

        for (const auto& modId : settings.loadLast)
        {
            if (!modIndexById.contains(modId))
            {
                PS::Log<RC::LogLevel::Warning>(STR("load_last references unknown mod id '{}'.\n"), RC::to_generic_string(modId));
            }
        }

        std::vector<std::unordered_set<size_t>> edges(mods.size());
        std::vector<size_t> indegree(mods.size(), 0);
        std::unordered_set<uint64_t> metadataEdgeKeys;

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

        auto addMetadataEdge = [&](size_t fromIndex, size_t toIndex) {
            addEdge(fromIndex, toIndex);
            metadataEdgeKeys.insert(makeEdgeKey(fromIndex, toIndex));
        };

        auto addOverrideEdge = [&](size_t fromIndex, size_t toIndex, const char* sourceField) {
            if (metadataEdgeKeys.contains(makeEdgeKey(toIndex, fromIndex)))
            {
                PS::Log<RC::LogLevel::Warning>(
                    STR("Skipping '{}' ordering edge because it conflicts with metadata dependency/order rules: '{}' -> '{}'.\n"),
                    RC::to_generic_string(sourceField),
                    RC::to_generic_string(mods[fromIndex].modId),
                    RC::to_generic_string(mods[toIndex].modId)
                );
                return;
            }

            addEdge(fromIndex, toIndex);
        };

        for (size_t index = 0; index < mods.size(); ++index)
        {
            const auto& mod = mods[index];

            for (const auto& dependency : mod.dependencies)
            {
                if (auto it = modIndexById.find(dependency); it != modIndexById.end())
                {
                    addMetadataEdge(it->second, index);
                }
                else
                {
                    PS::Log<RC::LogLevel::Warning>(STR("Mod '{}' depends on unknown mod id '{}'.\n"), RC::to_generic_string(mod.modId), RC::to_generic_string(dependency));
                }
            }

            for (const auto& dependency : mod.loadAfter)
            {
                if (auto it = modIndexById.find(dependency); it != modIndexById.end())
                {
                    addMetadataEdge(it->second, index);
                }
                else
                {
                    PS::Log<RC::LogLevel::Warning>(STR("Mod '{}' load_after references unknown mod id '{}'.\n"), RC::to_generic_string(mod.modId), RC::to_generic_string(dependency));
                }
            }

            for (const auto& dependency : mod.loadBefore)
            {
                if (auto it = modIndexById.find(dependency); it != modIndexById.end())
                {
                    addMetadataEdge(index, it->second);
                }
                else
                {
                    PS::Log<RC::LogLevel::Warning>(STR("Mod '{}' load_before references unknown mod id '{}'.\n"), RC::to_generic_string(mod.modId), RC::to_generic_string(dependency));
                }
            }
        }

        for (size_t index = 1; index < settings.loadFirst.size(); ++index)
        {
            auto previous = modIndexById.find(settings.loadFirst[index - 1]);
            auto current = modIndexById.find(settings.loadFirst[index]);
            if (previous != modIndexById.end() && current != modIndexById.end())
            {
                addOverrideEdge(previous->second, current->second, "load_first");
            }
        }

        for (size_t index = 1; index < settings.explicitOrder.size(); ++index)
        {
            auto previous = modIndexById.find(settings.explicitOrder[index - 1]);
            auto current = modIndexById.find(settings.explicitOrder[index]);
            if (previous != modIndexById.end() && current != modIndexById.end())
            {
                addOverrideEdge(previous->second, current->second, "explicit_order");
            }
        }

        for (size_t index = 1; index < settings.loadLast.size(); ++index)
        {
            auto previous = modIndexById.find(settings.loadLast[index - 1]);
            auto current = modIndexById.find(settings.loadLast[index]);
            if (previous != modIndexById.end() && current != modIndexById.end())
            {
                addOverrideEdge(previous->second, current->second, "load_last");
            }
        }

        for (size_t left = 0; left < mods.size(); ++left)
        {
            for (size_t right = 0; right < mods.size(); ++right)
            {
                if (left == right)
                {
                    continue;
                }

                const auto& leftMod = mods[left];
                const auto& rightMod = mods[right];

                if (loadFirstIds.contains(leftMod.modId) && !loadFirstIds.contains(rightMod.modId))
                {
                    addOverrideEdge(left, right, "load_first");
                }

                if (explicitIds.contains(leftMod.modId) && !loadFirstIds.contains(rightMod.modId) && !explicitIds.contains(rightMod.modId))
                {
                    addOverrideEdge(left, right, "explicit_order");
                }

                if (loadLastIds.contains(rightMod.modId) && !loadLastIds.contains(leftMod.modId))
                {
                    addOverrideEdge(left, right, "load_last");
                }
            }
        }

        auto compareIndices = [&](size_t leftIndex, size_t rightIndex) {
            const auto getRank = [](const auto& ranks, const std::string& modId) {
                auto it = ranks.find(modId);
                return it == ranks.end() ? std::numeric_limits<size_t>::max() : it->second;
            };

            const auto& left = mods[leftIndex];
            const auto& right = mods[rightIndex];

            auto leftLoadFirstRank = getRank(loadFirstRank, left.modId);
            auto rightLoadFirstRank = getRank(loadFirstRank, right.modId);
            if (leftLoadFirstRank != rightLoadFirstRank)
            {
                return leftLoadFirstRank > rightLoadFirstRank;
            }

            auto leftExplicitRank = getRank(explicitRank, left.modId);
            auto rightExplicitRank = getRank(explicitRank, right.modId);
            if (leftExplicitRank != rightExplicitRank)
            {
                return leftExplicitRank > rightExplicitRank;
            }

            if (left.hasLoadPriority != right.hasLoadPriority)
            {
                return left.hasLoadPriority < right.hasLoadPriority;
            }

            if (left.hasLoadPriority && right.hasLoadPriority && left.loadPriority != right.loadPriority)
            {
                return left.loadPriority > right.loadPriority;
            }

            auto leftLoadLastRank = getRank(loadLastRank, left.modId);
            auto rightLoadLastRank = getRank(loadLastRank, right.modId);
            if (leftLoadLastRank != rightLoadLastRank)
            {
                return leftLoadLastRank > rightLoadLastRank;
            }

            return left.folderName > right.folderName;
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

    std::vector<std::string> ModLoadOrderHelper::ReadStringArray(const nlohmann::json& data, const std::string& fieldName) const
    {
        std::vector<std::string> values;

        if (!data.contains(fieldName))
        {
            return values;
        }

        const auto& field = data.at(fieldName);
        if (!field.is_array())
        {
            PS::Log<RC::LogLevel::Warning>(STR("Field '{}' must be an array of strings.\n"), RC::to_generic_string(fieldName));
            return values;
        }

        for (const auto& item : field)
        {
            if (!item.is_string())
            {
                PS::Log<RC::LogLevel::Warning>(STR("Field '{}' contains a non-string entry.\n"), RC::to_generic_string(fieldName));
                continue;
            }

            values.push_back(item.get<std::string>());
        }

        return values;
    }

    nlohmann::json ModLoadOrderHelper::LoadJsonFile(const std::filesystem::path& path) const
    {
        std::ifstream stream(path);
        if (!stream.is_open())
        {
            PS::Log<RC::LogLevel::Warning>(STR("Unable to open JSON file '{}'.\n"), RC::to_generic_string(path.string()));
            return nlohmann::json::object();
        }

        try
        {
            auto ignoreComments = path.extension() == ".jsonc";
            return nlohmann::json::parse(stream, nullptr, true, ignoreComments);
        }
        catch (const std::exception& e)
        {
            PS::Log<RC::LogLevel::Error>(STR("Failed to parse JSON file '{}' - {}\n"), RC::to_generic_string(path.string()), RC::to_generic_string(e.what()));
            return nlohmann::json::object();
        }
    }
}
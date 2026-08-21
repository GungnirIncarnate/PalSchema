#include "Utility/LoadOrderSystem/ModLoadOrderHelper.h"
#include "Utility/LoadOrderSystem/ModLoadOrderSorter.h"
#include "Utility/Logging.h"
#include <algorithm>
#include <fstream>
#include <sstream>

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

    ModLoadPlan ModLoadOrderHelper::Resolve(const std::filesystem::path& modsPath)
    {
        ModLoadPlan plan;
        plan.modsPath = modsPath;
        auto mods = DiscoverMods(modsPath);
        plan.settings = LoadSettings(modsPath);

        plan.entries.reserve(mods.size());
        for (auto& mod : mods)
        {
            plan.entries.push_back({ std::move(mod), ModLoadStatus::Loaded, {} });
        }

        std::unordered_map<std::string, std::vector<std::string>> foldersByModId;
        for (const auto& entry : plan.entries)
        {
            foldersByModId[entry.mod.modId].push_back(entry.mod.folderName);
        }

        for (auto& entry : plan.entries)
        {
            const auto& conflictingFolders = foldersByModId[entry.mod.modId];
            if (conflictingFolders.size() > 1)
            {
                std::ostringstream reason;
                reason << "Duplicate ID with ";
                bool hasOtherFolder = false;
                for (const auto& folderName : conflictingFolders)
                {
                    if (folderName == entry.mod.folderName)
                    {
                        continue;
                    }

                    if (hasOtherFolder)
                    {
                        reason << ", ";
                    }

                    reason << "'" << folderName << "'";
                    hasOtherFolder = true;
                }

                entry.status = ModLoadStatus::DuplicateId;
                entry.reason = reason.str();
            }
        }

        std::unordered_set<std::string> disabledIds(plan.settings.disabledMods.begin(), plan.settings.disabledMods.end());
        for (auto& entry : plan.entries)
        {
            if (entry.status == ModLoadStatus::Loaded && disabledIds.contains(entry.mod.modId))
            {
                entry.status = ModLoadStatus::Disabled;
                entry.reason = "Listed in disabled_mods";
            }
        }

        std::vector<ModLoadOrderEntry> activeMods;
        for (const auto& entry : plan.entries)
        {
            if (entry.status == ModLoadStatus::Loaded)
            {
                activeMods.push_back(entry.mod);
            }
        }

        bool hasChanges = true;
        while (hasChanges)
        {
            hasChanges = false;
            std::unordered_set<std::string> activeIds;
            for (const auto& entry : plan.entries)
            {
                if (entry.status == ModLoadStatus::Loaded)
                {
                    activeIds.insert(entry.mod.modId);
                }
            }

            for (auto& entry : plan.entries)
            {
                if (entry.status != ModLoadStatus::Loaded)
                {
                    continue;
                }

                for (const auto& dependency : entry.mod.dependencies)
                {
                    if (!activeIds.contains(dependency))
                    {
                        entry.status = ModLoadStatus::MissingDependency;
                        entry.reason = "Required dependency '" + dependency + "' is missing or disabled";
                        hasChanges = true;
                        break;
                    }
                }
            }
        }

        activeMods.clear();
        for (const auto& entry : plan.entries)
        {
            if (entry.status == ModLoadStatus::Loaded)
            {
                activeMods.push_back(entry.mod);
            }
        }

        ModLoadOrderSorter sorter;
        plan.orderedMods = sorter.SortMods(activeMods, plan.settings);

        std::unordered_map<std::string, size_t> orderedIndices;
        for (size_t index = 0; index < plan.orderedMods.size(); ++index)
        {
            orderedIndices[plan.orderedMods[index].folderName] = index;
        }

        std::stable_sort(plan.entries.begin(), plan.entries.end(), [&](const auto& left, const auto& right) {
            const bool leftLoaded = left.status == ModLoadStatus::Loaded;
            const bool rightLoaded = right.status == ModLoadStatus::Loaded;
            if (leftLoaded != rightLoaded)
            {
                return !leftLoaded;
            }

            if (!leftLoaded)
            {
                return left.mod.folderName < right.mod.folderName;
            }

            return orderedIndices[left.mod.folderName] < orderedIndices[right.mod.folderName];
        });

        plan.displayEntries = plan.entries;
        return plan;
    }

    std::vector<ModLoadOrderEntry> ModLoadOrderHelper::DiscoverMods(const std::filesystem::path& modsPath) const
    {
        std::vector<ModLoadOrderEntry> mods;

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

}
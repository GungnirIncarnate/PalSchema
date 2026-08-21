#include "Utility/LoadOrderSystem/ModLoadOrderHelper.h"
#include "Utility/LoadOrderSystem/ModLoadOrderSorter.h"
#include "Utility/Logging.h"
#include <fstream>
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
        ModLoadOrderSorter sorter;
        auto filteredMods = sorter.FilterDisabledMods(mods, settings);
        auto dependencyValidMods = sorter.FilterMissingDependencies(filteredMods);
        return sorter.SortMods(dependencyValidMods, settings);
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

        std::unordered_map<std::string, std::vector<std::string>> foldersByModId;
        for (const auto& mod : mods)
        {
            foldersByModId[mod.modId].push_back(mod.folderName);
        }

        std::unordered_set<std::string> conflictingModIds;
        for (const auto& [modId, folderNames] : foldersByModId)
        {
            if (folderNames.size() > 1)
            {
                std::ostringstream folders;
                for (size_t index = 0; index < folderNames.size(); ++index)
                {
                    if (index > 0)
                    {
                        folders << ", ";
                    }

                    folders << "'" << folderNames[index] << "'";
                }

                PS::Log<RC::LogLevel::Error>(
                    STR("Duplicate mod id '{}' found in folders {}. Skipping all conflicting mods.\n"),
                    RC::to_generic_string(modId),
                    RC::to_generic_string(folders.str())
                );
                conflictingModIds.insert(modId);
            }
        }

        std::erase_if(mods, [&](const auto& mod) {
            return conflictingModIds.contains(mod.modId);
        });

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
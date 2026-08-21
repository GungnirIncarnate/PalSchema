#pragma once

#include <filesystem>
#include <string>
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

    enum class ModLoadStatus
    {
        Loaded,
        Disabled,
        DuplicateId,
        MissingDependency,
        DependencySkipped,
        LoadFailed,
        LoadOrderConflict
    };

    struct ModLoadPlanEntry
    {
        ModLoadOrderEntry mod{};
        ModLoadStatus status = ModLoadStatus::Loaded;
        std::string reason{};
    };

    struct ModLoadPlan
    {
        std::filesystem::path modsPath{};
        ModLoadOrderSettings settings{};
        std::vector<ModLoadPlanEntry> entries{};
        std::vector<ModLoadOrderEntry> orderedMods{};
        std::vector<ModLoadPlanEntry> displayEntries{};
    };

    class ModLoadOrderHelper {
    public:
        ModLoadPlan Resolve(const std::filesystem::path& modsPath);

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
    };
}
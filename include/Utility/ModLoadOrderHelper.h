#pragma once

#include <filesystem>
#include <string>
#include <vector>
#include "nlohmann/json_fwd.hpp"

namespace PS {
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
        std::vector<ModLoadOrderEntry> DiscoverMods(const std::filesystem::path& modsPath) const;
        ModLoadOrderEntry LoadModMetadata(const std::filesystem::path& modPath) const;
        ModLoadOrderSettings LoadSettings(const std::filesystem::path& modsPath) const;

        std::filesystem::path FindMetadataPath(const std::filesystem::path& modPath) const;
        std::filesystem::path FindLoadOrderPath(const std::filesystem::path& modsPath) const;

        std::vector<ModLoadOrderEntry> FilterDisabledMods(const std::vector<ModLoadOrderEntry>& mods, const ModLoadOrderSettings& settings) const;
        std::vector<ModLoadOrderEntry> FilterMissingDependencies(const std::vector<ModLoadOrderEntry>& mods) const;
        std::vector<ModLoadOrderEntry> SortMods(const std::vector<ModLoadOrderEntry>& mods, const ModLoadOrderSettings& settings) const;

        std::vector<std::string> ReadStringArray(const nlohmann::json& data, const std::string& fieldName) const;
        nlohmann::json LoadJsonFile(const std::filesystem::path& path) const;
    };
}
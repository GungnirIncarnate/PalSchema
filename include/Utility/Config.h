#pragma once

#include <string>

namespace PS {
    class PSConfig {
    public:
        static void Load();

        static std::string GetLanguageOverride();
        
        static bool IsExperimentalBlueprintSupportEnabled();
    private:
        static inline std::unique_ptr<PSConfig> m_config;
    public:
    private:
        std::string m_languageOverride = "";
        bool m_enableExperimentalBlueprintSupport = false;
    };
}
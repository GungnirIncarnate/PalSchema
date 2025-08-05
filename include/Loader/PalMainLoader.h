#pragma once

#include <vector>
#include <functional>
#include "Loader/PalMonsterModLoader.h"
#include "Loader/PalHumanModLoader.h"
#include "Loader/PalLanguageModLoader.h"
#include "Loader/PalItemModLoader.h"
#include "Loader/PalSkinModLoader.h"
#include "Loader/PalAppearanceModLoader.h"
#include "Loader/PalBuildingModLoader.h"
#include "Loader/PalRawTableLoader.h"
#include "Loader/PalBlueprintModLoader.h"
#include "Loader/PalEnumLoader.h"
#include "FileWatch.hpp"

namespace RC::Unreal {
    class AGameModeBase;
}

namespace UECustom {
    class UDataTable;
    class UCompositeDataTable;
}

namespace Palworld {
	class PalMainLoader {
	public:
        PalMainLoader();

        ~PalMainLoader();

		void PreInitialize();

		void Initialize();

        // Should be called in Game Thread
        void ReloadMods();
	private:
		PalLanguageModLoader LanguageModLoader;
		PalMonsterModLoader MonsterModLoader;
		PalHumanModLoader HumanModLoader;
		PalItemModLoader ItemModLoader;
		PalSkinModLoader SkinModLoader;
		PalAppearanceModLoader AppearanceModLoader;
		PalBuildingModLoader BuildingModLoader;
		PalRawTableLoader RawTableLoader;
		PalBlueprintModLoader BlueprintModLoader;
        PalEnumLoader EnumLoader;

        int m_errorCount = 0;

        std::unique_ptr<filewatch::FileWatch<std::wstring>> m_fileWatch;

        void SetupAutoReload();

        // Makes PalSchema read paks from the 'PalSchema/mods' folder. Although the paks can be anywhere, prefer for them to be put inside 'YourModName/paks'.
        // This is intended for custom assets like models or textures that are part of a schema mod which makes it easier for modders to package their mod.
        // Requires a signature for FPakPlatformFile::GetPakFolders, otherwise this feature will not be available.
        void SetupAlternativePakPathReader();

        void InitCore();

        void InitLoaders();

		void Load();

		void LoadLanguageMods(const std::filesystem::path& path);

		void LoadPalMods(const std::filesystem::path& path);

		void LoadBuildingMods(const std::filesystem::path& path);

		void LoadRawTables(const std::filesystem::path& path);

		void LoadBlueprintMods(const std::filesystem::path& path);

		void LoadBlueprintModsSafe(const std::filesystem::path& path);

		void LoadAppearanceMods(const std::filesystem::path& path);

        void LoadItemMods(const std::filesystem::path& path);

        void LoadSkinMods(const std::filesystem::path& path);

        void LoadCustomEnums();

        void IterateModsFolder(const std::function<void(const std::filesystem::directory_entry&)>& callback);

        void ParseJsonFileInPath(const std::filesystem::path& path, const std::function<void(const nlohmann::json&)>& callback);

        void ParseJsonFilesInPath(const std::filesystem::path& path, const std::function<void(const nlohmann::json&)>& callback);
    private:
        static void PostLoad(RC::Unreal::UClass* This);

        static void GetPakFolders(const RC::Unreal::TCHAR* CmdLine, RC::Unreal::TArray<RC::Unreal::FString>* OutPakFolders);

        static void OnDataTableSerialized(UECustom::UDataTable* This, RC::Unreal::FArchive* Archive);

        static void OnGameInstanceInit(RC::Unreal::UObject* This);

        bool m_hasInit = false;

        static inline std::vector<std::function<void(UECustom::UDataTable*)>> DatatableSerializeCallbacks;
        static inline std::vector<std::function<void(RC::Unreal::UObject*)>> GameInstanceInitCallbacks;
        static inline std::vector<std::function<void(RC::Unreal::UClass*)>> PostLoadCallbacks;
        static inline std::vector<std::function<void()>> GetPakFoldersCallback;

        static inline SafetyHookInline DatatableSerialize_Hook;
        static inline SafetyHookInline GameInstanceInit_Hook;
        static inline SafetyHookInline PostLoad_Hook;
        static inline SafetyHookInline GetPakFolders_Hook;
	};
}
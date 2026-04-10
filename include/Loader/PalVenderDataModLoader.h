#pragma once

#include "Loader/PalModLoaderBase.h"
#include "nlohmann/json.hpp"

namespace RC::Unreal {
    class UDataTable;
    class FName;
}

namespace Palworld {
    class PalVenderDataModLoader : public PalModLoaderBase {
    public:
        PalVenderDataModLoader();
        ~PalVenderDataModLoader();

    protected:
        virtual void OnLoad(const std::filesystem::path& loaderPath, const RC::StringType& modName, const EEngineLifecyclePhase& engineLifecyclePhase) override final;
        virtual std::filesystem::path ResolveLoaderPath(const std::filesystem::path& modPath) const override final;

        virtual bool CanInitialize(const EEngineLifecyclePhase& engineLifecyclePhase) override final;
        virtual bool OnInitialize() override final;

    private:
        void LoadVenderData(const nlohmann::json& data);
        void AddOrEditShop(const RC::Unreal::FName& characterId, const nlohmann::json& data);

        RC::Unreal::UDataTable* m_itemShopLotteryDataTable = nullptr;
        RC::Unreal::UDataTable* m_itemShopCreateDataTable = nullptr;
        RC::Unreal::UDataTable* m_itemShopSettingDataTable = nullptr;
        RC::Unreal::UDataTable* m_palShopCreateDataTable = nullptr;
    };
}

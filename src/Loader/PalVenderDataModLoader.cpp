#include "Unreal/CoreUObject/UObject/Class.hpp"
#include "Unreal/Engine/UDataTable.hpp"
#include "Unreal/FProperty.hpp"
#include "SDK/Structs/FPalItemShopLotteryDataRow.h"
#include "SDK/Structs/FPalItemShopSettingDataRow.h"
#include "SDK/Structs/Custom/FManagedStruct.h"
#include "SDK/Helper/PropertyHelper.h"
#include "Utility/JsonHelpers.h"
#include "Utility/Logging.h"
#include "Loader/PalVenderDataModLoader.h"

using namespace RC;
using namespace RC::Unreal;

namespace Palworld {
    PalVenderDataModLoader::PalVenderDataModLoader() : PalModLoaderBase("venderdata")
    {
        SetDisplayName(TEXT("NPC Vender Data Loader"));
    }

    PalVenderDataModLoader::~PalVenderDataModLoader() {}

    bool PalVenderDataModLoader::CanInitialize(const EEngineLifecyclePhase& engineLifecyclePhase)
    {
        return engineLifecyclePhase == EEngineLifecyclePhase::GameInstanceInit;
    }

    bool PalVenderDataModLoader::OnInitialize()
    {
        try
        {
            m_itemShopLotteryDataTable = GetDatatableByName("DT_ItemShopLotteryData");
            m_itemShopCreateDataTable = GetDatatableByName("DT_ItemShopCreateData");
            m_itemShopSettingDataTable = GetDatatableByName("DT_ItemShopSettingData");
            m_palShopCreateDataTable = GetDatatableByName("DT_PalShopCreateData");
        }
        catch (const std::exception& e)
        {
            PS::Log<LogLevel::Error>(STR("Unable to initialize {}, {}\n"), GetDisplayName(), RC::to_generic_string(e.what()));
            return false;
        }

        return true;
    }

    std::filesystem::path PalVenderDataModLoader::ResolveLoaderPath(const std::filesystem::path& modPath) const
    {
        return modPath / "npcs" / "venderdata";
    }

    void PalVenderDataModLoader::OnLoad(const std::filesystem::path& loaderPath, const RC::StringType& modName, const EEngineLifecyclePhase& engineLifecyclePhase)
    {
        if (engineLifecyclePhase != EEngineLifecyclePhase::GameInstanceInit)
        {
            return;
        }

        PS::JsonHelpers::ParseJsonFilesInPath(loaderPath, [&](const nlohmann::json& data) {
            LoadVenderData(data);
        });
    }

    void PalVenderDataModLoader::LoadVenderData(const nlohmann::json& data)
    {
        for (auto& [characterIdString, properties] : data.items())
        {
            if (!properties.is_object())
            {
                PS::Log<RC::LogLevel::Error>(
                    STR("Vender data for {} must be an object.\n"),
                    RC::to_generic_string(characterIdString)
                );
                continue;
            }

            const auto characterId = FName(RC::to_generic_string(characterIdString), FNAME_Add);
            AddOrEditShop(characterId, properties);
        }
    }

    void PalVenderDataModLoader::AddOrEditShop(const RC::Unreal::FName& characterId, const nlohmann::json& data)
    {
        if (!data.is_object())
        {
            PS::Log<RC::LogLevel::Error>(STR("Shop entry for {} must be an object, skipping.\n"), characterId.ToString());
            return;
        }

        const nlohmann::json* itemShopData = nullptr;
        const nlohmann::json* palShopData = nullptr;

        if (data.contains("ItemShop"))
        {
            if (!data.at("ItemShop").is_object())
            {
                PS::Log<RC::LogLevel::Error>(STR("ItemShop for {} must be an object, skipping item shop data.\n"), characterId.ToString());
            }
            else
            {
                itemShopData = &data.at("ItemShop");
            }
        }
        if (data.contains("PalShop"))
        {
            if (!data.at("PalShop").is_object())
            {
                PS::Log<RC::LogLevel::Error>(STR("PalShop for {} must be an object, skipping pal shop data.\n"), characterId.ToString());
            }
            else
            {
                palShopData = &data.at("PalShop");
            }
        }

        if (!itemShopData && !palShopData)
        {
            PS::Log<RC::LogLevel::Warning>(STR("No ItemShop or PalShop data found for {}, skipping entry.\n"), characterId.ToString());
            return;
        }

        bool addSucceeded = true;
        bool processedItemShop = false;
        bool processedPalShop = false;
        std::string itemShopTableIdRaw;

        if (itemShopData)
        {
            processedItemShop = true;

            if (!itemShopData->is_object())
            {
                PS::Log<RC::LogLevel::Error>(STR("Item shop entry for {} must be an object, skipping item shop data.\n"), characterId.ToString());
                addSucceeded = false;
            }
            else if (!itemShopData->contains("TableId") || !itemShopData->at("TableId").is_string())
            {
                PS::Log<RC::LogLevel::Error>(STR("ItemShop table id was not specified for {}, skipping item shop data.\n"), characterId.ToString());
                addSucceeded = false;
            }
            else if (!itemShopData->contains("Currency") || !itemShopData->at("Currency").is_string())
            {
                PS::Log<RC::LogLevel::Error>(STR("ItemShop Currency in {} must be a string, skipping item shop data.\n"), characterId.ToString());
                addSucceeded = false;
            }
            else if (!itemShopData->contains("Items") || !itemShopData->at("Items").is_array())
            {
                PS::Log<RC::LogLevel::Error>(STR("ItemShop Items in {} must be an array, skipping item shop data.\n"), characterId.ToString());
                addSucceeded = false;
            }
            else
            {
                itemShopTableIdRaw = itemShopData->at("TableId").get<std::string>();
                const auto itemShopTableId = RC::to_generic_string(itemShopTableIdRaw);

                if (m_itemShopLotteryDataTable)
                {
                    FPalItemShopLotteryEntry entry{};
                    entry.ShopGroupName = characterId;
                    entry.Weight = 100;

                    FPalItemShopLotteryDataRow row{};
                    row.lotteryDataArray.Add(entry);

                    m_itemShopLotteryDataTable->AddRow(FName(itemShopTableId, FNAME_Add), row);
                    if (!m_itemShopLotteryDataTable->FindRowUnchecked(FName(itemShopTableId, FNAME_Add))) addSucceeded = false;
                }
                else
                {
                    PS::Log<RC::LogLevel::Warning>(STR("ItemShopLotteryDataTable not found, skipping adding lottery row for {}\n"), characterId.ToString());
                    addSucceeded = false;
                }

                if (m_itemShopCreateDataTable)
                {
                    auto createRowJson = nlohmann::json::object();
                    createRowJson["productDataArray"] = itemShopData->at("Items");

                    auto existingCreateRow = m_itemShopCreateDataTable->FindRowUnchecked(characterId);
                    auto createRowStruct = m_itemShopCreateDataTable->GetRowStruct().Get();
                    if (existingCreateRow)
                    {
                        try
                        {
                            for (FProperty* property : TFieldRange<FProperty>(createRowStruct, EFieldIterationFlags::IncludeSuper))
                            {
                                auto propertyName = RC::to_string(property->GetName());
                                if (createRowJson.contains(propertyName))
                                {
                                    PropertyHelper::CopyJsonValueToContainer(existingCreateRow, property, createRowJson.at(propertyName));
                                }
                            }
                            if (!m_itemShopCreateDataTable->FindRowUnchecked(characterId)) addSucceeded = false;
                        }
                        catch (const std::exception& e)
                        {
                            PS::Log<RC::LogLevel::Error>(STR("Failed to modify Row '{}' in {}: {}\n"), characterId.ToString(), m_itemShopCreateDataTable->GetFullName(), RC::to_generic_string(e.what()));
                            addSucceeded = false;
                        }
                    }
                    else
                    {
                        FManagedStruct createRowData{ createRowStruct };
                        try
                        {
                            for (FProperty* property : TFieldRange<FProperty>(createRowStruct, EFieldIterationFlags::IncludeSuper))
                            {
                                auto propertyName = RC::to_string(property->GetName());
                                if (createRowJson.contains(propertyName))
                                {
                                    PropertyHelper::CopyJsonValueToContainer(createRowData.GetData(), property, createRowJson.at(propertyName));
                                }
                            }
                            m_itemShopCreateDataTable->AddRow(characterId, *reinterpret_cast<RC::Unreal::FTableRowBase*>(createRowData.GetData()));
                            if (!m_itemShopCreateDataTable->FindRowUnchecked(characterId)) addSucceeded = false;
                        }
                        catch (const std::exception& e)
                        {
                            PS::Log<RC::LogLevel::Error>(STR("Failed to add Row '{}' to {}: {}\n"), characterId.ToString(), m_itemShopCreateDataTable->GetFullName(), RC::to_generic_string(e.what()));
                            addSucceeded = false;
                        }
                    }
                }
                else
                {
                    PS::Log<RC::LogLevel::Warning>(STR("ItemShopCreateDataTable not found, skipping adding create-data row for {}\n"), characterId.ToString());
                    addSucceeded = false;
                }

                if (m_itemShopSettingDataTable)
                {
                    auto currencyStr = RC::to_generic_string(itemShopData->at("Currency").get<std::string>());
                    FPalItemShopSettingDataRow settingRow{ currencyStr };

                    auto existingRow = m_itemShopSettingDataTable->FindRowUnchecked(characterId);
                    if (existingRow)
                    {
                        auto rowStruct = m_itemShopSettingDataTable->GetRowStruct().Get();
                        auto currencyProp = rowStruct->GetPropertyByName(STR("CurrencyItemID"));
                        if (currencyProp)
                        {
                            PropertyHelper::CopyJsonValueToContainer(existingRow, currencyProp, currencyStr);
                        }
                        else
                        {
                            PS::Log<RC::LogLevel::Warning>(STR("CurrencyItemID property not found in ItemShopSettingData row struct, skipping update for {}\n"), characterId.ToString());
                        }
                        if (!m_itemShopSettingDataTable->FindRowUnchecked(characterId)) addSucceeded = false;
                    }
                    else
                    {
                        m_itemShopSettingDataTable->AddRow(characterId, settingRow);
                        if (!m_itemShopSettingDataTable->FindRowUnchecked(characterId)) addSucceeded = false;
                    }
                }
                else
                {
                    PS::Log<RC::LogLevel::Warning>(STR("ItemShopSettingDataTable not found, skipping adding setting row for {}\n"), characterId.ToString());
                    addSucceeded = false;
                }
            }
        }

        if (palShopData)
        {
            processedPalShop = true;

            if (!m_palShopCreateDataTable)
            {
                PS::Log<RC::LogLevel::Warning>(STR("PalShopCreateDataTable not found, skipping adding pal shop row for {}\n"), characterId.ToString());
                addSucceeded = false;
            }
            else
            {
                    if (!palShopData->contains("TableId") || !palShopData->at("TableId").is_string())
                {
                    PS::Log<RC::LogLevel::Error>(STR("PalShop table id was not specified in {}, skipping pal shop data.\n"), characterId.ToString());
                    addSucceeded = false;
                }
                    else
                {
                        const auto palEntry = palShopData;
                        const auto palShopTableIdRaw = palShopData->at("TableId").get<std::string>();

                    auto palRowJson = nlohmann::json::object();

                        if (palEntry->contains("MaxLostPalNum"))
                        {
                            palRowJson["MaxLostPalNum"] = palEntry->at("MaxLostPalNum");
                        }
                        if (palEntry->contains("CharacterNum"))
                        {
                            palRowJson["CharacterNum"] = palEntry->at("CharacterNum");
                        }
                        if (palEntry->contains("MinCharacterLevel"))
                        {
                            palRowJson["MinCharacterLevel"] = palEntry->at("MinCharacterLevel");
                        }
                        if (palEntry->contains("MaxCharacterLevel"))
                        {
                            palRowJson["MaxCharacterLevel"] = palEntry->at("MaxCharacterLevel");
                        }

                        if (palEntry->contains("CharacterIDArray"))
                        {
                            if (!palEntry->at("CharacterIDArray").is_array())
                            {
                                PS::Log<RC::LogLevel::Error>(STR("CharacterIDArray in {} PalShop entry must be an array, skipping pal shop data.\n"), characterId.ToString());
                                addSucceeded = false;
                            }
                            else
                            {
                                auto normalizedCharacterIds = nlohmann::json::array();
                                for (const auto& value : palEntry->at("CharacterIDArray"))
                                {
                                    if (value.is_string())
                                    {
                                        normalizedCharacterIds.push_back(nlohmann::json{ { "Key", value.get<std::string>() } });
                                    }
                                    else if (value.is_object() && value.contains("Key") && value.at("Key").is_string())
                                    {
                                        normalizedCharacterIds.push_back(value);
                                    }
                                    else
                                    {
                                        PS::Log<RC::LogLevel::Warning>(STR("Invalid CharacterIDArray entry in {}, skipping one element.\n"), characterId.ToString());
                                    }
                                }
                                palRowJson["CharacterIDArray"] = normalizedCharacterIds;

                                if (!palRowJson.contains("CharacterNum"))
                                {
                                    palRowJson["CharacterNum"] = static_cast<int32_t>(normalizedCharacterIds.size());
                                }
                                if (!palRowJson.contains("MaxLostPalNum") && palRowJson.contains("CharacterNum"))
                                {
                                    palRowJson["MaxLostPalNum"] = palRowJson.at("CharacterNum");
                                }
                            }
                        }

                        if (!palRowJson.empty())
                        {
                            auto palRowName = FName(RC::to_generic_string(palShopTableIdRaw), FNAME_Add);
                            auto rowStruct = m_palShopCreateDataTable->GetRowStruct().Get();
                            auto existingPalRow = m_palShopCreateDataTable->FindRowUnchecked(palRowName);

                            if (existingPalRow)
                            {
                                try
                                {
                                    for (FProperty* property : TFieldRange<FProperty>(rowStruct, EFieldIterationFlags::IncludeSuper))
                                    {
                                        const auto propertyName = RC::to_string(property->GetName());
                                        if (palRowJson.contains(propertyName))
                                        {
                                            PropertyHelper::CopyJsonValueToContainer(existingPalRow, property, palRowJson.at(propertyName));
                                        }
                                    }
                                    if (!m_palShopCreateDataTable->FindRowUnchecked(palRowName)) addSucceeded = false;
                                }
                                catch (const std::exception& e)
                                {
                                    PS::Log<RC::LogLevel::Error>(STR("Failed to modify Pal shop Row '{}' in {}: {}\n"), palRowName.ToString(), m_palShopCreateDataTable->GetFullName(), RC::to_generic_string(e.what()));
                                    addSucceeded = false;
                                }
                            }
                            else
                            {
                                FManagedStruct palRowData{ rowStruct };
                                try
                                {
                                    for (FProperty* property : TFieldRange<FProperty>(rowStruct, EFieldIterationFlags::IncludeSuper))
                                    {
                                        const auto propertyName = RC::to_string(property->GetName());
                                        if (palRowJson.contains(propertyName))
                                        {
                                            PropertyHelper::CopyJsonValueToContainer(palRowData.GetData(), property, palRowJson.at(propertyName));
                                        }
                                    }

                                    m_palShopCreateDataTable->AddRow(palRowName, *reinterpret_cast<RC::Unreal::FTableRowBase*>(palRowData.GetData()));
                                    if (!m_palShopCreateDataTable->FindRowUnchecked(palRowName)) addSucceeded = false;
                                }
                                catch (const std::exception& e)
                                {
                                    PS::Log<RC::LogLevel::Error>(STR("Failed to add Pal shop Row '{}' to {}: {}\n"), palRowName.ToString(), m_palShopCreateDataTable->GetFullName(), RC::to_generic_string(e.what()));
                                    addSucceeded = false;
                                }
                            }
                        }
                        else if (palEntry->contains("CharacterIDArray") || palEntry->contains("CharacterNum") || palEntry->contains("MinCharacterLevel") || palEntry->contains("MaxCharacterLevel") || palEntry->contains("MaxLostPalNum"))
                        {
                            PS::Log<RC::LogLevel::Warning>(STR("No valid pal shop fields were provided in PalShop for {}, skipping pal shop row creation.\n"), characterId.ToString());
                        }
                }
            }
        }

        if (addSucceeded)
        {
            PS::Log<RC::LogLevel::Verbose>(STR("Added vender data for {} (ItemShop: {}, PalShop: {})\n"), characterId.ToString(), processedItemShop ? STR("yes") : STR("no"), processedPalShop ? STR("yes") : STR("no"));
        }
        else
        {
            PS::Log<RC::LogLevel::Error>(STR("Failed to fully add vender data for {} (ItemShop: {}, PalShop: {})\n"), characterId.ToString(), processedItemShop ? STR("yes") : STR("no"), processedPalShop ? STR("yes") : STR("no"));
        }
    }
}
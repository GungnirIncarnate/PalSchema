#pragma once

#include "Unreal/NameTypes.hpp"

#include <map>
#include <vector>
#include <variant>

namespace PS
{
    struct MerchantShopProfile
    {
        bool Enabled = false;
        int RestockTime = 0;
        RC::Unreal::FName TableName = RC::Unreal::NAME_None;
    };

    struct MerchantProfile
    {
        bool Enabled = false;
        MerchantShopProfile ItemShop;
        MerchantShopProfile PalShop;
    };

    enum class MonoNpcProfileComponentType : RC::Unreal::uint8
    {
        Merchant
    };

    using MonoNpcComponentPayload = std::variant<MerchantProfile>;

    struct MonoNpcComponentProfileState
    {
        std::vector<MonoNpcProfileComponentType> RequiredComponents;
        std::map<MonoNpcProfileComponentType, MonoNpcComponentPayload> Payloads;
    };
}
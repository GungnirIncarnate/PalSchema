#include <Loader/Spawner/Components/MonoNpcMerchantComponentHandler.h>

#include <SDK/Helper/PropertyHelper.h>
#include <SDK/Classes/Custom/UObjectGlobals.h>
#include <Utility/Logging.h>
#include <Utility/JsonHelpers.h>
#include <Unreal/Core/Containers/StringConv.hpp>
#include <Unreal/CoreUObject/UObject/Class.hpp>
#include <Unreal/AActor.hpp>
#include <Unreal/Transform.hpp>

namespace
{
    constexpr const char* kMerchantComponentPath = "/Game/Pal/Blueprint/Component/VenderData/BP_PalShopVenderDataComponent.BP_PalShopVenderDataComponent_C";
}

namespace PS
{
    void MonoNpcMerchantComponentHandler::ParseConfig(const nlohmann::json& merchantNode, PS::MerchantProfile& outProfile) const
    {
        if (!merchantNode.is_object())
        {
            throw std::runtime_error("Merchant must be an object.");
        }

        auto parseMerchantShop = [](const nlohmann::json& shopNode, const char* fieldName, PS::MerchantShopProfile& outProfile)
        {
            if (!PS::JsonHelpers::FieldExists(shopNode, fieldName))
            {
                return;
            }

            auto& merchantShop = shopNode.at(fieldName);
            if (!merchantShop.is_object())
            {
                throw std::runtime_error(std::format("Merchant.{} must be an object.", fieldName));
            }

            outProfile.Enabled = true;
            PS::JsonHelpers::ValidateFieldExists(merchantShop, "RestockTime");
            PS::JsonHelpers::ValidateFieldExists(merchantShop, "TableName");
            PS::JsonHelpers::ParseInteger(merchantShop, "RestockTime", outProfile.RestockTime);
            PS::JsonHelpers::ParseFName(merchantShop, "TableName", outProfile.TableName);
        };

        outProfile.Enabled = true;
        parseMerchantShop(merchantNode, "ItemShop", outProfile.ItemShop);
        parseMerchantShop(merchantNode, "PalShop", outProfile.PalShop);

        if (!outProfile.ItemShop.Enabled && !outProfile.PalShop.Enabled)
        {
            throw std::runtime_error("Merchant requires at least one configured shop: ItemShop and/or PalShop.");
        }
    }

    RC::Unreal::UClass* MonoNpcMerchantComponentHandler::ResolveComponentClass() const
    {
        const auto componentPath = RC::to_wstring(kMerchantComponentPath);
        auto* componentClass = UECustom::UObjectGlobals::StaticFindObject<RC::Unreal::UClass*>(nullptr, nullptr, componentPath.c_str(), false);

        if (!componentClass)
        {
            PS::Log<RC::LogLevel::Error>(STR("Failed to resolve merchant component class from {}\n"), RC::to_generic_string(kMerchantComponentPath));
        }

        return componentClass;
    }

    RC::Unreal::UObject* MonoNpcMerchantComponentHandler::FindOrAddComponent(RC::Unreal::AActor* resolvedNpc, RC::Unreal::UClass* componentClass) const
    {
        if (!resolvedNpc || !componentClass)
        {
            return nullptr;
        }

        auto existingComponents = resolvedNpc->K2_GetComponentsByClass(componentClass);
        if (existingComponents.Num() > 0)
        {
            return existingComponents.GetData()[0];
        }

        auto* addComponentFunction = resolvedNpc->GetFunctionByNameInChain(TEXT("AddComponentByClass"));
        if (!addComponentFunction)
        {
            return nullptr;
        }

        auto* finishAddComponentFunction = resolvedNpc->GetFunctionByNameInChain(TEXT("FinishAddComponent"));

        struct
        {
            RC::Unreal::UClass* ComponentClass = nullptr;
            bool bManualAttachment = false;
            RC::Unreal::FTransform RelativeTransform;
            bool bDeferredFinish = false;
            RC::Unreal::UObject* ReturnValue = nullptr;
        } params{};

        params.ComponentClass = componentClass;
        params.bManualAttachment = false;
        params.RelativeTransform = RC::Unreal::FTransform(
            RC::Unreal::FRotator{ 0.0, 0.0, 0.0 },
            RC::Unreal::FVector{ 0.0, 0.0, 0.0 },
            RC::Unreal::FVector{ 1.0, 1.0, 1.0 });
        params.bDeferredFinish = finishAddComponentFunction != nullptr;

        resolvedNpc->ProcessEvent(addComponentFunction, &params);

        if (params.ReturnValue && finishAddComponentFunction)
        {
            struct
            {
                RC::Unreal::UObject* Component = nullptr;
                bool bManualAttachment = false;
                RC::Unreal::FTransform RelativeTransform;
            } finishParams{};

            finishParams.Component = params.ReturnValue;
            finishParams.RelativeTransform = params.RelativeTransform;
            resolvedNpc->ProcessEvent(finishAddComponentFunction, &finishParams);
        }

        return params.ReturnValue;
    }

    void MonoNpcMerchantComponentHandler::ApplyMerchantProperties(RC::Unreal::UObject* componentInstance, const PS::MerchantProfile& merchantProfile) const
    {
        if (!componentInstance)
        {
            return;
        }

        auto applyProperty = [componentInstance](const RC::StringType& propertyName, const nlohmann::json& propertyValue)
        {
            auto* property = Palworld::PropertyHelper::GetPropertyByName(componentInstance->GetClassPrivate(), propertyName);
            if (!property)
            {
                PS::Log<RC::LogLevel::Warning>(STR("Failed to set merchant property {} on component {} because the property was not found.\n"),
                    propertyName, componentInstance->GetName());
                return;
            }

            Palworld::PropertyHelper::CopyJsonValueToContainer(componentInstance, property, propertyValue);
        };

        if (merchantProfile.ItemShop.Enabled)
        {
            applyProperty(STR("ItemShopRestockMinute"), nlohmann::json(merchantProfile.ItemShop.RestockTime));
            applyProperty(STR("itemShopLotteryType"), nlohmann::json("EPalShopLotteryType::SimpleLottery"));
            applyProperty(STR("itemShopSimpleLotteryTableName"), nlohmann::json{{"Key", RC::to_string(merchantProfile.ItemShop.TableName.ToString())}});
        }

        if (merchantProfile.PalShop.Enabled)
        {
            applyProperty(STR("PalShopRestockMinute"), nlohmann::json(merchantProfile.PalShop.RestockTime));
            applyProperty(STR("palShopLotteryType"), nlohmann::json("EPalShopLotteryType::SimpleLottery"));
            applyProperty(STR("palShopSimpleLotteryTableName"), nlohmann::json{{"Key", RC::to_string(merchantProfile.PalShop.TableName.ToString())}});
        }
    }

    void MonoNpcMerchantComponentHandler::Apply(RC::Unreal::AActor* resolvedNpc, const PS::MerchantProfile& merchantProfile) const
    {
        if (!resolvedNpc || !merchantProfile.Enabled)
        {
            return;
        }

        auto* componentClass = ResolveComponentClass();
        auto* componentInstance = FindOrAddComponent(resolvedNpc, componentClass);
        if (!componentInstance)
        {
            PS::Log<RC::LogLevel::Error>(STR("Failed to add or find merchant component for resolved actor.\n"));
            return;
        }

        ApplyMerchantProperties(componentInstance, merchantProfile);
    }
}

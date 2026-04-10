#pragma once

#include "Loader/TalkFlow/FlowNodeCatalog.h"
#include "String/StringType.hpp"

namespace RC::Unreal
{
    class UClass;
    class UObject;
}

namespace Palworld::TalkFlow
{
    class FlowNodeCatalogBuilder
    {
    public:
        static FlowNodeCatalog Build(
            RC::Unreal::UClass* flowNodeClass,
            RC::Unreal::UObject* flowAsset,
            const RC::StringType& assetPath);
    };
}

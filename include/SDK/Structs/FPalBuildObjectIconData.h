#pragma once

#include "SDK/Classes/UDataTable.h"
#include "SDK/Classes/TSoftObjectPtr.h"
#include "SDK/Classes/Texture2D.h"
#include "SDK/Structs/FTableRowBase.h"

namespace Palworld {
    struct FPalBuildObjectIconData : public UECustom::FTableRowBase
    {
        FPalCharacterIconDataRow(const RC::StringType& Path) : SoftIcon(UECustom::TSoftObjectPtr<UECustom::UTexture2D>(UECustom::FSoftObjectPath(RC::Unreal::FString(Path.c_str()))))
        {
        }

        UECustom::TSoftObjectPtr<UECustom::UTexture2D> SoftIcon;
    };
}